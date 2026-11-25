//------------------------------------------------------------------------
// WornVinyl - the panel's views (the black panel)
//------------------------------------------------------------------------

#include "WornVinylPanelViews.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace VSTGUI;

namespace WornVinyl {
namespace Panel {

//------------------------------------------------------------------------
namespace {

constexpr double kPi = 3.14159265358979323846;

/** VSTGUI's angles: degrees, 0 at three o'clock, clockwise (y is down). */
CPoint pointAt (const CPoint& centre, double radius, double degrees)
{
	const double t = degrees * kPi / 180.0;
	return CPoint (centre.x + radius * std::cos (t), centre.y + radius * std::sin (t));
}

/** The layout's angles: degrees from twelve o'clock, clockwise. */
CPoint pointFromTop (const CPoint& centre, double radius, double degrees)
{
	return pointAt (centre, radius, degrees - 90.0);
}

CRect circleRect (const CPoint& centre, double radius)
{
	return CRect (centre.x - radius, centre.y - radius, centre.x + radius, centre.y + radius);
}

CRect toCRect (const Layout::Rect& r)
{
	return CRect (r.left, r.top, r.right, r.bottom);
}

CColor withAlpha (CColor c, uint8_t alpha)
{
	c.alpha = alpha;
	return c;
}

CLineStyle roundLine ()
{
	return CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound);
}

void strokeLine (CDrawContext* context, const CPoint& from, const CPoint& to, const CColor& colour, CCoord width)
{
	context->setFrameColor (colour);
	context->setLineWidth (width);
	context->setLineStyle (roundLine ());
	context->drawLine (from, to);
}

void fillCircle (CDrawContext* context, const CPoint& centre, double radius, const CColor& fill)
{
	context->setFillColor (fill);
	context->drawEllipse (circleRect (centre, radius), kDrawFilled);
}

void strokeCircle (CDrawContext* context, const CPoint& centre, double radius, const CColor& colour, CCoord width)
{
	context->setFrameColor (colour);
	context->setLineWidth (width);
	context->setLineStyle (kLineSolid);
	context->drawEllipse (circleRect (centre, radius), kDrawStroked);
}

void roundRect (CDrawContext* context, const CRect& r, CCoord radius, const CColor* fill,
                const CColor* stroke = nullptr, CCoord strokeWidth = 1.)
{
	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;
	path->addRoundRect (r, radius);
	if (fill)
	{
		context->setFillColor (*fill);
		context->drawGraphicsPath (path, CDrawContext::kPathFilled);
	}
	if (stroke)
	{
		context->setFrameColor (*stroke);
		context->setLineWidth (strokeWidth);
		context->setLineStyle (kLineSolid);
		context->drawGraphicsPath (path, CDrawContext::kPathStroked);
	}
}

/** A black knob's cap: lit from the top left, a near-black at the rim. */
void fillCap (CDrawContext* context, CGraphicsPath* path, const CPoint& centre, double radius)
{
	auto gradient = owned (CGradient::create (0.0, 1.0, CColor (58, 58, 58, 255), CColor (5, 5, 5, 255)));
	if (!gradient)
		return;
	gradient->addColorStop (0.6, CColor (22, 22, 22, 255));
	context->fillRadialGradient (path, *gradient, centre, radius * 1.4, CPoint (-radius * 0.25, -radius * 0.35));
}

/** The glyphs, parsed once each. */
const std::vector<Lettering::Command>& commandsOf (const Lettering::Glyph& glyph)
{
	static std::map<const char*, std::vector<Lettering::Command>> cache;
	auto it = cache.find (glyph.path);
	if (it == cache.end ())
		it = cache.emplace (glyph.path, Lettering::parse (glyph.path)).first;
	return it->second;
}

/** Each era's label colour on the screen: Layout::eraLabelColour, which
    tools/render-panel.py reads too. */
CColor labelColour (int era)
{
	const Layout::Rgb c = Layout::eraLabelColour (era);
	return CColor (static_cast<uint8_t> (c.r), static_cast<uint8_t> (c.g), static_cast<uint8_t> (c.b), 255);
}

} // anonymous namespace

//------------------------------------------------------------------------
// Lettering
//------------------------------------------------------------------------
void drawLettering (CDrawContext* context, const std::string& text, const Lettering::Style& style,
                    const CColor& colour, CCoord x, CCoord centreY, Align align, double strokeUnits)
{
	double endUnits = 0.0;
	const auto placed = Lettering::layout (text, style, &endUnits);
	if (placed.empty ())
		return;

	const double s = style.scale ();
	const double width = Lettering::width (text, style);
	double left = x;
	if (align == Align::Centre)
		left = x - width / 2.0;
	else if (align == Align::Right)
		left = x - width;
	const double top = centreY - style.height () / 2.0;
	const double half = style.weight / 2.0;

	auto path = owned (context->createGraphicsPath ());
	if (!path)
		return;

	for (const auto& p : placed)
	{
		const double ox = left + (p.x + half) * s;
		const double oy = top + half * s;
		auto at = [&] (double gx, double gy) { return CPoint (ox + gx * s, oy + gy * s); };

		CPoint current, start;
		for (const auto& c : commandsOf (Lettering::glyphOrDot (p.code)))
		{
			switch (c.op)
			{
				case 'M':
					current = start = at (c.x, c.y);
					path->beginSubpath (current);
					break;
				case 'L':
					current = at (c.x, c.y);
					path->addLine (current);
					break;
				case 'Q':
				{
					// The quadratic as a cubic: c1 = p0 + 2/3 (q - p0),
					// c2 = p2 + 2/3 (q - p2).
					const CPoint q = at (c.x1, c.y1);
					const CPoint end = at (c.x, c.y);
					const CPoint c1 (current.x + 2.0 / 3.0 * (q.x - current.x), current.y + 2.0 / 3.0 * (q.y - current.y));
					const CPoint c2 (end.x + 2.0 / 3.0 * (q.x - end.x), end.y + 2.0 / 3.0 * (q.y - end.y));
					path->addBezierCurve (c1, c2, end);
					current = end;
					break;
				}
				case 'Z':
					path->closeSubpath ();
					current = start;
					break;
				default:
					break;
			}
		}
	}

	context->setFrameColor (colour);
	context->setLineWidth ((strokeUnits > 0.0 ? strokeUnits : style.weight) * s);
	context->setLineStyle (roundLine ());
	context->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

//------------------------------------------------------------------------
// Backdrop
//------------------------------------------------------------------------
Backdrop::Backdrop (const CRect& size)
: CView (size)
{
	setMouseEnabled (false);
}

void Backdrop::draw (CDrawContext* context)
{
	using namespace Layout;
	const CRect all = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	// ---- the ground, the header band and the white rule under it --------
	context->setFillColor (Palette::kGround);
	context->drawRect (all, kDrawFilled);
	context->setFillColor (Palette::kHeader);
	context->drawRect (CRect (all.left, all.top, all.right, all.top + kHeaderHeight), kDrawFilled);
	context->setFillColor (Palette::kHeaderRule);
	context->drawRect (CRect (all.left, kHeaderHeight - 2, all.right, kHeaderHeight), kDrawFilled);

	// ---- the corner screws ------------------------------------------------
	for (const auto& s : kScrews)
	{
		const CPoint c (s.x, s.y);
		fillCircle (context, c, 3.2, Palette::kScrew);
		strokeCircle (context, c, 3.2, CColor (0, 0, 0, 255), 0.6);
		strokeLine (context, CPoint (c.x - 2.2, c.y - 2.2), CPoint (c.x + 2.2, c.y + 2.2), CColor (34, 34, 34, 255), 0.8);
	}

	// ---- the wordmark: WORN VINYL, heavy, with the inline cut -------------
	{
		const double centreY = kHeaderHeight / 2.0 + 1.0;
		const double second = kWordmarkLeft + Lettering::width ("WORN", kTextWordmark) + kWordmarkGap;
		for (int pass = 0; pass < 2; ++pass)
		{
			const CColor ink = pass ? Palette::kHeader : Palette::kInk;
			const double stroke = pass ? 0.32 : -1.0;
			drawLettering (context, "WORN", kTextWordmark, ink, kWordmarkLeft, centreY, Align::Left, stroke);
			drawLettering (context, "VINYL", kTextWordmark, ink, second, centreY, Align::Left, stroke);
		}
		context->setFillColor (Palette::kDivider);
		context->drawRect (CRect (kHeaderDivider, 16, kHeaderDivider + 1, 40), kDrawFilled);
	}

	// ---- the sections: boxed in white, the title in its own box -----------
	for (const auto& s : kSections)
	{
		CRect r = toCRect (s.r);
		r.inset (0.5, 0.5);
		roundRect (context, r, kSectionRadius, &Palette::kSection, &Palette::kSectionEdge, 1.2);

		const Box b = titleBox (s);
		roundRect (context, CRect (b.left, b.top, b.right, b.bottom), 2., &Palette::kSection,
		           &Palette::kSectionEdge, 1.2);
		const double centre = (b.top + b.bottom) / 2.0;
		drawLettering (context, s.title, kTextTitle, Palette::kInk, b.left + 8.0, centre);
		if (s.subtitle && *s.subtitle)
			drawLettering (context, s.subtitle, kTextSubtitle, Palette::kInkSoft, subtitleLeft (s), centre);
	}

	// ---- the rule over OUTPUT, and its heading ------------------------------
	context->setFillColor (Palette::kRule);
	context->drawRect (toCRect (kRecordRule), kDrawFilled);
	drawLettering (context, "OUTPUT", kTextBlock, Palette::kInk, kOutputTitle.left,
	               (kOutputTitle.top + kOutputTitle.bottom) / 2.0);
	drawLettering (context, kSurfaceNoteText, kTextNote, Palette::kInkSoft, kSurfaceNote.left,
	               (kSurfaceNote.top + kSurfaceNote.bottom) / 2.0);

	// ---- the screen's black cut-out ------------------------------------------
	{
		CRect r = toCRect (bezelOf (kScreen));
		r.inset (0.5, 0.5);
		roundRect (context, r, 4., &Palette::kBezel, &Palette::kBezelEdge, 1.);
	}

	setDirty (false);
}

//------------------------------------------------------------------------
// Knob
//------------------------------------------------------------------------
namespace {
constexpr float kDragPixels = 200.f;
} // anonymous namespace

Knob::Knob (const CRect& size, IControlListener* listener, int32_t tag, int diameter)
: CControl (size, listener, tag)
, mDiameter (diameter)
{
}

void Knob::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	const double v = std::clamp (static_cast<double> (getValueNormalized ()), 0.0, 1.0);
	const CPoint centre (r.left + r.getWidth () / 2., r.top + mDiameter / 2.);
	const double outer = mDiameter / 2.0;
	const double body = outer - ((mDiameter >= 44) ? 6.0 : 5.0);
	const double a = angleOf (v);

	// ---- the printed scale: eleven ticks, long at the ends and the middle
	for (int i = 0; i <= 10; ++i)
	{
		const double t = 135.0 + 27.0 * i;
		const bool longTick = (i == 0 || i == 5 || i == 10);
		strokeLine (context, pointAt (centre, body + 1.5, t), pointAt (centre, outer - (longTick ? 0.0 : 1.8), t),
		            Palette::kTick, longTick ? 1.3 : 0.9);
	}
	{
		const CPoint lo = pointFromTop (centre, outer + 2.0, -138.0);
		const CPoint hi = pointFromTop (centre, outer + 2.0, 138.0);
		drawLettering (context, "0", Layout::kTextTick, Palette::kTick, lo.x - 2., lo.y + 4., Align::Right);
		drawLettering (context, "10", Layout::kTextTick, Palette::kTick, hi.x + 2., hi.y + 4., Align::Left);
	}

	// ---- the knurled body -----------------------------------------------------
	{
		auto rim = owned (context->createGraphicsPath ());
		if (rim)
		{
			constexpr int kTeeth = 28;
			for (int i = 0; i < kTeeth * 2; ++i)
			{
				const CPoint p = pointAt (centre, (i % 2 == 0) ? body : body - 1.4, 360.0 * i / (kTeeth * 2));
				if (i == 0)
					rim->beginSubpath (p);
				else
					rim->addLine (p);
			}
			rim->closeSubpath ();
			context->setFillColor (Palette::kKnobBody);
			context->drawGraphicsPath (rim, CDrawContext::kPathFilled);
			context->setFrameColor (Palette::kKnobRim);
			context->setLineWidth (0.6);
			context->setLineStyle (kLineSolid);
			context->drawGraphicsPath (rim, CDrawContext::kPathStroked);
		}
		auto cap = owned (context->createGraphicsPath ());
		if (cap)
		{
			cap->addEllipse (circleRect (centre, body * 0.78));
			fillCap (context, cap, centre, body * 0.78);
			context->setFrameColor (CColor (0, 0, 0, 255));
			context->setLineWidth (0.8);
			context->setLineStyle (kLineSolid);
			context->drawGraphicsPath (cap, CDrawContext::kPathStroked);
		}
	}

	// ---- the white line -------------------------------------------------------
	strokeLine (context, pointAt (centre, body * 0.15, a), pointAt (centre, body - 0.5, a), Palette::kInk,
	            (mDiameter >= 44) ? 2.2 : 1.8);

	// ---- the reading and the label --------------------------------------------
	const CCoord textTop = r.top + mDiameter + Layout::kKnobGap;
	if (mFormatter)
		drawLettering (context, mFormatter (getValueNormalized ()), Layout::kTextValue, Palette::kInk, centre.x,
		               textTop + Layout::kKnobValueH / 2., Align::Centre);
	if (!mLabel.empty ())
		drawLettering (context, mLabel, Layout::kTextLabel, Palette::kInk, centre.x,
		               textTop + Layout::kKnobValueH + Layout::kKnobLabelH / 2., Align::Centre);

	setDirty (false);
}

void Knob::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft ())
		return;

	// DOUBLE-CLICK: the default. The first click of the pair began and
	// ended an ordinary (empty) drag, so this is one edit of its own.
	if (event.clickCount >= 2)
	{
		beginEdit ();
		setValue (getDefaultValue ());
		valueChanged ();
		endEdit ();
		invalid ();
		event.consumed = true;
		return;
	}

	mDragging = true;
	mLastY = event.mousePosition.y;
	beginEdit ();
	invalid ();
	event.consumed = true;
}

