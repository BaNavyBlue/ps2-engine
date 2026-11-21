#!/usr/bin/env python3
"""Static control-flow map of ps2.asm.

Three things are recoverable without running anything:

  calls        bsr/jsr to a named label
  transitions  `move.b #ScreenID_X, (game_screen).w` inside a routine,
               which is how the screen state machine actually moves
  dispatches   computed jumps (`jsr Table(pc,d0.w)`), which do NOT name
               their target -- the table has to be read separately

The third is why a call graph alone is not the whole story: every screen,
object and battle action goes through a computed jump, so the edges that
matter most are exactly the ones a naive scan loses.
"""

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
CALL_RE = re.compile(r"^\s*(bsr|jsr)(?:\.[bwsl])?\s+\(?([A-Za-z_][A-Za-z0-9_]*)")
JUMP_RE = re.compile(r"^\s*(jmp|bra)(?:\.[bwsl])?\s+\(?([A-Za-z_][A-Za-z0-9_]*)")
COMPUTED_RE = re.compile(
    r"^\s*(jmp|jsr)(?:\.[bwsl])?\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(pc\s*,")
SCREEN_RE = re.compile(
    r"^\s*move\.b\s+#(ScreenID_[A-Za-z0-9_]+)\s*,\s*\(game_screen\)")


def parse(asm_path):
    lines = asm_path.read_text(errors="replace").splitlines()

    routines = []          # (start_line, name)
    for i, line in enumerate(lines):
        m = LABEL_RE.match(line)
        if m:
            routines.append((i, m.group(1)))

    def owner(index):
        """Name of the routine a line belongs to."""
        lo, hi = 0, len(routines) - 1
        best = None
        while lo <= hi:
            mid = (lo + hi) // 2
            if routines[mid][0] <= index:
                best = routines[mid][1]
                lo = mid + 1
            else:
                hi = mid - 1
        return best

    calls = defaultdict(set)
    jumps = defaultdict(set)
    computed = defaultdict(set)
    transitions = defaultdict(set)

    for i, line in enumerate(lines):
        src = owner(i)
        if src is None:
            continue

        m = COMPUTED_RE.match(line)
        if m:
            computed[src].add(m.group(2))
            continue

        m = CALL_RE.match(line)
        if m:
            calls[src].add(m.group(2))
            continue

        m = JUMP_RE.match(line)
        if m:
            jumps[src].add(m.group(2))
            continue

        m = SCREEN_RE.match(line)
        if m:
            transitions[src].add(m.group(1))

    return {
        "routines": {name: ln + 1 for ln, name in routines},
        "calls": {k: sorted(v) for k, v in calls.items()},
        "jumps": {k: sorted(v) for k, v in jumps.items()},
        "computed": {k: sorted(v) for k, v in computed.items()},
        "transitions": {k: sorted(v) for k, v in transitions.items()},
    }


def screen_flow(graph):
    """Which routine moves the game to which screen."""
    flow = defaultdict(set)
    for routine, targets in graph["transitions"].items():
        for t in targets:
            flow[routine].add(t)
    return flow


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("asm", type=Path)
    ap.add_argument("-o", "--out", type=Path,
                    default=Path("route_out/callgraph.json"))
    ap.add_argument("--screens", action="store_true",
                    help="print the screen state machine")
    ap.add_argument("--calls", metavar="ROUTINE",
                    help="print what one routine calls")
    args = ap.parse_args()

    graph = parse(args.asm)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(graph, indent=1, sort_keys=True), newline="\n")

    print(f"{len(graph['routines'])} routines, "
          f"{sum(len(v) for v in graph['calls'].values())} calls, "
          f"{sum(len(v) for v in graph['computed'].values())} computed jumps, "
          f"{sum(len(v) for v in graph['transitions'].values())} "
          f"screen transitions -> {args.out}")

    if args.screens:
        print("\nscreen state machine:")
        for routine, targets in sorted(screen_flow(graph).items()):
            line = graph["routines"].get(routine, 0)
            print(f"  {routine} (line {line})")
            for t in sorted(targets):
                print(f"      -> {t}")

    if args.calls:
        name = args.calls
        print(f"\n{name} (line {graph['routines'].get(name, 0)})")
        for c in graph["calls"].get(name, []):
            print(f"   calls     {c}")
        for c in graph["computed"].get(name, []):
            print(f"   dispatch  {c}(pc,dn)   [table, target unresolved]")
        for c in graph["transitions"].get(name, []):
            print(f"   screen -> {c}")


if __name__ == "__main__":
    main()
