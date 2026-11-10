# SPDX-License-Identifier: MIT
"""Tests for tools/le1-save.py over synthetic saves (no player's save is committed: they are personal data).

A save is built from the tool's own schema with every struct present and one legacy object of each class, so a read or
write mistake in any type shows. Run: python -I tests/Le1SaveTests.py (ctest mevr-le1-save). With MEVR_LE1_SAVES set to
a folder, its .pcsav files are round-tripped too (the owner's saves, locally).
"""
import glob
import importlib.util
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('le1save', os.path.join(HERE, '..', 'tools', 'le1-save.py'))
ls = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ls)

failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
        print(f'FAIL {what}')


def sample(t, depth=0):
    """A value of schema type t, with something in every list and option (up to a depth, to end the recursion)."""
    if t == 'i32':
        return -7
    if t == 'u32':
        return 0x80000001
    if t == 'u8':
        return 3
    if t == 'f32':
        return 1.5
    if t == 'bool':
        return True
    if t == 'str':
        return ls.ustr('Shepard')
    if t == 'object':
        return sample_object('BioPawnSaveObject', depth)
    if isinstance(t, tuple):
        if t[0] == 'vec':
            return [sample(t[1], depth + 1)] if depth < 4 else []
        if t[0] == 'opt':
            return sample(t[1], depth + 1) if depth < 4 else None
        if t[0] == 'map':
            return {ls.ustr(f'k{depth}'): sample(t[2], depth + 1)} if depth < 4 else {}
        if t[0] == 'bytes':
            return bytes(range(t[1]))
    return {name: sample(ft, depth + 1) for name, ft in ls.STRUCTS[t]}


def sample_object(cls, depth):
    return {'class_name': ls.ustr(cls), 'owner_name': ls.ustr('TheWorld.PersistentLevel.BioPawn_1'),
            'owner_class': ls.ustr('BioPawn'), 'body': sample(ls.OBJECT_BODIES[cls], depth + 1)}


def sample_save():
    data = sample('SaveData')
    data['version'] = ls.VERSION
    data['base_level_name'] = ls.ustr('BIOA_PRO00')
    data['map_name'] = ls.ustr('MapPRO01')
    data['parent_map_name'] = ls.ustr('')
    data['display_name'] = ls.ustr(' 21        John 2026/11/09 10:48:37            John02   0  NA 00 007   1h34m')
    data['file_name'] = ls.ustr('John02_21')
    data['plot']['booleans'] = [0, 0x10]
    objects = [sample_object(cls, 1) for cls in ls.OBJECT_BODIES]
    world = sample_object('BioWorldInfoSaveObject', 1)
    data['no_export'] = {
        'legacy_maps': {ls.ustr('BIOA_PRO00'): {'levels': {ls.ustr('BIOA_PRO10'): {'objects': objects,
                                                                                    'actors': [ls.ustr('BioPawn_1')]}},
                                                 'world': world}},
        'mako': sample('Mako'),
    }
    return data


# A synthetic save survives write, read, write.
s = ls.Save(sample_save())
b = s.to_bytes()
r = ls.Save.from_bytes(b)
check(r.to_bytes() == b, 'a synthetic save writes back byte for byte')
check(ls.build_payload(r.data) == ls.build_payload(s.data), 'its payload reads back the same')
check(set(r.data['no_export']['legacy_maps']['BIOA_PRO00']['levels']['BIOA_PRO10']['objects'][i]['class_name']
          for i in range(len(ls.OBJECT_BODIES))) == set(ls.OBJECT_BODIES), 'every legacy object class reads back')

# The container: magic, block, totals, one chunk, CRC-32/BZIP2 of what comes before it, flag 1, the size.
magic, block, tc, tu, c0, u0 = struct.unpack_from('<IIIIII', b, 0)
check(magic == 0x9E2A83C1 and block == 0x40000, 'magic and block size')
check(tc == c0 and tu == u0 and tu == len(ls.build_payload(s.data)), 'one chunk: the totals are its sizes')
crc, flag, size = struct.unpack_from('<III', b, len(b) - 12)
check(crc == ls.crc32_bzip2(b[:-12]) and flag == 1 and size == tu, 'the tail: checksum, flag, size')
check(ls.crc32_bzip2(b'123456789') == 0xFC891918, 'CRC-32/BZIP2 check value')

