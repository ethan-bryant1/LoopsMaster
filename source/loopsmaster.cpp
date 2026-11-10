// LoopsMaster VST3 wrapper: parameters, presets, state and the audio callback.

// This header must come before any other SDK header. It renames the controller's
// setState/getState with a macro while including them, to resolve a name clash with
// IComponent, and that only works if it is the first to include them.
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include "dsp.h"
#include "editor.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <atomic>
#include <cmath>
#include <cstdio>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace loopsmaster {

static const double kMinCrossover = 40.0;
static const double kMaxCrossover = 320.0;
static const double kMinOutput = -24.0;
static const double kMaxOutput = 12.0;

static double crossoverToNorm (double hz) { return std::log (hz / kMinCrossover) / std::log (kMaxCrossover / kMinCrossover); }
static double logToNorm (double v, double low, double high) { return std::log (v / low) / std::log (high / low); }
static double logFromNorm (double n, double low, double high) { return low * std::pow (high / low, n); }
static double linToNorm (double v, double low, double high) { return (v - low) / (high - low); }
// The lowest setting of these two means off.
static double offLogToNorm (double hz) { return hz <= 0.0 ? 0.0 : logToNorm (hz, 20.0, 300.0); }
static double offLogFromNorm (double n) { return n <= 0.001 ? 0.0 : logFromNorm (n, 20.0, 300.0); }
static double outputToNorm (double db) { return (db - kMinOutput) / (kMaxOutput - kMinOutput); }

static const double kDefaults[kNumParams] = {
    0.0, 0.0, crossoverToNorm (110.0), 0.5, 0.5, 0.5, 0.25, 0.35, 0.3, 0.1, 0.0, 0.3, 0.0, 1.0, 1.0, outputToNorm (-3.0),
    0.0, logToNorm (200.0, 60.0, 400.0), linToNorm (-28.0, -48.0, 0.0), logToNorm (40.0, 5.0, 300.0),
    1.0 / 1.5, 0.0, 0.0, 0.5, 0.5, 0.5, 0.0,
    1.0, 0.0, 0.25, 0.0,
    1.0, 0.0, linToNorm (-1.0, -6.0, 0.0), logToNorm (100.0, 10.0, 500.0),
    0.5, 1.0, 1.0, 0.0, 1.0, 1.0, 1.0, 0.0,
    0.0, 0.5, logToNorm (88.2818, 40.0, 400.0), logToNorm (2500.0, 1000.0, 8000.0), 0.5, 0.5, 0.5, 0.0,
    0.0, eqHzToNorm (60.0), 0.5, eqQToNorm (1.0),
    0.0, eqHzToNorm (200.0), 0.5, eqQToNorm (1.0),
    0.0, eqHzToNorm (800.0), 0.5, eqQToNorm (1.0),
    0.0, eqHzToNorm (3000.0), 0.5, eqQToNorm (1.0),
    0.0, eqHzToNorm (10000.0), 0.5, eqQToNorm (1.0),
    0.5, 0.5, 0.5, 0.0, 0.0, 0.0, 0.0, 20.0 / 26.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.5, 0.5, 0.5, 0.5, 0.5,
    0.0, 0.0, 0.0, 0.0, 0.0};

// The bass section's newer controls. A preset without its own set returns them to these neutral values.
struct BassSplit
{
	double dryBass, highBand, wetHpHz;
};
static const BassSplit kNeutralSplit = {1, 1, 0};
static const BassSplit kReplaceLows = {0, 1, 0};
static const BassSplit kReplaceWithCut = {0, 1, 30};

struct MasterPreset
{
	double duckDepth, duckHz, duckThresholdDb, duckReleaseMs;
	double widthPercent, monoBelowHz, squash, lowDb, highDb, punch, heat;
	bool clipOn;
	double clipDriveDb, clipSoft, cleanBelowHz;
	bool limitOn;
	double limitPushDb, ceilingDb, limitReleaseMs;
};

//                                       duck                  width mono squash low high punch heat   clip               limit
static const MasterPreset kMasterSafe = {0, 200, -28, 40,      100, 0, 0, 0, 0, 0, 0,        true, 0, 0.25, 0,       true, 0, -1, 100};
static const MasterPreset kMasterLoud = {0, 200, -28, 40,      100, 80, 0, 0, 0, 0, 0,       true, 6, 0.25, 50,      true, 1, -1, 100};
static const MasterPreset kMasterSlam = {0, 200, -28, 40,      100, 120, 0, 0, 0, 0, 0.1,    true, 12, 0.1, 0,       true, 2, -1, 150};
// For a bass group, with the kick sent to the sidechain input.
static const MasterPreset kBassGroup = {1, 200, -28, 40,       100, 0, 0.31, 0, 0, 0, 0.2,   true, 6, 1, 120,        false, 0, -1, 100};
static const MasterPreset kDrumBus = {0, 200, -28, 40,         100, 0, 0, 0, 0, 0.22, 0,     true, 6, 0, 0,          false, 0, -1, 100};

struct Preset
{
	const char16* name;
	const char* shortName;
	double crossoverHz, slope, bodyLevel, bodyTone, bodyDrive, subLevel, subTone, subDrive, deepLevel, deepTone, deepDrive, dry, wet, outputDb;
	const MasterPreset* master; // null leaves the Master page as it is
	const BassSplit* split;     // null means the neutral values
};

