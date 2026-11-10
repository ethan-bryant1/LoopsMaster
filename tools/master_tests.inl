// Measurements for the master chain. Included by dsp_test.cpp.

struct Stereo
{
	std::vector<float> l, r;
};

static Settings masterOnly ()
{
	Settings s;
	s.bodyLevel = s.subLevel = s.deepLevel = 0.0;
	s.bodyDrive = s.subDrive = s.deepDrive = 0.0;
	s.dry = 1.0;
	s.outputDb = 0.0;
	return s;
}

static Stereo runStereo (Engine& e, const Settings& s, double rate, Stereo in, const std::vector<float>* side = nullptr, int blockSize = 256)
{
	e.update (s);
	e.prepare (rate);
	for (size_t pos = 0; pos < in.l.size (); pos += static_cast<size_t> (blockSize))
	{
		const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (blockSize), in.l.size () - pos));
		e.update (s);
		const float* sc = side ? side->data () + pos : nullptr;
		e.process (in.l.data () + pos, in.r.data () + pos, in.l.data () + pos, in.r.data () + pos, n, sc, sc);
	}
	return in;
}

static Stereo runStereo (const Settings& s, Stereo in, const std::vector<float>* side = nullptr)
{
	Engine e;
	return runStereo (e, s, kRate, std::move (in), side);
}

static Stereo both (const std::vector<float>& x) { return {x, x}; }

static double peakOf (const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
	double p = 0.0;
	if (to == 0 || to > x.size ())
		to = x.size ();
	for (size_t i = from; i < to; ++i)
		p = std::max (p, static_cast<double> (std::fabs (x[i])));
	return p;
}

static double dbOf (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }

// A separate, longer reference for true peak: 16x band-limited interpolation, not the plugin's own.
static double measureTruePeak (const std::vector<float>& x, size_t from)
{
	const int over = 16, half = 32;
	double peak = 0.0;
	for (size_t i = std::max<size_t> (from, static_cast<size_t> (half)); i + static_cast<size_t> (half) < x.size (); ++i)
		for (int f = 0; f < over; ++f)
		{
			const double frac = static_cast<double> (f) / over;
			double y = 0.0;
			for (int k = -half + 1; k <= half; ++k)
			{
				const double t = k - frac;
				const double sinc = std::fabs (t) < 1e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);
				const double w = 0.5 + 0.5 * std::cos (kPi * t / half);
				y += x[i + static_cast<size_t> (k + half) - static_cast<size_t> (half)] * sinc * w;
			}
			peak = std::max (peak, std::fabs (y));
		}
	return peak;
}

// A hot, dense signal: saw-like bass, kick and a bright part.
static Stereo hotMix (double seconds, double gain)
{
	const size_t n = static_cast<size_t> (seconds * kRate);
	Stereo out;
	out.l.resize (n);
	out.r.resize (n);
	double phase = 0.0, smooth = 0.0;
	for (size_t i = 0; i < n; ++i)
	{
		const double t = static_cast<double> (i) / kRate;
		const double beat = std::fmod (t, 0.5);
		phase += 55.0 / kRate;
		phase -= std::floor (phase);
		smooth += 0.2 * ((2.0 * phase - 1.0) - smooth);
		const double kick = std::sin (2.0 * kPi * (52.0 * beat + 3.0 * (1.0 - std::exp (-beat * 38.0)))) * std::exp (-beat * 12.0);
		const double bright = 0.15 * std::sin (2.0 * kPi * 3733.0 * t) + 0.1 * std::sin (2.0 * kPi * 11025.0 * t + 0.78);
		out.l[i] = static_cast<float> (gain * (0.5 * smooth + 0.6 * kick + bright));
		out.r[i] = static_cast<float> (gain * (0.5 * smooth + 0.6 * kick + 0.8 * bright));
	}
	return out;
}

