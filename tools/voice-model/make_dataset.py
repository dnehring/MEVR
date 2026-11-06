# SPDX-License-Identifier: MIT
"""The model tier's training data (research/54 "Rules first, a fine-tuned sub-1B model second"): the data writes itself from
the action grammar. Legal scenes are sampled (the party, their unlocked powers and cooldowns, one to six enemy groups with
their words, what the gun and the gaze point at), gold action lines are drawn for them, and each gold line gets spoken
phrasings: the parser's own corpus where it has one (tests/SquadPhrasings.cpp, the Pass lines, re-targeted to the sampled
scene), templates for every order with fillers, restarts, compounds and deictics, plus non-commands ("I'm hit") whose
answer is `none`; then ASR noise aimed at the names and the power words (the recognizer's typical slips), so the model
learns to read a transcript, not text. Each example is one JSONL record {"prompt": <the scene block as
core/vr/SquadContext's Block writes it, with the SAY line>, "answer": <action line(s) or none>} that train_lora.py turns
into a chat sample. No teacher model is needed for this first dataset; a teacher's paraphrases (never a Gemma model:
its terms would make ours a derivative) can be added with --teacher-lines <jsonl> later.

  python tools/voice-model/make_dataset.py --out scratch/voice-model/train.jsonl --count 20000 [--seed 1] [--noise 0.3]
"""
import argparse
import json
import pathlib
import random
import re

ROSTER = {
    "G": ("Garrus", ["Garrus", "Vakarian"], {"OVR": "Overload", "DMP": "Damping", "SAB": "Sabotage", "MRK": "Marksman"}),
    "W": ("Wrex", ["Wrex"], {"THR": "Throw", "WRP": "Warp", "STA": "Stasis", "BRR": "Barrier", "CRN": "Carnage"}),
    "L": ("Liara", ["Liara", "T'Soni"], {"SNG": "Singularity", "LFT": "Lift", "STA": "Stasis", "THR": "Throw", "WRP": "Warp"}),
    "T": ("Tali", ["Tali"], {"OVR": "Overload", "SAB": "Sabotage", "AIH": "AI Hacking", "DMP": "Damping"}),
    "A": ("Ashley", ["Ashley", "Williams", "Ash"], {"MRK": "Marksman", "CRN": "Carnage", "ADR": "Adrenaline Burst", "IMM": "Immunity"}),
    "K": ("Kaidan", ["Kaidan", "Alenko"], {"THR": "Throw", "LFT": "Lift", "OVR": "Overload", "SAB": "Sabotage", "NSH": "Neural Shock"}),
}
WEAPONS = {"PI": ["pistol", "sidearm", "handgun"], "SG": ["shotgun", "scattergun"], "AR": ["assault rifle", "rifle", "AR"], "SR": ["sniper rifle", "sniper", "long gun"]}
TYPES = ["geth trooper", "geth destroyer", "geth hunter", "geth prime", "krogan", "husk", "mercenary", "rachni soldier", "thorian creeper", "varren"]
SURFACES = ["bridge", "catwalk", "ridge", "stairs", "platform", "roof", "balcony", "ledge", None, None, None]
SECTORS = ["ahead", "left", "right", "behind"]
BANDS = ["near", "mid", "far"]
ELEVATIONS = ["level", "above", "below"]

ASR_SLIPS = {
    "garrus": ["gareth", "garris", "garrus", "carus", "garrison"],
    "wrex": ["rex", "wrecks", "wrex", "rax"],
    "liara": ["leara", "liara", "lyara", "lara"],
    "tali": ["tally", "tali", "tolly"],
    "ashley": ["ashley", "ashleigh", "ash lee"],
    "kaidan": ["kaiden", "caden", "kaidan", "kay den"],
    "overload": ["overload", "over load", "overlord"],
    "sabotage": ["sabotage", "sabbotage"],
    "singularity": ["singularity", "singular city"],
    "stasis": ["stasis", "stay sis", "states is"],
    "warp": ["warp", "warped", "worp"],
    "lift": ["lift", "lyft"],
    "throw": ["throw", "throe"],
    "barrier": ["barrier", "barry er"],
    "marksman": ["marksman", "marks man"],
    "carnage": ["carnage", "carnidge"],
    "damping": ["damping", "dampening", "damping field"],
    "geth": ["geth", "get", "guess"],
    "krogan": ["krogan", "crow gun", "crogan"],
    "husk": ["husk", "husks", "hask"],
}