static const Preset kPresets[] = {
    {STR16 ("Init"), "Init", 110, 0.5, 0.5, 0.5, 0.25, 0.35, 0.3, 0.1, 0.0, 0.3, 0.0, 1.0, 1.0, -3},
    {STR16 ("Warm Lift"), "Warm Lift", 140, 0.5, 0.6, 0.65, 0.35, 0.0, 0.5, 0.0, 0.0, 0.5, 0.0, 1.0, 1.0, -4},
    {STR16 ("Sub Layer"), "Sub Layer", 100, 0.5, 0.15, 0.4, 0.1, 0.6, 0.35, 0.05, 0.0, 0.5, 0.0, 1.0, 1.0, -5},
    {STR16 ("Two Floors"), "Two Floors", 90, 1.0, 0.2, 0.4, 0.1, 0.5, 0.35, 0.1, 0.4, 0.3, 0.1, 1.0, 1.0, -7},
    {STR16 ("Dirty Body"), "Dirty Body", 180, 0.5, 0.7, 0.85, 0.8, 0.2, 0.5, 0.3, 0.0, 0.5, 0.0, 0.8, 1.0, -8},
    {STR16 ("Kick Weight"), "Kick Weight", 80, 1.0, 0.3, 0.3, 0.2, 0.5, 0.25, 0.2, 0.0, 0.5, 0.0, 1.0, 1.0, -5},
    {STR16 ("Sub Only"), "Sub Only", 120, 1.0, 0.0, 0.5, 0.0, 1.0, 0.4, 0.0, 0.0, 0.5, 0.0, 0.0, 1.0, -3},
    // New presets go on the end, so a saved preset choice still points at the same one.
    {STR16 ("Master Safe"), "Master Safe", 110, 0.5, 0, 0.5, 0, 0, 0.3, 0, 0, 0.3, 0, 1, 1, 0, &kMasterSafe},
    {STR16 ("Master Loud"), "Master Loud", 110, 0.5, 0, 0.5, 0, 0, 0.3, 0, 0, 0.3, 0, 1, 1, 0, &kMasterLoud},
    {STR16 ("Master Slam"), "Master Slam", 110, 0.5, 0, 0.5, 0, 0, 0.3, 0, 0, 0.3, 0, 1, 1, 0, &kMasterSlam},
    {STR16 ("Bass Group"), "Bass Group", 110, 0.5, 0, 0.5, 0, 0, 0.3, 0, 0, 0.3, 0, 1, 1, 0, &kBassGroup},
    {STR16 ("Drum Bus"), "Drum Bus", 110, 0.5, 0, 0.5, 0, 0, 0.3, 0, 0, 0.3, 0, 1, 1, 0, &kDrumBus},
    {STR16 ("Replace Lows"), "Replace Lows", 110, 0.5, 1, 1, 0.1, 0, 0.3, 0, 0, 0.3, 0, 1, 1, 0, nullptr, &kReplaceLows},
    {STR16 ("Replace + Sub"), "Replace + Sub", 100, 0.5, 0.8, 1, 0.2, 0.5, 0.3, 0.1, 0, 0.3, 0, 1, 1, -3, nullptr, &kReplaceWithCut},
};
static const int kNumPresets = sizeof (kPresets) / sizeof (kPresets[0]);

static void presetToNorm (const Preset& p, double out[kNumParams])
{
	out[kCrossover] = crossoverToNorm (p.crossoverHz);
	out[kSlope] = p.slope;
	out[kBodyLevel] = p.bodyLevel;
	out[kBodyTone] = p.bodyTone;
	out[kBodyDrive] = p.bodyDrive;
	out[kSubLevel] = p.subLevel;
	out[kSubTone] = p.subTone;
	out[kSubDrive] = p.subDrive;
	out[kDeepLevel] = p.deepLevel;
	out[kDeepTone] = p.deepTone;
	out[kDeepDrive] = p.deepDrive;
	out[kDry] = p.dry;
	out[kWet] = p.wet;
	out[kOutput] = outputToNorm (p.outputDb);
	const BassSplit& split = p.split ? *p.split : kNeutralSplit;
	out[kInputGain] = 0.5;
	out[kDryBass] = split.dryBass;
	out[kHighBand] = split.highBand;
	out[kWetHp] = offLogToNorm (split.wetHpHz);
	out[kBodyOn] = out[kSubOn] = out[kDeepOn] = 1.0;
	out[kSoloBass] = 0.0;
	if (const MasterPreset* m = p.master)
	{
		out[kDuckDepth] = m->duckDepth;
		out[kDuckBand] = logToNorm (m->duckHz, 60.0, 400.0);
		out[kDuckThreshold] = linToNorm (m->duckThresholdDb, -48.0, 0.0);
		out[kDuckRelease] = logToNorm (m->duckReleaseMs, 5.0, 300.0);
		out[kWidth] = m->widthPercent / 150.0;
		out[kMonoBelow] = offLogToNorm (m->monoBelowHz);
		out[kSquash] = m->squash;
		out[kLowShelf] = linToNorm (m->lowDb, -12.0, 12.0);
		out[kHighShelf] = linToNorm (m->highDb, -12.0, 12.0);
		out[kPunch] = 0.5 + 0.5 * m->punch;
		out[kHeat] = m->heat;
		out[kClipOn] = m->clipOn ? 1.0 : 0.0;
		out[kClipDrive] = m->clipDriveDb / 24.0;
		out[kClipSoft] = m->clipSoft;
		out[kCleanBelow] = offLogToNorm (m->cleanBelowHz);
		out[kLimitOn] = m->limitOn ? 1.0 : 0.0;
		out[kLimitPush] = m->limitPushDb / 12.0;
		out[kCeiling] = linToNorm (m->ceilingDb, -6.0, 0.0);
		out[kLimitRelease] = logToNorm (m->limitReleaseMs, 10.0, 500.0);
	}
}

static int presetIndex (double norm)
{
	const int i = static_cast<int> (norm * (kNumPresets - 1) + 0.5);
	return i < 0 ? 0 : i >= kNumPresets ? kNumPresets - 1 : i;
}

