//------------------------------------------------------------------------
// WornVinyl - DSP tests. SDK-free: nothing but a compiler.
//
//   c++ -std=c++17 -O2 -Isource tests/DspTests.cpp source/WornVinylDsp.cpp -o /tmp/dsptests && /tmp/dsptests
//
// Every guard here has a NEGATIVE CONTROL beside it - the same comparison
// made where it must fail - because a check that has never failed is a
// guess.
//------------------------------------------------------------------------

#include "WornVinylDsp.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace WornVinyl;

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
constexpr double kPi = 3.14159265358979323846;

/** A clean patch: nothing but the era's chain. */
void clean (Dsp& d)
{
	d.setScratchesPct (0); d.setDustPct (0); d.setWowPct (0); d.setFlutterPct (0); d.setWarpPct (0);
	d.setMixPct (100); d.setOutputDb (0); d.setBypass (false); d.setJumps (true);
	d.snapParameters ();
}

void run (Dsp& d, std::vector<float>& l, std::vector<float>& r, std::vector<float>& ol, std::vector<float>& orr,
          int block = 256)
{
	ol.resize (l.size ()); orr.resize (r.size ());
	for (size_t i = 0; i < l.size (); i += block)
	{
		const int n = static_cast<int> (std::min<size_t> (block, l.size () - i));
		d.process (l.data () + i, r.data () + i, ol.data () + i, orr.data () + i, n);
	}
}

double rmsDb (const std::vector<float>& x, size_t from = 0)
{
	double s = 0;
	for (size_t i = from; i < x.size (); ++i) s += double (x[i]) * x[i];
	return 10.0 * std::log10 (s / std::max<size_t> (1, x.size () - from) + 1e-30);
}

double peakDb (const std::vector<float>& x, size_t from = 0)
{
	double p = 0;
	for (size_t i = from; i < x.size (); ++i) p = std::max (p, std::fabs (double (x[i])));
	return 20.0 * std::log10 (p + 1e-30);
}

std::vector<float> sine (double fs, double hz, double seconds, double amp)
{
	std::vector<float> v (static_cast<size_t> (fs * seconds));
	for (size_t i = 0; i < v.size (); ++i) v[i] = static_cast<float> (amp * std::sin (2 * kPi * hz * i / fs));
	return v;
}

/** The largest pitch deviation, as a fraction, from a sine's
    zero-crossing periods (rising crossings, linearly interpolated). */
double pitchDeviation (const std::vector<float>& y, double fs, double hz, size_t from)
{
	std::vector<double> t;
	for (size_t i = from + 1; i < y.size (); ++i)
		if (y[i - 1] < 0 && y[i] >= 0)
			t.push_back ((i - 1) + y[i - 1] / (y[i - 1] - y[i]));
	double worst = 0;
	for (size_t k = 4; k < t.size (); ++k)   // over four periods, to ride out the ripple
	{
		const double f = 4.0 * fs / (t[k] - t[k - 4]);
		worst = std::max (worst, std::fabs (f / hz - 1.0));
	}
	return worst;
}
} // namespace