// Functions added from the black-box study. Targets marked "measured" are the readings in docs/SPEC-black-box.md.
static void studyTests ()
{
	std::printf ("\n-- Functions from the plugin study --\n");
	const auto gainAt = [] (const Settings& s, double hz, double amp = 0.1) {
		return dbOf (levelAt (runStereo (s, both (sine (hz, amp, 3.0))).l, hz)) - dbOf (amp);
	};
	{
		Settings bell = masterOnly ();
		bell.master.eq[0].gainDb = 6.0;
		check (std::fabs (gainAt (bell, 508.0) - 2.91) < 0.3, "EQ bell Q1 +6 at 508 Hz, measured 2.91 (dB)", gainAt (bell, 508.0));
		check (std::fabs (gainAt (bell, 2032.0) - 2.75) < 0.3, "EQ bell Q1 +6 at 2032 Hz, measured 2.75 (dB)", gainAt (bell, 2032.0));
		Settings cut = masterOnly ();
		cut.master.eq[0].type = kEqLowCut;
		cut.master.eq[0].hz = 100.0;
		check (std::fabs (gainAt (cut, 100.8) + 2.95) < 0.3, "EQ low cut Q1 at 100 Hz, measured -2.95 (dB)", gainAt (cut, 100.8));
		check (std::fabs (gainAt (cut, 31.7) + 19.99) < 0.5, "EQ low cut Q1 at 31.7 Hz, measured -19.99 (dB)", gainAt (cut, 31.7));
		Settings notch = masterOnly ();
		notch.master.eq[0].type = kEqNotch;
		check (std::fabs (gainAt (notch, 508.0) + 2.87) < 0.3, "EQ notch Q1 at 508 Hz, measured -2.87 (dB)", gainAt (notch, 508.0));
		check (gainAt (notch, 1000.0) < -40.0, "EQ notch removes its centre (dB)", gainAt (notch, 1000.0));

		cut.master.eq[0].slope = 1;
		check (std::fabs (gainAt (cut, 50.0) + 24.0) < 1.0, "EQ 24 dB low cut, one octave down (dB)", gainAt (cut, 50.0));
		cut.master.eq[0].slope = 2;
		check (std::fabs (gainAt (cut, 50.0) + 48.0) < 1.5, "EQ 48 dB low cut, one octave down (dB)", gainAt (cut, 50.0));

		Settings tilt = masterOnly ();
		tilt.master.eq[0].type = kEqTilt;
		tilt.master.eq[0].gainDb = 6.0;
		check (std::fabs (gainAt (tilt, 20.0) + 3.0) < 0.3 && std::fabs (gainAt (tilt, 18000.0) - 3.0) < 0.3, "EQ tilt +6: ends go -3 and +3 (dB at 18k)", gainAt (tilt, 18000.0));
		Settings allPass = masterOnly ();
		allPass.master.eq[0].type = kEqAllPass;
		check (std::fabs (gainAt (allPass, 1000.0)) < 0.01, "EQ all pass keeps the level (dB)", gainAt (allPass, 1000.0));

		// Placement: a Side bell must not touch a mono signal; a Left bell must not touch the right channel.
		Settings side = masterOnly ();
		side.master.eq[0].gainDb = 12.0;
		side.master.eq[0].place = kPlaceSide;
		check (std::fabs (gainAt (side, 1000.0)) < 0.01, "EQ Side band leaves mono alone (dB)", gainAt (side, 1000.0));
		Settings left = side;
		left.master.eq[0].place = kPlaceLeft;
		const auto out = runStereo (left, both (sine (1000.0, 0.1, 3.0)));
		check (std::fabs (dbOf (levelAt (out.l, 1000.0)) - dbOf (0.1) - 12.0) < 0.1 && std::fabs (dbOf (levelAt (out.r, 1000.0)) - dbOf (0.1)) < 0.01, "EQ Left band changes only left (dB)", dbOf (levelAt (out.l, 1000.0)) - dbOf (levelAt (out.r, 1000.0)));
	}
	{
		// Per-band modes and thresholds, on a mid-band tone.
		Settings s = masterOnly ();
		s.master.squash = 1.0;
		s.master.squashUp = 1.0;
		const double both = gainAt (s, 500.0, 0.5);
		// All three bands, because a little of this tone leaks into the other two and takes their gain.
		s.master.bandMode[0] = s.master.bandMode[1] = s.master.bandMode[2] = 3;
		check (std::fabs (gainAt (s, 500.0, 0.5)) < 0.02, "Band mode Off on all bands leaves the tone alone (dB)", gainAt (s, 500.0, 0.5));
		s.master.bandMode[0] = s.master.bandMode[2] = 0;
		check (std::fabs (gainAt (s, 500.0, 0.5)) < 0.3, "Band mode Off on one band: only leakage is left (dB)", gainAt (s, 500.0, 0.5));
		s.master.bandMode[1] = 2;
		check (std::fabs (gainAt (s, 500.0, 0.5) - both) < 0.1, "Band mode Down matches Both on a loud tone (dB)", gainAt (s, 500.0, 0.5));
		s.master.bandMode[1] = 1;
		check (std::fabs (gainAt (s, 500.0, 0.5)) < 0.3, "Band mode Up leaves a loud tone (dB)", gainAt (s, 500.0, 0.5));
		s.master.bandMode[1] = 2;
		const double before = gainAt (s, 500.0, 0.5);
		s.master.bandThresholdDb[1] = 12.0;
		check (gainAt (s, 500.0, 0.5) > before + 8.0, "Band threshold +12 lets a loud tone through by about that (dB)", gainAt (s, 500.0, 0.5) - before);

		Settings listen = masterOnly ();
		listen.master.listen = 1;
		check (gainAt (listen, 5000.0) < -30.0, "Listen Low removes 5 kHz (dB)", gainAt (listen, 5000.0));
		check (std::fabs (gainAt (listen, 30.0)) < 1.0, "Listen Low keeps 30 Hz (dB)", gainAt (listen, 30.0));
	}
	{
		// Clip ceiling, compared with the measured -7 dB threshold clipper.
		Settings s = masterOnly ();
		s.master.clipOn = true;
		s.master.clipSoft = 0.0;
		s.master.clipCeilingDb = -7.0;
		const auto hot = runStereo (s, both (sine (1000.0, std::pow (10.0, 10.0 / 20.0), 3.0)));
		const double top = dbOf (peakOf (hot.l, 40000));
		check (std::fabs (top + 7.0) < 0.5, "Clip ceiling -7: peaks stop at -7 dB (dB)", top);
		check (std::fabs (gainAt (s, 1000.0, 0.1)) < 0.01, "Clip ceiling leaves material below it (dB)", gainAt (s, 1000.0, 0.1));
	}
	{
		// Limiter styles: recovery after a 5 dB hit against the measured curves.
		const auto stepTest = [] (int style, double expectAt100, double expectAt500, double expectAt1000, const char* name) {
			Settings s = masterOnly ();
			s.master.limitOn = true;
			s.master.ceilingDb = 0.0;
			s.master.limitPushDb = 15.0;
			s.master.limitStyle = style;
			const double quiet = std::pow (10.0, -40.0 / 20.0), loud = std::pow (10.0, -10.0 / 20.0);
			auto x = sine (1000.0, 1.0, 5.0);
			const size_t a = static_cast<size_t> (1.0 * kRate), b = static_cast<size_t> (3.0 * kRate);
			for (size_t i = 0; i < x.size (); ++i)
				x[i] *= static_cast<float> (i >= a && i < b ? loud : quiet);
			const auto out = runStereo (s, both (x)).l;
			const size_t lag = static_cast<size_t> (Engine::latencyAt (kRate));
			const auto gainDb = [&] (double ms) {
				const size_t from = b + lag + static_cast<size_t> (ms * 0.001 * kRate);
				return dbOf (peakOf (out, from, from + 441)) - dbOf (quiet);
			};
			char label[96];
			std::snprintf (label, sizeof label, "%s recovery at 500 ms, measured %.1f (dB)", name, expectAt500);
			check (std::fabs (gainDb (100.0) - expectAt100) < 1.0 && std::fabs (gainDb (500.0) - expectAt500) < 1.0 && std::fabs (gainDb (1000.0) - expectAt1000) < 1.0, label, gainDb (500.0));
		};
		stepTest (kStyleClean, 12.7, 14.6, 15.0, "Clean");
		stepTest (kStyleHard, 10.7, 13.9, 14.8, "Hard");
		stepTest (kStyleModern, 10.9, 13.4, 14.5, "Modern");
		stepTest (kStyleSafe, 10.8, 12.8, 13.9, "Safe");

		Settings unity = masterOnly ();
		unity.master.limitOn = true;
		unity.master.limitPushDb = 6.0;
		unity.master.limitUnity = true;
		check (std::fabs (gainAt (unity, 1000.0)) < 0.05, "Unity Gain cancels Push when not limiting (dB)", gainAt (unity, 1000.0));
		Settings audition = masterOnly ();
		audition.master.limitOn = true;
		audition.master.limitAudition = true;
		check (gainAt (audition, 1000.0) < -60.0, "Audition is silent when nothing is limited (dB)", gainAt (audition, 1000.0));
		const double removed = gainAt (audition, 1000.0, 3.0);
		check (removed > -30.0, "Audition plays what was removed when limiting (dB)", removed);
	}
	{
		Settings dc = masterOnly ();
		dc.master.dcFilter = true;
		std::vector<float> offset (static_cast<size_t> (3.0 * kRate), 0.25f);
		const auto out = runStereo (dc, both (offset)).l;
		check (std::fabs (out[out.size () - 100]) < 0.001, "DC filter removes an offset", std::fabs (out[out.size () - 100]));
		check (std::fabs (gainAt (dc, 50.0)) < 0.1, "DC filter keeps 50 Hz (dB)", gainAt (dc, 50.0));

		Settings dither = masterOnly ();
		dither.master.ditherBits = 16;
		std::vector<float> silence (static_cast<size_t> (1.0 * kRate), 0.f);
		const auto noisy = runStereo (dither, both (silence)).l;
		const double top = peakOf (noisy, 5000);
		check (top > 1.0 / 65536.0 && top <= 2.0 / 32768.0 + 1e-7, "16 bit dither stays within 2 LSB", top * 32768.0);
	}
}

