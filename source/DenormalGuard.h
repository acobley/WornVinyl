//------------------------------------------------------------------------
// WornVinyl - flush-to-zero for the length of one process() call
//------------------------------------------------------------------------
//
// Taken from SpaceDub. Once the input goes silent, recursive state (the
// era's EQ, the half-band oversamplers, the speaker's cabinet echo, the
// warp's low-pass) does not decay to zero: it sinks into the denormal
// range and stays there, and denormal arithmetic is slow on x86.
// Measured in WornVinyl (Nov 2026, x86-64, 10 s of silence after music,
// transport stopped): 2.9 s of CPU without the guard, 0.15 s with it;
// 5.3 s against 0.23 s with the Horn speaker on.
//
// THIS GUARD IS THE ONLY PROTECTION. Nothing in WornVinylDsp flushes its
// own state - no filter adds DC, no state is tested against a floor - so
// every float and double operation on the audio thread must run inside
// it. DspTests (testDenormals) proves the state comes out clean with it,
// and denormal without it.
//
// The FPU mode belongs to the host's thread, so it is saved on entry and
// put back on exit - never left switched on.
//
//   x86_64 : MXCSR FTZ (bit 15) and DAZ (bit 6)
//   arm64  : FPCR FZ (bit 24), which flushes inputs and outputs both
//
// Standard library only, so tests/DspTests.cpp can check it without
// the VST3 SDK.
//------------------------------------------------------------------------

#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__SSE2__)
#include <xmmintrin.h>
#define WORNVINYL_FTZ_X86 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define WORNVINYL_FTZ_ARM64 1
#endif

namespace WornVinyl {

class ScopedFlushDenormals
{
public:
	ScopedFlushDenormals ()
	{
#if defined(WORNVINYL_FTZ_X86)
		mSaved = _mm_getcsr ();
		_mm_setcsr (static_cast<unsigned> (mSaved) | 0x8040u);
		compilerBarrier ();
#elif defined(WORNVINYL_FTZ_ARM64)
		std::uint64_t fpcr;
		asm volatile ("mrs %0, fpcr" : "=r" (fpcr) : : "memory");
		mSaved = fpcr;
		asm volatile ("msr fpcr, %0" : : "r" (fpcr | (std::uint64_t (1) << 24)) : "memory");
#endif
	}

	~ScopedFlushDenormals ()
	{
#if defined(WORNVINYL_FTZ_X86)
		compilerBarrier ();
		_mm_setcsr (static_cast<unsigned> (mSaved));
#elif defined(WORNVINYL_FTZ_ARM64)
		asm volatile ("msr fpcr, %0" : : "r" (mSaved) : "memory");
#endif
	}

	/** False on an architecture the guard does not know: it then does
	    nothing, and nothing else flushes. */
	static constexpr bool supported ()
	{
#if defined(WORNVINYL_FTZ_X86) || defined(WORNVINYL_FTZ_ARM64)
		return true;
#else
		return false;
#endif
	}

	ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
	ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

private:
	/** THE MODE SWITCH IS INVISIBLE TO THE OPTIMISER. Nothing in C++ ties
	    a multiply to the FPU control register, so without a barrier the
	    compiler may move arithmetic across the switch - measured on arm64
	    at -O2: a multiply written inside the guard was scheduled after
	    the restore and came out denormal. The "memory" clobbers (and this
	    barrier beside _mm_setcsr) pin every load and store, and so every
	    result that reaches memory, to its own side of the switch. */
	static void compilerBarrier ()
	{
#if defined(__GNUC__) || defined(__clang__)
		asm volatile ("" : : : "memory");
#endif
	}

	std::uint64_t mSaved = 0;
};

} // namespace WornVinyl
