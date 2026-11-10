// The LoopsMaster window.
#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/vst/vstguieditor.h"

#include "dsp.h"

namespace loopsmaster {

enum ParamId : Steinberg::Vst::ParamID
{
	kBypass,
	kPreset,
	kCrossover,
	kSlope,
	kBodyLevel,
	kBodyTone,
	kBodyDrive,
	kSubLevel,
	kSubTone,
	kSubDrive,
	kDeepLevel,
	kDeepTone,
	kDeepDrive,
	kDry,
	kWet,
	kOutput,
	// New parameters go on the end so saved sessions and automation keep their meaning.
	kDuckDepth,
	kDuckBand,
	kDuckThreshold,
	kDuckRelease,
	kWidth,
	kMonoBelow,
	kSquash,
	kLowShelf,
	kHighShelf,
	kPunch,
	kHeat,
	kClipOn,
	kClipDrive,
	kClipSoft,
	kCleanBelow,
	kLimitOn,
	kLimitPush,
	kCeiling,
	kLimitRelease,
	kInputGain,
	kDryBass,
	kHighBand,
	kWetHp,
	kBodyOn,
	kSubOn,
	kDeepOn,
	kSoloBass,
	kSquashUp,
	kSquashTime,
	kSplitLow,
	kSplitHigh,
	kBandLow,
	kBandMid,
	kBandHigh,
	kDuckSource,
	kEqFirst, // five bands of four: type, frequency, gain, Q
	kEqEnd = kEqFirst + 20,
	kThreshLow = kEqEnd,
	kThreshMid,
	kThreshHigh,
	kModeLow,
	kModeMid,
	kModeHigh,
	kListen,
	kClipCeiling,
	kLimitStyle,
	kLimitUnity,
	kLimitAudition,
	kDcFilter,
	kDither,
	kEqPlaceFirst,
	kEqSlopeFirst = kEqPlaceFirst + 5,
	kNumParams = kEqSlopeFirst + 5
};

inline bool isEqParam (int param) { return param >= kEqFirst && param < kEqEnd; }

// What the window needs from the plugin.
class EditorModel
{
public:
	virtual ~EditorModel () = default;
	virtual double currentValue (int param) const = 0;
	virtual double defaultValue (int param) const = 0;
	virtual const ScopeData& scopeData () const = 0;
	virtual MeterData& meterData () = 0;
	virtual int presetCount () const = 0;
	virtual const char* presetName (int index) const = 0;
};

Steinberg::IPlugView* createEditor (Steinberg::Vst::EditController* controller, EditorModel* model);

} // namespace loopsmaster
