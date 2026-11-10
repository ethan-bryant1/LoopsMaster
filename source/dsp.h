// LoopsMaster audio engine. No VST3 dependencies, so it can be tested on its own.
//
// Signal flow:
//   input -> low-pass at the crossover -> Body (per channel: drive, tone, level)
//                                      -> mono tracker -> Sub  (one octave down)
//                                                      -> Deep (two octaves down, from Sub)
//   input gain -> layers = dry * (high + dry bass) + wet * (body + sub + deep), then layer output gain.
//   While Dry Bass and High Band are both at 100%, "high + dry bass" is the untouched input. Otherwise
//   a Linkwitz-Riley crossover splits it, so Body at 100% can replace the lows and still sum flat.
//   layers -> master chain (see master.h) -> output.
//
// The octave layers use a flip-flop: a toggle at each positive zero crossing gives
// a +1/-1 signal at half the input frequency. Multiplying the low band by it moves
// the energy down an octave and keeps the note's natural envelope. Toggling only at
// a zero crossing keeps the result continuous, so it does not click.
#pragma once

#include "dsp_core.h"
#include "master.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace loopsmaster {

// Display feed: the audio thread writes and the window reads, without locks. A display may
// occasionally mix two frames, which is harmless and never blocks audio.
struct ScopeData {
	static constexpr uint32_t kSize = 1024;
	static constexpr double kPointsPerSecond = 2048.0;
	std::atomic<float> body[kSize];
	std::atomic<float> sub[kSize];
	std::atomic<float> deep[kSize];
	std::atomic<uint32_t> written {0};
	std::atomic<uint32_t> lastDeepCycle {0}; // keeps the three waves lined up and standing still
	std::atomic<float> noteHz {0.f};         // 0 when no note is being tracked

	ScopeData ()
	{
		for (uint32_t i = 0; i < kSize; ++i)
		{
			body[i].store (0.f);
			sub[i].store (0.f);
			deep[i].store (0.f);
		}
	}
};

struct Settings {
	double crossoverHz = 110.0;
	int slopeIndex = 1; // 0 = 12, 1 = 24, 2 = 48 dB/oct
	double bodyLevel = 0.5, bodyTone = 0.5, bodyDrive = 0.25;
	double subLevel = 0.35, subTone = 0.3, subDrive = 0.1;
	double deepLevel = 0.0, deepTone = 0.3, deepDrive = 0.0;
	double dry = 1.0, wet = 1.0;
	double outputDb = -3.0;
	bool bypass = false;
	double inputDb = 0.0, dryBass = 1.0, highBand = 1.0, wetHpHz = 0.0; // 0 Hz = off
	bool bodyOn = true, subOn = true, deepOn = true, soloBass = false;
	MasterSettings master;
};

class Engine {
public:
	static constexpr int kMaxSections = 4;

	ScopeData scope;
	MeterData meters;

	// Every path through the plugin is delayed by this much, including bypass.
	int latency () const { return master.latency (); }
	void setTransport (double beats, double tempo, bool playing) { master.setTransport (beats, tempo, playing); }
	static int latencyAt (double sr)
	{
		return MasterChain::kOsDelay + std::max (8, static_cast<int> (0.0015 * sr + 0.5)) + TruePeak::kDelay;
	}

	void prepare (double sr)
	{
		sampleRate = sr > 0.0 ? sr : 44100.0;
		for (Smoothed* s : {&bodyGain, &subGain, &deepGain, &bodyDrv, &subDrv, &deepDrv, &dryGain,
		                    &wetGain, &outGain, &bypassMix, &inputGain, &dryBassGain, &highGain, &splitMix, &soloMix, &wetHpMix})
			s->setRate (sampleRate);
		master.prepare (sampleRate);
		for (auto& d : bypassDelay)
			d.prepare (master.latency ());
		meters.latencyMs.store (static_cast<float> (1000.0 * master.latency () / sampleRate));
		meters.sampleRate.store (static_cast<float> (sampleRate));
		envRelease = std::exp (-1.0 / (0.05 * sampleRate));
		scopeStep = std::max (1, static_cast<int> (sampleRate / ScopeData::kPointsPerSecond + 0.5));
		reset ();
		haveCoefficients = false;
		update (current);
		jumpToTargets ();
	}

