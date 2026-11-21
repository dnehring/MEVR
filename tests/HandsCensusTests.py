# SPDX-License-Identifier: MIT
"""Tests for tools/hands-census.py (research/74, the hands census), over synthetic scenes written the way
`conversation-scan --hands-census` writes them (made-up names in the games' naming styles; no game data).
Run: python -I tests/HandsCensusTests.py (ctest mevr-hands-census)."""
import importlib.util
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('hands_census', os.path.join(HERE, '..', 'tools', 'hands-census.py'))
hc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hc)
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print('FAILED:', what)


def gesture_track(actor, *keys, prop_keys=None):
    """A conversation track (BioEvtSysTrackGesture) for an actor, its keys (time, gesture anim)."""
    return {'class': 'BioEvtSysTrackGesture', 'actor': actor,
            'gestures': [{'t': t, 'nmGestureSet': 'HMM_Test', 'nmGestureAnim': a} for t, a in keys]}


def prop_track(actor, *keys):
    """A prop track: keys (time, prop, mesh, equip)."""
    return {'class': 'BioEvtSysTrackProp', 'actor': actor, 'keyTimes': [k[0] for k in keys], 'keyCount': len(keys),
            'props': {'m_aPropKeys': [{'nmProp': k[1], 'pPropMesh': 'Pkg.' + k[2], 'nmAction': 'None', 'bEquip': k[3]} for k in keys]}}


def anim_track(*keys):
    return {'class': 'InterpTrackAnimControl', 'anims': [{'t': t, 'end': t + 2, 'anim': a, 'rate': 1, 'loop': False} for t, a in keys]}


def scene(path, groups, game='LE3', package='BioD_Test_100Bar', conversation=False, length=10.0, siblings=(), comment=None, file=None):
    return {'kind': 'scene', 'game': game, 'package': package, 'file': file or ('CookedPCConsole\\' + package + '.pcc'), 'path': path,
            'sequence': path.rsplit('.', 1)[0], 'inConversation': conversation, 'length': length, 'groups': groups,
            'siblings': list(siblings), 'upstream': [], 'downstream': [], 'comment': comment}


def player_group(*tracks, name='Player'):
    return {'class': 'InterpGroup', 'name': name, 'bound': [{'var': 'SeqVar_Player'}], 'tracks': list(tracks)}


def kind_of(s):
    f = hc.classify(s)
    return (f['kind'], f['confidence']) if f else None


# 1. A level matinee at a bar: the player variable, a drink gesture, a glass prop: drink, high; the pause at the gesture's start.
bar = scene('TheWorld.PersistentLevel.Main_Sequence.Drinks.SeqAct_Interp_0', [
    {'class': 'InterpGroupDirector', 'name': None, 'tracks': [{'class': 'InterpTrackDirector', 'cuts': 4}]},
    player_group(gesture_track('Player', (0.5, 'DP_LeanBar_HeadUp_Enter'), (3.9, 'DP_LeanBar_Drink')),
                 prop_track('Player', (4.4, 'Test_Glass_Drink', 'GlassShort09', True))),
], comment='Shep drinks')
f = hc.classify(bar)
check(f and f['kind'] == 'drink' and f['confidence'] == 'high', f'a bar drink is drink/high: {f and (f["kind"], f["confidence"])}')
check(f and f['pauseAt'] == 3.9, f'the pause is the drink gesture\'s start: {f and f["pauseAt"]}')
check(f and f['cuts'] == 4 and not f['conversation'], 'the director\'s cuts counted, a level scene')

