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
#include "DenormalGuard.h"

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
/** WHEN EACH BURST CAME OUT: 64-sample noise bursts every `spacing`
    samples, each found by cross-correlation within +/- `window` of the
    reported latency, to a fraction of a sample (parabolic peak). This is
    what a host's latency test does - one burst of it. */
static std::vector<double> burstDelays (Dsp& d, double fs, double seconds, int spacing, int window)
{
	const int lat = d.latencySamples ();
	const size_t n = static_cast<size_t> (fs * seconds);
	std::vector<float> in (n, 0.0f), l, r;
	uint32_t s = 12345;
	std::vector<size_t> starts;
	for (size_t at = static_cast<size_t> (spacing); at + spacing < n; at += static_cast<size_t> (spacing))
	{
		starts.push_back (at);
		for (int i = 0; i < 64; ++i)
		{
			s = s * 1664525u + 1013904223u;
			in[at + i] = static_cast<float> (((s >> 8) / 16777216.0 - 0.5) * 0.8);
		}
	}
	run (d, in, in, l, r);
	std::vector<double> out;
	for (size_t at : starts)
	{
		std::vector<double> c (static_cast<size_t> (2 * window + 1));
		int best = 0;
		for (int k = -window; k <= window; ++k)
		{
			double a = 0;
			for (int i = 0; i < 64; ++i)
				a += double (in[at + i]) * l[at + i + lat + k];
			c[static_cast<size_t> (k + window)] = a;
			if (a > c[static_cast<size_t> (best + window)])
				best = k;
		}
		const size_t b = static_cast<size_t> (best + window);
		double frac = 0.0;
		if (b > 0 && b + 1 < c.size ())
		{
			const double den = c[b - 1] - 2 * c[b] + c[b + 1];
			frac = den != 0.0 ? 0.5 * (c[b - 1] - c[b + 1]) / den : 0.0;
		}
		out.push_back (lat + best + frac);
	}
	return out;
}

