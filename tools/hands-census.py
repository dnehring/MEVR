# SPDX-License-Identifier: MIT
"""The hands census (research/74): which scripted scenes of ME1, ME2 and ME3 have Shepard's hands doing something a player
could do with their own (take the drink at Purgatory's bar, press the console, take the datapad, salute, shake a hand), sorted
into gesture kinds by rule from the scenes' data, never per scene.

Reads the JSON lines `conversation-scan <BioGame> <out.jsonl> --hands-census --game LE1|LE2|LE3` writes (C#, over Legendary
Explorer's library; tools/conversation-scan/README.md "Hands census"):

  python tools/hands-census.py summarize scratch/hands-census/le1.jsonl scratch/hands-census/le2.jsonl ... \\
      [--out scratch/hands-census] [--top 10]
  python tools/hands-census.py show scratch/hands-census/scenes.jsonl <text>   the classified scenes whose package or path holds it
  python tools/hands-census.py names scratch/hands-census/scenes.jsonl <kind>   the names that drove a kind, counted

`summarize` writes <out>/scenes.jsonl (one line per scene with Shepard in it: how he is bound, his animation windows, the props,
what else the scene drives, the kind, its confidence and the names that drove it), scenes.csv, uses.jsonl, summary.json (the
counts per game, kind and confidence, the uses, the top candidates, the unclassified names) and prints the report. Its output is
game data (object paths, animation and mesh names): keep it in scratch/, out of the repository. Tests: tests/HandsCensusTests.py
(ctest mevr-hands-census), over synthetic scenes.

The rule (research/74 "The classification rule"). Each name played on Shepard (an InterpTrackAnimControl key's sequence, a gesture
key's gesture, transition or pose animation) gets a word kind, the first that matches:
  handshake    ShakeHand / HandShake (not a denied one)
  salute       Salut-
  other-actor  a two-person animation (2P_), a contact (kiss, hug, punch, shove, slap, stab, takedown, grabbed, held hands, a hand
               on the shoulder, a fight, damage gestures DG_, dying), a romance scene (ROM_, NorROM_): someone acts on him, or a
               fight: stays authored
  drink        drink, cheers, toast, sip, pour, bartender
  press        button, keypad, typing, console, terminal, keyboard, panel, lever, switch, computer, a tap, a hatch
  give         give, hand over, offer
  take         pick up, take, catch, receive, retrieve, collect
  other-hands  the omni-tool, a datapad or PDA, a weapon (pistol, rifle, aim, draw, holster, fire, reload), examine, the
               communicator, a door held, closed or pried, a refused handshake, grab (of a thing)
  not-hands    the body and talk vocabulary: the dialogue sets (DL_, AM_, DP_, PoseBreaker, Wander, WalktoNode, HalfTurn), work and
               combat idles (WI_, CB_, AR_, EX_), and words: walk, run, sit, stand, turn, lean, idle, look, nod, point, wave ...
  (unknown)    anything else: a scene-coded baked animation (ME1's GLO_00_B_Sovereign_3, ME3's Cit004_Goodbye_2)
The scene's kind: the first of other-actor, handshake, salute, drink, press, give, take that a name has; else what his props say
(a glass or bottle: drink; an object that passes between him and another actor: take or give by which hand holds it first; an
object he gets during the scene: take; a weapon, the omni-tool or an object he holds throughout: other-hands); else other-hands
if a name is; else not-hands if every name is; else unclassified.
Confidence: high = the word on an animation or prop track bound to Shepard explicitly (the player variable, a group that finds
"Player", a conversation track for "Player"), or a name and his prop agree; medium = Shepard bound only by his group's name, or the
kind only from a prop or a transfer; low = his stunt double (a skeletal mesh actor in a cutscene group named for him), or only
the designer comment.
"""
import collections
import csv
import json
import os
import re
import sys

KINDS = ('drink', 'press', 'take', 'give', 'salute', 'handshake', 'other-hands', 'not-hands', 'other-actor', 'unclassified')
GESTURE_KINDS = ('drink', 'press', 'take', 'give', 'salute', 'handshake')

# ---- who is Shepard ------------------------------------------------------------------------------------------------------------

PLAYER_TAGS = re.compile(r'^(player|shepard|shep|pc|playerpawn|sfxpawn_player)$', re.IGNORECASE)
PLAYER_CLASSES = re.compile(r'^(SFXPawn_Player\w*|BioPawnPlayer)$')
# A group named for him (ME1's cutscene groups PC_, HMM_PC_, HMF_FEMALEPC_; the sequels' Player, Shepard, Shep).
PLAYER_GROUP = re.compile(r'^(pc(_|$|\d)|hm[mf]_(female|male)?pc|shep|player|pc_female|femalepc|malepc)', re.IGNORECASE)