void Knob::onMouseMoveEvent (MouseMoveEvent& event)
{
	if (!mDragging)
		return;

	const CCoord dy = mLastY - event.mousePosition.y;   // up is more
	if (std::fabs (dy) < 0.5)
		return;
	mLastY = event.mousePosition.y;

	const float scale = event.modifiers.has (ModifierKey::Shift) ? 0.1f : 1.f;
	setValueNormalized (std::clamp (getValueNormalized () + static_cast<float> (dy) / kDragPixels * scale, 0.f, 1.f));
	valueChanged ();
	invalid ();
	event.consumed = true;
}

void Knob::onMouseUpEvent (MouseUpEvent& event)
{
	if (!mDragging)
		return;
	mDragging = false;
	endEdit ();
	invalid ();
	event.consumed = true;
}

void Knob::onMouseCancelEvent (MouseCancelEvent& event)
{
	if (mDragging)
	{
		mDragging = false;
		endEdit ();
		invalid ();
	}
	event.consumed = true;
}

void Knob::onMouseWheelEvent (MouseWheelEvent& event)
{
	const float step = event.modifiers.has (ModifierKey::Shift) ? 0.001f : 0.01f;
	beginEdit ();
	setValueNormalized (std::clamp (getValueNormalized () + static_cast<float> (event.deltaY) * step, 0.f, 1.f));
	valueChanged ();
	endEdit ();
	invalid ();
	event.consumed = true;
}

