#!/usr/bin/env python3
"""Collect Wakes' manual and reference docs next to the emulator page.

usage: gen_manual.py OUT_DIR

Writes OUT_DIR/manual.json, copies docs/Wakes - Manual.pdf and the reference docs, and
(when poppler's pdftoppm/pdftotext are installed) renders each manual page to PNG and
reads its title from the page itself, so the page list follows the manual.
"""
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
            for i, img in enumerate(imgs, 1):
                title = 'Page %d' % i
                if have('pdftotext'):
                    txt = subprocess.run(['pdftotext', '-f', str(i), '-l', str(i), PDF, '-'],
                                         capture_output=True, text=True).stdout
                    m = re.search(r'Pg\.\s*(\d+)\s*-\s*(.+)', txt)
                    if m:
                        title = m.group(2).strip()
                info['pages'].append({'n': i, 'title': title, 'img': img})

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
