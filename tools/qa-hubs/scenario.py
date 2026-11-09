# SPDX-License-Identifier: MIT
"""Validate declarative LE1 walk scenarios. Draft routes are inspectable but never executable."""
import argparse
import hashlib
import json
import math
import re
from pathlib import Path


def validate(s, for_run=False):
    errors = []
    def need(ok, message):
        if not ok: errors.append(message)
    def number(v, lo, hi):
        return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) and lo <= v <= hi
    def vec(v):
        return isinstance(v, list) and len(v) == 3 and all(number(x, -1e7, 1e7) for x in v)
    def strings(v):
        return isinstance(v, list) and all(isinstance(x, str) and x.strip() for x in v)
    required = ('spdxLicenseIdentifier', 'schemaVersion', 'id', 'label', 'game', 'status',
                'expectedMap', 'start', 'steps', 'capture', 'prerequisites', 'evidence')
    need(all(k in s for k in required), 'missing required manifest field')
    need(strings(s.get('prerequisites')), 'prerequisites must be a list of nonempty strings')
    evidence = s.get('evidence')
    need(isinstance(evidence, dict), 'evidence must be an object')
    if isinstance(evidence, dict):
        if 'sources' in evidence:
            need(strings(evidence['sources']), 'evidence.sources must be a list of nonempty strings')
        if 'routeReceipt' in evidence:
            need(isinstance(evidence['routeReceipt'], str) and bool(evidence['routeReceipt'].strip()),
                 'evidence.routeReceipt must be a nonempty string reference')
        for key in ('nativeRouteValidated', 'nativeTraversalValidated'):
            if key in evidence:
                need(isinstance(evidence[key], bool), f'evidence.{key} must be boolean')
    need(s.get('schemaVersion') == 1, 'schemaVersion must be 1')
    need(s.get('spdxLicenseIdentifier') == 'MIT', 'spdxLicenseIdentifier must be MIT')
    need(isinstance(s.get('id'), str) and re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', s['id']), 'invalid scenario id')
    need(isinstance(s.get('label'), str) and bool(s['label'].strip()), 'label required')
    need(s.get('game') == 'LE1', 'only LE1 is supported')
    need(s.get('status') in ('draft', 'ready', 'validated'), 'invalid status')
    executable = s.get('status') in ('ready', 'validated')
    need(not for_run or executable, 'draft scenario cannot execute')
    if executable or s.get('expectedMap') is not None:
        need(isinstance(s.get('expectedMap'), str) and re.fullmatch(r'BIOA_[A-Z0-9]+', s['expectedMap']), 'invalid expectedMap')
    start = s.get('start')
    if executable or start is not None:
        need(isinstance(start, dict), 'start required')
        if isinstance(start, dict):
            need(vec(start.get('positionUU')), 'start.positionUU must contain three finite measured coordinates')
            need(number(start.get('yawUnreal'), -32768, 65535), 'start.yawUnreal required')
            need(number(start.get('positionToleranceUU', 60), 1, 200), 'invalid starting position tolerance')
            need(number(start.get('yawToleranceUnreal', 512), 1, 4096), 'invalid starting yaw tolerance')
    steps = s.get('steps')
    need(isinstance(steps, list), 'steps must be a list')
    if isinstance(steps, list):
        need(not executable or bool(steps), 'executable route needs steps')
        need(len(steps) <= 100, 'route has too many steps')
        for i, step in enumerate(steps):
            if not isinstance(step, dict):
                errors.append(f'step {i} must be an object'); continue
            kind = step.get('type')
            need(kind in ('walk', 'look', 'pulse', 'use', 'checkpoint', 'wait'), f'unsupported step {i}')
            if kind in ('walk', 'checkpoint'):
                need(vec(step.get('positionUU')), f'step {i} needs measured positionUU')
                need(isinstance(step.get('name'), str) and bool(step['name']), f'step {i} needs name')
                need(number(step.get('toleranceUU', 50), 1, 200), f'invalid tolerance at step {i}')
            if kind in ('look', 'pulse'):
                need(number(step.get('yawDegrees'), -720, 720), f'invalid yaw at step {i}')
                need(number(step.get('pitchDegrees', 0), -70, 70), f'invalid pitch at step {i}')
            if kind in ('pulse', 'wait'):
                need(number(step.get('seconds'), .1, 20), f'invalid duration at step {i}')
            if kind == 'pulse':
                need(number(step.get('stick', .55), -.75, .75), f'invalid stick at step {i}')
        need(not executable or any(x.get('type') == 'walk' for x in steps if isinstance(x, dict)), 'executable walk route needs a walk step')
        need(not executable or (bool(steps) and isinstance(steps[-1], dict) and steps[-1].get('type') in ('walk', 'checkpoint')),
             'executable route must end at a checked position')
    capture = s.get('capture', {})
    need(isinstance(capture, dict), 'capture must be an object')
    if isinstance(capture, dict):
        if executable:
            need(all(k in capture for k in ('seconds', 'yawDegrees', 'pitchDegrees', 'sweepDegrees')), 'executable capture needs seconds/yawDegrees/pitchDegrees/sweepDegrees')
        need(capture.get('seconds', 90) == 90, 'initial hub regression workload requires 90-second captures')
        need(number(capture.get('yawDegrees', 130), -720, 720), 'invalid capture yaw')
        need(number(capture.get('pitchDegrees', -10), -70, 70), 'invalid capture pitch')
        need(number(capture.get('sweepDegrees', 60), 1, 360), 'invalid capture sweep')
    if s.get('status') == 'validated':
        need(isinstance(evidence, dict) and isinstance(evidence.get('routeReceipt'), str)
             and bool(evidence['routeReceipt'].strip()), 'validated needs a routeReceipt reference')
    return errors


def load(path, for_run=False):
    s = json.loads(Path(path).read_text(encoding='utf-8-sig'))
    if not isinstance(s, dict): raise ValueError('scenario must be an object')
    errors = validate(s, for_run)
    if errors: raise ValueError('; '.join(errors))
    return s


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('scenario', type=Path)
    p.add_argument('--for-run', action='store_true')
    a = p.parse_args()
    try:
        raw = a.scenario.read_bytes()
        s = json.loads(raw.decode('utf-8-sig'))
        if not isinstance(s, dict): raise ValueError('scenario must be an object')
        errors = validate(s, a.for_run)
        if errors: raise ValueError('; '.join(errors))
        print(json.dumps({'valid': True, 'id': s['id'], 'status': s['status'], 'executable': s['status'] != 'draft',
                          'performanceAccepted': False, 'sha256': hashlib.sha256(raw).hexdigest().upper(), 'scenario': s}, indent=2))
    except (OSError, ValueError, TypeError) as e:
        print(json.dumps({'valid': False, 'error': str(e)})); raise SystemExit(1)
