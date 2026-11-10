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
void Dsp::designProfile ()
{
	mActiveEra = mEra;
	mActiveDisc = mDisc;
	mActivePlayer = mPlayer;
	const int disc = effectiveDisc (mEra, mDisc);
	const Profile& p = profileOf (mEra, disc);

	// The recording (the music) and the player (the surface).
	mEqL.design (p, mFs);
	mEqR.design (p, mFs);
	mEqL.clear ();
	mEqR.clear ();
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
	mPlayL.clear (); mPlayR.clear ();
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

void Dsp::markIfChanged ()
{
	if (mEra != mActiveEra || effectiveDisc (mEra, mDisc) != effectiveDisc (mActiveEra, mActiveDisc) ||
	    mPlayer != mActivePlayer)
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

		double d = mBaseDelay
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
			// signals, softer at the top.
			const double dr = prof.drive;
			wL = (1.0 - dr) * wL + dr * std::tanh (1.5 * wL) / 1.5;
			wR = (1.0 - dr) * wR + dr * std::tanh (1.5 * wR) / 1.5;
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

		// An era or player change: the wet fades out, the chain swaps, in.
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