# A payload over a block splits into chunks of a block and a rest (a small block here), and reads back.
small = ls.Save(sample_save(), block=256)
sb = small.to_bytes()
n_chunks = (tu + 255) // 256
check(struct.unpack_from('<I', sb, 16 + 8 * (n_chunks - 1) + 4)[0] == tu - 256 * (n_chunks - 1), 'the last chunk holds the rest')
check(ls.build_payload(ls.Save.from_bytes(sb).data) == ls.build_payload(s.data), 'a multi-chunk save reads back')

# Checksum and truncation are refused.
bad = bytearray(b)
bad[30] ^= 0xFF
try:
    ls.Save.from_bytes(bytes(bad))
    check(False, 'a corrupted save is refused')
except (ls.SaveError, zlib.error):
    pass
try:
    ls.Save.from_bytes(b[:-1])
    check(False, 'a truncated save is refused')
except ls.SaveError:
    pass

# A wide (UTF-16) string stays wide; a new one with a character past Latin-1 becomes wide; a Latin-1 one stays narrow.
w = ls.Save(sample_save())
w.data['player']['first_name'] = ls.ustr('Shep', True)
w.data['character_id'] = 'Ŝhep'
w.data['player']['face_code'] = 'Café'
rw = ls.Save.from_bytes(w.to_bytes())
check(rw.data['player']['first_name'] == 'Shep' and rw.data['player']['first_name'].wide, 'a wide string stays wide')
check(rw.data['character_id'] == 'Ŝhep' and rw.data['character_id'].wide, 'a non-Latin-1 string is written wide')
check(rw.data['player']['face_code'] == 'Café' and not rw.data['player']['face_code'].wide, 'Latin-1 stays narrow')

# Edits: the level, the place, the facing, plot values past the end, the file name, the shown date.
e = ls.Save(sample_save())
e.set_level('BIOA_STA00', 'MapSTA60_05')
e.set_place((100.0, -200.5, 300.25), 90.0)
e.set_plot_bool(4, False)
e.set_plot_bool(100, True)
e.set_plot_int(7, 42)
e.set_plot_float(2, 0.25)
e.set_file_name('John02_MEVR')
e.stamp_now()
re_ = ls.Save.from_bytes(e.to_bytes())
d = re_.data
check(d['base_level_name'] == 'BIOA_STA00' and d['map_name'] == 'MapSTA60_05', 'the level and area')
check(d['location'] == {'x': 100.0, 'y': -200.5, 'z': 300.25}, 'the place')
check(d['rotation'] == {'pitch': 0, 'yaw': 16384, 'roll': 0}, 'the facing: 90 degrees is 16384')
check(not re_.plot_bool(4) and re_.plot_bool(100) and len(d['plot']['booleans']) == 4, 'plot booleans, the array grown')
check(d['plot']['integers'][7] == 42 and len(d['plot']['integers']) == 8, 'a plot integer past the end')
check(d['plot']['floats'][2] == 0.25, 'a plot float')
check(d['file_name'] == 'John02_MEVR', 'the file name')
check(ls.DATE_IN_NAME.search(d['display_name']).group(0) != '2026/11/09 10:48:37', 'the shown date follows the time')
check(ls.unreal_angle(-90) == 49152 and ls.unreal_angle(360) == 0, 'angles wrap')

# Rewind: a beat between two saves (a boolean set, one cleared, an integer and a float changed, a map's world state changed,
# a map visited for the first time) is found by diff and put back on a later save; the later save's other progress stays.
before = ls.Save(sample_save())
after = ls.Save.from_bytes(before.to_bytes())
after.set_plot_bool(40, True)
after.set_plot_bool(36, False)  # the sample's set boolean (word 1, bit 4)
after.set_plot_int(2, 99)
after.set_plot_float(0, 2.5)
after.data['no_export']['legacy_maps']['BIOA_PRO00']['levels']['BIOA_PRO10']['actors'].append(ls.ustr('BioPawn_9'))
after.data['no_export']['legacy_maps'][ls.ustr('BIOA_STA00')] = sample('Map')
spec = ls.diff_saves(before, after)
check(spec['booleans'] == {'36': True, '40': False}, 'diff: the booleans with their values before')
check(spec['integers'] == {'2': 0} and spec['floats'] == {'0': 1.5}, 'diff: the integer and float before')
check(spec['afterIntegers'] == {'2': 99}, 'diff: the integers after, for reading')
check(set(spec['maps']) == {'BIOA_PRO00', 'BIOA_STA00'} and spec['maps']['BIOA_STA00'] is None, 'diff: a changed and a new map')
check(spec['mapLevels']['BIOA_PRO00'] == ['BIOA_PRO10'], 'diff: the changed level named')
later = ls.Save.from_bytes(after.to_bytes())
later.set_plot_bool(41, True)  # progress after the beat
done = ls.apply_rewind(later, spec)
check(not later.plot_bool(40) and later.plot_bool(36) and later.plot_bool(41), 'rewind: the beat undone, later progress kept')
check(later.data['plot']['integers'][2] == 0 and later.data['plot']['floats'][0] == 1.5, 'rewind: integer and float back')
check('BIOA_STA00' in later.data['no_export']['legacy_maps'], 'rewind without --with-maps leaves the maps')
check(len(done) == 4, f'rewind: one line per value put back ({len(done)})')
ls.apply_rewind(later, spec, with_maps=True)
check('BIOA_STA00' not in later.data['no_export']['legacy_maps'], 'rewind --with-maps forgets the map visited in the beat')
check(ls.serialize('Map', later.data['no_export']['legacy_maps']['BIOA_PRO00'])
      == ls.serialize('Map', before.data['no_export']['legacy_maps']['BIOA_PRO00']), 'rewind --with-maps: the map as before')