//------------------------------------------------------------------------
static void testSharedRules ()
{
	std::printf ("shared rules\n");
	CHECK (effectiveDisc (kEra1920, kDiscVinyl) == kDiscShellac, "1920s are shellac whatever the switch");
	CHECK (effectiveDisc (kEra1940, kDiscVinyl) == kDiscShellac, "1940s shellac");
	CHECK (effectiveDisc (kEra1950, kDiscShellac) == kDiscShellac, "1950s: the switch");
	CHECK (effectiveDisc (kEra1950, kDiscVinyl) == kDiscVinyl, "1950s: the switch");
	CHECK (effectiveDisc (kEra1960, kDiscShellac) == kDiscVinyl, "1960s vinyl");
	CHECK (effectiveDisc (kEra1970, kDiscShellac) == kDiscVinyl, "1970s vinyl");
	CHECK (discLocked (kEra1920) && ! discLocked (kEra1950) && discLocked (kEra1970), "locked but the 50s");
	CHECK (std::fabs (rpmOf (kDiscShellac) - 78.0) < 1e-9 && std::fabs (rpmOf (kDiscVinyl) - 33.3333333) < 1e-6, "rpm");
	CHECK (! isStereo (kEra1950) && isStereo (kEra1960), "stereo from the 60s");
	CHECK (turntableRpm (kEra1960, kDiscVinyl, kSpeedAuto) == rpmOf (kDiscVinyl), "Auto: an LP at 33");
	CHECK (turntableRpm (kEra1920, kDiscVinyl, kSpeedAuto) == 78.0, "Auto: shellac at 78");
	CHECK (turntableRpm (kEra1960, kDiscVinyl, kSpeed45) == 45.0, "45");
	CHECK (turntableRpm (kEra1920, kDiscShellac, kSpeed33) == rpmOf (kDiscVinyl), "a 78 at 33");
	for (int sp = 0; sp < kSpeedCount; ++sp)
		CHECK (turntableRpm (kEra1950, kDiscVinyl, sp) >= rpmOf (kDiscVinyl),
		       "nothing turns slower than 33 - the ring buffer is sized for it");

	CHECK (activeScratches (0) == 0, "none at 0");
	CHECK (activeScratches (0.1) == 1, "one at the first touch");
	CHECK (activeScratches (50) == 6, "%d at 50", activeScratches (50));
	CHECK (activeScratches (100) == kMaxScratches, "all at 100");
	double deepest = 0;
	for (int i = 0; i < kMaxScratches; ++i)
	{
		const auto s = scratchSlot (i);
		CHECK (s.phase >= 0 && s.phase < 1 && s.depth > 0 && s.depth <= 1, "slot %d in range", i);
		deepest = std::max (deepest, s.depth);
	}
	CHECK (scratchSlot (0).depth == deepest, "slot 0 is the deepest - the needle jumps at it");

	CHECK (jumpChancePerSecond (kJumpThresholdPct, true) == 0.0, "no jumps at the threshold");
	CHECK (jumpChancePerSecond (100, true) > 0.1, "jumps at 100");
	CHECK (jumpChancePerSecond (100, false) == 0.0, "none with Jumps off");
	CHECK (crackleRate (kEra1960, kDiscVinyl, 0) == 0.0, "no crackle without dust");
	CHECK (crackleRate (kEra1920, kDiscShellac, 50) > crackleRate (kEra1970, kDiscVinyl, 50), "shellac crackles more");

	std::printf ("  bandwidths:");
	for (int e = 0; e < kEraCount; ++e)
		for (int disc = 0; disc < kDiscCount; ++disc)
			if (! discLocked (e) || disc == effectiveDisc (e, disc))
				std::printf ("  %s %s: %s", eraName (e), discName (effectiveDisc (e, disc)), bandwidthText (e, disc).c_str ());
	std::printf ("\n");
	CHECK (bandwidthText (kEra1960, kDiscVinyl) == "28 Hz - 15 kHz", "%s", bandwidthText (kEra1960, kDiscVinyl).c_str ());
}

