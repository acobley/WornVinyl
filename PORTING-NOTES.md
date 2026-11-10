# Worn Vinyl — project notes

Stereo VST3 + AUv2 effect that plays a track back off a worn record: the
decade's playback chain (1920s–1970s, shellac or vinyl), scratches with the
needle sticking and skipping at high settings, dust (crackle, hiss, rumble),
wow, flutter and warp. Nothing is ported: a new plug-in scaffolded from
`~/DXi-DEv/vst3-port-template` with the `blank-vst3-plugin` skill on
10 Nov 2026, with the editor written straight away in R2Synth's black
("Polivoks") panel and Andy's 70s sci-fi lettering.

**Status:** all source written; DSP, layout and parameter suites pass in the
VM; every `.cpp` compiles to an object against the VST3 SDK 3.8.1 headers
and every `WornVinyl::` symbol referenced is defined. **Not yet built on the
Mac** — cmake, Xcode, the validator and `auval` cannot run from the session's
Linux VM. See *Verification* for what to run first.

---

## 1. The four scaffold decisions

| Decision | Choice |
|---|---|
| Effect or instrument | Effect (`aufx`), `PlugType::kFxDistortion` (where hosts file lo-fi / character effects), no event input |
| Bus layout | Stereo in / stereo out only. `setBusArrangements` accepts 2/2 and nothing else; `SupportedNumChannels` in `resource/au-info.plist` cut to the single 2/2 entry |
| Name and codes | See §2 |
| Editor now or later | **Now** — the look was part of the request, and an AU from this template must never ship without an editor anyway (MasterLoud §5a: the SDK 3.8.1 AU wrapper crashes the host when `createView` returns null) |

## 2. Identity — permanent once a build ships

| Field | Value |
|---|---|
| Display name | Worn Vinyl (factory `stringPluginName`) |
| `PLUGIN_NAME` / namespace | `WornVinyl` |
| Folder | `~/DXi-DEv/WornVinyl-VSTi` |
| Vendor | `AE Cobley` (no full stops) |
| AU name | `AE Cobley: Worn Vinyl` |
| VST3 bundle id | `audio.WornVinyl.vst3` |
| AU bundle id | `audio.WornVinyl.audiounit` |
| AU type / subtype / manufacturer | `aufx` / `WVnl` / `AECo` |
| Processor UID | `0x60BD0A8C, 0x000572B3, 0x73B3540F, 0xD31204EF` |
| Controller UID | `0x9BCDC74F, 0x5BB78CE2, 0x8D7E04B4, 0x39A6E4D6` |
| Version | 1.0.0.0 |

UIDs generated fresh with the skill's `os.urandom` one-liner on 10 Nov 2026.
**Never reuse or change them.**

`WVnl` clash check, 10 Nov 2026 — every `*/resource/au-info.plist` under
`~/DXi-DEv`: FDrm, FTrn, MLud, Prj6, SiSy, SmDr, SDub, SpyB, VcFl. `WVnl` is free.

`CMakeLists.txt`, `setup-xcode.sh` and `.gitignore` are **MasterLoud's**, not
the template's: they are the template plus the later fixes (universal
arm64 + x86_64 by default, `--native`, `-ffile-prefix-map` to keep build paths
out of the binary, installer ignores). Only the identity block was edited.
The template itself should get those fixes too.

## 3. Why the DSP is SDK-free

`WornVinylDsp.{h,cpp}` include no VST3 header. They compile with plain
`c++ -std=c++17`, so the sound is measured before anything is built
(`tests/DspTests.cpp`). And because VST3 splits processor from controller,
everything the panel shows that the DSP decides is **one function both
call**: `effectiveDisc`, `discLocked`, `rpmOf`, `profileOf` /
`bandwidthText`, `scratchSlot` / `activeScratches` (the turntable screen
draws the very scratches the stylus hits), `crackleRate`,
`jumpChancePerSecond`, `warpBump` (the arm lifts where the pitch swoops).

## 4. Parameters

Every row carries normalised, plain and internal ranges; internal == plain
(the slot is empty, not absent). **Append, never insert.**

