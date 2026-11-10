//------------------------------------------------------------------------
// WornVinyl - audio processor implementation
//------------------------------------------------------------------------

#include "WornVinylProcessor.h"
#include "WornVinylIDs.h"
#include "DenormalGuard.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace WornVinyl {

//------------------------------------------------------------------------
WornVinylProcessor::WornVinylProcessor ()
{
	setControllerClass (kWornVinylControllerUID);

	// NO processContextRequirements. Since VST3 3.7 the ProcessContext is
	// opt-in and the default is no flags - which is right today: the
	// record turns at its own speed, not the song's. If anything ever syncs
	// to the transport (a jump only while playing, say), add
	//     processContextRequirements.needTransportState ();
	// and for the beat needTempo () - without it tempo silently reads 120
	// in every host and the validator prints "- None" rather than
	// complaining.

	mParams.assign (kNumParams, 0.0);
	for (ParamID id = 0; id < kNumParams; ++id)
		mParams[id] = kParams[id].defaultNormalized ();
	mPending = mParams;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylProcessor::initialize (FUnknown* context)
{
	const tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	addAudioInput (STR16 ("Stereo In"), SpeakerArr::kStereo);
	addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);

	// No event input. This is an EFFECT: nothing here reads a MIDI queue.

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylProcessor::terminate ()
{
	return AudioEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylProcessor::setActive (TBool state)
{
	mActive.store (state != 0);
	if (state)
	{
		pushParameters ();
		mDsp.snapParameters ();
		mDsp.reset ();
	}
	return AudioEffect::setActive (state);
}

//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylProcessor::setupProcessing (ProcessSetup& setup)
{
	mSampleRate = setup.sampleRate;

	// UI thread, not the audio thread: the only place buffers are sized -
	// the disc's ring buffer (four turns of an LP, ~4 MB at 48 k) and the
	// 64-bit scratch buffers.
	mDsp.setSampleRate (mSampleRate);

	const std::size_t maxFrames =
		static_cast<std::size_t> (std::max<int32> (setup.maxSamplesPerBlock, 1));
	for (int c = 0; c < 2; ++c)
	{
		mScratchIn[c].assign (maxFrames, 0.0f);
		mScratchOut[c].assign (maxFrames, 0.0f);
	}

	return AudioEffect::setupProcessing (setup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
		return kResultTrue;
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylProcessor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                           SpeakerArrangement* outputs, int32 numOuts)
{
	// Stereo in, stereo out, and nothing else. Whatever is accepted here
	// MUST match the SupportedNumChannels list in resource/au-info.plist
	// (2/2 only) or hosts will not offer the layout and auval will say so.
	// A mono track goes in on a stereo bus - the old eras sum to mono
	// anyway.
	if (numIns == 1 && numOuts == 1 &&
	    inputs[0] == SpeakerArr::kStereo && outputs[0] == SpeakerArr::kStereo)
	{
		return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	}
	return kResultFalse;
}

//------------------------------------------------------------------------
void WornVinylProcessor::applyParameterChanges (IParameterChanges* changes)
{
	if (changes == nullptr)
		return;

	const int32 count = changes->getParameterCount ();
	for (int32 i = 0; i < count; ++i)
	{
		IParamValueQueue* queue = changes->getParameterData (i);
		if (queue == nullptr)
			continue;

		const int32 points = queue->getPointCount ();
		if (points <= 0)
			continue;

		int32 sampleOffset = 0;
		ParamValue value = 0.0;
		if (queue->getPoint (points - 1, sampleOffset, value) != kResultTrue)
			continue;

		const ParamID id = queue->getParameterId ();

		// RANGE-CHECK before indexing: kBypass is 1000 and is not in the
		// table. The outputs are ours to write, never the host's.
		if (id == kBypass)
			mBypass = (value >= 0.5);
		else if (isTableParam (id) && ! isOutputParam (id))
			mParams[id] = std::min (1.0, std::max (0.0, value));
	}
}

//------------------------------------------------------------------------
void WornVinylProcessor::pushParameters ()
{
	// The DSP is handed PLAIN units, so nothing in WornVinylDsp has to know
	// what a ParamID is. The table is the only place the two meet.
	const double* p = mParams.data ();
	mDsp.setEra (static_cast<int> (kParams[kEra].toInternal (p[kEra])));
	mDsp.setDisc (static_cast<int> (kParams[kDisc].toInternal (p[kDisc])));
	mDsp.setScratchesPct (plainValue (p, kScratches));
	mDsp.setDustPct (plainValue (p, kDust));
	mDsp.setWowPct (plainValue (p, kWow));
	mDsp.setFlutterPct (plainValue (p, kFlutter));
	mDsp.setWarpPct (plainValue (p, kWarp));
	mDsp.setJumps (kParams[kJumps].toInternal (p[kJumps]) >= 0.5);
	mDsp.setMixPct (plainValue (p, kMix));
	mDsp.setOutputDb (plainValue (p, kOutput));

	// Bypass goes through the DSP too, so the latency holds: a straight
	// copy would jump the track 12 ms early against the others.
	mDsp.setBypass (mBypass);
}

//------------------------------------------------------------------------
void WornVinylProcessor::publishOutputs (IParameterChanges* out)
{
	if (out == nullptr)
		return;

	auto publish = [out] (ParamID id, double normalized) {
		int32 index = 0;
		if (IParamValueQueue* queue = out->addParameterData (id, index))
		{
			int32 point = 0;
			queue->addPoint (0, std::min (1.0, std::max (0.0, normalized)), point);
		}
	};
	publish (kDiscAngle, mDsp.discAngle ());
	publish (kSkipping, mDsp.skipping () ? 1.0 : 0.0);
}

//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylProcessor::process (ProcessData& data)
{
	// Flush-to-zero for this call only; the host's FPU mode comes back on
	// return. See DenormalGuard.h.
	ScopedFlushDenormals noDenormals;

	// A state load: take the values setState parked, and jump the
	// smoothers rather than ramp from the previous project's settings.
	if (mStatePending.exchange (false))
	{
		mParams = mPending;      // same size, so no allocation
		mBypass = mPendingBypass;
		pushParameters ();
		mDsp.snapParameters ();
	}

	applyParameterChanges (data.inputParameterChanges);
	pushParameters ();

	// A parameter-only block. Legal, and common when a host is drawing
	// automation while the transport is stopped.
	if (data.numSamples <= 0 || data.numInputs <= 0 || data.numOutputs <= 0)
		return kResultOk;

	AudioBusBuffers& in  = data.inputs[0];
	AudioBusBuffers& out = data.outputs[0];
	const int32 frames = data.numSamples;

	if (in.numChannels < 2 || out.numChannels < 2)
		return kResultOk;

	// Never declared silent: a silent input still has a surface under it,
	// and the delay line is still emptying.
	out.silenceFlags = 0;

	if (data.symbolicSampleSize == kSample32)
	{
		float* inL  = in.channelBuffers32[0];
		float* inR  = in.channelBuffers32[1];
		float* outL = out.channelBuffers32[0];
		float* outR = out.channelBuffers32[1];
		if (! inL || ! inR || ! outL || ! outR)
			return kResultOk;

		mDsp.process (inL, inR, outL, outR, frames);
		publishOutputs (data.outputParameterChanges);
		return kResultOk;
	}

	if (data.symbolicSampleSize == kSample64)
	{
		double* inL  = in.channelBuffers64[0];
		double* inR  = in.channelBuffers64[1];
		double* outL = out.channelBuffers64[0];
		double* outR = out.channelBuffers64[1];
		if (! inL || ! inR || ! outL || ! outR)
			return kResultOk;

		// setupProcessing sized these to maxSamplesPerBlock; a host that
		// exceeds its own declared maximum gets a pass-through rather
		// than an allocation on the audio thread.
		const std::size_t need = static_cast<std::size_t> (frames);
		if (mScratchIn[0].size () < need || mScratchOut[0].size () < need)
		{
			if (outL != inL)
				std::memcpy (outL, inL, need * sizeof (double));
			if (outR != inR)
				std::memcpy (outR, inR, need * sizeof (double));
			return kResultOk;
		}

		for (int32 i = 0; i < frames; ++i)
		{
			mScratchIn[0][i] = static_cast<float> (inL[i]);
			mScratchIn[1][i] = static_cast<float> (inR[i]);
		}

		mDsp.process (mScratchIn[0].data (), mScratchIn[1].data (),
		              mScratchOut[0].data (), mScratchOut[1].data (), frames);

		for (int32 i = 0; i < frames; ++i)
		{
			outL[i] = mScratchOut[0][i];
			outR[i] = mScratchOut[1][i];
		}
		publishOutputs (data.outputParameterChanges);
		return kResultOk;
	}

	return kResultOk;
}

//------------------------------------------------------------------------
uint32 PLUGIN_API WornVinylProcessor::getLatencySamples ()
{
	// The read head's resting delay (12 ms), which gives wow, flutter and
	// warp room to swing either side. It depends only on the sample rate,
	// which a host changes only while the plug-in is INACTIVE, so it never
	// changes while processing and needs no restartComponent
	// (kLatencyChanged). The JUMPS are not latency: a stuck needle plays
	// the last turn again and then skips on, as a record does.
	return static_cast<uint32> (mDsp.latencySamples ());
}

//------------------------------------------------------------------------
uint32 PLUGIN_API WornVinylProcessor::getTailSamples ()
{
	// The delay line. (The surface noise never stops while audio runs, but
	// a host asking for the tail wants the music to finish, not the hiss.)
	return static_cast<uint32> (mDsp.tailSamples ());
}

//------------------------------------------------------------------------
// STATE LAYOUT, version 1 - WornVinylController::setComponentState reads
// the identical layout:
//
//     int32   version
//     double  normalised value of each SETTING, ids 0 .. kNumStoredParams-1
//     int32   bypass
//
// A setting appended later goes AFTER the bypass, read as "absent" (and so
// its default) by a short stream.
//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylProcessor::setState (IBStream* state)
{
	if (state == nullptr)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);

	int32 version = 0;
	if (! streamer.readInt32 (version))
		return kResultFalse;
	if (version > kStateVersion)
		return kResultFalse;

	// Anything a SHORT stream does not mention goes back to its DEFAULT,
	// or loading an old project after a new one would inherit the new
	// one's settings.
	std::vector<double> loaded (kNumParams, 0.0);
	for (ParamID id = 0; id < kNumParams; ++id)
		loaded[id] = kParams[id].defaultNormalized ();
	bool bypass = false;

	double value = 0.0;
	for (ParamID id = 0; id < kNumStoredParams; ++id)
	{
		if (! streamer.readDouble (value))
			break;
		loaded[id] = std::min (1.0, std::max (0.0, value));
	}
	int32 b = 0;
	if (streamer.readInt32 (b))
		bypass = (b != 0);

	// Handed to the audio thread, which applies it at the top of the next
	// process() - the DSP is never touched from here. (Two loads faster
	// than one audio block would race on mPending; the second wins, which
	// is what was asked for.)
	mPending = loaded;
	mPendingBypass = bypass;
	mStatePending.store (true);

	// Not processing yet (a load before activation): apply now too, so
	// getState straight after returns what was loaded.
	if (! mActive.load ())
	{
		mParams = loaded;
		mBypass = bypass;
	}
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WornVinylProcessor::getState (IBStream* state)
{
	if (state == nullptr)
		return kResultFalse;

	IBStreamer streamer (state, kLittleEndian);
	streamer.writeInt32 (kStateVersion);

	// A load not yet taken by the audio thread is what the project now
	// holds, so it is what is saved.
	const std::vector<double>& source = mStatePending.load () ? mPending : mParams;
	for (ParamID id = 0; id < kNumStoredParams; ++id)
		streamer.writeDouble (source[id]);
	streamer.writeInt32 ((mStatePending.load () ? mPendingBypass : mBypass) ? 1 : 0);
	return kResultOk;
}

//------------------------------------------------------------------------
} // namespace WornVinyl
