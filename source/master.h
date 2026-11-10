// The master chain that follows the bass layers. Everything here is our own DSP.
//
//   Duck  -> ducks only the low band from a sidechain input, on a smooth curve.
//   Width -> mid/side width, plus mono below a chosen frequency.
//   Squash-> three-band compression, downward and upward, with a level per band. All at zero is a no-op.
//   Tone  -> low and high shelves, then a five-band parametric EQ.
//   Punch -> transient emphasis, stereo linked.
//   Clip  -> heat and clipping at 4x oversampling, optionally keeping the lows clean.
//   Limit -> lookahead limiter driven by true-peak readings.
//   Meter -> peak, gain reduction, BS.1770 loudness, true peak, correlation.
#pragma once

#include "dsp_core.h"

#include <atomic>
#include <cstdint>

namespace loopsmaster {

// Knob ranges for the EQ, shared by the plugin and the window.
inline double eqHzFromNorm (double n) { return 20.0 * std::pow (1000.0, n); }
inline double eqHzToNorm (double hz) { return std::log (hz / 20.0) / std::log (1000.0); }
inline double eqGainFromNorm (double n) { return -18.0 + 36.0 * n; }
inline double eqGainToNorm (double db) { return (db + 18.0) / 36.0; }
inline double eqQFromNorm (double n) { return 0.2 * std::pow (50.0, n); }
inline double eqQToNorm (double q) { return std::log (q / 0.2) / std::log (50.0); }
inline int eqTypeFromNorm (double n) { return static_cast<int> (n * 8.0 + 0.5); }

enum EqType { kEqBell, kEqLowShelf, kEqHighShelf, kEqLowCut, kEqHighCut, kEqNotch, kEqBandPass, kEqTilt, kEqAllPass };
enum EqPlace { kPlaceLeft, kPlaceRight, kPlaceStereo, kPlaceMid, kPlaceSide };

// Shapes with no gain of their own.
inline bool eqIsGainless (int type) { return type == kEqLowCut || type == kEqHighCut || type == kEqNotch || type == kEqBandPass || type == kEqAllPass; }
inline bool eqBandActive (int type, double gainDb) { return eqIsGainless (type) || gainDb != 0.0; }

// A displayed Q of 1 is this filter Q. It makes a Q 1 cut Butterworth and widens bells and notches to suit.
constexpr double kEqQScale = 0.70710678;

inline void setEqFilter (Biquad& f, int type, double hz, double gainDb, double shownQ, double sampleRate)
{
	const double q = shownQ * kEqQScale;
	switch (type)
	{
		case kEqLowShelf: f.setLowShelf (hz, gainDb, sampleRate); break;
		case kEqHighShelf: f.setHighShelf (hz, gainDb, sampleRate); break;
		case kEqLowCut: f.setHighPass (hz, q, sampleRate); break;
		case kEqHighCut: f.setLowPass (hz, q, sampleRate); break;
		case kEqNotch: f.setNotch (hz, q, sampleRate); break;
		case kEqBandPass: f.setBandPass (hz, q, sampleRate); break;
		case kEqTilt: f.setTilt (hz, gainDb, sampleRate); break;
		case kEqAllPass: f.setAllPass (hz, q, sampleRate); break;
		default: f.setPeak (hz, gainDb, q, sampleRate); break;
	}
}

struct EqBandSettings {
	int type = kEqBell;
	double hz = 1000.0, gainDb = 0.0, q = 1.0;
	int place = kPlaceStereo, slope = 0; // slope: 0 = 12, 1 = 24, 2 = 48 dB/oct, cuts only
	bool operator!= (const EqBandSettings& o) const { return type != o.type || hz != o.hz || gainDb != o.gainDb || q != o.q || slope != o.slope; }
};

// Cuts steeper than 12 dB/oct are stacked Butterworth stages, with Q on the last stage only.
constexpr int kEqMaxStages = 4;
inline int setEqBand (Biquad* stages, const EqBandSettings& band, double sampleRate)
{
	if ((band.type != kEqLowCut && band.type != kEqHighCut) || band.slope == 0)
	{
		setEqFilter (stages[0], band.type, band.hz, band.gainDb, band.q, sampleRate);
		return 1;
	}
	static const double four[2] = {0.54119610, 1.30656296};
	static const double eight[4] = {0.50979558, 0.60134489, 0.89997622, 2.56291545};
	const int count = band.slope == 1 ? 2 : 4;
	for (int i = 0; i < count; ++i)
	{
		const double butterworth = count == 2 ? four[i] : eight[i];
		const double q = i == count - 1 ? butterworth * band.q : butterworth;
		// setEqFilter scales Q, so undo that for these exact values.
		setEqFilter (stages[i], band.type, band.hz, 0.0, q / kEqQScale, sampleRate);
	}
	return count;
}

// How the limiter recovers. Hold and time per style were fitted to measured recovery curves.
enum LimitStyle { kStyleManual, kStyleClean, kStyleHard, kStyleModern, kStyleSafe };

struct MasterSettings {
	double bandThresholdDb[3] {0.0, 0.0, 0.0};
	int bandMode[3] {0, 0, 0}; // 0 both, 1 up only, 2 down only, 3 off
	int listen = 0;            // 0 all, 1 low, 2 mid, 3 high
	double clipCeilingDb = 0.0;
	int limitStyle = kStyleManual;
	bool limitUnity = false, limitAudition = false, dcFilter = false;
	int ditherBits = 0; // 0 off
	int duckSource = 0; // 0 = kick on the sidechain, 1 = half note, 2 = quarter, 3 = eighth
	double squashUp = 0.0, squashTime = 1.0, splitLowHz = 88.2818, splitHighHz = 2500.0;
	double bandDb[3] {0.0, 0.0, 0.0};
	EqBandSettings eq[5];
	double duckDepth = 0.0, duckHz = 200.0, duckThresholdDb = -28.0, duckReleaseMs = 40.0;
	double width = 1.0, monoBelowHz = 0.0; // 0 Hz = off
	double squash = 0.0;
	double lowDb = 0.0, highDb = 0.0;
	double punch = 0.0; // -1 to 1
	double heat = 0.0;
	bool clipOn = false;
	double clipDriveDb = 0.0, clipSoft = 0.25, cleanBelowHz = 0.0; // 0 Hz = off
	bool limitOn = false;
	double limitPushDb = 0.0, ceilingDb = -1.0, limitReleaseMs = 100.0;
};

// Written by the audio thread and read by the window without locks.
struct MeterData {
	std::atomic<float> inPeak {0.f}, outPeak {0.f}; // linear, read and cleared by the window
	std::atomic<float> duckDb {0.f}, clipDb {0.f}, limitDb {0.f};
	std::atomic<float> sidechainPeak {0.f};
	std::atomic<float> momentary {-100.f}, shortTerm {-100.f}, integrated {-100.f}; // LUFS, -100 = nothing yet
	std::atomic<float> truePeakDb {-100.f};
	std::atomic<float> correlation {0.f};
	std::atomic<float> latencyMs {0.f};
	std::atomic<float> sampleRate {44100.f};
	std::atomic<float> loudnessRange {-1.f}; // LU, below zero = not enough audio yet
	std::atomic<float> squashDb[3] {{0.f}, {0.f}, {0.f}};
	static constexpr uint32_t kSpectrumSize = 4096;
	std::atomic<float> spectrum[kSpectrumSize] {};
	std::atomic<uint32_t> spectrumWritten {0};
	std::atomic<bool> resetRequest {false};

