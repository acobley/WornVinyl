//------------------------------------------------------------------------
// WornVinyl - edit controller implementation
//------------------------------------------------------------------------

#include "WornVinylController.h"
#include "WornVinylEditor.h"
#include "WornVinylIDs.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace WornVinyl {

//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylController::initialize (FUnknown* context)
{
	const tresult result = EditControllerEx1::initialize (context);
	if (result != kResultOk)
		return result;

	addParameters ();
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylController::terminate ()
{
	return EditControllerEx1::terminate ();
}

//------------------------------------------------------------------------
void WornVinylController::addParameters ()
{
	for (ParamID id = 0; id < kNumParams; ++id)
	{
		const ParamDef& def = kParams[id];

		// NEVER pass a null title or units to RangeParameter: it
		// dereferences both without a null check, and the symptom is the
		// VALIDATOR segfaulting in the post-build step rather than
		// anything pointing at this line. Empty string, never null.
		String128 title;
		String128 units;
		UString (title, str16BufferSize (String128)).assign (def.title ? def.title : "Parameter");
		UString (units, str16BufferSize (String128)).assign (def.units ? def.units : "");

		// The outputs are READ-ONLY and HIDDEN: they change every block
		// (the disc's angle), so a host's generic list would flicker.
		const int32 flags = isOutputParam (id)
			? (ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden)
			: ParameterInfo::kCanAutomate;

		// An enumerated parameter is a StringListParameter, not a
		// RangeParameter with a step count: the host shows the names, and
		// the string round-trip is exact.
		if (def.type == ParamType::Enum)
		{
			auto* list = new StringListParameter (title, id, units, flags);
			for (int choice = 0; choice <= static_cast<int> (def.plainMax); ++choice)
			{
				String128 name;
				UString (name, str16BufferSize (String128)).assign (enumChoiceName (id, choice));
				list->appendString (name);
			}
			parameters.addParameter (list);
			list->setNormalized (def.defaultNormalized ());
			continue;
		}

		// Every range here is LINEAR in the units shown, so the
		// validator's string round-trip at the current value is exact. A
		// non-linear display mapping needs toString AND fromString
		// overridden.
		RangeParameter* parameter = new RangeParameter (
			title, id, units, def.plainMin, def.plainMax, def.plainDefault, def.stepCount, flags);
		parameters.addParameter (parameter);
	}

	// The host's own bypass. Its id is 1000, far past the end of the
	// table, which is why everything that indexes kParams range-checks
	// first.
	{
		String128 title;
		String128 units;
		UString (title, str16BufferSize (String128)).assign ("Bypass");
		UString (units, str16BufferSize (String128)).assign ("");

		auto* bypass = new StringListParameter (
			title, kBypass, units, ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass);
		bypass->appendString (STR16 ("Off"));
		bypass->appendString (STR16 ("On"));
		parameters.addParameter (bypass);
		bypass->setNormalized (0.0);
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylController::setComponentState (IBStream* state)
{
	// The SAME layout WornVinylProcessor::getState writes. If one side
	// changes, both change - and a short stream resets everything it does
	// not mention back to its default, exactly as the processor does.
	if (state == nullptr)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	int32 version = 0;
	if (! streamer.readInt32 (version))
		return kResultFalse;
	if (version > kStateVersion)
		return kResultFalse;

	for (ParamID id = 0; id < kNumStoredParams; ++id)
		setParamNormalized (id, kParams[id].defaultNormalized ());
	setParamNormalized (kBypass, 0.0);

	double value = 0.0;
	for (ParamID id = 0; id < kNumStoredParams; ++id)
	{
		if (! streamer.readDouble (value))
			break;
		setParamNormalized (id, std::min (1.0, std::max (0.0, value)));
	}

	int32 bypass = 0;
	if (streamer.readInt32 (bypass))
		setParamNormalized (kBypass, bypass ? 1.0 : 0.0);

	return kResultOk;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API WornVinylController::createView (FIDString name)
{
	if (name && FIDStringsEqual (name, ViewType::kEditor))
		return new WornVinylEditor (this);
	return nullptr;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylController::setParamNormalized (ParamID tag, ParamValue value)
{
	if (tag == kSkipping && value >= 0.5)
		mSkipLatch = true;
	return EditControllerEx1::setParamNormalized (tag, value);
}

//------------------------------------------------------------------------
bool WornVinylController::takeSkipped ()
{
	const bool was = mSkipLatch || getParamNormalized (kSkipping) >= 0.5;
	mSkipLatch = false;
	return was;
}

//------------------------------------------------------------------------
} // namespace WornVinyl
