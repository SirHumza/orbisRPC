#!/usr/bin/env python3
"""Add an entry to config/custom.json (the community title list).

    python3 scripts/add_custom_title.py                 ask for id, name, art
    python3 scripts/add_custom_title.py --list          show every entry
    python3 scripts/add_custom_title.py --remove ORPC00042

pkg-zone only carries the apps its own scraper knows about, and the retro
indexes are gated on retro prefixes. custom.json is the catch-all: a plain
TITLEID -> name map anyone can contribute to.

Format matches the retro indexes so the payload reuses the same scanner:

    [{"id":"ORPC00042","name":"Some App","cover":"https://.../a.png"}]

Compact separators are required. The scanner matches entries on the literal
byte run "id":"<TITLEID>", so a space after the colon -- what you get from a
normal pretty-printer -- means the lookup never matches and the payload falls
back to the raw TITLEID with no error. save() below enforces this.

"cover" is optional; without one the presence falls back to the default art.

custom runs last in the chain, so TMDB stays authoritative for retail titles.
"""
import json
import os
import sys

PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    'config', 'custom.json')


def load():
    try:
        with open(PATH, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return []


def save(entries):
    """One entry per line so a contribution reads as a one-line diff.

    separators=(',', ':') is not cosmetic: the scanner matches entries on the
    literal byte run "id":"<TITLEID>", so a space after the colon -- what a
    normal pretty-printer emits -- means the lookup never matches and the
    payload falls back to the raw TITLEID with no error."""
    with open(PATH, 'w', encoding='utf-8', newline='\n') as f:
        f.write('[\n')
        for i, e in enumerate(entries):
            tail = '' if i == len(entries) - 1 else ','
            f.write('  ' + json.dumps(e, ensure_ascii=False,
                                      separators=(',', ':')) + tail + '\n')
        f.write(']\n')


def main():
    argv = sys.argv[1:]

    if argv and argv[0] == '--list':
        entries = load()
        for e in entries:
            art = e.get('cover', '')
            print('  %-12s %s%s' % (e['id'], e['name'],
                                    ('  [%s]' % art) if art else ''))
        print('%d entries' % len(entries))
        return

    if argv and argv[0] == '--remove':
        if len(argv) < 2:
            sys.exit('usage: add_custom_title.py --remove TITLEID')
        tid = argv[1].upper()
        entries = load()
        kept = [e for e in entries if e['id'].upper() != tid]
        if len(kept) == len(entries):
            print('%s not in the list' % tid)
            return
        save(kept)
        print('removed %s (%d entries left)' % (tid, len(kept)))
        return

    if argv:
        sys.exit('usage: add_custom_title.py [--list | --remove TITLEID]')

    # --- interactive add ---------------------------------------------------
    tid = input('TITLEID: ').strip().upper()
    name = input('Name: ').strip()
    art = input('Cover URL (blank for none): ').strip()

    entry = {'id': tid, 'name': name}
    if art:
        entry['cover'] = art

    entries = [e for e in load() if e['id'].upper() != tid]
    entries.append(entry)
    save(entries)

    print('%s -> %s' % (tid, name))


if __name__ == '__main__':
    main()