def sector_words(sector, band, elevation):
    w = []
    w.append({"ahead": "ahead", "left": "left", "right": "right", "behind": "behind"}[sector])
    w.append({"near": "near", "mid": "mid", "far": "far"}[band])
    if elevation != "level":
        w.append({"above": "above", "below": "below"}[elevation])
    return w


def sample_scene(rng):
    present = rng.sample(list(ROSTER), 2)
    party = []
    for letter in present:
        name, aliases, powers = ROSTER[letter]
        chosen = rng.sample(list(powers.items()), rng.randint(1, min(3, len(powers))))
        party.append({"letter": letter, "name": name, "aliases": aliases,
                      "powers": [{"code": c, "name": n, "ready": rng.random() < 0.7, "readyIn": rng.randint(2, 20)} for c, n in chosen],
                      "weapons": rng.sample(list(WEAPONS), rng.randint(2, 4)), "inHand": rng.choice(list(WEAPONS))})
    groups = []
    for i in range(rng.randint(1, 5)):
        groups.append({"id": i + 1, "type": rng.choice(TYPES), "count": rng.choice([1, 1, 2, 2, 3, 4]), "sector": rng.choice(SECTORS),
                       "band": rng.choice(BANDS), "elevation": rng.choice(ELEVATIONS), "surface": rng.choice(SURFACES),
                       "distanceM": rng.randint(4, 45)})
    if groups:
        nearest = min(groups, key=lambda g: g["distanceM"])
        nearest["nearest"] = True
        biggest = max(groups, key=lambda g: g["count"])
        biggest["biggest"] = True
    aim = rng.choice([None] + [g["id"] for g in groups]) if groups else None
    gaze = rng.choice([None] + [g["id"] for g in groups]) if groups else None
    combat = bool(groups) and rng.random() < 0.8
    return {"party": party, "groups": groups, "aim": aim, "gaze": gaze, "combat": combat,
            "shepardWeapon": rng.choice(list(WEAPONS)) if combat or rng.random() < 0.3 else None}


def prefix(scene):
    """core/vr/SquadContext's Prefix: the rules and the roster (here the party only: the live one lists everyone seen)."""
    out = ("You turn a spoken squad order into one line: WHO|WEAPON|POWER|ORDER|TARGET.\n"
           "WHO: a party letter, ALL, or LAST. WEAPON: a code or -. POWER: a code or -.\n"
           "ORDER: attack, move, follow, hold, regroup, use, cancel, repeat.\n"
           "TARGET: an E id, PT1..PT3 (what the player pointed at while saying a word), AIM, GAZE, LAST, OTHER, SELF, or -.\n"
           "Only name squadmates listed in PARTY. Use only powers marked ok unless told to wait.\n"
           "ROSTER\n")
    for m in scene["party"]:
        out += f"{m['letter']} {m['name']}: " + ", ".join(m["aliases"]) + ". Weapons " + ", ".join(m["weapons"]) + ". Powers " + \
            ", ".join(f"{p['code']} {p['name']}" for p in m["powers"]) + ".\n"
    out += ("WEAPONS PI pistol, SG shotgun, AR assault rifle, SR sniper rifle.\n"
            "WORDS troops, soldiers, guys, them, those = enemies. bridge = catwalk = walkway.\n")
    return out


def block(scene, say, deictics=()):
    """The text block as core/vr/SquadContext's Block writes it (kept in step by hand: research/54's representation)."""
    lines = ["NOW combat" if scene["combat"] else "NOW explore"]
    for m in scene["party"]:
        weapons = ",".join(f"{w}{'*' if w == m['inHand'] else ''}" for w in m["weapons"]) or "-"
        powers = ", ".join(f"{p['code']} {'ok' if p['ready'] else str(p['readyIn']) + 's'}" for p in m["powers"]) or "-"
        lines.append(f"PARTY {m['letter']}:{weapons} | {powers}")
    lines.append(f"SHEP {scene['shepardWeapon'] + '*' if scene.get('shepardWeapon') else 'holstered'}")
    for g in scene["groups"]:
        line = f"E{g['id']} {g['type']} x{g['count']} | {g['sector']} {g['band']} {g['elevation']}"
        if g["surface"]:
            line += f" | {g['surface']}"
        flags = " ".join(w for w, on in (("nearest", g.get("nearest")), ("biggest", g.get("biggest") and len(scene["groups"]) > 1)) if on)
        if flags:
            line += f" | {flags}"
        lines.append(line)
    if scene["aim"] or scene["gaze"]:
        lines.append(f"AIM {'E' + str(scene['aim']) if scene['aim'] else '-'} | GAZE {'E' + str(scene['gaze']) if scene['gaze'] else '-'}")
    for i, (word, candidates) in enumerate(deictics, 1):
        scored = ", ".join(f"{c} {s}" for c, s in zip(candidates, (".92", ".41", ".20")) if c != "-")
        lines.append(f"PT{i} \"{word}\" = {scored if scored else 'nothing'}")
    lines.append(f"SAY {say}")
    return "\n".join(lines) + "\n"


