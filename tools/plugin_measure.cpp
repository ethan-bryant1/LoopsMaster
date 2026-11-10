// Black-box measurements: sends test signals through a VST3 plugin through its public interface and
// reports what comes out. Nothing about the plugin's insides is read.
// Usage: plugin_measure <plugin.vst3> <static|response|step|latency> <hz> [id=normalized ...]
//   static    tone at rising levels: level in -> gain, output peak, 3rd harmonic
//   response  gain at log-spaced frequencies with a -30 dB tone
//   step      tone jumping -40 -> -10 -> -40 dB: gain against time
//   latency   reported latency, and where an impulse actually comes out
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

const double kRate = 44100.0;
const double kPi = 3.14159265358979323846;
const int kBlock = 512;

double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-9)); }

class Rig
{
public:
	IComponent* component = nullptr;
	IAudioProcessor* processor = nullptr;
	std::map<ParamID, double> settings;

	bool prepare ()
	{
		ProcessSetup setup {kOffline, kSample32, kBlock, kRate};
		if (processor->setupProcessing (setup) != kResultOk)
			return false;
		inputs = component->getBusCount (kAudio, kInput);
		outputs = component->getBusCount (kAudio, kOutput);
		for (int32 i = 0; i < inputs; ++i)
			component->activateBus (kAudio, kInput, i, true);
		for (int32 i = 0; i < outputs; ++i)
			component->activateBus (kAudio, kOutput, i, true);
		return component->setActive (true) == kResultOk;
	}

	// Stereo in, stereo out. Any other input (a sidechain) gets silence.
	std::vector<float> render (const std::vector<float>& input)
	{
		processor->setProcessing (true);
		std::vector<std::vector<float>> in (2 * static_cast<size_t> (inputs), std::vector<float> (kBlock, 0.f));
		std::vector<std::vector<float>> out (2 * static_cast<size_t> (outputs), std::vector<float> (kBlock, 0.f));
		std::vector<float*> inPtr, outPtr;
		for (auto& c : in)
			inPtr.push_back (c.data ());
		for (auto& c : out)
			outPtr.push_back (c.data ());
		std::vector<AudioBusBuffers> inBus (static_cast<size_t> (inputs)), outBus (static_cast<size_t> (outputs));
		for (int32 i = 0; i < inputs; ++i)
		{
			inBus[static_cast<size_t> (i)].numChannels = 2;
			inBus[static_cast<size_t> (i)].channelBuffers32 = &inPtr[2 * static_cast<size_t> (i)];
		}
		for (int32 i = 0; i < outputs; ++i)
		{
			outBus[static_cast<size_t> (i)].numChannels = 2;
			outBus[static_cast<size_t> (i)].channelBuffers32 = &outPtr[2 * static_cast<size_t> (i)];
		}
		std::vector<float> result (input.size (), 0.f);
		for (size_t pos = 0; pos + kBlock <= input.size (); pos += kBlock)
		{
			for (int i = 0; i < kBlock; ++i)
				in[0][static_cast<size_t> (i)] = in[1][static_cast<size_t> (i)] = input[pos + static_cast<size_t> (i)];
			ParameterChanges changes, outChanges;
			if (pos == 0)
				for (auto& s : settings)
				{
					int32 slot = 0, point = 0;
					if (auto* q = changes.addParameterData (s.first, slot))
						q->addPoint (0, s.second, point);
				}
			ProcessData data {};
			data.processMode = kOffline;
			data.symbolicSampleSize = kSample32;
			data.numSamples = kBlock;
			data.numInputs = inputs;
			data.numOutputs = outputs;
			data.inputs = inBus.data ();
			data.outputs = outBus.data ();
			data.inputParameterChanges = &changes;
			data.outputParameterChanges = &outChanges;
			processor->process (data);
			std::memcpy (result.data () + pos, out[0].data (), sizeof (float) * kBlock);
		}
		processor->setProcessing (false);
		return result;
	}

	int32 inputs = 0, outputs = 0;
};

double toneLevel (const std::vector<float>& x, size_t from, size_t to, double hz)
{
	double re = 0.0, im = 0.0;
	for (size_t i = from; i < to; ++i)
	{
		const double w = 2.0 * kPi * hz * static_cast<double> (i) / kRate;
		re += x[i] * std::cos (w);
		im += x[i] * std::sin (w);
	}
	return 2.0 * std::sqrt (re * re + im * im) / static_cast<double> (to - from);
}

double peak (const std::vector<float>& x, size_t from, size_t to)
{
	double p = 0.0;
	for (size_t i = from; i < to; ++i)
		p = std::max (p, static_cast<double> (std::fabs (x[i])));
	return p;
}

std::vector<float> tone (double hz, double amp, double seconds)
{
	std::vector<float> x (static_cast<size_t> (seconds * kRate));
	for (size_t i = 0; i < x.size (); ++i)
		x[i] = static_cast<float> (amp * std::sin (2.0 * kPi * hz * static_cast<double> (i) / kRate));
	return x;
}

