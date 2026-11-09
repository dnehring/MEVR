# SPDX-License-Identifier: MIT
"""Fingerprint private starting saves; never distribute save contents or infer the selected slot from location."""
import argparse
import hashlib
import json
from pathlib import Path


def fingerprint(profile, selected=None, expected=None):
    profile = Path(profile).resolve()
    files = [dict(path=p.relative_to(profile).as_posix(), sha256=hashlib.sha256(p.read_bytes()).hexdigest().upper())
             for p in sorted(profile.rglob('*.pcsav')) if p.is_file()]
    if not files: raise ValueError('No LE1 save files in the protected profile')
    inventory_hash = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(',', ':')).encode()).hexdigest().upper()
    selected_hash = None
    if selected:
        selected_hash = hashlib.sha256(Path(selected).read_bytes()).hexdigest().upper()
        if not expected or selected_hash != expected.upper(): raise ValueError('Selected fixture hash differs from the reviewed hash')
        if selected_hash not in {f['sha256'] for f in files}: raise ValueError('Selected fixture is not present in the protected player profile')
    return dict(inventorySha256=inventory_hash, selectedSha256=selected_hash, files=files,
                selectionEvidence='operator-attested Resume selection; bytes present in protected profile; not an engine-confirmed slot' if selected_hash else 'inventory only; selected Resume slot unverified')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('profile', type=Path)
    p.add_argument('--selected', type=Path)
    p.add_argument('--expected-sha256')
    a = p.parse_args()
    try: print(json.dumps(fingerprint(a.profile, a.selected, a.expected_sha256), indent=2))
    except (ValueError, OSError) as e:
        print(json.dumps({'error': str(e)})); raise SystemExit(1)