//------------------------------------------------------------------------
static void testLatencyAndDry ()
{
	std::printf ("latency, dry, bypass\n");
	for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
	{
		Dsp d;
		d.setSampleRate (fs);
		clean (d);
		const int lat = d.latencySamples ();
		CHECK (lat == static_cast<int> (std::ceil (kBaseDelaySeconds * fs)), "latency %d at %.0f", lat, fs);
		CHECK (d.bufferFrames () >= static_cast<int> (fs * kMaxStick * 1.8), "the buffer holds %d turns", kMaxStick);
	}

	// Mix 0: the dry path, exactly the input `latency` samples late.
	Dsp d;
	d.setSampleRate (48000);
	clean (d);
	d.setScratchesPct (100); d.setDustPct (100); d.setWowPct (100); d.setWarpPct (100);
	d.setMixPct (0);
	d.snapParameters ();
	auto in = sine (48000, 440, 1.0, 0.5);
	std::vector<float> outL, outR;
	run (d, in, in, outL, outR);
	const int lat = d.latencySamples ();
	bool exact = true;
	for (size_t i = lat; i < in.size (); ++i)
		exact = exact && outL[i] == in[i - lat] && outR[i] == in[i - lat];
	CHECK (exact, "mix 0 is the input, delayed, bit for bit");

	// Bypass: the same, whatever else is set.
	Dsp b;
	b.setSampleRate (48000);
	b.setDustPct (100); b.setScratchesPct (100);
	b.setBypass (true);
	b.snapParameters ();
	run (b, in, in, outL, outR);
	exact = true;
	for (size_t i = lat; i < in.size (); ++i)
		exact = exact && outL[i] == in[i - lat];
	CHECK (exact, "bypass is the input, delayed (the latency holds)");

	// NEGATIVE CONTROL: half gain must not be identical.
	Dsp n;
	n.setSampleRate (48000);
	clean (n);
	n.setMixPct (0); n.setOutputDb (-6.0206);
	n.snapParameters ();
	run (n, in, in, outL, outR);
	exact = true;
	for (size_t i = lat; i < in.size (); ++i)
		exact = exact && outL[i] == in[i - lat];
	CHECK (! exact, "negative control: at -6 dB the comparison fails");
}

//------------------------------------------------------------------------
static void testLevels ()
{
	std::printf ("levels: -12 dBFS sine at 1 kHz through each era (clean patch)\n");
	const double fs = 48000;
	auto in = sine (fs, 1000, 2.0, 0.25);
	for (int e = 0; e < kEraCount; ++e)
		for (int disc = 0; disc < kDiscCount; ++disc)
		{
			if (discLocked (e) && disc != effectiveDisc (e, disc))
				continue;
			Dsp d;
			d.setSampleRate (fs);
			d.setEra (e); d.setDisc (disc);
			clean (d);
			std::vector<float> l, r;
			run (d, in, in, l, r);
			const double gain = rmsDb (l, 24000) - rmsDb (in, 24000);
			std::printf ("  %s %-7s  in -15.1 dB RMS  out %+6.1f dB  (make-up %+.1f dB)\n", eraName (e),
			             discName (effectiveDisc (e, disc)), gain, d.makeupDb ());
			CHECK (std::fabs (gain) < 6.0, "1 kHz within 6 dB of unity in %s", eraName (e));
		}

	// THE DEFAULT PATCH, before anyone plays it: a -12 dBFS sine and the
	// default 1960s vinyl, 20 % scratches, 30 % dust, wow and flutter.
	Dsp d;
	d.setSampleRate (fs);
	d.snapParameters ();
	auto in10 = sine (fs, 1000, 10.0, 0.25);
	std::vector<float> l, r;
	run (d, in10, in10, l, r);
	std::printf ("  DEFAULT PATCH: input peak %.1f dBFS -> output peak %.1f dBFS, RMS %.1f dBFS\n",
	             peakDb (in10), peakDb (l), rmsDb (l, 4800));
	CHECK (peakDb (l) < -3.0, "the default patch does not clip a -12 dBFS sine");

	// The surface alone, at the default dust, per era.
	std::vector<float> silence (static_cast<size_t> (fs * 4), 0.0f);
	std::printf ("  surface alone (silence in, 30 %% dust, 20 %% scratches):");
	for (int e = 0; e < kEraCount; ++e)
	{
		Dsp s;
		s.setSampleRate (fs);
		s.setEra (e);
		s.snapParameters ();
		run (s, silence, silence, l, r);
		std::printf ("  %s %.1f/%.1f", eraName (e), rmsDb (l), peakDb (l));
		CHECK (rmsDb (l) < -30.0 && rmsDb (l) > -90.0, "audible but not loud in %s", eraName (e));
	}
	std::printf ("  (RMS/peak dBFS)\n");
}

