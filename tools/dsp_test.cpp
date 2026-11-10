// Audio test for the engine. Measures real output rather than trusting the design:
// the sub layers must land on the right octaves, bypass must be exact, nothing may blow up.
// With two arguments it also renders a WAV through the engine: dsp_test in.wav out.wav
#include "dsp.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using namespace loopsmaster;

static const double kRate = 44100.0;
static const double kPi = 3.14159265358979323846;
static int failures = 0;

static void check (bool ok, const char* what, double value)
{
	std::printf ("%s  %-52s %.4f\n", ok ? "PASS" : "FAIL", what, value);
	if (!ok)
		++failures;
}

// Goertzel: the level of one frequency, skipping the start-up transient.
static double levelAt (const std::vector<float>& x, double hz)
{
	const size_t start = x.size () / 4;
	const double w = 2.0 * kPi * hz / kRate;
	const double c = 2.0 * std::cos (w);
	double s1 = 0.0, s2 = 0.0;
	for (size_t i = start; i < x.size (); ++i)
	{
		const double s = x[i] + c * s1 - s2;
		s2 = s1;
		s1 = s;
	}
	const double n = static_cast<double> (x.size () - start);
	return 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / n;
}

static std::vector<float> sine (double hz, double amp, double seconds)
{
	std::vector<float> v (static_cast<size_t> (seconds * kRate));
	for (size_t i = 0; i < v.size (); ++i)
		v[i] = static_cast<float> (amp * std::sin (2.0 * kPi * hz * static_cast<double> (i) / kRate));
	return v;
}

static std::vector<float> run (const std::vector<float>& input, Settings s)
{
	Engine e;
	e.update (s);
	e.prepare (kRate);
	std::vector<float> l = input, r = input;
	for (size_t pos = 0; pos < l.size (); pos += 256)
	{
		const int n = static_cast<int> (std::min<size_t> (256, l.size () - pos));
		e.update (s);
		e.process (l.data () + pos, r.data () + pos, l.data () + pos, r.data () + pos, n);
	}
	return l;
}

static Settings wetOnly ()
{
	Settings s;
	s.dry = 0.0;
	s.bodyLevel = s.subLevel = s.deepLevel = 0.0;
	s.subDrive = s.deepDrive = s.bodyDrive = 0.0;
	s.outputDb = 0.0;
	s.subTone = s.deepTone = 0.6;
	return s;
}

static bool readWav (const char* path, std::vector<float>& l, std::vector<float>& r, uint32_t& rate)
{
	FILE* f = std::fopen (path, "rb");
	if (!f)
		return false;
	std::vector<uint8_t> b;
	uint8_t buf[65536];
	size_t got;
	while ((got = std::fread (buf, 1, sizeof buf, f)) > 0)
		b.insert (b.end (), buf, buf + got);
	std::fclose (f);
	uint16_t channels = 0, bits = 0;
	for (size_t p = 12; p + 8 <= b.size ();)
	{
		uint32_t size;
		std::memcpy (&size, &b[p + 4], 4);
		if (!std::memcmp (&b[p], "fmt ", 4))
		{
			std::memcpy (&channels, &b[p + 10], 2);
			std::memcpy (&rate, &b[p + 12], 4);
			std::memcpy (&bits, &b[p + 22], 2);
		}
		else if (!std::memcmp (&b[p], "data", 4) && bits == 16 && channels == 2)
		{
			const size_t frames = size / 4;
			l.resize (frames);
			r.resize (frames);
			for (size_t i = 0; i < frames; ++i)
			{
				int16_t a, c;
				std::memcpy (&a, &b[p + 8 + i * 4], 2);
				std::memcpy (&c, &b[p + 10 + i * 4], 2);
				l[i] = a / 32768.0f;
				r[i] = c / 32768.0f;
			}
			return true;
		}
		p += 8 + size + (size & 1);
	}
	return false;
}

static bool writeWav (const char* path, const std::vector<float>& l, const std::vector<float>& r, uint32_t rate)
{
	FILE* f = std::fopen (path, "wb");
	if (!f)
		return false;
	const uint32_t dataSize = static_cast<uint32_t> (l.size () * 4), riffSize = 36 + dataSize, fmtSize = 16, byteRate = rate * 4;
	const uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
	std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riffSize, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
	std::fwrite (&fmtSize, 4, 1, f); std::fwrite (&pcm, 2, 1, f); std::fwrite (&channels, 2, 1, f);
	std::fwrite (&rate, 4, 1, f); std::fwrite (&byteRate, 4, 1, f); std::fwrite (&align, 2, 1, f);
	std::fwrite (&bits, 2, 1, f); std::fwrite ("data", 1, 4, f); std::fwrite (&dataSize, 4, 1, f);
	for (size_t i = 0; i < l.size (); ++i)
	{
		const int16_t a = static_cast<int16_t> (std::clamp (l[i], -1.0f, 1.0f) * 32767.0f);
		const int16_t b = static_cast<int16_t> (std::clamp (r[i], -1.0f, 1.0f) * 32767.0f);
		std::fwrite (&a, 2, 1, f);
		std::fwrite (&b, 2, 1, f);
	}
	std::fclose (f);
	return true;
}

