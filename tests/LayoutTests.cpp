//------------------------------------------------------------------------
// WornVinyl - layout tests. SDK-free: the panel's numbers and lettering,
// measured with the same Lettering::width () the views draw with.
//
//   c++ -std=c++17 -O2 -Isource tests/LayoutTests.cpp source/WornVinylDsp.cpp -o /tmp/layouttests && /tmp/layouttests
//------------------------------------------------------------------------

#include "WornVinylLayout.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace WornVinyl;
using namespace WornVinyl::Layout;

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

namespace {
bool inside (const Box& in, const Rect& r, double pad = 0.0)
{
	return in.left >= r.left - pad && in.right <= r.right + pad && in.top >= r.top - pad && in.bottom <= r.bottom + pad;
}
bool inside (const Rect& in, const Rect& r)
{
	return in.left >= r.left && in.right <= r.right && in.top >= r.top && in.bottom <= r.bottom;
}
bool overlap (const Box& a, const Box& b)
{
	return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}
Box box (const Rect& r) { return { double (r.left), double (r.top), double (r.right), double (r.bottom) }; }

int positionsOf (int tag)
{
	return tag == kTagEra ? kEraCount : tag == kTagSpeed ? kSpeedCount : tag == kTagSpeaker ? kSpeakerCount
	     : 2;   // Disc, Jumps, Player: two
}
std::string positionName (int tag, int i)
{
	if (tag == kTagEra)
	{
		std::string s = eraName (i);
		s.pop_back ();
		return s;
	}
	if (tag == kTagDisc)
		return upperCase (discName (i));
	if (tag == kTagSpeed)
		return i == kSpeedAuto ? "AUTO" : speedName (i);
	if (tag == kTagPlayer)
		return upperCase (playerName (i));
	if (tag == kTagSpeaker)
		return speakerDialName (i);
	return i ? "ON" : "OFF";
}

/** Every character in `s` has a glyph (or is a space). */
bool allDrawable (const std::string& s)
{
	for (uint32_t c : Lettering::codePoints (s))
		if (c != ' ' && Lettering::glyphFor (c) == nullptr)
			return false;
	return true;
}
} // namespace