//------------------------------------------------------------------------
static void testWorstCase ()
{
	std::printf ("worst case: everything at 100 %%, full-scale noise, every era\n");
	const double fs = 44100;
	std::vector<float> in (static_cast<size_t> (fs * 20));
	uint32_t x = 1;
	for (auto& v : in) { x = x * 1664525u + 1013904223u; v = (x >> 8) / 8388608.0f - 1.0f; }
	for (int e = 0; e < kEraCount; ++e)
	{
		Dsp d;
		d.setSampleRate (fs);
		d.setEra (e); d.setDisc (kDiscShellac);
		d.setScratchesPct (100); d.setDustPct (100); d.setWowPct (100); d.setFlutterPct (100); d.setWarpPct (100);
		d.setOutputDb (12);
		d.snapParameters ();
		std::vector<float> l, r;
		run (d, in, in, l, r, 512);
		bool finite = true;
		for (size_t i = 0; i < l.size (); ++i)
			finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
		const double minDelay = d.takeMinReadDelay ();
		std::printf ("  %s  closest read %.1f samples, peak %.1f dBFS, jumps %d\n", eraName (e), minDelay,
		             peakDb (l), d.jumpsTaken ());
		CHECK (finite, "no NaN or inf in %s", eraName (e));
		CHECK (minDelay >= 3.0, "the read head stays %.1f >= 3 samples behind the write head", minDelay);
		CHECK (peakDb (l) < 24.0, "bounded");
	}
}

//------------------------------------------------------------------------
static void testPitch ()
{
	std::printf ("pitch: a 1 kHz sine, one modulation at a time (1970s, 48 k)\n");
	const double fs = 48000;
	auto in = sine (fs, 1000, 12.0, 0.25);
	struct Case { const char* name; double wow, flutter, warp, lo, hi; };
	const Case cases[] = {
		{ "none",         0,   0,   0, 0.0,    0.0005 },
		{ "wow 100",    100,   0,   0, 0.008,  0.0160 },
		{ "flutter 100",  0, 100,   0, 0.0015, 0.0045 },
		{ "warp 100",     0,   0, 100, 0.025,  0.0450 },
		{ "wow 15",      15,   0,   0, 0.0010, 0.0025 },
	};
	for (const auto& c : cases)
	{
		Dsp d;
		d.setSampleRate (fs);
		d.setEra (kEra1970);
		clean (d);
		d.setWowPct (c.wow); d.setFlutterPct (c.flutter); d.setWarpPct (c.warp);
		d.snapParameters ();
		std::vector<float> l, r;
		run (d, in, in, l, r);
		const double dev = pitchDeviation (l, fs, 1000, 4800);
		std::printf ("  %-12s peak deviation %.3f %%\n", c.name, dev * 100);
		CHECK (dev >= c.lo && dev <= c.hi, "%s: %.4f in [%.4f, %.4f]", c.name, dev, c.lo, c.hi);
	}
}

//------------------------------------------------------------------------
static void testJumps ()
{
	std::printf ("jumps\n");
	const double fs = 44100;
	std::vector<float> in (static_cast<size_t> (fs * 90), 0.1f);
	auto count = [&] (double scratches, bool on, int era) {
		Dsp d;
		d.setSampleRate (fs);
		d.setEra (era);
		clean (d);
		d.setScratchesPct (scratches); d.setJumps (on);
		d.snapParameters ();
		std::vector<float> l, r;
		run (d, in, in, l, r);
		return d.jumpsTaken ();
	};
	const int lp = count (100, true, kEra1970), s78 = count (100, true, kEra1920);
	std::printf ("  90 s at 100 %%: %d jumps on an LP, %d on a 78\n", lp, s78);
	CHECK (lp > 2 && s78 > 2, "the needle jumps at 100 %%");
	CHECK (count (100, false, kEra1970) == 0, "never with Jumps off");
	CHECK (count (kJumpThresholdPct, true, kEra1970) == 0, "never at the threshold");

	// After a stick the needle always comes back to where the music is.
	Dsp d;
	d.setSampleRate (fs);
	clean (d);
	d.setScratchesPct (100);
	d.snapParameters ();
	std::vector<float> l, r;
	std::vector<float> chunk (static_cast<size_t> (fs * 30), 0.1f);
	run (d, chunk, chunk, l, r);
	d.setScratchesPct (0);    // no more sticks: whatever is under way releases at the next turn
	std::vector<float> tail (static_cast<size_t> (fs * 2 * 60.0 / rpmOf (kDiscVinyl)) + 100, 0.1f);
	run (d, tail, tail, l, r);
	CHECK (! d.skipping (), "released after the scratches went");
}