// The parametric EQ, fuller multiband, beat-locked duck and loudness range.
static void suiteTests ()
{
	std::printf ("\n-- EQ, multiband, tempo duck, range --\n");
	const auto gainAt = [] (const Settings& s, double hz) {
		return dbOf (levelAt (runStereo (s, both (sine (hz, 0.1, 3.0))).l, hz)) - dbOf (0.1);
	};
	{
		Settings s = masterOnly ();
		s.master.eq[2].hz = 1000.0;
		s.master.eq[2].gainDb = 6.0;
		s.master.eq[2].q = 2.0;
		check (std::fabs (gainAt (s, 1000.0) - 6.0) < 0.1, "EQ bell +6 dB at its centre (dB)", gainAt (s, 1000.0));
		check (std::fabs (gainAt (s, 100.0)) < 0.1, "EQ bell leaves 100 Hz alone (dB)", gainAt (s, 100.0));

		s = masterOnly ();
		s.master.eq[0].type = kEqLowCut;
		s.master.eq[0].hz = 100.0;
		s.master.eq[0].q = 1.0;
		check (gainAt (s, 30.0) < -18.0, "EQ low cut at 100 Hz removes 30 Hz (dB)", gainAt (s, 30.0));
		check (std::fabs (gainAt (s, 1000.0)) < 0.1, "EQ low cut keeps 1 kHz (dB)", gainAt (s, 1000.0));

		s = masterOnly ();
		s.master.eq[4].type = kEqHighShelf;
		s.master.eq[4].hz = 5000.0;
		s.master.eq[4].gainDb = -6.0;
		check (std::fabs (gainAt (s, 18000.0) + 6.0) < 0.5, "EQ high shelf -6 dB at 18 kHz (dB)", gainAt (s, 18000.0));

		// The curve the window draws must match what the audio does.
		Biquad drawn;
		setEqFilter (drawn, kEqBell, 1000.0, 6.0, 2.0, kRate);
		check (std::fabs (drawn.magnitudeDb (700.0, kRate) - [&] { Settings b = masterOnly (); b.master.eq[2].hz = 1000.0; b.master.eq[2].gainDb = 6.0; b.master.eq[2].q = 2.0; return gainAt (b, 700.0); } ()) < 0.1,
		       "EQ drawn curve matches the audio at 700 Hz", drawn.magnitudeDb (700.0, kRate));
	}
	{
		// Upward lifts a quiet passage, and leaves a loud one where it is.
		const size_t n = static_cast<size_t> (4.0 * kRate);
		Settings s = masterOnly ();
		s.master.squashUp = 0.5;
		const double quiet = dbOf (levelAt (runStereo (s, both (sine (500.0, 0.01, 4.0))).l, 500.0)) - dbOf (0.01);
		const double loud = dbOf (levelAt (runStereo (s, both (sine (500.0, 0.5, 4.0))).l, 500.0)) - dbOf (0.5);
		check (quiet > 5.0 && quiet < 6.5, "Squash Up 50% lifts a -40 dB signal about 6 dB (dB)", quiet);
		Settings full = masterOnly ();
		full.master.squashUp = 1.0;
		const double fullLift = dbOf (levelAt (runStereo (full, both (sine (500.0, 0.01, 4.0))).l, 500.0)) - dbOf (0.01);
		check (std::fabs (fullLift - 2.0 * quiet) < 0.2, "Squash Up 100% lifts twice as much (dB)", fullLift);
		check (std::fabs (loud) < 0.1, "Squash Up leaves a -6 dB signal alone (dB)", loud);
		(void) n;
		const double floorLift = dbOf (levelAt (runStereo (s, both (sine (500.0, 0.00001, 4.0))).l, 500.0)) - dbOf (0.00001);
		check (floorLift < 1.0, "Squash Up does not raise the noise floor at -100 dB (dB)", floorLift);

		// What Up is for: the tail of a decaying hit comes up, the body of the hit stays put.
		{
			std::vector<float> hits (static_cast<size_t> (4.0 * kRate));
			for (size_t i = 0; i < hits.size (); ++i)
			{
				const double t = static_cast<double> (i) / kRate;
				hits[i] = static_cast<float> (0.5 * std::exp (-4.6 * std::fmod (t, 1.0)) * std::sin (2.0 * kPi * 500.0 * t));
			}
			Settings none = masterOnly ();
			Settings all = masterOnly ();
			all.master.squashUp = 1.0;
			const auto flat = runStereo (none, both (hits)).l;
			const auto lifted = runStereo (all, both (hits)).l;
			const size_t lag = static_cast<size_t> (Engine::latencyAt (kRate));
			const auto lift = [&] (double from, double to) {
				const size_t a = static_cast<size_t> ((3.0 + from) * kRate) + lag, z = std::min (static_cast<size_t> ((3.0 + to) * kRate) + lag, flat.size ());
				return dbOf (peakOf (lifted, a, z)) - dbOf (peakOf (flat, a, z));
			};
			check (lift (0.8, 0.98) > 4.0, "Squash Up 100% raises the tail of a hit (dB)", lift (0.8, 0.98));
			check (std::fabs (lift (0.1, 0.15)) < 1.0, "Squash Up 100% leaves the body of a hit (dB)", lift (0.1, 0.15));
			std::printf ("      front of each hit, first 10 ms, rises by %.1f dB\n", lift (0.0, 0.01));
		}

		Settings b = masterOnly ();
		b.master.bandDb[1] = 6.0;
		check (std::fabs (gainAt (b, 500.0) - 6.0) < 0.1, "Mid band +6 dB raises 500 Hz (dB)", gainAt (b, 500.0));
		check (std::fabs (gainAt (b, 30.0)) < 0.5, "Mid band leaves 30 Hz alone (dB)", gainAt (b, 30.0));

		b.master.splitLowHz = 300.0;
		check (std::fabs (gainAt (b, 150.0)) < 1.0, "Moving Low split to 300 Hz puts 150 Hz in the low band (dB)", gainAt (b, 150.0));
	}
	{
		// Beat-locked ducking, with no sidechain: low end drops on the beat and returns before the next.
		const double tempo = 120.0;
		Settings s = masterOnly ();
		s.master.duckDepth = 1.0;
		s.master.duckSource = 2;
		s.master.duckReleaseMs = 100.0;
		const auto input = sine (55.0, 0.5, 4.0);
		Engine e;
		e.update (s);
		e.prepare (kRate);
		std::vector<float> l = input, r = input;
		for (size_t pos = 0; pos < l.size (); pos += 256)
		{
			const int n = static_cast<int> (std::min<size_t> (256, l.size () - pos));
			e.update (s);
			e.setTransport (static_cast<double> (pos) / kRate * tempo / 60.0, tempo, true);
			e.process (l.data () + pos, r.data () + pos, l.data () + pos, r.data () + pos, n);
		}
		const size_t lag = static_cast<size_t> (Engine::latencyAt (kRate));
		const size_t beat = static_cast<size_t> (2.0 * kRate) + lag;
		const double onBeat = peakOf (l, beat + 400, beat + 1200);
		const double between = peakOf (l, beat + 11000, beat + 19000);
		check (dbOf (onBeat) < dbOf (between) - 20.0, "Tempo duck: low end drops on the beat (dB)", dbOf (onBeat) - dbOf (between));
		check (between > 0.45, "Tempo duck: low end is back before the next beat", between);
		double step = 0.0;
		for (size_t i = lag + 4000; i + 1 < l.size (); ++i)
			step = std::max (step, static_cast<double> (std::fabs (l[i + 1] - l[i])));
		const double natural = 2.0 * kPi * 55.0 * 0.5 / kRate;
		check (step < 2.5 * natural, "Tempo duck: no clicks", step / natural);

		Engine stopped;
		auto still = runStereo (stopped, s, kRate, both (input));
		check (peakOf (still.l, lag + 8000) > 0.45, "Tempo duck: transport stopped means no ducking", peakOf (still.l, lag + 8000));
	}
	{
		// 20 s at one level and 20 s at another, 10 dB apart, should read close to 10 LU of range.
		const double a = std::pow (10.0, -20.0 / 20.0), b = std::pow (10.0, -30.0 / 20.0);
		std::vector<float> two (static_cast<size_t> (40.0 * kRate));
		for (size_t i = 0; i < two.size (); ++i)
			two[i] = static_cast<float> ((i < two.size () / 2 ? a : b) * std::sin (2.0 * kPi * 1000.0 * i / kRate));
		Engine e;
		runStereo (e, masterOnly (), kRate, both (two));
		check (std::fabs (e.meters.loudnessRange.load () - 10.0) < 1.0, "Loudness range reads about 10 LU on a 10 dB step", e.meters.loudnessRange.load ());
		check (e.meters.spectrumWritten.load () > 100000u, "Spectrum feed is being written", static_cast<double> (e.meters.spectrumWritten.load ()));
	}
}

