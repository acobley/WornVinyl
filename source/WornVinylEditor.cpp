//------------------------------------------------------------------------
// WornVinyl - the editor, implementation
//------------------------------------------------------------------------

#include "WornVinylEditor.h"
#include "WornVinylController.h"
#include "WornVinylParams.h"

#include "vstgui/vstgui.h"

#include <cmath>

using namespace Steinberg;
using namespace VSTGUI;

namespace WornVinyl {

namespace {
CRect toCRect (const Layout::Rect& r) { return CRect (r.left, r.top, r.right, r.bottom); }
}

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
		frame->forget ();
		frame = nullptr;
		mControls.clear ();
		return false;
	}

	mLampHold = 0;
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

	if (frame)
	{
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
			if (id == kOutput)
				k->setFormatter ([] (float n) { return Layout::decibelText (kParams[kOutput].toPlain (n)); });
			else
			{
				const bool offAtZero = (id != kMix);
				k->setFormatter ([id, offAtZero] (float n) {
					return Layout::percentText (kParams[id].toPlain (n), offAtZero);
				});
			}
			add (k, id);
			continue;
		}

		auto* sw = new RotarySwitch (toCRect (c.r), this, static_cast<int32_t> (id), c);
		std::vector<std::string> names;
		for (int choice = 0; choice <= static_cast<int> (kParams[id].plainMax); ++choice)
		{
			// The era's positions read 1920 ... 1970, not "1920S".
			std::string name = enumChoiceName (id, choice);
			if (id == kEra && ! name.empty () && name.back () == 's')
				name.pop_back ();
			// Speed's numbers are rpm; Auto is the disc's own speed.
			if (id == kSpeed && choice == kSpeedAuto)
				name = "AUTO";
			names.push_back (name);
		}
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

	// The Disc switch: the era decides, except in the 1950s.
	if (mDiscSwitch)
		mDiscSwitch->setLocked (discLocked (era) ? effectiveDisc (era, disc) : -1);

	mHeaderLine->setText (Layout::headerLineText (era, disc, speed, player));
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
}

//------------------------------------------------------------------------
} // namespace WornVinyl
