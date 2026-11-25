//------------------------------------------------------------------------
// WornVinyl - the audio line, implementation
//------------------------------------------------------------------------

#include "WornVinylDsp.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

namespace WornVinyl {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

/** Butterworth 4-pole as two biquads. */
constexpr double kButterQ1 = 0.54119610014619698;
constexpr double kButterQ2 = 1.30656296487637652;

/** The warp's bump: one high spot a turn, this wide (a fraction of a turn,
    Gaussian), and where it is round the disc. */
constexpr double kWarpSigma = 0.12;
constexpr double kWarpPhase = 0.25;
constexpr double kWarpBumpMean = 0.21269446210866192;   // sigma * sqrt(pi)
constexpr double kWarpSlopePeak = 0.85776388496070679;  // max |d/dx exp(-(x/s)^2)| * s = sqrt(2/e)

/** The motor's slow drift (part of Wow) and the two flutter rates. */
constexpr double kDriftHz = 0.13;
constexpr double kFlutterHz1 = 7.3;
constexpr double kFlutterHz2 = 11.1;
constexpr double kFlutterWobbleHz = 0.7;

constexpr uint64_t kSeed = 0x5EED0F5A11C0FFEEull;
} // namespace

//------------------------------------------------------------------------
// Names, profiles, shared rules
//------------------------------------------------------------------------
const char* eraName (int era)
{
	static const char* const names[kEraCount] = { "1920s", "1940s", "1950s", "1960s", "1970s" };
	return (era >= 0 && era < kEraCount) ? names[era] : "";
}

const char* discName (int disc)
{
	return disc == kDiscShellac ? "Shellac" : (disc == kDiscVinyl ? "Vinyl" : "");
}

const char* playerName (int player)
{
	return player == kPlayerPeriod ? "Period" : (player == kPlayerModern ? "Modern" : "");
}