// The bass section's band split: replace the lows, level each band, solo the bass.
static void splitTests ()
{
	std::printf ("\n-- Bass split --\n");
	const auto gainAt = [] (const Settings& s, double hz) {
		return dbOf (levelAt (runStereo (s, both (sine (hz, 0.1, 3.0))).l, hz)) - dbOf (0.1);
	};
	for (int slope = 0; slope < 3; ++slope)
	{
		Settings s = masterOnly ();
		s.slopeIndex = slope;
		s.dryBass = 0.0;
		s.bodyLevel = 1.0;
		s.bodyTone = 1.0;
		for (double hz : {30.0, 80.0, 110.0, 160.0, 500.0, 5000.0})
		{
			char name[90];
			std::snprintf (name, sizeof name, "Replace, slope %d: Body plus highs flat at %5.0f Hz (dB)", slope, hz);
			const double g = gainAt (s, hz);
			check (std::fabs (g) < 0.5, name, g);
		}
	}
	{
		Settings s = masterOnly ();
		s.dryBass = 0.0;
		const double low = gainAt (s, 40.0);
		check (low < -25.0, "Dry Bass 0%, no Body: 40 Hz removed (dB)", low);
		const double high = gainAt (s, 2000.0);
		check (std::fabs (high) < 0.2, "Dry Bass 0%, no Body: 2 kHz untouched (dB)", high);

		s = masterOnly ();
		s.highBand = 0.0;
		const double top = gainAt (s, 2000.0);
		check (top < -40.0, "High Band 0%: 2 kHz removed (dB)", top);
		const double bottom = gainAt (s, 40.0);
		check (std::fabs (bottom) < 0.5, "High Band 0%: 40 Hz untouched (dB)", bottom);

		s = masterOnly ();
		s.soloBass = true;
		check (gainAt (s, 2000.0) < -40.0, "Solo Bass: 2 kHz removed (dB)", gainAt (s, 2000.0));
		check (std::fabs (gainAt (s, 40.0)) < 0.5, "Solo Bass: 40 Hz kept (dB)", gainAt (s, 40.0));

		s = masterOnly ();
		s.inputDb = 6.0;
		check (std::fabs (gainAt (s, 1000.0) - 6.0) < 0.1, "Input Gain +6 dB raises the output 6 dB (dB)", gainAt (s, 1000.0));
	}
	{
		// Switching a layer off silences it but keeps its Level value.
		Settings s = wetOnly ();
		s.subLevel = 1.0;
		const double on = levelAt (run (sine (110.0, 0.5, 4.0), s), 55.0);
		s.subOn = false;
		const double off = levelAt (run (sine (110.0, 0.5, 4.0), s), 55.0);
		check (on > 0.1 && off < 1e-5, "Sub switch off silences the Sub layer", off);

		// Wet High-pass cuts what is too low to hear from the new layers, not the dry signal.
		s = wetOnly ();
		s.subLevel = 1.0;
		const double open = levelAt (run (sine (55.0, 0.5, 4.0), s), 27.5);
		s.wetHpHz = 60.0;
		const double cut = levelAt (run (sine (55.0, 0.5, 4.0), s), 27.5);
		check (dbOf (cut) < dbOf (open) - 15.0, "Wet High-pass 60 Hz cuts a 27.5 Hz sub (dB)", dbOf (cut) - dbOf (open));
		Settings d = masterOnly ();
		d.wetHpHz = 60.0;
		const double dryKept = gainAt (d, 27.5);
		check (std::fabs (dryKept) < 0.1, "Wet High-pass leaves the dry signal alone (dB)", dryKept);
	}
	{
		// Moving Dry Bass mid-play must fade, not click.
		const auto input = sine (60.0, 0.5, 2.0);
		Settings s = masterOnly ();
		Engine e;
		e.update (s);
		e.prepare (kRate);
		std::vector<float> l = input, r = input;
		for (size_t pos = 0; pos < l.size (); pos += 256)
		{
			s.dryBass = pos >= l.size () / 2 ? 0.0 : 1.0;
			s.bodyLevel = pos >= l.size () / 2 ? 1.0 : 0.0;
			const int n = static_cast<int> (std::min<size_t> (256, l.size () - pos));
			e.update (s);
			e.process (l.data () + pos, r.data () + pos, l.data () + pos, r.data () + pos, n);
		}
		double step = 0.0;
		for (size_t i = 2000; i + 1 < l.size (); ++i)
			step = std::max (step, static_cast<double> (std::fabs (l[i + 1] - l[i])));
		check (step < 0.02, "Switching to Replace mid-play: no click (largest jump)", step);
	}
}