SECTOR_PHRASE = {"ahead": "ahead", "left": "on the left", "right": "on the right", "behind": "behind us"}
ELEVATION_PHRASE = {"above": "up there", "below": "down there"}


def describe(group, rng, groups):
    """Words a player would use for a group, chosen among the forms that fit ONLY this group in the scene; None when no
    words single it out (two groups alike in type, place and surface), and the caller points instead. Run1 and run2's
    residual misses were all descriptions that fit two groups ("the left ones" with two groups on the left, "those thorian
    creepers" with creepers in two groups): the gold was a coin toss (research/54 "The fine-tune")."""
    plural = group["count"] > 1
    noun = group["type"] + ("s" if plural else "")
    same_type = lambda g: g["type"] == group["type"]
    surface_word = lambda s: "bridge" if s in ("bridge", "catwalk") else s  # the prefix's WORDS line: bridge = catwalk = walkway
    same_surface = lambda g: g["surface"] and surface_word(g["surface"]) == surface_word(group["surface"])
    same_sector = lambda g: g["sector"] == group["sector"]
    same_elevation = lambda g: g["elevation"] == group["elevation"]
    # (the words, what they match)
    forms = [(f"the {noun}", same_type), (f"{'those' if plural else 'that'} {noun}", same_type)]
    if group["surface"]:
        forms.append((f"the ones on the {group['surface']}", same_surface))
        forms.append((f"the {noun} on the {group['surface']}", lambda g: same_type(g) and same_surface(g)))
    if group.get("nearest"):
        forms.append(("the nearest ones" if plural else "the nearest one", lambda g: g.get("nearest")))
        forms.append(("the closest", lambda g: g.get("nearest")))
    if group.get("biggest") and len(groups) > 1:
        forms.append(("the big group", lambda g: g.get("biggest")))
    forms.append((f"the {group['sector']} ones" if group["sector"] != "ahead" else "the ones ahead", same_sector))
    forms.append((f"the {noun} {SECTOR_PHRASE[group['sector']]}", lambda g: same_type(g) and same_sector(g)))
    if group["elevation"] != "level":
        forms.append((f"the ones {ELEVATION_PHRASE[group['elevation']]}", same_elevation))
        if group["elevation"] == "above":
            forms.append(("the ones up top", same_elevation))
        forms.append((f"the {noun} {ELEVATION_PHRASE[group['elevation']]}", lambda g: same_type(g) and same_elevation(g)))
        forms.append((f"the ones {ELEVATION_PHRASE[group['elevation']]} {SECTOR_PHRASE[group['sector']]}", lambda g: same_sector(g) and same_elevation(g)))
    unique = [words for words, fits in forms if sum(1 for g in groups if fits(g)) == 1]
    return rng.choice(unique) if unique else None


def unique_surface(group, groups):
    same = {"bridge": "bridge", "catwalk": "bridge"}
    return bool(group["surface"]) and sum(1 for g in groups if g["surface"] and same.get(g["surface"], g["surface"]) == same.get(group["surface"], group["surface"])) == 1


ATTACK_VERBS = ["take out", "attack", "kill", "focus on", "deal with", "hit", "engage", "light up", "take down", "open fire on", "shoot", "get", "waste", "drop"]
MOVE_VERBS = ["move to", "go to", "get to", "move up to", "head to", "get over to", "push to"]
HOLD_VERBS = ["hold", "hold position at", "stay at", "hold the", "dig in at", "stay on"]
FOLLOW = ["on me", "regroup", "follow me", "fall back", "get back here", "come back", "rally on me", "stay with me", "form up", "back to me", "cover me", "stick with me"]
FILLERS = ["", "", "", "uh ", "um ", "okay ", "hey ", "alright ", "right "]
CONJ = [" and ", " then ", ", "]
CANCEL = ["belay that", "cancel that", "never mind", "forget it", "scratch that"]
TALK = ["I'm hit", "watch out", "nice shot", "they're flanking us", "taking fire", "good job", "we're clear", "reloading", "behind you", "I'm out", "that was close",
        "heads up", "medi-gel", "hold fire", "I need a medic", "what was that", "keep your head down", "there's more coming", "I see them", "contact"]


