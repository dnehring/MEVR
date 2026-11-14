# SPDX-License-Identifier: MIT
"""Bounded rejection checks: only empty ping requests, never gameplay commands.

Uses the ordinary client's lock, sequence and pending-intent files. A timeout or
unexpected reply blocks further traffic; collect reconciles without resending.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import time
import uuid

_spec = importlib.util.spec_from_file_location('le2_control', Path(__file__).with_name('control.py'))
c = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(c)
REASONS = {'wrong-lease': 'identity mismatch', 'expired': 'expired or excessive deadline'}


def receive(run, directory, identity, request, case, bind):
    source = c.session.safe_path(directory / 'result.control')
    if not source.exists():
        source = c.session.safe_path(directory / 'history' / (request['id'] + '.result'))
    result = c.decode(source.read_bytes())
    if result['status'] != 'rejected':
        raise c.ControlError('Negative control was not rejected; evidence and pending intent retained')
    payload = json.loads(result['payload'])
    if (not isinstance(payload, dict) or payload.get('source') != 'diagnostic-host'
            or payload.get('ready') is not False or payload.get('reason') != REASONS[case]):
        raise c.ControlError('Unexpected rejection reason; evidence and pending intent retained')
    # The producer must report its OWN identity, not echo the deliberately bad
    # lease. All other correlation, consumption, archival and binding checks are
    # the existing client's checks. The archived request remains unmodified.
    expected = dict(request, **identity)
    return c.receive(run, directory, identity, expected, bind)


def probe(run, case, timeout=10, *, bind=c.binding):
    if case not in REASONS or type(timeout) is not int or not 1 <= timeout <= 30:
        raise c.ControlError('Choose wrong-lease or expired with timeout 1..30 seconds')
    directory, identity = bind(run)
    token = str(uuid.uuid4())
    lock = c.session.safe_path(directory / 'client.lock')
    with lock.open('x', encoding='ascii') as stream:
        stream.write(token)
    try:
        pending = c.session.safe_path(directory / 'outstanding.json')
        if any(c.session.safe_path(directory / name).exists()
               for name in ('outstanding.json', 'request.control', 'result.control')):
            raise c.ControlError('Unresolved traffic exists; no negative control sent')
        history = c.session.safe_path(directory / 'history')
        history.mkdir(exist_ok=True)
        sequence_path = c.session.safe_path(directory / 'sequence.txt')
        sequence = int(sequence_path.read_text()) + 1 if sequence_path.exists() else 1
        now = time.time_ns() // 1000000
        request = dict(identity, kind='request', id=token, sequence=str(sequence),
                       deadline=str(now - 1 if case == 'expired' else now + timeout * 1000),
                       verb='ping', status='pending', bytes='0', payload=b'')
        if case == 'wrong-lease':
            request['lease'] = str(uuid.uuid4())
            if request['lease'] == identity['lease']:
                raise c.ControlError('Negative lease unexpectedly equals live lease')
        wire = c.encode(request)
        c.session.write_json(pending, dict(id=token, sequence=sequence, status='pending',
                                          identity=identity, negativeCase=case))
        sequence_path.write_text(str(sequence), encoding='ascii')
        c.publish(history / (token + '.request'), wire)
        # Revalidate ownership immediately before publication as well as reply.
        if bind(run) != (directory, identity):
            raise c.ControlError('Binding changed before negative control publication')
        c.publish(directory / 'request.control', wire)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if (directory / 'result.control').exists():
                return receive(run, directory, identity, request, case, bind)
            time.sleep(.05)
        raise c.ControlError('Negative control timed out; retain intent and collect, never resend')
    finally:
        if lock.read_text(encoding='ascii') == token:
            lock.unlink()


def collect(run, *, bind=c.binding):
    directory, identity = bind(run)
    token = str(uuid.uuid4())
    lock = c.session.safe_path(directory / 'client.lock')
    with lock.open('x', encoding='ascii') as stream:
        stream.write(token)
    try:
        pending = c.session.read_json(c.session.safe_path(directory / 'outstanding.json'))
        case = pending.get('negativeCase')
        if (case not in REASONS or pending.get('identity') != identity
                or not c.valid_uuid(pending.get('id'))):
            raise c.ControlError('Pending negative control identity is invalid')
        request = c.decode(c.session.safe_path(directory / 'history' / (pending['id'] + '.request')).read_bytes())
        if (request['kind'] != 'request' or request['verb'] != 'ping' or request['payload']
                or request['id'] != pending['id'] or request['sequence'] != str(pending['sequence'])
                or any(request[k] != identity[k] for k in c.IDENTITY if k != 'lease')
                or (case == 'wrong-lease' and request['lease'] == identity['lease'])
                or (case == 'expired' and (request['lease'] != identity['lease']
                                          or int(request['deadline']) >= time.time_ns() // 1000000))):
            raise c.ControlError('Archived negative control provenance is invalid')
        return receive(run, directory, identity, request, case, bind)
    finally:
        if lock.read_text(encoding='ascii') == token:
            lock.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('case', choices=(*REASONS, 'collect'))
    parser.add_argument('--timeout', type=int, default=10)
    args = parser.parse_args()
    try:
        result = collect(args.run.absolute()) if args.case == 'collect' else probe(args.run.absolute(), args.case, args.timeout)
        print(json.dumps(dict(result, payload=result['payload'].decode('utf-8')), indent=2))
        return 0
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(f'Negative control unresolved: {error}')
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