static void testMeasuredLatency ()
{
	std::printf ("latency, measured as a host measures it\n");

	// STILL: no wow, flutter or warp - the read head rests at the latency,
	// and every burst comes out on it. What is left is the era EQ's own
	// group delay: the 1960s' 4-pole low-pass at 15 kHz delays by ~0.03 ms
	// (1.3 samples at 48 k, 3 at 96 k) - a filter's delay, which no plug-in
	// reports. So the tolerance is in TIME: 0.05 ms. The bursts land within
	// a sample of each other, not exactly together: the oversamplers' IIR
	// half-bands delay a little more or less with frequency, and each
	// burst's spectrum differs (wow, by contrast, spreads them by ~100).
	double still48 = 0.0;
	for (double fs : { 44100.0, 48000.0, 96000.0 })
	{
		Dsp d;
		d.setSampleRate (fs);
		clean (d);
		const auto t = burstDelays (d, fs, 2.0, static_cast<int> (fs / 10), 60);
		double lo = 1e9, hi = -1e9, mean = 0;
		for (double v : t) { lo = std::min (lo, v); hi = std::max (hi, v); mean += v; }
		mean /= t.size ();
		if (fs == 48000.0) still48 = mean;
		std::printf ("  %.0f Hz still: reported %d, measured %.1f (%.1f .. %.1f)\n", fs, d.latencySamples (), mean, lo, hi);
		CHECK (std::fabs (mean - d.latencySamples ()) < 0.00005 * fs && hi - lo < 1.0,
		       "%.0f Hz: with nothing moving the head, measured = reported (%.1f vs %d)", fs, mean, d.latencySamples ());
	}

	// MOVING: the defaults (Wow 15 %, Flutter 10 %). Each burst lands up
	// to ~50 samples either side - that is what wow is - but over whole
	// cycles of the motor's drift (3 x 1/0.13 Hz) they average out to the
	// latency the plug-in reports, so the host lines the track up right.
	{
		const double fs = 48000;
		Dsp d;
		d.setSampleRate (fs);
		d.setDustPct (0); d.setScratchesPct (0);   // Wow and Flutter stay at their defaults
		d.snapParameters ();
		const double seconds = 3.0 / 0.13 + 0.1;
		const auto t = burstDelays (d, fs, seconds, 2400, 120);
		double lo = 1e9, hi = -1e9, mean = 0;
		for (double v : t) { lo = std::min (lo, v); hi = std::max (hi, v); mean += v; }
		mean /= t.size ();
		std::printf ("  48 kHz, default wow + flutter: %zu bursts %.1f .. %.1f, mean %.1f (still %.1f, reported %d)\n",
		             t.size (), lo, hi, mean, still48, d.latencySamples ());
		CHECK (hi - lo > 40.0, "the wow really moves the head (%.1f samples), so the next check is not vacuous", hi - lo);
		CHECK (std::fabs (mean - still48) < 1.5, "on average the moving head sits where the still one does (%.1f vs %.1f)",
		       mean, still48);
		CHECK (std::fabs (mean - d.latencySamples ()) < 0.00005 * fs, "so the reported latency is right on average (%.1f vs %d)",
		       mean, d.latencySamples ());
		CHECK (std::fabs (mean - (d.latencySamples () + 5)) >= 0.00005 * fs,
		       "negative control: a latency misreported by 5 samples fails this check");
	}
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
/** A tone's level in `x` (dBFS), from a Blackman-Harris windowed DFT over
    the last `n` samples - for spotting an alias 100 dB down. */
static double toneDb (const std::vector<float>& x, size_t n, double fs, double hz)
{
	const size_t from = x.size () - n;
	double re = 0, im = 0, w = 0;
	for (size_t i = 0; i < n; ++i)
	{
		const double t = double (i) / (n - 1);
		const double win = 0.35875 - 0.48829 * std::cos (2 * kPi * t) + 0.14128 * std::cos (4 * kPi * t) -
		                   0.01168 * std::cos (6 * kPi * t);
		re += win * x[from + i] * std::cos (2 * kPi * hz * i / fs);
		im += win * x[from + i] * std::sin (2 * kPi * hz * i / fs);
		w += win;
	}
	return 20 * std::log10 (2 * std::sqrt (re * re + im * im) / w + 1e-30);
}

static void testAliasing ()
{
	std::printf ("aliasing: a 10 kHz tone's third harmonic (30 kHz) must not fold back to 18 kHz\n");
	const double fs = 48000;
	const size_t n = 65536;
	auto in = sine (fs, 10000, 2.0, 0.5);   // -6 dBFS
	auto loud = sine (fs, 10000, 2.0, 0.9); // -1 dBFS

	// NEGATIVE CONTROL: the lathe's curve applied the naive way, at 48 k -
	// what the plug-in did before the oversampling. The measurement sees it.
	{
		std::vector<float> naive (in.size ());
		for (size_t i = 0; i < in.size (); ++i)
			naive[i] = static_cast<float> (latheCurve (in[i], profileOf (kEra1960, kDiscVinyl).drive));
		const double a = toneDb (naive, n, fs, 18000) - toneDb (naive, n, fs, 10000);
		std::printf ("  the lathe's curve, not oversampled: 18 kHz at %.1f dBc\n", a);
		CHECK (a > -60.0, "negative control: the naive curve aliases (%.1f dBc), so the measurement can see it", a);
	}

	double worst = -300;
	for (int e = 0; e < kEraCount; ++e)
		for (int spk = 0; spk < kSpeakerCount; ++spk)
			for (auto* x : { &in, &loud })
			{
				Dsp d;
				d.setSampleRate (fs);
				d.setEra (e);
				d.setSpeaker (spk);
				clean (d);
				std::vector<float> l, r;
				run (d, *x, *x, l, r);
				const double a = toneDb (l, n, fs, 18000) - toneDb (l, n, fs, 10000);
				worst = std::max (worst, a);
				CHECK (a < -100.0, "%s through %s, %s: 18 kHz at %.1f dBc", eraName (e), speakerName (spk),
				       x == &in ? "-6 dBFS" : "-1 dBFS", a);
			}
	std::printf ("  every era x every speaker, -6 and -1 dBFS: worst alias %.1f dBc\n", worst);

	// THE OVERSAMPLERS' DELAY IS READ BACK: at Mix 50 % the wet lines up
	// with the dry, so a tone does not comb. Uncompensated, the wet would
	// be 5 samples late (lathe) or 10.6 (lathe + speaker): -12.5 dB at
	// 5 kHz, and at 3 kHz for the second (10.6 samples is nearly a whole
	// 5 kHz period, which would pass by luck). Through the 1970s' chain.
	for (int spk : { kSpeakerOff, kSpeakerHiFi })
		for (double hz : { 3000.0, 5000.0 })
		{
			Dsp d;
			d.setSampleRate (fs);
			d.setEra (kEra1970);
			d.setSpeaker (spk);
			clean (d);
			d.setMixPct (50);
			d.snapParameters ();
			auto t = sine (fs, hz, 1.0, 0.25);
			std::vector<float> l, r;
			run (d, t, t, l, r);
			const double g = rmsDb (l, t.size () / 2) - rmsDb (t, t.size () / 2);
			std::printf ("  mix 50 %%, 1970s, speaker %s: %.0f kHz at %+.2f dB\n", speakerName (spk), hz / 1000, g);
			CHECK (std::fabs (g) < 1.0, "speaker %s: wet and dry line up at %.0f Hz (%+.2f dB)", speakerName (spk), hz, g);
		}
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
static void testPlayer ()
{
	std::printf ("player: the music the same, the surface Period-dull or Modern-bright\n");
	const double fs = 48000;

	// The music alone (no surface): Period and Modern bit-identical.
	auto in = sine (fs, 700, 2.0, 0.3);
	std::vector<float> l1, r1, l2, r2;
	for (int e : { kEra1920, kEra1970 })
	{
		Dsp a, b;
		for (Dsp* d : { &a, &b })
		{
			d->setSampleRate (fs);
			d->setEra (e);
			clean (*d);
		}
		a.setPlayer (kPlayerPeriod); b.setPlayer (kPlayerModern);
		a.snapParameters (); b.snapParameters ();
		run (a, in, in, l1, r1);
		run (b, in, in, l2, r2);
		CHECK (l1 == l2 && r1 == r2, "%s: without a surface the player changes nothing", eraName (e));
	}

	// The surface alone: its treble (first difference, ~above 5 kHz).
	auto treble = [] (const std::vector<float>& x) {
		double s = 0;
		for (size_t i = 1; i < x.size (); ++i) { const double d = double (x[i]) - x[i - 1]; s += d * d; }
		return 10.0 * std::log10 (s / x.size () + 1e-30);
	};
	std::vector<float> silence (static_cast<size_t> (fs * 4), 0.0f);
	auto surface = [&] (int era, int player) {
		Dsp d;
		d.setSampleRate (fs);
		d.setEra (era);
		d.setPlayer (player);
		d.setDustPct (60); d.setScratchesPct (40);
		d.snapParameters ();
		std::vector<float> l, r;
		run (d, silence, silence, l, r);
		return treble (l);
	};
	const double p20 = surface (kEra1920, kPlayerPeriod), m20 = surface (kEra1920, kPlayerModern);
	const double p70 = surface (kEra1970, kPlayerPeriod), m70 = surface (kEra1970, kPlayerModern);
	std::printf ("  surface treble: 1920s period %.1f, modern %.1f dB  |  1970s period %.1f, modern %.1f dB\n",
	             p20, m20, p70, m70);
	CHECK (m20 - p20 > 6.0, "a 78 on a modern deck crackles far brighter (%.1f dB)", m20 - p20);
	CHECK (std::fabs (m70 - p70) < 3.0, "negative control: a 1970s LP's own band is nearly modern (%.1f dB)", m70 - p70);

	// Switching the player fades rather than clicks.
	Dsp d;
	d.setSampleRate (fs);
	clean (d);
	auto s = sine (fs, 200, 0.5, 0.5);
	std::vector<float> l, r;
	run (d, s, s, l, r);
	d.setPlayer (kPlayerModern);
	run (d, s, s, l, r);
	double step = 0;
	for (size_t i = 1; i < l.size (); ++i)
		step = std::max (step, std::fabs (double (l[i]) - l[i - 1]));
	CHECK (step < 0.5 * 2 * kPi * 200 / fs * 1.5, "a player change: biggest step %.4f", step);
}

//------------------------------------------------------------------------
/** A sine's level through `d` (dB re its input, from a least-squares fit
    of the fundamental) and what is left over (THD + noise, % of it). */
static void sineThrough (Dsp& d, double fs, double hz, double amp, double& gainDb, double& thdPct)
{
	auto in = sine (fs, hz, 1.0, amp);
	std::vector<float> l, r;
	run (d, in, in, l, r);
	const size_t from = in.size () / 2;
	const size_t n = static_cast<size_t> (std::floor ((in.size () - from) * hz / fs) * fs / hz);
	double a = 0, b = 0, tot = 0;
	for (size_t i = from; i < from + n; ++i)
	{
		const double t = 2 * kPi * hz * i / fs;
		a += l[i] * std::sin (t); b += l[i] * std::cos (t); tot += double (l[i]) * l[i];
	}
	a *= 2.0 / n; b *= 2.0 / n;
	const double fund = (a * a + b * b) / 2;
	gainDb = 10 * std::log10 (fund / (amp * amp / 2) + 1e-30);
	thdPct = 100 * std::sqrt (std::max (0.0, tot / n - fund) / (fund + 1e-30));
}

static void testSpeaker ()
{
	std::printf ("speaker: off is nothing, each one its own band, level and mono-ness\n");
	const double fs = 48000;
	auto fresh = [fs] (Dsp& d, int era, int speaker) {
		d.setSampleRate (fs);
		d.setEra (era);
		d.setSpeaker (speaker);
		clean (d);
	};

	// OFF IS NO STAGE: bit-identical to a Dsp never told about speakers,
	// with every fault on.
	{
		auto in = sine (fs, 440, 2.0, 0.4);
		std::vector<float> l1, r1, l2, r2;
		Dsp a, b;
		for (Dsp* d : { &a, &b })
		{
			d->setSampleRate (fs);
			d->setScratchesPct (90); d->setDustPct (90); d->setWowPct (60); d->setWarpPct (40);
		}
		b.setSpeaker (kSpeakerOff);
		a.snapParameters (); b.snapParameters ();
		run (a, in, in, l1, r1);
		run (b, in, in, l2, r2);
		CHECK (l1 == l2 && r1 == r2, "speaker Off is bit-identical to before the switch");
		Dsp c;
		c.setSampleRate (fs);
		c.setScratchesPct (90); c.setDustPct (90); c.setWowPct (60); c.setWarpPct (40);
		c.setSpeaker (kSpeakerHiFi);
		c.snapParameters ();
		run (c, in, in, l2, r2);
		CHECK (l1 != l2, "negative control: even the near-flat hi-fi changes the samples");
	}

	// THE BANDS, on a 1970s record (its own band nearly flat).
	std::printf ("  response re 1 kHz (dB):     60 Hz  150 Hz  400 Hz  3 kHz  10 kHz   1 kHz level  make-up\n");
	double at150[kSpeakerCount] = {}, at10k[kSpeakerCount] = {}, at60[kSpeakerCount] = {};
	for (int spk = 0; spk < kSpeakerCount; ++spk)
	{
		double g1k, t, g[5];
		const double hz[5] = { 60, 150, 400, 3000, 10000 };
		Dsp d; fresh (d, kEra1970, spk); sineThrough (d, fs, 1000, 0.25, g1k, t);
		for (int i = 0; i < 5; ++i)
		{
			Dsp e; fresh (e, kEra1970, spk); sineThrough (e, fs, hz[i], 0.05, g[i], t);
			g[i] -= g1k;
		}
		at60[spk] = g[0]; at150[spk] = g[1]; at10k[spk] = g[4];
		std::printf ("  %-24s %6.1f  %6.1f  %6.1f  %5.1f  %6.1f   %+6.1f dB   %+5.1f\n", speakerName (spk), g[0], g[1],
		             g[2], g[3], g[4], g1k, d.speakerMakeupDb ());
		CHECK (std::fabs (g1k) < 4.5, "%s: 1 kHz within 4.5 dB of unity (%.1f)", speakerName (spk), g1k);
	}
	CHECK (at150[kSpeakerHorn] < -15 && at10k[kSpeakerHorn] < -20, "the horn: no bass, no treble");
	CHECK (at150[kSpeaker2Inch] < -15 && at10k[kSpeaker2Inch] < -20, "the 2-inch: no bass, no treble");
	CHECK (at150[kSpeaker2Inch] < at150[kSpeaker3Inch] && at150[kSpeaker3Inch] < at150[kSpeaker6Inch],
	       "bigger cones, more bass");
	CHECK (at60[kSpeaker6Inch] < at60[kSpeakerHiFi] - 6.0,
	       "the portable booms at 150 Hz, but only the hi-fi reaches 60");
	CHECK (at10k[kSpeaker2Inch] < at10k[kSpeaker3Inch] && at10k[kSpeaker3Inch] < at10k[kSpeaker6Inch],
	       "and more treble: the 1960s' tweeter");
	CHECK (std::fabs (at60[kSpeakerHiFi]) < 3 && std::fabs (at150[kSpeakerHiFi]) < 3 && std::fabs (at10k[kSpeakerHiFi]) < 3,
	       "the hi-fi: within 3 dB from 60 Hz to 10 kHz");
	CHECK (at60[kSpeaker6Inch] < -6, "negative control: a 6-inch portable has no 60 Hz");

	// THE CONE GIVES OUT: a loud 400 Hz.
	double g, thdSmall, thdHiFi, thdQuiet;
	{ Dsp d; fresh (d, kEra1970, kSpeaker2Inch); sineThrough (d, fs, 400, 0.9, g, thdSmall); }
	{ Dsp d; fresh (d, kEra1970, kSpeakerHiFi);  sineThrough (d, fs, 400, 0.9, g, thdHiFi); }
	{ Dsp d; fresh (d, kEra1970, kSpeaker2Inch); sineThrough (d, fs, 400, 0.02, g, thdQuiet); }
	std::printf ("  400 Hz THD+N: 2-inch loud %.2f %%, quiet %.2f %%  |  hi-fi loud %.2f %%\n", thdSmall, thdQuiet, thdHiFi);
	CHECK (thdSmall > 3.0 * thdHiFi, "pushed, the 2-inch distorts far more than the hi-fi");
	CHECK (thdQuiet < 0.2 * thdSmall, "negative control: played quietly, it barely does");

	// MONO: one cabinet sums. A left-only stereo record through each.
	{
		auto left = sine (fs, 500, 1.0, 0.3);
		std::vector<float> right (left.size (), 0.0f), l, r;
		for (int spk = kSpeakerHorn; spk < kSpeakerCount; ++spk)
		{
			Dsp d; fresh (d, kEra1970, spk);
			run (d, left, right, l, r);
			const bool same = std::memcmp (l.data (), r.data (), l.size () * sizeof (float)) == 0;
			if (speakerIsMono (spk))
				CHECK (same, "%s is one speaker: L == R", speakerName (spk));
			else
				CHECK (! same, "negative control: the hi-fi is a stereo pair");
		}
	}

	// THE SURFACE comes out of it too: a 78 on a modern deck, through a horn.
	{
		auto treble = [] (const std::vector<float>& x) {
			double s = 0;
			for (size_t i = 1; i < x.size (); ++i) { const double d = double (x[i]) - x[i - 1]; s += d * d; }
			return 10.0 * std::log10 (s / x.size () + 1e-30);
		};
		std::vector<float> silence (static_cast<size_t> (fs * 3), 0.0f), l, r;
		double t[2];
		for (int i = 0; i < 2; ++i)
		{
			Dsp d;
			d.setSampleRate (fs);
			d.setEra (kEra1920); d.setPlayer (kPlayerModern);
			d.setSpeaker (i ? kSpeakerHorn : kSpeakerOff);
			d.setDustPct (60); d.setScratchesPct (40);
			d.snapParameters ();
			run (d, silence, silence, l, r);
			t[i] = treble (l);
		}
		std::printf ("  modern-deck 78 surface treble: line out %.1f dB, horn %.1f dB\n", t[0], t[1]);
		CHECK (t[0] - t[1] > 6.0, "the crackle comes out of the horn dull (%.1f dB less treble)", t[0] - t[1]);
	}

	// A SPEAKER CHANGE fades rather than clicks.
	{
		Dsp d; fresh (d, kEra1960, kSpeakerOff);
		auto s = sine (fs, 200, 0.5, 0.5);
		std::vector<float> l, r;
		run (d, s, s, l, r);
		double step = 0;
		for (int spk : { kSpeakerHorn, kSpeakerHiFi, kSpeaker2Inch, kSpeakerOff })
		{
			d.setSpeaker (spk);
			run (d, s, s, l, r);
			for (size_t i = 1; i < l.size (); ++i)
				step = std::max (step, std::fabs (double (l[i]) - l[i - 1]));
			CHECK (d.activeSpeaker () == spk, "the chain swapped to %s", speakerName (spk));
		}
		const double bound = 0.5 * 2 * kPi * 200 / fs * 3.0;
		CHECK (step < bound, "speaker changes: biggest step %.4f < %.4f", step, bound);
	}

	// EVERY RATE: finite, about the same level, the horn's echo in range.
	for (double rate : { 44100.0, 96000.0, 192000.0 })
	{
		Dsp d;
		d.setSampleRate (rate);
		d.setEra (kEra1970); d.setSpeaker (kSpeakerHorn);
		clean (d);
		auto in = sine (rate, 1000, 1.0, 0.25);
		std::vector<float> l, r;
		run (d, in, in, l, r);
		bool finite = true;
		for (float v : l) finite = finite && std::isfinite (v);
		const double gain = rmsDb (l, in.size () / 2) - rmsDb (in, in.size () / 2);
		CHECK (finite && std::fabs (gain) < 4.5, "the horn at %.0f Hz: finite, %.1f dB", rate, gain);
	}

	// Determinism: reset empties the cabinet's echo too.
	{
		Dsp d; fresh (d, kEra1920, kSpeakerHorn);
		d.setDustPct (50); d.snapParameters ();
		auto in = sine (fs, 300, 1.0, 0.5);
		std::vector<float> l1, r1, l2, r2;
		run (d, in, in, l1, r1);
		d.reset ();
		run (d, in, in, l2, r2);
		CHECK (l1 == l2, "reset clears the horn: the same bounce twice");
	}
}

//------------------------------------------------------------------------
/** Run with a transport: playing or not, from a song position. */
static void runTransport (Dsp& d, const std::vector<float>& in, std::vector<float>& out, bool playing,
                          int64_t& position, int block = 256)
{
	out.resize (in.size ());
	std::vector<float> r (in.size ());
	for (size_t i = 0; i < in.size (); i += block)
	{
		const int n = static_cast<int> (std::min<size_t> (block, in.size () - i));
		d.setTransport (true, playing, position, n);
		d.process (in.data () + i, in.data () + i, out.data () + i, r.data () + i, n);
		if (playing)
			position += n;
	}
}

static void testTransport ()
{
	std::printf ("transport: the record turns while the song plays\n");
	const double fs = 48000;
	std::vector<float> silence (static_cast<size_t> (fs), 0.0f), out;
	auto busy = [&] (Dsp& d) {
		d.setSampleRate (fs);
		d.setEra (kEra1920);
		d.setPlayer (kPlayerModern);
		d.setDustPct (100); d.setScratchesPct (100); d.setWowPct (50);
		d.snapParameters ();
	};

	// Stopped: the surface goes and the disc stands still.
	{
		Dsp d;
		busy (d);
		int64_t pos = 0;
		runTransport (d, silence, out, true, pos);
		const double playingDb = rmsDb (out, 24000);
		const double angle = d.discAngle ();
		runTransport (d, silence, out, false, pos);
		const double stoppedDb = rmsDb (out, 9600);   // from 0.2 s after the stop
		std::printf ("  surface: playing %.1f dBFS RMS, stopped %.1f dBFS\n", playingDb, stoppedDb);
		CHECK (playingDb > -60.0, "negative control: the surface is there while playing");
		CHECK (stoppedDb < -120.0, "silent underneath when stopped");
		CHECK (d.discAngle () == angle && ! d.turning (), "the disc stands still");
		runTransport (d, silence, out, true, pos);
		CHECK (rmsDb (out, 24000) > -60.0 && d.turning (), "and starts again with the song");
	}

	// The music still goes through while stopped (monitoring).
	{
		Dsp d;
		d.setSampleRate (fs);
		clean (d);
		auto in = sine (fs, 1000, 1.0, 0.25);
		int64_t pos = 0;
		runTransport (d, in, out, false, pos);
		CHECK (std::fabs (rmsDb (out, 4800) - rmsDb (in, 4800)) < 1.0, "a stopped transport still passes the music");
	}

	// The same song position plays the same - whatever came before.
	auto from = [&] (int64_t start, double warmup) {
		Dsp d;
		busy (d);
		int64_t pos = 77777;
		std::vector<float> w (static_cast<size_t> (fs * warmup), 0.0f), o;
		runTransport (d, w, o, true, pos);       // some other passage first
		runTransport (d, w, o, false, pos);      // stop
		pos = start;
		runTransport (d, silence, o, true, pos); // play from `start`
		return o;
	};
	const auto a = from (480000, 0.5), b = from (480000, 1.3), c = from (500000, 0.5);
	CHECK (a == b, "the same passage gets the same scratches and crackle every time");
	CHECK (a != c, "negative control: another passage does not");

	// A loop: the song jumps back, the disc follows.
	{
		Dsp d;
		busy (d);
		int64_t pos = 480000;
		std::vector<float> first, second;
		runTransport (d, silence, first, true, pos);
		pos = 480000;                            // the host loops, still playing
		runTransport (d, silence, second, true, pos);
		CHECK (first == second, "a loop replays its passage exactly");
	}

	// Stopping with the needle stuck lets it go.
	{
		Dsp d;
		busy (d);
		d.setScratchesPct (100);
		d.snapParameters ();
		int64_t pos = 0;
		std::vector<float> chunk (480, 0.0f);
		int guard = 0;
		while (! d.skipping () && guard++ < 20000)
			runTransport (d, chunk, out, true, pos, 480);
		CHECK (d.skipping (), "the needle stuck at 100 %%");
		runTransport (d, silence, out, false, pos);
		CHECK (! d.skipping (), "and let go when the transport stopped");
	}

	// No ProcessContext at all: it turns, as before.
	{
		Dsp d;
		busy (d);
		d.setTransport (false, false, 0, 256);
		CHECK (d.turning (), "a host that does not say: always turning");
	}
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
/** Music, then silence with the record stopped and Dust and Warp turned
    down: every filter, oversampler, echo and smoother heads for zero.
    Under the guard (as the processor runs it) none of it may be left
    denormal; without the guard the same run must leave some - or this
    test cannot tell a guard that works from one that does nothing. */
static int denormalsAfterSilence (bool guard)
{
	const double fs = 48000;
	Dsp d;
	d.setSampleRate (fs);
	clean (d);
	d.setSpeaker (kSpeakerHorn);   // the most state: the oversampled cone, the horn's echo
	d.setDustPct (30); d.setWarpPct (40);
	d.snapParameters ();

	const int block = 512;
	std::vector<float> in (block), l (block), r (block);
	uint32_t seed = 1;
	auto go = [&] (bool loud, bool playing) {
		for (auto& x : in)
		{
			seed = seed * 1664525u + 1013904223u;
			x = loud ? static_cast<float> (static_cast<int32_t> (seed) >> 8) / 8388608.0f * 0.5f : 0.0f;
		}
		d.setTransport (true, playing, 0, block);
		if (guard)
		{
			ScopedFlushDenormals noDenormals;
			d.process (in.data (), in.data (), l.data (), r.data (), block);
		}
		else
			d.process (in.data (), in.data (), l.data (), r.data (), block);
	};
	for (int b = 0; b < 94; ++b) go (true, true);          // 1 s of music
	d.setDustPct (0); d.setWarpPct (0);
	for (int b = 0; b < 94 * 30; ++b) go (false, false);   // 30 s stopped, silent
	return d.denormalStateCount ();
}

static void testDenormals ()
{
	const int with = denormalsAfterSilence (true);
	CHECK (with == 0, "with the guard: %d denormal state values after silence", with);

	if (ScopedFlushDenormals::supported ())
	{
		const int without = denormalsAfterSilence (false);
		CHECK (without > 0, "negative control: without the guard %d denormal (must be > 0)", without);
	}
	else
		std::printf ("  (no flush-to-zero on this architecture: the guard does nothing here)\n");

	// The host's mode comes back: outside the guard a denormal survives.
	{ ScopedFlushDenormals noDenormals; }
	volatile double tiny = 2.2250738585072014e-308;
	const double half = tiny * 0.5;
	CHECK (half != 0.0, "the guard restores the host's FPU mode on exit");
}

//------------------------------------------------------------------------
/** A smoother aimed at 0 must GET there. Turning Dust down to 0 and then
    playing a passage must be the same as playing it with Dust loaded at 0
    - the crackle draw's random numbers included, so the needle jumps in
    the same places. Silent music, so only the surface and the jumps'
    thumps are heard. */
static std::vector<float> passage (Dsp& d, double fs, double seconds, int64_t from)
{
	const int block = 512;
	std::vector<float> in (block, 0.0f), l (block), r (block), out;
	int64_t pos = from;
	const auto n = static_cast<int64_t> (fs * seconds) / block;
	for (int64_t b = 0; b < n; ++b, pos += block)
	{
		d.setTransport (true, true, pos, block);
		d.process (in.data (), in.data (), l.data (), r.data (), block);
		out.insert (out.end (), l.begin (), l.end ());
	}
	return out;
}

static void testSmoothersArrive ()
{
	const double fs = 48000;
	auto make = [fs] (Dsp& d, double dust) {
		d.setSampleRate (fs);
		clean (d);
		d.setScratchesPct (100);   // ticks and jumps: both drawn from the random numbers
		d.setDustPct (dust);
		d.snapParameters ();
	};

	Dsp swept; make (swept, 30);
	swept.setDustPct (0);
	passage (swept, fs, 30, static_cast<int64_t> (fs * 100));   // somewhere else, long enough to arrive
	const auto a = passage (swept, fs, 20, 0);                    // then locate to the start

	Dsp loaded; make (loaded, 0);
	const auto b = passage (loaded, fs, 20, 0);

	Dsp control; make (control, 0.001);   // a real, tiny Dust: the draws go on
	const auto c = passage (control, fs, 20, 0);

	const bool same = a.size () == b.size () && std::memcmp (a.data (), b.data (), a.size () * sizeof (float)) == 0;
	const bool differs = c.size () != b.size () || std::memcmp (c.data (), b.data (), c.size () * sizeof (float)) != 0;
	CHECK (same, "Dust turned down to 0 plays the passage as Dust loaded at 0, bit for bit");
	CHECK (differs, "negative control: Dust at 0.001 %% must play it differently");
}

//------------------------------------------------------------------------
static void testCost ()
{
	const double fs = 48000;
	Dsp d;
	d.setSampleRate (fs);
	d.setScratchesPct (100); d.setDustPct (100); d.setWowPct (100); d.setFlutterPct (100); d.setWarpPct (100);
	d.setSpeaker (kSpeakerHiFi);   // the stereo pair: two speaker chains
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
	testMeasuredLatency ();
	testLevels ();
	testAliasing ();
	testWorstCase ();
	testPitch ();
	testJumps ();
	testSpeed ();
	testPlayer ();
	testSpeaker ();
	testTransport ();
	testMonoAndDeterminism ();
	testSmoothingAndEdges ();
	testDenormals ();
	testSmoothersArrive ();
	testCost ();
	std::printf ("\nDSP: %d checks, %d failed\n", gChecks, gFails);
	return gFails ? 1 : 0;
}
