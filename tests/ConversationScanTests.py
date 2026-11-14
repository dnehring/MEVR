# SPDX-License-Identifier: MIT
"""Exercise read-only scanner CLI; optional installed-corpus checks keep outputs temporary.

No game launch, save access, package edits or embedded game data. Supply a built
scanner and optionally installed cooked directories / the previous scanner binary.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--scanner', required=True)
    p.add_argument('--lex-dir')
    p.add_argument('--le2-cooked')
    p.add_argument('--le1-cooked')
    p.add_argument('--baseline-scanner')
    args = p.parse_args()
    checked = []

    def run(arguments, expected=0, scanner=None):
        command = [str(Path(scanner or args.scanner).resolve()), *map(str, arguments)]
        if args.lex_dir:
            command += ['--lex-dir', args.lex_dir]
        result = subprocess.run(command, text=True, capture_output=True, timeout=120)
        if result.returncode != expected:
            raise AssertionError(f'Expected exit {expected}, got {result.returncode}: {result.stderr[:1000]}')
        return result

    def check(condition, label):
        if not condition:
            raise AssertionError(label)
        checked.append(label)

    result = run(['--self-test', '--game', 'LE2'])
    check('15 synthetic inspection checks passed' in result.stdout, 'synthetic property invariants')
    for options in (['--game'], ['--game', 'LE3'], ['--game', 'LE1', '--game', 'LE2']):
        run(['unused', 'unused', *options], expected=2)
    for mode in ('--wheels', '--vo-extract', '--state-events', '--references'):
        run(['unused', 'unused', '--game', 'LE2', mode, 'unused'], expected=2)
    checked.append('invalid game and unsupported LE2 modes rejected')

    with tempfile.TemporaryDirectory(prefix='mevr-conversation-inspection-') as temporary:
        root = Path(temporary)
        empty = root / 'empty'
        empty.mkdir()
        run([empty, root / 'empty.jsonl', '--game', 'LE2'], expected=1)
        run([empty, root / 'empty.txt', '--game', 'LE2', '--class-dump', 'BioConversation'], expected=1)
        checked.append('empty LE2 inspection cannot report success')
        untouched = root / 'existing.txt'
        untouched.write_text('preserve prior evidence')
        run([empty, untouched, '--game', 'LE2'], expected=1)
        check(untouched.read_text() == 'preserve prior evidence', 'existing output cannot be overwritten')
        run([empty, empty / 'new.txt', '--game', 'LE2'], expected=1)
        check(not (empty / 'new.txt').exists(), 'LE2 output cannot write into package directory')

        if args.le2_cooked:
            cooked = Path(args.le2_cooked)
            stem = 'BioD_OmgGrA_112Hangar_LOC_INT'
            package = cooked / (stem + '.pcc')
            before = hashlib.sha256(package.read_bytes()).hexdigest()
            output = root / 'le2.jsonl'
            run([cooked, output, '--game', 'LE2', '--only', stem])
            rows = [json.loads(line) for line in output.read_text(encoding='utf-8-sig').splitlines()]
            conversations = [r for r in rows if r['kind'] == 'conversation']
            interrupts = [r for r in rows if r['kind'] == 'interrupt']
            check(len(conversations) == 1 and len(interrupts) == 1, 'installed candidate contains conversation and interrupt')
            conv, interrupt = conversations[0], interrupts[0]
            check(all(n['text'] > 0 for n in conv['nodes']), 'installed LE2 StringRef text survives')
            check(any(edge['category'] == 'REPLY_CATEGORY_RENEGADE_INTERRUPT'
                      for node in conv['nodes'] for edge in node['replyEdges']), 'native reply category captured')
            check(interrupt['warnings'] == [] and len(interrupt['keys']) == 2
                  and interrupt['keys'][0]['showInterrupt'] is True
                  and interrupt['keys'][1]['showInterrupt'] is False
                  and interrupt['keys'][0]['timeSeconds'] < interrupt['keys'][1]['timeSeconds'],
                  'authored visibility keys retain order and both edges')
            check(interrupt['conversations'][0]['uIndex'] == conv['exportIndex']
                  and interrupt['sequence'] == conv['matineeSequence'], 'track resolves conversation sequence')
            linked = [row for row in rows if row['kind'] == 'cinematic'
                      if any(data == interrupt['interpData'] for matinee in row['matinees'] for data in matinee.get('data', []))]
            check(bool(linked) and all(any(n['exportId'] == row['props'].get('m_nNodeID')
                                          for n in conv['nodes'] + conv['replyNodes']) for row in linked),
                  'interrupt interpolation data joins to authored conversation node')
            check(all(row['game'] == 'LE2' and row['packageSha256'] == before
                      and Path(row['packageFile']).resolve() == package.resolve() for row in rows),
                  'every candidate record pins actual game/package bytes')
            run([cooked, root / 'wrong-game.jsonl', '--only', stem], expected=1)
            sequence = root / 'node-sequence.txt'
            run([cooked, sequence, '--game', 'LE2', '--sequence', stem + ':Node_Data_Sequence'])
            check('BioEvtSysTrackInterrupt' in sequence.read_text(), 'conversation sequence traversal reaches track')
            reload_sequence = root / 'reload-sequence.txt'
            run([cooked, reload_sequence, '--game', 'LE2', '--sequence', 'BioD_ProCer_100RezRoom:LS10_Reload_Heatsink'])
            reload_text = reload_sequence.read_text()
            check('SFXSeqEvt_TutorialReload' in reload_text and 'SFXSeqAct_SaveGame' in reload_text,
                  'reload tutorial acquisition events visible')
            declaration = root / 'native-declaration.txt'
            run([cooked, declaration, '--game', 'LE2', '--script', 'BioConversation.SelectInterruption', '--only', 'SFXGame'])
            check('native function bool SelectInterruption' in declaration.read_text(),
                  'LE2 script inspection identifies native declaration without inventing a body')
            check(hashlib.sha256(package.read_bytes()).hexdigest() == before, 'inspected package remains unchanged')

        if args.le1_cooked:
            output = root / 'le1.jsonl'
            options = ['--only', 'BIOA_STA70_02captain_DSG_LOC_INT']
            run([args.le1_cooked, output, *options])
            rows = [json.loads(line) for line in output.read_text(encoding='utf-8-sig').splitlines()]
            check(bool(rows) and any(r['kind'] == 'conversation' for r in rows), 'LE1 default still produces real records')
            explicit = root / 'le1-explicit.jsonl'
            run([args.le1_cooked, explicit, '--game', 'LE1', *options])
            check(output.read_bytes() == explicit.read_bytes(), 'LE1 explicit/default output identical')
            if args.baseline_scanner:
                baseline = root / 'baseline.jsonl'
                run([args.le1_cooked, baseline, *options], scanner=args.baseline_scanner)
                check(output.read_bytes() == baseline.read_bytes(), 'LE1 output byte-identical to previous scanner')

    print(json.dumps({'checksPassed': len(checked), 'checks': checked,
                      'le2CorpusChecked': bool(args.le2_cooked), 'le1CorpusChecked': bool(args.le1_cooked),
                      'nativeRuntimeAcceptance': False}, indent=2))


if __name__ == '__main__':
    main()