def binding_is_player(b, depth=0):
    """How a variable is the player (SeqVar_Player, a find-by-tag for the player, the player's class, a named or external variable
    that resolves to one), else None."""
    if b.get('var') == 'SeqVar_Player':
        return 'seqvar-player'
    for key in ('m_sObjectTagToFind', 'm_nmObjectTagToFind', 'm_nmFindActor', 'FindTag'):
        if key in b and PLAYER_TAGS.match(str(b[key])):
            return 'tag:' + str(b[key])
    if PLAYER_CLASSES.match(b.get('objClass') or ''):
        return 'class:' + b['objClass']
    if depth < 4:
        for inner in b.get('named', []):
            how = binding_is_player(inner, depth + 1)
            if how:
                return 'named:' + how
        for inner in b.get('outer', []):
            how = binding_is_player(inner, depth + 1)
            if how:
                return 'outer:' + how
    return None


def binding_double(b):
    """A stand-in actor (a skeletal mesh actor, ME1's cutscenes) rather than the pawn."""
    classes = [b.get('objClass') or ''] + [i.get('objClass') or '' for i in b.get('named', []) + b.get('outer', [])]
    return any('SkeletalMesh' in c for c in classes)


def shepard_tracks(scene):
    """[(group, track, how, strength)] for every track that acts on Shepard. strength: 3 explicit (a binding, a group that finds
    "Player", a conversation track for "Player"), 2 a group named for him, 1 his stunt double."""
    out = []
    # ME1's conversation lines tag every actor's track (sActorTag "Owner" or a speaker's tag) but the player's: an untagged gesture
    # track is Shepard's (research/74 "Who is Shepard": 7,994 of the replies' gesture tracks, Shepard's own lines, are untagged,
    # against the speakers' tagged tracks in the entries) [verify].
    me1_line = scene.get('game') == 'LE1' and scene.get('inConversation')
    for g in scene.get('groups', []):
        if me1_line:
            for t in g.get('tracks', []):
                if t.get('class') in ('BioEvtSysTrackGesture', 'BioEvtSysTrackProp') and t.get('actor') is None:
                    out.append((g, t, 'me1-untagged', 2))
                elif t.get('actor') and PLAYER_TAGS.match(t['actor']):
                    out.append((g, t, 'actor:' + t['actor'], 3))
            continue
        hows = [h for h in (binding_is_player(b) for b in g.get('bound', [])) if h]
        find = g.get('name:m_nmSFXFindActor') or ''
        if not hows and find and PLAYER_TAGS.match(find):
            hows = ['find:' + find]
        group_name = g.get('name') or ''
        named = bool(PLAYER_GROUP.match(group_name))
        double = any(binding_double(b) for b in g.get('bound', []))
        for t in g.get('tracks', []):
            actor = t.get('actor')
            if actor is not None:
                if PLAYER_TAGS.match(actor):
                    out.append((g, t, 'actor:' + actor, 3))
                continue  # a conversation track for someone else, whatever its group
            if hows:
                out.append((g, t, hows[0], 3))
            elif named and double:
                out.append((g, t, 'double:' + group_name, 1))
            elif named:
                out.append((g, t, 'group:' + group_name, 2))
    return out


# ---- the words -----------------------------------------------------------------------------------------------------------------

def W(pattern):
    return re.compile(pattern, re.IGNORECASE)