| Id | Parameter | Range | Notes |
|---|---|---|---|
| 0 | Era | 1920s / 1940s / 1950s / 1960s / 1970s, default 1960s | |
| 1 | Disc | Shellac / Vinyl, default Vinyl | **heard in the 1950s only** — `effectiveDisc`: 1920s/40s shellac, 60s/70s vinyl. The parameter keeps its value, so the 50s remember the choice; the panel shows the switch locked and "SET BY THE ERA" |
| 2 | Scratches | 0–100 %, default 20 | 0 → 12 radial scratches; **jumps above 60 %** |
| 3 | Dust | 0–100 %, default 30 | crackle (rate ∝ dust²), surface hiss, rumble |
| 4 | Wow | 0–100 %, default 15 | once a turn, ±1.2 % at 100 %, plus the motor's 0.13 Hz drift (¼ of it) |
| 5 | Flutter | 0–100 %, default 10 | 7.3 + 11.1 Hz, ±0.4 % at 100 % |
| 6 | Warp | 0–100 %, default 0 | one high spot a turn: a ±4 % swoop, up to −2.5 dB and a dull moment |
| 7 | Jumps | Off / On, default On | may the needle stick |
| 8 | Mix | 0–100 %, default 100 | against the dry signal delayed by the same latency |
| 9 | Output | −12…+12 dB, default 0 | |
| 10 | Disc Angle | 0–1 | **output**, hidden: the turntable screen |
| 11 | Skipping | 0/1 | **output**, hidden: the SKIP lamp (latched in the controller) |
| 12 | Speed | Auto / 33 / 45 / 78, default Auto | **appended 10 Nov 2026**, a late setting (after the outputs). Auto = the disc's own speed (78 shellac, 33⅓ vinyl). Moves only the motor: scratch ticks, wow rate, warp, a stuck groove's length. The music keeps its pitch and tempo (tested) |
| 1000 | Bypass | Off / On | through the DSP, so the latency holds |

**State v1:** `int32 version`, ten `double` settings, `int32 bypass`, then
`int32 speed` (appended without a version bump, so an older build still opens
a newer project; a stream without it is Auto). A short
stream resets what it omits to default; processor and controller read the
same layout. `setState` (UI thread) parks the values and the audio thread
takes them at the top of the next `process()` and snaps the smoothers — the
DSP is never touched from the UI thread.

## 5. The sound

```
input → ring buffer ─(read: 12 ms + wow + drift + flutter + warp + jump offset, Hermite)→
      → mono sum / stereo width of the era
      + hiss (shellac: band-passed 2.5 kHz roar; vinyl: soft and dark), rumble (25 Hz),
        crackle grains (ticks, 10 % pops), scratch ticks (one per scratch per turn)
      → era EQ: 2-pole HP, 4-pole Butterworth LP, two resonances (the 1920s horn)
      → warp: HF loss and level dip at the high spot
      → saturation: (1−d)·x + d·tanh(1.5x)/1.5 — unity small-signal
      → make-up gain (the EQ's mean power 125 Hz–4 kHz put back, capped −3…+9 dB)
      → Mix, Output, Bypass
```

| Era / disc | Band | Width | Notes |
|---|---|---|---|
| 1920s shellac | 180 Hz – 3.2 kHz | mono | horn peaks +6 dB @ 1 kHz, +4 dB @ 2.3 kHz; loudest surface |
| 1940s shellac | 70 Hz – 5.5 kHz | mono | +3 dB @ 2.5 kHz |
| 1950s shellac | 60 Hz – 7 kHz | mono | |
| 1950s vinyl | 40 Hz – 11 kHz | mono | the first LPs |
| 1960s vinyl | 28 Hz – 15 kHz | 0.85 | |
| 1970s vinyl | 20 Hz – 18.5 kHz | 1.0 | quietest surface |

**Speeds:** on Auto, shellac 78 rpm (a turn every 0.77 s), vinyl 33⅓ (1.8 s);
the Speed switch can turn any disc at 33⅓, 45 or 78 (`turntableRpm`). The
scratches, the wow and the warp all follow the turn. Nothing turns slower
than 33⅓, which is what the ring buffer is sized for (tested).

**Jumps.** Only at the deepest scratch (slot 0), only above 60 % with Jumps
on; the chance rises as ((s−60)/40)² / 6 a second (≈ one stick every 6 s at
100 %). A stick jumps the read head back one turn (we hear the last turn
again) 1–4 times, then skips forward to where the music is now — no latency
needed, because the needle only ever goes back first. Each jump is a 6 ms
crossfade plus a stylus-landing thump. The ring buffer holds four LP turns
(≈ 4 MB at 48 k, 16 MB at 192 k), allocated in `setSampleRate` only.

