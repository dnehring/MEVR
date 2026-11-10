# SPDX-License-Identifier: MIT
"""What a played session's logs say about its first-person scenes (research/69 "A played session, judged").

  python tools/conversation-session.py [--log <LE1MEVR.log>] [--halcyon <MELEVR_Log.txt>] [--strict]

Reads the mod's log of one run of the game (and Halcyon's, for the first-person hides and the presentation) and lists, per
scene shown in the world (a conversation, or a moment: a matinee that walks Shepard), what a player would have noticed:

  flash        the 3D screen shown and taken away again within a second (two dips)
  dip          a dip from black inside the scene, with what asked for it (the script's black screen, the opening, a move)
  body-hidden  Halcyon cleared the first-person hides during the scene: the player's own body and arms not drawn
  screen       the scene fell back to the 3D screen for a moment (the mod's state went unpublished)
  pivot        people whose bodies turned 60 degrees or more
  reply-move   Shepard moved in the half second after one of his own lines began (the wheel's stick walking him)
  drawn-jump   a placement seen in a drawn frame (the summary's "drawn")
  body-strayed Shepard's head bone more than 0.8 m from the player's view (the summary's "eyeToViewM")

The owner's notes after a headset session name these in his own words; this names them from the logs, the same way for
every run, so a note can be matched to its cause without reading 3,000 lines. No game needed. The logs are the player's own
data: keep them out of the repository. --strict: exit 1 when anything was found.
"""
import json
import os
import re
import sys

LINE = re.compile(r'^\[(\d\d):(\d\d):(\d\d)\.(\d\d\d)\] \[\w+\] ?(.*)$')
HALCYON = re.compile(r'^\d{4}-\d\d-\d\d (\d\d):(\d\d):(\d\d)\.(\d\d\d) \| (.*)$')
BEGAN = re.compile(r'convo: conversation (\d+) began: BioConversation (\S+) \(owner (\S+), player (\w+),.*shown in the world')
ENDED = re.compile(r'convo: conversation (\d+) ended .*? after ([\d.]+) s: (.*)$')
TURNED = re.compile(r'(\w+): moved at most ([\d.]+) m, turned at most (\d+) deg')
FADE = re.compile(r'transition fade: (.+?) -> (.+?): from black')
REPLY = re.compile(r'convo: at ([\d.]+) s the line: "Shepard: ')
MOVED = re.compile(r'convo: at ([\d.]+) s (\w+) (?:moved|JUMPED) to \((-?\d+), (-?\d+), (-?\d+)\)')


def seconds(h, m, s, ms):
    return int(h) * 3600 + int(m) * 60 + int(s) + int(ms) / 1000.0


def clock(t):
    return f'{int(t // 3600):02d}:{int(t % 3600 // 60):02d}:{t % 60:06.3f}'


def parse(lines, pattern):
    out = []
    for line in lines:
        m = pattern.match(line.rstrip('\r\n'))
        if m:
            out.append((seconds(*m.groups()[:4]), m.group(5)))
    return out


def scenes_of(mod):
    """The scenes shown in the world, in order: {'kind', 'name', 'start', 'end', 'player', 'summary', 'ended'}."""
    scenes = []
    current = None
    for t, text in mod:
        if text.startswith('moment: a matinee walks Shepard: held in first person'):
            current = {'kind': 'moment', 'name': 'a walk of Shepard\'s', 'start': t, 'end': None, 'player': None, 'summary': {}, 'ended': ''}
            scenes.append(current)
        elif text.startswith('moment: the first-person view of the moment ends') and current and current['kind'] == 'moment':
            current['end'] = t
        elif text.startswith('moment: view summary ') and scenes and scenes[-1]['kind'] == 'moment':
            try:
                scenes[-1]['summary'] = json.loads(text[len('moment: view summary '):])
            except ValueError:
                pass
        elif (m := BEGAN.search(text)):
            current = {'kind': 'conversation', 'name': m.group(2).split('.')[-1], 'start': t, 'end': None, 'player': m.group(4),
                       'summary': {}, 'ended': '', 'number': m.group(1)}
            scenes.append(current)
        elif (m := ENDED.search(text)) and current and current['kind'] == 'conversation' and current.get('number') == m.group(1):
            current['end'] = t
            current['ended'] = m.group(3)
        elif text.startswith('convo: summary ') and current and current['kind'] == 'conversation':
            try:
                current['summary'] = json.loads(text[len('convo: summary '):])
            except ValueError:
                pass
            if current['end'] is None:
                current['end'] = t
    last = mod[-1][0] if mod else 0.0
    for s in scenes:
        if s['end'] is None:
            s['end'] = last
    return scenes


