//------------------------------------------------------------------------
// WornVinyl - parameter table tests. Needs the VST3 SDK's header-only
// pluginterfaces (for vsttypes.h); nothing is linked from it.
//
//   c++ -std=c++17 -O2 -Isource -I<sdk> tests/ParamsTests.cpp source/WornVinylParams.cpp
//       source/WornVinylDsp.cpp -o /tmp/paramtests && /tmp/paramtests      (one line)
//------------------------------------------------------------------------

#include "WornVinylLayout.h"
#include "WornVinylParams.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace WornVinyl;

static int gChecks = 0, gFails = 0;
#define CHECK(cond, ...)                                              \
	do {                                                              \
		++gChecks;                                                    \
		if (! (cond)) {                                               \
			++gFails;                                                 \
			std::printf ("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); \
			std::printf (__VA_ARGS__);                                \
			std::printf ("\n");                                       \
		}                                                             \
	} while (0)

int main ()
{
	for (Steinberg::Vst::ParamID id = 0; id < kNumParams; ++id)
	{
		const ParamDef& d = kParams[id];
		CHECK (d.id == id, "row %u sits at its own id", id);
		CHECK (d.title && *d.title && d.units, "row %u: title and units never null", id);
		CHECK (d.plainDefault >= d.plainMin && d.plainDefault <= d.plainMax, "row %u: default in range", id);
		const double n = d.defaultNormalized ();
		CHECK (n >= 0 && n <= 1, "row %u: normalised default in 0..1", id);
		CHECK (std::fabs (d.toPlain (n) - d.plainDefault) < 1e-9, "row %u: default round-trips", id);
		if (d.type == ParamType::Enum)
		{
			for (int c = 0; c <= static_cast<int> (d.plainMax); ++c)
			{
				CHECK (std::strlen (enumChoiceName (id, c)) > 0, "row %u choice %d has a name", id, c);
				CHECK (static_cast<int> (d.toInternal (d.toNormalized (c))) == c, "row %u choice %d round-trips", id, c);
			}
			CHECK (d.stepCount == static_cast<int> (d.plainMax), "row %u: one step per choice", id);
		}
	}
	CHECK (static_cast<int> (kParams[kEra].toInternal (kParams[kEra].defaultNormalized ())) == kEraDefault, "era default");
	CHECK (static_cast<int> (kParams[kDisc].toInternal (kParams[kDisc].defaultNormalized ())) == kDiscDefault, "disc default");
	CHECK (kParams[kJumps].toInternal (kParams[kJumps].defaultNormalized ()) >= 0.5, "Jumps on by default");
	CHECK (std::fabs (kParams[kOutput].defaultNormalized () - 0.5) < 1e-12, "Output's unity is the middle");

	// The layout restates the ids as plain ints to stay SDK-free.
	CHECK (int (Layout::kTagEra) == int (kEra) && int (Layout::kTagDisc) == int (kDisc) && int (Layout::kTagScratches) == int (kScratches) &&
	       int (Layout::kTagDust) == int (kDust) && int (Layout::kTagWow) == int (kWow) && int (Layout::kTagFlutter) == int (kFlutter) &&
	       int (Layout::kTagWarp) == int (kWarp) && int (Layout::kTagJumps) == int (kJumps) && int (Layout::kTagMix) == int (kMix) &&
	       int (Layout::kTagOutput) == int (kOutput) && int (Layout::kTagSpeed) == int (kSpeed) && int (Layout::kTagPlayer) == int (kPlayer) &&
	       int (Layout::kTagSpeaker) == int (kSpeaker),
	       "the layout's tags are the parameter ids");
	CHECK (static_cast<int> (kParams[kSpeed].toInternal (kParams[kSpeed].defaultNormalized ())) == kSpeedAuto,
	       "Speed defaults to Auto, so a project saved before it existed sounds the same");
	CHECK (isLateSetting (kSpeed) && kSpeed >= kNumStoredParams, "Speed is saved after the bypass");
	CHECK (static_cast<int> (kParams[kPlayer].toInternal (kParams[kPlayer].defaultNormalized ())) == kPlayerPeriod,
	       "Player defaults to Period - how older projects sounded");
	CHECK (isLateSetting (kPlayer) && kPlayer > kSpeed, "Player is saved after Speed");
	CHECK (static_cast<int> (kParams[kSpeaker].toInternal (kParams[kSpeaker].defaultNormalized ())) == kSpeakerOff,
	       "Speaker defaults to Off - how older projects sounded");
	CHECK (isLateSetting (kSpeaker) && kSpeaker > kPlayer && kSpeaker == kNumParams - 1, "Speaker is saved after Player, last");
	CHECK (std::strcmp (enumChoiceName (kSpeaker, kSpeakerHorn), "1920s Horn") == 0 &&
	       std::strcmp (enumChoiceName (kSpeaker, kSpeakerHiFi), "1970s 3-Way") == 0, "the host's speaker names");
	for (const auto& c : Layout::kControls)
	{
		CHECK (! isOutputParam (static_cast<Steinberg::Vst::ParamID> (c.tag)), "no control writes an output");
		if (c.kind == Layout::Kind::Selector)
			CHECK (Layout::positionCount (c.tag) == kParams[c.tag].stepCount + 1,
			       "%s: the panel prints as many positions as the parameter has", c.label);
	}

	CHECK (&paramDef (kBypass) == &kParams[kOutput], "an unknown id never indexes past the table");
	CHECK (! isTableParam (kBypass), "bypass is not a table id");

	std::printf ("Params: %d checks, %d failed\n", gChecks, gFails);
	return gFails ? 1 : 0;
}