//------------------------------------------------------------------------
// RotarySwitch
//------------------------------------------------------------------------
RotarySwitch::RotarySwitch (const CRect& size, IControlListener* listener, int32_t tag,
                            const Layout::Control& layout)
: CControl (size, listener, tag)
, mLayout (layout)
, mGeometry (Layout::selectorOf (layout, 2))
{
}

void RotarySwitch::setNames (const std::vector<std::string>& names)
{
	mNames = names;
	mGeometry = Layout::selectorOf (mLayout, count ());
	invalid ();
}

void RotarySwitch::setLocked (int index)
{
	if (index != mLocked)
	{
		mLocked = index;
		invalid ();
	}
}

int RotarySwitch::currentIndex () const
{
	if (count () < 2)
		return 0;
	const int i = static_cast<int> (std::floor (getValueNormalized () * (count () - 1) + 0.5f));
	return std::clamp (i, 0, count () - 1);
}

void RotarySwitch::select (int index)
{
	if (count () < 2 || mLocked >= 0)
		return;
	index = std::clamp (index, 0, count () - 1);
	beginEdit ();
	setValueNormalized (static_cast<float> (index) / static_cast<float> (count () - 1));
	valueChanged ();
	endEdit ();
	invalid ();
}

void RotarySwitch::draw (CDrawContext* context)
{
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	const auto& g = mGeometry;
	const CPoint centre (g.cx, g.cy);
	const int n = std::min (count (), g.positions);
	const int current = (mLocked >= 0 && mLocked < n) ? mLocked : currentIndex ();

	// ---- the printed positions: a mark and a name for each -----------------
	for (int i = 0; i < n; ++i)
	{
		const bool set = (i == current);
		const double t = g.angle[i];
		strokeLine (context, pointFromTop (centre, g.radius + 2.5, t), pointFromTop (centre, g.radius + 8.0, t),
		            set ? Palette::kRed : Palette::kTick, set ? 2.0 : 1.6);
		const std::string name = Layout::upperCase (mNames[static_cast<size_t> (i)]);
		const Layout::Box b = Layout::selectorLabelBox (g, i, name);
		drawLettering (context, name, Layout::kTextLabel, set ? Palette::kInk : Palette::kInkDim, b.left,
		               (b.top + b.bottom) / 2.);
	}

	// ---- the chicken head: a round hub and a beak that points --------------
	const double a = (n > 0) ? g.angle[current] : 0.0;
	fillCircle (context, centre, g.radius * 0.8, CColor (11, 11, 11, 255));
	strokeCircle (context, centre, g.radius * 0.8, CColor (68, 68, 68, 255), 0.8);

	auto beak = owned (context->createGraphicsPath ());
	if (beak)
	{
		const CPoint tip = pointFromTop (centre, g.radius + 3.0, a);
		const CPoint left = pointFromTop (centre, 8.0, a - 90.0);
		const CPoint right = pointFromTop (centre, 8.0, a + 90.0);
		const CPoint tail = pointFromTop (centre, g.radius, a + 180.0);
		beak->beginSubpath (tip);
		beak->addLine (right);
		const CPoint c1 (right.x + 2.0 / 3.0 * (tail.x - right.x), right.y + 2.0 / 3.0 * (tail.y - right.y));
		const CPoint c2 (left.x + 2.0 / 3.0 * (tail.x - left.x), left.y + 2.0 / 3.0 * (tail.y - left.y));
		beak->addBezierCurve (c1, c2, left);
		beak->closeSubpath ();
		fillCap (context, beak, centre, g.radius);
		context->setFrameColor (CColor (0, 0, 0, 255));
		context->setLineWidth (0.8);
		context->setLineStyle (roundLine ());
		context->drawGraphicsPath (beak, CDrawContext::kPathStroked);
		strokeLine (context, pointFromTop (centre, 2.0, a), tip, mLocked >= 0 ? Palette::kInkDim : Palette::kInk, 1.8);
	}

	// ---- its name, under it ---------------------------------------------------
	if (!mLabel.empty ())
		drawLettering (context, mLabel, Layout::kTextLabel, Palette::kInk, g.cx, g.nameCentreY, Align::Centre);

	setDirty (false);
}