	void reset ()
	{
		for (auto& channel : crossover)
			for (auto& section : channel)
				section.reset ();
		for (auto& t : bodyToneFilter)
			t.reset ();
		trackFilter.reset ();
		subFilter.reset ();
		subFilter2.reset ();
		deepFilter.reset ();
		deepFilter2.reset ();
		deepSource.reset ();
		subTracker.reset ();
		deepTracker.reset ();
		flipSub = flipDeep = 1.0;
		samplesSinceCrossing = 0;
		noteHz = kStartNoteHz;
		displayHz = 0.0;
		lastReading = 1.0;
		agreeingReadings = 0;
		retuneLayers ();
		master.reset ();
		for (auto& d : bypassDelay)
			d.reset ();
		for (int c = 0; c < 2; ++c)
		{
			for (auto& f : splitLow[c])
				f.reset ();
			for (auto& f : splitHigh[c])
				f.reset ();
			for (auto& f : wetHp[c])
				f.reset ();
		}
	}

	// Call once per block, before processing.
	void update (const Settings& s)
	{
		const int slope = std::clamp (s.slopeIndex, 0, 2);
		if (!haveCoefficients || s.crossoverHz != current.crossoverHz || slope != activeSlope)
		{
			if (haveCoefficients && slope != activeSlope)
				for (auto& channel : crossover)
					for (auto& section : channel)
						section.reset ();
			static const double q12[] = {0.70710678};
			static const double q24[] = {0.54119610, 1.30656296};
			static const double q48[] = {0.50979558, 0.60134489, 0.89997622, 2.56291545};
			const double* q = slope == 0 ? q12 : slope == 1 ? q24 : q48;
			sections = slope == 0 ? 1 : slope == 1 ? 2 : 4;
			for (auto& channel : crossover)
				for (int i = 0; i < sections; ++i)
					channel[i].setLowPass (s.crossoverHz, q[i], sampleRate);
			// The tracker listens to the fundamental only, so harmonics cannot double-trigger it.
			trackFilter.setLowPass (std::min (s.crossoverHz, 150.0), 0.70710678, sampleRate);
			configureSplit (s.crossoverHz, slope, haveCoefficients && slope != activeSlope);
			activeSlope = slope;
		}
		if (!haveCoefficients || s.bodyTone != current.bodyTone)
		{
			const double hz = 150.0 * std::pow (4000.0 / 150.0, s.bodyTone);
			for (auto& t : bodyToneFilter)
				t.set (hz, sampleRate);
		}
		if (!haveCoefficients || s.subTone != current.subTone || s.deepTone != current.deepTone)
		{
			subToneValue = s.subTone;
			deepToneValue = s.deepTone;
			retuneLayers ();
		}

		if ((!haveCoefficients || s.wetHpHz != current.wetHpHz) && s.wetHpHz > 0.0)
			for (auto& channel : wetHp)
			{
				channel[0].setHighPass (s.wetHpHz, 0.54119610, sampleRate);
				channel[1].setHighPass (s.wetHpHz, 1.30656296, sampleRate);
			}
		bodyGain.setTarget (s.bodyOn ? s.bodyLevel : 0.0);
		subGain.setTarget (s.subOn ? s.subLevel : 0.0);
		deepGain.setTarget (s.deepOn ? s.deepLevel : 0.0);
		inputGain.setTarget (std::pow (10.0, s.inputDb / 20.0));
		dryBassGain.setTarget (s.dryBass);
		highGain.setTarget (s.highBand);
		splitMix.setTarget (needsSplit (s) ? 1.0 : 0.0);
		soloMix.setTarget (s.soloBass ? 1.0 : 0.0);
		wetHpMix.setTarget (s.wetHpHz > 0.0 ? 1.0 : 0.0);
		bodyDrv.setTarget (s.bodyDrive);
		subDrv.setTarget (s.subDrive);
		deepDrv.setTarget (s.deepDrive);
		dryGain.setTarget (s.dry);
		wetGain.setTarget (s.wet);
		outGain.setTarget (std::pow (10.0, s.outputDb / 20.0));
		bypassMix.setTarget (s.bypass ? 1.0 : 0.0);
		master.update (s.master);

		current = s;
		haveCoefficients = true;
	}