const Profile& playerProfile (int era, int disc, int player)
{
	// A modern turntable and phono stage: flat but for the edges.
	static const Profile kModern { 20.0, 20000.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
	return player == kPlayerModern ? kModern : profileOf (era, disc);
}

//------------------------------------------------------------------------
// The speakers
//------------------------------------------------------------------------
const char* speakerName (int speaker)
{
	static const char* const names[kSpeakerCount] = {
		"Off", "1920s Horn", "1940s 2-inch", "1950s 3-inch", "1960s 6-inch + Tweeter", "1970s 3-Way"
	};
	return (speaker >= 0 && speaker < kSpeakerCount) ? names[speaker] : "";
}

const char* speakerDialName (int speaker)
{
	static const char* const names[kSpeakerCount] = { "OFF", "HORN", "2 IN", "3 IN", "6 IN", "3-WAY" };
	return (speaker >= 0 && speaker < kSpeakerCount) ? names[speaker] : "";
}

const char* speakerDescription (int speaker)
{
	static const char* const names[kSpeakerCount] = {
		"", "HORN GRAMOPHONE", "2-INCH SPEAKER", "3-INCH SPEAKER", "6-INCH + TWEETER", "HI-FI 3-WAY"
	};
	return (speaker >= 0 && speaker < kSpeakerCount) ? names[speaker] : "";
}

const SpeakerProfile& speakerProfile (int speaker)
{
	// hp1 Hz, Q   hp2 Hz, Q   lp1 Hz, Q   lp2 Hz, Q
	// peaks: Hz x4, dB x4, Q x4
	// cone Hz, k   amp drive   cabinet / horn: ms, feedback, loop low-pass Hz

	// 1920s: a mica soundbox on a horn. The horn will not load below its
	// cut-off (two steep high-passes), the soundbox rolls off past 4 kHz;
	// the diaphragm rings at 1.5 and 2.8 kHz, the horn's throat at 700 Hz,
	// and the sound runs back and forth down 55 cm of horn - the hollow,
	// "down a tube" colour (a 3.2 ms echo, 310 Hz apart).
	static const SpeakerProfile kHorn {
		280.0, 1.2, 200.0, 0.707,   4200.0, 0.9, 5000.0, 0.707,
		{ 700.0, 1500.0, 2800.0, 450.0 }, { 4.0, 5.0, 4.0, -3.0 }, { 2.5, 3.0, 3.0, 2.0 },
		500.0, 1.2,   0.15,   3.2, 0.38, 2000.0 };

	// 1940s: a 2-inch cone in a radio-gram's little box, a single valve.
	// Resonance at ~420 Hz and nothing under it, a dip at 1.2 kHz and the
	// cone breaking up at 2.9 and 4.2 kHz - shouty; the cone and the
	// valve both give out when pushed. A small box's ring (0.6 ms).
	static const SpeakerProfile k2Inch {
		420.0, 1.6, 300.0, 0.707,   5000.0, 0.707, 6000.0, 1.0,
		{ 1200.0, 2900.0, 4200.0, 0.0 }, { -2.0, 6.0, 3.0, 0.0 }, { 1.5, 2.5, 3.0, 1.0 },
		600.0, 2.2,   0.35,   0.6, 0.18, 3000.0 };

	// 1950s: a 3-inch cone in a portable's wooden case: a little lower,
	// a little smoother, the case thumping softly at 180 Hz.
	static const SpeakerProfile k3Inch {
		300.0, 1.3, 200.0, 0.707,   6500.0, 0.707, 7500.0, 0.9,
		{ 2400.0, 900.0, 4500.0, 180.0 }, { 4.0, -1.5, 2.0, 2.0 }, { 2.0, 1.2, 3.0, 2.0 },
		450.0, 1.6,   0.25,   0.8, 0.15, 3500.0 };

	// 1960s: a 6-inch cone and a small tweeter in an open-backed portable.
	// Bass from ~110 Hz, the case booming at 150 Hz, the open back
	// cancelling at 450 Hz, the cone's presence at 1.8 kHz and the little
	// tweeter's sparkle at 6.5 kHz.
	static const SpeakerProfile k6Inch {
		110.0, 1.1, 70.0, 0.707,    12000.0, 0.707, 0.0, 0.707,
		{ 150.0, 450.0, 1800.0, 6500.0 }, { 2.5, -2.0, 2.5, 3.5 }, { 1.5, 1.2, 1.4, 1.8 },
		200.0, 1.2,   0.18,   1.5, 0.12, 2500.0 };

	// 1970s: an 8-inch bass, a mid and a dome tweeter in a sealed box.
	// 48 Hz - 19 kHz, a little bass warmth, the crossover's dip at 3 kHz,
	// the tweeter a touch forward. The woofer barely compresses.
	static const SpeakerProfile kHiFi {
		48.0, 0.9, 0.0, 0.707,      19000.0, 0.707, 0.0, 0.707,
		{ 65.0, 700.0, 3000.0, 10000.0 }, { 1.5, 0.8, -1.5, 1.5 }, { 1.2, 1.0, 2.0, 1.2 },
		80.0, 0.6,   0.05,   0.0, 0.0, 0.0 };

	switch (speaker)
	{
		case kSpeakerHorn:  return kHorn;
		case kSpeaker2Inch: return k2Inch;
		case kSpeaker3Inch: return k3Inch;
		case kSpeaker6Inch: return k6Inch;
		default:            return kHiFi;
	}
}

const char* speedName (int speed)
{
	static const char* const names[kSpeedCount] = { "Auto", "33", "45", "78" };
	return (speed >= 0 && speed < kSpeedCount) ? names[speed] : "";
}

const Profile& profileOf (int era, int disc)
{
	//                     hp     lp      peak 1             peak 2             width drive hiss  rumble crackle/s crackle scratch
	static const Profile k1920    { 180.0, 3200.0,  1000.0, 6.0, 1.6,  2300.0, 4.0, 3.0,  0.0, 0.35, -34.0, -42.0, 420.0, -12.0,  -9.0 };
	static const Profile k1940    {  70.0, 5500.0,  2500.0, 3.0, 1.0,     0.0, 0.0, 1.0,  0.0, 0.25, -40.0, -46.0, 380.0, -13.0, -10.0 };
	static const Profile k1950Sh  {  60.0, 7000.0,  3000.0, 2.0, 1.0,     0.0, 0.0, 1.0,  0.0, 0.20, -42.0, -48.0, 360.0, -13.0, -10.0 };
	static const Profile k1950Vi  {  40.0, 11000.0, 3000.0, 1.5, 0.8,     0.0, 0.0, 1.0,  0.0, 0.15, -54.0, -52.0, 260.0, -16.0, -13.0 };
	static const Profile k1960    {  28.0, 15000.0,    0.0, 0.0, 1.0,     0.0, 0.0, 1.0, 0.85, 0.12, -58.0, -56.0, 240.0, -17.0, -14.0 };
	static const Profile k1970    {  20.0, 18500.0,    0.0, 0.0, 1.0,     0.0, 0.0, 1.0,  1.0, 0.08, -62.0, -60.0, 220.0, -18.0, -15.0 };

	switch (era)
	{
		case kEra1920: return k1920;
		case kEra1940: return k1940;
		case kEra1950: return effectiveDisc (era, disc) == kDiscShellac ? k1950Sh : k1950Vi;
		case kEra1970: return k1970;
		default:       return k1960;
	}
}

std::string bandwidthText (int era, int disc)
{
	const Profile& p = profileOf (era, disc);
	char buf[64];
	const double lpK = p.lpHz / 1000.0;
	if (std::fabs (lpK - std::round (lpK)) < 0.05)
		std::snprintf (buf, sizeof buf, "%.0f Hz - %.0f kHz", p.hpHz, lpK);
	else
		std::snprintf (buf, sizeof buf, "%.0f Hz - %.1f kHz", p.hpHz, lpK);
	return buf;
}

ScratchSlot scratchSlot (int index)
{
	// Fixed, so the screen and the stylus agree, and a project sounds the
	// same every time it opens. Slot 0 is the deepest.
	static const ScratchSlot kSlots[kMaxScratches] = {
		{ 0.07, 1.00 }, { 0.31, 0.60 }, { 0.55, 0.80 }, { 0.83, 0.45 },
		{ 0.19, 0.70 }, { 0.66, 0.35 }, { 0.43, 0.55 }, { 0.92, 0.30 },
		{ 0.12, 0.50 }, { 0.74, 0.40 }, { 0.37, 0.25 }, { 0.61, 0.65 },
	};
	if (index < 0 || index >= kMaxScratches)
		return { 0.0, 0.0 };
	return kSlots[index];
}

int activeScratches (double scratchesPct)
{
	if (scratchesPct <= 0.0)
		return 0;
	const int n = 1 + static_cast<int> (scratchesPct / 100.0 * (kMaxScratches - 1) + 1e-9);
	return std::min (n, kMaxScratches);
}

double crackleRate (int era, int disc, double dustPct)
{
	const double d = std::clamp (dustPct, 0.0, 100.0) / 100.0;
	return profileOf (era, disc).crackleRate * d * d;
}

double jumpChancePerSecond (double scratchesPct, bool jumpsOn)
{
	if (! jumpsOn || scratchesPct <= kJumpThresholdPct)
		return 0.0;
	const double x = std::min (1.0, (scratchesPct - kJumpThresholdPct) / (100.0 - kJumpThresholdPct));
	return x * x / 6.0;   // one every six seconds or so at 100 %
}

double warpBump (double discAngle)
{
	double dist = discAngle - kWarpPhase;
	dist -= std::floor (dist + 0.5);
	return std::exp (-(dist / kWarpSigma) * (dist / kWarpSigma));
}

double dbToGain (double db)
{
	return std::pow (10.0, db / 20.0);
}

//------------------------------------------------------------------------
// Biquad (RBJ cookbook)
//------------------------------------------------------------------------
void Biquad::lowPass (double fs, double hz, double q)
{
	const double w = kTwoPi * hz / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
	const double a0 = 1.0 + alpha;
	b0 = (1.0 - c) / 2.0 / a0; b1 = (1.0 - c) / a0; b2 = b0;
	a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
}

void Biquad::highPass (double fs, double hz, double q)
{
	const double w = kTwoPi * hz / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
	const double a0 = 1.0 + alpha;
	b0 = (1.0 + c) / 2.0 / a0; b1 = -(1.0 + c) / a0; b2 = b0;
	a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
}

void Biquad::peak (double fs, double hz, double db, double q)
{
	if (db == 0.0 || hz <= 0.0)
	{
		setIdentity ();
		return;
	}
	const double A = std::pow (10.0, db / 40.0);
	const double w = kTwoPi * hz / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
	const double a0 = 1.0 + alpha / A;
	b0 = (1.0 + alpha * A) / a0; b1 = -2.0 * c / a0; b2 = (1.0 - alpha * A) / a0;
	a1 = -2.0 * c / a0; a2 = (1.0 - alpha / A) / a0;
}

void Biquad::bandPass (double fs, double hz, double q)
{
	const double w = kTwoPi * hz / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
	const double a0 = 1.0 + alpha;
	b0 = alpha / a0; b1 = 0.0; b2 = -alpha / a0;
	a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
}

double Biquad::magnitude (double fs, double hz) const
{
	const std::complex<double> z1i = std::polar (1.0, -kTwoPi * hz / fs);
	const std::complex<double> z2i = z1i * z1i;
	return std::abs ((b0 + b1 * z1i + b2 * z2i) / (1.0 + a1 * z1i + a2 * z2i));
}

//------------------------------------------------------------------------
// HalfBand - hiir's polyphase IIR half-band (Laurent de Soras, WTFPL):
// the coefficients from elliptic-function series, then two allpass chains.
//------------------------------------------------------------------------
namespace {
double ipow (double x, int n)
{
	double r = 1.0;
	while (n-- > 0)
		r *= x;
	return r;
}

double accNum (double q, int order, int c)
{
	double acc = 0.0, term = 0.0;
	int sign = 1;
	for (int i = 0; i < 64; ++i, sign = -sign)
	{
		term = ipow (q, i * (i + 1)) * std::sin ((i * 2 + 1) * c * kPi / order) * sign;
		acc += term;
		if (std::fabs (term) <= 1e-100)
			break;
	}
	return acc;
}

double accDen (double q, int order, int c)
{
	double acc = 0.0, term = 0.0;
	int sign = -1;
	for (int i = 1; i < 64; ++i, sign = -sign)
	{
		term = ipow (q, i * i) * std::cos (i * 2 * c * kPi / order) * sign;
		acc += term;
		if (std::fabs (term) <= 1e-100)
			break;
	}
	return acc;
}
} // namespace

void HalfBand::design (double attenuationDb, double transition)
{
	double k = std::tan ((1.0 - transition * 2.0) * kPi / 4.0);
	k *= k;
	const double kk = std::pow (1.0 - k * k, 0.25);
	const double e = 0.5 * (1.0 - kk) / (1.0 + kk);
	const double e4 = e * e * e * e;
	const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));

	const double ap = std::pow (10.0, -attenuationDb / 10.0);
	const double a = ap / (1.0 - ap);
	int order = static_cast<int> (std::ceil (std::log (a * a / 16.0) / std::log (q)));
	if ((order & 1) == 0)
		++order;
	order = std::clamp (order, 3, 2 * kMaxCoefs + 1);
	n = (order - 1) / 2;

	for (int i = 0; i < n; ++i)
	{
		const int cc = i + 1;
		const double ww = accNum (q, order, cc) * std::pow (q, 0.25) / (accDen (q, order, cc) + 0.5);
		const double w2 = ww * ww;
		const double xx = std::sqrt ((1.0 - w2 * k) * (1.0 - w2 / k)) / (1.0 + w2);
		c[i] = (1.0 - xx) / (1.0 + xx);
	}
	clear ();
}