void RotarySwitch::onMouseDownEvent (MouseDownEvent& event)
{
	if (!event.buttonState.isLeft () || count () < 2)
		return;
	event.consumed = true;
	if (mLocked >= 0)
		return;
	const int picked = Layout::selectorPositionAt (mGeometry, event.mousePosition.x, event.mousePosition.y);
	select (picked >= 0 ? picked : (currentIndex () + 1) % count ());
}

void RotarySwitch::onMouseUpEvent (MouseUpEvent& event)
{
	event.consumed = true;
}

void RotarySwitch::onMouseWheelEvent (MouseWheelEvent& event)
{
	if (event.deltaY > 0.)
		select (currentIndex () + 1);
	else if (event.deltaY < 0.)
		select (currentIndex () - 1);
	event.consumed = true;
}

//------------------------------------------------------------------------
// Turntable
//------------------------------------------------------------------------
Turntable::Turntable (const CRect& size)
: CView (size)
{
	setMouseEnabled (false);
}

void Turntable::setDisc (int era, int disc, int speed)
{
	if (era != mEra || disc != mDisc || speed != mSpeed)
	{
		mEra = era;
		mDisc = disc;
		mSpeed = speed;
		invalid ();
	}
}

void Turntable::setAngle (double discAngle)
{
	if (std::fabs (discAngle - mAngle) > 1e-5)
	{
		mAngle = discAngle;
		invalid ();
	}
}