WORDS = (
    ('handshake', W(r'^(?!.*denied).*(shake_?hand|hand_?shake)')),
    ('salute', W(r'salut')),
    # A two-person exchange (2P_ExchangeOne/Two: dog tags, a datapad, a rifle handed over): give or take, by the props.
    ('exchange', W(r'exchange')),
    ('other-actor', W(r'kiss|hug(ging|_|$)|punch|shove|slap|stab|takedown|collapse|strangl|chok(e|ing)|headbutt|tackl|fight|brawl'
                      r'|melee|block_|death|(^|_)die|dying|getup|knock|stagger|thrown|grabbed|romance|romantic|sex|holdhand|hold_hand'
                      r'|handonshoulder|hand_on_shoulder|shoud?lder|cheek|carried|drag|tango|^dg_|strike|hit_|2p_?grab|grab_?2p'
                      r'|2p_\w*push|restrain|arrest|interrupt_reaction')),
    ('drink', W(r'drink|cheers|toast|(^|_)sip|pour|bartender|shotglass|chug')),
    ('press', W(r'button|keypad|(^|_|[a-z])typ(e|ing)|console|terminal|keyboard|panel|lever|switch|computer|corepush|tap($|_|\d)|hatch')),
    ('give', W(r'give|hand_?over|handoff|offer')),
    ('take', W(r'pick_?up|(^|_)take|catch|receive|retriev|collect')),
    ('other-hands', W(r'omni|pda|data_?pad|pistol|rifle|shotgun|weapon|(^|_)aim|draw(_|$)|holster|fire(start|_|$)|shoot|reload|exam'
                      r'|communicator|repair|weld|(^|_)tool|hack|bypass|denied|holddoor|hold_door|closedoor|opendoor|prydoor|pry_|grab'
                      r'|revive|medigel|inject')),
)
# The body and talk vocabulary: the dialogue sets' talking gestures and poses, idles, locomotion, looks.
BODY = W(r'^(dl|am|dp|wi|ex|cb|ar|fc|mo|molib)_|^posebreaker|^wander|^walk|^halfturn|^idle|^head_|^stairs|^holdceiling|^holdbar|^flirt'
         r'|^thinking|^impatient|^jokersit|^sparringpose|^basepose|^standingdefault|walk|run|jog|sprint|stand|sit|seat|lean|turn|idle'
         r'|look|nod|shake|breath|step|kneel|crouch|wait|pose|enter|exit|loop|shrug|weight_?shift|chuckle|laugh|sigh|talk|speak'
         r'|listen|wave|point|beckon|hand(s)?(other|left|right|self|flat|up|chop|dismiss|stop|forehead|wringing|behindback|tohip)'
         r'|chin|arms|cover|cov_|bow|search|jumpy|twitch|bored|nervous|dance|eyes|blink|smile|frown|cower|fidget|react|flinch'
         r'|cheer|panic|refuse|stop(one|two)hand|accus|heroic|dismiss|injur|wound|limp|stool|dance|rave|sway|arm(right|left)_')
# A two- or three-person animation: synchronised with someone, so another actor is in it (unless it only sits or walks).
TWO_PERSON = W(r'(^|_)[23]p_|_[23]p(_|$)')
TWO_PERSON_BODY = W(r'sit|stool|walk|stand|wait|idle_only|look')

DRINK_PROP = W(r'glass|bottle|cup|mug|flask|beer|whisk|brandy|wine|liquor|drink|(^|_)shot(?!gun)|tumbler')
WEAPON_PROP = W(r'pistol|rifle|shotgun|smg|weapon|wpn|carnifex|predator|avenger|argus|mantis|cannon|(^|_)grn|silencer|locust|knife'
                r'|blade|sword|gun')
OMNI_PROP = W(r'omni')
# Prop keys that are actions, not things (fire the prop weapon, a hologram's expand/collapse, IK on/off), and the vehicles' HUDs.
NOT_A_PROP = W(r'^(fire\w*|dummyfire\w*|expand|collapse|enableik|disableik|full\w*|empty|reload|hand_loop|arm_loop|hand_on|arm_on'
               r'|\w*_off|\w*_on)$|atlas|hud|tattoo|^currently_equipped|biotic|vfx|debris|bubble|laser|target|blast|(^|_)fx|_fx|glow'
               r'|beam|spark|smoke')
# Things a hand holds, takes or gives (a prop of none of these words is "held", other-hands, with its name listed).
OBJECT_PROP = W(r'data_?pad|pda|disk|dog_?tag|frame|picture|photo|phone|radio|earpiece|pan(\d|_|$)|case|box|card|chip|tablet|book'
                r'|package|key|cigar|food|plate|flower|toy|ball|model|bag|device|drive|sample|canister|vial|helm|module|medal'
                r'|letter|note|ring|necklace|badge|cube|core|crate')


def word_kind(name):
    for kind, regex in WORDS:
        if regex.search(name):
            return kind
    if TWO_PERSON.search(name):
        return 'not-hands' if TWO_PERSON_BODY.search(name) else 'other-actor'
    return 'not-hands' if BODY.search(name) else None


def names_of(track, length=None):
    """(name, start, end) of every animation a track plays (InterpTrackAnimControl keys, gesture keys' gesture, pose and
    transition anims), and ('set:' + set, ...) for the gesture sets."""
    out = []
    for k in track.get('anims', []):
        if k.get('anim') and k['anim'] != 'None':
            out.append((k['anim'], k.get('t'), k.get('end')))
    gestures = track.get('gestures', [])
    for i, k in enumerate(gestures):
        end = gestures[i + 1].get('t') if i + 1 < len(gestures) else length
        for field in ('nmGestureAnim', 'nmTransitionAnim', 'nmPoseAnim'):
            if k.get(field):
                out.append((k[field], k.get('t'), end))
        for field in ('nmGestureSet', 'nmTransitionSet', 'nmPoseSet'):
            if k.get(field):
                out.append(('set:' + k[field], k.get('t'), end))
    return out


