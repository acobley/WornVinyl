//------------------------------------------------------------------------
// WornVinyl - the audio line
//
// A record player in front of the track: the music is read back off a
// "disc" that turns at 78, 45 or 33 1/3 rpm. The era's recording chain is
// cut into the groove with the music; the disc's faults are met at the
// stylus; the player decides what those faults sound like.
//
//   input -> ring buffer --(modulated read: wow, flutter, warp, jumps)-->
//         -> mono / stereo width of the era
//         -> THE RECORDING (music only): the era's EQ - high-pass, 4-pole
//            low-pass, the horn's resonances - the lathe's saturation and a
//            make-up gain from the EQ itself
//         + THE SURFACE through THE PLAYER: hiss, rumble, crackle (Dust),
//            scratch ticks (Scratches), through the era's EQ again (Period)
//            or a modern turntable's (Modern)
//         -> the warp's HF loss and level dip (the stylus lifting: both)
//         -> THE SPEAKER (Off, or one of five): what it all comes out of -
//            its band, its cone or horn, its cabinet, its amplifier
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

#include <cmath>
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

//------------------------------------------------------------------------
/** THE PLAYER SWITCH. The era's EQ is cut into the groove with the music;
    the player decides what the SURFACE sounds like. Period: a player as
    old as the disc, the era's band again, so dust and scratches come out
    as dull as the music (the sound before this switch existed). Modern: a
    good turntable today, wide open, so the crackle is bright and crisp
    over the old sound. The music is the same either way. */
enum Player
{
	kPlayerPeriod,
	kPlayerModern,
	kPlayerCount
};
constexpr int kPlayerDefault = kPlayerPeriod;

const char* playerName (int player);   // "Period", "Modern"

//------------------------------------------------------------------------
/** THE SPEAKER SWITCH: what the record is heard through, at the very end
    of the wet chain, so the music AND the surface come out of it. Off is
    the line out (bit-identical to before the switch existed). The others
    are the loudspeakers of five eras, chosen freely - a 1970s LP on a
    wind-up's horn is allowed:

      Horn   a 1920s hand-cranked gramophone: no electricity, a mica
             soundbox and a horn - a band from ~250 Hz to ~4 kHz, the
             soundbox's peaks, and the horn's echo down its own length
      2 in   a 1940s radio-gram's tiny cone and valve amplifier: nothing
             below ~400 Hz, a shouty cone break-up, the cone and the
             valves both giving out when pushed
      3 in   a 1950s portable: a little more bass, a little less shout
      6 in   a 1960s portable with a small tweeter, open-backed: bass from
             ~100 Hz, a boxy hump, the tweeter's sparkle on top
      3-way  1970s hi-fi: an 8-inch bass, a mid and a tweeter in a sealed
             box - nearly flat, 45 Hz - 19 kHz, a crossover dip at 3 kHz

    The four single-cabinet players are MONO (summed, as a stereo record
    on one speaker is); the hi-fi is a stereo pair. */
enum Speaker
{
	kSpeakerOff,
	kSpeakerHorn,
	kSpeaker2Inch,
	kSpeaker3Inch,
	kSpeaker6Inch,
	kSpeakerHiFi,
	kSpeakerCount
};
constexpr int kSpeakerDefault = kSpeakerOff;

const char* speakerName (int speaker);         // the host's: "Off", "1920s Horn", ... "1970s 3-Way"
const char* speakerDialName (int speaker);     // the panel's: "OFF", "HORN", "2 IN", ... "3-WAY"
const char* speakerDescription (int speaker);  // the header's: "", "HORN GRAMOPHONE", ... ("" for Off)

/** One speaker in its cabinet - everything a single box sums to one. */
constexpr bool speakerIsMono (int speaker) { return speaker >= kSpeakerHorn && speaker <= kSpeaker6Inch; }

//------------------------------------------------------------------------
/** THE SPEED SWITCH: Auto plays the disc at its own speed (78 shellac,
    33 1/3 vinyl); the others turn any disc at that speed - a 1960s 45
    single, a shellac 78 put on at 33. It sets how often the disc's faults
    come round (scratch ticks, wow, the warp, a stuck groove's length);
    the music keeps its own pitch and tempo. */
enum Speed
{
	kSpeedAuto,
	kSpeed33,
	kSpeed45,
	kSpeed78,
	kSpeedCount
};
constexpr int kSpeedDefault = kSpeedAuto;

const char* speedName (int speed);     // "Auto", "33", "45", "78"

/** The rpm the disc turns at, all three settings considered. */
constexpr double turntableRpm (int era, int disc, int speed)
{
	return speed == kSpeed33 ? 100.0 / 3.0
	     : speed == kSpeed45 ? 45.0
	     : speed == kSpeed78 ? 78.0
	     : rpmOf (effectiveDisc (era, disc));
}

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