void Turntable::setAmounts (double scratchesPct, double dustPct, double warpPct)
{
	if (scratchesPct != mScratches || dustPct != mDust || warpPct != mWarp)
	{
		mScratches = scratchesPct;
		mDust = dustPct;
		mWarp = warpPct;
		invalid ();
	}
}

void Turntable::setSkipping (bool skipping)
{
	if (skipping != mSkipping)
	{
		mSkipping = skipping;
		invalid ();
	}
}

void Turntable::draw (CDrawContext* context)
{
	using namespace Layout;
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	// The view sits on Layout::kScreen; the disc's numbers are panel
	// coordinates, so draw relative to the view's own origin.
	const double ox = r.left - kScreen.left, oy = r.top - kScreen.top;
	const CPoint disc (kDiscX + ox, kDiscY + oy);
	const int playing = effectiveDisc (mEra, mDisc);

	// ---- the glass and its dot grid ------------------------------------------
	context->setFillColor (Palette::kScreen);
	context->drawRect (r, kDrawFilled);
	context->setFillColor (Palette::kGridDot);
	for (double x = r.left + 8; x < r.right; x += 16)
		for (double y = r.top + 8; y < r.bottom; y += 16)
			context->drawEllipse (CRect (x - 0.7, y - 0.7, x + 0.7, y + 0.7), kDrawFilled);

	// ---- the platter, the record, its grooves ---------------------------------
	strokeCircle (context, disc, kPlatterR, withAlpha (Palette::kOled, 90), 1.0);
	fillCircle (context, disc, kRecordR, CColor (18, 12, 2, 255));
	strokeCircle (context, disc, kRecordR, Palette::kOled, 1.6);
	{
		int i = 0;
		for (double rr = kGrooveInner; rr <= kGrooveOuter; rr += 3.2, ++i)
			strokeCircle (context, disc, rr, withAlpha (Palette::kOled, static_cast<uint8_t> (26 + 20 * (i % 3))), 0.6);
	}

	// ---- the label, turning: two marks show it -------------------------------
	fillCircle (context, disc, kLabelR, labelColour (mEra));
	if (mEra == kEra1940)
		strokeCircle (context, disc, kLabelR - 5.0,
		              CColor (kLabelGoldRing.r, kLabelGoldRing.g, kLabelGoldRing.b, 255), 1.0);
	strokeCircle (context, disc, kLabelR, Palette::kOled, 1.2);
	for (double off : { 0.0, 180.0 })
	{
		const double t = screenAngle (0.0, mAngle) + off;
		strokeLine (context, pointFromTop (disc, 18.0, t), pointFromTop (disc, 34.0, t),
		            CColor (244, 241, 232, 180), 1.6);
	}
	fillCircle (context, disc, 3.0, CColor (0, 0, 0, 255));
	strokeCircle (context, disc, 3.0, Palette::kOled, 1.0);

	// ---- the dust ---------------------------------------------------------------
	for (int i = 0; i < specksShown (mDust); ++i)
	{
		const Speck s = speckOf (i);
		fillCircle (context, pointFromTop (disc, s.radius, screenAngle (s.phase, mAngle)), s.size,
		            CColor (255, 255, 255, 140));
	}

	// ---- the scratches: the very ones the stylus hits -------------------------
	for (int i = 0; i < activeScratches (mScratches); ++i)
	{
		const ScratchSlot s = scratchSlot (i);
		const double t = screenAngle (s.phase, mAngle);
		strokeLine (context, pointFromTop (disc, kGrooveOuter - 50.0 * s.depth, t),
		            pointFromTop (disc, kGrooveOuter + 1.0, t),
		            CColor (255, 255, 255, static_cast<uint8_t> (255 * (0.45 + 0.5 * s.depth))), 0.8 + 0.8 * s.depth);
	}

	// ---- the arm: its head grows as the warp lifts it --------------------------
	{
		const CPoint pivot (kArmPivotX + ox, kArmPivotY + oy);
		const CPoint stylus = pointFromTop (disc, kStylusR, kStylusAngle);
		const double lift = mWarp / 100.0 * warpBump (mAngle);
		strokeCircle (context, pivot, 9.0, Palette::kScreenText, 1.4);
		strokeLine (context, pivot, stylus, Palette::kArm, 2.4);

		const double dx = stylus.x - pivot.x, dy = stylus.y - pivot.y;
		const double len = std::sqrt (dx * dx + dy * dy);
		const double ux = dx / len, uy = dy / len;          // along the arm
		const double vx = -uy, vy = ux;                     // across it
		const double hw = 7.0 * (1.0 + 0.35 * lift), hh = 4.0 * (1.0 + 0.35 * lift);
		auto head = owned (context->createGraphicsPath ());
		if (head)
		{
			head->beginSubpath (CPoint (stylus.x + vx * hw + ux * hh, stylus.y + vy * hw + uy * hh));
			head->addLine (CPoint (stylus.x - vx * hw + ux * hh, stylus.y - vy * hw + uy * hh));
			head->addLine (CPoint (stylus.x - vx * hw - ux * hh, stylus.y - vy * hw - uy * hh));
			head->addLine (CPoint (stylus.x + vx * hw - ux * hh, stylus.y + vy * hw - uy * hh));
			head->closeSubpath ();
			context->setFillColor (Palette::kArm);
			context->drawGraphicsPath (head, CDrawContext::kPathFilled);
		}
	}

	// ---- the captions -------------------------------------------------------------
	drawLettering (context, rpmText (mEra, mDisc, mSpeed), kTextScreen, Palette::kScreenText, r.left + 10, r.top + 14);
	if (mSkipping)
		drawLettering (context, "STUCK", kTextScreen, Palette::kRed, r.right - 10, r.top + 14, Align::Right);
	drawLettering (context, isStereo (mEra) ? "STEREO" : "MONO", kTextScreen, Palette::kScreenText, r.left + 10,
	               r.bottom - 12);
	drawLettering (context, discName (playing), kTextScreen, Palette::kOled, r.right - 10, r.bottom - 12,
	               Align::Right);

	setDirty (false);
}