def prop_keys(track):
    """[(time, prop name, mesh, action, equip)] of a prop track (BioEvtSysTrackProp's m_aPropKeys, its m_aTrackKeys' times)."""
    keys = (track.get('props') or {}).get('m_aPropKeys') or []
    times = track.get('keyTimes') or []
    out = []
    for i, k in enumerate(keys):
        if not isinstance(k, dict):
            continue
        mesh = str(k.get('pPropMesh') or '').split('.')[-1]
        out.append((times[i] if i < len(times) else None, k.get('nmProp') if k.get('nmProp') not in (None, 'None') else '',
                    mesh, k.get('nmAction') if k.get('nmAction') not in (None, 'None') else '', k.get('bEquip')))
    return out


def props_of(scene, shep):
    """Props in Shepard's hand: [kind of evidence, name, detail]. His prop tracks' props (prop name and mesh, with the time each is
    equipped), the attach actions beside a level matinee that attach something to him (SeqAct_AttachToActor's Target and
    Attachment; a conversation's sequence holds every line's, so not for lines), weapon actions on him."""
    props = []
    for g, t, how, s in shep:
        for time, prop, mesh, action, equip in prop_keys(t):
            for v in (prop, mesh):
                if v and not NOT_A_PROP.search(v):
                    props.append(['track', v, time])
            if action:
                props.append(['action', action, time])
    if scene.get('inConversation'):
        return props
    for s in scene.get('siblings', []):
        vars_ = s.get('vars') or {}
        target = vars_.get('Target', [])
        attachment = vars_.get('Attachment', [])
        on_shep = any(binding_is_player(b) for b in target)
        if 'Attach' in s['class'] and on_shep:
            for b in attachment:
                for v in [b.get('m_sObjectTagToFind'), b.get('tag'), b.get('obj')] + list(b.get('meshes', [])):
                    if v and not re.search(r'muzzle|flash|vfx|crust|light|effect|target|point|loc$', str(v), re.I):
                        props.append(['attach', str(v), (s.get('props') or {}).get('BoneName')])
        elif re.search(r'Weapon', s['class']) and on_shep:
            props.append(['weapon-action', s['class'], None])
    return props


def transfer(scene, shep, mesh_names):
    """Whether one of his props passes between him and another actor in the scene: 'take' (the other holds it first), 'give' (he
    does), else None. Another actor's prop track with the same prop or mesh."""
    shep_tracks = {id(t) for g, t, h, s in shep}
    mine = {}
    for g, t, h, s in shep:
        for time, prop, mesh, action, equip in prop_keys(t):
            for v in (prop, mesh):
                if v and equip is not False and time is not None:
                    mine[v] = min(mine.get(v, 1e9), time)
    for g in scene.get('groups', []):
        for t in g.get('tracks', []):
            if id(t) in shep_tracks or 'props' not in t:
                continue
            for time, prop, mesh, action, equip in prop_keys(t):
                for v in (prop, mesh):
                    if v in mine and v in mesh_names and time is not None and equip is not False:
                        return ('take' if time < mine[v] else 'give'), v
    return None


def other_actors(scene, shep):
    """Other groups' animations: [(group name, [names])]; the director's cuts and fades."""
    shep_tracks = {id(t) for g, t, h, s in shep}
    others = []
    cuts = fades = 0
    for g in scene.get('groups', []):
        anims = []
        for t in g.get('tracks', []):
            cuts += t.get('cuts', 0) or 0
            if 'Fade' in t['class'] or 'BlackScreen' in t['class']:
                fades += 1
            if id(t) in shep_tracks:
                continue
            anims += [n for n, a, b in names_of(t) if not n.startswith('set:')]
        if anims:
            name = g.get('name') or '?'
            actors = sorted({t['actor'] for t in g.get('tracks', []) if t.get('actor') and id(t) not in shep_tracks})
            others.append((('/'.join(actors) if name.startswith('Conversation') and actors else name), anims))
    return others, cuts, fades


def conversation_of(scene):
    path = scene.get('path') or ''
    return path.split('.Node_Data_Sequence')[0] if '.Node_Data_Sequence' in path else None


