//------------------------------------------------------------------------
// WornVinyl - edit controller
//
// The parameter list the host sees, and the editor (WornVinylEditor.h).
//
// THE EDITOR POLLS this controller at 30 Hz rather than being pushed to,
// so the controller keeps no editor list - and the editorDestroyed trap
// cannot arise: EditorView::~EditorView() is one of its callers, by then
// the derived editor sub-object is gone, a dynamic_cast yields NULL, and
// the entry survives as a dangling pointer the next parameter change
// walks. If this class ever does track editors, compare UPCAST pointers:
//
//     static_cast<EditorView*> (e) == editor
//
// VocalFilterController.cpp has the worked version.
//------------------------------------------------------------------------

#pragma once

#include "WornVinylParams.h"

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace WornVinyl {

//------------------------------------------------------------------------
class WornVinylController : public Steinberg::Vst::EditControllerEx1
{
public:
	WornVinylController () = default;
	~WornVinylController () SMTG_OVERRIDE = default;

	static Steinberg::FUnknown* createInstance (void*)
	{
		return (Steinberg::Vst::IEditController*)new WornVinylController;
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	/** Every value change arrives here. A SKIP output that reaches 1 is
	    latched until the editor takes it (takeSkipped), so a jump shorter
	    than the editor's 33 ms tick still lights the lamp. */
	Steinberg::tresult PLUGIN_API setParamNormalized (Steinberg::Vst::ParamID tag,
	                                                  Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;

	/** NEVER returns null for kEditor: Steinberg's AU wrapper (SDK 3.8.1)
	    always advertises a Cocoa UI and crashes the HOST when createView
	    returns null - MasterLoud's PORTING-NOTES 5a. */
	Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;

	/** True if the needle was skipping at any moment since the last call.
	    UI thread only. */
	bool takeSkipped ();

private:
	void addParameters ();

	bool mSkipLatch = false;
};

//------------------------------------------------------------------------
} // namespace WornVinyl
