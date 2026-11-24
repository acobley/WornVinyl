# Worn Vinyl — licence notice

Copyright (C) 2026 A. E. Cobley

Worn Vinyl is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. The full text is in [`LICENSE`](LICENSE).

Worn Vinyl is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

SPDX-License-Identifier: GPL-3.0-or-later

## Additional term under section 7 of the GPL

Under section 7(e) of the GNU General Public License version 3, this licence
does **not** grant any right to use the name "Worn Vinyl", or any associated
logo or trade dress, to identify a modified version. If you distribute a
modified version, give it a different name, so that nobody mistakes it for the
official build or asks the author to support it. You may still say truthfully
that your version is based on Worn Vinyl.

## Third-party components

These are not covered by the copyright above and keep their own licences,
all of which are compatible with GPL version 3:

| Component | Where | Licence |
|---|---|---|
| Steinberg VST 3 SDK | fetched into `external/vst3sdk` | MIT |
| VSTGUI | inside the VST 3 SDK | BSD 3-Clause |
| Apple AudioUnitSDK | fetched into `external/AudioUnitSDK` | Apache 2.0 |

VST is a registered trademark of Steinberg Media Technologies GmbH. Audio
Units is a trademark of Apple Inc.

## Credits

- The oversampling's half-band filters are designed with the formulae of
  Laurent de Soras' hiir library (WTFPL), reimplemented in
  `source/WornVinylDsp.cpp` (`HalfBand`).
- The biquad filters follow Robert Bristow-Johnson's *Audio EQ Cookbook*.
- The 70s sci-fi lettering is the author's own stroke face: there is no font
  file and no font licence.