static void masterTests ()
{
	std::printf ("\n-- Master chain --\n");
	const size_t lag = static_cast<size_t> (Engine::latencyAt (kRate));

	{
		Engine e;
		e.update (masterOnly ());
		e.prepare (kRate);
		check (e.latency () == static_cast<int> (lag), "Latency: engine and host figure agree", e.latency ());
		Settings s = masterOnly ();
		s.master.clipOn = s.master.limitOn = true;
		std::vector<float> impulse (4096, 0.f);
		impulse[100] = 0.5f;
		auto y = runStereo (s, both (impulse));
		size_t at = 0;
		for (size_t i = 0; i < y.l.size (); ++i)
			if (std::fabs (y.l[i]) > std::fabs (y.l[at]))
				at = i;
		check (at == 100 + lag, "Latency: an impulse comes out exactly where reported", static_cast<double> (at) - 100.0);
	}

	// Duck: only the low band moves, and only when the kick is there.
	{
		const size_t n = static_cast<size_t> (2.0 * kRate);
		std::vector<float> bass (n), side (n, 0.f);
		for (size_t i = 0; i < n; ++i)
			bass[i] = static_cast<float> (0.4 * std::sin (2.0 * kPi * 55.0 * i / kRate) + 0.2 * std::sin (2.0 * kPi * 1500.0 * i / kRate));
		const size_t kickFrom = n / 2, kickTo = kickFrom + static_cast<size_t> (0.1 * kRate);
		for (size_t i = kickFrom; i < kickTo; ++i)
			side[i] = static_cast<float> (0.5 * std::sin (2.0 * kPi * 60.0 * i / kRate));

		Settings s = masterOnly ();
		s.master.duckDepth = 1.0;
		auto y = runStereo (s, both (bass), &side);
		const auto window = [&] (size_t from, size_t to) { return std::vector<float> (y.l.begin () + static_cast<long long> (from + lag), y.l.begin () + static_cast<long long> (to + lag)); };
		const size_t quarter = kickFrom - static_cast<size_t> (0.5 * kRate);
		const size_t mid = kickFrom + static_cast<size_t> (0.03 * kRate);
		const auto before = window (quarter, quarter + 2600);
		const auto during = window (mid, mid + 2600);
		const auto levelFull = [] (const std::vector<float>& x, double hz) {
			const double c = 2.0 * std::cos (2.0 * kPi * hz / kRate);
			double a = 0.0, b = 0.0;
			for (float v : x)
			{
				const double t = v + c * a - b;
				b = a;
				a = t;
			}
			return 2.0 * std::sqrt (std::max (0.0, a * a + b * b - c * a * b)) / static_cast<double> (x.size ());
		};
		const double lowDrop = dbOf (levelFull (during, 55.0)) - dbOf (levelFull (before, 55.0));
		const double highDrop = dbOf (levelFull (during, 1500.0)) - dbOf (levelFull (before, 1500.0));
		check (lowDrop < -18.0, "Duck: 55 Hz drops hard while the kick plays (dB)", lowDrop);
		check (std::fabs (highDrop) < 0.5, "Duck: 1.5 kHz stays put while the kick plays (dB)", highDrop);

		auto noKick = runStereo (s, both (bass));
		double diff = levelAt (noKick.l, 55.0);
		check (diff > 0.35, "Duck: no sidechain signal means no ducking", diff);

		// Low band only, so jumps in level would show as clicks on a pure low tone.
		const std::vector<float> low = sine (55.0, 0.5, 2.0);
		std::vector<float> side2 = side;
		side2.resize (low.size (), 0.f);
		auto lowOut = runStereo (s, both (low), &side2);
		double step = 0.0;
		for (size_t i = lag + 2000; i + 1 < lowOut.l.size (); ++i)
			step = std::max (step, static_cast<double> (std::fabs (lowOut.l[i + 1] - lowOut.l[i])));
		const double natural = 2.0 * kPi * 55.0 * 0.5 / kRate;
		check (step < 1.6 * natural, "Duck: no click, jump stays near the tone's own slope", step / natural);
	}

	// Width and mono-below.
	{
		const size_t n = static_cast<size_t> (2.0 * kRate);
		Stereo wide;
		wide.l.resize (n);
		wide.r.resize (n);
		for (size_t i = 0; i < n; ++i)
		{
			const double low = 0.3 * std::sin (2.0 * kPi * 50.0 * i / kRate);
			const double high = 0.3 * std::sin (2.0 * kPi * 2000.0 * i / kRate);
			wide.l[i] = static_cast<float> (low + high);
			wide.r[i] = static_cast<float> (-low - high);
		}
		Settings s = masterOnly ();
		s.master.width = 0.0;
		auto mono = runStereo (s, wide);
		check (peakOf (mono.l, n / 2) < 1e-3, "Width 0: out-of-phase signal cancels to mono", peakOf (mono.l, n / 2));

		s = masterOnly ();
		s.master.monoBelowHz = 150.0;
		auto y = runStereo (s, wide);
		check (levelAt (y.l, 50.0) < 0.02, "Mono below 150 Hz: 50 Hz side signal removed", levelAt (y.l, 50.0));
		check (levelAt (y.l, 2000.0) > 0.29, "Mono below 150 Hz: 2 kHz side signal kept", levelAt (y.l, 2000.0));
	}

	// Squash: the amount evens out a level step, and zero does nothing.
	{
		const size_t n = static_cast<size_t> (4.0 * kRate);
		std::vector<float> steps (n);
		for (size_t i = 0; i < n; ++i)
			steps[i] = static_cast<float> ((i < n / 2 ? 0.05 : 0.5) * std::sin (2.0 * kPi * 500.0 * i / kRate));
		Settings s = masterOnly ();
		s.master.squash = 0.31;
		auto y = runStereo (s, both (steps));
		const double quiet = peakOf (y.l, n / 4, n / 2 - 2000);
		const double loud = peakOf (y.l, 3 * n / 4, n);
		const double inputStep = 20.0;
		const double outputStep = dbOf (loud) - dbOf (quiet);
		check (outputStep < inputStep - 4.0, "Squash 31%: a 20 dB step comes out smaller (dB)", outputStep);
		check (outputStep > 8.0, "Squash 31%: dynamics reduced, not flattened (dB)", outputStep);

		Settings full = masterOnly ();
		full.master.squash = 1.0;
		auto sweep = runStereo (full, both (sine (500.0, 0.5, 2.0)));
		check (peakOf (sweep.l) < 8.0, "Squash 100%: output stays bounded", peakOf (sweep.l));

		// Bands must add back to flat, or the squash itself would colour the sound.
		Settings tiny = masterOnly ();
		tiny.master.squash = 1e-6;
		for (double hz : {40.0, 88.0, 300.0, 2500.0, 9000.0})
		{
			auto flat = runStereo (tiny, both (sine (hz, 0.25, 2.0)));
			char name[80];
			std::snprintf (name, sizeof name, "Squash bands sum flat at %5.0f Hz (dB)", hz);
			const double delta = dbOf (levelAt (flat.l, hz)) - dbOf (0.25);
			check (std::fabs (delta) < 0.1, name, delta);
		}
	}

	// Tone.
	{
		Settings s = masterOnly ();
		s.master.lowDb = 6.0;
		s.master.highDb = -6.0;
		const double low = dbOf (levelAt (runStereo (s, both (sine (30.0, 0.1, 2.0))).l, 30.0)) - dbOf (0.1);
		const double high = dbOf (levelAt (runStereo (s, both (sine (18000.0, 0.1, 2.0))).l, 18000.0)) - dbOf (0.1);
		const double middle = dbOf (levelAt (runStereo (s, both (sine (800.0, 0.1, 2.0))).l, 800.0)) - dbOf (0.1);
		check (std::fabs (low - 6.0) < 0.5, "Low shelf +6 dB at 30 Hz (dB)", low);
		check (std::fabs (high + 6.0) < 0.5, "High shelf -6 dB at 18 kHz (dB)", high);
		check (std::fabs (middle) < 0.3, "Shelves leave 800 Hz alone (dB)", middle);
	}

	// Punch lifts the start of a hit without changing a steady tone.
	{
		const size_t n = static_cast<size_t> (2.0 * kRate);
		std::vector<float> hits (n, 0.f);
		const size_t start = n / 2;
		for (size_t i = start; i < n; ++i)
			hits[i] = static_cast<float> (0.2 * std::sin (2.0 * kPi * 200.0 * (i - start) / kRate));
		Settings s = masterOnly ();
		s.master.punch = 0.6;
		auto y = runStereo (s, both (hits));
		const double attack = peakOf (y.l, start + lag, start + lag + 1000);
		const double later = peakOf (y.l, n - 8000, n);
		check (attack > 1.3 * later, "Punch: attack louder than the sustain", attack / later);
		check (std::fabs (dbOf (later) - dbOf (0.2)) < 0.1, "Punch: settled 200 Hz tone keeps its level (dB)", dbOf (later) - dbOf (0.2));
		auto bass = runStereo (s, both (sine (41.2, 0.3, 2.0)));
		const double bassLevel = dbOf (peakOf (bass.l, bass.l.size () / 2)) - dbOf (0.3);
		check (std::fabs (bassLevel) < 0.1, "Punch: settled 41 Hz bass keeps its level (dB)", bassLevel);
	}

	// Clip.
	{
		Settings s = masterOnly ();
		s.master.clipOn = true;
		s.master.clipDriveDb = 12.0;
		s.master.clipSoft = 0.0;
		auto y = runStereo (s, both (sine (110.0, 0.9, 2.0)));
		check (peakOf (y.l, lag + 2000) < 1.05, "Clip: hard clip holds near full scale", peakOf (y.l, lag + 2000));
		check (levelAt (y.l, 330.0) > 0.1, "Clip: drive really does create harmonics", levelAt (y.l, 330.0));

		// Below the knee the oversampled path must sound the same as the plain one.
		Settings gentle = masterOnly ();
		gentle.master.clipOn = true;
		gentle.master.clipSoft = 0.25;
		for (double hz : {100.0, 5000.0, 15000.0})
		{
			const double delta = dbOf (levelAt (runStereo (gentle, both (sine (hz, 0.25, 2.0))).l, hz)) - dbOf (0.25);
			char name[80];
			std::snprintf (name, sizeof name, "Clip on, under the knee: flat at %5.0f Hz (dB)", hz);
			check (std::fabs (delta) < 0.1, name, delta);
		}

		// The 3rd harmonic of 9.5 kHz is 28.5 kHz, which would fold down to 15.6 kHz without oversampling.
		Settings alias = masterOnly ();
		alias.master.clipOn = true;
		alias.master.clipDriveDb = 12.0;
		alias.master.clipSoft = 0.0;
		auto a = runStereo (alias, both (sine (9500.0, 0.9, 2.0)));
		const double folded = dbOf (levelAt (a.l, 15600.0)) - dbOf (levelAt (a.l, 9500.0));
		check (folded < -70.0, "Clip: aliasing product at 15.6 kHz well down (dB)", folded);

		// Two close tones clipped make a low difference tone. Clean Below removes it.
		const size_t n = static_cast<size_t> (3.0 * kRate);
		std::vector<float> pair (n);
		for (size_t i = 0; i < n; ++i)
			pair[i] = static_cast<float> (0.45 * (std::sin (2.0 * kPi * 1000.0 * i / kRate) + std::sin (2.0 * kPi * 1040.0 * i / kRate)));
		Settings dirty = masterOnly ();
		dirty.master.clipOn = true;
		dirty.master.clipDriveDb = 12.0;
		dirty.master.clipSoft = 0.0;
		Settings clean = dirty;
		clean.master.cleanBelowHz = 150.0;
		auto dirtyOut = runStereo (dirty, both (pair));
		auto cleanOut = runStereo (clean, both (pair));
		const double dirtyLow = levelAt (dirtyOut.l, 40.0);
		const double cleanLow = levelAt (cleanOut.l, 40.0);
		check (dbOf (cleanLow) < dbOf (dirtyLow) - 20.0, "Clean Below: low distortion falls by over 20 dB", dbOf (cleanLow) - dbOf (dirtyLow));
		const double aboveChange = dbOf (levelAt (cleanOut.l, 3000.0)) - dbOf (levelAt (dirtyOut.l, 3000.0));
		check (std::fabs (aboveChange) < 0.2, "Clean Below: 3 kHz distortion product unchanged (dB)", aboveChange);

		// Below the Clean Below frequency, drive must not raise the level at all.
		Settings driven = masterOnly ();
		driven.master.clipOn = true;
		driven.master.clipDriveDb = 12.0;
		driven.master.cleanBelowHz = 120.0;
		const double lowChange = dbOf (levelAt (runStereo (driven, both (sine (40.0, 0.5, 3.0))).l, 40.0)) - dbOf (0.5);
		check (std::fabs (lowChange) < 0.5, "Clean Below: 12 dB of drive leaves 40 Hz level alone (dB)", lowChange);

		// With the path in time with the dry signal, clipping must not leave any part of a
		// signal it did not clip: just under the knee, the output equals the delayed input.
		auto under = runStereo (gentle, both (sine (3000.0, 0.25, 1.0)));
		const auto underRef = sine (3000.0, 0.25, 1.0);
		double worst = 0.0;
		for (size_t i = lag + 2000; i < under.l.size (); ++i)
			worst = std::max (worst, static_cast<double> (std::fabs (under.l[i] - underRef[i - lag])));
		check (worst < 1e-3, "Clip path is sample-accurate with the dry path", worst);
	}

	// Limiter.
	{
		// fs/4 at 45 degrees: samples sit about 3 dB under the real peak, so only a true-peak reader sees it.
		std::vector<float> sneaky (static_cast<size_t> (2.0 * kRate));
		for (size_t i = 0; i < sneaky.size (); ++i)
			sneaky[i] = static_cast<float> (0.99 * std::sin (kPi * 0.5 * i + kPi * 0.25));
		Settings s = masterOnly ();
		s.master.limitOn = true;
		s.master.ceilingDb = -1.0;
		auto y = runStereo (s, both (sneaky));
		const double tp = dbOf (measureTruePeak (y.l, lag + 4000));
		check (tp <= -0.9, "Limit: inter-sample peak held at the ceiling (dBTP)", tp);

		auto hot = hotMix (6.0, 1.6);
		s.master.limitPushDb = 3.0;
		auto limited = runStereo (s, hot);
		const size_t settled = lag + 4410;
		const double tpHot = dbOf (measureTruePeak (std::vector<float> (limited.l.begin (), limited.l.begin () + 88200), settled));
		check (tpHot <= -0.8, "Limit: dense hot mix stays under the ceiling (dBTP)", tpHot);

		Settings quiet = masterOnly ();
		quiet.master.limitOn = true;
		auto low = runStereo (quiet, both (sine (440.0, 0.25, 2.0)));
		double worst = 0.0;
		const auto ref = sine (440.0, 0.25, 2.0);
		for (size_t i = lag; i < low.l.size (); ++i)
			worst = std::max (worst, static_cast<double> (std::fabs (low.l[i] - ref[i - lag])));
		check (worst < 1e-5, "Limit: leaves signal under the ceiling untouched", worst);

		// The ceiling must also hold at a high sample rate, where the window is longer.
		std::vector<float> fast (static_cast<size_t> (96000.0));
		for (size_t i = 0; i < fast.size (); ++i)
			fast[i] = static_cast<float> (1.4 * std::sin (2.0 * kPi * 80.0 * i / 96000.0));
		Engine e96;
		Settings s96 = masterOnly ();
		s96.master.limitOn = true;
		auto y96 = runStereo (e96, s96, 96000.0, both (fast));
		check (dbOf (peakOf (y96.l, 4800)) <= -0.95, "Limit at 96 kHz: holds the ceiling (dB)", dbOf (peakOf (y96.l, 4800)));
	}

	// Loudness reference: a stereo 1 kHz sine at -23 dBFS reads -23 LUFS by the standard's own definition.
	{
		Engine e;
		const double amp = std::pow (10.0, -23.0 / 20.0);
		runStereo (e, masterOnly (), kRate, both (sine (1000.0, amp, 6.0)));
		check (std::fabs (e.meters.momentary.load () + 23.0) < 0.1, "LUFS momentary reads -23 on the reference", e.meters.momentary.load ());
		check (std::fabs (e.meters.shortTerm.load () + 23.0) < 0.1, "LUFS short term reads -23 on the reference", e.meters.shortTerm.load ());
		check (std::fabs (e.meters.integrated.load () + 23.0) < 0.1, "LUFS integrated reads -23 on the reference", e.meters.integrated.load ());

		// Long quiet tail is gated out of the integrated figure.
		std::vector<float> two (static_cast<size_t> (20.0 * kRate));
		for (size_t i = 0; i < two.size (); ++i)
			two[i] = static_cast<float> ((i < static_cast<size_t> (5.0 * kRate) ? amp : amp * 0.01) * std::sin (2.0 * kPi * 1000.0 * i / kRate));
		Engine g;
		runStereo (g, masterOnly (), kRate, both (two));
		check (std::fabs (g.meters.integrated.load () + 23.0) < 0.3, "LUFS integrated: gate ignores a quiet tail", g.meters.integrated.load ());

		// True-peak meter: it should see the hidden fs/4 peak.
		std::vector<float> sneaky (static_cast<size_t> (kRate));
		for (size_t i = 0; i < sneaky.size (); ++i)
			sneaky[i] = static_cast<float> (0.7071 * std::sin (kPi * 0.5 * i + kPi * 0.25));
		Engine t;
		runStereo (t, masterOnly (), kRate, both (sneaky));
		// Samples sit at 0.5 (-6 dB) but the wave itself peaks at 0.7071 (-3 dB).
		check (std::fabs (t.meters.truePeakDb.load () + 3.01) < 0.3, "True peak meter finds -3 dB between -6 dB samples", t.meters.truePeakDb.load ());

		Engine c;
		std::vector<float> w = sine (500.0, 0.3, 1.0);
		Stereo opposite = both (w);
		for (auto& v : opposite.r)
			v = -v;
		runStereo (c, masterOnly (), kRate, opposite);
		check (c.meters.correlation.load () < -0.99, "Correlation reads -1 for opposite channels", c.meters.correlation.load ());

		// The meters must describe what is heard, so Bypass reads the untouched input.
		Settings clipped = masterOnly ();
		clipped.master.clipOn = true;
		clipped.master.clipCeilingDb = -12.0;
		const std::vector<float> hot = sine (500.0, 0.9, 2.0);
		Engine on;
		runStereo (on, clipped, kRate, both (hot));
		check (on.meters.truePeakDb.load () < -9.0, "Bypass test: processed true peak follows the clip ceiling", on.meters.truePeakDb.load ());
		check (on.meters.clipDb.load () > 6.0, "Bypass test: Clip meter moves when processing", on.meters.clipDb.load ());
		Settings skipped = clipped;
		skipped.bypass = true;
		Engine off;
		runStereo (off, skipped, kRate, both (hot));
		check (std::fabs (off.meters.truePeakDb.load () - dbOf (0.9)) < 0.3, "Bypass: true peak reads the untouched input", off.meters.truePeakDb.load ());
		check (off.meters.clipDb.load () < 0.01f, "Bypass: Clip meter rests at zero", off.meters.clipDb.load ());
		check (off.meters.integrated.load () > on.meters.integrated.load () + 6.0, "Bypass: loudness reads the untouched input", off.meters.integrated.load ());
	}

	// The three master presets must get louder in order and leave the limiter doing the least work.
	{
		struct Level { const char* name; double drive, soft, clean, push, heat, mono; };
		const Level levels[3] = {{"Safe", 0, 0.25, 0, 0, 0, 0}, {"Loud", 6, 0.25, 50, 1, 0, 80}, {"Slam", 12, 0.1, 0, 2, 0.1, 120}};
		double last = -100.0;
		for (const Level& p : levels)
		{
			Settings s = masterOnly ();
			s.master.clipOn = s.master.limitOn = true;
			s.master.clipDriveDb = p.drive;
			s.master.clipSoft = p.soft;
			s.master.cleanBelowHz = p.clean;
			s.master.limitPushDb = p.push;
			s.master.heat = p.heat;
			s.master.monoBelowHz = p.mono;
			Engine e;
			runStereo (e, s, kRate, hotMix (6.0, 0.5));
			const double lufs = e.meters.integrated.load ();
			char name[80];
			std::snprintf (name, sizeof name, "Preset %s is louder than the one before (LUFS)", p.name);
			check (lufs > last + 1.0, name, lufs);
			std::snprintf (name, sizeof name, "Preset %s holds the ceiling (dBTP)", p.name);
			check (e.meters.truePeakDb.load () <= -0.8, name, e.meters.truePeakDb.load ());
			std::snprintf (name, sizeof name, "Preset %s leaves the limiter gentle (most reduction, dB)", p.name);
			check (e.meters.limitDb.load () < 6.0, name, e.meters.limitDb.load ());
			last = lufs;
		}
	}

	// Everything on at once, with bad input on both the main and sidechain buses.
	{
		Settings s = masterOnly ();
		s.subLevel = 0.5;
		s.master.duckDepth = 1.0;
		s.master.width = 1.5;
		s.master.monoBelowHz = 120.0;
		s.master.squash = 0.5;
		s.master.lowDb = 3.0;
		s.master.highDb = 3.0;
		s.master.punch = 0.3;
		s.master.heat = 0.5;
		s.master.clipOn = true;
		s.master.clipDriveDb = 18.0;
		s.master.cleanBelowHz = 100.0;
		s.master.limitOn = true;
		s.master.limitPushDb = 6.0;
		auto mix = hotMix (3.0, 1.0);
		std::vector<float> side = mix.l;
		mix.l[5000] = std::numeric_limits<float>::quiet_NaN ();
		mix.r[9000] = std::numeric_limits<float>::infinity ();
		side[7000] = std::numeric_limits<float>::quiet_NaN ();
		auto y = runStereo (s, mix, &side);
		bool finite = true;
		for (size_t i = 0; i < y.l.size (); ++i)
			finite = finite && std::isfinite (y.l[i]) && std::isfinite (y.r[i]);
		check (finite, "All stages on with NaN and Inf input: stays finite", finite ? 1.0 : 0.0);
		check (dbOf (peakOf (y.l, lag + 4410)) <= -0.95, "All stages on: ceiling still holds (dB)", dbOf (peakOf (y.l, lag + 4410)));

		// The host may use any block size; the result must not depend on it.
		Engine a, b;
		auto small = runStereo (a, s, kRate, hotMix (1.0, 1.0), nullptr, 37);
		auto large = runStereo (b, s, kRate, hotMix (1.0, 1.0), nullptr, 512);
		double worst = 0.0;
		for (size_t i = 0; i < small.l.size (); ++i)
			worst = std::max (worst, static_cast<double> (std::fabs (small.l[i] - large.l[i])));
		check (worst < 1e-6, "Block size does not change the result", worst);
	}
}