void HalfBand::clear ()
{
	for (int i = 0; i < kMaxCoefs; ++i)
		x[i] = y[i] = 0.0;
}

void HalfBand::paths (double& even, double& odd)
{
	// Coefficients 0, 2, 4 ... on one path, 1, 3, 5 ... on the other; each
	// a first-order allpass in z^2, run at the low rate.
	for (int i = 0; i < n; i += 2)
	{
		const double t0 = (even - y[i]) * c[i] + x[i];
		x[i] = even; y[i] = t0; even = t0;
		if (i + 1 < n)
		{
			const double t1 = (odd - y[i + 1]) * c[i + 1] + x[i + 1];
			x[i + 1] = odd; y[i + 1] = t1; odd = t1;
		}
	}
}

void HalfBand::up (double in, double& out0, double& out1)
{
	double even = in, odd = in;
	paths (even, odd);
	out0 = even;
	out1 = odd;
}

double HalfBand::down (double in0, double in1)
{
	double even = in1, odd = in0;
	paths (even, odd);
	return 0.5 * (even + odd);
}

void Oversampler4x::design ()
{
	// 2x stage: flat to 0.21 of 2fs (20 kHz at 48 k), 100 dB down beyond.
	// 4x stage: the signal is already under a quarter of ITS rate, so a
	// wide transition does, with far fewer coefficients.
	up1.design (100.0, 0.04);
	down1.design (100.0, 0.04);
	up2.design (100.0, 0.12);
	down2.design (100.0, 0.12);
}

void Oversampler4x::clear ()
{
	up1.clear (); up2.clear (); down2.clear (); down1.clear ();
}

double Oversampler4x::delaySamples ()
{
	static const double kDelay = [] {
		Oversampler4x o;
		o.design ();
		double sum = 0.0, moment = 0.0;
		for (int i = 0; i < 4096; ++i)
		{
			const double h = o.process (i == 0 ? 1.0 : 0.0, [] (double v) { return v; });
			sum += h;
			moment += h * i;
		}
		return moment / sum;
	} ();
	return kDelay;
}

//------------------------------------------------------------------------
void EraEq::design (const Profile& p, double fs)
{
	const double lp = std::min (p.lpHz, 0.45 * fs);
	hp.highPass (fs, p.hpHz, 0.7071067811865476);
	lp1.lowPass (fs, lp, kButterQ1);
	lp2.lowPass (fs, lp, kButterQ2);
	pk1.peak (fs, std::min (p.peak1Hz, 0.4 * fs), p.peak1Db, p.peak1Q);
	pk2.peak (fs, std::min (p.peak2Hz, 0.4 * fs), p.peak2Db, p.peak2Q);
}

void EraEq::clear ()
{
	hp.clear (); lp1.clear (); lp2.clear (); pk1.clear (); pk2.clear ();
}

double EraEq::magnitude (double fs, double hz) const
{
	return hp.magnitude (fs, hz) * lp1.magnitude (fs, hz) * lp2.magnitude (fs, hz) *
	       pk1.magnitude (fs, hz) * pk2.magnitude (fs, hz);
}

//------------------------------------------------------------------------
// SpeakerChain
//------------------------------------------------------------------------
void SpeakerChain::allocate (double fs)
{
	uint32_t size = 1;
	while (size < static_cast<uint32_t> (kMaxSpeakerCombMs * 0.001 * fs) + 4)
		size <<= 1;
	comb.assign (size, 0.0);
	combMask = size - 1;
	combCapacity = static_cast<int> (size) - 2;
	combWrite = 0;
}

