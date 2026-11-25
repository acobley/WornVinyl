#!/usr/bin/env python3
"""Draw the plug-in's panel into docs/panel.png.

WHY IT DOES NOT PARSE THE SOURCE (R2Synth's approach). This script asks
the plug-in: tools/dump-layout.cpp is compiled against
source/WornVinylLayout.h, WornVinylParams.cpp and WornVinylDsp.cpp and
prints every position, every piece of text, the turntable's scratches and
specks and every glyph of the lettering as JSON - from the SAME code the
editor runs. This script only draws, as WornVinylPanelViews.cpp does. A
layout change that breaks the tool fails to compile rather than drawing
last week's panel.

What it cannot show is VSTGUI's own rendering: this is a reconstruction
from the same numbers in the panel's palette (WornVinylPanelViews.h), not
a screenshot. It exists so that a control in the wrong place HERE is a
control in the wrong place in the plug-in - the only way to judge a
layout without a host. The panel is drawn at its defaults, the disc at
angle 0, as a fresh instance opens.

    python3 tools/render-panel.py [out.png]
    python3 tools/render-panel.py --about [out.png]   # with the About box open

Needs a C++ compiler, Pillow, and the VST3 SDK for one header-only file:
external/vst3sdk (the in-tree clone), or VST3_SDK_ROOT in the environment.
"""

import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
SCALE = 2          # the PNG is drawn at 2x so the text is legible
SS = 4             # ...from a 4x drawing, for smooth curves


# ---------------------------------------------------------------------------
# Ask the plug-in
# ---------------------------------------------------------------------------
def dump_layout():
    sdk = os.environ.get('VST3_SDK_ROOT') or os.path.join(ROOT, 'external', 'vst3sdk')
    if not os.path.exists(os.path.join(sdk, 'pluginterfaces', 'vst', 'vsttypes.h')):
        raise SystemExit('render-panel.py: no VST3 SDK at %s - run ./setup-xcode.sh once, '
                         'or set VST3_SDK_ROOT' % sdk)
    cxx = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
    if not cxx:
        raise SystemExit('render-panel.py: no C++ compiler. Refusing to draw the panel '
                         'from a second copy of its numbers.')
    src = os.path.join(ROOT, 'source')
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, 'dump-layout')
        subprocess.run([cxx, '-std=c++17', '-O1', '-I' + sdk, '-I' + src, '-o', exe,
                        os.path.join(ROOT, 'tools', 'dump-layout.cpp'),
                        os.path.join(src, 'WornVinylParams.cpp'),
                        os.path.join(src, 'WornVinylDsp.cpp')], check=True)
        out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout
    return json.loads(out)


# ---------------------------------------------------------------------------
# The palette - WornVinylPanelViews.h
# ---------------------------------------------------------------------------
GROUND = (18, 18, 18)
HEADER = (12, 12, 12)
HEADER_RULE = (233, 230, 220)
SECTION = (21, 21, 21)
SECTION_EDGE = (217, 214, 204)
INK = (244, 241, 232)
INK_SOFT = (185, 182, 174)
INK_DIM = (143, 140, 133)
DIVIDER = (90, 88, 82)
RULE = (58, 58, 58)
TICK = (233, 230, 220)
RED = (226, 52, 43)
SCREW = (58, 58, 58)
KNOB_BODY = (10, 10, 10)
KNOB_RIM = (47, 47, 47)
BEZEL = (5, 5, 5)
BEZEL_EDGE = (60, 60, 60)
SCREEN = (0, 0, 0)
GRID_DOT = (42, 45, 51)
SCREEN_TEXT = (141, 150, 163)
OLED = (255, 176, 46)
ARM = (200, 204, 210)
LAMP_ON = (255, 59, 47)
LAMP_OFF = (58, 21, 18)


# ---------------------------------------------------------------------------
# Drawing helpers, in panel pixels
# ---------------------------------------------------------------------------
def p(v):
    return v * SS