//------------------------------------------------------------------------
int main ()
{
	const Rect panel { 0, 0, kEditorWidth, kEditorHeight };

	std::printf ("sections\n");
	for (const auto& s : kSections)
	{
		CHECK (inside (s.r, panel) && s.r.top >= kHeaderHeight + 8, "%s on the panel, under the header", s.title);
		const Box t = titleBox (s);
		const double end = subtitleLeft (s) + Lettering::width (s.subtitle, kTextSubtitle);
		CHECK (end <= s.r.right - 10, "%s: title and subtitle fit (%.0f of %d)", s.title, end, s.r.right);
		CHECK (t.bottom < s.r.top + 40, "%s: title row", s.title);
	}
	for (int i = 0; i < kNumSections; ++i)
		for (int j = i + 1; j < kNumSections; ++j)
			CHECK (! overlap (box (kSections[i].r), box (kSections[j].r)), "sections %d and %d apart", i, j);
	{
		const double wm = kWordmarkLeft + Lettering::width ("WORN", kTextWordmark) + kWordmarkGap +
		                  Lettering::width ("VINYL", kTextWordmark);
		std::printf ("  wordmark ends at %.1f, divider at %d\n", wm, kHeaderDivider);
		CHECK (wm < kHeaderDivider - 8, "the wordmark clears the divider");
	}

	std::printf ("controls\n");
	CHECK (kNumControls == 13, "every setting has a control");
	for (int tag : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13, 14 })
		CHECK (controlFor (tag) != nullptr, "tag %d is on the panel", tag);
	CHECK (controlFor (10) == nullptr && controlFor (11) == nullptr, "the outputs have no control");
	for (int i = 0; i < kNumControls; ++i)
	{
		const Control& c = kControls[i];
		const Rect& sec = kSections[c.section].r;
		CHECK (inside (c.r, sec), "%s inside %s", c.label, kSections[c.section].title);
		CHECK (c.r.top >= titleBox (kSections[c.section]).bottom, "%s under its section's title", c.label);
		for (int j = i + 1; j < kNumControls; ++j)
			CHECK (! overlap (box (c.r), box (kControls[j].r)), "%s and %s apart", c.label, kControls[j].label);

		if (c.kind == Kind::Knob)
		{
			CHECK (Lettering::width (c.label, kTextLabel) <= c.r.width () - 4, "%s's label fits", c.label);
			for (const std::string& v : { percentText (100, true), percentText (0, true), percentText (0, false),
			                              decibelText (-12), decibelText (12), decibelText (0) })
				CHECK (Lettering::width (v, kTextValue) <= c.r.width () - 4, "%s fits under %s", v.c_str (), c.label);
			CHECK (c.r.height () == c.diameter + kKnobTextBand, "%s's height", c.label);
			continue;
		}

		const int n = positionsOf (c.tag);
		const Selector s = selectorOf (c, n);
		CHECK (s.positions == n, "%s has %d positions", c.label, n);
		std::vector<Box> boxes;
		for (int p = 0; p < n; ++p)
		{
			const std::string name = positionName (c.tag, p);
			const Box b = selectorLabelBox (s, p, name);
			CHECK (inside (b, c.r), "%s: '%s' inside the selector [%.0f %.0f %.0f %.0f]", c.label, name.c_str (),
			       b.left, b.top, b.right, b.bottom);
			// clear of the knob's circle and its tick
			const double nx = std::fmax (b.left, std::fmin (s.cx, b.right));
			const double ny = std::fmax (b.top, std::fmin (s.cy, b.bottom));
			const double d = std::hypot (nx - s.cx, ny - s.cy);
			CHECK (d >= s.radius + 8.0, "%s: '%s' clear of the knob (%.1f)", c.label, name.c_str (), d);
			// a click on the name picks it
			CHECK (selectorPositionAt (s, (b.left + b.right) / 2, (b.top + b.bottom) / 2) == p,
			       "%s: a click on '%s' picks it", c.label, name.c_str ());
			for (const Box& o : boxes)
				CHECK (! overlap (b, o), "%s: '%s' clear of the others", c.label, name.c_str ());
			boxes.push_back (b);
		}
		CHECK (selectorPositionAt (s, s.cx, s.cy) == -1, "%s: a click on the knob steps", c.label);
		CHECK (s.nameCentreY - kTextLabel.height () / 2 > s.cy + s.radius + 2, "%s: name under the knob", c.label);
	}

	std::printf ("turntable\n");
	const Rect& tt = kSections[1].r;
	CHECK (inside (bezelOf (kScreen), tt), "the screen inside TURNTABLE");
	CHECK (bezelOf (kScreen).top > titleBox (kSections[1]).bottom, "the screen under the title");
	CHECK (kDiscX - kPlatterR - 1 >= kScreen.left && kDiscX + kPlatterR + 1 <= kScreen.right &&
	       kDiscY - kPlatterR - 1 >= kScreen.top && kDiscY + kPlatterR + 1 <= kScreen.bottom, "the platter on the glass");
	CHECK (kArmPivotX + 10 <= kScreen.right && kArmPivotY - 10 >= kScreen.top, "the arm's pivot on the glass");
	CHECK (kArmPivotY - 10 > kScreen.top + 20, "the pivot clear of the caption row");
	CHECK (std::fabs (screenAngle (0.3, 0.3) - kStylusAngle) < 1e-9, "a point is under the stylus when the disc reaches it");
	CHECK (screenAngle (0.3, 0.35) > kStylusAngle, "the disc turns clockwise: past the stylus, further round");
	for (int i = 0; i < kMaxSpecks; ++i)
	{
		const Speck s = speckOf (i);
		CHECK (s.radius >= kGrooveInner && s.radius <= kGrooveOuter && s.phase >= 0 && s.phase < 1, "speck %d on the grooves", i);
	}
	CHECK (specksShown (0) == 0 && specksShown (100) == kMaxSpecks, "specks follow Dust");
	{
		// SKIP in TURNTABLE's title row, right of the title, above the glass.
		const Box tb = titleBox (kSections[1]);
		CHECK (inside (kLamp, tt) && kLamp.left > tb.right + 8 && kLamp.top >= tb.top - 2 && kLamp.bottom <= tb.bottom + 2,
		       "the lamp in the title row");
		CHECK (kLamp.bottom < bezelOf (kScreen).top, "the lamp above the screen");
	}
	for (int tag : { kTagSpeaker, kTagPlayer })
	{
		const Control* c = controlFor (tag);
		CHECK (c->r.top > bezelOf (kScreen).bottom && c->r.bottom <= kTurntableLine.top,
		       "%s between the screen and the line", c->label);
		CHECK (! overlap (box (kLamp), box (c->r)), "the lamp clear of %s", c->label);
	}
	CHECK (inside (kTurntableLine, tt) && kTurntableLine.top >= controlFor (kTagSpeaker)->r.bottom, "the line under SPEAKER");
	{
		// NEGATIVE CONTROL: six names at the usual radius would touch the knob.
		Control c = *controlFor (kTagSpeaker);
		Selector s = selectorOf (c, kSpeakerCount);
		s.labelRadius = s.radius + 12.0;
		bool clash = false;
		for (int p = 0; p < kSpeakerCount; ++p)
		{
			const Box b = selectorLabelBox (s, p, speakerDialName (p));
			const double nx = std::fmax (b.left, std::fmin (s.cx, b.right));
			const double ny = std::fmax (b.top, std::fmin (s.cy, b.bottom));
			clash = clash || std::hypot (nx - s.cx, ny - s.cy) < s.radius + 8.0;
		}
		CHECK (clash, "negative control: six names at radius + 12 crowd the knob");
	}
	CHECK (14 + 20 + Lettering::width ("SKIP", kTextBlock) <= kLamp.width () + 14, "SKIP fits its lamp");
	{
		// The screen's captions, corner to corner.
		const double w = Lettering::width ("STEREO", kTextScreen) + Lettering::width ("SHELLAC", kTextScreen) + 30;
		CHECK (w < kScreen.width () - 2 * (kPlatterR - 100), "bottom captions fit");
	}

	std::printf ("lines that change, at their longest\n");
	double headerMax = 0, ttMax = 0, speedMax = 0, noteMax = 0;
	std::string headerLongest;
	for (int e = 0; e < kEraCount; ++e)
		for (int d = 0; d < kDiscCount; ++d)
		for (int sp = 0; sp < kSpeedCount; ++sp)
		{
			for (int pl = 0; pl < kPlayerCount; ++pl)
				for (int spk = 0; spk < kSpeakerCount; ++spk)
				{
					const std::string h = headerLineText (e, d, sp, pl, spk);
					CHECK (allDrawable (h), "every character drawable: %s", h.c_str ());
					if (Lettering::width (h, kTextHeader) > headerMax)
						headerMax = Lettering::width (h, kTextHeader), headerLongest = h;
				}
			for (double sc : { 0.0, 0.5, 100.0 })
				for (double du : { 0.0, 100.0 })
				{
					const std::string t = turntableLineText (e, d, sc, du);
					CHECK (allDrawable (t), "drawable: %s", t.c_str ());
					ttMax = std::fmax (ttMax, Lettering::width (t, kTextNote));
				}
			speedMax = std::fmax (speedMax, Lettering::width (speedLineText (e, d, sp), kTextNote));
			noteMax = std::fmax (noteMax, Lettering::width (discNoteText (e), kTextNote));
		}
	std::printf ("  header %.0f of %d (%s)\n  turntable %.0f of %d, speed %.0f of %d, disc note %.0f of %d\n",
	             headerMax, kHeaderLine.width (), headerLongest.c_str (), ttMax, kTurntableLine.width (), speedMax,
	             kSpeedLine.width (), noteMax, kDiscNote.width ());
	CHECK (headerMax <= kHeaderLine.width (), "the header line fits");
	CHECK (ttMax <= kTurntableLine.width (), "the turntable line fits");
	CHECK (speedMax <= kSpeedLine.width (), "the speed line fits");
	CHECK (noteMax <= kDiscNote.width (), "the disc note fits");
	CHECK (Lettering::width (kSurfaceNoteText, kTextNote) <= kSurfaceNote.width (), "the surface note fits");
	CHECK (headerLineText (kEra1960, kDiscVinyl, kSpeedAuto, kPlayerPeriod, kSpeakerOff) ==
	       headerLineText (kEra1960, kDiscVinyl, kSpeedAuto, kPlayerPeriod), "Off adds nothing to the header");
	CHECK (headerLineText (kEra1960, kDiscVinyl, kSpeedAuto, kPlayerPeriod, kSpeaker6Inch).find ("6-INCH + TWEETER") !=
	       std::string::npos, "a speaker is named in the header");
	CHECK (rpmText (kEra1960, kDiscVinyl, kSpeedAuto) == "33 RPM" && rpmText (kEra1960, kDiscVinyl, kSpeed45) == "45 RPM" &&
	       rpmText (kEra1920, kDiscVinyl, kSpeedAuto) == "78 RPM" && rpmText (kEra1920, kDiscVinyl, kSpeed33) == "33 RPM",
	       "the rpm shown follows the switch, and Auto follows the disc");
	{
		// The DISC note sits under DISC, not under SPEED.
		const Control* d = controlFor (kTagDisc);
		CHECK (std::fabs ((kDiscNote.left + kDiscNote.right) / 2.0 - d->centreX) < 1.0, "the disc note centred under DISC");
	}
	CHECK (allDrawable (kSurfaceNoteText) && allDrawable ("OUTPUT SKIP STUCK MONO STEREO 78 RPM 33 RPM"), "fixed text drawable");
	CHECK (inside (kSurfaceNote, kSections[2].r) && kSurfaceNote.top > controlFor (kTagScratches)->r.bottom,
	       "the surface note under the knobs");
	CHECK (inside (kSpeedLine, kSections[3].r) && kSpeedLine.top > controlFor (kTagWow)->r.bottom,
	       "the speed line under the knobs");
	CHECK (kDiscNote.top >= controlFor (kTagDisc)->r.bottom && kDiscNote.bottom < kRecordRule.top, "the disc note between");
	CHECK (kOutputTitle.top > kRecordRule.bottom && kOutputTitle.bottom < controlFor (kTagMix)->r.top, "OUTPUT heading");

	// NEGATIVE CONTROL: the header in a style three times the size must NOT fit.
	CHECK (Lettering::width (headerLongest, Lettering::Style { kTextHeader.cap * 3, kTextHeader.weight }) >
	       kHeaderLine.width (), "negative control: a header three times the size overflows");

	std::printf ("\nLayout: %d checks, %d failed\n", gChecks, gFails);
	return gFails ? 1 : 0;
}