void SpeakerChain::design (const SpeakerProfile& p, double fs, double makeupDb)
{
	auto hp = [fs] (Biquad& b, double hz, double q) {
		if (hz > 0.0) b.highPass (fs, hz, q); else b.setIdentity ();
	};
	auto lp = [fs] (Biquad& b, double hz, double q) {
		if (hz > 0.0 && hz < 0.45 * fs) b.lowPass (fs, hz, q); else b.setIdentity ();
	};
	hp (hp1, p.hp1Hz, p.hp1Q);
	hp (hp2, p.hp2Hz, p.hp2Q);
	lp (lp1, p.lp1Hz, p.lp1Q);
	lp (lp2, p.lp2Hz, p.lp2Q);
	for (int i = 0; i < 4; ++i)
		pk[i].peak (fs, std::min (p.pkHz[i], 0.4 * fs), p.pkDb[i], p.pkQ[i]);

	ampDrive = p.ampDrive;
	coneK = p.coneK;
	coneCoef = p.coneHz > 0.0 ? 1.0 - std::exp (-kTwoPi * p.coneHz / (4.0 * fs)) : 0.0;   // runs at 4x
	os.design ();

	// The echo, rounded to whole samples (a few cents off the horn's
	// length at 44.1 k - inaudible), never longer than the buffer.
	combDelay = std::min (combCapacity, static_cast<int> (std::lround (p.combMs * 0.001 * fs)));
	combFeedback = combDelay > 0 ? p.combFeedback : 0.0;
	combCoef = p.combLpHz > 0.0 ? 1.0 - std::exp (-kTwoPi * std::min (p.combLpHz, 0.45 * fs) / fs) : 1.0;

	makeup = dbToGain (makeupDb);
}

void SpeakerChain::clear ()
{
	hp1.clear (); hp2.clear (); lp1.clear (); lp2.clear ();
	for (auto& b : pk)
		b.clear ();
	coneLp = 0.0;
	os.clear ();
	combLp = 0.0;
	std::fill (comb.begin (), comb.end (), 0.0);
	combWrite = 0;
}

double SpeakerChain::process (double x)
{
	// The amplifier (a soft clip, the lathe's curve) and the cone (the band
	// under its resonance moves it furthest, and gives out first): the
	// two curves, at 4x the rate so their harmonics do not alias.
	x = os.process (x, [this] (double v) {
		v = latheCurve (v, ampDrive);
		if (coneK > 0.0)
		{
			coneLp += coneCoef * (v - coneLp);
			v += std::tanh (coneK * coneLp) / coneK - coneLp;
		}
		return v;
	});

	// The cone's (or the horn's) response.
	x = lp2.process (lp1.process (hp2.process (hp1.process (x))));
	for (auto& b : pk)
		x = b.process (x);

	// The cabinet, or the horn's length: an echo fed back, duller each
	// time round.
	if (combFeedback > 0.0)
	{
		combLp += combCoef * (comb[(combWrite - static_cast<uint32_t> (combDelay)) & combMask] - combLp);
		x += combFeedback * combLp;
		comb[combWrite & combMask] = x;
		++combWrite;
	}
	return x * makeup;
}

double SpeakerChain::magnitude (double fs, double hz) const
{
	double m = hp1.magnitude (fs, hz) * hp2.magnitude (fs, hz) * lp1.magnitude (fs, hz) * lp2.magnitude (fs, hz);
	for (const auto& b : pk)
		m *= b.magnitude (fs, hz);
	if (combFeedback > 0.0)
	{
		// y = x + g L(z) z^-D y, L the one-pole low-pass in the loop.
		const std::complex<double> zi = std::polar (1.0, -kTwoPi * hz / fs);
		const std::complex<double> L = combCoef / (1.0 - (1.0 - combCoef) * zi);
		const std::complex<double> zD = std::polar (1.0, -kTwoPi * hz / fs * combDelay);
		m *= 1.0 / std::abs (1.0 - combFeedback * L * zD);
	}
	return m;
}

//------------------------------------------------------------------------
// Dsp
//------------------------------------------------------------------------
Dsp::Dsp ()
{
	mScratches.target = kScratchesDefault;
	mDust.target = kDustDefault;
	mWow.target = kWowDefault;
	mFlutter.target = kFlutterDefault;
	mWarp.target = kWarpDefault;
	mMix.target = kMixDefault / 100.0;
	mOutput.target = dbToGain (kOutputDefaultDb);
	mBypass.target = 0.0;
	mSurface.target = 1.0;
	setSampleRate (44100.0);
	snapParameters ();
}

