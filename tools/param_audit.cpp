// Measures whether each control changes the sound. Loads the built plugin the way a host does,
// renders the same audio with a control moved, and reports how far the output moved.
// Usage: param_audit <plugin.vst3> [older build.vst3]
// With an older build, it also saves a session from that build, loads it into this one, and
// checks that the two sound the same.
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

const double kRate = 44100.0;
const double kPi = 3.14159265358979323846;
const int kBlock = 512;
const int kNumParams = 94;
const int kPresetId = 1;

struct Audio
{
	std::vector<float> l, r, side;
};

// A loud bass on F1 with a kick, peaking near +2.5 dB like the mix in the user's screenshot.
Audio makeInput (bool stereo)
{
	const size_t n = static_cast<size_t> (2.0 * kRate);
	Audio a;
	a.l.resize (n);
	a.r.resize (n);
	a.side.assign (n, 0.f);
	double phase = 0.0, smooth = 0.0;
	for (size_t i = 0; i < n; ++i)
	{
		const double t = static_cast<double> (i) / kRate;
		const double beat = std::fmod (t, 0.5);
		phase += 43.65 / kRate;
		phase -= std::floor (phase);
		smooth += 0.25 * ((2.0 * phase - 1.0) - smooth);
		const double kick = std::sin (2.0 * kPi * (52.0 * beat + 3.0 * (1.0 - std::exp (-beat * 38.0)))) * std::exp (-beat * 12.0);
		const double mid = 0.12 * std::sin (2.0 * kPi * 610.0 * t) + 0.08 * std::sin (2.0 * kPi * 5200.0 * t);
		const double spread = stereo ? 0.1 * std::sin (2.0 * kPi * 1300.0 * t) + 0.1 * std::sin (2.0 * kPi * 70.0 * t) : 0.0;
		const double centre = 0.6 * smooth + 0.55 * kick + mid;
		a.l[i] = static_cast<float> (centre + spread);
		a.r[i] = static_cast<float> (centre - spread);
		a.side[i] = static_cast<float> (0.7 * kick);
	}
	return a;
}

class Rig
{
public:
	IComponent* component = nullptr;
	IAudioProcessor* processor = nullptr;
	IEditController* controller = nullptr;

	// Resets the plugin, applies the settings, and renders. useQueue sends the values the way a
	// host sends automation; otherwise they go the way the plugin's own window sets them.
	std::vector<float> render (const std::vector<double>& settings, const Audio& input, bool useQueue)
	{
		processor->setProcessing (false);
		// Empty settings keep whatever the plugin already holds, such as a loaded session.
		for (int id = 0; static_cast<size_t> (id) < settings.size (); ++id)
			if (!useQueue)
				controller->setParamNormalized (static_cast<ParamID> (id), settings[static_cast<size_t> (id)]);
		processor->setProcessing (true);

		std::vector<float> out (input.l.size () * 2);
		std::vector<float> inL (kBlock), inR (kBlock), sideL (kBlock), outL (kBlock), outR (kBlock);
		float* inPtr[2] = {inL.data (), inR.data ()};
		float* sidePtr[2] = {sideL.data (), sideL.data ()};
		float* outPtr[2] = {outL.data (), outR.data ()};
		AudioBusBuffers buses[2] {};
		buses[0].numChannels = 2;
		buses[0].channelBuffers32 = inPtr;
		buses[1].numChannels = 2;
		buses[1].channelBuffers32 = sidePtr;
		AudioBusBuffers outBus {};
		outBus.numChannels = 2;
		outBus.channelBuffers32 = outPtr;

		for (size_t pos = 0; pos + kBlock <= input.l.size (); pos += kBlock)
		{
			for (int i = 0; i < kBlock; ++i)
			{
				inL[static_cast<size_t> (i)] = input.l[pos + static_cast<size_t> (i)];
				inR[static_cast<size_t> (i)] = input.r[pos + static_cast<size_t> (i)];
				sideL[static_cast<size_t> (i)] = input.side[pos + static_cast<size_t> (i)];
			}
			ParameterChanges inChanges, outChanges;
			if (useQueue && pos == 0)
				for (int id = 0; id < kNumParams; ++id)
				{
					int32 slot = 0, point = 0;
					if (auto* q = inChanges.addParameterData (static_cast<ParamID> (id), slot))
						q->addPoint (0, settings[static_cast<size_t> (id)], point);
				}
			ProcessData data {};
			data.processMode = kOffline;
			data.symbolicSampleSize = kSample32;
			data.numSamples = kBlock;
			data.numInputs = 2;
			data.numOutputs = 1;
			data.inputs = buses;
			data.outputs = &outBus;
			data.inputParameterChanges = &inChanges;
			data.outputParameterChanges = &outChanges;
			processor->process (data);
			for (int i = 0; i < kBlock; ++i)
			{
				out[2 * (pos + static_cast<size_t> (i))] = outL[static_cast<size_t> (i)];
				out[2 * (pos + static_cast<size_t> (i)) + 1] = outR[static_cast<size_t> (i)];
			}
		}
		return out;
	}
};