# 2. Words: each kind from a gesture name on a conversation track for the player.
for anim, want in (('FC_ButtonPress01', 'press'), ('WI_TypeStanding1Idle', 'press'), ('CB_KeypadArmed_Rifle_Enter', 'press'),
                   ('DP_SaluteIdle', 'salute'), ('2P_ShakeHand', 'handshake'), ('2P_HandShake', 'handshake'),
                   ('DP_PickUpObj_Stand_Enter', 'take'), ('DP_TakePistol', 'take'), ('DP_SitByBed_LeanIn_GiveObject', 'give'),
                   ('2P_Kissing_Male_Female_A_Idle', 'other-actor'), ('2P_HoldHand_A_Idle', 'other-actor'),
                   ('DG_Stomach', 'other-actor'), ('2P_PunchFace_Shep', 'other-actor'), ('2P_SomethingTogether', 'other-actor'),
                   ('WI_OmniIdle', 'other-hands'), ('DP_PistolThreatenIdle', 'other-hands'), ('DL_DeniedHandShake', 'other-hands'),
                   ('DL_HeadShake', 'not-hands'), ('DL_PointRight', 'not-hands'), ('DL_Temple_Tap', 'not-hands'),
                   ('ForceFieldTap', 'press'), ('AM_WeightShift01', 'not-hands'),
                   ('DL_Injured_WalkForward_Enter', 'not-hands'), ('2P_BarStool_Idle', 'not-hands'), ('Wander_LongWalkEnter', 'not-hands'),
                   ('Test004_Farewell_3', 'unclassified')):
    s = scene('test_dlg.Node_Data_Sequence.SeqAct_Interp_1', [
        {'class': 'InterpGroup', 'name': 'Conversation', 'tracks': [gesture_track('Player', (0.2, anim)), gesture_track('Owner', (0.0, 'DL_Idle'))]}],
        conversation=True)
    got = kind_of(s)
    check(got and got[0] == want, f'{anim} -> {want}: {got}')

# 3. A handshake with a kiss in the same scene: someone acts on him, the scene stays authored.
s = scene('test_dlg.Node_Data_Sequence.SeqAct_Interp_2', [
    {'class': 'InterpGroup', 'name': 'Conversation', 'tracks': [gesture_track('Player', (0.1, '2P_ShakeHand'), (2.0, '2P_Kissing_A'))]}],
    conversation=True)
check(kind_of(s) == ('other-actor', 'high'), f'a contact outranks a gesture: {kind_of(s)}')

# 4. Tracks for someone else only: Shepard is not in the scene.
s = scene('test_dlg.Node_Data_Sequence.SeqAct_Interp_3', [
    {'class': 'InterpGroup', 'name': 'Conversation', 'tracks': [gesture_track('Owner', (0.1, 'DP_LeanBar_Drink'))]}], conversation=True)
check(hc.classify(s) is None, 'a drink of the owner\'s is not Shepard\'s')

# 4b. ME1's conversation lines tag every actor but the player: an untagged gesture track there is Shepard's (medium); not on ME2.
s = scene('test_N.Node_Data_Sequence.SeqAct_Interp_9', [
    {'class': 'InterpGroup', 'name': 'Conversation', 'tracks': [{'class': 'BioEvtSysTrackGesture', 'gestures': [{'t': 0.4, 'nmGestureAnim': 'DP_LeanBar_Drink'}]},
                                                             gesture_track('Owner', (0.0, 'DL_Idle'))]}], game='LE1', conversation=True)
check(kind_of(s) == ('drink', 'medium'), f'ME1: an untagged line track is Shepard\'s: {kind_of(s)}')
s['game'] = 'LE2'
check(hc.classify(s) is None, 'ME2: an untagged track is nobody\'s')

# 5. How he is bound: a find-by-tag for the player, a named variable, a sub-sequence's external variable, a group that finds
# "Player" (ME3's SFX groups), his group's name alone (medium), a stunt double (low).
bindings = (
    ([{'var': 'BioSeqVar_ObjectFindByTag', 'm_sObjectTagToFind': 'player'}], 'Pawn', 'high'),
    ([{'var': 'SeqVar_Named', 'named': [{'var': 'SeqVar_Player'}]}], 'Pawn', 'high'),
    ([{'var': 'SeqVar_External', 'outer': [{'var': 'SeqVar_External', 'outer': [{'var': 'SeqVar_Player'}]}]}], 'Pawn', 'high'),
    ([], 'HMM_PC_', 'medium'),
    ([{'var': 'SeqVar_Object', 'objClass': 'SkeletalMeshActor', 'obj': 'SkeletalMeshActor_3'}], 'PC_', 'low'),
)
for bound, name, want in bindings:
    s = scene('TheWorld.PersistentLevel.Main_Sequence.Bind.SeqAct_Interp_0',
              [{'class': 'InterpGroup', 'name': name, 'bound': bound, 'tracks': [anim_track((1.0, 'FC_ButtonPress02'))]}])
    got = kind_of(s)
    check(got == ('press', want), f'bound {name} {bound} -> press/{want}: {got}')
