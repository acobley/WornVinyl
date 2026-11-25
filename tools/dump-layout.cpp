//------------------------------------------------------------------------
// tools/dump-layout.cpp - the panel as JSON, for tools/render-panel.py
//
// WHY A C++ TOOL AND NOT A PARSER (R2Synth's approach). Everything the
// picture needs - every position, every piece of text, the turntable's
// scratches and specks, every glyph of the lettering - is printed here by
// the SAME code the editor runs: WornVinylLayout.h for where things are and
// what they say, WornVinylDsp's shared rules for the disc, WornVinylParams
// for the defaults. The Python script only draws. So the picture cannot
// drift from the panel without this failing to compile first.
//
// The values are the parameter table's DEFAULTS and the disc at angle 0,
// so docs/panel.png is the panel as it opens on a fresh instance.
//
// "about" is the About box, its rows from AboutInfo.h for a typical
// install (render-panel.py --about draws it; docs/panel.png leaves it shut).
//
//   c++ -std=c++17 -I<sdk> -Isource tools/dump-layout.cpp
//       source/WornVinylParams.cpp source/WornVinylDsp.cpp -o /tmp/dump-layout
//------------------------------------------------------------------------

#include "AboutInfo.h"
#include "WornVinylLayout.h"
#include "WornVinylParams.h"
#include "version.h"

#include <cstdio>
#include <string>

using namespace WornVinyl;
using namespace WornVinyl::Layout;

namespace {

std::string quoted (const std::string& s)
{
	std::string out = "\"";
	for (char c : s)
	{
		if (c == '"' || c == '\\') out += '\\';
		out += c;
	}
	return out + "\"";
}

std::string num (double v)
{
	char t[32];
	std::snprintf (t, sizeof (t), "%.3f", v);
	return t;
}

std::string rect (const Rect& r)
{
	char t[64];
	std::snprintf (t, sizeof (t), "[%d,%d,%d,%d]", r.left, r.top, r.right, r.bottom);
	return t;
}

std::string box (const Box& b)
{
	return "[" + num (b.left) + "," + num (b.top) + "," + num (b.right) + "," + num (b.bottom) + "]";
}

std::string style (const Lettering::Style& s)
{
	return "{\"cap\": " + num (s.cap) + ", \"weight\": " + num (s.weight) + ", \"track\": " + num (s.track) + "}";
}

std::string commands (const char* path)
{
	std::string out = "[";
	bool first = true;
	for (const auto& c : Lettering::parse (path))
	{
		out += first ? "" : ",";
		first = false;
		out += "[\"" + std::string (1, c.op) + "\"," + num (c.x1) + "," + num (c.y1) + "," + num (c.x) + "," + num (c.y) + "]";
	}
	return out + "]";
}

std::string rgb (const Rgb& c)
{
	char t[32];
	std::snprintf (t, sizeof (t), "[%d,%d,%d]", c.r, c.g, c.b);
	return t;
}

double plainDefault (Steinberg::Vst::ParamID id) { return kParams[id].plainDefault; }
int choiceDefault (Steinberg::Vst::ParamID id)
{
	return static_cast<int> (kParams[id].toInternal (kParams[id].defaultNormalized ()));
}

} // namespace

