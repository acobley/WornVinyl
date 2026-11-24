//------------------------------------------------------------------------
// WornVinyl - the panel's layout
//
// From Designs/WornVinyl-1-BlackPanel.png (Designs/panel.html draws the
// same numbers): R2Synth's black panel - matte black, white printing,
// boxed sections with their titles in their own boxes, black knurled knobs
// with printed scales, chicken-head selectors, a colour OLED - and every
// word in Andy's 70s sci-fi lettering.
//
//   header      WORN VINYL, and a line saying what is playing
//   RECORD      ERA (five positions), DISC (two - set by the era but in
//               the 1950s) and SPEED (Auto / 33 / 45 / 78) side by side,
//               then OUTPUT: Mix and Output
//   TURNTABLE   the SKIP lamp in its title row; the OLED: the disc
//               turning at its real speed, the scratches where the stylus
//               meets them, the dust, the arm lifting over a warp; under
//               it SPEAKER (Off + five, 24 Nov 2026) and PLAYER (Period /
//               Modern), and a line under them
//   SURFACE     Scratches, Dust, JUMPS
//   SPEED       Wow, Flutter, Warp
//
// THIS HEADER INCLUDES NO VSTGUI, so the editor and tests/LayoutTests.cpp
// read the same numbers: every label must fit, nothing may overlap, every
// control must sit in its section. The readouts are here too, for the
// same reason - the panel cannot print one thing and the tests check
// another.
//------------------------------------------------------------------------

#pragma once

