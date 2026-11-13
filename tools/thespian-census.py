# SPDX-License-Identifier: MIT
"""Thespian mode's census of the game's dialogue (docs/thespian-mode-plan.md phase 0, research/70): reads the line dump of
tools/conversation-scan (--wheels) and counts what the mode has to handle.

  tools/conversation-scan/bin/Release/net10.0-windows/conversation-scan.exe <CookedPCConsole> scratch/thespian/lines.jsonl --wheels
  python tools/thespian-census.py scratch/thespian/lines.jsonl [--cases scratch/thespian/cases.tsv] [--examples N]

Words used here (the same in the research note and the C++ matcher):
  wheel      an entry (someone's line) that offers at least one reply of type REPLY_STANDARD: the game shows the wheel
  card       one reply on a wheel that the player can pick: it has a label (the wheel's short text) and usually a line
             (what Shepard says); a card without a line is a "menu" card (the label is all there is to say)
  unchosen   a line Shepard says without the wheel: a REPLY_AUTOCONTINUE reply with text, or an entry spoken by the player

--cases writes one row per card of every wheel for the matcher's bench (tools/thespian-bench, mevr-thespian-bench):
  wheel id <TAB> card index <TAB> reply index <TAB> category <TAB> style <TAB> label <TAB> line <TAB> outcome key
--vo-list writes the recordings to extract for the speech bench (tools/thespian-vo-bench.py): one card with a recorded line
from each of --vo-sample wheels picked at random (--seed), the male and the female player's take of it:
  banks <TAB> string id <TAB> M or F <TAB> wheel id <TAB> card index <TAB> line
The dump and the cases are game text: keep them out of the repository (scratch/ is ignored).
"""
import argparse
import collections
import json
import re
import statistics
import sys

STANDARD = "REPLY_STANDARD"
AUTO = "REPLY_AUTOCONTINUE"


def words(text):
    """The matcher's own word split, near enough for counting: lower case, letters, digits and apostrophes."""
    return [w for w in re.sub(r"[^a-z0-9' ]+", " ", (text or "").lower().replace("’", "'")).split() if w]


def clean(text):
    return " ".join((text or "").split())


def percentile(values, p):
    if not values:
        return 0
    values = sorted(values)
    return values[min(len(values) - 1, int(round(p * (len(values) - 1))))]


