#!/usr/bin/env python3
"""Collect Wakes' manual and reference docs next to the emulator page.

usage: gen_manual.py OUT_DIR

Writes OUT_DIR/manual.json, copies docs/Wakes - Manual.pdf and the reference docs, and
(when poppler's pdftoppm/pdftotext are installed) renders each manual page to PNG and
reads its title from the page itself, so the page list follows the manual.
"""
import html
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PDF = os.path.join(ROOT, 'docs', 'Wakes - Manual.pdf')

# The reference docs worth having beside the device, with the tab name for each.
DOCS = [
    ('UI-PAGES.md', 'Controls'),
    ('PLAITS-ENGINES.md', 'Plaits engines'),
    ('MARBLES-SETTINGS.md', 'Marbles'),
    ('MIDI.md', 'MIDI'),
    ('DEFAULTS.md', 'Defaults'),
    ('SAFETY.md', 'Safety'),
]


def have(tool):
    return shutil.which(tool) is not None


# ---- the SP-1 diagrams on each page ----
# Every page with a device drawing has the same template: magenta F1-F4 / T1-T4 markers,
# the page's name for each control on the row above the faders and below the keys, the
# top keys' names above them, the rocker's names (rotated) at its left, and the right
# edge's names at its right. The left drawing is the page's BASE layer; a right-hand
# drawing, when there is one, is its SHFT layer. Positions are read from the PDF's text,
# so the hotspots and labels follow the manual when it changes.

def page_words(pdf):
    """[[(x0, y0, x1, y1, word), ...] per page], plus the page size in points."""
    xml = subprocess.run(['pdftotext', '-bbox', pdf, '-'], capture_output=True, text=True).stdout
    pages, size = [], None
    for m in re.finditer(r'<page width="([\d.]+)" height="([\d.]+)">(.*?)</page>', xml, re.S):
        size = (float(m.group(1)), float(m.group(2)))
        words = [(float(a), float(b), float(c), float(d), html.unescape(w)) for a, b, c, d, w in
                 re.findall(r'<word xMin="([\d.]+)" yMin="([\d.]+)" xMax="([\d.]+)" '
                            r'yMax="([\d.]+)">([^<]*)</word>', m.group(3))]
        pages.append(words)
    return pages, size


def group_line(ws, gap=-1.0):
    """Words on one row -> labels: neighbours closer than `gap` points join."""
    out = []
    for w in sorted(ws):
        if out and w[0] - out[-1][2] < gap:
            x0, y0, x1, y1, t = out[-1]
            out[-1] = (x0, min(y0, w[1]), w[2], max(y1, w[3]), t + w[4])
        else:
            out.append(w)
    return out


def box(ws, pad=0.0):
    return [min(w[0] for w in ws) - pad, min(w[1] for w in ws) - pad,
            max(w[2] for w in ws) + pad, max(w[3] for w in ws) + pad]