static const FUID kPluginUid (0x7B3E91C4, 0x5D2A4F18, 0xA6C07E33, 0x19F4D2B7);

class Plugin : public SingleComponentEffect, public EditorModel
{
public:
	static FUnknown* createInstance (void*) { return static_cast<IAudioProcessor*> (new Plugin); }

	IPlugView* PLUGIN_API createView (FIDString name) SMTG_OVERRIDE
	{
		if (name && FIDStringsEqual (name, ViewType::kEditor))
			return createEditor (this, this);
		return nullptr;
	}

	double currentValue (int param) const override { return values[param].load (); }
	double defaultValue (int param) const override { return kDefaults[param]; }
	const ScopeData& scopeData () const override { return engine.scope; }
	MeterData& meterData () override { return engine.meters; }

	uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE
	{
		return static_cast<uint32> (Engine::latencyAt (processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 44100.0));
	}
	int presetCount () const override { return kNumPresets; }
	const char* presetName (int index) const override { return kPresets[index].shortName; }

	tresult PLUGIN_API initialize (FUnknown* context) SMTG_OVERRIDE
	{
		tresult result = SingleComponentEffect::initialize (context);
		if (result != kResultOk)
			return result;

		addAudioInput (STR16 ("Stereo In"), SpeakerArr::kStereo);
		addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);
		addAudioInput (STR16 ("Sidechain"), SpeakerArr::kStereo, kAux, 0);

		const int32 auto_ = ParameterInfo::kCanAutomate;
		parameters.addParameter (STR16 ("Bypass"), nullptr, 1, 0.0, auto_ | ParameterInfo::kIsBypass, kBypass);

		auto* preset = new StringListParameter (STR16 ("Preset"), kPreset, nullptr, auto_ | ParameterInfo::kIsList);
		for (const Preset& p : kPresets)
			preset->appendString (p.name);
		parameters.addParameter (preset);

		parameters.addParameter (STR16 ("Crossover"), STR16 ("Hz"), 0, kDefaults[kCrossover], auto_, kCrossover);

		auto* slope = new StringListParameter (STR16 ("Slope"), kSlope, STR16 ("dB/oct"), auto_ | ParameterInfo::kIsList);
		slope->appendString (STR16 ("12"));
		slope->appendString (STR16 ("24"));
		slope->appendString (STR16 ("48"));
		slope->setNormalized (kDefaults[kSlope]);
		parameters.addParameter (slope);