def unique_prefix(card_words, others):
    """How many words of this card must be heard, from its start, before no other card of the wheel starts the same way.
    None: another card is identical or this one is a prefix of another (never told apart by its opening alone)."""
    for n in range(1, len(card_words) + 1):
        head = card_words[:n]
        if not any(o[:n] == head for o in others):
            return n
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--cases")
    ap.add_argument("--examples", type=int, default=6)
    ap.add_argument("--vo-list", help="write the extractor's list (conversation-scan --vo-extract) for a sample of recorded lines")
    ap.add_argument("--vo-sample", type=int, default=600, help="wheels to sample for --vo-list (one card each, both takes)")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    conversations = [json.loads(l) for l in open(args.dump, encoding="utf-8") if '"kind":"lines"' in l]
    wheels = []            # (conversation name, entry index, cards)
    sizes = collections.Counter()
    spoken_sizes = collections.Counter()
    categories = collections.Counter()
    styles = collections.Counter()
    line_words = []        # per distinct card line
    line_seconds = []
    menu_cards = 0
    cards_total = 0
    one_word = collections.Counter()
    identical_same = identical_diff = 0
    identical_examples = []
    prefix_need = []
    never_by_opening = 0
    unchosen_auto = unchosen_entry = 0
    unchosen_words = []
    unchosen_seconds = []
    unskippable_cards = 0
    gender_differs = 0
    tokens = 0
    investigate_wheels = 0
    seen_lines = set()
    npc_lines = 0
    rows = []
    recorded = []          # (wheel id, banks, [(card index, card)] with a recording, the conversation's waves)

    for conv in conversations:
        replies = conv["replies"]
        waves = conv.get("waves", {})
        for ei, entry in enumerate(conv["entries"]):
            if entry["speaker"] == -2 and entry.get("text") and not entry.get("nonText"):
                unchosen_entry += 1
                unchosen_words.append(len(words(entry["text"])))
            elif entry.get("text"):
                npc_lines += 1
            offers = [o for o in entry["offers"] if 0 <= o["reply"] < len(replies)]
            cards = []
            for o in offers:
                r = replies[o["reply"]]
                if r["type"] != STANDARD:
                    continue
                line = clean(r.get("text"))
                label = clean(o.get("label"))
                if not line and not label:
                    continue  # nothing to show or say: the game shows no card for it either
                cards.append({"reply": o["reply"], "category": o["category"].replace("REPLY_CATEGORY_", ""),
                              "style": r["style"].replace("GUI_STYLE_", ""), "label": label, "line": line,
                              "outcome": (tuple(r["leadsTo"]), r["transition"], r["script"]), "unskippable": r["unskippable"],
                              "ref": r["textRef"], "differs": "textM" in r})
            if not cards:
                continue
            wheel_id = f"{conv['name']}#{ei}"
            wheels.append((wheel_id, cards))
            recorded.append((wheel_id, ",".join(conv.get("banks", [])),
                             [(i, c) for i, c in enumerate(cards) if c["line"] and (f"{c['ref']}_M" in waves or f"{c['ref']}_F" in waves)], waves))
            sizes[len(cards)] += 1
            spoken = [c for c in cards if c["line"]]
            spoken_sizes[len(spoken)] += 1
            if any(c["category"] == "INVESTIGATE" for c in cards):
                investigate_wheels += 1
            texts = collections.defaultdict(list)
            for c in cards:
                cards_total += 1
                categories[c["category"]] += 1
                styles[c["style"]] += 1
                if not c["line"]:
                    menu_cards += 1
                    continue
                unskippable_cards += 1 if c["unskippable"] else 0
                texts[" ".join(words(c["line"]))].append(c)
                if c["ref"] not in seen_lines:
                    seen_lines.add(c["ref"])
                    w = words(c["line"])
                    line_words.append(len(w))
                    if len(w) == 1:
                        one_word[w[0]] += 1
                    seconds = waves.get(f"{c['ref']}_M", waves.get(f"{c['ref']}_F"))
                    if seconds:
                        line_seconds.append(seconds)
                    gender_differs += 1 if c["differs"] else 0
                    tokens += 1 if re.search(r"[<\[{%]", c["line"]) else 0
            for text, same in texts.items():
                if len(same) > 1:
                    if len({c["outcome"] for c in same}) == 1:
                        identical_same += 1
                    else:
                        identical_diff += 1
                        if len(identical_examples) < args.examples:
                            identical_examples.append((wheel_id, [(c["label"], c["line"]) for c in same]))
            split = [(c, words(c["line"])) for c in spoken]
            for c, w in split:
                others = [ow for oc, ow in split if oc is not c]
                need = unique_prefix(w, others) if others else 1
                if need is None:
                    never_by_opening += 1
                else:
                    prefix_need.append(need)
            for i, c in enumerate(cards):
                rows.append("\t".join([wheel_id, str(i), str(c["reply"]), c["category"], c["style"], c["label"], c["line"],
                                       f"{list(c['outcome'][0])}/{c['outcome'][1]}/{c['outcome'][2]}"]))
        for r in replies:
            if r["type"] == AUTO and clean(r.get("text")) and not r.get("nonText"):
                unchosen_auto += 1
                unchosen_words.append(len(words(r["text"])))
                seconds = waves.get(f"{r['textRef']}_M", waves.get(f"{r['textRef']}_F"))
                if seconds:
                    unchosen_seconds.append(seconds)

    def table(counter, order=None):
        keys = order or sorted(counter)
        return ", ".join(f"{k}: {counter[k]}" for k in keys if counter[k])

    print(f"conversations                         {len(conversations)}")
    print(f"wheels (entries offering picked replies) {len(wheels)}")
    print(f"  cards on a wheel                     {table(sizes)}")
    print(f"  cards with a spoken line on a wheel  {table(spoken_sizes)}")
    print(f"  wheels with investigate cards        {investigate_wheels}")
    print(f"cards                                  {cards_total}")
    print(f"  with a spoken line                   {cards_total - menu_cards}  (distinct lines: {len(seen_lines)})")
    print(f"  label only (menu cards)              {menu_cards}")
    print(f"  by category                          {table(categories)}")
    print(f"  by style                             {table(styles)}")
    print(f"  unskippable                          {unskippable_cards}")
    print(f"Shepard's picked lines, distinct       {len(seen_lines)}")
    if line_words:
        print(f"  words per line                       median {statistics.median(line_words)}, p10 {percentile(line_words, 0.1)}, "
              f"p90 {percentile(line_words, 0.9)}, max {max(line_words)}")
        print(f"  one-word lines                       {sum(one_word.values())}  ({', '.join(f'{w} {n}' for w, n in one_word.most_common(8))})")
        print(f"  two words or fewer                   {sum(1 for n in line_words if n <= 2)}")
    if line_seconds:
        print(f"  seconds per line (recorded)          median {statistics.median(line_seconds):.1f}, p90 {percentile(line_seconds, 0.9):.1f}, "
              f"max {max(line_seconds):.1f}, total {sum(line_seconds) / 3600:.1f} h over {len(line_seconds)} lines with a recording")
    print(f"  text differs for the male player     {gender_differs}")
    print(f"  lines with tokens (<...>, [...])     {tokens}")
    print(f"telling cards apart by their opening words (cards with a line, on wheels with two or more such cards)")
    if prefix_need:
        hist = collections.Counter(min(n, 8) for n in prefix_need)
        print(f"  words needed                         {', '.join(f'{k if k < 8 else '8+'}: {hist[k]}' for k in sorted(hist))}")
        print(f"  median {statistics.median(prefix_need)}, p90 {percentile(prefix_need, 0.9)}, p99 {percentile(prefix_need, 0.99)}")
    print(f"  never by the opening (identical to, or the start of, another card) {never_by_opening}")
    print(f"same line twice on one wheel           same outcome {identical_same}, different outcome {identical_diff}")
    for wheel_id, same in identical_examples:
        print(f"    {wheel_id}: " + " | ".join(f"[{label}] {line[:60]}" for label, line in same))
    print(f"Shepard's unchosen lines               {unchosen_auto + unchosen_entry}  (auto-continue replies with text {unchosen_auto}, "
          f"entries spoken by the player {unchosen_entry})")
    if unchosen_words:
        print(f"  words per line                       median {statistics.median(unchosen_words)}, p90 {percentile(unchosen_words, 0.9)}, max {max(unchosen_words)}")
    if unchosen_seconds:
        print(f"  seconds (recorded)                   total {sum(unchosen_seconds) / 60:.0f} min over {len(unchosen_seconds)} lines")
    print(f"other people's lines with text         {npc_lines}")
    if args.vo_list:
        import random
        rng = random.Random(args.seed)
        pool = [r for r in recorded if r[1] and r[2]]
        rng.shuffle(pool)
        out = []
        for wheel_id, banks, with_wave, waves in pool[:args.vo_sample]:
            i, c = rng.choice(with_wave)
            for take, key in (("F", f"{c['ref']}_F"), ("M", f"{c['ref']}_M")):
                # The female player's takes are in the banks without a SoundNodeWave of their own in the dialogue
                # packages: listed always; the extractor skips the ones a bank doesn't hold.
                if key in waves or take == "F":
                    out.append("\t".join([banks, str(c["ref"]), take, wheel_id, str(i), c["line"]]))
        with open(args.vo_list, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(out) + "\n")
        print(f"{len(out)} recordings of {min(args.vo_sample, len(pool))} wheels (of {len(pool)} with recorded lines) -> {args.vo_list}")
    if args.cases:
        with open(args.cases, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(rows) + "\n")
        print(f"{len(rows)} cards of {len(wheels)} wheels -> {args.cases}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