def pt(x, y):
    return (p(x), p(y))


def from_top(cx, cy, r, deg):
    """The layout's angles: degrees from twelve o'clock, clockwise."""
    t = math.radians(deg)
    return cx + r * math.sin(t), cy - r * math.cos(t)


def vstgui_angle(cx, cy, r, deg):
    """VSTGUI's angles: 0 at three o'clock, clockwise (y down)."""
    t = math.radians(deg)
    return cx + r * math.cos(t), cy + r * math.sin(t)


def stroke(d, pts, colour, width):
    """A polyline with round caps and joins."""
    if len(pts) == 1:
        pts = pts * 2
    d.line([pt(x, y) for x, y in pts], fill=colour, width=max(1, round(p(width))))
    rr = width / 2
    for x, y in pts:
        d.ellipse([p(x - rr), p(y - rr), p(x + rr), p(y + rr)], fill=colour)


def disc(d, cx, cy, radius, fill=None, outline=None, width=1.0):
    d.ellipse([p(cx - radius), p(cy - radius), p(cx + radius), p(cy + radius)], fill=fill, outline=outline,
              width=max(1, round(p(width))) if outline else 0)


def ring(d, cx, cy, radius, colour, width):
    """A stroked circle centred on its radius, as VSTGUI strokes one."""
    disc(d, cx, cy, radius + width / 2, outline=colour, width=width)


def round_rect(d, r, radius, fill=None, outline=None, width=1.0):
    d.rounded_rectangle([p(r[0]), p(r[1]), p(r[2]), p(r[3])], radius=p(radius), fill=fill, outline=outline,
                        width=max(1, round(p(width))) if outline else 0)


def quad(p0, q, p2, steps=8):
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * q[0] + t * t * p2[0],
             (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * q[1] + t * t * p2[1])
            for t in (i / steps for i in range(1, steps + 1))]


def blend(c1, c2, t):
    return tuple(int(round(a + (b - a) * t)) for a, b in zip(c1, c2))


def layer(img):
    """A clear layer the size of the panel, and something to draw on it."""
    o = Image.new('RGBA', img.size, (0, 0, 0, 0))
    return o, ImageDraw.Draw(o)


# ---------------------------------------------------------------------------
# The lettering - the glyphs as dump-layout printed them (SciFiLettering.h)
# ---------------------------------------------------------------------------
L = None


def code_points(text):
    out = []
    for ch in text:
        c = ord(ch)
        if ord('a') <= c <= ord('z'):
            c -= 32
        elif 0xC0 <= c <= 0xFF and c not in (0xD7, 0xF7):
            c = ord('Y') if c == 0xFF else ord('AAAAAAACEEEEIIIIDNOOOOO*OUUUUYTS'[(c & ~0x20) - 0xC0])
        out.append(c)
    return out


def glyph(c):
    return L['glyphs'].get(str(c)) or L['glyphs']['missing']


def placed(text, style):
    x, out = 0.0, []
    for c in code_points(text):
        if c == 32:
            x += L['space']
            continue
        out.append((c, x))
        x += glyph(c)[0] + style['weight'] + style['track']
    return out, x


def text_width(text, style):
    _, end = placed(text, style)
    return (end - style['track']) * style['cap'] / 6 if end > 0 else 0.0


def lettering(d, text, style, colour, x, cy, align='left', stroke_units=None):
    s = style['cap'] / 6
    w = text_width(text, style)
    left = x - w if align == 'right' else (x - w / 2 if align == 'centre' else x)
    top = cy - (style['cap'] + style['weight'] * s) / 2
    half = style['weight'] / 2
    lw = (stroke_units or style['weight']) * s
    glyphs, _ = placed(text, style)
    for c, gx in glyphs:
        ox, oy = left + (gx + half) * s, top + half * s
        at = lambda u, v: (ox + u * s, oy + v * s)
        line, start = [], None
        for op, x1, y1, ex, ey in glyph(c)[1]:
            if op == 'M':
                if line:
                    stroke(d, line, colour, lw)
                line = [at(ex, ey)]
                start = line[0]
            elif op == 'L':
                line.append(at(ex, ey))
            elif op == 'Q':
                line += quad(line[-1], at(x1, y1), at(ex, ey))
            elif op == 'Z':
                line.append(start)
        if line:
            stroke(d, line, colour, lw)