		parameters.addParameter (STR16 ("Body Level"), STR16 ("%"), 0, kDefaults[kBodyLevel], auto_, kBodyLevel);
		parameters.addParameter (STR16 ("Body Tone"), STR16 ("%"), 0, kDefaults[kBodyTone], auto_, kBodyTone);
		parameters.addParameter (STR16 ("Body Drive"), STR16 ("%"), 0, kDefaults[kBodyDrive], auto_, kBodyDrive);
		parameters.addParameter (STR16 ("Sub Level"), STR16 ("%"), 0, kDefaults[kSubLevel], auto_, kSubLevel);
		parameters.addParameter (STR16 ("Sub Tone"), STR16 ("%"), 0, kDefaults[kSubTone], auto_, kSubTone);
		parameters.addParameter (STR16 ("Sub Drive"), STR16 ("%"), 0, kDefaults[kSubDrive], auto_, kSubDrive);
		parameters.addParameter (STR16 ("Deep Level"), STR16 ("%"), 0, kDefaults[kDeepLevel], auto_, kDeepLevel);
		parameters.addParameter (STR16 ("Deep Tone"), STR16 ("%"), 0, kDefaults[kDeepTone], auto_, kDeepTone);
		parameters.addParameter (STR16 ("Deep Drive"), STR16 ("%"), 0, kDefaults[kDeepDrive], auto_, kDeepDrive);
		parameters.addParameter (STR16 ("Dry"), STR16 ("%"), 0, kDefaults[kDry], auto_, kDry);
		parameters.addParameter (STR16 ("Wet"), STR16 ("%"), 0, kDefaults[kWet], auto_, kWet);
		parameters.addParameter (STR16 ("Layer Output"), STR16 ("dB"), 0, kDefaults[kOutput], auto_, kOutput);
		parameters.addParameter (STR16 ("Duck Depth"), STR16 ("%"), 0, kDefaults[kDuckDepth], auto_, kDuckDepth);
		parameters.addParameter (STR16 ("Duck Band"), STR16 ("Hz"), 0, kDefaults[kDuckBand], auto_, kDuckBand);
		parameters.addParameter (STR16 ("Duck Threshold"), STR16 ("dB"), 0, kDefaults[kDuckThreshold], auto_, kDuckThreshold);
		parameters.addParameter (STR16 ("Duck Release"), STR16 ("ms"), 0, kDefaults[kDuckRelease], auto_, kDuckRelease);
		parameters.addParameter (STR16 ("Width"), STR16 ("%"), 0, kDefaults[kWidth], auto_, kWidth);
		parameters.addParameter (STR16 ("Mono Below"), STR16 ("Hz"), 0, kDefaults[kMonoBelow], auto_, kMonoBelow);
		parameters.addParameter (STR16 ("Squash"), STR16 ("%"), 0, kDefaults[kSquash], auto_, kSquash);
		parameters.addParameter (STR16 ("Low Shelf"), STR16 ("dB"), 0, kDefaults[kLowShelf], auto_, kLowShelf);
		parameters.addParameter (STR16 ("High Shelf"), STR16 ("dB"), 0, kDefaults[kHighShelf], auto_, kHighShelf);
		parameters.addParameter (STR16 ("Punch"), STR16 ("%"), 0, kDefaults[kPunch], auto_, kPunch);
		parameters.addParameter (STR16 ("Heat"), STR16 ("%"), 0, kDefaults[kHeat], auto_, kHeat);
		parameters.addParameter (STR16 ("Clip On"), nullptr, 1, kDefaults[kClipOn], auto_, kClipOn);
		parameters.addParameter (STR16 ("Clip Drive"), STR16 ("dB"), 0, kDefaults[kClipDrive], auto_, kClipDrive);
		parameters.addParameter (STR16 ("Clip Softness"), STR16 ("%"), 0, kDefaults[kClipSoft], auto_, kClipSoft);
		parameters.addParameter (STR16 ("Clean Below"), STR16 ("Hz"), 0, kDefaults[kCleanBelow], auto_, kCleanBelow);
		parameters.addParameter (STR16 ("Limit On"), nullptr, 1, kDefaults[kLimitOn], auto_, kLimitOn);
		parameters.addParameter (STR16 ("Limit Push"), STR16 ("dB"), 0, kDefaults[kLimitPush], auto_, kLimitPush);
		parameters.addParameter (STR16 ("Ceiling"), STR16 ("dBTP"), 0, kDefaults[kCeiling], auto_, kCeiling);
		parameters.addParameter (STR16 ("Limit Release"), STR16 ("ms"), 0, kDefaults[kLimitRelease], auto_, kLimitRelease);
		parameters.addParameter (STR16 ("Input Gain"), STR16 ("dB"), 0, kDefaults[kInputGain], auto_, kInputGain);
		parameters.addParameter (STR16 ("Dry Bass"), STR16 ("%"), 0, kDefaults[kDryBass], auto_, kDryBass);
		parameters.addParameter (STR16 ("High Band"), STR16 ("%"), 0, kDefaults[kHighBand], auto_, kHighBand);
		parameters.addParameter (STR16 ("Wet High-pass"), STR16 ("Hz"), 0, kDefaults[kWetHp], auto_, kWetHp);
		parameters.addParameter (STR16 ("Body On"), nullptr, 1, kDefaults[kBodyOn], auto_, kBodyOn);
		parameters.addParameter (STR16 ("Sub On"), nullptr, 1, kDefaults[kSubOn], auto_, kSubOn);
		parameters.addParameter (STR16 ("Deep On"), nullptr, 1, kDefaults[kDeepOn], auto_, kDeepOn);
		parameters.addParameter (STR16 ("Solo Bass"), nullptr, 1, kDefaults[kSoloBass], auto_, kSoloBass);
		parameters.addParameter (STR16 ("Squash Up"), STR16 ("%"), 0, kDefaults[kSquashUp], auto_, kSquashUp);
		parameters.addParameter (STR16 ("Squash Time"), STR16 ("%"), 0, kDefaults[kSquashTime], auto_, kSquashTime);
		parameters.addParameter (STR16 ("Split Low"), STR16 ("Hz"), 0, kDefaults[kSplitLow], auto_, kSplitLow);
		parameters.addParameter (STR16 ("Split High"), STR16 ("Hz"), 0, kDefaults[kSplitHigh], auto_, kSplitHigh);
		parameters.addParameter (STR16 ("Low Band"), STR16 ("dB"), 0, kDefaults[kBandLow], auto_, kBandLow);
		parameters.addParameter (STR16 ("Mid Band"), STR16 ("dB"), 0, kDefaults[kBandMid], auto_, kBandMid);
		parameters.addParameter (STR16 ("High Band Level"), STR16 ("dB"), 0, kDefaults[kBandHigh], auto_, kBandHigh);
		auto* duckSource = new StringListParameter (STR16 ("Duck Source"), kDuckSource, nullptr, auto_ | ParameterInfo::kIsList);
		duckSource->appendString (STR16 ("Kick"));
		duckSource->appendString (STR16 ("1/2"));
		duckSource->appendString (STR16 ("1/4"));
		duckSource->appendString (STR16 ("1/8"));
		parameters.addParameter (duckSource);
		for (int band = 0; band < 5; ++band)
		{
			const int first = kEqFirst + band * 4;
			char ascii[32];
			UString128 title;
			std::snprintf (ascii, sizeof ascii, "EQ %d Type", band + 1);
			title.fromAscii (ascii);
			auto* type = new StringListParameter (title, first, nullptr, auto_ | ParameterInfo::kIsList);
			type->appendString (STR16 ("Bell"));
			type->appendString (STR16 ("Low shelf"));
			type->appendString (STR16 ("High shelf"));
			type->appendString (STR16 ("Low cut"));
			type->appendString (STR16 ("High cut"));
			type->appendString (STR16 ("Notch"));
			type->appendString (STR16 ("Band pass"));
			type->appendString (STR16 ("Tilt"));
			type->appendString (STR16 ("All pass"));
			parameters.addParameter (type);
			std::snprintf (ascii, sizeof ascii, "EQ %d Freq", band + 1);
			title.fromAscii (ascii);
			parameters.addParameter (title, STR16 ("Hz"), 0, kDefaults[first + 1], auto_, first + 1);
			std::snprintf (ascii, sizeof ascii, "EQ %d Gain", band + 1);
			title.fromAscii (ascii);
			parameters.addParameter (title, STR16 ("dB"), 0, kDefaults[first + 2], auto_, first + 2);
			std::snprintf (ascii, sizeof ascii, "EQ %d Q", band + 1);
			title.fromAscii (ascii);
			parameters.addParameter (title, nullptr, 0, kDefaults[first + 3], auto_, first + 3);
		}