// The settings from the user's Live session: Body layer on, Punch, tone, Clip on, Limit off.
static Settings sessionSettings (double cleanHz)
{
	Settings s;
	s.crossoverHz = 75.0;
	s.bodyLevel = 0.39;
	s.bodyTone = 0.22;
	s.bodyDrive = 0.26;
	s.subLevel = s.deepLevel = 0.0;
	s.subDrive = s.deepDrive = 0.0;
	s.subTone = s.deepTone = 0.5;
	s.outputDb = -2.4;
	s.master.lowDb = -1.9;
	s.master.highDb = 1.2;
	s.master.punch = 0.22;
	s.master.clipOn = true;
	s.master.clipDriveDb = 6.2;
	s.master.clipSoft = 0.26;
	s.master.cleanBelowHz = cleanHz;
	s.master.limitOn = false;
	return s;
}

static void reportPeaks (const char* name, Engine& e, const Stereo& y)
{
	const size_t from = static_cast<size_t> (Engine::latencyAt (kRate)) + 2000;
	const double reference = std::max (measureTruePeak (y.l, from), measureTruePeak (y.r, from));
	std::printf ("%-44s meter %6.2f dBTP | independent %6.2f dBTP | sample peak %6.2f dB | clip %5.1f dB | %6.2f LUFS\n", name,
	             e.meters.truePeakDb.load (), dbOf (reference), dbOf (std::max (peakOf (y.l, from), peakOf (y.r, from))),
	             e.meters.clipDb.load (), e.meters.integrated.load ());
}