def classify(scene):
    """The scene's facts and kind, or None when Shepard isn't in it."""
    shep = shepard_tracks(scene)
    if not shep:
        return None
    length = scene.get('length') or 0
    strength = max(s for g, t, h, s in shep)
    windows = []
    for g, t, how, s in shep:
        for n, a, b in names_of(t, length):
            if not n.startswith('set:'):
                windows.append({'name': n, 't': a, 'end': b, 'kind': word_kind(n), 'track': t['class'], 'how': how})
    sets = sorted({n[4:] for g, t, h, s in shep for n, a, b in names_of(t) if n.startswith('set:')})
    props = props_of(scene, shep)
    things = [p[1] for p in props if p[0] in ('track', 'attach')]
    actions = [p[1] for p in props if p[0] in ('action', 'weapon-action')]
    others, cuts, fades = other_actors(scene, shep)
    comment = scene.get('comment') or ''
    by_kind = collections.defaultdict(list)
    for w in windows:
        by_kind[w['kind']].append(w['name'])
    sure = {3: 'high', 2: 'medium', 1: 'low'}[strength]
    kind = conf = None
    drivers, why = [], ''
    drink_things = [p for p in things if DRINK_PROP.search(p)]
    weapon_things = [p for p in things if WEAPON_PROP.search(p)] + [a for a in actions if re.search(r'fire|weapon', a, re.I)]
    omni_things = [p for p in things if OMNI_PROP.search(p)]
    held = [p for p in things if p not in drink_things + weapon_things + omni_things]
    objects = [p for p in held if OBJECT_PROP.search(p)]
    lesser = 'medium' if strength >= 2 else 'low'
    for k in ('other-actor', 'handshake', 'salute', 'exchange', 'drink', 'press', 'give', 'take'):
        if by_kind.get(k):
            kind, drivers, conf, why = k, by_kind[k], sure, 'name'
            break
    if kind == 'exchange':
        # Who holds the thing first: another actor's prop track with the same thing, else when his own hand gets it.
        start = min((w['t'] for w in windows if w['kind'] == 'exchange' and isinstance(w['t'], (int, float))), default=0)
        things_passed = objects + weapon_things + drink_things
        passed = transfer(scene, shep, set(things_passed))
        mine = [p for p in props if p[0] == 'track' and p[1] in things_passed and isinstance(p[2], (int, float))]
        if passed:
            kind, why = passed[0], 'exchange; the prop passes between actors'
        elif mine and min(p[2] for p in mine) >= start - 0.25:
            kind, why = 'take', 'exchange; the prop comes to his hand after it starts'
        elif mine:
            kind, why = 'give', 'exchange; he holds the prop before it'
        else:
            kind, conf, why = 'take', lesser, 'exchange; no prop on him, direction unknown'
        drivers = drivers + sorted({p[1] for p in mine})
    if kind == 'drink' and drink_things:
        conf, drivers, why = 'high' if strength >= 2 else conf, drivers + drink_things, 'name and prop'
    if kind is None and drink_things:
        kind, drivers, conf, why = 'drink', drink_things, lesser, 'prop'
    if kind is None and objects:
        # Only a thing that passes between him and someone is a take or a give; one that appears in his hand without a take
        # animation is shown held (a datapad read during a line): other-hands below.
        passed = transfer(scene, shep, set(objects))
        if passed:
            kind, drivers, conf, why = passed[0], [passed[1]], lesser, 'prop passes between actors'
    if kind is None and (weapon_things or omni_things or held):
        kind, drivers, conf, why = 'other-hands', weapon_things + omni_things + held, sure, 'prop held'
    if kind is None and by_kind.get('other-hands'):
        kind, drivers, conf, why = 'other-hands', by_kind['other-hands'], sure, 'name'
    if kind is None:
        for k in ('handshake', 'salute', 'drink', 'press', 'give', 'take'):
            if comment and dict(WORDS)[k].search(comment):
                kind, drivers, conf, why = k, ['comment:' + comment], 'low', 'comment'
                break
    if kind is None:
        unknown = by_kind.get(None, [])
        if unknown:
            kind, drivers, conf, why = 'unclassified', unknown, 'low', 'unknown names'
        else:
            kind, drivers, conf, why = 'not-hands', by_kind.get('not-hands', [])[:6] or ['no animation'], 'high' if windows else 'medium', 'body names only'
    first = [w for w in windows if w['name'] in drivers]
    pause = min((w['t'] for w in first if isinstance(w.get('t'), (int, float))), default=None)
    fact = {
        'game': scene.get('game'), 'package': scene.get('package'), 'file': scene.get('file'), 'path': scene.get('path'),
        'sequence': scene.get('sequence'), 'data': scene.get('data'), 'length': round(length, 2),
        'conversation': conversation_of(scene), 'comment': comment or None,
        'bound': sorted({h for g, t, h, s in shep}), 'strength': strength,
        'windows': windows, 'sets': sets, 'props': props,
        'others': [[n, a[:6]] for n, a in others[:8]], 'cuts': cuts, 'fades': fades,
        'upstream': [u.get('class') for u in scene.get('upstream', [])],
        'downstream': sorted({d['class'] for d in scene.get('downstream', []) if re.search(
            r'Plot|PMExecute|Transition|Conversation|Teleport|Fade|Interp|Give|Remove|Destroy|Inventory|Prop|Award|Save', d['class'])}),
        'kind': kind, 'confidence': conf, 'why': why, 'drivers': sorted(set(drivers))[:8], 'pauseAt': pause,
    }
    fact['score'] = score(fact)
    return fact