		const auto addList = [&] (const char* name, int tag, std::initializer_list<const char*> names) {
			UString128 title;
			title.fromAscii (name);
			auto* list = new StringListParameter (title, tag, nullptr, auto_ | ParameterInfo::kIsList);
			for (const char* item : names)
			{
				UString128 shown;
				shown.fromAscii (item);
				list->appendString (shown);
			}
			list->setNormalized (kDefaults[tag]);
			parameters.addParameter (list);
		};
		parameters.addParameter (STR16 ("Low Threshold"), STR16 ("dB"), 0, kDefaults[kThreshLow], auto_, kThreshLow);
		parameters.addParameter (STR16 ("Mid Threshold"), STR16 ("dB"), 0, kDefaults[kThreshMid], auto_, kThreshMid);
		parameters.addParameter (STR16 ("High Threshold"), STR16 ("dB"), 0, kDefaults[kThreshHigh], auto_, kThreshHigh);
		addList ("Low Mode", kModeLow, {"Both", "Up", "Down", "Off"});
		addList ("Mid Mode", kModeMid, {"Both", "Up", "Down", "Off"});
		addList ("High Mode", kModeHigh, {"Both", "Up", "Down", "Off"});
		addList ("Listen", kListen, {"All", "Low", "Mid", "High"});
		parameters.addParameter (STR16 ("Clip Ceiling"), STR16 ("dB"), 0, kDefaults[kClipCeiling], auto_, kClipCeiling);
		addList ("Limit Style", kLimitStyle, {"Manual", "Clean", "Hard", "Modern", "Safe"});
		parameters.addParameter (STR16 ("Unity Gain"), nullptr, 1, kDefaults[kLimitUnity], auto_, kLimitUnity);
		parameters.addParameter (STR16 ("Audition"), nullptr, 1, kDefaults[kLimitAudition], auto_, kLimitAudition);
		parameters.addParameter (STR16 ("DC Filter"), nullptr, 1, kDefaults[kDcFilter], auto_, kDcFilter);
		addList ("Dither", kDither, {"Off", "16 bit", "24 bit"});
		for (int band = 0; band < 5; ++band)
		{
			char name[32];
			std::snprintf (name, sizeof name, "EQ %d Placement", band + 1);
			addList (name, kEqPlaceFirst + band, {"Left", "Right", "Stereo", "Mid", "Side"});
		}
		for (int band = 0; band < 5; ++band)
		{
			char name[32];
			std::snprintf (name, sizeof name, "EQ %d Slope", band + 1);
			addList (name, kEqSlopeFirst + band, {"12 dB", "24 dB", "48 dB"});
		}
		for (int i = 0; i < kNumParams; ++i)
			values[i].store (kDefaults[i]);
		return kResultOk;
	}

	tresult PLUGIN_API setBusArrangements (SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs, int32 numOuts) SMTG_OVERRIDE
	{
		const bool sideOk = numIns == 1 || (numIns == 2 && (inputs[1] == SpeakerArr::kStereo || inputs[1] == SpeakerArr::kMono));
		if (numIns >= 1 && numOuts == 1 && sideOk && inputs[0] == SpeakerArr::kStereo && outputs[0] == SpeakerArr::kStereo)
			return SingleComponentEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
		return kResultFalse;
	}

	tresult PLUGIN_API setActive (TBool state) SMTG_OVERRIDE
	{
		if (state)
		{
			engine.update (readSettings ());
			engine.prepare (processSetup.sampleRate);
		}
		return SingleComponentEffect::setActive (state);
	}

	tresult PLUGIN_API setProcessing (TBool state) SMTG_OVERRIDE
	{
		if (state)
			engine.reset ();
		return kResultOk;
	}

	tresult PLUGIN_API process (ProcessData& data) SMTG_OVERRIDE
	{
		if (data.inputParameterChanges)
		{
			const int32 count = data.inputParameterChanges->getParameterCount ();
			for (int32 i = 0; i < count; ++i)
			{
				IParamValueQueue* queue = data.inputParameterChanges->getParameterData (i);
				int32 offset = 0;
				ParamValue value = 0.0;
				const int32 points = queue ? queue->getPointCount () : 0;
				if (points > 0 && queue->getPoint (points - 1, offset, value) == kResultOk && queue->getParameterId () < kNumParams)
					values[queue->getParameterId ()].store (value);
			}
		}

		applyPresetIfChanged (data);

		if (data.numSamples <= 0 || data.numInputs == 0 || data.numOutputs == 0)
			return kResultOk;
		if (data.inputs[0].numChannels < 2 || data.outputs[0].numChannels < 2 || !data.inputs[0].channelBuffers32 || !data.outputs[0].channelBuffers32)
			return kResultOk;

		engine.update (readSettings ());
		const ProcessContext* context = data.processContext;
		const bool timed = context && (context->state & ProcessContext::kProjectTimeMusicValid) && (context->state & ProcessContext::kTempoValid);
		engine.setTransport (timed ? context->projectTimeMusic : 0.0, timed ? context->tempo : 120.0, timed && (context->state & ProcessContext::kPlaying));
		const float* sideL = nullptr;
		const float* sideR = nullptr;
		if (data.numInputs > 1 && data.inputs[1].numChannels > 0 && data.inputs[1].channelBuffers32)
		{
			sideL = data.inputs[1].channelBuffers32[0];
			sideR = data.inputs[1].numChannels > 1 ? data.inputs[1].channelBuffers32[1] : sideL;
		}
		engine.process (data.inputs[0].channelBuffers32[0], data.inputs[0].channelBuffers32[1],
		                data.outputs[0].channelBuffers32[0], data.outputs[0].channelBuffers32[1], data.numSamples, sideL, sideR);
		data.outputs[0].silenceFlags = 0;
		return kResultOk;
	}

	tresult PLUGIN_API setParamNormalized (ParamID tag, ParamValue value) SMTG_OVERRIDE
	{
		if (tag < kNumParams)
			values[tag].store (value);
		return SingleComponentEffect::setParamNormalized (tag, value);
	}

	tresult PLUGIN_API getState (IBStream* state) SMTG_OVERRIDE
	{
		IBStreamer stream (state, kLittleEndian);
		if (!stream.writeInt32 (kStateVersion))
			return kResultFalse;
		for (int i = 0; i < kNumParams; ++i)
			if (!stream.writeDouble (values[i].load ()))
				return kResultFalse;
		return kResultOk;
	}

	tresult PLUGIN_API setState (IBStream* state) SMTG_OVERRIDE
	{
		IBStreamer stream (state, kLittleEndian);
		int32 version = 0;
		if (!stream.readInt32 (version) || version < 1 || version > kStateVersion)
			return kResultFalse;
		// Sessions saved before the master chain existed load with it switched off, so they
		// keep sounding as they did. Only the small added latency differs.
		double loaded[kNumParams];
		for (int i = 0; i < kNumParams; ++i)
			loaded[i] = kDefaults[i];
		const int stored = version == 1 ? kOutput + 1 : version == 2 ? kLimitRelease + 1 : version == 3 ? kSoloBass + 1 : version == 4 ? static_cast<int> (kEqEnd) : kNumParams;
		for (int i = 0; i < stored; ++i)
			if (!stream.readDouble (loaded[i]))
				return kResultFalse;
		if (version == 1)
		{
			loaded[kClipOn] = 0.0;
			loaded[kLimitOn] = 0.0;
		}
		if (version == 4)
			for (int band = 0; band < 5; ++band)
			{
				// 1.3 had five EQ shapes, and its Q was not scaled. Keep those sessions sounding the same.
				const int first = kEqFirst + band * 4;
				loaded[first] *= 4.0 / 8.0;
				loaded[first + 3] = std::clamp (eqQToNorm (eqQFromNorm (loaded[first + 3]) / kEqQScale), 0.0, 1.0);
			}
		if (version < kStateVersion)
		{
			const int oldCount = version == 1 ? kVersion1Presets : version == 2 ? kVersion2Presets : kNumPresets;
			const double oldIndex = std::floor (loaded[kPreset] * (oldCount - 1) + 0.5);
			loaded[kPreset] = oldIndex / (kNumPresets - 1);
		}
		// Mark the saved preset as applied first, so restoring a session keeps the user's tweaks.
		appliedPreset.store (presetIndex (loaded[kPreset]));
		for (int i = 0; i < kNumParams; ++i)
		{
			values[i].store (loaded[i]);
			SingleComponentEffect::setParamNormalized (static_cast<ParamID> (i), loaded[i]);
		}
		return kResultOk;
	}

	tresult PLUGIN_API getParamStringByValue (ParamID tag, ParamValue norm, String128 string) SMTG_OVERRIDE
	{
		if (isSwitch (tag))
			return SingleComponentEffect::getParamStringByValue (tag, norm, string);
		if (tag >= kNumParams)
			return kResultFalse;
		UString128 text;
		if ((tag == kMonoBelow || tag == kCleanBelow || tag == kWetHp) && norm <= 0.001)
		{
			text.fromAscii ("Off");
			text.copyTo (string, 128);
			return kResultOk;
		}
		const bool isQ = isEqParam (tag) && (tag - kEqFirst) % 4 == 3;
		text.printFloat (toDisplay (tag, norm), isQ ? 2 : isDecibels (tag) ? 1 : 0);
		text.copyTo (string, 128);
		return kResultOk;
	}

	tresult PLUGIN_API getParamValueByString (ParamID tag, TChar* string, ParamValue& norm) SMTG_OVERRIDE
	{
		if (isSwitch (tag))
			return SingleComponentEffect::getParamValueByString (tag, string, norm);
		if (tag >= kNumParams)
			return kResultFalse;
		double shown = 0.0;
		if (!UString (string, tstrlen (string)).scanFloat (shown))
			return kResultFalse;
		const double v = fromDisplay (tag, shown);
		norm = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
		return kResultOk;
	}

