//------------------------------------------------------------------------
// WornVinyl - parameter definitions
//
// One table, read by BOTH the processor and the controller, so a range or
// a default cannot be typed twice and disagree.
//
// Every parameter carries THREE ranges, the shape the SpaceDub, ForTran,
// SpyBand, VocalFilter and MasterLoud plug-ins all settled on:
//
//   * VST3 normalised, 0..1, which is what the host works in;
//   * "plain", what the user sees - %, dB, an era's name;
//   * "internal", what the DSP is handed, via toInternal().
//
// There is no DXi behind this plug-in, so internal == plain for every row.
// The slot is EMPTY, not absent: the first parameter whose DSP units differ
// from its display units fills it rather than inventing a fourth mapping.
//
// APPEND NEW PARAMETERS, NEVER INSERT. An id that moves loads a saved
// project's value into the wrong control.
//------------------------------------------------------------------------

#pragma once

#include "WornVinylDsp.h"

#include "pluginterfaces/vst/vsttypes.h"

namespace WornVinyl {

//------------------------------------------------------------------------
enum Param : Steinberg::Vst::ParamID
{
	//--------------------------------------------------------------------
	// SETTINGS - saved in the state, automatable.
	//--------------------------------------------------------------------
	kEra,           // 0   1920s / 1940s / 1950s / 1960s / 1970s
	kDisc,          // 1   Shellac / Vinyl - heard in the 1950s only (effectiveDisc)
	kScratches,     // 2   %, how many scratches and how deep; jumps above 60 %
	kDust,          // 3   %, crackle, surface hiss and rumble
	kWow,           // 4   %, once a turn: an off-centre hole, and the motor's drift
	kFlutter,       // 5   %, 7 and 11 Hz: the idler and the belt
	kWarp,          // 6   %, one high spot a turn: a swoop, a dip, a dull moment
	kJumps,         // 7   Off / On - may the needle stick and skip
	kMix,           // 8   %, against the dry signal (delayed by the latency)
	kOutput,        // 9   dB

	//--------------------------------------------------------------------
	// OUTPUTS - published by the processor through
	// data.outputParameterChanges every block. Read-only and HIDDEN, so no
	// host lists them and nothing outside the plug-in writes them; NOT
	// saved, because they are a view of the DSP. This is the route because
	// a message sent from process() is silently discarded by the host's
	// connection proxy.
	//--------------------------------------------------------------------
	kDiscAngle,     // 10  where the disc is, 0..1 of a turn - the turntable screen
	kSkipping,      // 11  0 / 1, the needle is stuck or jumping - the SKIP lamp

	//--------------------------------------------------------------------
	// SETTINGS APPENDED AFTER THE OUTPUTS (ids never move), so outside
	// kNumStoredParams: each is saved explicitly after the bypass - see
	// isLateSetting and the state layout in WornVinylProcessor.cpp.
	//--------------------------------------------------------------------
	kSpeed,         // 12  Auto / 33 / 45 / 78 rpm - how fast the disc turns
	kPlayer,        // 13  Period / Modern - what the surface sounds like
	kSpeaker,       // 14  Off / Horn / 2 in / 3 in / 6 in / 3-way - what it comes out of

	kNumParams
};

constexpr Steinberg::Vst::ParamID kOutputBase = kDiscAngle;
constexpr Steinberg::Vst::ParamID kOutputEnd  = kSkipping + 1;

/** BOUNDED BY THE BLOCK, NOT BY THE TABLE (VocalFilter's kVoice bug): a
    setting appended after kSkipping must stay outside this range and be
    saved explicitly after the bypass. */
constexpr bool isOutputParam (Steinberg::Vst::ParamID id)
{
	return id >= kOutputBase && id < kOutputEnd;
}

/** The settings appended after the output block, saved one by one after
    the bypass, in id order. */
constexpr bool isLateSetting (Steinberg::Vst::ParamID id)
{
	return id == kSpeed || id == kPlayer || id == kSpeaker;
}

/** The settings run from 0 to here and are saved; the outputs follow. */
constexpr Steinberg::Vst::ParamID kNumStoredParams = kOutputBase;

/** The VST3 bypass, which hosts expect. 1000 is the convention, and it is
    far past the end of kParams - so RANGE-CHECK every id before indexing
    the table. paramDef() below does. */
constexpr Steinberg::Vst::ParamID kBypass = 1000;

/** The state layout's version. Bump only when an EXISTING field changes
    meaning; a field appended at the end is read as "absent" by a short
    stream and needs no bump. */
constexpr Steinberg::int32 kStateVersion = 1;

//------------------------------------------------------------------------
enum class ParamType { Float, Bool, Enum, Int };

//------------------------------------------------------------------------
struct ParamDef
{
	Steinberg::Vst::ParamID id;
	const char* title;          // NEVER null - RangeParameter dereferences it
	const char* units;          // NEVER null; "" when the value string carries its own
	ParamType   type;
	double      plainMin;
	double      plainMax;
	double      plainDefault;
	double      internalMin;    // range the DSP expects
	double      internalMax;
	int         stepCount;      // 0 = continuous

	//--------------------------------------------------------------------
	double toPlain (double normalized) const
	{
		return plainMin + normalized * (plainMax - plainMin);
	}

	double toNormalized (double plain) const
	{
		if (plainMax == plainMin)
			return 0.0;
		return (plain - plainMin) / (plainMax - plainMin);
	}

	/** Booleans snap, enums and integers round, floats scale. */
	double toInternal (double normalized) const
	{
		if (type == ParamType::Bool)
			return (normalized < 0.5) ? 0.0 : 1.0;

		const double v = internalMin + normalized * (internalMax - internalMin);
		if (type == ParamType::Enum || type == ParamType::Int)
			return static_cast<double> (static_cast<long> (v + 0.5));
		return v;
	}

	double defaultNormalized () const { return toNormalized (plainDefault); }
};

//------------------------------------------------------------------------
extern const ParamDef kParams[kNumParams];

/** Look a definition up by id, range-checked. Returns kParams[kOutput]
    for anything unknown - including kBypass, which is not in the table. */
const ParamDef& paramDef (Steinberg::Vst::ParamID id);

/** True for an id the table actually describes. */
inline bool isTableParam (Steinberg::Vst::ParamID id) { return id < kNumParams; }

/** The plain value of a parameter, given the whole normalised set. */
inline double plainValue (const double* normalized, Steinberg::Vst::ParamID id)
{
	return kParams[id].toPlain (normalized[id]);
}

/** The name of one choice of an enumerated parameter. Never null. */
inline const char* enumChoiceName (Steinberg::Vst::ParamID id, int choice)
{
	if (id == kEra)
		return eraName (choice);
	if (id == kDisc)
		return discName (choice);
	if (id == kSpeed)
		return speedName (choice);
	if (id == kPlayer)
		return playerName (choice);
	if (id == kSpeaker)
		return speakerName (choice);
	if (id == kJumps)
		return choice == 0 ? "Off" : (choice == 1 ? "On" : "");
	return "";
}

//------------------------------------------------------------------------
} // namespace WornVinyl