//------------------------------------------------------------------------
uint64_t Dsp::nextBits ()
{
	uint64_t z = (mSeed += 0x9E3779B97F4A7C15ull);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

double Dsp::uniform ()
{
	return static_cast<double> (nextBits () >> 11) * (1.0 / 9007199254740992.0);
}

double Dsp::bipolar ()
{
	return 2.0 * uniform () - 1.0;
}

//------------------------------------------------------------------------
void Dsp::setSampleRate (double fs)
{
	mFs = (fs > 1000.0) ? fs : 44100.0;

	// The ring buffer: the deepest stick (kMaxStick turns at 33 1/3, the
	// slowest the Speed switch goes) plus
	// the resting delay and its swing, rounded up to a power of two.
	const double seconds = kMaxStick * 60.0 / rpmOf (kDiscVinyl) + 4.0 * kBaseDelaySeconds + 0.05;
	uint32_t size = 1;
	while (size < static_cast<uint32_t> (seconds * mFs) + 16)
		size <<= 1;
	mBufL.assign (size, 0.0f);
	mBufR.assign (size, 0.0f);
	mMask = size - 1;

	mBaseDelay = static_cast<int> (std::ceil (kBaseDelaySeconds * mFs));
	mSmoothK = 1.0 - std::exp (-1.0 / (0.02 * mFs));         // 20 ms
	mXfadeLen = std::max (1, static_cast<int> (0.006 * mFs)); // 6 ms

	mWarpLpCoef = 1.0 - std::exp (-kTwoPi * 3000.0 / mFs);

	mSpkL.allocate (mFs);
	mSpkR.allocate (mFs);
	mLatheOsL.design ();
	mLatheOsR.design ();
	mOsDelay = Oversampler4x::delaySamples ();

	designProfile ();
	reset ();
}

//------------------------------------------------------------------------
double Dsp::profileMakeupDb (const Profile& p) const
{
	// The mean power of the chain over the third-octaves 125 Hz - 4 kHz,
	// where the ear judges level, put back - capped, so a 1920s band-pass
	// does not turn its own noise up by 15 dB.
	EraEq eq;
	eq.design (p, mFs);
	double sum = 0.0;
	int n = 0;
	for (double f = 125.0; f <= 4001.0; f *= 1.2599210498948732)
	{
		const double m = eq.magnitude (mFs, f);
		sum += m * m;
		++n;
	}
	const double db = -10.0 * std::log10 (sum / n);
	return std::clamp (db, -3.0, 9.0);
}

//------------------------------------------------------------------------
double Dsp::speakerMakeup (const SpeakerProfile& p) const
{
	// As the era's: the speaker's mean power over 125 Hz - 4 kHz put back,
	// capped - so switching speakers changes the colour, not the level
	// (and a 2-inch's missing bass is not turned up to make up for it).
	// NO ALLOCATION: this runs on the audio thread on a speaker change.
	// The copy's echo buffer stays empty; only its length is read.
	SpeakerChain c;
	c.combCapacity = mSpkL.combCapacity;
	c.design (p, mFs, 0.0);
	double sum = 0.0;
	int n = 0;
	for (double f = 125.0; f <= 4001.0; f *= 1.2599210498948732)
	{
		const double m = c.magnitude (mFs, f);
		sum += m * m;
		++n;
	}
	return std::clamp (-10.0 * std::log10 (sum / n), -9.0, 9.0);
}

//------------------------------------------------------------------------
void Dsp::designProfile ()
{
	mActiveEra = mEra;
	mActiveDisc = mDisc;
	mActivePlayer = mPlayer;
	mActiveSpeaker = mSpeaker;
	// The read head reads earlier by each oversampler's delay. Changed only
	// here, while the wet is faded out, so the step is never heard.
	// Whole samples: a still record's read head stays on a sample, so the
	// Hermite reads it exactly (a fractional offset would dull the top).
	mOsComp = std::round (mOsDelay * (mSpeaker != kSpeakerOff ? 2.0 : 1.0));

	// The speaker. The UI thread's setSampleRate sized its echo; this
	// only sets coefficients, so it is safe on the audio thread too.
	if (mSpeaker != kSpeakerOff)
	{
		const SpeakerProfile& sp = speakerProfile (mSpeaker);
		mSpeakerMakeupDb = speakerMakeup (sp);
		mSpkL.design (sp, mFs, mSpeakerMakeupDb);
		mSpkR.design (sp, mFs, mSpeakerMakeupDb);
	}
	else
		mSpeakerMakeupDb = 0.0;
	mSpkL.clear ();
	mSpkR.clear ();
	const int disc = effectiveDisc (mEra, mDisc);
	const Profile& p = profileOf (mEra, disc);

	// The recording (the music) and the player (the surface).
	mEqL.design (p, mFs);
	mEqR.design (p, mFs);
	mEqL.clear ();
	mEqR.clear ();
	mLatheOsL.clear ();
	mLatheOsR.clear ();
	mMakeupDb = profileMakeupDb (p);
	const Profile& player = playerProfile (mEra, disc, mPlayer);
	mPlayL.design (player, mFs);
	mPlayR.design (player, mFs);
	mPlayL.clear ();
	mPlayR.clear ();

	// Surface noise's colour: shellac roars in the presence band, vinyl
	// hisses softly and darker.
	if (disc == kDiscShellac)
	{
		mHissShapeL.bandPass (mFs, 2500.0, 0.7);
		mHissShape2L.highPass (mFs, 300.0, 0.7071);
	}
	else
	{
		mHissShapeL.lowPass (mFs, std::min (6000.0, 0.45 * mFs), 0.7071);
		mHissShape2L.highPass (mFs, 120.0, 0.7071);
	}
	mHissShapeR = mHissShapeL;
	mHissShape2R = mHissShape2L;
	mRumbleL.lowPass (mFs, 25.0, 0.7071);
	mRumbleR = mRumbleL;

	// Normalise each noise to unit RMS, so the profile's dB are what comes
	// out (before the era EQ): integrate |H|^2 numerically.
	auto noiseGain = [this] (const Biquad& a, const Biquad& b) {
		double sum = 0.0;
		constexpr int kSteps = 4000;
		const double df = 0.5 * mFs / kSteps;
		for (int i = 1; i <= kSteps; ++i)
		{
			const double f = (i - 0.5) * df;
			const double m = a.magnitude (mFs, f) * b.magnitude (mFs, f);
			sum += m * m * df;
		}
		const double sigma = 0.5773502691896258;   // RMS of uniform [-1, 1)
		const double rms = sigma * std::sqrt (2.0 * sum / mFs);
		return rms > 1e-12 ? 1.0 / rms : 0.0;
	};
	mHissNorm = noiseGain (mHissShapeL, mHissShape2L);
	Biquad identity;
	mRumbleNorm = noiseGain (mRumbleL, identity);

	mHissShapeL.clear (); mHissShapeR.clear (); mHissShape2L.clear (); mHissShape2R.clear ();
	mRumbleL.clear (); mRumbleR.clear ();
	mProfilePending = false;
}

//------------------------------------------------------------------------
void Dsp::reset ()
{
	std::fill (mBufL.begin (), mBufL.end (), 0.0f);
	std::fill (mBufR.begin (), mBufR.end (), 0.0f);
	mWrite = 0;
	mSeed = kSeed;
	mTheta = 0.0;
	mSynced = false;
	mNextSample = 0;
	mDriftPhase = 0.0;
	mFlutterPhase1 = 0.0;
	mFlutterPhase2 = 0.37;
	mFlutterWobble = 0.0;
	mOffset = 0.0;
	mXfadeFrom = 0.0;
	mXfade = 0;
	mStickLeft = 0;
	mJumpCount = 0;
	mMinDelay = 1e9;
	for (auto& g : mGrains)
		g.live = false;
	mEqL.clear (); mEqR.clear ();
	mLatheOsL.clear (); mLatheOsR.clear ();
	mPlayL.clear (); mPlayR.clear ();
	mSpkL.clear (); mSpkR.clear ();
	mHissShapeL.clear (); mHissShapeR.clear (); mHissShape2L.clear (); mHissShape2R.clear ();
	mRumbleL.clear (); mRumbleR.clear ();
	mWarpLpL = mWarpLpR = 0.0;
}

//------------------------------------------------------------------------
void Dsp::snapParameters ()
{
	mScratches.snap (); mDust.snap (); mWow.snap (); mFlutter.snap (); mWarp.snap ();
	mMix.snap (); mOutput.snap (); mBypass.snap (); mSurface.snap ();
	if (mProfilePending)
		designProfile ();
	mProfileGain = 1.0;
}

//------------------------------------------------------------------------
void Dsp::setEra (int era)
{
	era = std::clamp (era, 0, kEraCount - 1);
	mEra = era;
	markIfChanged ();
}

void Dsp::setDisc (int disc)
{
	mDisc = std::clamp (disc, 0, kDiscCount - 1);
	markIfChanged ();
}

void Dsp::setPlayer (int player)
{
	mPlayer = std::clamp (player, 0, kPlayerCount - 1);
	markIfChanged ();
}

void Dsp::setSpeaker (int speaker)
{
	// A new speaker fades like a new era: its filters' state on the old
	// signal would thump.
	mSpeaker = std::clamp (speaker, 0, kSpeakerCount - 1);
	markIfChanged ();
}

void Dsp::markIfChanged ()
{
	if (mEra != mActiveEra || effectiveDisc (mEra, mDisc) != effectiveDisc (mActiveEra, mActiveDisc) ||
	    mPlayer != mActivePlayer || mSpeaker != mActiveSpeaker)
		mProfilePending = true;
}

void Dsp::setSpeed (int speed)
{
	// No fade needed: the speed moves only the motor, and the motor's
	// angle carries on from where it is.
	mSpeed = std::clamp (speed, 0, kSpeedCount - 1);
}

void Dsp::setOutputDb (double db)
{
	mOutput.target = dbToGain (std::clamp (db, kOutputMinDb, kOutputMaxDb));
}

void Dsp::setTransport (bool known, bool playing, int64_t projectSample, int frames)
{
	const bool turning = ! known || playing;
	if (known && playing)
	{
		// A start, or the song jumped (a loop, a locate): the disc follows.
		if (! mSynced || projectSample != mNextSample)
			syncToSong (projectSample);
		mSynced = true;
		mNextSample = projectSample + (frames > 0 ? frames : 0);
	}
	else
		mSynced = false;
	mTurning = turning;
	mSurface.target = turning ? 1.0 : 0.0;
}

void Dsp::syncToSong (int64_t projectSample)
{
	// Everything that turns, from the song position: the same passage gets
	// the same scratches, wow and crackle every time it plays.
	const double t = static_cast<double> (projectSample) / mFs;
	auto frac = [] (double x) { return x - std::floor (x); };
	mTheta = frac (t * turntableRpm (mEra, mDisc, mSpeed) / 60.0);
	mDriftPhase = frac (t * kDriftHz);
	mFlutterPhase1 = frac (t * kFlutterHz1);
	mFlutterPhase2 = frac (t * kFlutterHz2 + 0.37);
	mFlutterWobble = frac (t * kFlutterWobbleHz);
	mSeed = kSeed ^ (static_cast<uint64_t> (projectSample) * 0x9E3779B97F4A7C15ull);

	// The surface starts afresh: no crackle left ringing from another
	// passage, the noise filters empty, and from silence if the record was
	// standing still.
	for (auto& g : mGrains)
		g.live = false;
	mHissShapeL.clear (); mHissShapeR.clear (); mHissShape2L.clear (); mHissShape2R.clear ();
	mRumbleL.clear (); mRumbleR.clear ();
	mPlayL.clear (); mPlayR.clear ();
	if (! mTurning)
		mSurface.value = 0.0;

	// A stuck needle belongs to the passage it stuck in.
	if (mOffset > 0.0 || mStickLeft > 0)
	{
		mStickLeft = 0;
		startJump (0.0, false);
	}
}

int Dsp::denormalStateCount () const
{
	int n = 0;
	auto v = [&n] (double x) {
		if (x != 0.0 && std::fabs (x) < 2.2250738585072014e-308)   // DBL_MIN
			++n;
	};
	auto bq = [&v] (const Biquad& b) { v (b.z1); v (b.z2); };
	auto hb = [&v] (const HalfBand& h) {
		for (int i = 0; i < HalfBand::kMaxCoefs; ++i) { v (h.x[i]); v (h.y[i]); }
	};
	auto os = [&hb] (const Oversampler4x& o) { hb (o.up1); hb (o.up2); hb (o.down2); hb (o.down1); };
	auto eq = [&bq] (const EraEq& e) { bq (e.hp); bq (e.lp1); bq (e.lp2); bq (e.pk1); bq (e.pk2); };
	auto spk = [&] (const SpeakerChain& s) {
		bq (s.hp1); bq (s.hp2); bq (s.lp1); bq (s.lp2);
		for (const auto& b : s.pk) bq (b);
		os (s.os);
		v (s.coneLp); v (s.combLp);
		for (double x : s.comb) v (x);
	};

	eq (mEqL); eq (mEqR); eq (mPlayL); eq (mPlayR);
	os (mLatheOsL); os (mLatheOsR);
	spk (mSpkL); spk (mSpkR);
	bq (mHissShapeL); bq (mHissShapeR); bq (mHissShape2L); bq (mHissShape2R);
	bq (mRumbleL); bq (mRumbleR);
	v (mWarpLpL); v (mWarpLpR);
	for (const Smooth* s : { &mScratches, &mDust, &mWow, &mFlutter, &mWarp, &mMix, &mOutput, &mBypass, &mSurface })
		v (s->value);
	return n;
}

double Dsp::takeMinReadDelay ()
{
	const double m = mMinDelay;
	mMinDelay = 1e9;
	return m;
}

//------------------------------------------------------------------------
void Dsp::triggerGrain (double noiseAmp, double noiseTauS, double ringAmp, double ringHz, double ringTauS,
                        double pan, bool stereo)
{
	for (auto& g : mGrains)
	{
		if (g.live)
			continue;
		g.live = true;
		g.n = 0;
		const double tauN = std::max (noiseTauS * mFs, 0.5);
		const double tauR = std::max (ringTauS * mFs, 0.5);
		g.noiseAmp = noiseAmp;
		g.noiseDecay = std::exp (-1.0 / tauN);
		g.ringAmp = ringAmp;
		g.ringDecay = std::exp (-1.0 / tauR);
		const double w = kTwoPi * std::min (ringHz, 0.45 * mFs) / mFs;
		g.cosw = std::cos (w);
		g.sinw = std::sin (w);
		g.c = 1.0;
		g.s = 0.0;
		g.length = static_cast<int> (6.0 * std::max (ringAmp > 0.0 ? tauR : 0.0, tauN)) + 1;
		if (stereo)
		{
			const double t = (std::clamp (pan, -1.0, 1.0) + 1.0) * kPi / 4.0;
			g.gainL = std::cos (t) * 1.4142135623730951;
			g.gainR = std::sin (t) * 1.4142135623730951;
		}
		else
			g.gainL = g.gainR = 1.0;
		return;
	}
	// All 32 busy: this one is lost under the others anyway.
}

//------------------------------------------------------------------------
void Dsp::startJump (double newOffset, bool thump)
{
	mXfadeFrom = mOffset;
	mOffset = newOffset;
	mXfade = mXfadeLen;
	++mJumpCount;
	if (! thump)
		return;

	// The stylus lands: a thump and a crack.
	const Profile& p = profileOf (mActiveEra, effectiveDisc (mActiveEra, mActiveDisc));
	const double a = dbToGain (p.scratchDb + 3.0);
	triggerGrain (a, 0.0015, a * 0.8, 70.0 + 30.0 * uniform (), 0.012, bipolar () * 0.5,
	              isStereo (mActiveEra));
}

//------------------------------------------------------------------------
double Dsp::readAt (const std::vector<float>& buf, double delay) const
{
	// 4-point, 3rd-order Hermite. The write head has already written the
	// current sample at mWrite, so a delay of 2 still has i + 2 to read.
	const double pos = static_cast<double> (mWrite) - delay;
	const double fl = std::floor (pos);
	const double t = pos - fl;
	const uint32_t i = static_cast<uint32_t> (static_cast<int64_t> (fl)) & mMask;
	const double xm1 = buf[(i - 1) & mMask];
	const double x0 = buf[i];
	const double x1 = buf[(i + 1) & mMask];
	const double x2 = buf[(i + 2) & mMask];
	const double c1 = 0.5 * (x1 - xm1);
	const double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
	const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
	return ((c3 * t + c2) * t + c1) * t + x0;
}

//------------------------------------------------------------------------
void Dsp::process (const float* inL, const float* inR, float* outL, float* outR, int frames)
{
	if (frames <= 0 || ! inL || ! inR || ! outL || ! outR || mBufL.empty ())
		return;

	const double k = mSmoothK;
	const double k3 = 1.0 - std::exp (-1.0 / (0.03 * mFs / 4.6));   // the surface: ~30 ms to silence
	const double fadeStep = 1.0 / (0.008 * mFs);
	const double maxOffset = static_cast<double> (mMask) - 4.0 * kBaseDelaySeconds * mFs - 8.0;

	for (int i = 0; i < frames; ++i)
	{
		const double xL = inL[i], xR = inR[i];
		mBufL[mWrite & mMask] = static_cast<float> (xL);
		mBufR[mWrite & mMask] = static_cast<float> (xR);

		// ---- the smoothers --------------------------------------------------
		const double scratches = mScratches.next (k);
		const double dust = mDust.next (k) / 100.0;
		const double wow = mWow.next (k) / 100.0;
		const double flutter = mFlutter.next (k) / 100.0;
		const double warp = mWarp.next (k) / 100.0;
		const double mix = mMix.next (k);
		const double outGain = mOutput.next (k);
		const double bypass = mBypass.next (k);

		// ---- an era change: fade the wet out, swap the chain, fade it in --
		if (mProfilePending)
		{
			mProfileGain -= fadeStep;
			if (mProfileGain <= 0.0)
			{
				mProfileGain = 0.0;
				designProfile ();
			}
		}
		else if (mProfileGain < 1.0)
			mProfileGain = std::min (1.0, mProfileGain + fadeStep);

		const int era = mActiveEra;
		const int disc = effectiveDisc (mActiveEra, mActiveDisc);
		const bool stereo = isStereo (era);
		const Profile& prof = profileOf (era, disc);
		const double fr = turntableRpm (era, mActiveDisc, mSpeed) / 60.0;

		// ---- the motor: it turns while the transport plays ---------------------
		const bool turning = mTurning;
		const double surfaceGain = mSurface.next (k3);
		const double prevTheta = mTheta;
		if (turning)
			mTheta += fr / mFs;
		const bool wrapped = mTheta >= 1.0;
		if (wrapped)
			mTheta -= 1.0;
		auto crossed = [&] (double phase) {
			return wrapped ? (phase > prevTheta || phase <= mTheta) : (phase > prevTheta && phase <= mTheta);
		};

		// ---- the scratches, and the needle sticking at the deepest ------------
		const int active = activeScratches (scratches);
		const bool jumpsAllowed = jumpChancePerSecond (scratches, mJumpsOn) > 0.0;
		if (! turning && (mOffset > 0.0 || mStickLeft > 0) && mXfade == 0)
		{
			// Stopped with the needle stuck: it lets go, quietly.
			mStickLeft = 0;
			startJump (0.0, false);
		}
		if (crossed (scratchSlot (0).phase))
		{
			const double rev = mFs / fr;
			if (mOffset > 0.0 || mStickLeft > 0)
			{
				// Stuck: go round again, or skip on to where the music is now.
				if (mStickLeft > 0 && jumpsAllowed && mOffset + rev <= maxOffset)
				{
					--mStickLeft;
					startJump (mOffset + rev);
				}
				else
				{
					mStickLeft = 0;
					startJump (0.0);
				}
			}
			else if (jumpsAllowed && uniform () < jumpChancePerSecond (scratches, mJumpsOn) / fr)
			{
				const double u = uniform ();
				const int repeats = std::clamp (1 + static_cast<int> (u * u * kMaxStick), 1, kMaxStick);
				mStickLeft = repeats - 1;
				startJump (rev);
			}
		}
		if (active > 0)
		{
			const double level = dbToGain (prof.scratchDb) * (0.35 + 0.65 * scratches / 100.0);
			for (int s = 0; s < active; ++s)
			{
				const ScratchSlot slot = scratchSlot (s);
				if (! crossed (slot.phase))
					continue;
				if (uniform () < 0.08)   // the stylus rides over it this time
					continue;
				const double a = level * slot.depth * (0.6 + 0.4 * uniform ());
				triggerGrain (a, 0.00015, a * 0.6, 150.0 + 110.0 * uniform (), 0.003, bipolar () * 0.6, stereo);
			}
		}

		// ---- the read delay: wow, drift, flutter, warp -------------------------
		const double wowDepth = wow * kWowMaxDepth;
		const double wowA = wowDepth / (kTwoPi * fr) * mFs;
		if (turning)
			mDriftPhase += kDriftHz / mFs;
		if (mDriftPhase >= 1.0) mDriftPhase -= 1.0;
		const double driftA = 0.25 * wowDepth / (kTwoPi * kDriftHz) * mFs;

		const double flDepth = flutter * kFlutterMaxDepth;
		if (turning)
		{
			mFlutterPhase1 += kFlutterHz1 / mFs; if (mFlutterPhase1 >= 1.0) mFlutterPhase1 -= 1.0;
			mFlutterPhase2 += kFlutterHz2 / mFs; if (mFlutterPhase2 >= 1.0) mFlutterPhase2 -= 1.0;
			mFlutterWobble += kFlutterWobbleHz / mFs; if (mFlutterWobble >= 1.0) mFlutterWobble -= 1.0;
		}
		const double wobble = 1.0 + 0.35 * std::sin (kTwoPi * mFlutterWobble);
		// 0.6 + 0.4 of the depth, and the wobble up to 1.35: the peak stays
		// within the stated depth / 1.35 of it on average.
		const double fl1 = 0.6 / 1.35 * flDepth / (kTwoPi * kFlutterHz1) * mFs;
		const double fl2 = 0.4 / 1.35 * flDepth / (kTwoPi * kFlutterHz2) * mFs;

		const double bump = warpBump (mTheta);
		const double warpA = warp * kWarpMaxDepth * kWarpSigma / (kWarpSlopePeak * fr) * mFs;

		double d = mBaseDelay - mOsComp
		         + wowA * std::sin (kTwoPi * mTheta + 0.3)
		         + driftA * std::sin (kTwoPi * mDriftPhase)
		         + wobble * (fl1 * std::sin (kTwoPi * mFlutterPhase1) + fl2 * std::sin (kTwoPi * mFlutterPhase2))
		         + warpA * (bump - kWarpBumpMean);
		mMinDelay = std::min (mMinDelay, d);   // before the clamp: the tests hold it off the write head
		d = std::max (d, 3.0);

		double wL, wR;
		if (mXfade > 0)
		{
			const double a = static_cast<double> (mXfade) / mXfadeLen;
			wL = a * readAt (mBufL, d + mXfadeFrom) + (1.0 - a) * readAt (mBufL, d + mOffset);
			wR = a * readAt (mBufR, d + mXfadeFrom) + (1.0 - a) * readAt (mBufR, d + mOffset);
			--mXfade;
		}
		else
		{
			wL = readAt (mBufL, d + mOffset);
			wR = readAt (mBufR, d + mOffset);
		}

		// ---- mono or stereo, as pressed ----------------------------------------
		{
			const double m = 0.5 * (wL + wR), s = 0.5 * (wL - wR) * prof.width;
			wL = m + s;
			wR = m - s;
		}

		// ---- THE RECORDING: the era's band, its horn, the cutting lathe -------
		// The music only - this was done to it before the disc was pressed.
		wL = mEqL.process (wL);
		wR = mEqR.process (wR);
		{
			// A gentle saturation, the lathe overloading: unity for small
			// signals, softer at the top - at 4x the rate, so its harmonics
			// (a 10 kHz note's third is at 30 kHz) do not fold back.
			const double dr = prof.drive;
			auto lathe = [dr] (double v) { return latheCurve (v, dr); };
			wL = mLatheOsL.process (wL, lathe);
			wR = mLatheOsR.process (wR, lathe);
			const double makeup = dbToGain (mMakeupDb);
			wL *= makeup;
			wR *= makeup;
		}

		// ---- the surface: what the stylus meets in the groove ------------------
		double sL = 0.0, sR = 0.0;
		if (dust > 0.0 && surfaceGain > 0.0)
		{
			const double swish = 1.0 + 0.2 * std::sin (kTwoPi * mTheta);
			const double hiss = dbToGain (prof.hissDb) * std::pow (dust, 0.75) * mHissNorm * swish * surfaceGain;
			const double nL = bipolar ();
			const double nR = stereo ? bipolar () : nL;
			const double hL = mHissShape2L.process (mHissShapeL.process (nL));
			const double hR = stereo ? mHissShape2R.process (mHissShapeR.process (nR)) : hL;

			const double rum = dbToGain (prof.rumbleDb) * dust * mRumbleNorm * surfaceGain;
			const double r0 = mRumbleL.process (bipolar ());
			const double r1 = stereo ? mRumbleR.process (bipolar ()) : r0;

			sL += hiss * hL + rum * r0;
			sR += hiss * hR + rum * r1;

			if (turning && uniform () < crackleRate (era, disc, dust * 100.0) / mFs)
			{
				const double u = uniform ();
				const double a = dbToGain (prof.crackleDb) * u * u * u;
				if (uniform () < 0.1)   // a pop: a dull, ringing one
					triggerGrain (a * 0.5, 0.0002, a, 300.0 + 900.0 * uniform (), 0.0008 + 0.0017 * uniform (),
					              bipolar (), stereo);
				else                    // a tick
					triggerGrain (a, 0.00005 + 0.0002 * uniform (), 0.0, 0.0, 0.0, bipolar (), stereo);
			}
		}

		for (auto& g : mGrains)
		{
			if (! g.live)
				continue;
			const double v = g.noiseAmp * bipolar () + g.ringAmp * g.s;
			sL += v * g.gainL;
			sR += v * g.gainR;
			g.noiseAmp *= g.noiseDecay;
			g.ringAmp *= g.ringDecay;
			const double c = g.c * g.cosw - g.s * g.sinw;
			g.s = g.s * g.cosw + g.c * g.sinw;
			g.c = c;
			if (++g.n >= g.length)
				g.live = false;
		}

		// ---- THE PLAYER: the surface through the pickup -------------------------
		// The music's colour is already in the groove; what the PLAYER adds
		// shows on the surface. Period: a player as old as the disc, the
		// era's band again, so the crackle is as dull as the music. Modern:
		// a good turntable today, wide open, so it crackles bright and crisp
		// over the old sound - the sound of most transfers of old 78s.
		wL += mPlayL.process (sL);
		wR += mPlayR.process (sR);

		// The warp's high spot: the stylus lifts, the top end goes and the
		// level dips. Exactly nothing when there is no warp.
		if (warp > 0.0)
		{
			const double lift = warp * bump;
			mWarpLpL += mWarpLpCoef * (wL - mWarpLpL);
			mWarpLpR += mWarpLpCoef * (wR - mWarpLpR);
			const double g = 1.0 - 0.25 * lift;
			wL = g * (wL + (mWarpLpL - wL) * 0.7 * lift);
			wR = g * (wR + (mWarpLpR - wR) * 0.7 * lift);
		}
		else
		{
			mWarpLpL = wL;
			mWarpLpR = wR;
		}

		// ---- THE SPEAKER: what it all comes out of ------------------------------
		// Off is no stage at all - not a flat filter - so the plug-in is
		// bit-identical to before the switch. One cabinet sums to mono.
		if (mActiveSpeaker != kSpeakerOff)
		{
			if (speakerIsMono (mActiveSpeaker))
			{
				wL = wR = mSpkL.process (0.5 * (wL + wR));
			}
			else
			{
				wL = mSpkL.process (wL);
				wR = mSpkR.process (wR);
			}
		}

		// An era, player or speaker change: the wet fades out, the chain
		// swaps, in.
		wL *= mProfileGain;
		wR *= mProfileGain;

		// ---- dry, mix, output, bypass ---------------------------------------------
		const double dL = mBufL[(mWrite - static_cast<uint32_t> (mBaseDelay)) & mMask];
		const double dR = mBufR[(mWrite - static_cast<uint32_t> (mBaseDelay)) & mMask];
		double yL = (dL * (1.0 - mix) + wL * mix) * outGain;
		double yR = (dR * (1.0 - mix) + wR * mix) * outGain;
		yL = yL * (1.0 - bypass) + dL * bypass;
		yR = yR * (1.0 - bypass) + dR * bypass;

		outL[i] = static_cast<float> (yL);
		outR[i] = static_cast<float> (yR);
		++mWrite;
	}
}

//------------------------------------------------------------------------
} // namespace WornVinyl