def cause_of(mod, t):
    """What asked for a dip at t: the nearest telling line within 0.25 s before it (0.05 s after)."""
    best = None
    for at, text in mod:
        if at < t - 0.25 or at > t + 0.05:
            continue
        if "the script's black screen" in text and ('a dip' in text or 'covered a change' in text):
            best = "in place of the script's black screen"
        elif 'convo: the opening' in text and ': cut' in text:
            best = "the conversation's opening was a cut"
        elif 'another composition' in text or 'put there under a dip' in text:
            best = 'the script moved Shepard to another place'
        elif 'convo: at ' in text and 'kept in place' in text and best is None:
            best = 'a placement'
    return best or 'nothing in the log says why'


def judge(mod, halcyon=()):
    """Scenes with their findings: [{'kind', 'name', 'start', 'end', 'findings': [(code, text)]}]."""
    scenes = scenes_of(mod)
    fades = [(t, m.group(1), m.group(2)) for t, text in mod if (m := FADE.search(text))]
    used = set()
    for s in scenes:
        found = []
        lo, hi = s['start'] - 0.5, s['end'] + 0.5
        # The 3D screen shown and taken away again: two dips for nothing.
        for i, (t, frm, to) in enumerate(fades):
            if i in used or not (lo <= t <= hi) or frm != 'the world' or to == 'the world':
                continue
            back = next((j for j in range(i + 1, len(fades)) if fades[j][1] == to and fades[j][2] == 'the world' and fades[j][0] - t < 1.0), None)
            if back is not None:
                used.update((i, back))
                found.append(('flash', f'{clock(t)}: {to} was shown for {fades[back][0] - t:.2f} s and taken away again (two dips)'
                              + (': as the scene began' if abs(t - s['start']) < 0.6 else '')))
        for i, (t, frm, to) in enumerate(fades):
            if i in used or not (s['start'] <= t <= s['end'] + 0.2) or frm != 'the world' or to != 'the world':
                continue
            used.add(i)
            found.append(('dip', f'{clock(t)}: a dip from black, {t - s["start"]:.1f} s in: {cause_of(mod, t)}'))
        # The first-person hides cleared: the player's own body not drawn.
        hides = [(t, text) for t, text in halcyon if s['start'] <= t <= s['end'] and text.startswith('[FPHIDE]')]
        for i, (t, text) in enumerate(hides):
            if 'cleared' not in text:
                continue
            back = next((u for u, later in hides[i + 1:] if 'applied' in later), s['end'])
            if back - t >= 0.3:
                found.append(('body-hidden', f'{clock(t)}: your own body and arms were not drawn for {back - t:.1f} s '
                              f'({text[len("[FPHIDE] "):]})'))
        for t, text in halcyon:
            if s['kind'] == 'conversation' and s['start'] <= t <= s['end'] and text.startswith('[VRSTATE]') and '3D screen' in text:
                found.append(('screen', f'{clock(t)}: the scene fell back to the 3D screen ({text[len("[VRSTATE] "):]})'))
        # People whose bodies turned.
        turned = [(name, int(deg)) for name, _, deg in TURNED.findall(s['ended']) if name != s['player'] and int(deg) >= 60]
        if turned:
            turned.sort(key=lambda x: -x[1])
            found.append(('pivot', f'{len(turned)} bodies turned 60 degrees or more ('
                          + ', '.join(f'{n} {d}' for n, d in turned[:4]) + (', ...' if len(turned) > 4 else '')
                          + f'; the summary counts {s["summary"].get("bodyTurns", "no")} body turns by the look-at rule)'))
        # Shepard moving right after his own line began.
        if s['kind'] == 'conversation' and s['player']:
            replies = [t for t, text in mod if s['start'] <= t <= s['end'] and REPLY.search(text)]
            moves = [t for t, text in mod if s['start'] <= t <= s['end'] and (m := MOVED.search(text)) and m.group(2) == s['player']]
            after = [r for r in replies if sum(1 for t in moves if r <= t <= r + 0.6) >= 2]
            if after:
                found.append(('reply-move', f'Shepard moved in the half second after {len(after)} of his {len(replies)} lines began '
                              f'(first at {clock(after[0])}); the summary counts {s["summary"].get("wheelSticks", "no")} replies with the stick still out'))
        if s['summary'].get('drawn', 0):
            found.append(('drawn-jump', f'{s["summary"]["drawn"]} placements were seen in a drawn frame'))
        if s['summary'].get('eyeToViewM', 0) > 0.8:
            found.append(('body-strayed', f'Shepard\'s head was up to {s["summary"]["eyeToViewM"]:.2f} m from your view'))
        s['findings'] = found
    return scenes


