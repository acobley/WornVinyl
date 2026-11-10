//------------------------------------------------------------------------
// WornVinyl - plug-in factory
//------------------------------------------------------------------------

#include "WornVinylController.h"
#include "WornVinylIDs.h"
#include "WornVinylProcessor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#if SMTG_OS_MACOS
#include <dlfcn.h>
#endif

// The DISPLAY name - what a host lists. PLUGIN_NAME in CMakeLists.txt is
// "WornVinyl", a plain identifier for the target and bundle.
#define stringPluginName "Worn Vinyl"

using namespace Steinberg::Vst;
using namespace WornVinyl;

//------------------------------------------------------------------------
// macOS: never unload this binary (MasterLoud's fix, kept)
//
// Steinberg's AU wrapper (and many VST3 hosts) unload the plug-in's binary
// with CFBundleUnloadExecutable when the last instance goes. dyld really
// does unmap it, and the static destructors dispose of VSTGUI's run-time
// NSView class; AppKit still frees the editor's views later, and a view
// whose class (and code) is gone crashes it: -[NSView dealloc] ->
// _removeFromKeyViewLoop -> objc_msgSend on 0x0.
//
// RTLD_NODELETE on our own image makes dlclose a no-op. bundleExit still
// runs, but the classes and the code stay where AppKit can reach them
// until the host quits.
//------------------------------------------------------------------------
#if SMTG_OS_MACOS
namespace {
struct PinImage
{
	PinImage ()
	{
		Dl_info info {};
		if (dladdr (reinterpret_cast<const void*> (&pinMarker), &info) != 0 && info.dli_fname)
			dlopen (info.dli_fname, RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE);
		// the handle is deliberately leaked: it is the pin
	}
	static void pinMarker () {}
};
const PinImage gPinImage;
} // namespace
#endif

//------------------------------------------------------------------------
BEGIN_FACTORY_DEF (stringCompanyName, "https://github.com/", "mailto:aecobley@googlemail.com")

	// An EFFECT: stereo in, stereo out, no event input. Fx|Distortion is
	// where hosts file lo-fi and character effects; Fx|Restoration would
	// put it among the de-clickers, which is the opposite of what it does.
	DEF_CLASS2 (INLINE_UID_FROM_FUID (kWornVinylProcessorUID),
	            PClassInfo::kManyInstances,
	            kVstAudioEffectClass,
	            stringPluginName,
	            Vst::kDistributable,
	            PlugType::kFxDistortion,
	            FULL_VERSION_STR,
	            kVstVersionString,
	            WornVinylProcessor::createInstance)

	DEF_CLASS2 (INLINE_UID_FROM_FUID (kWornVinylControllerUID),
	            PClassInfo::kManyInstances,
	            kVstComponentControllerClass,
	            stringPluginName " Controller",
	            0,
	            "",
	            FULL_VERSION_STR,
	            kVstVersionString,
	            WornVinylController::createInstance)

END_FACTORY