double rmsOf (const std::vector<float>& x, size_t from)
{
	double sum = 0.0;
	for (size_t i = from; i < x.size (); ++i)
		sum += static_cast<double> (x[i]) * x[i];
	return std::sqrt (sum / static_cast<double> (std::max<size_t> (1, x.size () - from)));
}

// How much of the output changed, in dB relative to the output itself. -6 is a big change,
// -40 is hard to hear, and below about -80 nothing changed.
double changeDb (const std::vector<float>& a, const std::vector<float>& b)
{
	const size_t from = static_cast<size_t> (0.3 * kRate) * 2;
	double diff = 0.0, base = 0.0;
	for (size_t i = from; i < a.size (); ++i)
	{
		const double d = static_cast<double> (a[i]) - b[i];
		diff += d * d;
		base += static_cast<double> (a[i]) * a[i];
	}
	return 10.0 * std::log10 (std::max (diff, 1e-24) / std::max (base, 1e-24));
}

const char* verdict (double db)
{
	return db < -80.0 ? "NOTHING" : db < -40.0 ? "barely" : db < -20.0 ? "subtle" : "clear";
}

void audit (Rig& rig, const char* title, const std::vector<double>& settings, const Audio& input, bool useQueue)
{
	std::printf ("\n%s\n", title);
	const auto base = rig.render (settings, input, useQueue);
	const auto again = rig.render (settings, input, useQueue);
	double peak = 0.0;
	for (float v : base)
		peak = std::max (peak, static_cast<double> (std::fabs (v)));
	std::printf ("output %.1f dB peak, %.1f dB rms. Same settings twice differ by %.0f dB (should be silent).\n",
	             20.0 * std::log10 (std::max (peak, 1e-9)), 20.0 * std::log10 (std::max (rmsOf (base, 26000), 1e-9)), changeDb (base, again));
	std::printf ("%-16s %7s %12s %10s %12s\n", "control", "now", "full range", "verdict", "10%% nudge");
	for (int id = 0; id < kNumParams; ++id)
	{
		if (id == kPresetId)
			continue;
		ParameterInfo info {};
		rig.controller->getParameterInfo (id, info);
		const std::string name = VST3::StringConvert::convert (info.title);
		const double now = settings[static_cast<size_t> (id)];
		double worst = -300.0;
		for (double target : {0.0, 1.0})
		{
			auto moved = settings;
			moved[static_cast<size_t> (id)] = target;
			if (std::fabs (target - now) > 1e-9)
				worst = std::max (worst, changeDb (base, rig.render (moved, input, useQueue)));
		}
		auto nudged = settings;
		nudged[static_cast<size_t> (id)] = now <= 0.9 ? now + 0.1 : now - 0.1;
		const double nudge = info.stepCount == 1 ? -300.0 : changeDb (base, rig.render (nudged, input, useQueue));
		std::printf ("%-16s %6.0f%% %9.1f dB %10s %9.1f dB\n", name.c_str (), now * 100.0, worst, verdict (worst), nudge);
	}
}

