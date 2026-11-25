# Worn Vinyl 1.1.0.0

Your track played back off a worn record. A stereo effect for macOS, as a
**VST3** and an **Audio Unit**. Free software under the GNU GPL, version 3 or
later.

## What changed since 1.0.0.0

- **An About box.** Click the **WORN VINYL** logo for the version, the exact
  build, whether it is running as a VST3 or an Audio Unit, the host, the
  audio settings (sample rate, channels, block size, realtime or offline),
  and the folder it was loaded from. Click anywhere, or press Esc, to close
  it.
- **Dust and Warp turned down to 0 now switch fully off.** In 1.0.0.0 a
  control turned down to 0 never quite reached it. Nothing could be heard,
  but the crackle generator kept running underneath. That changed where the
  scratches and skips fell: a passage played after Dust was turned down to 0
  could skip in different places from the same passage after the project was
  reopened, or in a bounce. Now they match. With the same fix, the surface
  noise stops running in the background once it has faded out after the
  transport stops.

**Projects and presets from 1.0.0.0 open unchanged.** No parameters were
added or removed, and the sound is the same.

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

Download `WornVinyl-1.1.0.0.pkg` and open it. The installer offers each format
separately:

    /Library/Audio/Plug-Ins/VST3/WornVinyl.vst3
    /Library/Audio/Plug-Ins/Components/WornVinyl.component

It installs over 1.0.0.0. Quit your DAW first - a running host holds the old
copy open. Signed with a Developer ID and notarised by Apple, so it installs
without any Privacy & Security detour.

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
* **Under Logic, the About box's host may read "out-of-process Audio Unit
  service".** Logic, GarageBand and MainStage can run an Audio Unit in a
  separate helper process, and that helper is what the plug-in finds itself
  in.

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
SHA-256: d3b3f1f33d52d268b07bfd230b85b6ecab984e5914e996064fbb8ddeaf721005
```

---

Copyright 2026 A. E. Cobley. GNU GPL v3 or later; see `LICENSE` and `NOTICE.md`.
VST is a registered trademark of Steinberg Media Technologies GmbH.