def who_text(member, rng):
    if member is None:
        return rng.choice(["", "", "everyone, ", "squad, ", "both of you, ", "you two, "])
    return rng.choice(member["aliases"] + [member["name"], member["name"]]) + rng.choice([", ", " ", ", "])


def gold_and_say(scene, rng):
    """One gold answer and one way of saying it; deictics listed when a pointing word was used."""
    members = scene["party"]
    groups = scene["groups"]
    deictics = []
    kind = rng.choices(["attack", "move", "hold", "follow", "equip", "use", "compound", "correction", "talk", "cancel"],
                       weights=[26, 10, 8, 14, 8, 14, 8, 4, 6, 2])[0]
    member = rng.choice(members + [None])
    who = member["letter"] if member else "ALL"

    def deictic(word, group):
        candidates = [f"E{group['id']}"] + [f"E{g['id']}" for g in groups if g is not group][:2]
        deictics.append((word, candidates))
        return f"PT{len(deictics)}"

    def target_phrase(group):
        r = rng.random()
        if r < 0.3:
            word = rng.choice(["those", "them", "that one", "those guys", "these"])
            return word, deictic(word, group)
        if r < 0.4 and scene["aim"] == group["id"]:
            return rng.choice(["my target", "what I'm shooting at"]), "AIM"
        if r < 0.5 and scene["gaze"] == group["id"]:
            return rng.choice(["what I'm looking at", "that one I'm looking at"]), "GAZE"
        words = describe(group, rng, groups)
        if words is None:  # nothing singles it out: the player points
            word = rng.choice(["those", "them", "those guys", "these"])
            return word, deictic(word, group)
        return words, f"E{group['id']}"

    if kind == "attack" and groups:
        g = rng.choice(groups)
        phrase, target = target_phrase(g)
        verb = rng.choice(ATTACK_VERBS)
        say = f"{rng.choice(FILLERS)}{who_text(member, rng)}{verb} {phrase}"
        if verb in ("take out", "take down", "light up") and rng.random() < 0.4:
            say = f"{rng.choice(FILLERS)}{who_text(member, rng)}take {phrase} {'out' if verb == 'take out' else 'down'}"
        return f"{who}|-|-|attack|{target}", say, deictics
    if kind == "move":
        if groups and rng.random() < 0.5:
            g = rng.choice(groups)
            phrase, target = target_phrase(g)
            return f"{who}|-|-|move|{target}", f"{rng.choice(FILLERS)}{who_text(member, rng)}{rng.choice(MOVE_VERBS)} {phrase}", deictics
        word = rng.choice(["there", "over there", "up there", "that spot"])
        t = deictic(word, groups[0] if groups else {"id": 0})
        if not groups:
            deictics[-1] = (word, ["-"])
        return f"{who}|-|-|move|{t}", f"{rng.choice(FILLERS)}{who_text(member, rng)}{rng.choice(MOVE_VERBS)} {word}", deictics
    if kind == "hold":
        named = [x for x in groups if unique_surface(x, groups)]  # a surface only one group stands on names the point
        if named and rng.random() < 0.6:
            g = rng.choice(named)
            return f"{who}|-|-|hold|E{g['id']}", f"{rng.choice(FILLERS)}{who_text(member, rng)}{rng.choice(HOLD_VERBS)} {g['surface']}".replace("hold the the", "hold the"), deictics
        return f"{who}|-|-|hold|-", f"{rng.choice(FILLERS)}{who_text(member, rng)}{rng.choice(['hold position', 'hold here', 'stay here', 'hold this position', 'dig in', 'take cover', 'stay put'])}", deictics
    if kind == "follow":
        return f"{who}|-|-|follow|-", f"{rng.choice(FILLERS)}{who_text(member, rng)}{rng.choice(FOLLOW)}", deictics
    if kind == "equip":
        m = member or rng.choice(members)
        w = rng.choice(m["weapons"])
        return f"{m['letter']}|{w}|-|-|-", f"{rng.choice(FILLERS)}{who_text(m, rng)}{rng.choice(['switch to your', 'equip your', 'use your', 'go', 'take your', 'pull out your'])} {rng.choice(WEAPONS[w])}", deictics
    if kind == "use":
        m = member or rng.choice(members)
        p = rng.choice(m["powers"])
        if groups and rng.random() < 0.7:
            g = rng.choice(groups)
            phrase, target = target_phrase(g)
            say = f"{rng.choice(FILLERS)}{who_text(m, rng)}{rng.choice(['', 'use ', 'hit them with ', 'throw a ', 'pop '])}{p['name'].lower()} {rng.choice(['on', 'at', ''])} {phrase}".replace("  ", " ")
            return f"{m['letter']}|-|{p['code']}|use|{target}", say, deictics
        return f"{m['letter']}|-|{p['code']}|use|-", f"{rng.choice(FILLERS)}{who_text(m, rng)}{rng.choice(['', 'use ', 'give me '])}{p['name'].lower()}", deictics
    if kind == "compound" and groups and len(members) == 2:
        a, b = members
        g = rng.choice(groups)
        w = rng.choice(a["weapons"])
        phrase, target = target_phrase(g)
        say = f"{who_text(a, rng)}{rng.choice(['switch to your', 'equip your'])} {rng.choice(WEAPONS[w])}{rng.choice(CONJ)}{rng.choice(ATTACK_VERBS)} {phrase}"
        if rng.random() < 0.5:
            say += f"{rng.choice(CONJ)}{who_text(b, rng)}{rng.choice(FOLLOW)}"
            return f"{a['letter']}|{w}|-|-|- ; {a['letter']}|-|-|attack|{target} ; {b['letter']}|-|-|follow|-", say, deictics
        return f"{a['letter']}|{w}|-|-|- ; {a['letter']}|-|-|attack|{target}", say, deictics
    if kind == "correction" and len(groups) >= 2:
        g1, g2 = rng.sample(groups, 2)
        p1, p2 = describe(g1, rng, groups), describe(g2, rng, groups)
        if p1 is not None and p2 is not None:
            say = f"{who_text(member, rng)}{rng.choice(ATTACK_VERBS)} {p1}, {rng.choice(['no', 'no wait', 'no no', 'sorry'])}, {p2}"
            return f"{who}|-|-|attack|E{g2['id']}", say, deictics
        kind = "talk"  # no words for one of them: this record is talk instead
    if kind == "cancel":
        return "-|-|-|cancel|-", rng.choice(CANCEL), deictics
    return "none", rng.choice(TALK), deictics