//------------------------------------------------------------------------
static void testSpeed ()
{
	std::printf ("speed: the disc turns at the switch's rpm, the music keeps its pitch\n");
	const double fs = 48000;
	for (int sp = 0; sp < kSpeedCount; ++sp)
	{
		Dsp d;
		d.setSampleRate (fs);
		d.setEra (kEra1960);
		clean (d);
		d.setSpeed (sp);
		d.snapParameters ();
		// Ten seconds of silence; count the turns by the angle wrapping.
		std::vector<float> in (480, 0.0f), l, r;
		int turns = 0;
		double last = d.discAngle ();
		for (int b = 0; b < 1000; ++b)
		{
			run (d, in, in, l, r, 480);
			if (d.discAngle () < last)
				++turns;
			last = d.discAngle ();
		}
		const double rpm = d.rpm ();
		const double expected = rpm / 6.0;   // turns in 10 s
		std::printf ("  %-4s %5.1f rpm: %d turns in 10 s\n", speedName (sp), rpm, turns);
		CHECK (std::abs (turns - expected) <= 1.0, "%s: %d turns, expected %.1f", speedName (sp), turns, expected);
	}

	// The pitch is untouched whatever the speed (wow off).
	auto in = sine (fs, 1000, 4.0, 0.25);
	Dsp d;
	d.setSampleRate (fs);
	d.setEra (kEra1970);
	clean (d);
	d.setSpeed (kSpeed78);
	d.snapParameters ();
	std::vector<float> l, r;
	run (d, in, in, l, r);
	CHECK (pitchDeviation (l, fs, 1000, 4800) < 0.0005, "78 rpm does not move the pitch");

	// Wow follows the speed: the same depth, a faster wobble - so the read
	// head swings LESS, and the 33 case stays the worst for the buffer.
	auto wow = [&] (int sp) {
		Dsp w;
		w.setSampleRate (fs);
		w.setEra (kEra1970);
		clean (w);
		w.setWowPct (100);
		w.setSpeed (sp);
		w.snapParameters ();
		std::vector<float> ol, orr;
		run (w, in, in, ol, orr);
		return pitchDeviation (ol, fs, 1000, 4800);
	};
	const double w33 = wow (kSpeed33), w78 = wow (kSpeed78);
	std::printf ("  wow 100 %%: peak deviation %.2f %% at 33, %.2f %% at 78\n", w33 * 100, w78 * 100);
	CHECK (w78 > 0.008 && w78 < 0.018, "the same wow depth at 78");
}

//------------------------------------------------------------------------
static void testMonoAndDeterminism ()
{
	std::printf ("mono eras, determinism\n");
	const double fs = 44100;
	auto left = sine (fs, 300, 3.0, 0.3);
	std::vector<float> right (left.size (), 0.0f);
	for (int e : { kEra1920, kEra1940, kEra1950 })
	{
		Dsp d;
		d.setSampleRate (fs);
		d.setEra (e);
		d.setDustPct (80); d.setScratchesPct (80);
		d.snapParameters ();
		std::vector<float> l, r;
		run (d, left, right, l, r);
		CHECK (std::memcmp (l.data (), r.data (), l.size () * sizeof (float)) == 0, "%s is mono, L == R", eraName (e));
	}
	{
		Dsp d;
		d.setSampleRate (fs);
		d.setEra (kEra1970);
		d.snapParameters ();
		std::vector<float> l, r;
		run (d, left, right, l, r);
		CHECK (std::memcmp (l.data (), r.data (), l.size () * sizeof (float)) != 0, "negative control: the 1970s are stereo");
	}

	Dsp a, b;
	a.setSampleRate (fs); b.setSampleRate (fs);
	a.snapParameters (); b.snapParameters ();
	std::vector<float> l1, r1, l2, r2;
	run (a, left, left, l1, r1, 128);
	run (b, left, left, l2, r2, 1000);   // block size must not matter
	CHECK (l1 == l2 && r1 == r2, "the same audio is the same bounce, at any block size");
	a.reset ();
	run (a, left, left, l2, r2);
	CHECK (l1 == l2, "reset re-seeds");
}