**Deterministic:** our own SplitMix64, re-seeded by `reset()` — the same
audio is the same bounce, at any block size (tested).

## 6. Deliberate omissions, and the trap each one hides

| Omitted | Why | What bites when it is undone |
|---|---|---|
| `processContextRequirements` | The record turns at its own speed | Since VST3 3.7 the context is opt-in; reading tempo without `needTempo ()` silently gives 120 |
| Messages | None needed | A message sent from `process()` is discarded by the host's connection proxy — per-block values go out as the hidden outputs |
| Controller editor list | The editor polls at 30 Hz | In `editorDestroyed`, `dynamic_cast` is null inside `~EditorView()` — compare upcast pointers |
| `restartComponent (kLatencyChanged)` | Latency (12 ms) depends only on the sample rate | If it ever changes while active, the host keeps the old compensation |
| Worn-panel marks (R2Synth's `PanelWear.h`) | Tied to R2Synth's layout | Port it if wanted — it would suit the name |
| Era change crossfade | Wet fades out 8 ms, the chain swaps, fades in 8 ms — a short dip, no click | A crossfade between two chains would need a second EQ pair |

Also seeded: `kBypass` = 1000 with every id range-checked; never a null title
or units to `RangeParameter`; `numSamples == 0` handled; 64-bit hosts via
scratch buffers sized in `setupProcessing`; `RTLD_NODELETE` pin from
MasterLoud in the entry file.

## 7. The editor — design 1, the black panel

![The panel](Designs/WornVinyl-1-BlackPanel.png)

1000 × 480. `WornVinylLayout.h` (VSTGUI-free, read by the tests) has every
rectangle, the selector geometry (now 2, 3, 4 or **5** positions — Era's are
at −90/−45/0/45/90°, Speed's four at −90/−30/30/90°) and the readout formatters; `WornVinylPanelViews.*` has
R2Synth's `Knob` and `RotarySwitch` (wear removed; the switch can be
**locked**), plus `Turntable`, `Lamp`, `TextLine`, `Backdrop`.
`SciFiLettering.h` is R2Synth's, namespace changed only. No bitmaps, no
system font. In public material: "70s sci-fi lettering" only.

The **turntable screen** turns the disc to the published `Disc Angle`; the
label marks show it turning; scratches are drawn at `scratchSlot` phases
and pass under the stylus exactly when they tick; specks follow Dust; the
arm's head grows over the warp's high spot; STUCK shows in red while the
needle is stuck. Era label colours are generic period colours, no company's.

## 8. Verification (VM, 10 Nov 2026)

```sh
tools/run-tests.sh                       # DSP 103, Layout 579, Params 115 — 0 failed (cloud VM and the Mac's VM, with Speed)
SDK=external/vst3sdk   # (in the VM: a clean 3.8.1_build_84 clone)
for f in source/*.cpp; do
  g++ -c -std=c++17 -DLINUX=1 -DRELEASE=1 -I$SDK -I$SDK/vstgui4 -Isource -o /tmp/$(basename $f .cpp).o $f
done
nm -C /tmp/*.o | grep " U " | grep "WornVinyl::"   # all 36 defined in the objects
```

Measured: default patch with a −12 dBFS 1 kHz sine → output peak **−9.1 dBFS**
(−8.5 in the Mac's Linux VM: the crackle peaks; same seed, but another libm's
`exp`/`tanh` round differently, so a bounce is identical on one machine, not
bit-identical across machines), RMS −15.1 (unity). 1 kHz through each era within
±0.5 dB except the 1920s, **+4.3 dB** (the horn's 1 kHz peak; broadband
material comes out near unity). Surface alone at default dust: −42 dBFS RMS
(1920s) to −57 (1970s). Pitch at 100 %: wow 1.52 %, flutter 0.41 %, warp
4.03 %. Worst case (everything 100 %, full-scale noise, +12 dB): finite, read
head never closer than 129 samples. Cost: 1.4 % of one VM core at 48 k with
everything at 100 %.

**On the Mac, first:**

```sh
cd ~/DXi-DEv/WornVinyl-VSTi && ./setup-xcode.sh      # clones the SDK into external/ (in-tree)
# build, then:
validator build/VST3/Release/WornVinyl.vst3
auval -v aufx WVnl AECo
```

## 9. The SDK

In-tree (`external/`, git-ignored), the default: this plug-in may ship on
its own, so it should not depend on another project's checkout.
