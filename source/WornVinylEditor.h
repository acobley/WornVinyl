//------------------------------------------------------------------------
// WornVinyl - the editor
//
// The black panel (WornVinylLayout.h has the layout, WornVinylPanelViews.h
// the views): RECORD (Era, Disc, Mix, Output), the TURNTABLE screen,
// SURFACE (Scratches, Dust, Jumps) and SPEED (Wow, Flutter, Warp). Every
// word in the 70s sci-fi lettering, drawn as stroked paths: no bitmaps,
// no system font.
//
// THE AU WRAPPER NEEDS AN EDITOR. Steinberg's AU wrapper (SDK 3.8.1)
// always advertises a Cocoa UI and crashes the HOST if createView returns
// null (the audioUnit / plugView ivar mix-up in aucocoaview.mm -
// MasterLoud's PORTING-NOTES 5a). This editor is what createView returns;
// it must never be removed without putting something in its place.
//
// HOW IT TALKS TO THE CONTROLLER
//
//   * A control's begin/end edit reaches the controller through the frame
//     (CControl -> CFrame -> VSTGUIEditor::beginEdit); valueChanged()
//     below sends the value with performEdit.
//   * A 30 Hz timer polls the controller: every control follows host
//     automation and state loads unless the user has hold of it, the disc
//     turns to the published angle, the lamp takes the latched SKIP.
//   * Polling rather than being pushed keeps the controller free of an
//     editor list, so the editorDestroyed trap cannot arise.
//
// THE ABOUT BOX (SpaceDub's, 25 Nov 2026): click WORN VINYL and a box opens
// over the dimmed panel - version, the git commit it was built from, VST3
// or AU, the host, the audio settings, where it was loaded from. Any click
// or Escape closes it. What it says is AboutInfo.h.
//------------------------------------------------------------------------

#pragma once

#include "WornVinylPanelViews.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"

#include <vector>

namespace WornVinyl {

class WornVinylController;

//------------------------------------------------------------------------
class WornVinylEditor : public Steinberg::Vst::VSTGUIEditor,
                        public VSTGUI::IControlListener
{
public:
	static constexpr int kEditorWidth  = Layout::kEditorWidth;
	static constexpr int kEditorHeight = Layout::kEditorHeight;
	static constexpr int kTimerMs = 33;

	explicit WornVinylEditor (Steinberg::Vst::EditController* controller);

	bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
	void PLUGIN_API close () SMTG_OVERRIDE;

	/** A control moved: tell the controller and the host. */
	void valueChanged (VSTGUI::CControl* control) override;

	/** The About box: opened by clicking the wordmark, closed by any click
	    or Escape. refreshAbout () rebuilds its rows (AboutInfo.h). */
	void openAbout ();
	void closeAbout ();
	bool isAboutOpen () const;
	About::Info aboutInfo () const;

private:
	void build ();
	void refresh ();
	WornVinylController* vinylController () const;
	void refreshAbout ();

	/** Every control and the parameter behind it. */
	std::vector<VSTGUI::CControl*> mControls;   // owned by the frame

	Panel::RotarySwitch* mDiscSwitch = nullptr;
	Panel::Turntable* mTurntable = nullptr;
	Panel::Lamp* mSkipLamp = nullptr;
	Panel::TextLine* mHeaderLine = nullptr;
	Panel::TextLine* mDiscNote = nullptr;
	Panel::TextLine* mTurntableLine = nullptr;
	Panel::TextLine* mSpeedLine = nullptr;

	/** The lamp stays lit this many more ticks after a skip, so a 6 ms
	    jump is still seen. */
	int mLampHold = 0;

	Panel::LogoButton* mLogo = nullptr;
	Panel::AboutBox* mAbout = nullptr;
	/** The controller's audioGeneration () when the box's rows were built. */
	int mAboutGeneration = -1;

	/** Escape closes the About box. A hook on the frame, because the box
	    never has keyboard focus. */
	struct EscapeHook : VSTGUI::IKeyboardHook
	{
		WornVinylEditor* editor = nullptr;
		void onKeyboardEvent (VSTGUI::KeyboardEvent& event, VSTGUI::CFrame* frame) override;
	};
	EscapeHook mEscapeHook;

	VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> mTimer;
};

//------------------------------------------------------------------------
} // namespace WornVinyl