#include "master_tests.inl"

int main (int argc, char** argv)
{
	if (argc == 4 && std::strcmp (argv[3], "master") == 0)
		return renderMaster (argv[1], argv[2]);
	if (argc == 3)
	{
		std::vector<float> l, r;
		uint32_t rate = 44100;
		if (!readWav (argv[1], l, r, rate))
		{
			std::printf ("could not read %s (needs 16-bit stereo)\n", argv[1]);
			return 2;
		}
		Settings s;
		Engine e;
		e.update (s);
		e.prepare (rate);
		e.process (l.data (), r.data (), l.data (), r.data (), static_cast<int> (l.size ()));
		return writeWav (argv[2], l, r, rate) ? 0 : 2;
	}

	const auto tone = sine (110.0, 0.5, 4.0);

	{
		Settings s = wetOnly ();
		s.subLevel = 1.0;
		auto y = run (tone, s);
		const double sub = levelAt (y, 55.0);
		check (sub > 0.1, "Sub: strong energy one octave down (55 Hz)", sub);
		check (sub > 4.0 * levelAt (y, 27.5), "Sub: stronger at 55 Hz than two octaves down", levelAt (y, 27.5));
	}
	{
		Settings s = wetOnly ();
		s.deepLevel = 1.0;
		s.deepTone = 0.3;
		auto y = run (tone, s);
		const double deep = levelAt (y, 27.5);
		check (deep > 0.05, "Deep: strong energy two octaves down (27.5 Hz)", deep);
		check (deep > 3.0 * levelAt (y, 82.5), "Deep: 27.5 Hz well above its 82.5 Hz product", levelAt (y, 82.5));
		check (deep > 3.0 * levelAt (y, 55.0), "Deep: 27.5 Hz well above 55 Hz", levelAt (y, 55.0));
		double step = 0.0;
		for (size_t i = y.size () / 4; i + 1 < y.size (); ++i)
			step = std::max (step, static_cast<double> (std::fabs (y[i + 1] - y[i])));
		check (step < 0.02, "Deep: no clicks (largest sample-to-sample jump)", step);
	}
	{
		// Several notes across the bass range, so the result is not tuned to one frequency.
		for (double note : {41.2, 55.0, 82.41, 110.0, 146.83})
		{
			const auto input = sine (note, 0.5, 8.0);
			char name[96];

			Settings s = wetOnly ();
			s.crossoverHz = 200.0;
			s.subLevel = 1.0;
			s.subTone = 0.3;
			auto y = run (input, s);
			const double wanted = levelAt (y, note / 2.0);
			const double unwanted = levelAt (y, note * 1.5);
			std::snprintf (name, sizeof name, "Sub  on %6.2f Hz: octave down, 3x product ratio", note);
			check (wanted > 0.05 && wanted > 3.0 * unwanted, name, wanted / std::max (unwanted, 1e-9));

			s = wetOnly ();
			s.crossoverHz = 200.0;
			s.deepLevel = 1.0;
			s.deepTone = 0.3;
			y = run (input, s);
			const double deepWanted = levelAt (y, note / 4.0);
			const double deepUnwanted = levelAt (y, note * 0.75);
			std::snprintf (name, sizeof name, "Deep on %6.2f Hz: two octaves down, 3x product ratio", note);
			check (deepWanted > 0.03 && deepWanted > 3.0 * deepUnwanted, name, deepWanted / std::max (deepUnwanted, 1e-9));
		}
	}
	{
		// Real bass is rich in harmonics. The tracker has to follow the note, not its overtones.
		for (double note : {41.2, 49.0, 55.0, 65.41, 73.42, 82.41, 110.0})
		{
			std::vector<float> saw (static_cast<size_t> (6.0 * kRate));
			double phase = 0.0;
			double smooth = 0.0;
			for (auto& v : saw)
			{
				phase += note / kRate;
				phase -= std::floor (phase);
				smooth += 0.08 * ((2.0 * phase - 1.0) - smooth); // a mellow, filtered bass tone
				v = static_cast<float> (0.5 * smooth);
			}
			Settings s = wetOnly ();
			s.crossoverHz = 110.0;
			s.subLevel = 1.0;
			s.subTone = 0.3;
			Engine e;
			e.update (s);
			e.prepare (kRate);
			std::vector<float> l = saw, r = saw;
			for (size_t pos = 0; pos < l.size (); pos += 256)
			{
				const int n = static_cast<int> (std::min<size_t> (256, l.size () - pos));
				e.process (l.data () + pos, r.data () + pos, l.data () + pos, r.data () + pos, n);
			}
			char name[96];
			const double heard = e.scope.noteHz.load ();
			std::snprintf (name, sizeof name, "Saw %6.2f Hz: tracker reads the right note", note);
			check (std::fabs (heard / note - 1.0) < 0.03, name, heard);
			const double wanted = levelAt (l, note / 2.0);
			const double oneUp = levelAt (l, note);
			std::snprintf (name, sizeof name, "Saw %6.2f Hz: Sub lands an octave down", note);
			check (wanted > 0.03 && wanted > 2.0 * oneUp, name, wanted / std::max (oneUp, 1e-9));
		}
	}
	{
		// A melody of short notes with a kick on each beat. Near the end of each note,
		// after the kick has passed, the readout should name that note.
		const double notes[] = {55.0, 73.42, 49.0, 82.41, 65.41, 55.0, 73.42, 49.0};
		Settings s;
		Engine e;
		e.update (s);
		e.prepare (kRate);
		int correct = 0;
		const int length = static_cast<int> (0.5 * kRate);
		std::vector<float> l (length), r (length);
		for (int round = 0; round < 2; ++round)
			for (double note : notes)
			{
				double phase = 0.0, smooth = 0.0, kickPhase = 0.0;
				for (int i = 0; i < length; ++i)
				{
					const double t = i / kRate;
					phase += note / kRate;
					phase -= std::floor (phase);
					smooth += 0.08 * ((2.0 * phase - 1.0) - smooth);
					kickPhase += (52.0 + 110.0 * std::exp (-t * 38.0)) / kRate;
					const double kick = t < 0.22 ? 0.5 * std::sin (2.0 * kPi * kickPhase) * std::exp (-t * 15.0) : 0.0;
					l[i] = r[i] = static_cast<float> (0.4 * smooth + kick);
				}
				for (int pos = 0; pos < length; pos += 256)
				{
					const int n = std::min (256, length - pos);
					e.process (l.data () + pos, r.data () + pos, l.data () + pos, r.data () + pos, n);
				}
				const double shown = e.scope.noteHz.load ();
				if (std::fabs (shown / note - 1.0) < 0.03)
					++correct;
			}
		check (correct == 16, "Melody with kick: readout right at the end of each note", correct);
	}
	{
		Settings s = wetOnly ();
		s.subLevel = 0.0;
		auto y = run (tone, s);
		double peak = 0.0;
		for (float v : y)
			peak = std::max (peak, static_cast<double> (std::fabs (v)));
		check (peak < 1e-6, "All layers and dry off: silence", peak);
	}
	{
		Settings s = wetOnly ();
		s.bodyLevel = 1.0;
		s.crossoverHz = 200.0;
		s.bodyTone = 1.0;
		const auto high = sine (2000.0, 0.5, 2.0);
		const double passed = levelAt (run (tone, s), 110.0);
		const double rejected = levelAt (run (high, s), 2000.0);
		check (passed > 0.25, "Crossover: lows pass through Body", passed);
		check (rejected < 0.01, "Crossover: 2 kHz is rejected from Body", rejected);
	}
	{
		Settings s;
		s.bypass = true;
		auto y = run (tone, s);
		double worst = 0.0;
		const size_t lag = static_cast<size_t> (Engine::latencyAt (kRate));
		for (size_t i = 0; i + lag < y.size (); ++i)
			worst = std::max (worst, static_cast<double> (std::fabs (y[i + lag] - tone[i])));
		check (worst < 1e-6, "Bypass: identical to input, after the reported latency", worst);
	}
	{
		Settings s = wetOnly ();
		s.dry = 1.0;
		auto y = run (tone, s);
		double worst = 0.0;
		const size_t lag = static_cast<size_t> (Engine::latencyAt (kRate));
		for (size_t i = 0; i + lag < y.size (); ++i)
			worst = std::max (worst, static_cast<double> (std::fabs (y[i + lag] - tone[i])));
		check (worst < 1e-5, "Dry only, master stages off: identical after latency", worst);
	}
	{
		// A sudden NaN must not reach the output or stop later audio.
		auto bad = tone;
		bad[2000] = std::numeric_limits<float>::quiet_NaN ();
		Settings s;
		auto y = run (bad, s);
		bool finite = true;
		for (float v : y)
			finite = finite && std::isfinite (v);
		check (finite, "NaN in input: output stays finite", finite ? 1.0 : 0.0);
		check (levelAt (y, 110.0) > 0.1, "NaN in input: audio recovers afterwards", levelAt (y, 110.0));
	}
	{
		// Largest toggle step in the sub signal, as a click check.
		Settings s = wetOnly ();
		s.subLevel = 1.0;
		auto y = run (tone, s);
		double step = 0.0;
		for (size_t i = y.size () / 4; i + 1 < y.size (); ++i)
			step = std::max (step, static_cast<double> (std::fabs (y[i + 1] - y[i])));
		check (step < 0.02, "Sub: no clicks (largest sample-to-sample jump)", step);
	}
	{
		Settings s;
		s.bodyLevel = s.subLevel = s.deepLevel = 1.0;
		s.bodyDrive = s.subDrive = s.deepDrive = 1.0;
		s.outputDb = 0.0;
		auto y = run (sine (55.0, 1.0, 4.0), s);
		double peak = 0.0;
		for (float v : y)
			peak = std::max (peak, static_cast<double> (std::fabs (v)));
		check (std::isfinite (peak) && peak < 6.0, "Everything maxed: level stays bounded", peak);
	}

	splitTests ();
	suiteTests ();
	studyTests ();
	masterTests ();
	peakDiagnostics ();

	std::printf (failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
	return failures ? 1 : 0;
}