s = scene('TheWorld.PersistentLevel.Main_Sequence.Bind.SeqAct_Interp_1',
          [{'class': 'InterpGroup', 'name': 'Shep1', 'name:m_nmSFXFindActor': 'Player', 'bound': [], 'tracks': [anim_track((1.0, 'FC_ButtonPress02'))]}])
check(kind_of(s) == ('press', 'high'), f'a group that finds Player is explicit: {kind_of(s)}')
s = scene('TheWorld.PersistentLevel.Main_Sequence.Bind.SeqAct_Interp_2',
          [{'class': 'InterpGroup', 'name': 'Guard', 'bound': [{'var': 'SeqVar_Object', 'objClass': 'BioPawn'}], 'tracks': [anim_track((1.0, 'FC_ButtonPress02'))]}])
check(hc.classify(s) is None, 'a guard pressing a button is not Shepard')

# 6. Props: a datapad passed from the owner to him is take; from him to the owner is give; an exchange with the thing in his hand
# before it is give; a weapon held is other-hands; an attach of a bottle to him beside a level matinee is drink (medium).
def passing(first_owner):
    owner_t, shep_t = (0.5, 2.0) if first_owner else (2.0, 0.5)
    return scene('test_dlg.Node_Data_Sequence.SeqAct_Interp_4', [{'class': 'InterpGroup', 'name': 'Conversation', 'tracks': [
        gesture_track('Player', (0.1, 'DL_Idle')), prop_track('Player', (shep_t, 'Datapad_Test', 'Datapad01', True)),
        prop_track('Owner', (owner_t, 'Datapad_Test', 'Datapad01', True))]}], conversation=True)
check(kind_of(passing(True)) == ('take', 'medium'), f'a datapad from the owner is take: {kind_of(passing(True))}')
check(kind_of(passing(False)) == ('give', 'medium'), f'a datapad to the owner is give: {kind_of(passing(False))}')
s = scene('test_dlg.Node_Data_Sequence.SeqAct_Interp_5', [{'class': 'InterpGroup', 'name': 'Conversation', 'tracks': [
    gesture_track('Player', (3.0, '2P_ExchangeOne')), prop_track('Player', (0.0, 'Tags', 'DogTag_Case_09', True))]}], conversation=True)
check(kind_of(s) and kind_of(s)[0] == 'give', f'an exchange with the thing in his hand before it is give: {kind_of(s)}')
s = scene('test_dlg.Node_Data_Sequence.SeqAct_Interp_6', [{'class': 'InterpGroup', 'name': 'Conversation', 'tracks': [
    gesture_track('Player', (3.0, '2P_ExchangeOne')), prop_track('Player', (3.1, 'Tags', 'DogTag_Case_09', True))]}], conversation=True)
check(kind_of(s) and kind_of(s)[0] == 'take', f'an exchange that brings the thing to his hand is take: {kind_of(s)}')
s = scene('test_dlg.Node_Data_Sequence.SeqAct_Interp_7', [{'class': 'InterpGroup', 'name': 'Conversation', 'tracks': [
    gesture_track('Player', (0.0, 'DL_Idle')), prop_track('Player', (0.0, 'HeavyPistol', 'Pistol_Test', True))]}], conversation=True)
check(kind_of(s) == ('other-hands', 'high'), f'a pistol held is other-hands: {kind_of(s)}')
s = scene('TheWorld.PersistentLevel.Main_Sequence.Party.SeqAct_Interp_0', [player_group(anim_track((0.0, 'Test07_Party_1')))],
          siblings=[{'class': 'SeqAct_AttachToActor', 'name': 'SeqAct_AttachToActor_0', 'props': {'BoneName': 'Prop01'},
                     'vars': {'Target': [{'var': 'SeqVar_Player'}], 'Attachment': [{'var': 'SeqVar_Object', 'obj': 'Bottle_Test', 'meshes': ['BeerBottle09']}]}}])
