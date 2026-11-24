//------------------------------------------------------------------------
// WornVinyl - the parameter table
//
// Every range and default here comes from a constant in WornVinylDsp.h,
// so the DSP's idea of a range and the host's cannot drift apart.
//------------------------------------------------------------------------

#include "WornVinylParams.h"

namespace WornVinyl {

//------------------------------------------------------------------------
// id            title        units  type              plainMin      plainMax         plainDefault       internalMin   internalMax      steps
//------------------------------------------------------------------------
const ParamDef kParams[kNumParams] =
{
	{ kEra,         "Era",        "",   ParamType::Enum,  0.0,          kEraCount - 1,   kEraDefault,       0.0,          kEraCount - 1,   kEraCount - 1  },
	{ kDisc,        "Disc",       "",   ParamType::Enum,  0.0,          kDiscCount - 1,  kDiscDefault,      0.0,          kDiscCount - 1,  kDiscCount - 1 },
	{ kScratches,   "Scratches",  "%",  ParamType::Float, kPercentMin,  kPercentMax,     kScratchesDefault, kPercentMin,  kPercentMax,     0              },
	{ kDust,        "Dust",       "%",  ParamType::Float, kPercentMin,  kPercentMax,     kDustDefault,      kPercentMin,  kPercentMax,     0              },
	{ kWow,         "Wow",        "%",  ParamType::Float, kPercentMin,  kPercentMax,     kWowDefault,       kPercentMin,  kPercentMax,     0              },
	{ kFlutter,     "Flutter",    "%",  ParamType::Float, kPercentMin,  kPercentMax,     kFlutterDefault,   kPercentMin,  kPercentMax,     0              },
	{ kWarp,        "Warp",       "%",  ParamType::Float, kPercentMin,  kPercentMax,     kWarpDefault,      kPercentMin,  kPercentMax,     0              },
	{ kJumps,       "Jumps",      "",   ParamType::Enum,  0.0,          1.0,             1.0,               0.0,          1.0,             1              },
	{ kMix,         "Mix",        "%",  ParamType::Float, kPercentMin,  kPercentMax,     kMixDefault,       kPercentMin,  kPercentMax,     0              },
	{ kOutput,      "Output",     "dB", ParamType::Float, kOutputMinDb, kOutputMaxDb,    kOutputDefaultDb,  kOutputMinDb, kOutputMaxDb,    0              },

	{ kDiscAngle,   "Disc Angle", "",   ParamType::Float, 0.0,          1.0,             0.0,               0.0,          1.0,             0              },
	{ kSkipping,    "Skipping",   "",   ParamType::Float, 0.0,          1.0,             0.0,               0.0,          1.0,             0              },

	{ kSpeed,       "Speed",      "",   ParamType::Enum,  0.0,          kSpeedCount - 1, kSpeedDefault,     0.0,          kSpeedCount - 1, kSpeedCount - 1 },
	{ kPlayer,      "Player",     "",   ParamType::Enum,  0.0,          kPlayerCount - 1, kPlayerDefault,   0.0,          kPlayerCount - 1, kPlayerCount - 1 },
	{ kSpeaker,     "Speaker",    "",   ParamType::Enum,  0.0,          kSpeakerCount - 1, kSpeakerDefault, 0.0,          kSpeakerCount - 1, kSpeakerCount - 1 },
};

//------------------------------------------------------------------------
// The table is written by hand, so prove its shape at COMPILE time - a
// row in the wrong place presents as "the Dust knob moves the wow".
//------------------------------------------------------------------------
static_assert (kNumParams == 15, "ten settings, two outputs, then Speed, Player and Speaker (late settings)");
static_assert (! isOutputParam (kPlayer) && isLateSetting (kPlayer), "Player is a late setting");
static_assert (! isOutputParam (kSpeaker) && isLateSetting (kSpeaker), "Speaker is a late setting");
static_assert (! isOutputParam (kSpeed) && isLateSetting (kSpeed) && ! isLateSetting (kOutput),
               "Speed is a setting saved after the bypass, not an output");
static_assert (kNumStoredParams == 10, "only the settings are saved");
static_assert (isOutputParam (kDiscAngle) && isOutputParam (kSkipping) && ! isOutputParam (kOutput),
               "output block boundaries");
static_assert (kBypass >= kNumParams, "kBypass must never collide with a table id");

// kParams is extern, so "every row sits at its own id" cannot be a
// static_assert: tests/ParamsTests.cpp checks it.

//------------------------------------------------------------------------
const ParamDef& paramDef (Steinberg::Vst::ParamID id)
{
	if (id < kNumParams)
		return kParams[id];
	return kParams[kOutput];
}

//------------------------------------------------------------------------
} // namespace WornVinyl