	void jumpToTargets ()
	{
		const Settings& s = current;
		bodyGain.jump (s.bodyOn ? s.bodyLevel : 0.0);
		subGain.jump (s.subOn ? s.subLevel : 0.0);
		deepGain.jump (s.deepOn ? s.deepLevel : 0.0);
		inputGain.jump (std::pow (10.0, s.inputDb / 20.0));
		dryBassGain.jump (s.dryBass);
		highGain.jump (s.highBand);
		splitMix.jump (needsSplit (s) ? 1.0 : 0.0);
		soloMix.jump (s.soloBass ? 1.0 : 0.0);
		wetHpMix.jump (s.wetHpHz > 0.0 ? 1.0 : 0.0);
		bodyDrv.jump (s.bodyDrive);
		subDrv.jump (s.subDrive);
		deepDrv.jump (s.deepDrive);
		dryGain.jump (s.dry);
		wetGain.jump (s.wet);
		outGain.jump (std::pow (10.0, s.outputDb / 20.0));
		bypassMix.jump (s.bypass ? 1.0 : 0.0);
		master.jump ();
	}

	// Safe when the output pointers equal the input pointers.
	// The sidechain pointers may be null when the host gives no kick signal.
	void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
	              const float* sideL = nullptr, const float* sideR = nullptr)
	{
		for (int n = 0; n < numSamples; ++n)
		{
			// Bad input must never reach the filters, the histories or the speakers.
			const double rawL = std::isfinite (inL[n]) ? inL[n] : 0.0;
			const double rawR = std::isfinite (inR[n]) ? inR[n] : 0.0;
			const double trim = inputGain.next ();
			const double xL = rawL * trim;
			const double xR = rawR * trim;
			MeterData::raise (meters.inPeak, static_cast<float> (std::max (std::fabs (xL), std::fabs (xR))));

			double side = 0.0;
			if (sideL)
			{
				const double a = std::isfinite (sideL[n]) ? std::fabs (sideL[n]) : 0.0;
				const double b = sideR && std::isfinite (sideR[n]) ? std::fabs (sideR[n]) : 0.0;
				side = std::max (a, b);
				MeterData::raise (meters.sidechainPeak, static_cast<float> (side));
			}

			double lowL = xL;
			double lowR = xR;
			for (int i = 0; i < sections; ++i)
			{
				lowL = crossover[0][i].process (lowL);
				lowR = crossover[1][i].process (lowR);
			}

			// Sub octaves are built from mono so the low end stays centred.
			const double mono = 0.5 * (lowL + lowR);
			++samplesSinceCrossing;
			if (subTracker.crossed (trackFilter.process (mono), envRelease))
			{
				flipSub = -flipSub;
				measureNote ();
			}
			const double rawSub = flipSub * mono;

			// Deep divides the filtered Sub, not the input. Dividing the input by 4 in one
			// step would put most of the energy at 3/4 of the note instead of 1/4.
			const double subClean = deepSource.process (rawSub);
			if (deepTracker.crossed (subClean, envRelease))
			{
				flipDeep = -flipDeep;
				if (flipDeep > 0.0)
					scope.lastDeepCycle.store (scope.written.load (std::memory_order_relaxed), std::memory_order_relaxed);
			}

			const double sub = shape (subFilter2.process (subFilter.process (rawSub)), subDrv.next ());
			const double deep = shape (deepFilter2.process (deepFilter.process (flipDeep * subClean)), deepDrv.next ());
			const double subOut = subGain.next () * sub;
			const double deepOut = deepGain.next () * deep;
			const double layers = subOut + deepOut;

			const double drive = bodyDrv.next ();
			const double level = bodyGain.next ();

			const double split = splitMix.next ();
			const double solo = soloMix.next ();
			const double dryBass = dryBassGain.next ();
			const double high = highGain.next () * (1.0 - solo);
			double dryL = xL, dryR = xR, bodyInL = lowL, bodyInR = lowR;
			if (split > 1e-7 || !splitMix.settledAt (0.0))
			{
				double bandL = xL, bandR = xR, upL = xL, upR = xR;
				for (int i = 0; i < splitSections; ++i)
				{
					bandL = splitLow[0][i].process (bandL);
					bandR = splitLow[1][i].process (bandR);
					upL = splitHigh[0][i].process (upL);
					upR = splitHigh[1][i].process (upR);
				}
				upL *= splitHighSign;
				upR *= splitHighSign;
				bodyInL += split * (bandL - lowL);
				bodyInR += split * (bandR - lowR);
				dryL += split * (high * upL + dryBass * bandL - xL);
				dryR += split * (high * upR + dryBass * bandR - xR);
			}
			const double bodyL = level * bodyToneFilter[0].process (shape (bodyInL, drive));
			const double bodyR = level * bodyToneFilter[1].process (shape (bodyInR, drive));

			if (++scopeCount >= scopeStep)
			{
				scopeCount = 0;
				const uint32_t w = scope.written.load (std::memory_order_relaxed);
				const uint32_t i = w & (ScopeData::kSize - 1);
				scope.body[i].store (static_cast<float> (0.5 * (bodyL + bodyR)), std::memory_order_relaxed);
				scope.sub[i].store (static_cast<float> (subOut), std::memory_order_relaxed);
				scope.deep[i].store (static_cast<float> (deepOut), std::memory_order_relaxed);
				scope.written.store (w + 1, std::memory_order_relaxed);
				scope.noteHz.store (subTracker.envelope > 0.003 ? static_cast<float> (displayHz) : 0.f, std::memory_order_relaxed);
			}

			const double dry = dryGain.next ();
			const double wet = wetGain.next ();
			const double out = outGain.next ();
			const double bypass = bypassMix.next ();

			double wetL = bodyL + layers;
			double wetR = bodyR + layers;
			const double cut = wetHpMix.next ();
			if (cut > 1e-7 || !wetHpMix.settledAt (0.0))
			{
				wetL += cut * (wetHp[0][1].process (wetHp[0][0].process (wetL)) - wetL);
				wetR += cut * (wetHp[1][1].process (wetHp[1][0].process (wetR)) - wetR);
			}
			double yL = out * (dry * dryL + wet * wetL);
			double yR = out * (dry * dryR + wet * wetR);
			master.process (yL, yR, side, meters);
			yL = bypass * bypassDelay[0].process (rawL) + (1.0 - bypass) * yL;
			yR = bypass * bypassDelay[1].process (rawR) + (1.0 - bypass) * yR;

			if (!std::isfinite (yL) || !std::isfinite (yR))
			{
				reset ();
				yL = yR = 0.0;
			}
			master.meter (yL, yR, meters, bypass > 0.5);
			MeterData::raise (meters.outPeak, static_cast<float> (std::max (std::fabs (yL), std::fabs (yR))));
			outL[n] = static_cast<float> (yL);
			outR[n] = static_cast<float> (yR);
		}
	}