static void peakDiagnostics ()
{
	std::printf ("\n-- Peak diagnostics, user's session settings, 2 seconds of hot mix --\n");
	for (double gain : {1.0, 1.5, 2.5})
	{
		char name[80];
		Engine a;
		auto y = runStereo (a, sessionSettings (0.0), kRate, hotMix (2.0, gain));
		std::snprintf (name, sizeof name, "input x%.1f, Clean below off", gain);
		reportPeaks (name, a, y);
		Engine b;
		y = runStereo (b, sessionSettings (225.0), kRate, hotMix (2.0, gain));
		std::snprintf (name, sizeof name, "input x%.1f, Clean below 225 Hz", gain);
		reportPeaks (name, b, y);
	}
	// Turning Clean below off while audio plays, and resetting the meter just before.
	{
		Engine e;
		Settings s = sessionSettings (225.0);
		auto mix = hotMix (3.0, 1.5);
		e.update (s);
		e.prepare (kRate);
		const size_t half = mix.l.size () / 2;
		double before = -100.0;
		for (size_t pos = 0; pos < mix.l.size (); pos += 256)
		{
			const int n = static_cast<int> (std::min<size_t> (256, mix.l.size () - pos));
			if (pos >= half && before == -100.0)
			{
				before = e.meters.truePeakDb.load ();
				e.meters.resetRequest.store (true);
				s.master.cleanBelowHz = 0.0;
			}
			e.update (s);
			e.process (mix.l.data () + pos, mix.r.data () + pos, mix.l.data () + pos, mix.r.data () + pos, n);
		}
		std::printf ("Reset, then Clean below 225 Hz to off mid-play: before %.2f dBTP, after %.2f dBTP\n", before, e.meters.truePeakDb.load ());
	}
	// How far above the samples the meter's interpolator can ever read: a worst-case pattern.
	{
		std::vector<float> worst (8000);
		for (size_t i = 0; i < worst.size (); ++i)
			worst[i] = ((i / 2) % 2 == 0) == (std::fmod (static_cast<double> (i) / 7.0, 2.0) < 1.0) ? 1.0f : -1.0f;
		Engine e;
		auto y = runStereo (e, masterOnly (), kRate, both (worst));
		reportPeaks ("full-scale jagged pattern, stages off", e, y);
	}
}