//------------------------------------------------------------------------
// Lamp
//------------------------------------------------------------------------
Lamp::Lamp (const CRect& size, const std::string& name)
: CView (size)
, mName (Layout::upperCase (name))
{
	setMouseEnabled (false);
}

void Lamp::setOn (bool on)
{
	if (on != mOn)
	{
		mOn = on;
		invalid ();
	}
}

void Lamp::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	const double centreY = (r.top + r.bottom) / 2.;
	const CPoint dot (r.left + 8., centreY);

	// A lamp in a black bezel: dark red when off, lit red with a glow.
	fillCircle (context, dot, 5.5, CColor (0, 0, 0, 255));
	strokeCircle (context, dot, 5.5, Palette::kSectionEdge, 1.);
	if (mOn)
	{
		fillCircle (context, dot, 8., withAlpha (Palette::kLampOn, 70));
		fillCircle (context, dot, 4., Palette::kLampOn);
	}
	else
		fillCircle (context, dot, 4., Palette::kLampOff);
	fillCircle (context, CPoint (dot.x - 1.2, dot.y - 1.2), 1.3, CColor (255, 255, 255, 64));

	drawLettering (context, mName, Layout::kTextBlock, Palette::kInk, r.left + 20., centreY);
	setDirty (false);
}

//------------------------------------------------------------------------
// TextLine
//------------------------------------------------------------------------
TextLine::TextLine (const CRect& size, const Lettering::Style& style, const CColor& colour, Align align)
: CView (size)
, mStyle (style)
, mColour (colour)
, mAlign (align)
{
	setMouseEnabled (false);
}