DROPPABLE = {"uh", "um", "okay", "hey", "alright", "the", "your", "then", "and", "please", "just"}  # never a word that carries the target


def noisy(text, rng, rate):
    out = []
    for i, word in enumerate(text.split()):
        bare = re.sub(r"[^a-z']", "", word.lower())
        if bare in ASR_SLIPS and rng.random() < rate:
            out.append(rng.choice(ASR_SLIPS[bare]))
        elif (bare in DROPPABLE or (i == 0 and bare == "right")) and rng.random() < rate * 0.3:
            continue  # a dropped filler (a dropped "at platform" once turned a hold at the platform into a bare "hold position" with the platform's gold)
        else:
            out.append(word.lower().strip(",") if rng.random() < 0.7 else word)
    s = " ".join(out)
    if rng.random() < rate * 0.3:
        s = s.replace(",", "")
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="scratch/voice-model/train.jsonl")
    ap.add_argument("--count", type=int, default=20000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--noise", type=float, default=0.3)
    ap.add_argument("--teacher-lines", default="")
    ap.add_argument("--generator", type=int, default=2, help="the generator's version, for the run config's hash (2: unambiguous descriptions)")
    args = ap.parse_args()
    rng = random.Random(args.seed)
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    kinds = {}
    with out.open("w", encoding="utf-8") as f:
        for _ in range(args.count):
            scene = sample_scene(rng)
            answer, say, deictics = gold_and_say(scene, rng)
            say = noisy(say, rng, args.noise) if rng.random() < 0.8 else say
            record = {"prompt": prefix(scene) + block(scene, say, deictics) + "\nACTION:", "answer": answer}  # core/voice/Grammar's Prompt
            f.write(json.dumps(record, ensure_ascii=False) + "\n")
            k = answer.split("|")[3] if "|" in answer else answer
            kinds[k] = kinds.get(k, 0) + 1
        if args.teacher_lines:
            for line in pathlib.Path(args.teacher_lines).read_text(encoding="utf-8").splitlines():
                if line.strip():
                    f.write(line.strip() + "\n")
    print(f"{args.count} examples -> {out}; orders: {dict(sorted(kinds.items()))}")


if __name__ == "__main__":
    main()