// A session file in memory.
class Stream : public IBStream
{
public:
	tresult PLUGIN_API queryInterface (const TUID, void** obj) SMTG_OVERRIDE
	{
		*obj = nullptr;
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef () SMTG_OVERRIDE { return 1; }
	uint32 PLUGIN_API release () SMTG_OVERRIDE { return 1; }
	tresult PLUGIN_API read (void* buffer, int32 count, int32* done) SMTG_OVERRIDE
	{
		const int32 n = std::min (count, static_cast<int32> (bytes.size () - at));
		if (n > 0)
			std::memcpy (buffer, bytes.data () + at, static_cast<size_t> (n));
		at += static_cast<size_t> (std::max (n, 0));
		if (done)
			*done = std::max (n, 0);
		return kResultOk;
	}
	tresult PLUGIN_API write (void* buffer, int32 count, int32* done) SMTG_OVERRIDE
	{
		const auto* p = static_cast<const char*> (buffer);
		bytes.insert (bytes.end (), p, p + count);
		at = bytes.size ();
		if (done)
			*done = count;
		return kResultOk;
	}
	tresult PLUGIN_API seek (int64 pos, int32 mode, int64* result) SMTG_OVERRIDE
	{
		const int64 base = mode == kIBSeekSet ? 0 : mode == kIBSeekCur ? static_cast<int64> (at) : static_cast<int64> (bytes.size ());
		at = static_cast<size_t> (std::clamp<int64> (base + pos, 0, static_cast<int64> (bytes.size ())));
		if (result)
			*result = static_cast<int64> (at);
		return kResultOk;
	}
	tresult PLUGIN_API tell (int64* pos) SMTG_OVERRIDE
	{
		if (pos)
			*pos = static_cast<int64> (at);
		return kResultOk;
	}
	void rewind () { at = 0; }
	std::vector<char> bytes;
	size_t at = 0;
};

int failures = 0;

void report (bool ok, const char* name, double value)
{
	std::printf ("%s  %-58s %.4f\n", ok ? "PASS" : "FAIL", name, value);
	if (!ok)
		++failures;
}

// Saves a session from the older build, loads it here, and compares the sound.
void upgradeTest (Rig& rig, const char* olderPath, FUnknown* host, const std::vector<double>& settings, const std::vector<double>& fresh, const Audio& input)
{
	std::printf ("\n=== SESSION SAVED BY AN OLDER BUILD ===\n");
	std::string error;
	auto module = VST3::Hosting::Module::create (olderPath, error);
	if (!module)
	{
		report (false, "Older build loads", 0.0);
		return;
	}
	auto factory = module->getFactory ();
	factory.setHostContext (host);
	IPtr<IComponent> older;
	for (auto& info : factory.classInfos ())
		if (info.category () == kVstAudioEffectClass)
			older = factory.createInstance<IComponent> (info.ID ());
	if (!older || older->initialize (host) != kResultOk)
	{
		report (false, "Older build starts", 0.0);
		return;
	}
	FUnknownPtr<IAudioProcessor> oldProcessor (older);
	FUnknownPtr<IEditController> oldController (older);
	ProcessSetup setup {kOffline, kSample32, kBlock, kRate};
	oldProcessor->setupProcessing (setup);
	older->activateBus (kAudio, kInput, 0, true);
	older->activateBus (kAudio, kInput, 1, true);
	older->activateBus (kAudio, kOutput, 0, true);
	older->setActive (true);
	Rig oldRig;
	oldRig.component = older;
	oldRig.processor = oldProcessor;
	oldRig.controller = oldController;

	const int oldCount = oldController->getParameterCount ();
	std::printf ("older build has %d controls, this one has %d\n", oldCount, rig.controller->getParameterCount ());
	const std::vector<double> oldSettings (settings.begin (), settings.begin () + oldCount);
	const auto oldOut = oldRig.render (oldSettings, input, false);
	Stream saved;
	report (older->getState (&saved) == kResultOk, "Older build saves a session (bytes)", static_cast<double> (saved.bytes.size ()));

	saved.rewind ();
	report (rig.component->setState (&saved) == kResultOk, "This build loads it", 0.0);
	double worstValue = 0.0, worstNew = 0.0;
	for (int id = 0; id < kNumParams; ++id)
	{
		const double now = rig.controller->getParamNormalized (static_cast<ParamID> (id));
		// EQ shape and Q are renumbered on purpose when a 1.3 session loads, to keep its sound.
		const bool remapped = id >= 51 && id < 71 && ((id - 51) % 4 == 0 || (id - 51) % 4 == 3);
		if (remapped)
			continue;
		if (id < oldCount)
			worstValue = std::max (worstValue, std::fabs (now - settings[static_cast<size_t> (id)]));
		else
			worstNew = std::max (worstNew, std::fabs (now - fresh[static_cast<size_t> (id)]));
	}
	report (worstValue < 1e-9, "Existing controls keep their values (largest change)", worstValue);
	report (worstNew < 1e-9, "New controls start at their defaults (largest change)", worstNew);

	const auto newOut = rig.render ({}, input, false);
	const double change = changeDb (oldOut, newOut);
	report (change < -70.0, "It sounds the same as the older build (dB difference)", change);

	Stream current;
	rig.component->getState (&current);
	current.rewind ();
	rig.component->setState (&current);
	const double again = changeDb (newOut, rig.render ({}, input, false));
	report (again < -70.0, "Saving and reloading in this build changes nothing (dB)", again);

	oldProcessor->setProcessing (false);
	older->setActive (false);
	older->terminate ();
}

} // namespace

