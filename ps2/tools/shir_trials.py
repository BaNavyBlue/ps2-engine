#!/usr/bin/env python3
"""Shir's thefts, measured (build 87a).

From a checkpoint in Paseo (story_dams.json by default: Shir not yet
joined, party_member_join_next 7), Shir is fetched into the party
(play.shir_join), raised to 10 beside the others when the Visiphone is
wanted (play.raise_member), and walked in and out of Paseo's shops and
the Central Tower's room until the party carries what is asked
(play.shir_thefts), back from Rolf's house after each theft. Prints the
tally against ProcessStealItem's one in sixteen.

    python3 tools/shir_trials.py
    python3 tools/shir_trials.py --want StarMist:2 MoonDew:2 Visiphone:1
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import play  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--save", default="routes/saves/story_dams.json")
    ap.add_argument("--want", nargs="+",
                    default=["StarMist:1", "MoonDew:1", "Visiphone:1"])
    ap.add_argument("--max-exits", type=int, default=600)
    args = ap.parse_args()
    wants = {w.split(":")[0]: int(w.split(":")[1]) for w in args.want}

    s = play.Session("shir_trials")
    play.continue_game(s, args.save)
    if not play.shir_join(s):
        sys.exit("Shir did not join")
    if any(play.steal_source(i) == "room" for i in wants):
        if not play.raise_member(s, "Shir", 10):
            sys.exit("could not raise Shir to 10")
        play.enter_town(s, play.PASEO)
    t = play.shir_thefts(s, wants, max_exits=args.max_exits)
    exits = sum(t["exits"].values())
    thefts = sum(t["thefts"].values())
    if exits:
        print(f"{thefts} thefts in {exits} exits: {thefts / exits:.3f} "
              f"against {play.STEAL_ODDS:.4f}")
    s.close()


if __name__ == "__main__":
    main()
