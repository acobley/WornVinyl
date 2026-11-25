//------------------------------------------------------------------------
// WornVinyl - audio processor
//
// Host plumbing only. The audio line itself is in WornVinylDsp.h and
// knows nothing about VST3.
//------------------------------------------------------------------------

#pragma once

#include "WornVinylDsp.h"
#include "WornVinylParams.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <atomic>
#include <vector>

namespace WornVinyl {

//------------------------------------------------------------------------
class WornVinylProcessor : public Steinberg::Vst::AudioEffect
{
public:
	WornVinylProcessor ();
	~WornVinylProcessor () SMTG_OVERRIDE = default;

	static Steinberg::FUnknown* createInstance (void*)
	{
		return (Steinberg::Vst::IAudioProcessor*)new WornVinylProcessor;
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs,
	                                                  Steinberg::int32 numIns,
	                                                  Steinberg::Vst::SpeakerArrangement* outputs,
	                                                  Steinberg::int32 numOuts) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	Steinberg::uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE;
	Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE;

private:
	/** Takes the LAST point of each parameter's change list - a block-rate
	    read. Every continuous control is smoothed per sample inside the
	    DSP, so nothing here needs to be sample-accurate. */
	void applyParameterChanges (Steinberg::Vst::IParameterChanges* changes);

	/** The audio settings, to the controller for the About box. From
	    setActive (UI thread) only - never from process(). */
	void sendAudioSettings ();

	/** Push mParams into the DSP, in PLAIN units. */
	void pushParameters ();

	/** The disc's angle and the SKIP lamp, as output parameter changes.
	    Called from process(), which is why it is not a message. */
	void publishOutputs (Steinberg::Vst::IParameterChanges* out);

	Dsp mDsp;

	/** Normalised parameter values, indexed by Param. Written by the audio
	    thread (applyParameterChanges) and by setState on the UI thread -
	    which hands them over through mPending, never directly. */
	std::vector<double> mParams;
	bool mBypass = false;

	/** setState (UI thread) parks the loaded values here and raises the
	    flag; process() takes them, pushes them and snaps the smoothers, so
	    the DSP is only ever touched on the audio thread. */
	std::vector<double> mPending;
	bool mPendingBypass = false;
	std::atomic<bool> mStatePending { false };

	/** Between setActive (true) and setActive (false). */
	std::atomic<bool> mActive { false };

	double mSampleRate = 44100.0;

	/** 64-bit processing goes through these. The line is float, but a
	    64-bit host is accepted rather than refused. Sized in
	    setupProcessing so the audio thread never allocates. */
	std::vector<float> mScratchIn[2];
	std::vector<float> mScratchOut[2];
};

//------------------------------------------------------------------------
} // namespace WornVinyl