# ---------------------------------------------------------------------------
# The panel - Backdrop::draw
# ---------------------------------------------------------------------------
def backdrop(d):
    W, H, HH = L['width'], L['height'], L['headerHeight']
    S = L['styles']
    d.rectangle([0, 0, p(W), p(H)], fill=GROUND)
    d.rectangle([0, 0, p(W), p(HH)], fill=HEADER)
    d.rectangle([0, p(HH - 2), p(W), p(HH)], fill=HEADER_RULE)

    for x, y in L['screws']:
        disc(d, x, y, 3.2, fill=SCREW, outline=(0, 0, 0), width=0.6)
        stroke(d, [(x - 2.2, y - 2.2), (x + 2.2, y + 2.2)], (34, 34, 34), 0.8)

    # WORN VINYL, heavy, with the inline cut
    cy = HH / 2 + 1
    left = L['wordmarkLeft']
    second = left + text_width('WORN', S['wordmark']) + L['wordmarkGap']
    for ink, units in ((INK, None), (HEADER, 0.32)):
        lettering(d, 'WORN', S['wordmark'], ink, left, cy, stroke_units=units)
        lettering(d, 'VINYL', S['wordmark'], ink, second, cy, stroke_units=units)
    dv = L['headerDivider']
    d.rectangle([p(dv), p(16), p(dv + 1), p(40)], fill=DIVIDER)

    for sec in L['sections']:
        r = sec['rect']
        round_rect(d, [r[0] + 0.5, r[1] + 0.5, r[2] - 0.5, r[3] - 0.5], L['sectionRadius'], fill=SECTION,
                   outline=SECTION_EDGE, width=1.2)
        b = sec['titleBox']
        round_rect(d, b, 2, fill=SECTION, outline=SECTION_EDGE, width=1.2)
        c = (b[1] + b[3]) / 2
        lettering(d, sec['title'], S['title'], INK, b[0] + 8, c)
        if sec['subtitle']:
            lettering(d, sec['subtitle'], S['subtitle'], INK_SOFT, sec['subtitleLeft'], c)

    r = L['recordRule']
    d.rectangle([p(r[0]), p(r[1]), p(r[2]) - 1, p(r[3]) - 1], fill=RULE)
    r = L['outputTitle']
    lettering(d, 'OUTPUT', S['block'], INK, r[0], (r[1] + r[3]) / 2)
    n = L['surfaceNote']
    lettering(d, n['text'], S['note'], INK_SOFT, n['rect'][0], (n['rect'][1] + n['rect'][3]) / 2)

    bz = L['screenBezel']
    r = L['turntable']['rect']
    round_rect(d, [r[0] - bz + 0.5, r[1] - bz + 0.5, r[2] + bz - 0.5, r[3] + bz - 0.5], 4, fill=BEZEL,
               outline=BEZEL_EDGE, width=1)


# ---------------------------------------------------------------------------
# Knob::draw and RotarySwitch::draw
# ---------------------------------------------------------------------------
def cap(d, cx, cy, radius):
    """A black cap lit from the top left: concentric discs, the light
    drifting towards the highlight as they shrink (fillCap's gradient)."""
    steps = 24
    for i in range(steps):
        t = i / (steps - 1)
        rr = radius * (1 - t * 0.85)
        ox, oy = -radius * 0.25 * t, -radius * 0.35 * t
        col = blend((5, 5, 5), (22, 22, 22), min(1, t / 0.4)) if t < 0.4 else blend((22, 22, 22), (58, 58, 58), (t - 0.4) / 0.6)
        disc(d, cx + ox, cy + oy, rr, fill=col)


