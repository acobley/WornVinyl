//------------------------------------------------------------------------
// WornVinyl - class UIDs and message identifiers
//
// These UIDs were generated fresh for this plug-in from os.urandom on
// 10 Nov 2026. NEVER CHANGE THEM once a build has been shipped: hosts store
// them in the project file, so a changed UID means every existing session
// silently loses the plug-in.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace WornVinyl {

static const Steinberg::FUID kWornVinylProcessorUID  (0x60BD0A8C, 0x000572B3, 0x73B3540F, 0xD31204EF);
static const Steinberg::FUID kWornVinylControllerUID (0x9BCDC74F, 0x5BB78CE2, 0x8D7E04B4, 0x39A6E4D6);

// The plug-in category is declared once, in WornVinylEntry.cpp, using the
// SDK's own PlugType - not repeated as a string here.

//------------------------------------------------------------------------
// Processor <-> controller messages
//
// ONE: the audio settings, for the About box (added 25 Nov 2026, as
// SpaceDub's). Sent from setActive, when the host has just set them.
//
// Every message travels on the UI THREAD. sendMessage from process()
// returns success and is then silently discarded by the host's connection
// proxy. What the DSP produces per block - the disc's angle for the
// turntable screen, whether the needle is stuck - goes out through
// data.outputParameterChanges instead (the hidden read-only parameters in
// WornVinylParams.h). Messages are fine from setActive, setState and
// notify.
//------------------------------------------------------------------------


/** Processor -> controller, from setActive (UI thread): how the host is
    running us. The controller keeps them for the About box. */
static const char* const kAudioSettingsMessage      = "WornVinylAudioSettings";
static const char* const kSampleRateAttribute       = "sampleRate";
static const char* const kMaxBlockAttribute         = "maxBlock";
static const char* const kSampleBitsAttribute       = "sampleBits";
static const char* const kProcessModeAttribute      = "processMode";
static const char* const kChannelsAttribute         = "channels";

//------------------------------------------------------------------------
} // namespace WornVinyl