void TextLine::setText (const std::string& text)
{
	if (text != mText)
	{
		mText = text;
		invalid ();
	}
}

void TextLine::draw (CDrawContext* context)
{
	const CRect r = getViewSize ();
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);
	const double x = (mAlign == Align::Centre) ? (r.left + r.right) / 2. : (mAlign == Align::Right ? r.right : r.left);
	drawLettering (context, mText, mStyle, mColour, x, (r.top + r.bottom) / 2., mAlign);
	setDirty (false);
}

//------------------------------------------------------------------------
// LogoButton
//------------------------------------------------------------------------
LogoButton::LogoButton (const CRect& size, std::function<void ()> onClick)
: CView (size)
, mOnClick (std::move (onClick))
{
	setMouseEnabled (true);
	setTransparency (true);
}

void LogoButton::draw (CDrawContext*)
{
	setDirty (false);                         // the Backdrop paints the wordmark
}

void LogoButton::onMouseEnterEvent (MouseEnterEvent& event)
{
	if (auto* frame = getFrame ())
		frame->setCursor (kCursorHand);
	event.consumed = true;
}

void LogoButton::onMouseExitEvent (MouseExitEvent& event)
{
	if (auto* frame = getFrame ())
		frame->setCursor (kCursorDefault);
	event.consumed = true;
}