//------------------------------------------------------------------------
static void testSmoothingAndEdges ()
{
	std::printf ("smoothing, edges\n");
	const double fs = 48000;
	Dsp d;
	d.setSampleRate (fs);
	clean (d);
	d.setMixPct (0); d.setOutputDb (kOutputMinDb);
	d.snapParameters ();
	std::vector<float> dc (static_cast<size_t> (fs), 0.5f), l, r;
	run (d, dc, dc, l, r);
	d.setOutputDb (kOutputMaxDb);
	run (d, dc, dc, l, r);
	const double span = 0.5 * (dbToGain (kOutputMaxDb) - dbToGain (kOutputMinDb));
	double worst = 0;
	for (size_t i = 1; i < l.size (); ++i)
		worst = std::max (worst, std::fabs (double (l[i]) - l[i - 1]) / span);
	std::printf ("  output -12 -> +12 dB: largest step %.3f %% of the span a sample\n", worst * 100);
	CHECK (worst <= 0.01, "the smoother never steps more than 1 %% a sample");
	CHECK (std::fabs (l.back () - 0.5 * dbToGain (kOutputMaxDb)) < 1e-3, "and arrives");

	// An era change fades rather than clicks: no sample-to-sample jump
	// bigger than the sine's own.
	Dsp e;
	e.setSampleRate (fs);
	clean (e);
	auto s = sine (fs, 200, 0.5, 0.5);
	run (e, s, s, l, r);
	e.setEra (kEra1920);
	run (e, s, s, l, r);
	double step = 0;
	for (size_t i = 1; i < l.size (); ++i)
		step = std::max (step, std::fabs (double (l[i]) - l[i - 1]));
	const double sineStep = 0.5 * 2 * kPi * 200 / fs * 3.0;   // the sine's, with the 1920s' make-up
	CHECK (step < sineStep, "an era change: biggest step %.4f < %.4f", step, sineStep);

	// Nothing breaks on nothing.
	float buf[4] = {};
	d.process (buf, buf, buf, buf, 0);
	d.process (nullptr, buf, buf, buf, 4);
	d.process (buf, buf, nullptr, buf, 4);
	float io[256];
	for (int i = 0; i < 256; ++i) io[i] = std::sin (i * 0.1f);
	d.process (io, io, io, io, 256);   // in place, both channels the same buffer
	CHECK (true, "zero frames, null buffers, in place");
}

//------------------------------------------------------------------------
static void testCost ()
{
	const double fs = 48000;
	Dsp d;
	d.setSampleRate (fs);
	d.setScratchesPct (100); d.setDustPct (100); d.setWowPct (100); d.setFlutterPct (100); d.setWarpPct (100);
	d.snapParameters ();
	std::vector<float> in (static_cast<size_t> (fs * 10), 0.1f), l, r;
	const auto t0 = std::chrono::steady_clock::now ();
	run (d, in, in, l, r, 512);
	const double s = std::chrono::duration<double> (std::chrono::steady_clock::now () - t0).count ();
	std::printf ("cost: everything at 100 %%, 48 k stereo: %.2f %% of one core\n", s / 10.0 * 100);
}

//------------------------------------------------------------------------
int main ()
{
	testSharedRules ();
	testLatencyAndDry ();
	testLevels ();
	testWorstCase ();
	testPitch ();
	testJumps ();
	testSpeed ();
	testMonoAndDeterminism ();
	testSmoothingAndEdges ();
	testCost ();
	std::printf ("\nDSP: %d checks, %d failed\n", gChecks, gFails);
	return gFails ? 1 : 0;
}
