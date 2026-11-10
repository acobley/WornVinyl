//------------------------------------------------------------------------
// WornVinyl - the panel's views
//
// R2Synth's black panel (branch PolivoksUI, Designs/R2Synth-Polivoks-1c-
// OLED.png) carried across: matte black, white printing, boxed sections,
// black knurled knobs with printed scales, chicken-head selectors, a
// colour OLED, and every letter in Andy's 70s sci-fi lettering
// (SciFiLettering.h). Knob and RotarySwitch are R2Synth's, without the
// worn-panel marks; RotarySwitch has grown to five positions and can be
// LOCKED to one (the Disc switch outside the 1950s).
//
//   Backdrop      everything that never changes, from WornVinylLayout.h
//   Knob          a black knurled knob, its scale, reading and label
//   RotarySwitch  a chicken-head selector, its positions printed round it
//   Turntable     the OLED: the disc turning, its scratches and dust, the
//                 arm riding the warp
//   Lamp          the SKIP lamp
//   TextLine      one line of lettering whose words change
//
// THE SAME TWO RULES AS R2SYNTH. No bitmaps - everything is paths, the
// lettering included, so the panel is resolution-independent and
// resource/ needs no artwork. And no numbers made here: every reading
// comes from a formatter in WornVinylLayout.h, every scratch from
// scratchSlot() - the one the stylus hits.
//------------------------------------------------------------------------

#pragma once

#include "SciFiLettering.h"
#include "WornVinylLayout.h"

#include "vstgui/vstgui.h"

#include <functional>
#include <string>
#include <vector>

namespace WornVinyl {
namespace Panel {

//------------------------------------------------------------------------
namespace Palette {

const VSTGUI::CColor kGround     ( 18,  18,  18, 255);
const VSTGUI::CColor kHeader     ( 12,  12,  12, 255);
const VSTGUI::CColor kHeaderRule (233, 230, 220, 255);
const VSTGUI::CColor kSection    ( 21,  21,  21, 255);
const VSTGUI::CColor kSectionEdge(217, 214, 204, 255);
const VSTGUI::CColor kInk        (244, 241, 232, 255);   // ALL the lettering: white on black
const VSTGUI::CColor kInkSoft    (185, 182, 174, 255);   // subtitles, notes
const VSTGUI::CColor kInkDim     (143, 140, 133, 255);   // a selector's positions not set
const VSTGUI::CColor kDivider    ( 90,  88,  82, 255);
const VSTGUI::CColor kRule       ( 58,  58,  58, 255);
const VSTGUI::CColor kTick       (233, 230, 220, 255);
const VSTGUI::CColor kRed        (226,  52,  43, 255);   // the position that is set
const VSTGUI::CColor kScrew      ( 58,  58,  58, 255);
const VSTGUI::CColor kKnobBody   ( 10,  10,  10, 255);
const VSTGUI::CColor kKnobRim    ( 47,  47,  47, 255);
const VSTGUI::CColor kBezel      (  5,   5,   5, 255);
const VSTGUI::CColor kBezelEdge  ( 60,  60,  60, 255);
const VSTGUI::CColor kScreen     (  0,   0,   0, 255);
const VSTGUI::CColor kGridDot    ( 42,  45,  51, 255);
const VSTGUI::CColor kScreenText (141, 150, 163, 255);
const VSTGUI::CColor kOled       (255, 176,  46, 255);   // the disc's amber
const VSTGUI::CColor kArm        (200, 204, 210, 255);
const VSTGUI::CColor kLampOn     (255,  59,  47, 255);
const VSTGUI::CColor kLampOff    ( 58,  21,  18, 255);

} // namespace Palette

//------------------------------------------------------------------------
enum class Align { Left, Centre, Right };

/** DRAW A RUN OF LETTERING: `text` in `style`, its ink box vertically
    centred on `centreY`, starting at, centred on or ending at `x`.
    `strokeUnits` > 0 strokes the same glyphs at another weight without
    moving them - the wordmark's inline cut. */
void drawLettering (VSTGUI::CDrawContext* context, const std::string& text, const Lettering::Style& style,
                    const VSTGUI::CColor& colour, VSTGUI::CCoord x, VSTGUI::CCoord centreY,
                    Align align = Align::Left, double strokeUnits = -1.0);

//------------------------------------------------------------------------
class Backdrop : public VSTGUI::CView
{
public:
	explicit Backdrop (const VSTGUI::CRect& size);
	void draw (VSTGUI::CDrawContext* context) override;
	CLASS_METHODS (Backdrop, VSTGUI::CView)
};

//------------------------------------------------------------------------
/** A ROTARY KNOB: black, knurled, a white line, a printed scale of eleven
    ticks over 270 degrees.

      drag           vertically; up increases; 200 pixels is the range
      Shift          ten times finer, drag and wheel alike
      double-click   the parameter's default (setDefaultValue)
      wheel          one per cent a click

    The whole rectangle is the target, reading and label included. */
class Knob : public VSTGUI::CControl
{
public:
	Knob (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag, int diameter);

