#!/usr/bin/env python3
"""(build 129d) Hunting the Mega Drive's lost music part: routes that play
menu sounds around a pause and an unpause, each at every tick offset, from
Motavia (routes/saves/jet_scooter.json), each ending `expect stuck none`
(a music track still lent to an effect that no longer holds it; build
132c: or one a pause turned down that nothing turned back up). Prints
the orders that leave one. Run from the tree's top after `make
route_probe`; each route is written, run and removed as routes/zz_*.route.
Add orders to `variants` to widen the search.
"""
import subprocess, itertools, os, sys
PRE = """start slot 1 routes/saves/jet_scooter.json
wait idle
press c
wait idle
press c
wait idle
press c
wait idle
press c
wait idle
press c
wait idle
press c
wait screen level
wait idle
wait ticks 30
"""
def run_raw(name, body):
    path = f"routes/zz_{name}.route"
    open(path, "w").write(body + "expect stuck none\n")
    p = subprocess.run(["./route_probe", path], capture_output=True, text=True)
    os.remove(path)
    subprocess.run(["rm", "-rf", f"route_out/zz_{name}"])
    return ((p.stdout.strip() or p.stderr.strip() or "no output").splitlines())[-1]


def run(name, body):
    path = f"routes/zz_{name}.route"
    open(path, "w").write(PRE + body + "wait ticks 150\nexpect stuck none\n")
    out = subprocess.run(["./route_probe", path], capture_output=True, text=True).stdout.strip().splitlines()[-1]
    os.remove(path)
    subprocess.run(["rm", "-rf", f"route_out/zz_{name}"])
    return out
# (build 129e) In a fight under Rise or Fall (formation 1, Rolf alone at
# level 1, so the rounds go on): FIGHT, then at each offset through the
# round a pause and an unpause, frame-advance by C, or four B presses.
BATTLE = "start battle 1\nparty 1 1\nlimit 8000\nwait idle\nexpect music $92\npress c\n"
battle = {}
for d in range(0, 240, 6):
    battle[f"b_pause_{d}"] = BATTLE + f"wait ticks {d}\npress start\nwait ticks 20\npress start\nwait idle\n"
    battle[f"b_advance_{d}"] = BATTLE + f"wait ticks {d}\npress start\nwait ticks 4\npress c\nwait ticks 4\npress c\nwait ticks 4\npress c\nwait ticks 4\npress start\nwait idle\n"
    battle[f"b_slowmo_{d}"] = BATTLE + f"wait ticks {d}\npress start\nwait ticks 4\n" + "press b\nwait ticks 2\n" * 4 + "press start\nwait idle\n"
hits = []
variants = {}
for d in range(0, 26, 1):
    variants[f"menu_open_{d}"] = f"press c\nwait ticks {d}\npress start\nwait ticks 20\npress start\n"
    variants[f"menu_move_{d}"] = f"press c\nwait idle\npress down\nwait ticks {d}\npress start\nwait ticks 20\npress start\n"
    variants[f"menu_pick_{d}"] = f"press c\nwait idle\npress c\nwait ticks {d}\npress start\nwait ticks 20\npress start\n"
    # (build 132c) the unpause, then a sound inside the unpause jingle's
    # phrase: an FM5 effect takes its FM5 track before the walk back up
    variants[f"unpause_a_{d}"] = f"press start\nwait ticks 40\npress start\nwait ticks {d}\npress a\n"
    variants[f"unpause_c_{d}"] = f"press start\nwait ticks 40\npress start\nwait ticks {d}\npress c\n"
    variants[f"unpause_menu_{d}"] = f"press start\nwait ticks 20\npress start\nwait ticks {d}\npress c\nwait idle\npress down\nwait idle\npress b\n"
for name, body in battle.items():
    variants[name] = None
    r = run_raw(name, body)
    if not r.startswith("PASS"):
        hits.append((name, r[:160]))
for name, body in variants.items():
    if body is None:
        continue
    r = run(name, body)
    if not r.startswith("PASS"):
        hits.append((name, r[:160]))
print(len(variants), "variants;", len(hits), "not PASS")
for h in hits[:20]: print(*h)