/** The surface's EQ - the player's: the era's own (Period) or a modern
    turntable's 20 Hz - 20 kHz (Modern). Only hp, lp and the peaks count. */
const Profile& playerProfile (int era, int disc, int player);

/** "30 Hz - 15 kHz", from the profile - the header line and the tests. */
std::string bandwidthText (int era, int disc);

//------------------------------------------------------------------------
/** A SPEAKER, as a recipe: its band and resonances (four filters and
    four peaks, 0 Hz = not there), the cone's excursion (the band below
    coneHz saturating, tanh (k x) / k - small for small signals, giving out
    when pushed), the amplifier's soft clip (as the lathe's), and its
    cabinet or horn: an echo `combMs` long fed back at `combFeedback`
    through a low-pass (0 ms = none). */
struct SpeakerProfile
{
	double hp1Hz, hp1Q, hp2Hz, hp2Q;
	double lp1Hz, lp1Q, lp2Hz, lp2Q;
	double pkHz[4], pkDb[4], pkQ[4];
	double coneHz, coneK;
	double ampDrive;
	double combMs, combFeedback, combLpHz;
};

/** Off's profile is never used - Off bypasses the stage. */
const SpeakerProfile& speakerProfile (int speaker);

/** The longest cabinet echo any speaker has, so the delay is sized once. */
constexpr double kMaxSpeakerCombMs = 4.0;

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
/** THE CURVES THAT BEND THE SIGNAL - and so make harmonics, which alias
    unless they run oversampled (Oversampler4x below). Public so the tests
    can show what the naive version does. Unity for small signals. */
inline double latheCurve (double x, double drive)
{
	return (1.0 - drive) * x + drive * std::tanh (1.5 * x) / 1.5;
}

//------------------------------------------------------------------------
/** A HALF-BAND FILTER for 2x up- or down-sampling: two parallel chains of
    first-order allpasses, polyphase (Valenzuela & Constantinides; the
    design formulae are Laurent de Soras' hiir's). IIR, so it adds only a
    few samples of delay where a linear-phase FIR would add dozens. */
struct HalfBand
{
	static constexpr int kMaxCoefs = 16;
	int n = 0;
	double c[kMaxCoefs] = {}, x[kMaxCoefs] = {}, y[kMaxCoefs] = {};

	/** `attenuationDb` in the stopband; `transition` the band from the
	    passband's edge to Nyquist/2, as a fraction of the HIGH rate. */
	void design (double attenuationDb, double transition);
	void clear ();

	/** One sample in at the low rate, two out at the high rate. */
	void up (double in, double& out0, double& out1);
	/** Two samples in at the high rate, one out at the low rate. */
	double down (double in0, double in1);

private:
	void paths (double& even, double& odd);
};

/** 4x OVERSAMPLING around a nonlinear curve: up 2x twice, the curve at
    4x, down twice. Harmonics up to the 9th of anything in the audio band
    stay below the 4x rate's Nyquist and are filtered off on the way down,
    instead of folding back as aliases. */
struct Oversampler4x
{
	HalfBand up1, up2, down2, down1;

	void design ();   // fixed coefficients; no allocation
	void clear ();

	/** Its delay at low frequencies, in samples at the base rate (an
	    impulse's centre of mass through the four half-bands; ~5.3, the
	    same at every rate). The read head reads this much earlier per
	    oversampler in the chain, so the wet comes out at the reported
	    latency and lines up with the dry. */
	static double delaySamples ();

	template <typename Curve>
	double process (double x, Curve&& curve)
	{
		double a, b, a0, a1, b0, b1;
		up1.up (x, a, b);
		up2.up (a, a0, a1);
		up2.up (b, b0, b1);
		const double p = down2.down (curve (a0), curve (a1));
		const double q = down2.down (curve (b0), curve (b1));
		return down1.down (p, q);
	}
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
/** A speaker on one channel: amp, cone, EQ, cabinet echo, make-up. */
struct SpeakerChain
{
	Biquad hp1, hp2, lp1, lp2, pk[4];
	double ampDrive = 0.0, coneK = 0.0, coneCoef = 0.0, coneLp = 0.0;   // coneCoef at 4x the rate
	Oversampler4x os;                   // the amp and the cone run at 4x
	std::vector<double> comb;           // sized by allocate() only
	uint32_t combMask = 0, combWrite = 0;
	int combCapacity = 0;               // the longest echo the buffer holds
	int combDelay = 0;
	double combFeedback = 0.0, combCoef = 1.0, combLp = 0.0;
	double makeup = 1.0;

	/** The cabinet echo's buffer, for the longest echo at this rate. */
	void allocate (double fs);
	/** No allocation: the audio thread calls this on a speaker change. */
	void design (const SpeakerProfile& p, double fs, double makeupDb);
	void clear ();
	double process (double x);