	void setLabel (const std::string& label) { mLabel = Layout::upperCase (label); }
	void setFormatter (std::function<std::string (float)> formatter) { mFormatter = std::move (formatter); }

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseMoveEvent (VSTGUI::MouseMoveEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseCancelEvent (VSTGUI::MouseCancelEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (Knob, VSTGUI::CControl)

	static double angleOf (double normalized) { return 135.0 + 270.0 * normalized; }

private:
	int mDiameter;
	std::string mLabel;
	std::function<std::string (float)> mFormatter;
	bool mDragging = false;
	VSTGUI::CCoord mLastY = 0.;
};

//------------------------------------------------------------------------
/** A ROTARY SELECTOR: a chicken-head knob, its positions printed round it
    (the one that is set in white with a red mark, the others dim), its
    name under it. Geometry from Layout::selectorOf (), which the tests
    read too.

      click a position's name (or off the knob towards it)   that one
      click the knob     the next position, round to the first again
      wheel              steps

    LOCKED (setLocked (i)): the knob shows position i whatever the
    parameter holds, and ignores the mouse - the Disc switch when the era
    decides the disc. The parameter keeps its own value, so the 1950s
    remember the choice. */
class RotarySwitch : public VSTGUI::CControl
{
public:
	RotarySwitch (const VSTGUI::CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
	              const Layout::Control& layout);

	void setNames (const std::vector<std::string>& names);
	void setLabel (const std::string& label) { mLabel = Layout::upperCase (label); }
	void setLocked (int index);

	void draw (VSTGUI::CDrawContext* context) override;
	void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;
	void onMouseUpEvent (VSTGUI::MouseUpEvent& event) override;
	void onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override;

	CLASS_METHODS (RotarySwitch, VSTGUI::CControl)

private:
	int count () const { return static_cast<int> (mNames.size ()); }
	int currentIndex () const;
	void select (int index);

	Layout::Control mLayout;
	Layout::Selector mGeometry;
	std::vector<std::string> mNames;
	std::string mLabel;
	int mLocked = -1;
};

//------------------------------------------------------------------------
/** THE TURNTABLE SCREEN: an amber OLED with the disc turning on it at the
    angle the DSP publishes, the era's label, the scratches (scratchSlot,
    activeScratches) where the stylus will meet them, dust specks, and the
    arm whose head lifts over the warp's high spot (warpBump). */
class Turntable : public VSTGUI::CView
{
public:
	explicit Turntable (const VSTGUI::CRect& size);

	void setDisc (int era, int disc, int speed);
	void setAngle (double discAngle);
	void setAmounts (double scratchesPct, double dustPct, double warpPct);
	void setSkipping (bool skipping);

	void draw (VSTGUI::CDrawContext* context) override;
	CLASS_METHODS (Turntable, VSTGUI::CView)

private:
	int mEra = kEraDefault, mDisc = kDiscDefault, mSpeed = kSpeedDefault;
	double mAngle = 0.0;
	double mScratches = 0.0, mDust = 0.0, mWarp = 0.0;
	bool mSkipping = false;
};

//------------------------------------------------------------------------
/** The SKIP lamp: a red lamp, its name to the right. */
class Lamp : public VSTGUI::CView
{
public:
	Lamp (const VSTGUI::CRect& size, const std::string& name);
	void setOn (bool on);
	void draw (VSTGUI::CDrawContext* context) override;
	CLASS_METHODS (Lamp, VSTGUI::CView)

private:
	std::string mName;
	bool mOn = false;
};

//------------------------------------------------------------------------
/** One line of lettering whose words change. Left-aligned or centred,
    vertically centred in the view. Mouse-disabled. */
class TextLine : public VSTGUI::CView
{
public:
	TextLine (const VSTGUI::CRect& size, const Lettering::Style& style, const VSTGUI::CColor& colour,
	          Align align = Align::Left);
	void setText (const std::string& text);
	void draw (VSTGUI::CDrawContext* context) override;
	CLASS_METHODS (TextLine, VSTGUI::CView)

private:
	Lettering::Style mStyle;
	VSTGUI::CColor mColour;
	Align mAlign;
	std::string mText;
};

//------------------------------------------------------------------------
} // namespace Panel
} // namespace WornVinyl
