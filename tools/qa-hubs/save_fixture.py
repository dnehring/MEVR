# SPDX-License-Identifier: MIT
"""Pin private LE1 saves and select Resume in a COPY of a v50 gamer profile.

No world/plot/save data is edited. The bounded profile footer consists of two Unreal
ANSI strings, a playthrough integer and CRC-32/BZIP2. Reject unfamiliar layouts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()


def crc32(data):
    crc = 0xffffffff
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04c11db7 if crc & 0x80000000 else 0)) & 0xffffffff
    return crc ^ 0xffffffff


def string(data, at):
    count, = struct.unpack_from('<i', data, at)
    if not 1 <= count <= 256 or at + 4 + count > len(data):
        raise ValueError('Unsupported ANSI string')
    raw = data[at+4:at+4+count]
    if raw[-1] or b'\0' in raw[:-1]:
        raise ValueError('Invalid string terminator')
    return raw[:-1].decode('ascii'), at + 4 + count


def profile_selection(data):
    if len(data) < 40 or struct.unpack_from('<I', data)[0] != 50:
        raise ValueError('Expected a v50 gamer profile')
    if crc32(data[:-4]) != struct.unpack_from('<I', data, len(data)-4)[0]:
        raise ValueError('Gamer profile CRC mismatch')
    candidates = []
    for at in range(max(4, len(data)-530), len(data)-8):
        try:
            career, pos = string(data, at)
            slot, end = string(data, pos)
        except (ValueError, UnicodeError, struct.error):
            continue
        if (end == len(data)-8 and re.fullmatch(r'[A-Za-z0-9_]+', career)
                and re.fullmatch(re.escape(career) + r'_(?:[0-9]+|AutoSave|QuickSave)', slot)):
            candidates.append((at, career, slot))
    if len(candidates) != 1:
        raise ValueError('Resume footer is not uniquely recognized; no profile was changed')
    return candidates[0]


def resume_profile(data, career, slot):
    if not re.fullmatch(r'[A-Za-z0-9_]+', career) or not re.fullmatch(re.escape(career)+r'_(?:[0-9]+|AutoSave|QuickSave)', slot):
        raise ValueError('Invalid career/save name')
    at, _, _ = profile_selection(data)
    def encoded(value):
        raw = value.encode('ascii') + b'\0'
        return struct.pack('<i', len(raw)) + raw
    body = data[:at] + encoded(career) + encoded(slot) + data[-8:-4]
    output = body + struct.pack('<I', crc32(body))
    if profile_selection(output)[1:] != (career, slot):
        raise ValueError('Resume selection verification failed')
    return output


def save_info(path):
    data = Path(path).read_bytes()
    if len(data) < 40 or struct.unpack_from('<I', data)[0] != 0x9e2a83c1:
        raise ValueError('Expected a compressed LE1 save')
    block, total_c, total_u = struct.unpack_from('<III', data, 4)
    if not 0 < block <= 1048576 or not 0 < total_u <= 64*1024*1024:
        raise ValueError('Invalid save chunk sizes')
    if crc32(data[:-12]) != struct.unpack_from('<I', data, len(data)-12)[0]:
        raise ValueError('Save CRC mismatch')
    if struct.unpack_from('<II', data, len(data)-8) != (1, total_u):
        raise ValueError('Unsupported save compression trailer')
    count = (total_u + block-1)//block
    at = 16 + count*8
    if at + total_c + 12 != len(data):
        raise ValueError('Save length mismatch')
    chunks = []
    for i in range(count):
        compressed, uncompressed = struct.unpack_from('<II', data, 16+i*8)
        if not 0 < uncompressed <= block or at+compressed > len(data)-12:
            raise ValueError('Invalid save chunk')
        decoder = zlib.decompressobj()
        chunk = decoder.decompress(data[at:at+compressed], uncompressed+1)
        if len(chunk) != uncompressed or not decoder.eof or decoder.unused_data:
            raise ValueError('Save decompression mismatch')
        chunks.append(chunk)
        at += compressed
    body = b''.join(chunks)
    if at != len(data)-12 or len(body) != total_u or struct.unpack_from('<I', body)[0] != 50:
        raise ValueError('Invalid v50 save body')
    career, _ = string(body, 4)
    # Read-only diagnostic scan: base level, map, parent map, Vector, Rotator.
    # It is NOT a complete save parser or proof of the original walkthrough slot.
    observations = []
    for match in re.finditer(rb'BIOA_[A-Za-z0-9_]+\0', body):
        start = match.start()-4
        if start < 0:
            continue
        try:
            level, pos = string(body, start)
            area, pos = string(body, pos)
            parent_size, = struct.unpack_from('<i', body, pos)
            if parent_size == 0:
                pos += 4
            else:
                _, pos = string(body, pos)
            if not re.fullmatch(r'Map[A-Za-z0-9_]+', area):
                continue
            xyz = struct.unpack_from('<fff', body, pos)
            rotation = struct.unpack_from('<iii', body, pos+12)
            import math
            if all(math.isfinite(v) and abs(v)<1e7 for v in xyz):
                observations.append(dict(map=level, area=area, positionUU=xyz, rotationUnreal=rotation))
        except (ValueError, UnicodeError, struct.error):
            continue
    return dict(career=career, sha256=sha(path), bytes=len(data), locationCandidates=observations)


def verify(package):
    folder = Path(package)/'fixture'
    manifest = json.loads((folder/'fixture.json').read_text(encoding='utf-8-sig'))
    if manifest.get('schemaVersion') != 1 or manifest.get('kind') != 'citadel-save-fixture':
        raise ValueError('Unknown fixture manifest')
    if not re.fullmatch(r'[A-Za-z0-9_]+', manifest['career']) or not re.fullmatch(re.escape(manifest['career'])+r'_(?:[0-9]+|AutoSave|QuickSave)', manifest['slot']):
        raise ValueError('Invalid fixture selection')
    for name, key in [('save.pcsav','saveSha256'), ('profile-template.pcsav','profileSha256')]:
        if sha(folder/name) != manifest[key]:
            raise ValueError('Archived fixture hash mismatch: '+name)
    info = save_info(folder/'save.pcsav')
    if info['career'] != manifest['career']:
        raise ValueError('Archived save career mismatch')
    if json.loads(json.dumps(info['locationCandidates'])) != manifest['locationCandidates']:
        raise ValueError('Archived save location metadata changed')
    profile_selection((folder/'profile-template.pcsav').read_bytes())
    return manifest


def freeze(package, save, profile):
    package, save, profile = Path(package), Path(save), Path(profile)
    info = save_info(save)
    if not re.fullmatch(re.escape(info['career'])+r'_(?:[0-9]+|AutoSave|QuickSave)', save.stem):
        raise ValueError('Save filename must retain its original career and slot')
    if len(info['locationCandidates']) != 1 or info['locationCandidates'][0]['map'] != 'BIOA_STA00':
        raise ValueError('Expected one unambiguous Citadel location record in this save')
    template = profile.read_bytes()
    resume_profile(template, info['career'], save.stem)  # Admission; retain the unedited template.
    bound = package/'start.json'
    if bound.exists() and json.loads(bound.read_text(encoding='utf-8-sig'))['saveSha256'] != info['sha256']:
        raise ValueError('Save differs from the bound replay start; use a new package')
    target = package/'fixture'
    target.mkdir(exist_ok=False)
    (target/'save.pcsav').write_bytes(save.read_bytes())
    if sha(target/'save.pcsav') != info['sha256']:
        raise ValueError('Source save changed during freeze')
    (target/'profile-template.pcsav').write_bytes(template)
    manifest = dict(schemaVersion=1, kind='citadel-save-fixture', career=info['career'], slot=save.stem,
                    saveSha256=info['sha256'], profileSha256=sha(target/'profile-template.pcsav'),
                    sourceSave=str(save.resolve()), locationCandidates=info['locationCandidates'],
                    originalWalkthroughSlotConfirmed=False, nativeLoadValidated=False)
    (target/'fixture.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    return verify(package)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    p=sub.add_parser('inspect');p.add_argument('save',type=Path)
    p=sub.add_parser('freeze');p.add_argument('package',type=Path);p.add_argument('save',type=Path);p.add_argument('profile',type=Path)
    p=sub.add_parser('verify');p.add_argument('package',type=Path)
    p=sub.add_parser('resume-profile');p.add_argument('package',type=Path);p.add_argument('profile',type=Path);p.add_argument('output',type=Path)
    args=parser.parse_args()
    if args.action=='inspect': result=save_info(args.save)
    elif args.action=='freeze': result=freeze(args.package,args.save,args.profile)
    elif args.action=='verify': result=verify(args.package)
    else:
        manifest=verify(args.package)
        if args.output.exists(): raise ValueError('Output already exists')
        args.output.write_bytes(resume_profile(args.profile.read_bytes(),manifest['career'],manifest['slot']))
        result=dict(output=str(args.output),sha256=sha(args.output),career=manifest['career'],slot=manifest['slot'])
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    try: main()
    except (ValueError,OSError,KeyError,struct.error,zlib.error) as error: raise SystemExit(str(error))