def score(f):
    """For the top lists: a gesture kind, how sure, a level scene (a player meets it on foot) over a conversation line, a prop in
    his hand, a scene the player starts with a use, a short scene. Cameras don't count (first person drops them), nor bystanders."""
    s = {'drink': 10, 'press': 9, 'take': 9, 'give': 9, 'handshake': 8, 'salute': 8}.get(f['kind'], 0)
    s += {'high': 6, 'medium': 3, 'low': 0}.get(f['confidence'], 0)
    s += 2 if not f['conversation'] else 0
    s += 2 if any(p[0] in ('track', 'attach') for p in f['props']) else 0
    s += 2 if f.get('usedFrom') else 0
    s -= 2 if f['length'] > 30 else 0
    return s


# ---- uses ----------------------------------------------------------------------------------------------------------------------

USE_KINDS = (
    ('traversal', W(r'SFXNav_|ladder|boost|jumpnode')),
    ('door', W(r'door|gate|hatch|elevator|lift')),
    ('console', W(r'terminal|console|computer|panel|keypad|switch|lever|button|station|datapad|kiosk|galaxy|map|screen|generator')),
    ('loot', W(r'crate|locker|container|loot|corpse|body|safe|box|case|cabinet|chest|storage|cache|wreck|medi|stash|weapon|armor'
               r'|treasure|mining')),
    ('person', W(r'pawn|npc|hench|squad|stuntactor')),
    # ME2's and ME3's interaction points (the prompt to talk to, examine or use something): what is used is the script's.
    ('point-of-interest', W(r'PointOfInterest')),
)


def use_kind(u):
    text = ' '.join(str(u.get(k) or '') for k in ('originatorClass', 'originator', 'tag')) + ' ' + ' '.join(u.get('meshes', []))
    if not u.get('originatorClass'):
        return 'unknown-object'  # the event's Originator is empty in the package (ME1's placeables' own sequences, the sequels' prefabs)
    for name, regex in USE_KINDS:
        if regex.search(text):
            return name
    return 'other'


# ---- reading, deduplicating -----------------------------------------------------------------------------------------------------

def mount_rank(file):
    """Which copy of a package the game mounts: a DLC's over the base game's, the later DLC folder (by name) over an earlier one.
    An approximation of the games' mount order [verify]."""
    parts = re.split(r'[\\/]', file or '')
    return (1, parts[1]) if parts and parts[0].upper() == 'DLC' and len(parts) > 1 else (0, '')


def read(paths):
    scenes, uses, meta = {}, {}, []
    dupes = collections.Counter()
    for path in paths:
        for line in open(path, encoding='utf-8'):
            r = json.loads(line)
            kind = r.get('kind')
            if kind == 'classes':
                meta.append(r)
                continue
            if kind not in ('scene', 'use'):
                continue
            key = (r.get('game'), (r.get('package') or '').upper(), r.get('path'))
            table = scenes if kind == 'scene' else uses
            if key in table:
                dupes[(r.get('game'), kind)] += 1
                if mount_rank(r.get('file')) <= mount_rank(table[key].get('file')):
                    continue
            table[key] = r
    # A conversation's line is the same line in every package that carries the conversation (ME1's cab ride is in 12 of the
    # Citadel's; ME3's Citadel levels share some): one copy, by its path (the conversation's name and the node's objects).
    lines = {}
    for key, r in sorted(scenes.items(), key=lambda kv: (kv[0][0], kv[0][1], kv[0][2] or '')):
        if not r.get('inConversation'):
            continue
        k = (r.get('game'), (r.get('path') or '').lower())
        if k in lines:
            dupes[(r.get('game'), 'line copies')] += 1
            del scenes[key]
        else:
            lines[k] = key
    return list(scenes.values()), list(uses.values()), meta, dupes