def knob(d, c):
    S = L['styles']
    r, dia = c['rect'], c['diameter']
    cx, cy = (r[0] + r[2]) / 2, r[1] + dia / 2
    outer = dia / 2
    body = outer - (6 if dia >= 44 else 5)
    a = 135 + 270 * max(0.0, min(1.0, c['value']))

    for i in range(11):
        t = 135 + 27 * i
        long_ = i in (0, 5, 10)
        stroke(d, [vstgui_angle(cx, cy, body + 1.5, t), vstgui_angle(cx, cy, outer - (0 if long_ else 1.8), t)],
               TICK, 1.3 if long_ else 0.9)
    lo = from_top(cx, cy, outer + 2, -138)
    hi = from_top(cx, cy, outer + 2, 138)
    lettering(d, '0', S['tick'], TICK, lo[0] - 2, lo[1] + 4, 'right')
    lettering(d, '10', S['tick'], TICK, hi[0] + 2, hi[1] + 4)

    teeth = [vstgui_angle(cx, cy, body if i % 2 == 0 else body - 1.4, 360 * i / 56) for i in range(56)]
    d.polygon([pt(x, y) for x, y in teeth], fill=KNOB_BODY, outline=KNOB_RIM)
    cap(d, cx, cy, body * 0.78)
    disc(d, cx, cy, body * 0.78, outline=(0, 0, 0), width=0.8)
    stroke(d, [vstgui_angle(cx, cy, body * 0.15, a), vstgui_angle(cx, cy, body - 0.5, a)], INK,
           2.2 if dia >= 44 else 1.8)

    top = r[1] + dia + L['knobGap']
    lettering(d, c['text'], S['value'], INK, cx, top + L['knobValueH'] / 2, 'centre')
    lettering(d, c['label'], S['label'], INK, cx, top + L['knobValueH'] + L['knobLabelH'] / 2, 'centre')


def selector(d, c):
    S = L['styles']
    cx, cy, rad = c['cx'], c['cy'], c['radius']
    cur = c['current']
    for i, name in enumerate(c['names']):
        t = c['angles'][i]
        on = i == cur
        stroke(d, [from_top(cx, cy, rad + 2.5, t), from_top(cx, cy, rad + 8, t)], RED if on else TICK, 2 if on else 1.6)
        b = c['labelBoxes'][i]
        lettering(d, name, S['label'], INK if on else INK_DIM, b[0], (b[1] + b[3]) / 2)

    a = c['angles'][cur]
    disc(d, cx, cy, rad * 0.8, fill=(11, 11, 11), outline=(68, 68, 68), width=0.8)
    tip = from_top(cx, cy, rad + 3, a)
    left = from_top(cx, cy, 8, a - 90)
    right = from_top(cx, cy, 8, a + 90)
    tail = from_top(cx, cy, rad, a + 180)
    beak = [tip, right] + quad(right, tail, left)
    d.polygon([pt(x, y) for x, y in beak], fill=(26, 26, 26))
    stroke(d, beak + [tip], (0, 0, 0), 0.8)
    stroke(d, [from_top(cx, cy, 2, a), tip], INK_DIM if c['locked'] else INK, 1.8)
    lettering(d, c['label'], S['label'], INK, cx, c['nameY'], 'centre')