#include "SciFiLettering.h"
#include "WornVinylDsp.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace WornVinyl {
namespace Layout {

//------------------------------------------------------------------------
struct Rect
{
	int left, top, right, bottom;
	constexpr int width () const { return right - left; }
	constexpr int height () const { return bottom - top; }
};

struct Box
{
	double left, top, right, bottom;
};

//------------------------------------------------------------------------
// The panel
//------------------------------------------------------------------------
constexpr int kEditorWidth  = 1000;
constexpr int kEditorHeight = 520;   // 480 until PLAYER (10 Nov 2026) needed room under the screen
constexpr int kHeaderHeight = 56;

constexpr int kSectionRadius = 2;

/** Under every knob: two pixels, the reading, the label. */
constexpr int kKnobGap      = 2;
constexpr int kKnobValueH   = 13;
constexpr int kKnobLabelH   = 12;
constexpr int kKnobTextBand = kKnobGap + kKnobValueH + kKnobLabelH;
constexpr int kKnobWidth    = 92;

/** A selector's rectangle, top to bottom: the printed names over the
    knob, the knob, its name at the foot. */
constexpr int kSelectorTopBand = 26;
constexpr int kSelectorNameH   = 12;

constexpr int kScreenBezel = 3;

//------------------------------------------------------------------------
// The lettering - R2Synth's sizes (cap heights 0.64 of the design's fonts)
//------------------------------------------------------------------------
constexpr Lettering::Style kTextWordmark { 24.0, 1.7, 1.2 };
constexpr Lettering::Style kTextTitle    { 8.3, Lettering::kBold };
constexpr Lettering::Style kTextSubtitle { 5.8, Lettering::kRegular };
constexpr Lettering::Style kTextLabel    { 6.1, Lettering::kBold };     // knob and selector names
constexpr Lettering::Style kTextValue    { 6.4, Lettering::kRegular };  // every reading
constexpr Lettering::Style kTextBlock    { 6.7, Lettering::kBold };     // "OUTPUT", "SKIP"
constexpr Lettering::Style kTextNote     { 5.4, Lettering::kRegular };  // the lines under things
constexpr Lettering::Style kTextHeader   { 6.7, Lettering::kRegular };
constexpr Lettering::Style kTextScreen   { 6.1, Lettering::kRegular };
constexpr Lettering::Style kTextTick     { 4.8, Lettering::kRegular };  // 0 / 10 by a knob's scale

constexpr double kWordmarkLeft   = 22.0;
constexpr double kWordmarkGap    = 12.0;     // between WORN and VINYL
constexpr int    kHeaderDivider  = 300;
constexpr Rect   kHeaderLine     { 314, 20, 984, 36 };

constexpr struct { int x, y; } kScrews[] = { { 8, 8 }, { 992, 8 }, { 8, 512 }, { 992, 512 } };

//------------------------------------------------------------------------
// Sections
//------------------------------------------------------------------------
struct Section
{
	Rect r;
	const char* title;
	const char* subtitle;
};

constexpr Section kSections[] = {
	{ { 16, 72, 296, 504 },  "RECORD",    "ERA, DISC + SPEED" },
	{ { 312, 72, 632, 504 }, "TURNTABLE", "" },
	{ { 648, 72, 984, 262 }, "SURFACE",   "WHAT THE STYLUS MEETS" },
	{ { 648, 278, 984, 504 }, "SPEED",    "THE PLATTER + THE DISC" },
};
constexpr int kNumSections = static_cast<int> (sizeof (kSections) / sizeof (kSections[0]));

/** A section's title box: left of the title row, wide enough for it. */
inline Box titleBox (const Section& s)
{
	const double left = s.r.left + 14.0, top = s.r.top + 10.0;
	return { left, top, left + 16.0 + Lettering::width (s.title, kTextTitle), top + 22.0 };
}

inline double subtitleLeft (const Section& s)
{
	return titleBox (s).right + 12.0;
}

//------------------------------------------------------------------------
// Controls
//------------------------------------------------------------------------
enum class Kind { Knob, Selector };

/** Ids match WornVinylParams.h's Param enum; restated as plain ints so
    this header stays SDK-free. tests/ParamsTests.cpp checks they agree. */
enum Tag
{
	kTagEra = 0, kTagDisc = 1, kTagScratches = 2, kTagDust = 3, kTagWow = 4,
	kTagFlutter = 5, kTagWarp = 6, kTagJumps = 7, kTagMix = 8, kTagOutput = 9,
	kTagSpeed = 12,     // appended after the two outputs (10, 11)
	kTagPlayer = 13,
	kTagSpeaker = 14
};

struct Control
{
	int tag;
	Kind kind;
	int section;        // index into kSections
	Rect r;             // the whole control, reading and names included
	int diameter;       // the knob; for a selector its chicken head
	int centreX;        // the knob's (or the chicken head's) centre
	const char* label;
};

constexpr Rect knobRect (int cx, int top, int diameter)
{
	return { cx - kKnobWidth / 2, top, cx + kKnobWidth / 2, top + diameter + kKnobTextBand };
}

constexpr Control kControls[] = {
	{ kTagEra,       Kind::Selector, 0, { 36, 106, 276, 216 }, 56, 156, "ERA" },
	{ kTagDisc,      Kind::Selector, 0, { 24, 220, 156, 304 }, 40, 96,  "DISC" },
	{ kTagSpeed,     Kind::Selector, 0, { 160, 220, 288, 304 }, 40, 226, "SPEED" },
	{ kTagMix,       Kind::Knob,     0, knobRect (96, 384, 52),  52, 96,  "MIX" },
	{ kTagOutput,    Kind::Knob,     0, knobRect (216, 384, 52), 52, 216, "OUTPUT" },
	{ kTagSpeaker,   Kind::Selector, 1, { 324, 400, 490, 480 }, 40, 410, "SPEAKER" },
	{ kTagPlayer,    Kind::Selector, 1, { 494, 400, 630, 480 }, 40, 560, "PLAYER" },
	{ kTagScratches, Kind::Knob,     2, knobRect (704, 112, 52), 52, 704, "SCRATCHES" },
	{ kTagDust,      Kind::Knob,     2, knobRect (806, 112, 52), 52, 806, "DUST" },
	{ kTagJumps,     Kind::Selector, 2, { 872, 112, 968, 205 },  40, 920, "JUMPS" },
	{ kTagWow,       Kind::Knob,     3, knobRect (718, 338, 52), 52, 718, "WOW" },
	{ kTagFlutter,   Kind::Knob,     3, knobRect (816, 338, 52), 52, 816, "FLUTTER" },
	{ kTagWarp,      Kind::Knob,     3, knobRect (914, 338, 52), 52, 914, "WARP" },
};
constexpr int kNumControls = static_cast<int> (sizeof (kControls) / sizeof (kControls[0]));

inline const Control* controlFor (int tag)
{
	for (const auto& c : kControls)
		if (c.tag == tag)
			return &c;
	return nullptr;
}

//------------------------------------------------------------------------
// The rotary selectors - two, three, four, five or six positions
//------------------------------------------------------------------------
constexpr int kMaxPositions = 6;

/** A selector's geometry: its knob, where its positions point (degrees
    from twelve o'clock, clockwise), the radius its names hang from. */
struct Selector
{
	double cx, cy, radius, labelRadius;
	int positions;
	double angle[kMaxPositions];
	double nameCentreY;
};

inline Selector selectorOf (const Control& c, int positions)
{
	Selector s {};
	s.radius = c.diameter / 2.0;
	s.cx = c.centreX;
	s.cy = c.r.top + kSelectorTopBand + s.radius;
	s.positions = positions < 2 ? 2 : (positions > kMaxPositions ? kMaxPositions : positions);
	// Six names round a small knob crowd it: they hang a little further
	// out, or the two near the top touch the knob's ticks.
	s.labelRadius = s.radius + (s.positions == 6 ? 15.0 : 12.0);
	static const double two[]   = { -45.0, 45.0 };
	static const double three[] = { -45.0, 0.0, 45.0 };
	static const double four[]  = { -90.0, -30.0, 30.0, 90.0 };
	static const double five[]  = { -90.0, -45.0, 0.0, 45.0, 90.0 };
	static const double six[]   = { -90.0, -54.0, -18.0, 18.0, 54.0, 90.0 };
	const double* a = s.positions == 2 ? two : s.positions == 3 ? three : s.positions == 4 ? four
	                : s.positions == 5 ? five : six;
	for (int i = 0; i < s.positions; ++i)
		s.angle[i] = a[i];
	s.nameCentreY = c.r.bottom - kSelectorNameH / 2.0;
	return s;
}

inline void selectorPoint (const Selector& s, double radius, double degrees, double& x, double& y)
{
	const double t = degrees * 3.14159265358979323846 / 180.0;
	x = s.cx + radius * std::sin (t);
	y = s.cy - radius * std::cos (t);
}

/** WHERE A POSITION'S NAME IS PRINTED: above the knob for the one
    straight up, level with the hub for the two sideways, and hung off
    the line either side for the diagonals. */
inline Box selectorLabelBox (const Selector& s, int index, const std::string& text)
{
	const double t = s.angle[index];
	double px = 0.0, py = 0.0;
	selectorPoint (s, s.labelRadius, t, px, py);
	const double w = Lettering::width (text, kTextLabel);
	const double h = kTextLabel.height ();
	double left = 0.0, centreY = 0.0;
	if (std::fabs (t) < 10.0)
	{
		left = px - w / 2.0;
		centreY = py - 8.0;
	}
	else if (std::fabs (t) > 80.0)
	{
		left = (t < 0.0) ? px - 2.0 - w : px + 2.0;
		centreY = py;
	}
	else
	{
		left = (t < 0.0) ? px - 2.0 - w : px + 2.0;
		centreY = py + 2.0;
	}
	return { left, centreY - h / 2.0, left + w, centreY + h / 2.0 };
}

/** THE POSITION A CLICK PICKS: on the knob itself, -1 (the knob steps
    on); anywhere else, the position whose angle is nearest the click's. */
inline int selectorPositionAt (const Selector& s, double x, double y)
{
	const double dx = x - s.cx, dy = y - s.cy;
	if (dx * dx + dy * dy <= s.radius * s.radius)
		return -1;
	const double t = std::atan2 (dx, -dy) * 180.0 / 3.14159265358979323846;
	int best = 0;
	for (int i = 1; i < s.positions; ++i)
		if (std::fabs (s.angle[i] - t) < std::fabs (s.angle[best] - t))
			best = i;
	return best;
}

//------------------------------------------------------------------------
// The turntable screen
//------------------------------------------------------------------------
constexpr Rect kScreen { 330, 114, 614, 394 };

constexpr Rect bezelOf (const Rect& r)
{
	return { r.left - kScreenBezel, r.top - kScreenBezel, r.right + kScreenBezel, r.bottom + kScreenBezel };
}

/** The disc on it - centre and radii in panel pixels - and the arm. */
constexpr double kDiscX = 472.0, kDiscY = 258.0;
constexpr double kPlatterR = 124.0, kRecordR = 118.0;
constexpr double kGrooveInner = 52.0, kGrooveOuter = 112.0, kLabelR = 40.0;
constexpr double kArmPivotX = 592.0, kArmPivotY = 148.0;
constexpr double kStylusR = 96.0, kStylusAngle = 62.0;   // degrees from twelve, clockwise

/** WHERE A POINT OF THE DISC IS ON THE SCREEN: the disc turns clockwise,
    and a point at `phase` (0..1 a turn, the DSP's) is under the stylus
    when the disc's angle reaches it. Degrees from twelve, clockwise. */
inline double screenAngle (double phase, double discAngle)
{
	return kStylusAngle + (discAngle - phase) * 360.0;
}

/** A dust speck on the disc, from a fixed table: phase, radius, size. */
struct Speck { double phase, radius, size; };
constexpr int kMaxSpecks = 40;

inline Speck speckOf (int i)
{
	uint32_t x = 2654435761u * static_cast<uint32_t> (i + 1);
	auto next = [&x] { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return (x & 0xFFFFFF) / 16777216.0; };
	const double phase = next ();
	const double radius = kGrooveInner + 2.0 + next () * (kGrooveOuter - kGrooveInner - 2.0);
	return { phase, radius, 0.6 + 0.9 * next () };
}

/** How many specks show at this Dust. */
inline int specksShown (double dustPct)
{
	return static_cast<int> (std::lround (std::fmax (0.0, std::fmin (100.0, dustPct)) / 100.0 * kMaxSpecks));
}

//------------------------------------------------------------------------
// The fixed captions, rules and the lines that change
//------------------------------------------------------------------------
constexpr Rect kRecordRule  { 30, 352, 282, 353 };
constexpr Rect kOutputTitle { 30, 362, 130, 374 };      // "OUTPUT", block style

constexpr Rect kDiscNote    { 24, 308, 168, 320 };      // centred under DISC: "SET BY THE ERA" / "1950S - YOUR CHOICE"
constexpr Rect kSurfaceNote { 664, 236, 976, 248 };     // fixed
constexpr Rect kSpeedLine   { 664, 476, 976, 488 };     // changes with the disc's speed
constexpr Rect kLamp        { 536, 83, 620, 103 };      // the SKIP lamp, in TURNTABLE's title row (SPEAKER took its place)
constexpr Rect kTurntableLine { 330, 482, 620, 494 };   // scratches and crackle

constexpr const char* kSurfaceNoteText = "THE NEEDLE CAN JUMP ABOVE 60 % SCRATCHES";

//------------------------------------------------------------------------
// The readouts - shared by the panel and the tests
//------------------------------------------------------------------------
inline std::string percentText (double pct, bool offAtZero)
{
	if (offAtZero && pct < 0.5)
		return "OFF";
	char b[16];
	std::snprintf (b, sizeof b, "%.0f %%", pct);
	return b;
}

inline std::string decibelText (double db)
{
	char b[16];
	std::snprintf (b, sizeof b, "%+.1f DB", std::fabs (db) < 0.05 ? 0.0 : db);
	return b;
}

inline std::string upperCase (std::string s)
{
	for (auto& c : s)
		if (c >= 'a' && c <= 'z')
			c = static_cast<char> (c - 'a' + 'A');
	return s;
}

/** "33 RPM", "45 RPM", "78 RPM" - the speed the disc turns at. */
inline std::string rpmText (int era, int disc, int speed)
{
	char b[16];
	std::snprintf (b, sizeof b, "%.0f RPM", std::floor (turntableRpm (era, disc, speed)));
	return b;
}

/** The header: "1960S / STEREO VINYL / 33 RPM / 28 HZ - 15 KHZ / PERIOD PLAYER",
    and " / 6-INCH + TWEETER" when a speaker is on. */
inline std::string headerLineText (int era, int disc, int speed, int player, int speaker = kSpeakerOff)
{
	const int d = effectiveDisc (era, disc);
	std::string s = std::string (eraName (era)) + "  /  " + (isStereo (era) ? "STEREO " : "MONO ") +
	                discName (d) + "  /  " + rpmText (era, disc, speed) + "  /  " + bandwidthText (era, d) +
	                "  /  " + playerName (player) + " PLAYER";
	if (speaker != kSpeakerOff)
		s += std::string ("  /  ") + speakerDescription (speaker);
	return upperCase (s);
}

inline std::string discNoteText (int era)
{
	return discLocked (era) ? "SET BY THE ERA" : "1950S - YOUR CHOICE";
}

/** Under the screen: "4 SCRATCHES  /  CRACKLE 22 A SECOND". */
inline std::string turntableLineText (int era, int disc, double scratchesPct, double dustPct)
{
	const int n = activeScratches (scratchesPct);
	const double rate = crackleRate (era, disc, dustPct);
	char b[96];
	if (n == 0 && rate < 0.5)
		std::snprintf (b, sizeof b, "A CLEAN DISC");
	else
		std::snprintf (b, sizeof b, "%d SCRATCH%s  /  CRACKLE %.0f A SECOND", n, n == 1 ? "" : "ES", rate);
	return b;
}

/** Under the speed knobs: the wow is once a turn, so it follows the speed. */
inline std::string speedLineText (int era, int disc, int speed)
{
	char b[96];
	std::snprintf (b, sizeof b, "WOW %.2f HZ  /  FLUTTER 7 - 11 HZ  /  ONE WARP A TURN",
	               turntableRpm (era, disc, speed) / 60.0);
	return b;
}

//------------------------------------------------------------------------
} // namespace Layout
} // namespace WornVinyl