def diagram(words, lo, hi):
    """One drawing in the x range [lo, hi), or None if the page has none there."""
    inr = [w for w in words if lo <= w[0] < hi]
    F = sorted(w for w in inr if re.fullmatch(r'F[1-4]', w[4]))
    T = sorted(w for w in inr if re.fullmatch(r'T[1-4]', w[4]))
    if [w[4] for w in F] != ['F1', 'F2', 'F3', 'F4'] or [w[4] for w in T] != ['T1', 'T2', 'T3', 'T4']:
        return None
    fy = sum(w[1] for w in F) / 4
    ty = sum(w[1] for w in T) / 4
    left, right = F[0][0] - 14, F[3][2] + 8

    def row(y_lo, y_hi, x_lo=left, x_hi=right):
        return group_line([w for w in inr if y_lo <= w[1] <= y_hi and x_lo <= w[0] < x_hi])

    def columns(labels, marks):
        # Four labels: one per column, in order. Fewer: nearest column wins.
        cols = [m[0] - 4.6 for m in marks]           # the marker sits up and right of the control
        out = [None] * 4
        if len(labels) == 4:
            out = labels
        else:
            for lab in labels:
                c = (lab[0] + lab[2]) / 2
                k = min(range(4), key=lambda i: abs(cols[i] - c))
                out[k] = lab
        return [(lab[4] if lab else None, ((lab[0] + lab[2]) / 2) if lab else cols[i])
                for i, lab in enumerate(out)]

    faders = columns(row(fy - 11, fy - 4), F)
    keys = columns(row(ty + 9, ty + 16), T)
    top = row(fy - 44.5, fy - 35.5, left, F[1][0] + 4)

    # The rocker's names are rotated: fragments read bottom to top, upper and lower halves.
    frags = [w for w in inr if F[0][0] - 21 <= w[0] < F[0][0] - 9.5 and fy - 33 <= w[1] <= fy - 6]
    rocker = {}
    if frags:
        mid = (min(w[1] for w in frags) + max(w[3] for w in frags)) / 2
        for half, ws in (('up', [w for w in frags if (w[1] + w[3]) / 2 < mid]),
                         ('down', [w for w in frags if (w[1] + w[3]) / 2 >= mid])):
            if ws:
                text = ''.join(w[4] for w in sorted(ws, key=lambda w: -(w[1] + w[3])))
                rocker[half] = {'label': text, 'box': box(ws, 2.0)}

    # The right edge: the PLAY key's name (upper), the function key's (lower), and a note
    # on the side LEDs between them (rotated the other way: top to bottom).
    edge_ws = [w for w in inr if F[3][0] + 12 <= w[0] < F[3][0] + 23 and fy - 30 <= w[1] <= ty + 12]
    edge = {}
    play = [w for w in edge_ws if (w[1] + w[3]) / 2 < fy - 12]
    fnc = [w for w in edge_ws if (w[1] + w[3]) / 2 > ty - 4]
    leds = [w for w in edge_ws if w not in play and w not in fnc]
    if play:
        edge['play'] = {'label': ' '.join(w[4] for w in sorted(play, key=lambda w: w[1])), 'box': box(play, 1.5)}
    if fnc:
        edge['fnc'] = {'label': ' '.join(w[4] for w in sorted(fnc, key=lambda w: w[1])), 'box': box(fnc, 1.5)}

    return {
        'box': [left - 9, fy - 46, right + 16, ty + 18],
        'faders': [{'label': t, 'x': x, 'y0': fy - 3.5, 'y1': ty - 5.5} for t, x in faders],
        'keys': [{'label': t, 'x': x, 'y0': ty - 1.5, 'y1': ty + 12.5} for t, x in keys],
        'top': [{'label': w[4], 'box': box([w], 1.5)} for w in top[:2]],
        'rocker': rocker,
        'edge': edge,
        'leds': ' '.join(w[4] for w in sorted(leds, key=lambda w: w[1])) or None,
    }


def scale(d, size):
    """Points -> fractions of the page, for overlays that scale with the image."""
    W, H = size

    def bx(b):
        return [round(b[0] / W, 5), round(b[1] / H, 5), round(b[2] / W, 5), round(b[3] / H, 5)]
    d['box'] = bx(d['box'])
    for f in d['faders'] + d['keys']:
        f['x'] = round(f['x'] / W, 5)
        f['y0'] = round(f['y0'] / H, 5)
        f['y1'] = round(f['y1'] / H, 5)
    for t in d['top']:
        t['box'] = bx(t['box'])
    for part in list(d['rocker'].values()) + list(d['edge'].values()):
        part['box'] = bx(part['box'])
    return d


def main():
    out = sys.argv[1]
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    info = {'pdf': None, 'pages': [], 'docs': []}

    if os.path.exists(PDF):
        shutil.copyfile(PDF, os.path.join(out, 'Wakes-Manual.pdf'))
        info['pdf'] = 'Wakes-Manual.pdf'
        if have('pdftoppm'):
            # 240 dpi: a 13 cm wide page comes out ~1230 px, sharp at 2x in the panel.
            subprocess.run(['pdftoppm', '-png', '-r', '240', PDF, os.path.join(out, 'page')],
                           check=True)
            imgs = sorted((f for f in os.listdir(out) if re.match(r'page-\d+\.png$', f)),
                          key=lambda f: int(re.findall(r'\d+', f)[0]))
            words, size = page_words(PDF) if have('pdftotext') else ([], None)
            for i, img in enumerate(imgs, 1):
                title = 'Page %d' % i
                if have('pdftotext'):
                    txt = subprocess.run(['pdftotext', '-f', str(i), '-l', str(i), PDF, '-'],
                                         capture_output=True, text=True).stdout
                    m = re.search(r'Pg\.\s*(\d+)\s*-\s*(.+)', txt)
                    if m:
                        title = m.group(2).strip()
                page = {'n': i, 'title': title, 'img': img}
                if i <= len(words):
                    W = size[0]
                    base = diagram(words[i - 1], 0, W * 0.21)
                    shift = diagram(words[i - 1], W * 0.77, W + 1)
                    if base:
                        page['base'] = scale(base, size)
                    if shift:
                        page['shift'] = scale(shift, size)
                info['pages'].append(page)

    for name, tab in DOCS:
        src = os.path.join(ROOT, 'docs', name)
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(out, name))
            info['docs'].append({'file': name, 'tab': tab})

    with open(os.path.join(out, 'manual.json'), 'w') as f:
        json.dump(info, f, indent=1)
    print('manual: %d pages, %d docs' % (len(info['pages']), len(info['docs'])))


if __name__ == '__main__':
    main()
