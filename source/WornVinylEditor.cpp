//------------------------------------------------------------------------
// WornVinyl - the editor, implementation
//------------------------------------------------------------------------

#include "WornVinylEditor.h"
#include "WornVinylController.h"
#include "WornVinylParams.h"
#include "version.h"

// The commit this was built from - generated on every build by
// cmake-git-version.cmake. Absent (a build outside CMake), the About box
// says "not built from git".
#if __has_include("WornVinylGitVersion.h")
#include "WornVinylGitVersion.h"
#endif
#ifndef WORNVINYL_GIT_COMMIT
#define WORNVINYL_GIT_COMMIT ""
#define WORNVINYL_GIT_BRANCH ""
#define WORNVINYL_GIT_DIRTY  0
#endif

#include "vstgui/vstgui.h"

#include <cmath>

#if SMTG_OS_WINDOWS
#include <windows.h>
#else
#include <dlfcn.h>
#include <stdlib.h>
#endif

using namespace Steinberg;
using namespace VSTGUI;

namespace WornVinyl {

namespace {
CRect toCRect (const Layout::Rect& r) { return CRect (r.left, r.top, r.right, r.bottom); }

#if SMTG_OS_WINDOWS
std::string narrow (const wchar_t* w)
{
	const int n = WideCharToMultiByte (CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1)
		return {};
	std::string s (static_cast<size_t> (n - 1), '\0');
	WideCharToMultiByte (CP_UTF8, 0, w, -1, s.data (), n, nullptr, nullptr);
	return s;
}
#endif

/** The file our own code was loaded from: ask the loader which binary holds
    this very function. (The AU wrapper's .component holds the VST3 inside
    it - AboutInfo.h's bundlePath () sorts out which bundle to name.) */
std::string moduleBinaryPath ()
{
#if SMTG_OS_WINDOWS
	HMODULE module = nullptr;
	if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                          reinterpret_cast<LPCWSTR> (&moduleBinaryPath), &module))
		return {};
	wchar_t buf[4096] = {};
	return GetModuleFileNameW (module, buf, 4096) ? narrow (buf) : std::string ();
#else
	Dl_info info = {};
	if (dladdr (reinterpret_cast<const void*> (&moduleBinaryPath), &info) && info.dli_fname)
		return info.dli_fname;
	return {};
#endif
}

/** The application we are running in. */
std::string processName ()
{
#if SMTG_OS_WINDOWS
	wchar_t buf[4096] = {};
	if (! GetModuleFileNameW (nullptr, buf, 4096))
		return {};
	std::string s = narrow (buf);
	const auto slash = s.find_last_of ("\\/");
	s = (slash == std::string::npos) ? s : s.substr (slash + 1);
	const auto dot = s.rfind ('.');
	return dot == std::string::npos ? s : s.substr (0, dot);
#elif SMTG_OS_MACOS
	const char* name = getprogname ();
	return name ? name : "";
#else
	return {};
#endif
}
} // anonymous namespace

//------------------------------------------------------------------------
WornVinylEditor::WornVinylEditor (Vst::EditController* controller)
: VSTGUIEditor (controller)
{
	ViewRect viewRect (0, 0, kEditorWidth, kEditorHeight);
	setRect (viewRect);
}

//------------------------------------------------------------------------
WornVinylController* WornVinylEditor::vinylController () const
{
	return static_cast<WornVinylController*> (getController ());
}

//------------------------------------------------------------------------
bool PLUGIN_API WornVinylEditor::open (void* parent, const PlatformType& platformType)
{
	if (frame != nullptr)
		return false;

	frame = new CFrame (CRect (0, 0, kEditorWidth, kEditorHeight), this);
	frame->setBackgroundColor (Panel::Palette::kGround);
	build ();

	if (! frame->open (parent, platformType))
	{
		frame->unregisterKeyboardHook (&mEscapeHook);
		mLogo = nullptr;
		mAbout = nullptr;
		frame->forget ();
		frame = nullptr;
		mControls.clear ();
		return false;
	}

	mLampHold = 0;
	mAboutGeneration = -1;
	refresh ();
	mTimer = makeOwned<CVSTGUITimer> ([this] (CVSTGUITimer*) { refresh (); }, kTimerMs);
	return true;
}