int main ()
{
	const int era = choiceDefault (kEra), disc = choiceDefault (kDisc), speed = choiceDefault (kSpeed);
	const int player = choiceDefault (kPlayer), speaker = choiceDefault (kSpeaker);
	const double scratches = plainDefault (kScratches), dust = plainDefault (kDust), warp = plainDefault (kWarp);
	const double angle = 0.0;

	std::printf ("{\n");
	std::printf ("  \"width\": %d, \"height\": %d, \"headerHeight\": %d, \"sectionRadius\": %d,\n", kEditorWidth,
	             kEditorHeight, kHeaderHeight, kSectionRadius);
	std::printf ("  \"knobGap\": %d, \"knobValueH\": %d, \"knobLabelH\": %d, \"screenBezel\": %d,\n", kKnobGap,
	             kKnobValueH, kKnobLabelH, kScreenBezel);
	std::printf ("  \"headerDivider\": %d, \"wordmarkLeft\": %s, \"wordmarkGap\": %s,\n", kHeaderDivider,
	             num (kWordmarkLeft).c_str (), num (kWordmarkGap).c_str ());

	// ---- the lettering: the styles, and every glyph parsed -------------------
	std::printf ("  \"styles\": {\"wordmark\": %s, \"title\": %s, \"subtitle\": %s, \"label\": %s, \"value\": %s,\n"
	             "             \"block\": %s, \"note\": %s, \"header\": %s, \"screen\": %s, \"tick\": %s},\n",
	             style (kTextWordmark).c_str (), style (kTextTitle).c_str (), style (kTextSubtitle).c_str (),
	             style (kTextLabel).c_str (), style (kTextValue).c_str (), style (kTextBlock).c_str (),
	             style (kTextNote).c_str (), style (kTextHeader).c_str (), style (kTextScreen).c_str (),
	             style (kTextTick).c_str ());
	std::printf ("  \"space\": %s,\n  \"glyphs\": {\n", num (Lettering::kSpace).c_str ());
	for (uint32_t c = 1; c < 0x2600; ++c)
		if (const Lettering::Glyph* g = Lettering::glyphFor (c))
			std::printf ("    \"%u\": [%s, %s],\n", c, num (g->advance).c_str (), commands (g->path).c_str ());
	std::printf ("    \"missing\": [%s, %s]\n  },\n", num (Lettering::missingGlyph ().advance).c_str (),
	             commands (Lettering::missingGlyph ().path).c_str ());

	// ---- the backdrop ----------------------------------------------------------
	std::printf ("  \"screws\": [");
	for (size_t i = 0; i < sizeof (kScrews) / sizeof (kScrews[0]); ++i)
		std::printf ("%s[%d,%d]", i ? "," : "", kScrews[i].x, kScrews[i].y);
	std::printf ("],\n");

	std::printf ("  \"sections\": [\n");
	for (int i = 0; i < kNumSections; ++i)
		std::printf ("    {\"title\": %s, \"subtitle\": %s, \"rect\": %s, \"titleBox\": %s, \"subtitleLeft\": %s}%s\n",
		             quoted (kSections[i].title).c_str (), quoted (kSections[i].subtitle).c_str (),
		             rect (kSections[i].r).c_str (), box (titleBox (kSections[i])).c_str (),
		             num (subtitleLeft (kSections[i])).c_str (), (i + 1 < kNumSections) ? "," : "");
	std::printf ("  ],\n");
	std::printf ("  \"recordRule\": %s, \"outputTitle\": %s,\n", rect (kRecordRule).c_str (), rect (kOutputTitle).c_str ());
	std::printf ("  \"surfaceNote\": {\"rect\": %s, \"text\": %s},\n", rect (kSurfaceNote).c_str (),
	             quoted (kSurfaceNoteText).c_str ());

	// ---- the lines that change, as the editor's refresh() sets them -------------
	std::printf ("  \"lines\": [\n");
	std::printf ("    {\"rect\": %s, \"style\": \"header\", \"ink\": \"ink\", \"align\": \"left\", \"text\": %s},\n",
	             rect (kHeaderLine).c_str (), quoted (headerLineText (era, disc, speed, player, speaker)).c_str ());
	std::printf ("    {\"rect\": %s, \"style\": \"note\", \"ink\": \"soft\", \"align\": \"centre\", \"text\": %s},\n",
	             rect (kDiscNote).c_str (), quoted (discNoteText (era)).c_str ());
	std::printf ("    {\"rect\": %s, \"style\": \"note\", \"ink\": \"soft\", \"align\": \"left\", \"text\": %s},\n",
	             rect (kTurntableLine).c_str (), quoted (turntableLineText (era, disc, scratches, dust)).c_str ());
	std::printf ("    {\"rect\": %s, \"style\": \"note\", \"ink\": \"soft\", \"align\": \"left\", \"text\": %s}\n",
	             rect (kSpeedLine).c_str (), quoted (speedLineText (era, disc, speed)).c_str ());
	std::printf ("  ],\n");
	std::printf ("  \"lamp\": {\"rect\": %s, \"name\": \"SKIP\", \"on\": false},\n", rect (kLamp).c_str ());

	// ---- the controls --------------------------------------------------------------
	std::printf ("  \"controls\": [\n");
	for (int i = 0; i < kNumControls; ++i)
	{
		const Control& c = kControls[i];
		const auto id = static_cast<Steinberg::Vst::ParamID> (c.tag);
		const double v = kParams[id].defaultNormalized ();
		if (c.kind == Kind::Knob)
		{
			std::printf ("    {\"kind\": \"knob\", \"rect\": %s, \"diameter\": %d, \"value\": %.4f, \"text\": %s, "
			             "\"label\": %s}%s\n",
			             rect (c.r).c_str (), c.diameter, v, quoted (knobReadout (c.tag, kParams[id].toPlain (v))).c_str (),
			             quoted (c.label).c_str (), (i + 1 < kNumControls) ? "," : "");
			continue;
		}
		const int n = positionCount (c.tag);
		const Selector g = selectorOf (c, n);
		std::string names = "[", angles = "[", boxes = "[";
		for (int p = 0; p < n; ++p)
		{
			const std::string name = positionName (c.tag, p);
			names += std::string (p ? "," : "") + quoted (name);
			angles += std::string (p ? "," : "") + num (g.angle[p]);
			boxes += std::string (p ? "," : "") + box (selectorLabelBox (g, p, name));
		}
		// The Disc switch, locked by the era, shows the disc the era plays.
		int current = static_cast<int> (kParams[id].toInternal (v));
		bool locked = false;
		if (c.tag == kTagDisc && discLocked (era))
		{
			current = effectiveDisc (era, disc);
			locked = true;
		}
		std::printf ("    {\"kind\": \"selector\", \"rect\": %s, \"label\": %s, \"current\": %d, \"locked\": %s, "
		             "\"cx\": %s, \"cy\": %s, \"radius\": %s, \"nameY\": %s, \"names\": %s], \"angles\": %s], "
		             "\"labelBoxes\": %s]}%s\n",
		             rect (c.r).c_str (), quoted (c.label).c_str (), current, locked ? "true" : "false",
		             num (g.cx).c_str (), num (g.cy).c_str (), num (g.radius).c_str (), num (g.nameCentreY).c_str (),
		             names.c_str (), angles.c_str (), boxes.c_str (), (i + 1 < kNumControls) ? "," : "");
	}
	std::printf ("  ],\n");

	// ---- the turntable screen, from the DSP's shared rules ---------------------------
	std::string scratchList = "[";
	for (int i = 0; i < activeScratches (scratches); ++i)
	{
		const ScratchSlot s = scratchSlot (i);
		scratchList += std::string (i ? "," : "") + "[" + num (screenAngle (s.phase, angle)) + "," + num (s.depth) + "]";
	}
	std::string speckList = "[";
	for (int i = 0; i < specksShown (dust); ++i)
	{
		const Speck s = speckOf (i);
		speckList += std::string (i ? "," : "") + "[" + num (screenAngle (s.phase, angle)) + "," + num (s.radius) + "," +
		             num (s.size) + "]";
	}
	std::printf ("  \"turntable\": {\"rect\": %s, \"disc\": [%s,%s], \"platterR\": %s, \"recordR\": %s,\n",
	             rect (kScreen).c_str (), num (kDiscX).c_str (), num (kDiscY).c_str (), num (kPlatterR).c_str (),
	             num (kRecordR).c_str ());
	std::printf ("    \"grooveInner\": %s, \"grooveOuter\": %s, \"labelR\": %s, \"labelColour\": %s, \"goldRing\": %s,\n",
	             num (kGrooveInner).c_str (), num (kGrooveOuter).c_str (), num (kLabelR).c_str (),
	             rgb (eraLabelColour (era)).c_str (), era == kEra1940 ? rgb (kLabelGoldRing).c_str () : "null");
	std::printf ("    \"labelMark\": %s, \"armPivot\": [%s,%s], \"stylusR\": %s, \"stylusAngle\": %s, \"lift\": %s,\n",
	             num (screenAngle (0.0, angle)).c_str (), num (kArmPivotX).c_str (), num (kArmPivotY).c_str (),
	             num (kStylusR).c_str (), num (kStylusAngle).c_str (), num (warp / 100.0 * warpBump (angle)).c_str ());
	std::printf ("    \"scratches\": %s], \"specks\": %s],\n", scratchList.c_str (), speckList.c_str ());
	std::printf ("    \"rpm\": %s, \"channels\": %s, \"discName\": %s, \"stuck\": false}\n",
	             quoted (rpmText (era, disc, speed)).c_str (), quoted (isStereo (era) ? "STEREO" : "MONO").c_str (),
	             quoted (upperCase (discName (effectiveDisc (era, disc)))).c_str ());

	// ---- the About box, as AboutBox::draw lays it out ------------------------------
	{
		About::Info info;
		info.version = FULL_VERSION_STR;
		info.commit = "24f0b18"; info.branch = "main";
		info.audio = { true, 48000.0, 512, 32, 0, 2 };
		info.hostName = "Cubase 14";
		info.processName = "Cubase 14";
		info.binaryPath = "/Library/Audio/Plug-Ins/VST3/WornVinyl.vst3/Contents/MacOS/WornVinyl";
		const auto rs = About::rows (info);
		std::string rowList = "[";
		for (size_t i = 0; i < rs.size (); ++i)
		{
			std::string ls = "[";
			for (size_t l = 0; l < rs[i].lines.size (); ++l)
				ls += std::string (l ? "," : "") + quoted (rs[i].lines[l]);
			rowList += std::string (i ? "," : "") + "{\"label\": " + quoted (rs[i].label) + ", \"y\": " +
			           num (About::rowCentreY (rs, i)) + ", \"lines\": " + ls + "]}";
		}
		std::printf ("  ,\"about\": {\"box\": %s, \"wordmark\": %s, \"labelX\": %s, \"valueX\": %s, \"lineGap\": %s,\n",
		             rect (kAboutBox).c_str (), rect (kWordmark).c_str (), num (kAboutLabelX).c_str (),
		             num (kAboutValueX).c_str (), num (kAboutLineGap).c_str ());
		std::printf ("    \"wordmarkY\": %s, \"taglineY\": %s, \"footerY\": %s, \"tagline\": %s, \"footer\": %s,\n",
		             num (kAboutWordmarkY).c_str (), num (kAboutTaglineY).c_str (), num (kAboutFooterY).c_str (),
		             quoted (kAboutTagline).c_str (), quoted (kAboutFooter).c_str ());
		std::printf ("    \"labelStyle\": %s, \"valueStyle\": %s,\n    \"rows\": %s]}\n",
		             style (kTextAboutLabel).c_str (), style (kTextAboutValue).c_str (), rowList.c_str ());
	}
	std::printf ("}\n");
	return 0;
}