# ---------------------------------------------------------------------------
# Turntable::draw
# ---------------------------------------------------------------------------
def turntable(img):
    T = L['turntable']
    S = L['styles']
    r = T['rect']
    dx, dy = T['disc']
    d = ImageDraw.Draw(img)

    d.rectangle([p(r[0]), p(r[1]), p(r[2]) - 1, p(r[3]) - 1], fill=SCREEN)
    x = r[0] + 8
    while x < r[2]:
        y = r[1] + 8
        while y < r[3]:
            disc(d, x, y, 0.7, fill=GRID_DOT)
            y += 16
        x += 16

    o, od = layer(img)
    ring(od, dx, dy, T['platterR'], OLED + (90,), 1.0)
    img.alpha_composite(o)
    d = ImageDraw.Draw(img)
    disc(d, dx, dy, T['recordR'], fill=(18, 12, 2))
    ring(d, dx, dy, T['recordR'], OLED, 1.6)
    o, od = layer(img)
    rr, i = T['grooveInner'], 0
    while rr <= T['grooveOuter'] + 1e-9:
        ring(od, dx, dy, rr, OLED + (26 + 20 * (i % 3),), 0.6)
        rr += 3.2
        i += 1
    img.alpha_composite(o)

    d = ImageDraw.Draw(img)
    disc(d, dx, dy, T['labelR'], fill=tuple(T['labelColour']))
    if T['goldRing']:
        ring(d, dx, dy, T['labelR'] - 5, tuple(T['goldRing']), 1.0)
    ring(d, dx, dy, T['labelR'], OLED, 1.2)
    o, od = layer(img)
    for off in (0, 180):
        t = T['labelMark'] + off
        stroke(od, [from_top(dx, dy, 18, t), from_top(dx, dy, 34, t)], INK + (180,), 1.6)
    img.alpha_composite(o)
    d = ImageDraw.Draw(img)
    disc(d, dx, dy, 3, fill=(0, 0, 0))
    ring(d, dx, dy, 3, OLED, 1.0)

    o, od = layer(img)
    for ang, radius, size in T['specks']:
        x, y = from_top(dx, dy, radius, ang)
        disc(od, x, y, size, fill=(255, 255, 255, 140))
    for ang, depth in T['scratches']:
        stroke(od, [from_top(dx, dy, T['grooveOuter'] - 50 * depth, ang), from_top(dx, dy, T['grooveOuter'] + 1, ang)],
               (255, 255, 255, int(255 * (0.45 + 0.5 * depth))), 0.8 + 0.8 * depth)
    img.alpha_composite(o)

    # the arm: its head grows as the warp lifts it
    d = ImageDraw.Draw(img)
    px, py = T['armPivot']
    sx, sy = from_top(dx, dy, T['stylusR'], T['stylusAngle'])
    ring(d, px, py, 9, SCREEN_TEXT, 1.4)
    stroke(d, [(px, py), (sx, sy)], ARM, 2.4)
    ln = math.hypot(sx - px, sy - py)
    ux, uy = (sx - px) / ln, (sy - py) / ln
    vx, vy = -uy, ux
    hw, hh = 7 * (1 + 0.35 * T['lift']), 4 * (1 + 0.35 * T['lift'])
    head = [(sx + vx * hw + ux * hh, sy + vy * hw + uy * hh), (sx - vx * hw + ux * hh, sy - vy * hw + uy * hh),
            (sx - vx * hw - ux * hh, sy - vy * hw - uy * hh), (sx + vx * hw - ux * hh, sy + vy * hw - uy * hh)]
    d.polygon([pt(x, y) for x, y in head], fill=ARM)

    lettering(d, T['rpm'], S['screen'], SCREEN_TEXT, r[0] + 10, r[1] + 14)
    if T['stuck']:
        lettering(d, 'STUCK', S['screen'], RED, r[2] - 10, r[1] + 14, 'right')
    lettering(d, T['channels'], S['screen'], SCREEN_TEXT, r[0] + 10, r[3] - 12)
    lettering(d, T['discName'], S['screen'], OLED, r[2] - 10, r[3] - 12, 'right')


# ---------------------------------------------------------------------------
# Lamp::draw, TextLine::draw
# ---------------------------------------------------------------------------
def lamp(d):
    lp = L['lamp']
    r = lp['rect']
    cy = (r[1] + r[3]) / 2
    x = r[0] + 8
    disc(d, x, cy, 5.5, fill=(0, 0, 0), outline=SECTION_EDGE, width=1)
    disc(d, x, cy, 4, fill=LAMP_ON if lp['on'] else LAMP_OFF)
    lettering(d, lp['name'], L['styles']['block'], INK, r[0] + 20, cy)


