# Worn Vinyl 1.0.0.0

Your track played back off a worn record. A stereo effect for macOS, as a
**VST3** and an **Audio Unit**. Free software under the GNU GPL, version 3 or
later. The first release.

## What it is

- **The decade's sound.** Era picks the 1920s, 1940s, 1950s, 1960s or 1970s:
  the recording chain's bandwidth (180 Hz – 3.2 kHz for an acoustic 78, up to
  20 Hz – 18.5 kHz for a 1970s LP), its resonances, the cutting lathe's
  saturation, and mono or stereo. Disc chooses shellac or vinyl in the 1950s.
- **The disc turns**, at 78 or 33⅓ rpm or whatever Speed sets, and its faults
  come round once a turn: **Scratches** (the needle can stick and skip above
  60 %), **Dust** (crackle, hiss, rumble), **Wow**, **Flutter** and **Warp**.
- **Player**: the surface noise as a player of the period heard it, or as a
  modern turntable hears it.
- **Speaker**: what it all comes out of — a 1920s horn gramophone, a 1940s
  2-inch speaker, a 1950s 3-inch, a 1960s 6-inch with a tweeter, a 1970s
  hi-fi 3-way, or Off (the line out).
- **Mix** and **Output**, and a turntable screen showing the disc, its
  scratches and the needle.

## Installing

Download `WornVinyl-1.0.0.0.pkg` and open it. The installer offers each format
separately:

    /Library/Audio/Plug-Ins/VST3/WornVinyl.vst3
    /Library/Audio/Plug-Ins/Components/WornVinyl.component

Quit your DAW first - a running host holds the old copy open. Signed with a
Developer ID and notarised by Apple, so it installs without any
Privacy & Security detour.

Requires **macOS 12 or later**; runs natively on Apple silicon and Intel Macs.

## Things that will look like bugs and are not

* **The record turns only while the song plays.** Stopped, the crackle, wow
  and flutter stop with it; the music still passes so a track can be
  monitored. Every start and loop sets the disc from the song position, so the
  same passage gets the same scratches every time.
* **A latency test reads a little more or less than 12 ms.** The plug-in
  reports 12 ms (576 samples at 48 kHz) and the host lines the track up by it.
  Wow and flutter move the timing either side of that - that is what wow is -
  so a single measurement lands anywhere in about ±1 ms. With Wow, Flutter and
  Warp at 0 it measures 12 ms.
* **The older speakers are mono**, as one speaker in one cabinet is. Only the
  1970s 3-way plays in stereo.
* **The Disc switch is locked outside the 1950s.** The 1920s and 1940s were
  shellac and the 1960s and 1970s vinyl; only the 1950s had both.

## Known gaps

* **No factory presets yet.**

## Uninstalling

A `.pkg` never will, so:

```sh
sudo rm -rf /Library/Audio/Plug-Ins/VST3/WornVinyl.vst3
sudo rm -rf /Library/Audio/Plug-Ins/Components/WornVinyl.component
```

## Checksum

```
SHA-256: a20503fdea1ffc5005ca5edcc68b5b2fab869c63a6c47ff8d6202972f6becdd4
```

---

Copyright 2026 A. E. Cobley. GNU GPL v3 or later; see `LICENSE` and `NOTICE.md`.
VST is a registered trademark of Steinberg Media Technologies GmbH.
