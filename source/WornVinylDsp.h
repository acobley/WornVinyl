//------------------------------------------------------------------------
// WornVinyl - the audio line
//
// A record player in front of the track: the music is read back off a
// "disc" that turns at 78 or 33 1/3 rpm, through the playback chain of
// its decade, with the disc's own faults added at the stylus.
//
//   input -> ring buffer --(modulated read: wow, flutter, warp, jumps)-->
//         -> mono / stereo width of the era
//         + surface hiss, rumble, crackle (Dust), scratch ticks (Scratches)
//         -> the era's playback EQ (high-pass, 4-pole low-pass, the horn's
//            or the pickup's resonances), the warp's HF loss and level dip
//         -> a gentle saturation, a make-up gain from the EQ itself
//         -> Mix against the dry signal (delayed by the same latency)
//         -> Output
//
// NO SDK HEADER MAY ENTER THIS FILE OR ITS .cpp. It compiles with plain
// c++ -std=c++17, so tests/DspTests.cpp can measure it before anything is
// built; and because VST3 splits processor from controller, everything the
// panel shows that the DSP decides - which disc an era plays, the rpm, the
// scratches' places on the disc, the crackle rate, the bandwidth - is ONE
// function here that both sides call. The turntable screen draws the same
// scratches the stylus hits.
//
// DETERMINISTIC: every random thing comes from our own SplitMix64, and
// reset() re-seeds it, so a bounce of the same audio is the same bounce.
//------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace WornVinyl {

//------------------------------------------------------------------------
// The choices
//------------------------------------------------------------------------
enum Era
{
	kEra1920,   // acoustic recording, shellac 78
	kEra1940,   // electrical, shellac 78
	kEra1950,   // shellac 78 OR the new mono vinyl LP - the one decade with a choice
	kEra1960,   // stereo vinyl LP
	kEra1970,   // stereo vinyl LP, the best of it
	kEraCount
};
constexpr int kEraDefault = kEra1960;

enum Disc
{
	kDiscShellac,
	kDiscVinyl,
	kDiscCount
};
constexpr int kDiscDefault = kDiscVinyl;

const char* eraName (int era);      // "1920s" ...
const char* discName (int disc);    // "Shellac", "Vinyl"

/** Only the 1950s had both. Before, every disc was shellac; after, vinyl -
    and the Disc switch is shown SET BY THE ERA. */
constexpr bool discLocked (int era) { return era != kEra1950; }

/** The disc an era actually plays: the switch counts in the 1950s only. */
constexpr int effectiveDisc (int era, int disc)
{
	return (era <= kEra1940) ? kDiscShellac
	     : (era >= kEra1960) ? kDiscVinyl
	     : ((disc == kDiscShellac) ? kDiscShellac : kDiscVinyl);
}

/** 78 rpm for shellac, 33 1/3 for an LP. */
constexpr double rpmOf (int disc) { return disc == kDiscShellac ? 78.0 : 100.0 / 3.0; }
constexpr double revolutionHz (int disc) { return rpmOf (disc) / 60.0; }

/** A disc pressed in stereo - the 1960s and 1970s. Everything older is
    mono: summed, and its noise identical in both channels. */
constexpr bool isStereo (int era) { return era >= kEra1960; }

//------------------------------------------------------------------------
/** THE PLAYBACK CHAIN OF AN ERA AND A DISC. Levels are dBFS at 100 %. */
struct Profile
{
	double hpHz;            // 2-pole high-pass
	double lpHz;            // 4-pole low-pass (Butterworth)
	double peak1Hz, peak1Db, peak1Q;   // the horn's / pickup's resonances
	double peak2Hz, peak2Db, peak2Q;   //   (0 dB = none)
	double width;           // 0 mono .. 1 full stereo
	double drive;           // 0 .. 1, the saturation's share at full scale
	double hissDb;          // surface noise at Dust 100 %
	double rumbleDb;        // platter rumble at Dust 100 %
	double crackleRate;     // crackles a second at Dust 100 %
	double crackleDb;       // the loudest crackle's peak
	double scratchDb;       // the deepest scratch's tick
};

const Profile& profileOf (int era, int disc);

/** "30 Hz - 15 kHz", from the profile - the header line and the tests. */
std::string bandwidthText (int era, int disc);

//------------------------------------------------------------------------
// Ranges - the parameter table reads these, so the host's range and the
// DSP's cannot drift apart.
//------------------------------------------------------------------------
constexpr double kPercentMin = 0.0;
constexpr double kPercentMax = 100.0;