// Whole cycles only, so the tone reading isn't smeared.
size_t wholeCycles (size_t from, size_t to, double hz)
{
	const double cycle = kRate / hz;
	return from + static_cast<size_t> (std::floor (static_cast<double> (to - from) / cycle) * cycle);
}

} // namespace

int main (int argc, char** argv)
{
	if (argc < 4)
		return 2;
	std::string error;
	auto module = VST3::Hosting::Module::create (argv[1], error);
	if (!module)
	{
		std::printf ("load failed: %s\n", error.c_str ());
		return 1;
	}
	const std::string mode = argv[2];
	const double hz = std::atof (argv[3]);
	HostApplication host;
	auto factory = module->getFactory ();
	factory.setHostContext (&host);
	IPtr<IComponent> component;
	for (auto& info : factory.classInfos ())
		if (!component && info.category () == kVstAudioEffectClass)
			component = factory.createInstance<IComponent> (info.ID ());
	if (!component || component->initialize (&host) != kResultOk)
		return 1;
	FUnknownPtr<IAudioProcessor> processor (component);
	Rig rig;
	rig.component = component;
	rig.processor = processor;
	for (int i = 4; i < argc; ++i)
	{
		const std::string arg = argv[i];
		const auto eq = arg.find ('=');
		if (eq != std::string::npos)
			rig.settings[static_cast<ParamID> (std::strtoul (arg.substr (0, eq).c_str (), nullptr, 10))] = std::atof (arg.substr (eq + 1).c_str ());
	}
	if (!rig.prepare ())
	{
		std::printf ("could not start\n");
		return 1;
	}

	if (mode == "static")
	{
		for (double level = -80.0; level <= 12.01; level += level < -20.0 ? 10.0 : 3.0)
		{
			const auto out = rig.render (tone (hz, std::pow (10.0, level / 20.0), 2.0));
			const size_t from = static_cast<size_t> (1.5 * kRate), to = wholeCycles (from, out.size () - kBlock, hz);
			const double fundamental = toneLevel (out, from, to, hz);
			std::printf ("in %6.1f dB  gain %+7.2f dB  peak out %+7.2f dB  3rd harmonic %+7.1f dB\n", level, db (fundamental) - level, db (peak (out, from, to)),
			             db (toneLevel (out, from, to, 3.0 * hz)) - db (fundamental));
		}
	}
	else if (mode == "response")
	{
		const double amp = std::pow (10.0, -30.0 / 20.0);
		for (double f = 20.0; f < 20001.0; f *= std::pow (2.0, 1.0 / 3.0))
		{
			const auto out = rig.render (tone (f, amp, 1.0));
			const size_t from = static_cast<size_t> (0.6 * kRate), to = wholeCycles (from, out.size () - kBlock, f);
			std::printf ("%8.1f Hz  %+7.2f dB\n", f, db (toneLevel (out, from, to, f)) + 30.0);
		}
	}
	else if (mode == "step")
	{
		const double quiet = std::pow (10.0, -40.0 / 20.0), loud = std::pow (10.0, -10.0 / 20.0);
		auto x = tone (hz, 1.0, 6.0);
		const size_t a = static_cast<size_t> (2.0 * kRate), b = static_cast<size_t> (4.0 * kRate);
		for (size_t i = 0; i < x.size (); ++i)
			x[i] *= static_cast<float> (i >= a && i < b ? loud : quiet);
		const auto out = rig.render (x);
		const size_t frame = static_cast<size_t> (kRate / hz * std::max (1.0, std::ceil (hz * 0.005)));
		const auto gainAt = [&] (double t, double in) {
			const size_t from = static_cast<size_t> (t * kRate) + static_cast<size_t> (processor->getLatencySamples ());
			return db (toneLevel (out, from, from + frame, hz)) - db (in);
		};
		std::printf ("settled quiet %+.2f dB, settled loud %+.2f dB\n", gainAt (1.9, quiet), gainAt (3.9, loud));
		std::printf ("after jump up (ms: gain)  ");
		for (double ms : {1.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0})
			std::printf ("%g: %+.1f  ", ms, gainAt (2.0 + ms / 1000.0, loud));
		std::printf ("\nafter jump down (ms: gain)  ");
		for (double ms : {1.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0})
			std::printf ("%g: %+.1f  ", ms, gainAt (4.0 + ms / 1000.0, quiet));
		std::printf ("\n");
	}
	else if (mode == "latency")
	{
		// Parameters reach the plugin on the first block, so let it take them before asking.
		rig.render (std::vector<float> (static_cast<size_t> (kBlock) * 4, 0.f));
		std::printf ("reported %d samples\n", processor->getLatencySamples ());
		std::vector<float> x (static_cast<size_t> (kRate), 0.f);
		x[static_cast<size_t> (0.5 * kRate)] = 0.25f;
		const auto out = rig.render (x);
		size_t at = 0;
		for (size_t i = 0; i < out.size (); ++i)
			if (std::fabs (out[i]) > std::fabs (out[at]))
				at = i;
		std::printf ("impulse came out %d samples late\n", static_cast<int> (at) - static_cast<int> (0.5 * kRate));
	}
	component->setActive (false);
	component->terminate ();
	std::fflush (stdout);
	return 0;
}
