// Loads a VST3 plugin the way a host does and prints what it publishes to hosts: its class details,
// buses, every parameter with its range text, latency, tail and the size of a saved state.
// It uses only the public VST3 interfaces and never opens the plugin's window.
// Usage: plugin_probe <plugin.vst3>
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// Counts what a plugin saves. The contents are not kept or read.
class CountingStream : public IBStream
{
public:
	tresult PLUGIN_API queryInterface (const TUID, void** obj) SMTG_OVERRIDE
	{
		*obj = nullptr;
		return kNoInterface;
	}
	uint32 PLUGIN_API addRef () SMTG_OVERRIDE { return 1; }
	uint32 PLUGIN_API release () SMTG_OVERRIDE { return 1; }
	tresult PLUGIN_API read (void*, int32, int32* done) SMTG_OVERRIDE
	{
		if (done)
			*done = 0;
		return kResultFalse;
	}
	tresult PLUGIN_API write (void*, int32 count, int32* done) SMTG_OVERRIDE
	{
		size += count;
		if (done)
			*done = count;
		return kResultOk;
	}
	tresult PLUGIN_API seek (int64, int32, int64* result) SMTG_OVERRIDE
	{
		if (result)
			*result = size;
		return kResultOk;
	}
	tresult PLUGIN_API tell (int64* pos) SMTG_OVERRIDE
	{
		if (pos)
			*pos = size;
		return kResultOk;
	}
	int64 size = 0;
};

std::string text (const TChar* s)
{
	return VST3::StringConvert::convert (s);
}

void printBuses (IComponent* component, MediaType media, BusDirection direction, const char* label)
{
	const int32 count = component->getBusCount (media, direction);
	for (int32 i = 0; i < count; ++i)
	{
		BusInfo info {};
		if (component->getBusInfo (media, direction, i, info) == kResultOk)
			std::printf ("bus\t%s\t%d\t%s\tchannels=%d\ttype=%s\tdefaultActive=%d\n", label, i, text (info.name).c_str (), info.channelCount,
			             info.busType == kMain ? "main" : "aux", (info.flags & BusInfo::kDefaultActive) ? 1 : 0);
	}
}

} // namespace

int main (int argc, char** argv)
{
	if (argc != 2)
		return 2;
	std::string error;
	auto module = VST3::Hosting::Module::create (argv[1], error);
	if (!module)
	{
		std::printf ("load failed\t%s\n", error.c_str ());
		return 1;
	}
	HostApplication host;
	auto factory = module->getFactory ();
	factory.setHostContext (&host);

	IPtr<IComponent> component;
	VST3::UID classId;
	for (auto& info : factory.classInfos ())
	{
		std::printf ("class\t%s\tcategory=%s\tvendor=%s\tversion=%s\tsdk=%s\tsub=%s\n", info.name ().c_str (), info.category ().c_str (), info.vendor ().c_str (),
		             info.version ().c_str (), info.sdkVersion ().c_str (), info.subCategoriesString ().c_str ());
		if (!component && info.category () == kVstAudioEffectClass)
			component = factory.createInstance<IComponent> (info.ID ());
	}
	if (!component)
	{
		std::printf ("no audio class could be created\n");
		return 1;
	}
	if (component->initialize (&host) != kResultOk)
	{
		std::printf ("initialize failed\n");
		return 1;
	}

	printBuses (component, kAudio, kInput, "audio in");
	printBuses (component, kAudio, kOutput, "audio out");
	printBuses (component, kEvent, kInput, "event in");
	printBuses (component, kEvent, kOutput, "event out");

	// Controllers are either part of the component or a separate class that has to be connected.
	FUnknownPtr<IEditController> joined (component);
	IPtr<IEditController> controller;
	bool separate = false;
	if (joined)
		controller = joined.getInterface ();
	else
	{
		TUID id;
		if (component->getControllerClassId (id) == kResultOk)
		{
			controller = factory.createInstance<IEditController> (VST3::UID::fromTUID (id));
			separate = true;
			if (controller && controller->initialize (&host) != kResultOk)
				controller = nullptr;
		}
	}
	std::printf ("controller\t%s\n", !controller ? "none" : separate ? "separate" : "joined");

	FUnknownPtr<IConnectionPoint> componentPoint (component);
	FUnknownPtr<IConnectionPoint> controllerPoint (controller);
	const bool connected = separate && componentPoint && controllerPoint;
	if (connected)
	{
		componentPoint->connect (controllerPoint);
		controllerPoint->connect (componentPoint);
	}

	FUnknownPtr<IAudioProcessor> processor (component);
	if (processor)
	{
		std::printf ("sample32\t%d\nsample64\t%d\n", processor->canProcessSampleSize (kSample32) == kResultTrue ? 1 : 0,
		             processor->canProcessSampleSize (kSample64) == kResultTrue ? 1 : 0);
		ProcessSetup setup {kOffline, kSample32, 512, 44100.0};
		const tresult ok = processor->setupProcessing (setup);
		if (ok == kResultOk && component->setActive (true) == kResultOk)
		{
			std::printf ("latency samples at 44.1k\t%d\ntail samples at 44.1k\t%d\n", processor->getLatencySamples (), processor->getTailSamples ());
			component->setActive (false);
		}
		else
			std::printf ("could not activate\n");
	}

	CountingStream state;
	if (component->getState (&state) == kResultOk)
		std::printf ("saved state bytes\t%lld\n", static_cast<long long> (state.size));

	if (controller)
	{
		const int32 count = controller->getParameterCount ();
		std::printf ("parameters\t%d\n", count);
		std::printf ("id\ttitle\tunits\tsteps\tdefault\tflags\tat 0\tat 0.25\tat 0.5\tat 0.75\tat 1\n");
		for (int32 i = 0; i < count; ++i)
		{
			ParameterInfo info {};
			if (controller->getParameterInfo (i, info) != kResultOk)
				continue;
			std::string flags;
			if (info.flags & ParameterInfo::kCanAutomate)
				flags += "auto ";
			if (info.flags & ParameterInfo::kIsReadOnly)
				flags += "readonly ";
			if (info.flags & ParameterInfo::kIsList)
				flags += "list ";
			if (info.flags & ParameterInfo::kIsBypass)
				flags += "bypass ";
			if (info.flags & ParameterInfo::kIsProgramChange)
				flags += "program ";
			std::printf ("%u\t%s\t%s\t%d\t%.4f\t%s", info.id, text (info.title).c_str (), text (info.units).c_str (), info.stepCount, info.defaultNormalizedValue, flags.c_str ());
			for (double v : {0.0, 0.25, 0.5, 0.75, 1.0})
			{
				String128 shown {};
				if (controller->getParamStringByValue (info.id, v, shown) == kResultOk)
					std::printf ("\t%s", text (shown).c_str ());
				else
					std::printf ("\t?");
			}
			std::printf ("\n");
			if (info.stepCount > 1 && info.stepCount <= 32)
			{
				std::printf ("options\t%u\t%s", info.id, text (info.title).c_str ());
				for (int32 step = 0; step <= info.stepCount; ++step)
				{
					String128 shown {};
					if (controller->getParamStringByValue (info.id, static_cast<double> (step) / info.stepCount, shown) == kResultOk)
						std::printf ("\t%s", text (shown).c_str ());
				}
				std::printf ("\n");
			}
		}
	}

	if (connected)
	{
		componentPoint->disconnect (controllerPoint);
		controllerPoint->disconnect (componentPoint);
	}
	if (separate && controller)
		controller->terminate ();
	component->terminate ();
	std::fflush (stdout);
	return 0;
}