//------------------------------------------------------------------------
void PLUGIN_API WornVinylEditor::close ()
{
	// The timer first: its callback touches views the frame owns.
	mTimer = nullptr;
	mControls.clear ();
	mDiscSwitch = nullptr;
	mTurntable = nullptr;
	mSkipLamp = nullptr;
	mHeaderLine = mDiscNote = mTurntableLine = mSpeedLine = nullptr;
	mLogo = nullptr;
	mAbout = nullptr;

	if (frame)
	{
		frame->unregisterKeyboardHook (&mEscapeHook);
		frame->forget ();
		frame = nullptr;
	}
}

//------------------------------------------------------------------------
void WornVinylEditor::build ()
{
	using namespace Panel;
	auto* controller = getController ();
	auto current = [controller] (Vst::ParamID id) {
		return controller ? controller->getParamNormalized (id) : kParams[id].defaultNormalized ();
	};
	auto add = [&] (CControl* c, Vst::ParamID id) {
		c->setValueNormalized (static_cast<float> (current (id)));
		c->setDefaultValue (static_cast<float> (kParams[id].defaultNormalized ()));
		frame->addView (c);
		mControls.push_back (c);
	};

	frame->addView (new Backdrop (CRect (0, 0, kEditorWidth, kEditorHeight)));

	for (const auto& c : Layout::kControls)
	{
		const Vst::ParamID id = static_cast<Vst::ParamID> (c.tag);
		if (c.kind == Layout::Kind::Knob)
		{
			auto* k = new Knob (toCRect (c.r), this, static_cast<int32_t> (id), c.diameter);
			k->setLabel (c.label);
			k->setFormatter ([id] (float n) {
				return Layout::knobReadout (static_cast<int> (id), kParams[id].toPlain (n));
			});
			add (k, id);
			continue;
		}

		auto* sw = new RotarySwitch (toCRect (c.r), this, static_cast<int32_t> (id), c);
		std::vector<std::string> names;
		// What each position is printed as: Layout::positionName, which
		// the tests and tools/render-panel.py read too.
		for (int choice = 0; choice <= static_cast<int> (kParams[id].plainMax); ++choice)
			names.push_back (Layout::positionName (c.tag, choice));
		sw->setNames (names);
		sw->setLabel (c.label);
		add (sw, id);
		if (id == kDisc)
			mDiscSwitch = sw;
	}

	mTurntable = new Turntable (toCRect (Layout::kScreen));
	frame->addView (mTurntable);

	mSkipLamp = new Lamp (toCRect (Layout::kLamp), "SKIP");
	frame->addView (mSkipLamp);

	mHeaderLine = new TextLine (toCRect (Layout::kHeaderLine), Layout::kTextHeader, Palette::kInk);
	mDiscNote = new TextLine (toCRect (Layout::kDiscNote), Layout::kTextNote, Palette::kInkSoft, Align::Centre);
	mTurntableLine = new TextLine (toCRect (Layout::kTurntableLine), Layout::kTextNote, Palette::kInkSoft);
	mSpeedLine = new TextLine (toCRect (Layout::kSpeedLine), Layout::kTextNote, Palette::kInkSoft);
	frame->addView (mHeaderLine);
	frame->addView (mDiscNote);
	frame->addView (mTurntableLine);
	frame->addView (mSpeedLine);

	// ---- the wordmark opens the About box ---------------------------------
	mLogo = new LogoButton (toCRect (Layout::kWordmark), [this] () { openAbout (); });
	frame->addView (mLogo);

	// ---- the About box: last, so it is on top of everything; hidden --------
	mAbout = new AboutBox (CRect (0, 0, kEditorWidth, kEditorHeight), [this] () { closeAbout (); });
	mAbout->setVisible (false);
	frame->addView (mAbout);
	mEscapeHook.editor = this;
	frame->registerKeyboardHook (&mEscapeHook);
}

//------------------------------------------------------------------------
void WornVinylEditor::valueChanged (CControl* control)
{
	auto* controller = getController ();
	if (control == nullptr || controller == nullptr)
		return;
	const auto id = static_cast<Vst::ParamID> (control->getTag ());
	// Range-check: our settings only, never an output.
	if (! isTableParam (id) || isOutputParam (id))
		return;
	const double v = control->getValueNormalized ();
	controller->setParamNormalized (id, v);
	controller->performEdit (id, v);             // begin/end came via the frame
}