def census(scenes, uses):
    """(facts, use rows, totals) of read()'s scenes and uses."""
    facts = []
    totals = collections.Counter()
    for s in scenes:
        g = s.get('game')
        totals[(g, 'scenes')] += 1
        totals[(g, 'conversation' if s.get('inConversation') else 'level')] += 1
        f = classify(s)
        if f:
            facts.append(f)
    by_path = {(f['game'], (f['package'] or '').upper(), f['path']): f for f in facts}
    rows = []
    for u in uses:
        hit = [by_path.get((u.get('game'), (u.get('package') or '').upper(), m)) for m in u.get('matinees', [])]
        hit = [h for h in hit if h]
        rows.append({'game': u.get('game'), 'kind': use_kind(u), 'matinees': len(u.get('matinees', [])), 'shepard': len(hit),
                     'candidate': any(h['kind'] in GESTURE_KINDS for h in hit), 'package': u.get('package'), 'path': u.get('path'),
                     'originatorClass': u.get('originatorClass'), 'scenes': [h['path'] for h in hit],
                     'sceneKinds': sorted({h['kind'] for h in hit})})
        for h in hit:
            h.setdefault('usedFrom', []).append(u.get('originatorClass') or u.get('class'))
    for f in facts:
        f['score'] = score(f)  # with the uses that reach it
    return facts, rows, totals


def summarize(argv):
    out_dir, top = 'scratch/hands-census', 10
    paths = []
    i = 0
    while i < len(argv):
        if argv[i] == '--out':
            out_dir, i = argv[i + 1], i + 2
        elif argv[i] == '--top':
            top, i = int(argv[i + 1]), i + 2
        else:
            paths.append(argv[i])
            i += 1
    scenes, uses, meta, dupes = read(paths)
    facts, use_rows, totals = census(scenes, uses)
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, 'scenes.jsonl'), 'w', encoding='utf-8') as fh:
        for f in sorted(facts, key=lambda f: (f['game'], -f['score'], f['package'], f['path'])):
            fh.write(json.dumps(f) + '\n')
    with open(os.path.join(out_dir, 'scenes.csv'), 'w', encoding='utf-8', newline='') as fh:
        w = csv.writer(fh)
        w.writerow(['game', 'kind', 'confidence', 'why', 'score', 'package', 'path', 'length', 'conversation', 'bound', 'drivers',
                    'pauseAt', 'props', 'cuts', 'fades', 'others'])
        for f in sorted(facts, key=lambda f: (f['game'], -f['score'])):
            w.writerow([f['game'], f['kind'], f['confidence'], f['why'], f['score'], f['package'], f['path'], f['length'],
                        f['conversation'] or '', ' '.join(f['bound']), ' '.join(f['drivers']), f['pauseAt'],
                        ' '.join(str(p[1]) for p in f['props']), f['cuts'], f['fades'], len(f['others'])])
    with open(os.path.join(out_dir, 'uses.jsonl'), 'w', encoding='utf-8') as fh:
        for u in use_rows:
            fh.write(json.dumps(u) + '\n')
    summary = report(facts, use_rows, totals, meta, dupes, top)
    with open(os.path.join(out_dir, 'summary.json'), 'w', encoding='utf-8') as fh:
        json.dump(summary, fh, indent=1)
    return 0