again = ls.Save.from_bytes(later.to_bytes())
check(ls.diff_saves(again, later)['booleans'] == {}, 'a rewound save writes and reads back')
check(ls.apply_rewind(again, spec) == [], 'a second rewind changes nothing')
try:
    ls.apply_rewind(again, {'format': 'something else'})
    check(False, 'a file that is not a rewind is refused')
except ls.SaveError:
    pass

# A character export (no world state) reads and writes without it.
x = sample_save()
x['no_export'] = None
xs = ls.Save.from_bytes(ls.Save(x).to_bytes())
check(xs.data['no_export'] is None, 'a character export has no world state')

# The command line: write, then roundtrip (in a temporary folder).
import tempfile
with tempfile.TemporaryDirectory(prefix='mevr-le1-save-') as tmp:
    src = os.path.join(tmp, 'John02_21.pcsav')
    out = os.path.join(tmp, 'John02_MEVR.pcsav')
    with open(src, 'wb') as f:
        f.write(b)
    rc = ls.main(['write', src, out, '--level', 'BIOA_STA00', '--location', '1', '2', '3', '--yaw', '180',
                  '--bool', '9=1', '--int', '3=5'])
    check(rc == 0 and os.path.exists(out), 'write from the command line')
    o = ls.Save.load(out)
    check(o.data['rotation']['yaw'] == 32768 and o.plot_bool(9) and o.data['file_name'] == 'John02_MEVR',
          'the command line edits')
    check(ls.main(['roundtrip', src, out]) == 0, 'roundtrip from the command line')
    # A visited map's kept streaming states: one changed (the map's name in any case), one added; an unknown map refused.
    out2 = os.path.join(tmp, 'John02_Stream.pcsav')
    names = [st['name'] for st in ls.Save.load(src).data['no_export']['legacy_maps']['BIOA_PRO00']['world']['body']['streaming_states']]
    check(len(names) >= 1, 'the sample map keeps a streaming state')
    rc = ls.main(['write', src, out2, '--stream-state', f'bioa_pro00:{names[0]}=0', '--stream-state', 'BIOA_PRO00:stream_wakeup=1'])
    states = {st['name']: st['enabled'] for st in
              ls.Save.load(out2).data['no_export']['legacy_maps']['BIOA_PRO00']['world']['body']['streaming_states']}
    check(rc == 0 and states.get(names[0]) == 0 and states.get('stream_wakeup') == 1, f'--stream-state set and added ({states})')
    try:
        ls.main(['write', src, os.path.join(tmp, 'x.pcsav'), '--stream-state', 'BIOA_NOR00:stream_wakeup=1'])
        check(False, '--stream-state on a map the save does not keep is refused')
    except SystemExit:
        pass

# Optional: the local saves (never committed).
folder = os.environ.get('MEVR_LE1_SAVES')
if folder:
    files = [f for f in glob.glob(os.path.join(folder, '**', '*.pcsav'), recursive=True)
             if os.path.basename(os.path.dirname(f)) != 'PROFILE']
    check(ls.main(['roundtrip', *files]) == 0, f'the saves in {folder} round-trip')

if failures:
    print(f'{len(failures)} failed')
    sys.exit(1)
print('le1-save: all tests passed')