	static void raise (std::atomic<float>& meter, float value)
	{
		if (value > meter.load (std::memory_order_relaxed))
			meter.store (value, std::memory_order_relaxed);
	}
};

// Estimates the peak between samples at 4x. Reports for the sample 6 steps back.
class TruePeak {
public:
	static constexpr int kDelay = 6;

	void prepare ()
	{
		kernel = makeKernel (kTaps, 0.125, 6.0);
		for (auto& v : kernel)
			v *= 4.0;
		history.prepare (kTaps / 4 + 1);
	}
	void reset () { history.reset (); }
	double process (double x)
	{
		history.push (x);
		const double* b = history.newest ();
		double peak = std::fabs (b[kDelay]);
		for (int phase = 1; phase < 4; ++phase)
		{
			double y = 0.0;
			for (int i = 0; phase + 4 * i < kTaps; ++i)
				y += kernel[static_cast<size_t> (phase + 4 * i)] * b[i];
			peak = std::max (peak, std::fabs (y));
		}
		return peak;
	}

private:
	static constexpr int kTaps = 49;
	std::vector<double> kernel;
	History history;
};

// ITU-R BS.1770 loudness with EBU momentary and short-term windows and two-stage gating.
class Loudness {
public:
	void prepare (double sr)
	{
		const double K = std::tan (Biquad::kPi * 1681.974450955533 / sr);
		const double Q = 0.7071752369554196;
		const double Vh = std::pow (10.0, 3.999843853973347 / 20.0);
		const double Vb = std::pow (Vh, 0.4996667741545416);
		const double a0 = 1.0 + K / Q + K * K;
		const double K2 = std::tan (Biquad::kPi * 38.13547087602444 / sr);
		const double Q2 = 0.5003270373238773;
		const double a02 = 1.0 + K2 / Q2 + K2 * K2;
		for (int c = 0; c < 2; ++c)
		{
			shelf[c].setRaw ((Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
			                 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
			rumble[c].setRaw (1.0, -2.0, 1.0, 2.0 * (K2 * K2 - 1.0) / a02, (1.0 - K2 / Q2 + K2 * K2) / a02);
		}
		chunkLength = std::max (1, static_cast<int> (0.1 * sr + 0.5));
		reset ();
	}
	void reset ()
	{
		for (int c = 0; c < 2; ++c)
		{
			shelf[c].reset ();
			rumble[c].reset ();
		}
		std::fill (std::begin (chunks), std::end (chunks), 0.0);
		chunkSum = 0.0;
		chunkCount = 0;
		chunksDone = 0;
		chunkAt = 0;
		resetIntegrated ();
	}
	void resetIntegrated ()
	{
		std::fill (std::begin (binCount), std::end (binCount), 0u);
		std::fill (std::begin (binEnergy), std::end (binEnergy), 0.0);
		std::fill (std::begin (rangeCount), std::end (rangeCount), 0u);
		std::fill (std::begin (rangeEnergy), std::end (rangeEnergy), 0.0);
	}
	void process (double l, double r, MeterData& m)
	{
		const double fl = rumble[0].process (shelf[0].process (l));
		const double fr = rumble[1].process (shelf[1].process (r));
		chunkSum += fl * fl + fr * fr;
		if (++chunkCount < chunkLength)
			return;
		chunks[chunkAt] = chunkSum / chunkLength;
		chunkAt = (chunkAt + 1) % kShortChunks;
		chunkSum = 0.0;
		chunkCount = 0;
		++chunksDone;

		const double momentary = windowEnergy (kMomentaryChunks);
		m.momentary.store (static_cast<float> (toLufs (momentary)), std::memory_order_relaxed);
		m.shortTerm.store (static_cast<float> (toLufs (windowEnergy (kShortChunks))), std::memory_order_relaxed);
		if (chunksDone >= kMomentaryChunks)
			addGatedBlock (momentary);
		m.integrated.store (static_cast<float> (integrated ()), std::memory_order_relaxed);
		if (chunksDone >= kShortChunks)
			addRangeBlock (windowEnergy (kShortChunks));
		m.loudnessRange.store (static_cast<float> (range ()), std::memory_order_relaxed);
	}

private:
	static constexpr int kMomentaryChunks = 4;
	static constexpr int kShortChunks = 30;
	static constexpr int kBins = 800; // 0.1 LU bins from -70 to +10

	static double toLufs (double energy) { return energy > 1e-12 ? -0.691 + 10.0 * std::log10 (energy) : -100.0; }

	double windowEnergy (int count) const
	{
		const int available = std::min (count, std::min (chunksDone, kShortChunks));
		if (available <= 0)
			return 0.0;
		double sum = 0.0;
		for (int i = 1; i <= available; ++i)
			sum += chunks[(chunkAt - i + kShortChunks) % kShortChunks];
		return sum / available;
	}
	void addGatedBlock (double energy)
	{
		const double lufs = toLufs (energy);
		if (lufs < -70.0)
			return;
		const int bin = std::min (kBins - 1, static_cast<int> ((lufs + 70.0) * 10.0));
		++binCount[bin];
		binEnergy[bin] += energy;
	}
	void addRangeBlock (double energy)
	{
		const double lufs = toLufs (energy);
		if (lufs < -70.0)
			return;
		const int bin = std::min (kBins - 1, static_cast<int> ((lufs + 70.0) * 10.0));
		++rangeCount[bin];
		rangeEnergy[bin] += energy;
	}
	// Loudness range per EBU Tech 3342: the spread of gated short-term loudness, 10th to 95th percentile.
	double range () const
	{
		double total = 0.0, blocks = 0.0;
		for (int i = 0; i < kBins; ++i)
		{
			total += rangeEnergy[i];
			blocks += rangeCount[i];
		}
		if (blocks < 1.0)
			return -1.0;
		const double gate = toLufs (total / blocks) - 20.0;
		double kept = 0.0;
		for (int i = 0; i < kBins; ++i)
			if (-70.0 + 0.1 * i >= gate)
				kept += rangeCount[i];
		double seen = 0.0, low = -100.0, high = -100.0;
		for (int i = 0; i < kBins; ++i)
		{
			if (-70.0 + 0.1 * i < gate || rangeCount[i] == 0)
				continue;
			seen += rangeCount[i];
			if (low <= -100.0 && seen >= 0.10 * kept)
				low = -70.0 + 0.1 * i;
			if (high <= -100.0 && seen >= 0.95 * kept)
				high = -70.0 + 0.1 * i;
		}
		return std::max (0.0, high - low);
	}
	double integrated () const
	{
		double total = 0.0;
		double blocks = 0.0;
		for (int i = 0; i < kBins; ++i)
		{
			total += binEnergy[i];
			blocks += binCount[i];
		}
		if (blocks <= 0.0)
			return -100.0;
		const double gate = toLufs (total / blocks) - 10.0;
		total = blocks = 0.0;
		for (int i = 0; i < kBins; ++i)
			if (-70.0 + 0.1 * i >= gate)
			{
				total += binEnergy[i];
				blocks += binCount[i];
			}
		return blocks > 0.0 ? toLufs (total / blocks) : -100.0;
	}

	Biquad shelf[2], rumble[2];
	double chunks[kShortChunks] {};
	double chunkSum = 0.0;
	int chunkLength = 4410, chunkCount = 0, chunksDone = 0, chunkAt = 0;
	uint32_t binCount[kBins] {};
	double binEnergy[kBins] {};
	uint32_t rangeCount[kBins] {};
	double rangeEnergy[kBins] {};
};

class MasterChain {
public:
	static constexpr int kOsTaps = 289;
	static constexpr int kOsDelay = 72; // base-rate samples across both 4x filters

	void prepare (double sr)
	{
		sampleRate = sr;
		for (Smoothed* s : {&duckDepth, &duckMix, &width, &monoMix, &squashAmount, &squashMix, &clipCeiling, &dcMix, &listenGain[0], &listenGain[1], &listenGain[2], &squashUp, &bandGain[0], &bandGain[1], &bandGain[2], &punch, &heat, &clipGain, &clipSoft, &clipMix, &cleanMix, &limitPush, &limitMix, &ceiling})
			s->setRate (sr);
		// Cutoff just under the base-rate Nyquist. The down filter's kernel doubles as the up filter's.
		kernel = makeKernel (kOsTaps, 0.5 * 0.465 / 2.0, 8.0);
		for (int c = 0; c < 2; ++c)
		{
			upHistory[c].prepare (kOsTaps / 4 + 1);
			downHistory[c].prepare (kOsTaps);
			clipDry[c].prepare (kOsDelay);
			plainDry[c].prepare (kOsDelay);
			peakReader[c].prepare ();
		}
		limitWindow = std::max (8, static_cast<int> (0.0015 * sr + 0.5));
		limitDelayLength = limitWindow + TruePeak::kDelay;
		for (int c = 0; c < 2; ++c)
		{
			limitDelay[c].prepare (limitDelayLength);
			plainLimitDelay[c].prepare (limitDelayLength);
			outReader[c].prepare ();
		}
		required.assign (static_cast<size_t> (limitWindow) + 1, 1.0);
		smoothing.assign (static_cast<size_t> (limitWindow) + 1, 1.0);
		loudness.prepare (sr);
		meterWindow = std::max (1, static_cast<int> (0.02 * sr));
		correlationWindow = std::max (1, static_cast<int> (0.1 * sr));
		duckAttackStep = 1.0 / std::max (1.0, 0.0035 * sr);
		duckHoldLength = static_cast<int> (0.012 * sr);
		duckEnvDecay = std::exp (-1.0 / (0.005 * sr));
		tempoSmooth = coefficient (0.002);
		punchFastAttack = coefficient (0.001);
		punchFastRelease = coefficient (0.04);
		punchSlowAttack = coefficient (0.03);
		punchSlowRelease = coefficient (0.15);
		const double atk[3] = {0.04785, 0.02236, 0.01347};
		for (int b = 0; b < 3; ++b)
		{
			squashAttack[b] = coefficient (atk[b]);
			squashRelease[b] = coefficient (0.28236);
			setUpTimes (b, 1.0);
		}
		reset ();
		have = false;
		update (current);
		jump ();
	}

	int latency () const { return kOsDelay + limitDelayLength; }

	void reset ()
	{
		for (int c = 0; c < 2; ++c)
		{
			for (auto& f : duckLow[c])
				f.reset ();
			for (auto& f : duckHigh[c])
				f.reset ();
			for (auto& f : lowSplit[c])
				f.reset ();
			for (auto& f : restSplit[c])
				f.reset ();
			for (auto& f : midSplit[c])
				f.reset ();
			for (auto& f : highSplit[c])
				f.reset ();
			for (auto& f : lowAllpassA[c])
				f.reset ();
			for (auto& f : lowAllpassB[c])
				f.reset ();
			for (auto& f : cleanFilter[c])
				f.reset ();
			for (auto& band : eqFilter[c])
				for (auto& f : band)
					f.reset ();
			dcFilter[c].reset ();
			lowShelf[c].reset ();
			highShelf[c].reset ();
			upHistory[c].reset ();
			downHistory[c].reset ();
			clipDry[c].reset ();
			plainDry[c].reset ();
			limitDelay[c].reset ();
			plainLimitDelay[c].reset ();
			peakReader[c].reset ();
			outReader[c].reset ();
		}
		for (auto& f : monoFilter)
			f.reset ();
		std::fill (required.begin (), required.end (), 1.0);
		std::fill (smoothing.begin (), smoothing.end (), 1.0);
		smoothingSum = static_cast<double> (smoothing.size ());
		limitEnv = 1.0;
		limitHoldLeft = 0;
		ditherState = 0x9E3779B9u;
		ringAt = 0;
		duckPhase = 0.0;
		tempoShape = 0.0;
		duckEnv = 0.0;
		duckHold = 0;
		punchFast = punchSlow = 0.0;
		std::fill (std::begin (squashEnv), std::end (squashEnv), 0.0);
		std::fill (std::begin (upEnv), std::end (upEnv), 0.0);
		loudness.reset ();
		truePeakMax = 0.0;
		limitLow = 1.0;
		preClipPeak = postClipPeak = 0.0;
		duckMost = 0.0;
		meterCount = 0;
		sumLL = sumRR = sumLR = 0.0;
		correlationCount = 0;
	}

	void update (const MasterSettings& s)
	{
		if (!have || s.duckHz != current.duckHz)
			for (int c = 0; c < 2; ++c)
				for (int i = 0; i < 2; ++i)
				{
					duckLow[c][i].setLowPass (s.duckHz, 0.70710678, sampleRate);
					duckHigh[c][i].setHighPass (s.duckHz, 0.70710678, sampleRate);
				}
		if (!have || s.splitLowHz != current.splitLowHz || s.splitHighHz != current.splitHighHz)
			for (int c = 0; c < 2; ++c)
				for (int i = 0; i < 2; ++i)
				{
					lowSplit[c][i].setLowPass (s.splitLowHz, 0.70710678, sampleRate);
					restSplit[c][i].setHighPass (s.splitLowHz, 0.70710678, sampleRate);
					midSplit[c][i].setLowPass (s.splitHighHz, 0.70710678, sampleRate);
					highSplit[c][i].setHighPass (s.splitHighHz, 0.70710678, sampleRate);
					lowAllpassA[c][i].setLowPass (s.splitHighHz, 0.70710678, sampleRate);
					lowAllpassB[c][i].setHighPass (s.splitHighHz, 0.70710678, sampleRate);
				}
		if (!have || s.squashTime != current.squashTime)
		{
			const double atk[3] = {0.04785, 0.02236, 0.01347};
			for (int b = 0; b < 3; ++b)
			{
				squashAttack[b] = coefficient (atk[b] * s.squashTime);
				squashRelease[b] = coefficient (0.28236 * s.squashTime);
				setUpTimes (b, s.squashTime);
			}
		}
		for (int b = 0; b < 5; ++b)
		{
			if (!have || s.eq[b] != current.eq[b])
				for (auto& channel : eqFilter)
					eqCount[b] = setEqBand (channel[b], s.eq[b], sampleRate);
			eqOn[b] = eqBandActive (s.eq[b].type, s.eq[b].gainDb);
		}
		if (!have || s.monoBelowHz != current.monoBelowHz)
		{
			if (s.monoBelowHz > 0.0)
			{
				monoFilter[0].setHighPass (s.monoBelowHz, 0.54119610, sampleRate);
				monoFilter[1].setHighPass (s.monoBelowHz, 1.30656296, sampleRate);
			}
		}
		if (!have || s.lowDb != current.lowDb)
			for (auto& f : lowShelf)
				f.setLowShelf (80.0, s.lowDb, sampleRate);
		if (!have || s.highDb != current.highDb)
			for (auto& f : highShelf)
				f.setHighShelf (8000.0, s.highDb, sampleRate);
		if ((!have || s.cleanBelowHz != current.cleanBelowHz) && s.cleanBelowHz > 0.0)
			for (auto& channel : cleanFilter)
			{
				channel[0].setHighPass (s.cleanBelowHz, 0.54119610, sampleRate);
				channel[1].setHighPass (s.cleanBelowHz, 1.30656296, sampleRate);
			}
		duckDepth.setTarget (s.duckDepth);
		duckMix.setTarget (s.duckDepth > 0.0 ? 1.0 : 0.0);
		width.setTarget (s.width);
		monoMix.setTarget (s.monoBelowHz > 0.0 ? 1.0 : 0.0);
		squashAmount.setTarget (s.squash);
		squashMix.setTarget (squashNeeded (s) ? 1.0 : 0.0);
		squashUp.setTarget (s.squashUp);
		for (int b = 0; b < 3; ++b)
			bandGain[b].setTarget (std::pow (10.0, s.bandDb[b] / 20.0));
		punch.setTarget (s.punch);
		heat.setTarget (s.heat);
		clipGain.setTarget (std::pow (10.0, s.clipDriveDb / 20.0));
		clipSoft.setTarget (s.clipSoft);
		clipMix.setTarget (s.clipOn ? 1.0 : 0.0);
		cleanMix.setTarget (s.cleanBelowHz > 0.0 ? 1.0 : 0.0);
		limitPush.setTarget (std::pow (10.0, s.limitPushDb / 20.0));
		limitMix.setTarget (s.limitOn ? 1.0 : 0.0);
		ceiling.setTarget (std::pow (10.0, s.ceilingDb / 20.0));
		duckReleaseStep = 1.0 / std::max (1.0, 0.001 * s.duckReleaseMs * sampleRate);
		duckThreshold = std::pow (10.0, s.duckThresholdDb / 20.0);
		static const double styleTime[5] = {0.0, 0.20, 0.27, 0.43, 0.65};
		static const double styleHold[5] = {0.0, 0.0, 0.10, 0.0, 0.0};
		limitRelease = coefficient (s.limitStyle == kStyleManual ? 0.001 * s.limitReleaseMs : styleTime[s.limitStyle]);
		limitHoldLength = static_cast<int> (styleHold[s.limitStyle] * sampleRate);
		for (int c = 0; c < 2; ++c)
			if (!have)
				dcFilter[c].set (5.0, sampleRate);
		clipCeiling.setTarget (std::pow (10.0, s.clipCeilingDb / 20.0));
		dcMix.setTarget (s.dcFilter ? 1.0 : 0.0);
		for (int b = 0; b < 3; ++b)
			listenGain[b].setTarget (s.listen == 0 || s.listen == b + 1 ? 1.0 : 0.0);
		ditherLsb = s.ditherBits > 0 ? std::pow (2.0, 1 - s.ditherBits) : 0.0;
		lowOn = s.lowDb != 0.0;
		highOn = s.highDb != 0.0;
		current = s;
		have = true;
	}

	void jump ()
	{
		const MasterSettings& s = current;
		clipCeiling.jump (std::pow (10.0, s.clipCeilingDb / 20.0));
		dcMix.jump (s.dcFilter ? 1.0 : 0.0);
		for (int b = 0; b < 3; ++b)
			listenGain[b].jump (s.listen == 0 || s.listen == b + 1 ? 1.0 : 0.0);
		duckDepth.jump (s.duckDepth);
		duckMix.jump (s.duckDepth > 0.0 ? 1.0 : 0.0);
		width.jump (s.width);
		monoMix.jump (s.monoBelowHz > 0.0 ? 1.0 : 0.0);
		squashAmount.jump (s.squash);
		squashMix.jump (squashNeeded (s) ? 1.0 : 0.0);
		squashUp.jump (s.squashUp);
		for (int b = 0; b < 3; ++b)
			bandGain[b].jump (std::pow (10.0, s.bandDb[b] / 20.0));
		punch.jump (s.punch);
		heat.jump (s.heat);
		clipGain.jump (std::pow (10.0, s.clipDriveDb / 20.0));
		clipSoft.jump (s.clipSoft);
		clipMix.jump (s.clipOn ? 1.0 : 0.0);
		cleanMix.jump (s.cleanBelowHz > 0.0 ? 1.0 : 0.0);
		limitPush.jump (std::pow (10.0, s.limitPushDb / 20.0));
		limitMix.jump (s.limitOn ? 1.0 : 0.0);
		ceiling.jump (std::pow (10.0, s.ceilingDb / 20.0));
	}

	// Runs one stereo frame. Output is delayed by latency () samples.
	void process (double& l, double& r, double sidechain, MeterData& m)
	{
		processDuck (l, r, sidechain);
		processWidth (l, r);
		processSquash (l, r);
		if (lowOn)
		{
			l = lowShelf[0].process (l);
			r = lowShelf[1].process (r);
		}
		if (highOn)
		{
			l = highShelf[0].process (l);
			r = highShelf[1].process (r);
		}
		for (int b = 0; b < 5; ++b)
			if (eqOn[b])
				processEqBand (b, l, r);
		processPunch (l, r);
		processClip (l, r);
		processDc (l, r);
		processLimit (l, r);
		if (ditherLsb > 0.0)
		{
			l += ditherLsb * (noise () - noise ());
			r += ditherLsb * (noise () - noise ());
		}
	}

	// Where the host's timeline is at the start of a block.
	void setTransport (double beats, double tempo, bool playing)
	{
		beatPos = beats;
		beatStep = tempo / 60.0 / sampleRate;
		transportOn = playing && tempo > 0.0;
	}

	Loudness& loudnessMeter () { return loudness; }
	void resetPeaks () { truePeakMax = 0.0; }

	// Reads what actually leaves the plugin. While bypassed nothing is being taken away,
	// so the reduction meters rest at zero even though the chain keeps running underneath.
	void meter (double l, double r, MeterData& m, bool bypassed)
	{
		if (m.resetRequest.exchange (false, std::memory_order_relaxed))
		{
			loudness.resetIntegrated ();
			truePeakMax = 0.0;
		}
		loudness.process (l, r, m);
		const uint32_t written = m.spectrumWritten.load (std::memory_order_relaxed);
		m.spectrum[written & (MeterData::kSpectrumSize - 1)].store (static_cast<float> (0.5 * (l + r)), std::memory_order_relaxed);
		m.spectrumWritten.store (written + 1, std::memory_order_relaxed);
		truePeakMax = std::max ({truePeakMax, outReader[0].process (l), outReader[1].process (r)});

		sumLL += l * l;
		sumRR += r * r;
		sumLR += l * r;
		if (++correlationCount >= correlationWindow)
		{
			const double denominator = std::sqrt (sumLL * sumRR);
			// During silence, keep the last reading rather than dropping to a false zero.
			if (denominator > 1e-9)
				m.correlation.store (static_cast<float> (sumLR / denominator), std::memory_order_relaxed);
			sumLL = sumRR = sumLR = 0.0;
			correlationCount = 0;
		}
		if (++meterCount >= meterWindow)
		{
			meterCount = 0;
			m.truePeakDb.store (truePeakMax > 1e-6 ? static_cast<float> (20.0 * std::log10 (truePeakMax)) : -100.f, std::memory_order_relaxed);
			if (!bypassed)
			{
				const double clipDb = preClipPeak > 1e-6 && postClipPeak > 1e-6 && clipMix.get () > 0.5 ? std::max (0.0, 20.0 * std::log10 (preClipPeak / postClipPeak)) : 0.0;
				MeterData::raise (m.clipDb, static_cast<float> (clipDb));
				MeterData::raise (m.limitDb, static_cast<float> (-20.0 * std::log10 (std::max (limitLow, 1e-6))));
				MeterData::raise (m.duckDb, static_cast<float> (-20.0 * std::log10 (std::max (1.0 - duckMost, 1e-3))));
			}
			preClipPeak = postClipPeak = 0.0;
			limitLow = 1.0;
			duckMost = 0.0;
			for (int b = 0; b < 3; ++b)
				m.squashDb[b].store (!bypassed && squashMix.get () > 0.5 ? static_cast<float> (squashNet[b]) : 0.f, std::memory_order_relaxed);
		}
	}

private:
	static bool squashNeeded (const MasterSettings& s)
	{
		return s.squash > 0.0 || s.squashUp > 0.0 || s.bandDb[0] != 0.0 || s.bandDb[1] != 0.0 || s.bandDb[2] != 0.0 || s.listen != 0;
	}

	double runEqStages (int channel, int band, double x)
	{
		for (int i = 0; i < eqCount[band]; ++i)
			x = eqFilter[channel][band][i].process (x);
		return x;
	}

	void processEqBand (int band, double& l, double& r)
	{
		switch (current.eq[band].place)
		{
			case kPlaceLeft: l = runEqStages (0, band, l); break;
			case kPlaceRight: r = runEqStages (1, band, r); break;
			case kPlaceMid:
			case kPlaceSide:
			{
				double mid = 0.5 * (l + r), side = 0.5 * (l - r);
				if (current.eq[band].place == kPlaceMid)
					mid = runEqStages (0, band, mid);
				else
					side = runEqStages (0, band, side);
				l = mid + side;
				r = mid - side;
				break;
			}
			default:
				l = runEqStages (0, band, l);
				r = runEqStages (1, band, r);
				break;
		}
	}

	void processDc (double& l, double& r)
	{
		const double mix = dcMix.next ();
		const double lowL = dcFilter[0].process (l);
		const double lowR = dcFilter[1].process (r);
		l -= mix * lowL;
		r -= mix * lowR;
	}

	// Uniform noise from -0.5 to 0.5. Two of these subtracted make TPDF dither.
	double noise ()
	{
		ditherState ^= ditherState << 13;
		ditherState ^= ditherState >> 17;
		ditherState ^= ditherState << 5;
		return static_cast<double> (ditherState) / 4294967296.0 - 0.5;
	}

	// The beat-locked duck: straight down on the beat, a short hold, then a smooth return over Release.
	double tempoDuckShape ()
	{
		double target = 0.0;
		if (transportOn)
		{
			beatPos += beatStep;
			const double length = current.duckSource == 1 ? 2.0 : current.duckSource == 2 ? 1.0 : 0.5;
			double into = std::fmod (beatPos, length);
			if (into < 0.0)
				into += length;
			const double seconds = into / (beatStep * sampleRate);
			const double hold = 0.03;
			const double back = 0.001 * current.duckReleaseMs;
			target = seconds < hold ? 1.0 : seconds < hold + back ? 0.5 + 0.5 * std::cos (Biquad::kPi * (seconds - hold) / back) : 0.0;
		}
		tempoShape += tempoSmooth * (target - tempoShape);
		return tempoShape;
	}
	static constexpr double kPunchDeadBandDb = 3.0;

	double coefficient (double seconds) const { return 1.0 - std::exp (-1.0 / (std::max (seconds, 1e-5) * sampleRate)); }

	void processDuck (double& l, double& r, double sidechain)
	{
		// Reading the input as a level with a short decay stops a low kick's own zero crossings
		// from reopening the gate mid-cycle.
		duckEnv = std::max (sidechain, duckEnv * duckEnvDecay);
		if (duckEnv >= duckThreshold)
			duckHold = duckHoldLength;
		else if (duckHold > 0)
			--duckHold;
		if (duckHold > 0)
			duckPhase = std::min (1.0, duckPhase + duckAttackStep);
		else
			duckPhase = std::max (0.0, duckPhase - duckReleaseStep);

		const double depth = duckDepth.next ();
		const double mix = duckMix.next ();
		if (mix < 1e-7 && duckMix.settledAt (0.0))
			return;
		// A raised cosine has no sharp corners, which is what stops the duck clicking.
		const double shaped = current.duckSource == 0 ? 0.5 - 0.5 * std::cos (Biquad::kPi * duckPhase) : tempoDuckShape ();
		const double gain = 1.0 - depth * shaped;
		duckMost = std::max (duckMost, 1.0 - gain);
		const double lowL = duckLow[0][1].process (duckLow[0][0].process (l));
		const double lowR = duckLow[1][1].process (duckLow[1][0].process (r));
		const double highL = duckHigh[0][1].process (duckHigh[0][0].process (l));
		const double highR = duckHigh[1][1].process (duckHigh[1][0].process (r));
		l += mix * (lowL * gain + highL - l);
		r += mix * (lowR * gain + highR - r);
	}

	void processWidth (double& l, double& r)
	{
		const double w = width.next ();
		const double mono = monoMix.next ();
		const double mid = 0.5 * (l + r);
		double side = 0.5 * (l - r);
		if (mono > 1e-7 || !monoMix.settledAt (0.0))
		{
			const double high = monoFilter[1].process (monoFilter[0].process (side));
			side += mono * (high - side);
		}
		side *= w;
		l = mid + side;
		r = mid - side;
	}

	void processSquash (double& l, double& r)
	{
		const double mix = squashMix.next ();
		const double amount = squashAmount.next ();
		const double up = squashUp.next ();
		const double trims[3] = {bandGain[0].next (), bandGain[1].next (), bandGain[2].next ()};
		if (mix < 1e-7 && squashMix.settledAt (0.0))
			return;
		double band[2][3];
		for (int c = 0; c < 2; ++c)
		{
			const double x = c == 0 ? l : r;
			double low = lowSplit[c][1].process (lowSplit[c][0].process (x));
			const double rest = restSplit[c][1].process (restSplit[c][0].process (x));
			// The low band gets the same phase turn as the upper split, so the bands still sum flat.
			low = lowAllpassA[c][1].process (lowAllpassA[c][0].process (low)) + lowAllpassB[c][1].process (lowAllpassB[c][0].process (low));
			band[c][0] = low;
			band[c][1] = midSplit[c][1].process (midSplit[c][0].process (rest));
			band[c][2] = highSplit[c][1].process (highSplit[c][0].process (rest));
		}
		double wetL = 0.0, wetR = 0.0;
		// Thresholds and timing as set on the KOTOMI project's downward multiband; the reference
		// levels are ours, so a band at its usual level stays put and Amount does not change loudness much.
		static const double threshold[3] = {-38.95, -35.45, -40.7};
		static const double reference[3] = {-18.0, -20.0, -28.0};
		const double slope = 0.985;
		for (int b = 0; b < 3; ++b)
		{
			const double offset = current.bandThresholdDb[b];
			const double bandThreshold = threshold[b] + offset, bandReference = reference[b] + offset;
			const bool upOn = current.bandMode[b] == 0 || current.bandMode[b] == 1;
			const bool downOn = current.bandMode[b] == 0 || current.bandMode[b] == 2;
			const double power = 0.5 * (band[0][b] * band[0][b] + band[1][b] * band[1][b]);
			squashEnv[b] += (power > squashEnv[b] ? squashAttack[b] : squashRelease[b]) * (power - squashEnv[b]);
			const double level = 10.0 * std::log10 (std::max (squashEnv[b], 1e-12));
			const double over = softOver (level - bandThreshold);
			// Upward: lift whatever falls below the band's usual level, so tails and gaps in a dense
			// mix come up too, but never the noise floor. It has its own detector, because the one
			// above falls about 15 dB a second, which is slower than a hit decays.
			upEnv[b] += (power > upEnv[b] ? upAttack[b] : upRelease[b]) * (power - upEnv[b]);
			const double upLevel = 10.0 * std::log10 (std::max (upEnv[b], 1e-12));
			const double under = std::min (std::max (0.0, bandReference - upLevel), 24.0);
			const double awake = std::clamp ((upLevel + 70.0) / 10.0, 0.0, 1.0);
			const double net = std::clamp ((downOn ? amount * slope * ((bandReference - bandThreshold) - over) : 0.0) + (upOn ? up * 0.5 * under * awake : 0.0), -30.0, 18.0);
			squashNet[b] = net;
			const double g = std::pow (10.0, net / 20.0) * trims[b] * listenGain[b].next ();
			wetL += g * band[0][b];
			wetR += g * band[1][b];
		}
		l += mix * (wetL - l);
		r += mix * (wetR - r);
	}

	// Slower in the low band, so the gain doesn't follow individual bass cycles.
	void setUpTimes (int band, double scale)
	{
		const double attack[3] = {0.012, 0.0056, 0.0034};
		const double release[3] = {0.12, 0.06, 0.04};
		upAttack[band] = coefficient (attack[band] * scale);
		upRelease[band] = coefficient (release[band] * scale);
	}

	static double softOver (double over)
	{
		const double knee = 6.0;
		if (over <= -0.5 * knee)
			return 0.0;
		if (over >= 0.5 * knee)
			return over;
		const double x = over + 0.5 * knee;
		return x * x / (2.0 * knee);
	}

	void processPunch (double& l, double& r)
	{
		const double amount = punch.next ();
		const double level = std::max (std::fabs (l), std::fabs (r));
		punchFast += (level > punchFast ? punchFastAttack : punchFastRelease) * (level - punchFast);
		punchSlow += (level > punchSlow ? punchSlowAttack : punchSlowRelease) * (level - punchSlow);
		if (std::fabs (amount) < 1e-5 && punch.settledAt (0.0))
			return;
		// On a steady tone the two followers still differ by their own ripple. The dead band stops
		// that ripple from moving the gain, which would otherwise distort sustained bass.
		const double difference = 20.0 * std::log10 (std::max (punchFast, 1e-6) / std::max (punchSlow, 1e-6)) - kPunchDeadBandDb;
		const double g = std::pow (10.0, amount * std::clamp (difference, 0.0, 12.0) / 20.0);
		l *= g;
		r *= g;
	}

	// Zero below the knee, then a tanh curve that never passes full scale. Softness 0 is a hard clip.
	static double clipSample (double x, double soft)
	{
		const double a = std::fabs (x);
		const double knee = 1.0 - 0.5 * soft;
		if (a <= knee)
			return x;
		const double y = soft <= 1e-4 ? 1.0 : knee + (1.0 - knee) * std::tanh ((a - knee) / (1.0 - knee));
		return x < 0.0 ? -y : y;
	}

	void processClip (double& l, double& r)
	{
		const double gain = clipGain.next ();
		const double soft = clipSoft.next ();
		const double h = heat.next ();
		const double mix = clipMix.next ();
		const double clean = cleanMix.next ();
		const double top = clipCeiling.next ();
		const double g = 1.0 + 15.0 * h;
		const double hNorm = std::tanh (g);
		double in[2] = {l, r};
		double out[2];
		for (int c = 0; c < 2; ++c)
		{
			const double driven = in[c] * gain;
			upHistory[c].push (driven);
			const double* up = upHistory[c].newest ();
			double clipped = 0.0;
			for (int phase = 0; phase < 4; ++phase)
			{
				double y = 0.0;
				for (int i = 0; phase + 4 * i < kOsTaps; ++i)
					y += kernel[static_cast<size_t> (phase + 4 * i)] * up[i];
				y *= 4.0;
				if (h > 1e-5)
					y = (1.0 - h) * y + h * std::tanh (g * y) / hNorm;
				downHistory[c].push (top * clipSample (y / top, soft));
				// Read the output right after phase 0, the one lined up with a base-rate sample.
				// Reading after phase 3 would put it 3/4 of a sample out of time with the dry path.
				if (phase == 0)
				{
					const double* down = downHistory[c].newest ();
					for (int i = 0; i < kOsTaps; ++i)
						clipped += kernel[static_cast<size_t> (i)] * down[i];
				}
			}

			const double dryDriven = clipDry[c].process (driven);
			const double plain = plainDry[c].process (in[c]);
			preClipPeak = std::max (preClipPeak, std::fabs (dryDriven));
			postClipPeak = std::max (postClipPeak, std::fabs (clipped));

			// Below the chosen frequency the signal skips the drive as well as the distortion. If the
			// lows kept the drive without being clipped, the limiter would have to take all of it
			// back from the whole mix.
			double wet = clipped;
			if (clean > 1e-7 || !cleanMix.settledAt (0.0))
				wet += clean * (plain + cleanFilter[c][1].process (cleanFilter[c][0].process (clipped - plain)) - clipped);
			out[c] = plain + mix * (wet - plain);
		}
		l = out[0];
		r = out[1];
	}

	void processLimit (double& l, double& r)
	{
		const double push = limitPush.next ();
		const double cap = ceiling.next ();
		const double mix = limitMix.next ();
		const double plainL = plainLimitDelay[0].process (l);
		const double plainR = plainLimitDelay[1].process (r);
		if (mix < 1e-7 && limitMix.settledAt (0.0))
		{
			l = plainL;
			r = plainR;
			limitLow = 1.0;
			return;
		}
		const double pushedL = l * push;
		const double pushedR = r * push;
		const double peak = std::max (peakReader[0].process (pushedL), peakReader[1].process (pushedR));
		const double need = peak > cap ? cap / peak : 1.0;

		required[ringAt] = need;
		double held = 1.0;
		for (const double v : required)
			held = std::min (held, v);
		if (held < limitEnv)
		{
			limitEnv = held;
			limitHoldLeft = limitHoldLength;
		}
		else if (limitHoldLeft > 0)
			--limitHoldLeft;
		else
			limitEnv += limitRelease * (held - limitEnv);
		smoothingSum += limitEnv - smoothing[ringAt];
		smoothing[ringAt] = limitEnv;
		ringAt = ringAt + 1 == required.size () ? 0 : ringAt + 1;
		const double gain = std::min (1.0, smoothingSum / static_cast<double> (smoothing.size ()));
		limitLow = std::min (limitLow, gain);

		// Audition plays only what the limiter removed. Unity Gain takes Push back off for level-matched listening.
		const double shape = current.limitAudition ? 1.0 - gain : gain;
		const double makeBack = current.limitUnity ? 1.0 / push : 1.0;
		const double limitedL = limitDelay[0].process (pushedL) * shape * makeBack;
		const double limitedR = limitDelay[1].process (pushedR) * shape * makeBack;
		l = plainL + mix * (limitedL - plainL);
		r = plainR + mix * (limitedR - plainR);
	}

	double sampleRate = 44100.0;
	MasterSettings current;
	bool have = false, lowOn = false, highOn = false;

	Smoothed squashUp, bandGain[3];
	Biquad eqFilter[2][5][kEqMaxStages];
	int eqCount[5] {1, 1, 1, 1, 1};
	bool eqOn[5] {};
	Smoothed clipCeiling, dcMix, listenGain[3];
	OnePole dcFilter[2];
	int limitHoldLength = 0, limitHoldLeft = 0;
	double ditherLsb = 0.0;
	uint32_t ditherState = 0x9E3779B9u;
	double squashNet[3] {};
	double beatPos = 0.0, beatStep = 0.0, tempoShape = 0.0, tempoSmooth = 0.1;
	bool transportOn = false;
	Smoothed duckDepth, duckMix, width, monoMix, squashAmount, squashMix, punch, heat, clipGain, clipSoft, clipMix, cleanMix, limitPush, limitMix, ceiling;

	Biquad duckLow[2][2], duckHigh[2][2];
	double duckPhase = 0.0, duckEnv = 0.0, duckThreshold = 0.04, duckAttackStep = 0.01, duckReleaseStep = 0.001, duckEnvDecay = 0.99, duckMost = 0.0;
	int duckHold = 0, duckHoldLength = 500;

	Biquad monoFilter[2];

	Biquad lowSplit[2][2], restSplit[2][2], midSplit[2][2], highSplit[2][2], lowAllpassA[2][2], lowAllpassB[2][2];
	double squashEnv[3] {}, squashAttack[3] {}, squashRelease[3] {};
	double upEnv[3] {}, upAttack[3] {}, upRelease[3] {};

	Biquad lowShelf[2], highShelf[2];

	double punchFast = 0.0, punchSlow = 0.0, punchFastAttack = 0.5, punchFastRelease = 0.01, punchSlowAttack = 0.01, punchSlowRelease = 0.001;

	std::vector<double> kernel;
	History upHistory[2], downHistory[2];
	DelayLine clipDry[2], plainDry[2];
	Biquad cleanFilter[2][2];
	double preClipPeak = 0.0, postClipPeak = 0.0;

	TruePeak peakReader[2], outReader[2];
	DelayLine limitDelay[2], plainLimitDelay[2];
	std::vector<double> required, smoothing;
	double smoothingSum = 0.0, limitEnv = 1.0, limitRelease = 0.001, limitLow = 1.0;
	int limitWindow = 66, limitDelayLength = 72;
	size_t ringAt = 0;

	Loudness loudness;
	double truePeakMax = 0.0, sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;
	int meterCount = 0, meterWindow = 882, correlationCount = 0, correlationWindow = 4410;
};

} // namespace loopsmaster