constexpr double kScratchesDefault = 20.0;
constexpr double kDustDefault      = 30.0;
constexpr double kWowDefault       = 15.0;
constexpr double kFlutterDefault   = 10.0;
constexpr double kWarpDefault      = 0.0;
constexpr double kMixDefault       = 100.0;

constexpr double kOutputMinDb     = -12.0;
constexpr double kOutputMaxDb     = 12.0;
constexpr double kOutputDefaultDb = 0.0;

/** Jumps happen only above this much Scratches (and with Jumps on). */
constexpr double kJumpThresholdPct = 60.0;

/** At most this many repeats of a groove before the needle skips on. */
constexpr int kMaxStick = 4;

/** Pitch deviation, peak, as a fraction, at 100 %. 1.2 % wow is a badly
    off-centre hole; 0.4 % flutter is a tired idler; a 4 % swoop is a disc
    left in the sun. */
constexpr double kWowMaxDepth     = 0.012;
constexpr double kFlutterMaxDepth = 0.004;
constexpr double kWarpMaxDepth    = 0.04;

/** The read head's resting delay, which is the plug-in's latency: room for
    the modulation to swing either side (DspTests proves the worst case
    never reads closer than a few samples to the write head). */
constexpr double kBaseDelaySeconds = 0.012;

//------------------------------------------------------------------------
// The scratches - shared with the turntable screen
//------------------------------------------------------------------------
/** A radial scratch: where it is round the disc (0..1 of a turn, 0 = under
    the stylus when the disc angle is 0) and how deep (0..1). Slot 0 is
    the deepest, and the only one the needle jumps at. */
struct ScratchSlot
{
	double phase;
	double depth;
};

constexpr int kMaxScratches = 12;

ScratchSlot scratchSlot (int index);

/** How many slots are cut at this Scratches setting: none at 0 %, one at
    the first touch, all twelve at 100 %. */
int activeScratches (double scratchesPct);

/** Crackles a second at this Dust - the info line, and the DSP's rate. */
double crackleRate (int era, int disc, double dustPct);

/** The chance a second that the needle sticks, at a setting. Zero below
    the threshold or with Jumps off. */
double jumpChancePerSecond (double scratchesPct, bool jumpsOn);

/** The warp's high spot at a disc angle (0..1 a turn): 0 flat, 1 at the
    top of the bump. The DSP's swoop and dip, and the screen's arm lifting,
    both come from this. */
double warpBump (double discAngle);

//------------------------------------------------------------------------
double dbToGain (double db);

//------------------------------------------------------------------------
/** A biquad, transposed direct form II, in double. */
struct Biquad
{
	double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
	double z1 = 0, z2 = 0;

	void clear () { z1 = z2 = 0.0; }
	void setIdentity () { b0 = 1; b1 = b2 = a1 = a2 = 0; }
	void lowPass (double fs, double hz, double q);
	void highPass (double fs, double hz, double q);
	void peak (double fs, double hz, double db, double q);
	void bandPass (double fs, double hz, double q);   // 0 dB at the centre

	double process (double x)
	{
		const double y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		return y;
	}

	/** |H| at `hz` - the make-up gain is worked out from these. */
	double magnitude (double fs, double hz) const;
};

//------------------------------------------------------------------------
/** The era's playback EQ on one channel: HP, 4-pole LP, two peaks. */
struct EraEq
{
	Biquad hp, lp1, lp2, pk1, pk2;
	void design (const Profile& p, double fs);
	void clear ();
	double process (double x) { return pk2.process (pk1.process (lp2.process (lp1.process (hp.process (x))))); }
	double magnitude (double fs, double hz) const;
};

//------------------------------------------------------------------------
class Dsp
{
public:
	Dsp ();

	/** UI thread only (setupProcessing): sizes the disc's ring buffer,
	    redesigns every filter. The only place this class allocates. */
	void setSampleRate (double fs);

	/** Clears every filter, the buffer, the jump and the grains, and
	    re-seeds the random numbers. Not the parameters. */
	void reset ();

	/** Jump every smoother to its target (a project load, activation). */
	void snapParameters ();