def report(scenes):
    out = []
    for s in scenes:
        facts = []
        sm = s['summary']
        if s['kind'] == 'conversation':
            facts = [f'{sm.get("lines", "?")} lines', f'opening: {sm.get("opening", "?")}'
                     + (f' ({sm["markM"]:.1f} m to the mark, {sm.get("walkInWay", "-")})' if sm.get('markM') else ''),
                     f'{sm.get("cuts", 0)} cuts', f'walked {sm.get("playerWalkedM", 0):.1f} m']
        else:
            facts = [f'walked {sm.get("walkedM", 0):.1f} m', f'carried {sm.get("carriedM", 0):.1f} m', f'{sm.get("dips", 0)} dips']
        out.append(f'{clock(s["start"])}  {s["kind"]}: {s["name"]}, {s["end"] - s["start"]:.0f} s; ' + ', '.join(facts))
        for code, text in s['findings']:
            out.append(f'    {code:<12} {text}')
        if not s['findings']:
            out.append('    nothing found')
    if not scenes:
        out.append('No scene was shown in the world in this log ([Cinematics] conversations = world?).')
    return '\n'.join(out)


def default_logs():
    game = r'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition\Game\ME1\Binaries\Win64\Logs\LE1MEVR.log'
    local = os.environ.get('LOCALAPPDATA', '')
    mod = game if os.path.exists(game) else os.path.join(local, 'MEVR', 'LE1MEVR.log')
    return mod, os.path.join(local, 'MELEVR', 'MELEVR_Log.txt')


def main(argv):
    def option(name, default):
        return argv[argv.index(name) + 1] if name in argv and argv.index(name) + 1 < len(argv) else default
    mod_path, halcyon_path = default_logs()
    mod_path = option('--log', mod_path)
    halcyon_path = option('--halcyon', halcyon_path)
    if '--help' in argv or not os.path.exists(mod_path):
        print(__doc__)
        return 2
    with open(mod_path, encoding='utf-8', errors='replace') as f:
        mod = parse(f, LINE)
    halcyon = []
    if os.path.exists(halcyon_path) and mod:
        with open(halcyon_path, encoding='utf-8', errors='replace') as f:
            halcyon = [(t, text) for t, text in parse(f, HALCYON) if mod[0][0] <= t <= mod[-1][0] + 1.0]
    scenes = judge(mod, halcyon)
    print(f'{mod_path}: {len(mod)} lines, {clock(mod[0][0]) if mod else "-"} to {clock(mod[-1][0]) if mod else "-"}'
          f'; Halcyon\'s log: {len(halcyon)} lines of that time')
    print(report(scenes))
    return 1 if '--strict' in argv and any(s['findings'] for s in scenes) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