private:
	static constexpr double kStartNoteHz = 110.0;

	static bool needsSplit (const Settings& s) { return s.dryBass != 1.0 || s.highBand != 1.0 || s.soloBass; }

	// Linkwitz-Riley filters: the two bands add back to a flat level at every slope.
	void configureSplit (double hz, int slope, bool slopeChanged)
	{
		if (slopeChanged)
			for (int c = 0; c < 2; ++c)
				for (int i = 0; i < kMaxSections; ++i)
				{
					splitLow[c][i].reset ();
					splitHigh[c][i].reset ();
				}
		static const double q12[] = {0.5};
		static const double q24[] = {0.70710678, 0.70710678};
		static const double q48[] = {0.54119610, 1.30656296, 0.54119610, 1.30656296};
		const double* q = slope == 0 ? q12 : slope == 1 ? q24 : q48;
		splitSections = slope == 0 ? 1 : slope == 1 ? 2 : 4;
		splitHighSign = slope == 0 ? -1.0 : 1.0; // the 12 dB form only sums flat with one band inverted
		for (int c = 0; c < 2; ++c)
			for (int i = 0; i < splitSections; ++i)
			{
				splitLow[c][i].setLowPass (hz, q[i], sampleRate);
				splitHigh[c][i].setHighPass (hz, q[i], sampleRate);
			}
	}

	// The time between crossings gives the pitch of the current note.
	void measureNote ()
	{
		const double measured = sampleRate / std::max (1, samplesSinceCrossing);
		samplesSinceCrossing = 0;

		// The display only accepts a pitch once readings agree, so kick sweeps and note
		// attacks do not flash wrong note names.
		agreeingReadings = std::fabs (measured / lastReading - 1.0) < 0.06 ? agreeingReadings + 1 : 0;
		lastReading = measured;
		if (agreeingReadings >= 2 && measured >= 20.0 && measured <= 400.0)
			displayHz = measured;

		// Averaging with the last reading steadies the filters; the range rejects false readings.
		noteHz = 0.5 * noteHz + 0.5 * std::clamp (measured, 20.0, 400.0);
		retuneLayers ();
	}

	// Each division leaves energy at 3x the new note, and that interval stays the same whatever
	// note is played. So the filters follow the note, and Tone sets how much of it gets through:
	// low values give a clean sine-like sub, high values a buzzier octaver.
	void retuneLayers ()
	{
		const double subHz = noteHz * 0.5;
		const double deepHz = noteHz * 0.25;
		const double subCut = subHz * 1.1 * std::pow (5.0, subToneValue);
		const double deepCut = deepHz * 1.1 * std::pow (5.0, deepToneValue);
		subFilter.setLowPass (subCut, 0.54119610, sampleRate);
		subFilter2.setLowPass (subCut, 1.30656296, sampleRate);
		deepFilter.setLowPass (deepCut, 0.54119610, sampleRate);
		deepFilter2.setLowPass (deepCut, 1.30656296, sampleRate);
		deepSource.setLowPass (subHz * 1.3, 0.70710678, sampleRate);
	}

	// Reports each positive zero crossing. The signal must first dip below a level-relative
	// threshold, so noise and small ripples around zero cannot trigger a toggle.
	struct ZeroCrossingTracker
	{
		bool crossed (double x, double release)
		{
			envelope = std::max (std::fabs (x), envelope * release);
			const double threshold = std::max (0.0005, 0.2 * envelope);
			if (x < -threshold)
				armed = true;
			else if (armed && x >= 0.0)
			{
				armed = false;
				return true;
			}
			return false;
		}
		void reset ()
		{
			armed = false;
			envelope = 0.0;
		}
		bool armed = false;
		double envelope = 0.0;
	};

	// Fades from clean to tanh saturation, with peaks normalised so drive does not jump in level.
	static double shape (double x, double drive)
	{
		if (drive <= 0.0)
			return x;
		const double g = 1.0 + 9.0 * drive;
		return (1.0 - drive) * x + drive * std::tanh (g * x) / std::tanh (g);
	}

	double sampleRate = 44100.0;
	Settings current;
	bool haveCoefficients = false;
	int activeSlope = -1;
	int sections = 2;

	Biquad crossover[2][kMaxSections];
	Biquad trackFilter, subFilter, subFilter2, deepFilter, deepFilter2, deepSource;
	double noteHz = kStartNoteHz, subToneValue = 0.3, deepToneValue = 0.3;
	int samplesSinceCrossing = 0;
	double displayHz = 0.0, lastReading = 1.0;
	int agreeingReadings = 0;
	ZeroCrossingTracker subTracker, deepTracker;
	OnePole bodyToneFilter[2];
	Smoothed bodyGain, subGain, deepGain, bodyDrv, subDrv, deepDrv, dryGain, wetGain, outGain, bypassMix, inputGain, dryBassGain, highGain, splitMix, soloMix, wetHpMix;
	Biquad splitLow[2][kMaxSections], splitHigh[2][kMaxSections], wetHp[2][2];
	int splitSections = 2;
	double splitHighSign = 1.0;

	MasterChain master;
	DelayLine bypassDelay[2];

	double flipSub = 1.0, flipDeep = 1.0, envRelease = 0.999;
	int scopeStep = 22, scopeCount = 0;
};

} // namespace loopsmaster