def pick(facts, top):
    """The best gesture scenes, one per sequence and kind (a scene's copies, a conversation's repeats of one gesture)."""
    best = sorted((f for f in facts if f['kind'] in GESTURE_KINDS), key=lambda f: (-f['score'], f['package'], f['path']))
    distinct, seen = [], set()
    for f in best:
        # A conversation is one whatever package carries a copy of it (ME3's Citadel levels share some).
        k = (f['conversation'].lower(), f['kind']) if f['conversation'] else (f['package'], f['sequence'], f['kind'])
        if k not in seen:
            seen.add(k)
            distinct.append(f)
    # At most a third of the list of one kind first, then the rest by score: the best of each kind show.
    cap = max(1, top // 3)
    picks, per = [], collections.Counter()
    for f in distinct:
        if per[f['kind']] < cap and len(picks) < top:
            picks.append(f)
            per[f['kind']] += 1
    for f in distinct:
        if len(picks) >= top:
            break
        if f not in picks:
            picks.append(f)
    return sorted(picks, key=lambda f: (-f['score'], f['package'], f['path']))


def report(facts, use_rows, totals, meta, dupes, top, quiet=False):
    games = sorted({f['game'] for f in facts} | {m.get('game') for m in meta})
    summary = {'games': {}}
    say = (lambda *a: None) if quiet else print
    for g in games:
        gf = [f for f in facts if f['game'] == g]
        m = next((x for x in meta if x.get('game') == g), {})
        by = collections.Counter((f['kind'], f['confidence']) for f in gf)
        kinds = {}
        for k in KINDS:
            row = {c: by[(k, c)] for c in ('high', 'medium', 'low') if by[(k, c)]}
            if row:
                row['total'] = sum(row.values())
                row['level'] = sum(1 for f in gf if f['kind'] == k and not f['conversation'])
                row['conversations'] = len({f['conversation'].lower() for f in gf if f['kind'] == k and f['conversation']})
                row['lines'] = sum(1 for f in gf if f['kind'] == k and f['conversation'])
                kinds[k] = row
        uses = [u for u in use_rows if u['game'] == g]
        unclassified = collections.Counter(n for f in gf if f['kind'] == 'unclassified' for n in f['drivers'])
        level = [f for f in gf if not f['conversation']]
        summary['games'][g] = {
            'packages': m.get('packages'), 'failed': m.get('failed'), 'seconds': m.get('seconds'), 'folders': m.get('folders', []),
            'scenes': totals[(g, 'scenes')], 'levelScenes': totals[(g, 'level')], 'conversationScenes': totals[(g, 'conversation')],
            'withShepard': len(gf), 'withShepardLevel': len(level), 'duplicates': {k[1]: v for k, v in dupes.items() if k[0] == g},
            'bound': dict(collections.Counter(b.split(':')[0] for f in gf for b in f['bound'])),
            'kinds': kinds,
            'uses': {'total': len(uses), 'byKind': dict(collections.Counter(u['kind'] for u in uses)),
                     'playMatinee': sum(1 for u in uses if u['matinees']), 'playShepardMatinee': sum(1 for u in uses if u['shepard']),
                     'candidates': sum(1 for u in uses if u['candidate']),
                     'shepardByKind': dict(collections.Counter(u['kind'] for u in uses if u['shepard']))},
            'top': [{k: f[k] for k in ('kind', 'confidence', 'why', 'score', 'package', 'path', 'length', 'conversation', 'drivers',
                                       'bound', 'cuts', 'pauseAt')}
                    | {'props': [p[1] for p in f['props']][:4], 'others': [o[0] for o in f['others']][:4]} for f in pick(gf, top)],
            'topLevel': [{k: f[k] for k in ('kind', 'confidence', 'package', 'path', 'length', 'drivers', 'pauseAt')}
                         for f in pick(level, top)],
            'unclassifiedNames': unclassified.most_common(30),
        }
        s = summary['games'][g]
        say(f'== {g}: {s["packages"]} packages ({s["failed"]} failed) from {len(s["folders"])} folders, {s["scenes"]} scenes '
            f'({s["levelScenes"]} level, {s["conversationScenes"]} conversation lines), {s["withShepard"]} with Shepard '
            f'({s["withShepardLevel"]} level)')
        for k, v in kinds.items():
            say(f'   {k:12} {v["total"]:6}  ' + '  '.join(f'{c} {v[c]:5}' for c in ('high', 'medium', 'low') if c in v)
                + f'   level {v["level"]}, lines {v["lines"]} in {v["conversations"]} conversations')
        u = s['uses']
        say(f'   uses {u["total"]}: ' + ', '.join(f'{k} {v}' for k, v in sorted(u['byKind'].items(), key=lambda x: -x[1]))
            + f'; {u["playMatinee"]} play a matinee, {u["playShepardMatinee"]} one on Shepard, {u["candidates"]} a gesture')
        for f in s['top']:
            say(f'   top {f["kind"]:9} {f["confidence"]:6} {f["score"]:3}  {f["package"]}  {f["conversation"] or f["path"]}  '
                f'{f["length"]} s  {",".join(f["drivers"][:3])}  {",".join(map(str, f["props"][:2]))}')
    return summary


def show(argv):
    path, text = argv[0], argv[1].lower()
    for line in open(path, encoding='utf-8'):
        f = json.loads(line)
        if text in (f['package'] + ' ' + f['path']).lower():
            print(json.dumps(f, indent=1))
    return 0


def names(argv):
    path, kind = argv[0], argv[1]
    c = collections.Counter()
    for line in open(path, encoding='utf-8'):
        f = json.loads(line)
        if f['kind'] == kind:
            c.update((f['game'], d) for d in f['drivers'])
    for (g, n), v in c.most_common(int(argv[2]) if len(argv) > 2 else 60):
        print(f'{v:7} {g} {n}')
    return 0


def main(argv):
    commands = {'summarize': (summarize, 1), 'show': (show, 2), 'names': (names, 2)}
    if len(argv) >= 2 and argv[1] in commands and len(argv) - 2 >= commands[argv[1]][1]:
        return commands[argv[1]][0](argv[2:])
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