check(kind_of(s) == ('drink', 'medium'), f'a bottle attached to him beside a baked cutscene is drink/medium: {kind_of(s)}')
s['inConversation'] = True
check(kind_of(s) == ('unclassified', 'low'), f'a conversation\'s attaches are every line\'s, not this one\'s: {kind_of(s)}')
# The prop track's actions and effects are not things in his hand.
s = scene('test_dlg.Node_Data_Sequence.SeqAct_Interp_8', [{'class': 'InterpGroup', 'name': 'Conversation', 'tracks': [
    gesture_track('Player', (0.0, 'DL_Idle')), prop_track('Player', (0.0, 'Fire', 'BioticBlast_Hand', True))]}], conversation=True)
check(kind_of(s) == ('not-hands', 'high'), f'a prop action and an effect are no prop: {kind_of(s)}')

# 7. The designer's comment alone: low.
s = scene('TheWorld.PersistentLevel.Main_Sequence.Panel.SeqAct_Interp_0', [player_group(anim_track((0.0, 'Test08_Scene_1')))],
          comment='Shep hits the button')
check(kind_of(s) == ('press', 'low'), f'the comment alone is low: {kind_of(s)}')

# 8. Reading: a DLC's copy replaces the base game's (the same package and path); uses join their matinees.
tmp = tempfile.mkdtemp(prefix='mevr-hands-census-')
try:
    base = dict(bar, file='CookedPCConsole\\BioD_Test_100Bar.pcc', length=10.0)
    patch = dict(bar, file='DLC\\DLC_TEST_Patch01\\CookedPCConsole\\BioD_Test_100Bar.pcc', length=11.0)
    use = {'kind': 'use', 'game': 'LE3', 'package': 'BioD_Test_100Bar', 'path': 'TheWorld.PersistentLevel.Main_Sequence.Drinks.SeqEvent_Used_0',
           'class': 'SeqEvent_Used', 'originatorClass': 'SFXPointOfInterest', 'originator': 'SFXPointOfInterest_1', 'tag': 'Drinking',
           'matinees': [bar['path']], 'file': base['file']}
    door = {'kind': 'use', 'game': 'LE3', 'package': 'BioD_Test_100Bar', 'path': 'TheWorld.PersistentLevel.Main_Sequence.SeqEvent_Used_1',
            'class': 'SeqEvent_Used', 'originatorClass': 'SFXDoor', 'originator': 'SFXDoor_2', 'matinees': [], 'file': base['file']}
    meta = {'kind': 'classes', 'game': 'LE3', 'packages': 2, 'failed': 0, 'files': 2, 'seconds': 0.1, 'folders': ['CookedPCConsole'], 'counts': {}}
    path = os.path.join(tmp, 'le3.jsonl')
    with open(path, 'w', encoding='utf-8') as fh:
        for r in (base, patch, use, door, meta):
            fh.write(json.dumps(r) + '\n')
    scenes, uses, metas, dupes = hc.read([path])
    check(len(scenes) == 1 and scenes[0]['length'] == 11.0, 'the DLC copy of a package wins')
    check(dupes[('LE3', 'scene')] == 1, 'the duplicate is counted')
    facts, rows, totals = hc.census(scenes, uses)
    by = {r['kind']: r for r in rows}
    check(by.get('door') and by['door']['shepard'] == 0, 'a door use plays no Shepard matinee')
    check(by.get('point-of-interest') and by['point-of-interest']['candidate'], f'the bar\'s use reaches its drink: {rows}')
    summary = hc.report(facts, rows, totals, metas, dupes, 10, quiet=True)
    g = summary['games']['LE3']
    check(g['kinds']['drink']['high'] == 1 and g['uses']['candidates'] == 1, f'the summary counts it: {g["kinds"]}')
    check(g['top'] and g['top'][0]['kind'] == 'drink', 'the drink is the top candidate')
    out = os.path.join(tmp, 'out')
    check(hc.main(['hands-census.py', 'summarize', path, '--out', out]) == 0 and os.path.exists(os.path.join(out, 'scenes.jsonl')),
          'summarize writes its files')
finally:
    shutil.rmtree(tmp, ignore_errors=True)

if failures:
    print(f'{len(failures)} hands census checks failed')
    sys.exit(1)
print('hands census checks passed')