	// ---- parameters, in PLAIN units ---------------------------------------
	void setEra (int era);
	void setDisc (int disc);
	void setScratchesPct (double pct) { mScratches.target = clampPct (pct); }
	void setDustPct (double pct)      { mDust.target = clampPct (pct); }
	void setWowPct (double pct)       { mWow.target = clampPct (pct); }
	void setFlutterPct (double pct)   { mFlutter.target = clampPct (pct); }
	void setWarpPct (double pct)      { mWarp.target = clampPct (pct); }
	void setJumps (bool on)           { mJumpsOn = on; }
	void setMixPct (double pct)       { mMix.target = clampPct (pct) / 100.0; }
	void setOutputDb (double db);
	void setBypass (bool on)          { mBypass.target = on ? 1.0 : 0.0; }

	/** In place is fine: each sample is read before it is written. */
	void process (const float* inL, const float* inR, float* outL, float* outR, int frames);

	// ---- what the panel shows --------------------------------------------
	int latencySamples () const { return mBaseDelay; }
	int tailSamples () const { return mBaseDelay; }

	/** Where the disc is, 0..1 of a turn: the motor's angle. */
	double discAngle () const { return mTheta; }

	/** True while the needle is stuck in a groove or jumping. */
	bool skipping () const { return mOffset > 0.0 || mXfade > 0; }

	int era () const { return mEra; }
	int playingDisc () const { return effectiveDisc (mEra, mDisc); }

	// ---- for the tests -----------------------------------------------------
	double sampleRate () const { return mFs; }
	int bufferFrames () const { return static_cast<int> (mBufL.size ()); }
	/** The smallest read delay since the last call, in samples. */
	double takeMinReadDelay ();
	double makeupDb () const { return mMakeupDb; }
	int jumpsTaken () const { return mJumpCount; }

private:
	struct Smooth
	{
		double value = 0.0, target = 0.0;
		double next (double k) { value += (target - value) * k; return value; }
		void snap () { value = target; }
	};

	struct Grain
	{
		bool live = false;
		int n = 0, length = 0;
		double noiseAmp = 0, noiseDecay = 0;        // a burst of noise
		double ringAmp = 0, ringDecay = 0;          // a damped sine
		double c = 1, s = 0, cosw = 1, sinw = 0;    // the sine, by rotation
		double gainL = 1, gainR = 1;
	};

	static double clampPct (double v) { return v < 0.0 ? 0.0 : (v > 100.0 ? 100.0 : v); }

	// random numbers: SplitMix64
	uint64_t mSeed = 0;
	uint64_t nextBits ();
	double uniform ();          // [0, 1)
	double bipolar ();          // [-1, 1)

	void designProfile ();
	void triggerGrain (double noiseAmp, double noiseTauS, double ringAmp, double ringHz, double ringTauS,
	                   double pan, bool stereo);
	void startJump (double newOffset);
	double readAt (const std::vector<float>& buf, double delay) const;
	double profileMakeupDb (const Profile& p) const;

	double mFs = 44100.0;
	int mEra = kEraDefault;
	int mDisc = kDiscDefault;

	// the profile in use, and the one asked for (an era change fades out,
	// swaps, fades in - a new EQ's state on the old signal would thump)
	int mActiveEra = kEraDefault, mActiveDisc = kDiscDefault;
	double mProfileGain = 1.0;
	bool mProfilePending = false;

	Smooth mScratches, mDust, mWow, mFlutter, mWarp, mMix, mOutput, mBypass;
	bool mJumpsOn = true;
	double mSmoothK = 0.001;

	// the disc
	std::vector<float> mBufL, mBufR;
	uint32_t mMask = 0;
	uint32_t mWrite = 0;
	int mBaseDelay = 529;
	double mTheta = 0.0;            // the motor, 0..1 a turn
	double mDriftPhase = 0.0, mFlutterPhase1 = 0.0, mFlutterPhase2 = 0.0, mFlutterWobble = 0.0;
	double mMinDelay = 1e9;

	// jumps
	double mOffset = 0.0;           // samples behind live: k whole turns while stuck
	double mXfadeFrom = 0.0;
	int mXfade = 0, mXfadeLen = 1;
	int mStickLeft = 0;
	int mJumpCount = 0;

	// noise
	Grain mGrains[32];
	Biquad mHissShapeL, mHissShapeR, mHissShape2L, mHissShape2R;
	Biquad mRumbleL, mRumbleR;
	double mRumbleNorm = 1.0, mHissNorm = 1.0;

	// the chain
	EraEq mEqL, mEqR;
	double mMakeupDb = 0.0;
	double mWarpLpL = 0.0, mWarpLpR = 0.0, mWarpLpCoef = 1.0;
	int mCoefCountdown = 0;
};

//------------------------------------------------------------------------
} // namespace WornVinyl