void LogoButton::onMouseDownEvent (MouseDownEvent& event)
{
	if (event.buttonState.isLeft ())
	{
		if (auto* frame = getFrame ())
			frame->setCursor (kCursorDefault);   // the box covers us now
		if (mOnClick)
			mOnClick ();
	}
	event.consumed = true;
}

void LogoButton::onMouseUpEvent (MouseUpEvent& event)
{
	event.consumed = true;
}

//------------------------------------------------------------------------
// AboutBox
//------------------------------------------------------------------------
AboutBox::AboutBox (const CRect& size, std::function<void ()> onClose)
: CView (size)
, mOnClose (std::move (onClose))
{
	setMouseEnabled (true);
	setTransparency (true);
}

void AboutBox::setRows (std::vector<About::Row> rows)
{
	mRows = std::move (rows);
	invalid ();
}

void AboutBox::draw (CDrawContext* context)
{
	using namespace Layout;
	context->setDrawMode (kAntiAliasing | kNonIntegralMode);

	// the panel, dimmed
	context->setFillColor (CColor (0, 0, 0, 178));
	context->drawRect (getViewSize (), kDrawFilled);

	// the box: a section, with a header band like the panel's own
	const CRect box (kAboutBox.left, kAboutBox.top, kAboutBox.right, kAboutBox.bottom);
	roundRect (context, box, kSectionRadius, &Palette::kSection, &Palette::kSectionEdge, 1.2);
	{
		CRect band (box.left + 1.2, box.top + 1.2, box.right - 1.2, box.top + 62.0);
		context->setFillColor (Palette::kHeader);
		context->drawRect (band, kDrawFilled);
		context->setFillColor (Palette::kHeaderRule);
		context->drawRect (CRect (band.left, band.bottom, band.right, band.bottom + 2.0), kDrawFilled);
	}

	// the wordmark, centred, drawn exactly as the header draws it
	{
		const double w = Lettering::width ("WORN", kTextWordmark) + kWordmarkGap + Lettering::width ("VINYL", kTextWordmark);
		const double left = (box.left + box.right) / 2.0 - w / 2.0;
		const double second = left + Lettering::width ("WORN", kTextWordmark) + kWordmarkGap;
		for (int pass = 0; pass < 2; ++pass)
		{
			const CColor ink = pass ? Palette::kHeader : Palette::kInk;
			const double stroke = pass ? 0.32 : -1.0;
			drawLettering (context, "WORN", kTextWordmark, ink, left, kAboutWordmarkY, Align::Left, stroke);
			drawLettering (context, "VINYL", kTextWordmark, ink, second, kAboutWordmarkY, Align::Left, stroke);
		}
	}
	drawLettering (context, kAboutTagline, kTextAboutValue, Palette::kInkSoft, (box.left + box.right) / 2.0,
	               kAboutTaglineY, Align::Centre);

	// the rows
	for (size_t i = 0; i < mRows.size (); ++i)
	{
		const double y = About::rowCentreY (mRows, i);
		drawLettering (context, mRows[i].label, kTextAboutLabel, Palette::kInkDim, kAboutLabelX, y);
		for (size_t l = 0; l < mRows[i].lines.size (); ++l)
			drawLettering (context, mRows[i].lines[l], kTextAboutValue, Palette::kInk, kAboutValueX,
			               y + kAboutLineGap * static_cast<double> (l));
	}

	// the way out
	context->setFillColor (Palette::kRule);
	context->drawRect (CRect (box.left + 24.0, kAboutFooterY - 16.0, box.right - 24.0, kAboutFooterY - 15.0),
	                   kDrawFilled);
	drawLettering (context, kAboutFooter, kTextNote, Palette::kInkDim, (box.left + box.right) / 2.0,
	               kAboutFooterY, Align::Centre);
	setDirty (false);
}

void AboutBox::onMouseDownEvent (MouseDownEvent& event)
{
	event.consumed = true;
	if (mOnClose)
		mOnClose ();
}

void AboutBox::onMouseUpEvent (MouseUpEvent& event)     { event.consumed = true; }
void AboutBox::onMouseMoveEvent (MouseMoveEvent& event) { event.consumed = true; }
void AboutBox::onMouseWheelEvent (MouseWheelEvent& event) { event.consumed = true; }

//------------------------------------------------------------------------
} // namespace Panel
} // namespace WornVinyl