int main (int argc, char** argv)
{
	if (argc < 2)
		return 2;
	std::string error;
	auto module = VST3::Hosting::Module::create (argv[1], error);
	if (!module)
	{
		std::printf ("could not load: %s\n", error.c_str ());
		return 1;
	}
	HostApplication host;
	auto factory = module->getFactory ();
	factory.setHostContext (&host);
	IPtr<IComponent> component;
	for (auto& info : factory.classInfos ())
		if (info.category () == kVstAudioEffectClass)
			component = factory.createInstance<IComponent> (info.ID ());
	if (!component || component->initialize (&host) != kResultOk)
		return 1;
	FUnknownPtr<IAudioProcessor> processor (component);
	FUnknownPtr<IEditController> controller (component);

	ProcessSetup setup {kOffline, kSample32, kBlock, kRate};
	processor->setupProcessing (setup);
	component->activateBus (kAudio, kInput, 0, true);
	component->activateBus (kAudio, kInput, 1, true);
	component->activateBus (kAudio, kOutput, 0, true);
	component->setActive (true);

	Rig rig;
	rig.component = component;
	rig.processor = processor;
	rig.controller = controller;

	std::vector<double> fresh (kNumParams);
	for (int id = 0; id < kNumParams; ++id)
		fresh[static_cast<size_t> (id)] = controller->getParamNormalized (static_cast<ParamID> (id));

	// Values read from the user's two screenshots. Limit is off and Duck depth is 0.
	std::vector<double> session = {0, 0, 0.302, 0.5, 0.39, 0.22, 0.26, 0, 0.5, 0, 0, 0.5, 0, 1, 1, 0.6,
	                               0, 0.5, 0.74, 0.47, 1.0 / 1.5, 0, 0, 0.42, 0.55, 0.61, 0,
	                               1, 0.258, 0.26, 0,
	                               0, 0, 5.0 / 6.0, 0.589,
	                               0.5, 1, 1, 0, 1, 1, 1, 0,
	                               0, 0.5, 0.344, 0.4406, 0.5, 0.5, 0.5, 0,
	                               0, 0.159, 0.5, 0.411, 0, 0.333, 0.5, 0.411, 0, 0.534, 0.5, 0.411, 0, 0.725, 0.5, 0.411, 0, 0.9, 0.5, 0.411,
	                               0.5, 0.5, 0.5, 0, 0, 0, 0, 20.0 / 26.0, 0, 0, 0, 0, 0,
	                               0.5, 0.5, 0.5, 0.5, 0.5, 0, 0, 0, 0, 0};
	audit (rig, "=== YOUR SETTINGS, mono mix, no kick on the sidechain, sent the way the window sends them ===", session, [] { Audio a = makeInput (false); std::fill (a.side.begin (), a.side.end (), 0.f); return a; } (), false);

	// Every stage active, so each control has something to act on.
	auto active = session;
	active[7] = 0.5;   // Sub level
	active[10] = 0.4;  // Deep level
	active[16] = 0.8;  // Duck depth
	active[21] = 0.6;  // Mono below
	active[22] = 0.3;  // Squash
	active[26] = 0.2;  // Heat
	active[28] = 0.125;// Clip drive 3 dB
	active[30] = 0.3;  // Clean below
	active[31] = 1.0;  // Limit on
	active[32] = 0.25; // Push 3 dB
	active[43] = 0.3;  // Squash up
	for (int band = 0; band < 5; ++band)
		active[static_cast<size_t> (51 + band * 4 + 2)] = 0.62; // each EQ band at about +4 dB
	audit (rig, "=== ALL STAGES ACTIVE, stereo mix, kick on the sidechain, sent the way a host sends automation ===", active, makeInput (true), true);

	if (argc > 2)
		upgradeTest (rig, argv[2], &host, active, fresh, makeInput (true));

	processor->setProcessing (false);
	component->setActive (false);
	component->terminate ();
	return failures == 0 ? 0 : 1;
}