// Renders a WAV through the Master Loud settings and prints what the meters read.
static int renderMaster (const char* in, const char* out)
{
	std::vector<float> l, r;
	uint32_t rate = 44100;
	if (!readWav (in, l, r, rate))
		return 2;
	struct Level { const char* name; double drive, soft, clean, push, heat, mono, release; };
	const Level levels[3] = {{"Safe", 0, 0.25, 0, 0, 0, 0, 100}, {"Loud", 6, 0.25, 50, 1, 0, 80, 100}, {"Slam", 12, 0.1, 0, 2, 0.1, 120, 150}};
	Stereo loud;
	for (const Level& p : levels)
	{
		Settings s = masterOnly ();
		s.master.clipOn = s.master.limitOn = true;
		s.master.clipDriveDb = p.drive;
		s.master.clipSoft = p.soft;
		s.master.cleanBelowHz = p.clean;
		s.master.limitPushDb = p.push;
		s.master.heat = p.heat;
		s.master.monoBelowHz = p.mono;
		s.master.limitReleaseMs = p.release;
		Engine e;
		Stereo y = runStereo (e, s, rate, Stereo {l, r});
		std::printf ("Master %-5s output sample peak %.1f dB, true peak %.1f dBTP, integrated %.1f LUFS, most limiting %.1f dB\n", p.name,
		             dbOf (std::max (peakOf (y.l), peakOf (y.r))), e.meters.truePeakDb.load (), e.meters.integrated.load (), e.meters.limitDb.load ());
		if (p.drive == 6.0)
			loud = y;
	}
	return writeWav (out, loud.l, loud.r, rate) ? 0 : 2;
}