	/** |H| of the linear part (filters and cabinet) - for the make-up. */
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
	void setSpeed (int speed);
	void setPlayer (int player);
	void setSpeaker (int speaker);
	void setScratchesPct (double pct) { mScratches.target = clampPct (pct); }
	void setDustPct (double pct)      { mDust.target = clampPct (pct); }
	void setWowPct (double pct)       { mWow.target = clampPct (pct); }
	void setFlutterPct (double pct)   { mFlutter.target = clampPct (pct); }
	void setWarpPct (double pct)      { mWarp.target = clampPct (pct); }
	void setJumps (bool on)           { mJumpsOn = on; }
	void setMixPct (double pct)       { mMix.target = clampPct (pct) / 100.0; }
	void setOutputDb (double db);
	void setBypass (bool on)          { mBypass.target = on ? 1.0 : 0.0; }

	/** THE HOST'S TRANSPORT, once a block before process(). The record
	    turns while the transport plays: stopped, the motor stops (the disc
	    and the turntable screen freeze, no wow or flutter), the surface
	    fades out in 30 ms and a stuck needle lets go. The music still
	    goes through the recording chain, so monitoring a stopped track
	    sounds the same, only silent underneath.

	    ON EVERY START, and whenever the song position jumps (a loop, a
	    locate), the disc's angle, the flutter's phases and the random
	    numbers are set FROM THE SONG POSITION - so the scratches land on
	    the same beats every time a passage plays, and a bounce of it is
	    the same bounce.

	    `known` false (a host that gives no ProcessContext): always
	    turning, as if playing. */
	void setTransport (bool known, bool playing, int64_t projectSample, int frames);

	/** In place is fine: each sample is read before it is written. */
	void process (const float* inL, const float* inR, float* outL, float* outR, int frames);

	// ---- what the panel shows --------------------------------------------
	int latencySamples () const { return mBaseDelay; }
	int tailSamples () const { return mBaseDelay; }

	/** Where the disc is, 0..1 of a turn: the motor's angle. */
	double discAngle () const { return mTheta; }

	/** True while the needle is stuck in a groove or jumping. */
	bool skipping () const { return mOffset > 0.0 || mXfade > 0; }

	/** True while the motor runs: playing, or a host that does not say. */
	bool turning () const { return mTurning; }

	int era () const { return mEra; }
	int playingDisc () const { return effectiveDisc (mEra, mDisc); }
	double rpm () const { return turntableRpm (mActiveEra, mActiveDisc, mSpeed); }

	// ---- for the tests -----------------------------------------------------
	double sampleRate () const { return mFs; }
	int bufferFrames () const { return static_cast<int> (mBufL.size ()); }
	/** The smallest read delay since the last call, in samples. */
	double takeMinReadDelay ();
	double makeupDb () const { return mMakeupDb; }
	double speakerMakeupDb () const { return mSpeakerMakeupDb; }
	int jumpsTaken () const { return mJumpCount; }
	int activeSpeaker () const { return mActiveSpeaker; }

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
	void markIfChanged ();
	void triggerGrain (double noiseAmp, double noiseTauS, double ringAmp, double ringHz, double ringTauS,
	                   double pan, bool stereo);
	void startJump (double newOffset, bool thump = true);
	void syncToSong (int64_t projectSample);
	double readAt (const std::vector<float>& buf, double delay) const;
	double profileMakeupDb (const Profile& p) const;
	double speakerMakeup (const SpeakerProfile& p) const;

	double mFs = 44100.0;
	int mEra = kEraDefault;
	int mDisc = kDiscDefault;
	int mSpeed = kSpeedDefault;
	int mPlayer = kPlayerDefault;
	int mActivePlayer = kPlayerDefault;
	int mSpeaker = kSpeakerDefault;
	int mActiveSpeaker = kSpeakerDefault;

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
	bool mTurning = true;           // the transport: playing, or unknown
	bool mSynced = false;           // set from the song position since the last start
	int64_t mNextSample = 0;        // where the song should be at the next block
	Smooth mSurface;                // 0..1: the surface fades with the motor
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
	EraEq mEqL, mEqR;           // the recording - the music
	Oversampler4x mLatheOsL, mLatheOsR;   // the lathe's saturation, at 4x
	EraEq mPlayL, mPlayR;       // the player - the surface
	double mMakeupDb = 0.0;
	double mOsDelay = 0.0;          // Oversampler4x::delaySamples (), read earlier by it
	double mOsComp = 0.0;           // ...once for the lathe, again while a speaker is on
	SpeakerChain mSpkL, mSpkR;  // the speaker - everything
	double mSpeakerMakeupDb = 0.0;
	double mWarpLpL = 0.0, mWarpLpR = 0.0, mWarpLpCoef = 1.0;
	int mCoefCountdown = 0;
};

//------------------------------------------------------------------------
} // namespace WornVinyl