def lines(d):
    S = L['styles']
    inks = {'ink': INK, 'soft': INK_SOFT}
    for ln in L['lines']:
        r = ln['rect']
        x = (r[0] + r[2]) / 2 if ln['align'] == 'centre' else (r[2] if ln['align'] == 'right' else r[0])
        lettering(d, ln['text'], S[ln['style']], inks[ln['ink']], x, (r[1] + r[3]) / 2, ln['align'])


# ---------------------------------------------------------------------------
# AboutBox::draw - the panel dimmed, the box, the wordmark, the rows
# ---------------------------------------------------------------------------
def about_box(img):
    A, S = L['about'], L['styles']
    o, d = layer(img)
    d.rectangle([0, 0, p(L['width']), p(L['height'])], fill=(0, 0, 0, 178))
    img.alpha_composite(o)
    d = ImageDraw.Draw(img)

    b = A['box']
    round_rect(d, b, L['sectionRadius'], fill=SECTION, outline=SECTION_EDGE, width=1.2)
    band = [b[0] + 1.2, b[1] + 1.2, b[2] - 1.2, b[1] + 62.0]
    d.rectangle([p(band[0]), p(band[1]), p(band[2]), p(band[3])], fill=HEADER)
    d.rectangle([p(band[0]), p(band[3]), p(band[2]), p(band[3] + 2)], fill=HEADER_RULE)

    cx = (b[0] + b[2]) / 2
    gap = L['wordmarkGap']
    w = text_width('WORN', S['wordmark']) + gap + text_width('VINYL', S['wordmark'])
    left = cx - w / 2
    second = left + text_width('WORN', S['wordmark']) + gap
    for ink, units in ((INK, None), (HEADER, 0.32)):
        lettering(d, 'WORN', S['wordmark'], ink, left, A['wordmarkY'], stroke_units=units)
        lettering(d, 'VINYL', S['wordmark'], ink, second, A['wordmarkY'], stroke_units=units)
    lettering(d, A['tagline'], A['valueStyle'], INK_SOFT, cx, A['taglineY'], 'centre')

    for row in A['rows']:
        lettering(d, row['label'], A['labelStyle'], INK_DIM, A['labelX'], row['y'])
        for i, line in enumerate(row['lines']):
            lettering(d, line, A['valueStyle'], INK, A['valueX'], row['y'] + A['lineGap'] * i)

    fy = A['footerY']
    d.rectangle([p(b[0] + 24), p(fy - 16), p(b[2] - 24), p(fy - 15)], fill=RULE)
    lettering(d, A['footer'], S['note'], INK_DIM, cx, fy, 'centre')


# ---------------------------------------------------------------------------
def main():
    global L
    args = [a for a in sys.argv[1:] if a != '--about']
    show_about = len(args) != len(sys.argv) - 1
    out = args[0] if args else os.path.join(ROOT, 'docs', 'panel.png')
    L = dump_layout()
    img = Image.new('RGBA', (p(L['width']), p(L['height'])), GROUND + (255,))
    backdrop(ImageDraw.Draw(img))
    # The editor's order: the controls, then the screen, the lamp and the lines.
    d = ImageDraw.Draw(img)
    for c in L['controls']:
        (knob if c['kind'] == 'knob' else selector)(d, c)
    turntable(img)
    d = ImageDraw.Draw(img)
    lamp(d)
    lines(d)
    if show_about:
        about_box(img)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    img = img.convert('RGB').resize((L['width'] * SCALE, L['height'] * SCALE), Image.LANCZOS)
    img.save(out)
    print('wrote %s (%dx%d)' % (out, img.width, img.height))


if __name__ == '__main__':
    main()