//------------------------------------------------------------------------
void WornVinylEditor::refresh ()
{
	auto* controller = vinylController ();
	if (controller == nullptr || frame == nullptr)
		return;

	auto norm = [controller] (Vst::ParamID id) { return controller->getParamNormalized (id); };
	auto plain = [&] (Vst::ParamID id) { return kParams[id].toPlain (norm (id)); };
	auto choice = [&] (Vst::ParamID id) { return static_cast<int> (kParams[id].toInternal (norm (id))); };

	// Controls follow their parameters unless the user holds one.
	for (CControl* c : mControls)
	{
		const double v = norm (static_cast<Vst::ParamID> (c->getTag ()));
		if (! c->isEditing () && std::fabs (c->getValueNormalized () - v) > 1e-6)
		{
			c->setValueNormalized (static_cast<float> (v));
			c->invalid ();
		}
	}

	const int era = choice (kEra);
	const int disc = choice (kDisc);
	const int speed = choice (kSpeed);
	const int player = choice (kPlayer);
	const int speaker = choice (kSpeaker);

	// The Disc switch: the era decides, except in the 1950s.
	if (mDiscSwitch)
		mDiscSwitch->setLocked (discLocked (era) ? effectiveDisc (era, disc) : -1);

	mHeaderLine->setText (Layout::headerLineText (era, disc, speed, player, speaker));
	mDiscNote->setText (Layout::discNoteText (era));
	mTurntableLine->setText (Layout::turntableLineText (era, disc, plain (kScratches), plain (kDust)));
	mSpeedLine->setText (Layout::speedLineText (era, disc, speed));

	// The turntable: the published angle, the settings as they stand.
	mTurntable->setDisc (era, disc, speed);
	mTurntable->setAngle (norm (kDiscAngle));
	mTurntable->setAmounts (plain (kScratches), plain (kDust), plain (kWarp));

	// SKIP: latched by the controller, held here for a quarter second.
	if (controller->takeSkipped ())
		mLampHold = 8;
	else if (mLampHold > 0)
		--mLampHold;
	mSkipLamp->setOn (mLampHold > 0);
	mTurntable->setSkipping (mLampHold > 0);

	// The About box, if it is up and the processor has been re-activated
	// (a new sample rate, block size, a bounce) since its rows were built.
	if (isAboutOpen () && controller->audioGeneration () != mAboutGeneration)
		refreshAbout ();
}

//------------------------------------------------------------------------
// The About box
//------------------------------------------------------------------------
About::Info WornVinylEditor::aboutInfo () const
{
	About::Info info;
	info.version     = FULL_VERSION_STR;
	info.commit      = WORNVINYL_GIT_COMMIT;
	info.branch      = WORNVINYL_GIT_BRANCH;
	info.dirty       = WORNVINYL_GIT_DIRTY != 0;
	info.binaryPath  = moduleBinaryPath ();
	info.processName = processName ();
	if (auto* controller = vinylController ())
	{
		info.audio    = controller->getAudioSettings ();
		info.hostName = controller->getHostName ();
	}
	return info;
}

bool WornVinylEditor::isAboutOpen () const
{
	return mAbout != nullptr && mAbout->isVisible ();
}

void WornVinylEditor::refreshAbout ()
{
	if (! mAbout)
		return;
	if (auto* controller = vinylController ())
		mAboutGeneration = controller->audioGeneration ();
	mAbout->setRows (About::rows (aboutInfo ()));
}

void WornVinylEditor::openAbout ()
{
	if (! mAbout || isAboutOpen ())
		return;
	refreshAbout ();
	mAbout->setVisible (true);
	mAbout->invalid ();
}

void WornVinylEditor::closeAbout ()
{
	// Called from the box's own mouse handler: hide it, never remove it.
	if (! isAboutOpen ())
		return;
	mAbout->setVisible (false);
	if (frame)
		frame->invalid ();
}

void WornVinylEditor::EscapeHook::onKeyboardEvent (KeyboardEvent& event, CFrame*)
{
	if (editor && editor->isAboutOpen () && event.type == EventType::KeyDown &&
	    event.virt == VirtualKey::Escape)
	{
		editor->closeAbout ();
		event.consumed = true;
	}
}

//------------------------------------------------------------------------
} // namespace WornVinyl