private:
	static constexpr int32 kStateVersion = 5;
	static constexpr int kVersion1Presets = 7;
	static constexpr int kVersion2Presets = 12;

	static bool isSwitch (ParamID tag) { return tag == kBypass || tag == kPreset || tag == kSlope || tag == kClipOn || tag == kLimitOn || tag == kBodyOn || tag == kSubOn || tag == kDeepOn || tag == kSoloBass || tag == kDuckSource || tag == kModeLow || tag == kModeMid || tag == kModeHigh || tag == kListen || tag == kLimitStyle || tag == kLimitUnity || tag == kLimitAudition || tag == kDcFilter || tag == kDither || tag >= kEqPlaceFirst || (isEqParam (tag) && (tag - kEqFirst) % 4 == 0); }
	static bool isDecibels (ParamID tag)
	{
		return tag == kOutput || tag == kDuckThreshold || tag == kLowShelf || tag == kHighShelf || tag == kClipDrive || tag == kLimitPush || tag == kCeiling || tag == kInputGain || tag == kBandLow || tag == kBandMid || tag == kBandHigh || tag == kThreshLow || tag == kThreshMid || tag == kThreshHigh || tag == kClipCeiling || (isEqParam (tag) && (tag - kEqFirst) % 4 == 2);
	}

	static double toDisplay (ParamID tag, double norm)
	{
		if (isEqParam (tag))
		{
			const int field = (tag - kEqFirst) % 4;
			return field == 1 ? eqHzFromNorm (norm) : field == 2 ? eqGainFromNorm (norm) : field == 3 ? eqQFromNorm (norm) : norm;
		}
		switch (tag)
		{
			case kThreshLow:
			case kThreshMid:
			case kThreshHigh: return -24.0 + norm * 48.0;
			case kClipCeiling: return -20.0 + norm * 26.0;
			case kSquashTime: return 25.0 * std::pow (16.0, norm);
			case kSplitLow: return logFromNorm (norm, 40.0, 400.0);
			case kSplitHigh: return logFromNorm (norm, 1000.0, 8000.0);
			case kBandLow:
			case kBandMid:
			case kBandHigh: return -12.0 + norm * 24.0;
			case kCrossover: return kMinCrossover * std::pow (kMaxCrossover / kMinCrossover, norm);
			case kOutput: return kMinOutput + norm * (kMaxOutput - kMinOutput);
			case kDuckBand: return logFromNorm (norm, 60.0, 400.0);
			case kDuckThreshold: return -48.0 + norm * 48.0;
			case kDuckRelease: return logFromNorm (norm, 5.0, 300.0);
			case kWidth: return norm * 150.0;
			case kMonoBelow:
			case kWetHp:
			case kCleanBelow: return offLogFromNorm (norm);
			case kInputGain: return -24.0 + norm * 48.0;
			case kLowShelf:
			case kHighShelf: return -12.0 + norm * 24.0;
			case kPunch: return -100.0 + norm * 200.0;
			case kClipDrive: return norm * 24.0;
			case kLimitPush: return norm * 12.0;
			case kCeiling: return -6.0 + norm * 6.0;
			case kLimitRelease: return logFromNorm (norm, 10.0, 500.0);
			default: return norm * 100.0;
		}
	}

	static double fromDisplay (ParamID tag, double shown)
	{
		if (isEqParam (tag))
		{
			const int field = (tag - kEqFirst) % 4;
			if (shown <= 0.0 && field != 2)
				return 0.0;
			return field == 1 ? eqHzToNorm (shown) : field == 2 ? eqGainToNorm (shown) : eqQToNorm (shown);
		}
		switch (tag)
		{
			case kThreshLow:
			case kThreshMid:
			case kThreshHigh: return linToNorm (shown, -24.0, 24.0);
			case kClipCeiling: return linToNorm (shown, -20.0, 6.0);
			case kSquashTime: return shown > 0.0 ? std::log (shown / 25.0) / std::log (16.0) : 0.0;
			case kSplitLow: return shown > 0.0 ? logToNorm (shown, 40.0, 400.0) : 0.0;
			case kSplitHigh: return shown > 0.0 ? logToNorm (shown, 1000.0, 8000.0) : 0.0;
			case kBandLow:
			case kBandMid:
			case kBandHigh: return linToNorm (shown, -12.0, 12.0);
			case kCrossover: return shown > 0.0 ? crossoverToNorm (shown) : 0.0;
			case kOutput: return outputToNorm (shown);
			case kDuckBand: return shown > 0.0 ? logToNorm (shown, 60.0, 400.0) : 0.0;
			case kDuckThreshold: return linToNorm (shown, -48.0, 0.0);
			case kDuckRelease: return shown > 0.0 ? logToNorm (shown, 5.0, 300.0) : 0.0;
			case kWidth: return shown / 150.0;
			case kMonoBelow:
			case kWetHp:
			case kCleanBelow: return offLogToNorm (shown);
			case kInputGain: return linToNorm (shown, -24.0, 24.0);
			case kLowShelf:
			case kHighShelf: return linToNorm (shown, -12.0, 12.0);
			case kPunch: return linToNorm (shown, -100.0, 100.0);
			case kClipDrive: return shown / 24.0;
			case kLimitPush: return shown / 12.0;
			case kCeiling: return linToNorm (shown, -6.0, 0.0);
			case kLimitRelease: return shown > 0.0 ? logToNorm (shown, 10.0, 500.0) : 0.0;
			default: return shown / 100.0;
		}
	}

	Settings readSettings () const
	{
		Settings s;
		s.bypass = values[kBypass].load () >= 0.5;
		s.crossoverHz = toDisplay (kCrossover, values[kCrossover].load ());
		s.slopeIndex = static_cast<int> (values[kSlope].load () * 2.0 + 0.5);
		s.bodyLevel = values[kBodyLevel].load ();
		s.bodyTone = values[kBodyTone].load ();
		s.bodyDrive = values[kBodyDrive].load ();
		s.subLevel = values[kSubLevel].load ();
		s.subTone = values[kSubTone].load ();
		s.subDrive = values[kSubDrive].load ();
		s.deepLevel = values[kDeepLevel].load ();
		s.deepTone = values[kDeepTone].load ();
		s.deepDrive = values[kDeepDrive].load ();
		s.dry = values[kDry].load ();
		s.wet = values[kWet].load ();
		s.outputDb = toDisplay (kOutput, values[kOutput].load ());
		s.inputDb = toDisplay (kInputGain, values[kInputGain].load ());
		s.dryBass = values[kDryBass].load ();
		s.highBand = values[kHighBand].load ();
		s.wetHpHz = toDisplay (kWetHp, values[kWetHp].load ());
		s.bodyOn = values[kBodyOn].load () >= 0.5;
		s.subOn = values[kSubOn].load () >= 0.5;
		s.deepOn = values[kDeepOn].load () >= 0.5;
		s.soloBass = values[kSoloBass].load () >= 0.5;
		MasterSettings& m = s.master;
		m.squashUp = values[kSquashUp].load ();
		m.squashTime = toDisplay (kSquashTime, values[kSquashTime].load ()) / 100.0;
		m.splitLowHz = toDisplay (kSplitLow, values[kSplitLow].load ());
		m.splitHighHz = toDisplay (kSplitHigh, values[kSplitHigh].load ());
		m.bandDb[0] = toDisplay (kBandLow, values[kBandLow].load ());
		m.bandDb[1] = toDisplay (kBandMid, values[kBandMid].load ());
		m.bandDb[2] = toDisplay (kBandHigh, values[kBandHigh].load ());
		m.duckSource = static_cast<int> (values[kDuckSource].load () * 3.0 + 0.5);
		const int thresholds[3] = {kThreshLow, kThreshMid, kThreshHigh};
		const int modes[3] = {kModeLow, kModeMid, kModeHigh};
		for (int b = 0; b < 3; ++b)
		{
			m.bandThresholdDb[b] = toDisplay (thresholds[b], values[thresholds[b]].load ());
			m.bandMode[b] = static_cast<int> (values[modes[b]].load () * 3.0 + 0.5);
		}
		m.listen = static_cast<int> (values[kListen].load () * 3.0 + 0.5);
		const double clipTop = toDisplay (kClipCeiling, values[kClipCeiling].load ());
		m.clipCeilingDb = std::fabs (clipTop) < 0.001 ? 0.0 : clipTop;
		m.limitStyle = static_cast<int> (values[kLimitStyle].load () * 4.0 + 0.5);
		m.limitUnity = values[kLimitUnity].load () >= 0.5;
		m.limitAudition = values[kLimitAudition].load () >= 0.5;
		m.dcFilter = values[kDcFilter].load () >= 0.5;
		const int dither = static_cast<int> (values[kDither].load () * 2.0 + 0.5);
		m.ditherBits = dither == 1 ? 16 : dither == 2 ? 24 : 0;
		for (int band = 0; band < 5; ++band)
		{
			const int first = kEqFirst + band * 4;
			m.eq[band].type = eqTypeFromNorm (values[first].load ());
			m.eq[band].hz = eqHzFromNorm (values[first + 1].load ());
			// Centred knobs often land a hair off zero; treat that as off so the band stays out of the path.
			const double gain = eqGainFromNorm (values[first + 2].load ());
			m.eq[band].gainDb = std::fabs (gain) < 0.01 ? 0.0 : gain;
			m.eq[band].q = eqQFromNorm (values[first + 3].load ());
			m.eq[band].place = static_cast<int> (values[kEqPlaceFirst + band].load () * 4.0 + 0.5);
			m.eq[band].slope = static_cast<int> (values[kEqSlopeFirst + band].load () * 2.0 + 0.5);
		}
		m.duckDepth = values[kDuckDepth].load ();
		m.duckHz = toDisplay (kDuckBand, values[kDuckBand].load ());
		m.duckThresholdDb = toDisplay (kDuckThreshold, values[kDuckThreshold].load ());
		m.duckReleaseMs = toDisplay (kDuckRelease, values[kDuckRelease].load ());
		m.width = toDisplay (kWidth, values[kWidth].load ()) / 100.0;
		m.monoBelowHz = toDisplay (kMonoBelow, values[kMonoBelow].load ());
		m.squash = values[kSquash].load ();
		m.lowDb = toDisplay (kLowShelf, values[kLowShelf].load ());
		m.highDb = toDisplay (kHighShelf, values[kHighShelf].load ());
		m.punch = toDisplay (kPunch, values[kPunch].load ()) / 100.0;
		m.heat = values[kHeat].load ();
		m.clipOn = values[kClipOn].load () >= 0.5;
		m.clipDriveDb = toDisplay (kClipDrive, values[kClipDrive].load ());
		m.clipSoft = values[kClipSoft].load ();
		m.cleanBelowHz = toDisplay (kCleanBelow, values[kCleanBelow].load ());
		m.limitOn = values[kLimitOn].load () >= 0.5;
		m.limitPushDb = toDisplay (kLimitPush, values[kLimitPush].load ());
		m.ceilingDb = toDisplay (kCeiling, values[kCeiling].load ());
		m.limitReleaseMs = toDisplay (kLimitRelease, values[kLimitRelease].load ());
		return s;
	}

	// A preset choice updates every control and tells the host, so the panel follows along.
	void applyPresetIfChanged (ProcessData& data)
	{
		const int wanted = presetIndex (values[kPreset].load ());
		if (wanted == appliedPreset.load ())
			return;
		appliedPreset.store (wanted);

		double norm[kNumParams];
		presetToNorm (kPresets[wanted], norm);
		// Presets leave the EQ and the multiband's extra controls where you set them.
		for (int id = kCrossover; id <= kSoloBass; ++id)
		{
			// A preset without Master settings leaves that page alone.
			if (!kPresets[wanted].master && id >= kDuckDepth && id <= kLimitRelease)
				continue;
			values[id].store (norm[id]);
			if (data.outputParameterChanges)
			{
				int32 slot = 0;
				if (IParamValueQueue* queue = data.outputParameterChanges->addParameterData (static_cast<ParamID> (id), slot))
				{
					int32 point = 0;
					queue->addPoint (0, norm[id], point);
				}
			}
		}
	}

	Engine engine;
	std::atomic<double> values[kNumParams];
	std::atomic<int> appliedPreset {0};
};

} // namespace loopsmaster

BEGIN_FACTORY_DEF ("Loops", "", "")

DEF_CLASS2 (INLINE_UID_FROM_FUID (loopsmaster::kPluginUid),
            PClassInfo::kManyInstances,
            kVstAudioEffectClass,
            "Loops Master",
            0,
            PlugType::kFxMastering,
            "1.7.0",
            kVstVersionString,
            loopsmaster::Plugin::createInstance)

END_FACTORY
