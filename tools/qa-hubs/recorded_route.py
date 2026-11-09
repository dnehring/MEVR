# SPDX-License-Identifier: MIT
"""Inspect/package a private Citadel input recording; never invent world-space waypoints."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mevrrec import FRAME, HEADER, MAGIC, read


def inspect(path):
    path = Path(path)
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError('Truncated recording header')
    magic, version, header_bytes, frame_bytes, flags, created = HEADER.unpack_from(data)
    if (magic, version, header_bytes, frame_bytes) != (MAGIC, 1, HEADER.size, FRAME.size):
        raise ValueError('Expected the current complete v1 recording layout')
    if (len(data) - header_bytes) % frame_bytes:
        raise ValueError('Truncated recording frame')
    count = (len(data) - header_bytes) // frame_bytes
    if count < 2:
        raise ValueError('Recording needs at least two frames')
    previous = -1
    for i in range(count):
        values = FRAME.unpack_from(data, header_bytes + i * frame_bytes)
        if values[0] <= previous or (i == 0 and values[0] != 0):
            raise ValueError('Recording timestamps must start at zero and strictly increase')
        if any(not math.isfinite(x) for x in values):
            raise ValueError('Non-finite pose/input value')
        previous = values[0]
    _, frames = read(path)
    duration = frames[-1]['timeS']
    if not 10 <= duration <= 600:
        raise ValueError('Expected a bounded 10-600 second walk')
    active = []
    for f in frames:
        if not f['headValid']:
            raise ValueError('The walk requires valid head tracking throughout')
        if not f['controllersActive'] and any(abs(f[f'{hand}.{key}']) > .001 for hand in ('left', 'right')
                for key in ('stickX', 'stickY', 'trigger', 'squeeze', 'primary', 'secondary', 'menu', 'stickClick')):
            raise ValueError('Inactive controllers contain non-neutral input')
        for hand in ('left', 'right'):
            for axis in ('stickX', 'stickY', 'trigger', 'squeeze'):
                v = f[f'{hand}.{axis}']
                if abs(v) > 1.001 or (axis in ('trigger', 'squeeze') and v < 0):
                    raise ValueError('Controller axis out of range')
        if any(abs(f[f'{hand}.{axis}']) > .15 for hand in ('left', 'right') for axis in ('stickX', 'stickY')):
            active.append(f['timeS'])
    # Finishing in neutral prevents a held button/stick at the return to live tracking.
    tail = [f for f in frames if f['timeS'] >= duration - .5]
    for f in tail:
        if any(abs(f[f'{hand}.{key}']) > .05 for hand in ('left', 'right')
               for key in ('stickX', 'stickY', 'trigger', 'squeeze', 'primary', 'secondary', 'menu', 'stickClick')):
            raise ValueError('The last half-second must have neutral buttons and sticks')
    return dict(spdxLicenseIdentifier='MIT', schemaVersion=1, kind='citadel-recorded-walk',
                status='replay-candidate', expectedMap='BIOA_STA00', recording='walk.mevrrec',
                sha256=hashlib.sha256(data).hexdigest().upper(), frames=count, durationSeconds=duration,
                firstStickSeconds=active[0] if active else None, lastStickSeconds=active[-1] if active else None,
                inactiveControllerFrames=sum(not f['controllersActive'] for f in frames),
                recordedWorldWaypoints=False, exactOriginalStartPoseKnown=False,
                nativeRouteValidated=False, cleanPerformanceCapture=False)


def parse_world(text):
    import re
    state = re.search(r'map (\w+), game mode (\d+), pawn .*?\(BioVehicleBase: (yes|no)\)', text)
    number = r'(-?\d+(?:\.\d+)?)'
    pose = re.search(r'loc ' + ' '.join([number] * 3) + r' rot (-?\d+) (-?\d+) (-?\d+)', text)
    if not state or not pose:
        raise ValueError('Missing map, gameplay state or pawn location')
    if state[1] != 'BIOA_STA00' or state[2] != '0' or state[3] != 'no':
        raise ValueError('Expected on-foot Citadel gameplay (BIOA_STA00, mode 0)')
    return dict(map=state[1], gameMode=0, positionUU=[float(pose[i]) for i in (1, 2, 3)],
                yawUnreal=int(pose[5]))


def check_start(observed, expected):
    if expected.get('map') != 'BIOA_STA00' or expected.get('gameMode') != 0:
        raise ValueError('Invalid bound start')
    pos = expected['positionUU']
    if len(pos) != 3 or any(not math.isfinite(x) for x in pos):
        raise ValueError('Invalid bound position')
    distance = math.dist(observed['positionUU'], pos)
    yaw = abs((observed['yawUnreal'] - expected['yawUnreal'] + 32768) % 65536 - 32768)
    if distance > 60 or yaw > 1024:
        raise ValueError(f'Start mismatch: {distance:.1f} UU, yaw delta {yaw}; reload the bound save and facing')
    return dict(distanceUU=distance, yawDeltaUnreal=yaw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    p = sub.add_parser('prepare')
    p.add_argument('recording', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--expected-sha256', required=True)
    p = sub.add_parser('verify')
    p.add_argument('package', type=Path)
    p = sub.add_parser('world')
    p.add_argument('observation', type=Path)
    p.add_argument('--fixture', type=Path)
    args = parser.parse_args()
    if args.action == 'prepare':
        manifest = inspect(args.recording)
        if manifest['sha256'] != args.expected_sha256.upper():
            raise ValueError('Source recording hash does not match')
        args.output.mkdir(parents=True, exist_ok=False)
        (args.output / 'walk.mevrrec').write_bytes(args.recording.read_bytes())
        if inspect(args.output / 'walk.mevrrec') != manifest:
            raise ValueError('Recording changed while packaging')
        (args.output / 'route.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(manifest))
    elif args.action == 'verify':
        manifest = json.loads((args.package / 'route.json').read_text(encoding='utf-8-sig'))
        actual = inspect(args.package / 'walk.mevrrec')
        if actual != manifest:
            raise ValueError('Private route manifest/recording changed; prepare a new package')
        print(json.dumps(actual))
    else:
        world = parse_world(args.observation.read_text(encoding='utf-8-sig'))
        if args.fixture:
            fixture = json.loads(args.fixture.read_text(encoding='utf-8-sig'))
            world['startCheck'] = check_start(world, fixture['start'])
        print(json.dumps(world))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, struct.error) as error:
        raise SystemExit(str(error))
