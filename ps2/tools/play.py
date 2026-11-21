#!/usr/bin/env python3
"""Play the game like a person, fast: a driver on `route_probe -i`.

The probe plays as quickly as the machine runs it and only renders on
`shot`, so "clock speed" is however many ticks a second the box manages.
This driver types route commands down its stdin, syncs on the `state`
line, and makes the decisions a player would: press C through fights and
text, count the fights and level-ups, stop grinding at a level.

    python3 tools/play.py            # the demo grind at the bottom
    from play import Session         # or drive it from a script

Everything the game printed -- scripts shown, screens, stderr -- streams
into self.events, and route_out/<name>/log.txt keeps the whole session.
"""

import contextlib
import functools
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path


class Stall(Exception):
    pass


def write_lf(path, text):
    """(build 162) `text` to the file `path` with LF line ends on every
    OS: Path.write_text's newline is Python 3.10's, and COMPUTER PLAYS runs
    on whatever Python the player has, 3.8 or newer (autoplay.c)."""
    with open(path, "w", newline="\n") as f:
        f.write(text)


# (build 98) Lean grinding, for tools/playthrough.py: a grind that runs out
# of trips (or a stage's time budget) short of its level goes on with the
# party it has, in town, healed and geared, instead of failing the stage.
# The targets are unchanged; they stop being a wall.
LEAN = False

# (build 98) A player's pauses: before a walk or a press on the map or in a
# fight, with this chance, 1 to JITTER_MAX ticks with nothing pressed.
JITTER_P = 0.25
JITTER_MAX = 24

MASK64 = (1 << 64) - 1


class Jitter:
    """(build 170) The seed's own stream of pauses (Session.jitter), drawn
    the same in C: FNV-1a of the stream's name (`seed/stage/attempt`) seeds
    a SplitMix64. Until 170 a random.Random(name): CPython's Mersenne
    Twister, seeded through a SHA-512 of the name. Only random() and
    randint are drawn."""

    def __init__(self, name):
        h = 0xCBF29CE484222325
        for b in str(name).encode("utf-8"):
            h = ((h ^ b) * 0x100000001B3) & MASK64
        self.state = h

    def next64(self):
        self.state = (self.state + 0x9E3779B97F4A7C15) & MASK64
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
        return z ^ (z >> 31)

    def random(self):
        """[0, 1): the top 53 bits."""
        return (self.next64() >> 11) / 9007199254740992.0

    def randint(self, a, b):
        """a to b, both in: a draw under the largest multiple of the
        count, taken mod it."""
        n = b - a + 1
        limit = ((1 << 64) // n) * n
        while True:
            x = self.next64()
            if x < limit:
                return a + x % n

# (build 101) The fight's pace: how the harness waits while a fight is busy
# (a round animating, a page of text typing) before it looks again.
#   probe: `wait ticks 240`, then C if the fight is still busy. The pace of
#     every route, trial and seed since build 60: free in the probe, 4 s at a
#     time in the window, most of it a menu or a finished page sitting there.
#   watch: COMPUTER PLAYS's. `wait pad 30` -- the moment the game waits on
#     the pad (a menu up, a page at its stop), or 30 ticks -- then C only if
#     the fight is still busy with something other than text typing (a
#     round that repeats is interrupted as before; a page is never hurried),
#     and the player's pauses in a fight half as long.
#   (build 101b) watch also holds each of the victory's pages at its stop
#     for victory_hold ticks before its C (The owner: 101's pages went by too
#     fast to read), so a won fight's pages last about twice as long.
#   (build 102b) the owner: that more than doubled them -- half the hold, 15
#     ticks a stop, give or take up to victory_slack (5) drawn from the
#     seed's own pauses (Session.jitter), so no two stops wait alike.
# A pace changes what the game meets (the RNG turns every tick), so a seed
# plays one game per pace; playthrough.py --pace watch is the window's game.
PACES = {
    "probe": {"fight_poll": 240, "pad_waits": False,
              "fight_jitter_max": JITTER_MAX, "victory_hold": 0,
              "victory_slack": 0},
    "watch": {"fight_poll": 30, "pad_waits": True,
              "fight_jitter_max": JITTER_MAX // 2, "victory_hold": 15,
              "victory_slack": 5},
}

# (build 101b) The victory's pages, each ending at a stop that waits for a
# button ($C4; five stop at a {pause} first): "<leader> and party are
# victorious!" ($1211) through "Learned a new technique!" ($1217) -- the
# victory and its EXP (loc_F270), the meseta (loc_F308), the level-ups
# (Character_ProcessLevelUp).
SCRIPT_VICTORY_PAGES = frozenset(range(0x1211, 0x1218))

# (build 108) The screens a wipe-out leaves the game on, in turn: the game
# over, the SEGA screen, the title. The party gets past none of them; a
# CONTINUE (story.recover, continue_game) is the way on.
WIPED = ("gameover", "sega", "title")


# The `state` line: where the game is, what holds the pad, and -- once a
# game is on -- the building, the live menu's cursor, the party and the
# money (route_probe.c, describe and describe_party).
STATE_RE = re.compile(
    r"ok\s+tick (\d+): screen (\w+)[^,]*"
    r", (waiting|busy): ([^,]+?)\s*"
    r"(?:, level (\$?\w+) cell (\d+),(\d+) facing (\w+)( mid-step)?"
    r"( riding)?)?"
    r"(?:, building (\d+))?"
    r"(?:, menu \$(\w+)(?: stop (\d+)/(\d+)( across)?( party)?)?)?"
    r"(?:, party (.*?), meseta (\d+))?"
    r"(?:, formation (\d+) enemies((?: \d+:\d+/\d+)*))?$")

MEMBER_RE = re.compile(
    r"(\w+) L(\d+) (\d+)/(\d+) (\d+)/(\d+)((?: dead)?(?: poison)?"
    r"(?: cursed \d)?(?: marked)?(?: sealed)?(?: deban)?)")


def state_level(word):
    """The state line's level: its name (build 106) or, from an older
    probe, $XX."""
    if not word:
        return None
    if word.startswith("$"):
        return int(word[1:], 16)
    return map_index(word)


def parse_state(m):
    g = [None] + [m.group(i) for i in range(1, 21)]
    st = {
        "tick": int(g[1]), "screen": g[2],
        "kind": g[3], "wait": g[4].strip(),
        "level": state_level(g[5]),
        "x": int(g[6]) if g[6] else None,
        "y": int(g[7]) if g[7] else None,
        "facing": g[8],
        "mid_step": bool(g[9]),
        "riding": bool(g[10]),
        "building": int(g[11]) if g[11] else None,
        "menu": int(g[12], 16) if g[12] else None,
        "menu_stop": int(g[13]) if g[13] else None,
        "menu_stops": int(g[14]) if g[14] else None,
        "menu_across": bool(g[15]),
        "menu_party": bool(g[16]),
        "party": [],
        "meseta": int(g[18]) if g[18] else None,
        "formation": int(g[19]) if g[19] else None,
        "enemies": [tuple(int(v) for v in re.split(r"[:/]", e))
                    for e in (g[20] or "").split()],
    }
    if g[17]:
        for mm in MEMBER_RE.finditer(g[17]):
            st["party"].append({
                "name": mm.group(1), "level": int(mm.group(2)),
                "hp": int(mm.group(3)), "max_hp": int(mm.group(4)),
                "tp": int(mm.group(5)), "max_tp": int(mm.group(6)),
                "dead": "dead" in mm.group(7),
                "poison": "poison" in mm.group(7),
                # Dark Force's curse (build 94): the kind while it is
                # pending or repeats (1-3 lose every turn), 0 once spent.
                "curse": (int(re.search(r"cursed (\d)", mm.group(7)).group(1))
                          if "cursed" in mm.group(7)
                          else (0 if "marked" in mm.group(7) else None)),
                "sealed": "sealed" in mm.group(7),
                "deban": "deban" in mm.group(7),
            })
    return st


class _Attached:
    """(build 100b) The game's own COMPUTER PLAYS pipes in place of a
    route_probe: commands written to one file descriptor, the log read from
    the other (tools/computer_plays.py, docs/computer_plays.md). The game
    owns the process; there is nothing here to kill or wait for."""

    def __init__(self, cmd_fd, log_fd):
        self.stdin = os.fdopen(cmd_fd, "w", buffering=1, newline="\n")
        self.stdout = os.fdopen(log_fd, "r")

    def kill(self):
        pass

    def wait(self):
        return 0


class Session:
    def __init__(self, name="play", probe="./route_probe", trace=None,
                 attach=None):
        self.name = name
        # (build 120) The game's COMPUTER PLAYS, not a route_probe: host
        # lines (grind) go to it.
        self.attached = attach is not None
        self._grind_depth = 0
        if attach is not None:
            self.p = _Attached(*attach)
        else:
            self.p = subprocess.Popen([probe, "-i", "-o",
                                       f"route_out/{name}"],
                                      stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, text=True)
        # (build 100b) Where the game writes this session's saves, and how
        # long a silence is a stall: a route_probe's own directory and 60 s;
        # the game's COMPUTER PLAYS, its slots and never (at 60 Hz a long
        # wait is minutes of real time with nothing to say).
        self.saves_dir = Path(f"route_out/{name}/saves")
        self.read_timeout = 60.0
        if attach is not None:
            self.saves_dir = Path("saves/computer/slots")
            self.read_timeout = None
        self.events = []        # every log line, in order
        self.state = {}
        self.shown = []         # every script id shown, in order
        self.shown_ticks = []   # (build 101) ... and the tick of each
        self.last_script = 0
        self.fights = []        # fight_smart's record of each fight
        self.tech_orders = []   # (member, technique, formation) ordered
        self.save_town = PASEO  # where a Telepipe / Ryuka lands ($C646)
        # (build 106) The floors loot() has worked since the last save,
        # and whether a CONTINUE has come since loot() last looked: a
        # CONTINUE shuts again every chest opened after the save it loads.
        self.looted = []
        self.continued = False
        self.careful = False    # goto gives up when hurt with no mates
        # (build 96c) goto's mending after each fight on the way: None is
        # the old rule (mates only once someone is under half); a fraction
        # revives the dead (Rever, a Moon Dew), cures poison and mends
        # everyone to it after every fight.
        self.walk_mend = None
        # (build 99) Moon Dews revive() leaves in the bags: on Noah the last
        # one is kept for the Nei Sword's holder (story.DEW_RESERVE).
        self.dew_reserve = 0
        self.hurt = False       # ... and says so here
        # (build 90) Who went down and how each came back: a member dead
        # in one state line and alive in a later one, with the screen,
        # the building and the last scripts at that moment (revival_how).
        self.downs = []
        self.revivals = []
        self._dead = set()
        self._screen = None
        self.trace = open(trace, "w", buffering=1, newline="\n") if trace else None
        # (build 98) A random.Random ((build 170) a Jitter), or None for no
        # pauses (every trial and every route is run without).
        # GenerateRandomNumber turns every frame, so a few idle ticks here
        # and there change every fight, every hit and every theft that
        # follows: one seed, one game.
        self.jitter = None
        self.jitter_ticks = 0
        # (build 101) The fight's pace (PACES): probe's unless set_pace.
        self.pace = "probe"
        self.fight_poll = 240
        self.pad_waits = False
        self.fight_jitter_max = JITTER_MAX
        self.victory_hold = 0
        self.victory_slack = 0
        # (build 101) Ticks played, counted from the state lines (a reset
        # starts the game's count again; the sum goes on), and the pauses
        # taken in a fight.
        self.ticks = 0
        self._last_tick = None
        self.jitter_fight_ticks = 0
        self._route_depth = 0
        # A reader thread and a queue, not select() on the pipe: readline
        # buffers a chunk at a time, so the fd can be quiet while lines
        # sit in the buffer, which select cannot see.
        self.q = queue.Queue()
        self.reader = threading.Thread(target=self._pump, daemon=True)
        self.reader.start()

    def _pump(self):
        for line in self.p.stdout:
            self.q.put(line.rstrip("\n"))
        self.q.put(None)

    def _readline(self, timeout=None):
        """One log line, or Stall if the probe says nothing for that
        long -- a wait command can run millions of ticks, so the timeout
        is on silence, not on the command."""
        if timeout is None:
            timeout = self.read_timeout
        try:
            line = self.q.get(timeout=timeout)
        except queue.Empty:
            raise Stall(f"no output for {timeout}s; last: "
                        + "; ".join(self.events[-3:])) from None
        if line is None:
            raise EOFError("the probe ended: " + "; ".join(self.events[-3:]))
        self.events.append(line)
        if self.trace:
            print(line, file=self.trace)
        return line

    def _pause(self, first):
        """(build 98) The idle ticks, if any, a player takes before `first`:
        only while the game waits on the pad on the map or in a fight, only
        before a walk or a press, never inside a .route file."""
        if self.jitter is None or self._route_depth or not first:
            return None
        st = self.state
        if st.get("kind") != "waiting" \
                or st.get("screen") not in ("level", "battle") \
                or not (first.startswith("walk ") or first.startswith("press ")):
            return None
        if self.jitter.random() >= JITTER_P:
            return None
        most = self.fight_jitter_max if st.get("screen") == "battle" \
            else JITTER_MAX
        n = self.jitter.randint(1, most)
        self.jitter_ticks += n
        if st.get("screen") == "battle":
            self.jitter_fight_ticks += n
        return f"wait ticks {n}"

    @contextlib.contextmanager
    def in_grind(self):
        """(build 120) A grind, for COMPUTER PLAYS's SPEED AUTO: `@grind
        on` to the game as the outermost begins, `@grind off` as it ends --
        host lines, which the game keeps from its interpreter and which run
        no tick. A route_probe is sent nothing, so its games are as before."""
        self._grind_depth += 1
        if self._grind_depth == 1:
            self._host_line("@grind on")
        try:
            yield
        finally:
            self._grind_depth -= 1
            if self._grind_depth == 0:
                self._host_line("@grind off")

    @contextlib.contextmanager
    def buying_gear(self):
        """(build 162a) A visit to a weapon or armour store that buys,
        for COMPUTER PLAYS's SPEED AUTO, which plays it at its own speed
        (The owner: buying gear can be watched; selling loot is
        tedious): `@gear on` before it, `@gear off` after, host lines as
        in_grind's."""
        self._host_line("@gear on")
        try:
            yield
        finally:
            self._host_line("@gear off")

    def _host_line(self, line):
        if not self.attached:
            return
        try:
            self.p.stdin.write(line + "\n")
            self.p.stdin.flush()
        except (OSError, ValueError):
            pass            # the pad was taken back: nobody to tell

    def set_pace(self, name):
        """(build 101) The fight's pace by name, from PACES."""
        for k, v in PACES[name].items():
            setattr(self, k, v)
        self.pace = name

    def fight_wait(self):
        """(build 101) The command that gives a busy fight a while."""
        if self.pad_waits:
            return f"wait pad {self.fight_poll}"
        return f"wait ticks {self.fight_poll}"

    def answer_page(self):
        """(build 101b) C at a page's stop; at the watch pace a victory page
        is held victory_hold ticks first. The probe's pace sends the C
        alone, as before."""
        if self.victory_hold and self.state.get("wait") == "text" \
                and self.last_script in SCRIPT_VICTORY_PAGES:
            hold = self.victory_hold
            if self.victory_slack and self.jitter is not None:
                # (build 102b) A little either way, from the seed's own
                # stream: the same seed holds the same stops alike.
                hold += self.jitter.randint(-self.victory_slack,
                                            self.victory_slack)
            self.send(f"wait ticks {hold}")
        return self.send("press c")

    def may_hurry(self):
        """(build 101) Whether C may interrupt the fight after fight_wait:
        always at the probe's pace; at the watch pace not while a page is
        still typing (a C there prints the rest at once, loc_FBE8)."""
        return not (self.pad_waits and self.state.get("wait") == "text printing")

    def send(self, *cmds):
        """Run commands, then `state`; return the lines they produced."""
        pause = self._pause(cmds[0] if cmds else None)
        if pause:
            self.p.stdin.write(pause + "\n")
        for c in cmds:
            self.p.stdin.write(c + "\n")
        self.p.stdin.write("state\n")
        self.p.stdin.flush()
        out = []
        while True:
            line = self._readline()
            out.append(line)
            m = re.search(r"\*\s+script \$(\w+)", line)
            if m:
                self.last_script = int(m.group(1), 16)
                self.shown.append(self.last_script)
                tm = re.match(r"\s*(\d+)", line)
                self.shown_ticks.append(int(tm.group(1)) if tm else None)
            m = STATE_RE.search(line)
            if m:
                self.state = parse_state(m)
                t = self.state["tick"]
                if self._last_tick is not None:
                    self.ticks += t - self._last_tick \
                        if t >= self._last_tick else t
                self._last_tick = t
                self._track_downs()
                return out

    def _track_downs(self):
        """(build 90) Downs and revivals from one state line to the next.
        Only a member in the party both times counts: one left dead at
        Rolf's house is not revived by leaving the party. A state that
        follows anything but the map, a building or a fight (a CONTINUE,
        the SEGA screen after a wipe) starts the count again."""
        st = self.state
        screen = st.get("screen")
        party = st.get("party") or []
        names = {p["name"] for p in party}
        dead = {p["name"] for p in party if p["dead"]}
        live = ("level", "building", "battle")
        if screen in live and self._screen in live:
            for n in sorted(dead - self._dead):
                self.downs.append({"name": n, "screen": screen,
                                   "level": st.get("level"),
                                   "tick": st.get("tick")})
            for n in sorted((self._dead - dead) & names):
                self.revivals.append({
                    "name": n, "screen": screen,
                    "building": st.get("building"),
                    "level": st.get("level"), "tick": st.get("tick"),
                    "scripts": self.shown[-4:]})
        if screen in live or screen is None:
            self._dead = dead
        else:
            self._dead = set()
        self._screen = screen

    def scripts(self, lines):
        return [int(m.group(1), 16) for ln in lines
                for m in [re.search(r"\*\s+script \$(\w+)", ln)] if m]

    def route(self, path):
        """A whole .route file, a line at a time (start included)."""
        self._route_depth += 1
        try:
            for line in Path(path).read_text().splitlines():
                body = line.split("#")[0].strip()
                if body:
                    self.send(body)
        finally:
            self._route_depth -= 1

    def new_game(self, limit=200):
        """(build 168e) Power on to the first frame the player has Rolf,
        in any language: routes/new_game.route's commands in its order,
        with no player's pauses as inside a .route file, but its pages
        answered until the street instead of counted. The route's counts
        are the cartridge's English: the retranslation's Governor has
        more pages, and its twelfth C left him talking under `wait screen
        level` (COMPUTER PLAYS at AUTO sat there at 60 Hz). In ENGLISH
        1989 this is the route tick for tick. The name: ROLF in ENGLISH
        1989, as the route types it; (build 168f) in any other language
        none (type_end), so the language's own default stands -- his Eusis,
        the cartridge's ユーシス. Until 168f the retranslation typed ROLF,
        the cartridge's default, which a loaded save makes the language's
        (gamedata.c's read_char): the hero was ROLF until the first
        CONTINUE and Eusis after it."""
        self._route_depth += 1
        try:
            for c in ("start new", "wait screen title", "wait idle",
                      "press start", "wait screen intro", "wait idle",
                      "press c", "wait idle"):          # NEW GAME, the board
                self.send(c)
            if language_is(self, "us"):
                for c in NAME_ROLF:
                    self.send(c)
            else:
                type_end(self)
            for _ in range(limit):
                self.send("wait idle")
                st = self.state
                if st.get("kind") != "waiting":
                    continue                    # a wait cut short: no C
                if st.get("screen") == "level" and st.get("wait") == "control":
                    break
                self.send("press c")
            self.send("walk down 2")            # the route's last steps
            self.send("wait step")
        finally:
            self._route_depth -= 1

    def press_through(self, screen="level", limit=200):
        """Play through whatever is up until `screen` waits on the pad:
        a fight (FIGHT every round, C to hurry a round that loops), a
        talk, a scene. Returns the scripts shown on the way. (build 108)
        Returns at once on a screen a wipe-out leaves (WIPED): nothing
        answered there reaches `screen`, and the title waits on Start
        alone, so C there went unanswered up to `limit` times."""
        shown = []
        for _ in range(limit):
            st = self.state
            if st.get("screen") in WIPED:
                return shown
            if (st.get("screen") == screen and st.get("kind") == "waiting"
                    and st.get("wait") == "control"):
                return shown
            if st.get("kind") == "waiting":
                if st.get("wait") == "menu" and st.get("screen") == "level":
                    out = self.send("press b")      # a menu C opened
                else:
                    out = self.answer_page()
            elif st.get("screen") == "battle":
                # A round repeats until a button interrupts (loc_F1D0):
                # give it a while, then press, as a player mashing would --
                # but look first, so a menu that has come back is seen
                # rather than answered blind.
                out = self.send(self.fight_wait())
                if self.state.get("kind") != "waiting" and self.may_hurry():
                    out += self.send("press c")
            else:
                out = self.send("wait ticks 60")
            shown += self.scripts(out)
        return shown

    def ready(self, limit=40):
        """Until the party is on the map under the pad: through a fade,
        a fight, text or a scene. False if it never gets there -- (build
        108) at once on a screen a wipe-out leaves (WIPED), where it used
        to hand the title to press_through up to `limit` times: up to
        8000 C presses the title never answers before a grind's loop
        gave up, the computer sitting at the title all the while."""
        for _ in range(limit):
            st = self.state
            if st.get("screen") in WIPED:
                return False
            if (st.get("screen") == "level" and st.get("kind") == "waiting"
                    and st.get("wait") == "control"):
                return True
            if st.get("screen") == "building":
                # Never C blind inside a store: that sells or buys
                # whatever the cursor is on. B backs out of everything.
                leave_building(self)
            elif st.get("screen") == "battle" or st.get("kind") == "waiting":
                self.press_through()
            else:
                self.send("wait idle")
        return False

    def fight(self):
        """One fight, FIGHT every round, from the battle screen back to
        the map. Returns the scripts shown (victory, EXP, level-ups)."""
        return self.press_through("level")

    def grind(self, walks, fights=5, shots=None, name="fight"):
        """Walk `walks` (a list of route walk commands) round and round;
        every battle that interrupts is fought with FIGHT. Stops after
        `fights` fights or when a walk ends somewhere unexpected.
        Returns per-fight script lists."""
        results = []
        i = 0
        while len(results) < fights:
            if not self.ready():
                break
            self.send(walks[i % len(walks)])
            i += 1
            if self.state.get("screen") == "battle":
                if shots is not None:
                    self.send(f"shot {name}_{len(results)}_in")
                results.append(self.fight())
        return results

    def close(self):
        self.p.stdin.close()
        if isinstance(self.p, _Attached):
            return              # the game's log pipe stays open: no EOF
        while True:
            line = self.q.get()
            if line is None:
                break
            self.events.append(line)
            if self.trace:
                print(line, file=self.trace)
        self.p.wait()


# ---------------------------------------------------------------------
# Finding the way: the level's own collision, as walk.c reads it.
# ---------------------------------------------------------------------

import heapq
import json
import os
import xml.etree.ElementTree as ET
from collections import deque

_LEVELS = None


def _levels():
    global _LEVELS
    if _LEVELS is None:
        _LEVELS = {l["index"]: l for l in json.loads(
            Path("assets/levels/levels.json").read_text())["levels"]}
    return _LEVELS


def map_index(value):
    """A map as a save names it -- a level's name in levels.json (build 104,
    any case) or, in older saves, its number -- as the level's index; None
    for a name no level has."""
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        for i, lv in _levels().items():
            if lv["name"].lower() == value.lower():
                return i
    return None


# (build 106) Names for the logs: a level, a script, a townsperson and an
# enemy type by what they are, never by number (docs/harness.md, "Names in
# the logs").

def level_name(index):
    """A level by the name levels.json gives it ("Paseo", "SkureB1")."""
    if index is None:
        return "no level (off the map)"
    lv = _levels().get(index) if isinstance(index, int) else None
    return lv["name"] if lv else f"level {index}"


_SCRIPT_NAMES = None


def _script_names():
    """Every script id's name: src/game/script_ids.h's SCRIPT_ name (less
    the prefix) where the game's code uses the id, else gen_script_ids'
    own rule -- the bank, then the line's first words -- with its _2, _3
    for a name already taken, the ids in order."""
    global _SCRIPT_NAMES
    if _SCRIPT_NAMES is not None:
        return _SCRIPT_NAMES
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import gen_script_ids                               # noqa: E402
    names = {}
    header = Path(__file__).resolve().parent.parent / "src/game/script_ids.h"
    if header.exists():
        for m in re.finditer(r"#define SCRIPT_(\w+)\s+0x([0-9A-F]{4})",
                             header.read_text()):
            names[int(m.group(2), 16)] = m.group(1)
    taken = set(names.values())
    scripts = gen_script_ids.load_scripts()
    for v in sorted(scripts):
        if v in names:
            continue
        bank = v >> 8
        base = (gen_script_ids.make_name(bank, scripts[v])
                or "%s_%02X" % (gen_script_ids.BANK_PREFIX[bank], v & 0xFF))
        n, k = base, 2
        while n in taken:
            n = "%s_%d" % (base, k)
            k += 1
        taken.add(n)
        names[v] = n
    _SCRIPT_NAMES = names
    return names


def script_name(script_id):
    """A script by name (ACTION_NOTHING_UNUSUAL_HERE); its id as $XXXX
    only when no block of window text has it."""
    return _script_names().get(script_id, f"${script_id:04X}")


def scripts_text(ids):
    """The scripts a window showed, by name, space-separated."""
    return " ".join(script_name(x) for x in (ids or []))


_PEOPLE = None


def person_name(level, index):
    """The townsperson whose dialogue number is `index` on `level`, as the
    kind of person and the cell they are placed at (level_sprites.json,
    npc_types.json): "MuskCat at 47,27"."""
    global _PEOPLE
    if _PEOPLE is None:
        kinds = {t["object"]: t["name"] for t in json.loads(
            Path("assets/data/npc_types.json").read_text())}
        _PEOPLE = {}
        for lv in json.loads(Path("assets/data/level_sprites.json")
                             .read_text())["levels"]:
            for o in lv.get("objects") or []:
                key = (lv["level"], o["index"])
                if o.get("interaction") == 1 and key not in _PEOPLE:
                    _PEOPLE[key] = (f"{kinds.get(o['object'], 'someone')} "
                                    f"at {o['x'] // 16},{o['y'] // 16}")
    return _PEOPLE.get((level, index),
                       f"the one with dialogue {index} on {level_name(level)}")


ENEMY_TYPE_NAME = {0x20: "living", 0x40: "robot", 0x60: "both"}


class LevelMap:
    """Cells of 16px on one level: which are solid and which special, on
    the collision plane walk_begin picks (plane B when bits 4-6 of the
    collision flags are set), and the plane-B block under each for the
    town edges ($53/$72, level type 2)."""

    def __init__(self, index):
        lv = _levels()[index]
        stem = f"{index:02x}_{lv['name']}"
        root = ET.parse(f"assets/levels/{stem}.tmx").getroot()
        self.planes = {}
        for layer in root.iter("layer"):
            data = layer.find("data").text.replace("\n", "").split(",")
            self.planes[layer.get("name")] = [int(x) - 1 for x in data if x]
        self.cols = lv["cols"]
        self.rows = lv["rows"]
        self.type = lv["tileset"]
        flags = lv.get("collision_flags", 0)
        self.coll = "planeB" if (flags & 0x70) else "planeA"
        if self.coll not in self.planes:
            self.coll = "planeB"
        self.attr = [0] * 256
        for line in Path(f"assets/levels/{stem}.collision.txt"
                         ).read_text().splitlines():
            if line.startswith("#"):
                continue
            b, solid, special = line.split()
            v = 0
            for q in range(4):
                v |= (1 << q) if solid[q] == "#" else 0
                v |= (0x10 << q) if special[q] == "#" else 0
            self.attr[int(b, 16)] = v
        self.w, self.h = self.cols * 2, self.rows * 2

    def block(self, plane, cx, cy):
        return self.planes[plane][((cy % self.h) >> 1) * self.cols
                                  + ((cx % self.w) >> 1)]

    def bits(self, cx, cy, plane=None):
        q = ((cy & 1) << 1) + (cx & 1)
        a = self.attr[self.block(plane or self.coll, cx, cy)]
        return bool(a & (1 << q)), bool(a & (0x10 << q))

    def open(self, cx, cy):
        return not self.bits(cx, cy)[0]

    def specials(self):
        """Open cells with the special bit: stairs, exits, holes."""
        if getattr(self, "_specials", None) is None:
            self._specials = {(cx, cy) for cy in range(self.h)
                              for cx in range(self.w)
                              if self.bits(cx, cy) == (False, True)}
        return self._specials

    def damage_floors(self):
        """Special open cells that only hurt (walk.c's dangerous_floor):
        type 3 block $63, type 5 blocks $1A/$46/$77/$78. Crossable."""
        if getattr(self, "_damage", None) is None:
            blocks = {3: (0x63,), 5: (0x1A, 0x46, 0x77, 0x78)}.get(
                self.type, ())
            self._damage = {c for c in self.specials()
                            if self.block(self.coll, *c) in blocks}
        return self._damage

    def town_edges(self):
        if getattr(self, "_edges", None) is not None:
            return self._edges
        out = set()
        self._edges = out
        if self.type != 2 or "planeB" not in self.planes:
            return out
        # check_underfoot reads plane B, but the step onto the cell is
        # walked on the collision plane: an edge under a wall there (all
        # of Kueri's west side) can never be stood on.
        for cy in range(self.h):
            for cx in range(self.w):
                if self.block("planeB", cx, cy) in (0x53, 0x72) \
                        and self.bits(cx, cy, "planeB")[1] \
                        and self.open(cx, cy):
                    out.add((cx, cy))
        return out

    def pieces(self):
        """(build 89) The level cut into what path() can cross as plain
        floor -- open, not a town edge, not special unless a damage
        floor -- joined across the level's wrap as path() joins it:
        {cell: piece number}. A cell not in it is solid, a town edge or a
        special cell, which path() only ever enters as a goal."""
        if getattr(self, "_pieces", None) is not None:
            return self._pieces
        edges = (self.town_edges() | self.specials()) - self.damage_floors()
        ids = {}
        n = 0
        for cy in range(self.h):
            for cx in range(self.w):
                c = (cx, cy)
                if c in ids or c in edges or not self.open(cx, cy):
                    continue
                ids[c] = n
                stack = [c]
                while stack:
                    x, y = stack.pop()
                    for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                        m = ((x + dx) % self.w, (y + dy) % self.h)
                        if m in ids or m in edges or not self.open(*m):
                            continue
                        ids[m] = n
                        stack.append(m)
                n += 1
        self._pieces = ids
        return ids

    def reaches(self, start, goals):
        """(build 89) `start in goals or path(start, goals) is not None`,
        from pieces() instead of a search: path() walks plain floor
        within one piece, and steps onto a goal from `start` or from any
        floor cell beside it."""
        if start in goals:
            return True
        ids = self.pieces()
        if start in ids:
            mine = {ids[start]}
        else:
            mine = set()
            for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                m = ((start[0] + dx) % self.w, (start[1] + dy) % self.h)
                if m in ids:
                    mine.add(ids[m])
        for g in goals:
            if ids.get(g, -1) in mine:
                return True
            for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                m = ((g[0] + dx) % self.w, (g[1] + dy) % self.h)
                if m == start or ids.get(m, -1) in mine:
                    return True
        return False

    def path(self, start, goals, avoid=()):
        """Cells from `start` to the nearest of `goals`, or None. A
        town's edges are only crossed when they are the goal: stepping
        onto one leaves for Motavia."""
        # A special open cell is never plain floor: a town's edge,
        # stairs, a hole. Only crossed as the goal. A damage floor is
        # crossed, at a cost (five steps' worth), since some levels --
        # the Biosystems Lab's basement -- are mostly that.
        damage = self.damage_floors()
        edges = (self.town_edges() | self.specials()) - set(goals) - damage
        prev = {start: None}
        cost = {start: 0}
        q = [(0, start)]
        while q:
            k, c = heapq.heappop(q)
            if k > cost[c]:
                continue
            if c in goals and c != start:
                out = []
                while prev[c] is not None:
                    out.append(c)
                    c = prev[c]
                return out[::-1]
            for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                n = ((c[0] + dx) % self.w, (c[1] + dy) % self.h)
                if n in avoid or n in edges:
                    continue
                if not (self.open(*n) or n in goals):
                    continue
                nk = k + (5 if n in damage else 1)
                if nk < cost.get(n, 1 << 30):
                    cost[n] = nk
                    prev[n] = c
                    heapq.heappush(q, (nk, n))
        return None


def _runs(start, cells, w, h):
    out, cur = [], start
    for c in cells:
        d = ("right" if c[0] == (cur[0] + 1) % w else
             "left" if c[0] == (cur[0] - 1) % w else
             "down" if c[1] == (cur[1] + 1) % h else "up")
        if out and out[-1][0] == d:
            out[-1][1] += 1
        else:
            out.append([d, 1])
        cur = c
    return out


def goto(s, goals, fights=None, tries=30, log=None, keep_off=None,
         after_fight=None):
    """Walk to any cell in `goals` on the current level, re-planning
    after a fight or a person in the way. Returns True when a goal is
    reached or the level changes. (build 102) `keep_off`: cells the plan
    never enters (roam: the stronger encounter regions); `after_fight`:
    called with the session after each fight won on this level, before
    the mending below (roam: Res). Both None by default: every other
    caller walks as before."""
    start_level = s.state.get("level")
    if start_level is None:
        # (build 98) Not on a map -- a fight, a fade: nothing to walk on.
        # Before, level_map(None) raised KeyError.
        s.why = f"not on a map ({s.state.get('screen')})"
        return False
    lm = level_map(start_level)         # the Prism's blocks (build 89)
    avoid = set()
    s.why = None
    # (build 89) hurt is this walk's finding, not the party's for good:
    # nothing cleared it, so after one retreat every later goto -- the
    # party mended and full, on DezolisSkure by Aukba's gate -- gave up.
    s.hurt = False
    for _ in range(tries):
        st = s.state
        if st.get("level") != start_level:
            return True
        cur = (st["x"], st["y"])
        if cur in goals:
            return True
        cells = lm.path(cur, goals, avoid | keep_off if keep_off else avoid)
        if cells is None:
            s.why = (f"no path on {level_name(start_level)} from {cur} to "
                     f"{sorted(goals)[:4]}" + (f" avoiding {sorted(avoid)}"
                                              if avoid else ""))
            return False
        pos = cur
        for d, n in _runs(cur, cells, lm.w, lm.h):
            if not s.ready():
                return False
            step = {"up": (0, -1), "down": (0, 1), "left": (-1, 0),
                    "right": (1, 0)}[d]
            pos = ((pos[0] + step[0] * n) % lm.w, (pos[1] + step[1] * n) % lm.h)
            out = s.send(f"walk {d} {n}")
            if s.state.get("kind") != "waiting" \
                    and s.state.get("screen") == "level":
                s.send("wait idle")         # a fade into a fight or a door
            st = s.state
            if st.get("screen") == "battle":
                # s.fight_opts: a caller's own policy for the fights on
                # the way (Gaira's, in prison clothes: RUN).
                r = fight_smart(s, **(getattr(s, "fight_opts", None) or {}))
                if fights is not None:
                    fights.append(r)
                if after_fight is not None \
                        and s.state.get("screen") == "level":
                    after_fight(s)
                if getattr(s, "walk_mend", None) \
                        and s.state.get("screen") == "level":
                    revive(s)
                    cure_poison(s)
                    mend(s, below=s.walk_mend)
                if needs_healing(s) and s.state.get("screen") == "level":
                    # (build 89) needs_healing counts poison and mend()
                    # does not cure it: Menobe's walks all gave up with
                    # Anna poisoned and full, two Antidotes in Rolf's bag.
                    cure_poison(s)
                    mend(s)
                    if needs_healing(s) and getattr(s, "careful", False):
                        s.hurt = True           # nothing left to mend with
                        s.why = "hurt with nothing left to mend with"
                        return False
                break
            if st.get("level") != start_level:
                return True
            if any("FAIL" in ln and "blocked" in ln for ln in out):
                # someone standing there: go round them next time
                m = re.search(r"at cell (\d+),(\d+)", " ".join(out))
                nxt = {"up": (0, -1), "down": (0, 1), "left": (-1, 0),
                       "right": (1, 0)}[d]
                avoid.add(((st["x"] + nxt[0]) % lm.w,
                           (st["y"] + nxt[1]) % lm.h))
                break
            if st.get("kind") != "waiting" or st.get("wait") != "control":
                s.press_through()
                break
            if (st.get("x"), st.get("y")) != pos:
                # The walk ended short with the pad handed back at once --
                # a damage floor takes it for a few frames (Gaira has
                # fourteen) -- and the rest of this plan starts from a
                # cell the party is not on: plan again from here. Before
                # build 83 the next run went on from the wrong cell and
                # stood blocked for 120 ticks, over and over.
                break
    ok = s.state.get("level") != start_level or \
        (s.state.get("x"), s.state.get("y")) in goals
    if not ok and s.why is None:
        s.why = (f"gave up after {tries} tries at "
                 f"{(s.state.get('x'), s.state.get('y'))}"
                 + (f" avoiding {sorted(avoid)}" if avoid else ""))
    return ok


def door_cells(level, ry, rx):
    """Cells to stand on for a record at (ry, rx). interact() matches a
    record whose (y, x) is within two cells down-right of the cell it is
    given: the cell underfoot for a special quadrant that is not solid
    (loc_6F56 -- stairs), or the cell pushed into for one that is solid
    as well (Sprites_ChkCanMove, loc_6DB2 -- a door, and in a town only
    walking up). So the answer is every special open cell of the 3x3
    up-left of the record, and the open cell below every special solid
    one, from which `walk up 1` pushes in."""
    lm = level_map(level)
    out = {}
    for cy in range(ry - 2, ry + 1):
        for cx in range(rx - 2, rx + 1):
            solid, special = lm.bits(cx, cy)
            if special and not solid:
                out[(cx, cy)] = None
            elif special and solid:
                sides = [("up", 0, 1)]
                if lm.type != 2:
                    sides += [("down", 0, -1), ("right", -1, 0),
                              ("left", 1, 0)]
                for d, dx, dy in sides:
                    n = ((cx + dx) % lm.w, (cy + dy) % lm.h)
                    if lm.open(*n) and n not in out:
                        out[n] = d
    return out


# ---------------------------------------------------------------------
# Buildings: the hospital, the shops, Data Memory. Each is a policy on
# the `state` line -- C through text, a stop on each menu by its window
# id and the last script shown -- run by visit() until the street.
# ---------------------------------------------------------------------

# Window ids (window.h) the policies key on.
WIN_YESNO       = 0x09
WIN_YESNO2      = 0x1F
WIN_YESNO3      = 0x20
WIN_BUYSELL     = 0x22
WIN_NAME_INPUT  = 0x24
WIN_SAVE_SLOTS  = 0x25
WIN_HEAL_CURE   = 0x27
WIN_STORE_STOCK = 0x28
WIN_STORE_CHARS = 0x5B
WIN_BATTLE_OPTS = 0x60      # FIGHT / STGY
WIN_BATTLE_OPTS2 = 0x61     # ORDR / RUN

# Paseo's doors (assets/levels/04_Paseo.doors.json), by building.
PASEO = 4
PASEO_DOORS = {
    "rolf_house": (17, 45), "data_memory": (7, 17), "clone_lab": (27, 7),
    "hospital": (21, 15), "weapon_store": (5, 58), "armor_store": (27, 58),
    "item_store": (17, 58), "tower": (15, 32), "teleport": (17, 7),
}


def level_doors(level):
    """Every door record of a level, as {(y, x): record}."""
    lv = _levels()[level]
    stem = f"{level:02x}_{lv['name']}"
    d = json.loads(Path(f"assets/levels/{stem}.doors.json").read_text())
    return {(e["y"], e["x"]): e for e in d["entries"]}


def menu_pick(s, stop):
    """Walk the live cursor to `stop` a press at a time and press C.
    False when nothing readable holds the pad."""
    for _ in range(24):
        st = s.state
        if st.get("wait") != "menu" or not st.get("menu_stops"):
            return False
        cur = st["menu_stop"]
        if cur == stop:
            s.send("press c")
            return True
        if st["menu_across"]:
            d = "right" if cur < stop else "left"
        else:
            d = "down" if cur < stop else "up"
        s.send(f"press {d}")
    return False


# routes/new_game.route's way over the stock board from A: ROLF, then END.
NAME_ROLF = ("press down",) + ("press right",) * 8 + ("press c",) \
    + ("press left",) * 3 + ("press c",) + ("press left",) * 3 \
    + ("press c", "press up") + ("press right",) * 3 \
    + ("press c", "press up", "press right", "wait idle", "press c")


def language_is(s, key):
    """(build 168f; 168e's japanese_board) Whether LANGUAGE is `key`
    (lang.json's: us, retranslation, ja), asked with the probe's `expect
    setting language`, no FAIL being yes. No tick passes."""
    out = s.send(f"expect setting language {key}")
    return not any("FAIL" in ln for ln in out)


def japanese_board(s):
    """(build 168e) Whether the name board is the Japanese cartridge's
    (build 165: six rows of katakana, every cell a stop, オワル at the
    last row's end) and not the stock one ENGLISH 1989 and the
    retranslation share."""
    return language_is(s, "ja")


def type_end(s):
    """The name board, cursor on its first letter; C ends a blank name,
    which Data Memory keeps as the slot's old one. The stock board, from
    A: up wraps to the bottom row (ADV), right skips to RUB, right again
    to END (name_move). (build 168e) The Japanese board, from ア: up to
    the last row's first cell, left round to オワル; until 168e the stock
    moves typed a letter there and the board stayed up."""
    if japanese_board(s):
        s.send("press up", "press left", "press c")
    else:
        s.send("press up", "press right", "press right", "press c")


def leave_building(s, limit=40):
    """B and C until the party is back on the street under the pad."""
    for _ in range(limit):
        st = s.state
        if st.get("screen") == "level" and st.get("kind") == "waiting" \
                and st.get("wait") == "control":
            return True
        if st.get("screen") not in ("building", "level"):
            return False
        if st.get("kind") == "waiting":
            s.send("press b" if st.get("wait") == "menu" else "press c",
                   "wait idle")
        else:
            s.send("wait idle")
    return False


def visit(s, level, door, policy, limit=80):
    """Walk onto the door's cells, then play the building by `policy`
    (a function of the session returning a menu stop, "b", or None for
    C) until the street. Returns the scripts shown inside."""
    cells = door_cells(level, *door)
    if not goto(s, cells):
        return None
    before = len(s.shown)
    if s.state.get("level") == level and s.ready():
        d = cells.get((s.state["x"], s.state["y"])) if hasattr(cells, "get") else "up"
        s.send(f"walk {d or 'up'} 1")           # into the door
    for _ in range(limit):
        st = s.state
        if st.get("screen") == "level":
            if st.get("kind") == "waiting" and st.get("wait") == "control":
                break
            if st.get("kind") == "waiting":
                s.send("press c")
            else:
                s.send("wait idle")
            continue
        if st.get("screen") == "battle":
            s.fight()
            continue
        if st.get("kind") != "waiting":
            s.send("wait idle")
            continue
        if st.get("wait") == "menu":
            if st.get("menu") == WIN_NAME_INPUT:
                type_end(s)
                continue
            pick = policy(s)
            if pick == "b":
                s.send("press b")
            elif pick is None:
                s.send("press c")
            else:
                menu_pick(s, pick)
            continue
        s.send("press c")
    return s.shown[before:]


def hospital_policy(s):
    st, last = s.state, s.last_script
    if st["menu"] == WIN_HEAL_CURE:
        return 0                                # HEAL
    if st["menu"] == WIN_YESNO2:
        return 0 if last == 0x0503 else 1       # pay; nobody else
    return None


def shop_policy(purchases):
    """purchases: [(stock index, party member), ...], bought in order.
    Works for the item store (bank $8) and both equipment stores ($6,
    $7): the same windows, and the yes/no is answered by the script that
    opened it."""
    todo = list(purchases)

    def policy(s):
        st, last = s.state, s.last_script
        low = last & 0xFF
        if st["menu"] == WIN_BUYSELL:
            return 0                            # BUY
        if st["menu"] == WIN_STORE_STOCK:
            return todo[0][0] if todo else "b"
        if st["menu"] == WIN_STORE_CHARS:
            return todo[0][1] if todo else "b"
        if st["menu"] == WIN_YESNO2:
            if last == 0x080A or low == 0x07 and last >> 8 in (6, 7):
                todo.pop(0)                     # bought
                return 0 if todo else 1
            if low == 0x03 and last >> 8 in (6, 7):
                return 1                        # "are you joking?": no
            return 1
        return None
    return policy


def save_policy(slot):
    def policy(s):
        st, last = s.state, s.last_script
        if st["menu"] == WIN_YESNO3:
            if last == 0x0303:
                return 0                        # save: yes
            if last == 0x0306:
                return 0                        # overwrite: yes
            return 0                            # more adventure: yes
        if st["menu"] == WIN_SAVE_SLOTS:
            return slot - 1
        return None
    return policy


def heal(s, level=PASEO, door=PASEO_DOORS["hospital"]):
    shown = visit(s, level, door, hospital_policy)
    cure_poisoned(s, level, door)               # build 89
    return shown


def buy(s, door, purchases, level=PASEO):
    return visit(s, level, door, shop_policy(purchases),
                 limit=40 + 12 * len(purchases))


def save(s, slot=1, level=PASEO, door=PASEO_DOORS["data_memory"]):
    """Data Memory. Its save (loc_B5A4) also writes $C646-$C64A, the
    level and cell a Telepipe or Ryuka goes back to on Motavia
    (ItemAction_Telepipe, loc_9E86), so this town is where the next
    retreat by pipe lands."""
    rolf_alive_for_shops(s, level)
    shown = visit(s, level, door, save_policy(slot))
    if shown is not None and 0x0307 in shown:
        s.save_town = level
        s.looted = []                   # (build 106) the save keeps them
    return shown


def needs_healing(s, frac=0.5):
    """Anyone down, poisoned or under `frac` of their HP -- leaving out
    the names in s.expendable: a member carried dead on purpose (Nei
    before Climatrol, whose share of the EXP is lost with her: loc_F2A4
    splits by the living)."""
    skip = getattr(s, "expendable", ())
    p = [m for m in (s.state.get("party") or []) if m["name"] not in skip]
    return any(m["dead"] or m["poison"] or m["hp"] < m["max_hp"] * frac
               for m in p)


WIN_BATTLE_COMMANDS  = 0x63     # ATTACK TECH ITEM DEFEND, across
WIN_BATTLE_ITEM_LIST = 0x67
CMD_ITEM = 2


def battle_stop(s, pos):
    """The triangle cursor's stop for party position `pos` in battle.
    loc_EBF8 and loc_EE00 read Battle_CharacterIdIndex (ps2.asm) --
    4, 0, 2, 6: byte offsets into party_member_id, the third member
    farthest left -- one byte on when party_members_num is under 2. So
    with three or four members stop 0 is position 2, stop 1 position 0,
    stop 2 position 1, stop 3 position 3; with one or two the stops are
    the positions. Before build 79 battle_order_item picked stop `pos`,
    which in a full party opened Rudo's bag for Rolf's order and used
    whatever sat at Rolf's slot number there -- or failed."""
    n = len(s.state.get("party") or [])
    if n <= 2:
        return pos
    return {0: 1, 1: 2, 2: 0, 3: 3}[pos]


def battle_order_item(s, holder, slot, target):
    """From FIGHT/STGY: STGY, ORDR, party member `holder`, ITEM, bag
    slot `slot`, on party member `target`; ends back at FIGHT/STGY with
    that command stored for the round. False if the menus did not go
    that way."""
    st = s.state
    if st.get("menu") != WIN_BATTLE_OPTS:
        return False
    menu_pick(s, 1)                             # STGY
    s.send("wait idle")
    if s.state.get("menu") != WIN_BATTLE_OPTS2:
        return False
    menu_pick(s, 0)                             # ORDR
    s.send("wait idle")
    if not s.state.get("menu_party"):
        return False
    menu_pick(s, battle_stop(s, holder))
    s.send("wait idle")
    if s.state.get("menu") != WIN_BATTLE_COMMANDS:
        return False
    menu_pick(s, CMD_ITEM)
    s.send("wait idle")
    if s.state.get("menu") != WIN_BATTLE_ITEM_LIST:
        return False
    if not battle_list_pick(s, slot):
        menu_escape_battle(s)
        return False
    s.send("wait idle")
    if s.state.get("menu_party"):
        menu_pick(s, battle_stop(s, target))
        s.send("wait idle")
    return s.state.get("menu") == WIN_BATTLE_OPTS


WIN_ENEMY_GROUPS     = 0x64
WIN_BATTLE_TECH_LIST = 0x66
CMD_ATTACK, CMD_TECH, CMD_DEFEND = 0, 1, 3

# Battle techniques that mend one member (TechniqueData+6 = $19: one of
# the party), most first, and the HP each is worth planning on -- the
# ratio of a heal to the deficit is all the choice uses.
HEALS = {"NARES": 120, "GIRES": 60, "RES": 20}


def techs(s):
    """The probe's `techs` line, by party position: {"name", "cmd",
    "techs": [(NAME, tp, flags), ...]} in battle-list order. cmd is the
    command stored for them -- attack, tech, item, defend -- which lasts
    from fight to fight (loc_F4F8 puts only a used technique back to
    attack; CharStartCommandsArray starts Amy, Hugh and Shir on defend)."""
    for ln in s.send("techs"):
        m = re.search(r"ok\s+techs (.*)$", ln)
        if not m:
            continue
        out = []
        for part in m.group(1).split("; "):
            who, _, rest = part.partition(": ")
            words = rest.split()
            ts = []
            for w in words[1:]:
                name, tp, flags = w.split(":")
                ts.append((name, int(tp), int(flags.lstrip("$"), 16)))
            out.append({"name": who, "cmd": words[0] if words else "",
                        "techs": ts})
        return out
    return []


def battle_order(s, holder, cmd, slot=None, target=None, group=0):
    """From FIGHT/STGY: STGY, ORDR, party member `holder`, command `cmd`
    (CMD_ATTACK, CMD_TECH, CMD_ITEM, CMD_DEFEND), list slot `slot` for a
    technique or item, party member `target` when the cursor asks for
    one, enemy group `group` (0 the first, 1 the second: Win_EnemyGroups'
    stops, window.c) when the enemy groups box does. Ends back at
    FIGHT/STGY with the command stored. False if the menus did not go
    that way (backed out of)."""
    if s.state.get("menu") != WIN_BATTLE_OPTS:
        return False
    menu_pick(s, 1)                             # STGY
    s.send("wait idle")
    if s.state.get("menu") != WIN_BATTLE_OPTS2:
        menu_escape_battle(s)
        return False
    menu_pick(s, 0)                             # ORDR
    s.send("wait idle")
    if not s.state.get("menu_party"):
        menu_escape_battle(s)
        return False
    menu_pick(s, battle_stop(s, holder))
    s.send("wait idle")
    if s.state.get("menu") != WIN_BATTLE_COMMANDS:
        menu_escape_battle(s)
        return False
    menu_pick(s, cmd)
    s.send("wait idle")
    if cmd in (CMD_TECH, CMD_ITEM):
        want = WIN_BATTLE_TECH_LIST if cmd == CMD_TECH else WIN_BATTLE_ITEM_LIST
        if s.state.get("menu") != want or not battle_list_pick(s, slot):
            menu_escape_battle(s)
            return False
        s.send("wait idle")
    if s.state.get("menu_party") and target is not None:
        menu_pick(s, battle_stop(s, target))
        s.send("wait idle")
    if s.state.get("menu") == WIN_ENEMY_GROUPS:
        menu_pick(s, group)
        s.send("wait idle")
    if s.state.get("menu") != WIN_BATTLE_OPTS:
        menu_escape_battle(s)
        return False
    return True


def plan_heal(party, known, heal_below):
    """(healer, list slot, target) for the member furthest under
    `heal_below` of their HP, by a living member who has the TP for a
    mending technique: the smallest that covers the deficit, else the
    largest. None when nobody needs it or can."""
    hurt = [(m["hp"] / m["max_hp"], i) for i, m in enumerate(party)
            if not m["dead"] and m["max_hp"]]
    if not hurt:
        return None
    frac, target = min(hurt)
    if frac >= heal_below:
        return None
    deficit = party[target]["max_hp"] - party[target]["hp"]
    best = None
    for h, m in enumerate(party):
        if m["dead"] or h >= len(known):
            continue
        for slot, (name, tp, flags) in enumerate(known[h]["techs"]):
            if name not in HEALS or m["tp"] < tp:
                continue
            worth = HEALS[name]
            # Prefer one that covers the deficit at the least TP, else the
            # biggest there is.
            key = (0, tp) if worth >= deficit else (1, -worth)
            if best is None or key < best[0]:
                best = (key, (h, slot, target))
    return best[1] if best else None


def battle_list_pick(s, slot):
    """Bag slot `slot` in the battle's item list (Win_BattleItemList,
    battle_list_cursor): four to a page. Under five items the stops are
    the items; five or more put NEXT at stop 0 and the page's items at
    stops 1-4, the list opening on page 0 (CommandAction_Item zeroes
    item_page) and NEXT stepping four, wrapping. Before build 74 this
    asked menu_pick for stop `slot` on a five-stop list, so a mate in
    slot 5 or up sent the cursor round for thousands of presses and the
    fight never went on."""
    stops = s.state.get("menu_stops") or 0
    window = s.state.get("menu")        # the item list, or (build 80) the
                                        # technique list, which pages alike
    if stops < 5 and slot < stops:
        return menu_pick(s, slot)
    if stops != 5 and slot >= stops:
        return False
    for _ in range(slot // 4):
        if not menu_pick(s, 0):                 # NEXT
            return False
        s.send("wait idle")
        if s.state.get("menu") != window:
            return False
    stops = s.state.get("menu_stops") or 0
    if slot % 4 + 1 >= stops:
        return False
    return menu_pick(s, slot % 4 + 1)


def menu_escape_battle(s, limit=6):
    """B out of the battle's lists back to FIGHT/STGY."""
    for _ in range(limit):
        if s.state.get("menu") == WIN_BATTLE_OPTS \
                or s.state.get("screen") != "battle":
            return True
        s.send("press b", "wait idle")
    return False


def give_round(s, item, n, positions):
    """`n` of `item` dealt round-robin into the bags of the party
    positions given (the probe's `give ITEM POS` puts one in one bag).
    Returns how many landed: a full bag is skipped."""
    given = 0
    full = set()
    i = 0
    while given < n and len(full) < len(positions):
        pos = positions[i % len(positions)]
        i += 1
        if pos in full:
            continue
        out = s.send(f"give {item} {pos}")
        if any(" ok " in ln and "into position" in ln for ln in out):
            given += 1
        else:
            full.add(pos)
    return given


def battle_find_mate(s):
    """(holder, slot) of a mate a living member carries, or None."""
    party = s.state.get("party") or []
    for holder, bag in enumerate(bags(s)):
        if holder < len(party) and party[holder]["dead"]:
            continue
        for slot, (name, worn) in enumerate(bag):
            if name in MATES and not worn:
                return (holder, slot)
    return None


SCRIPT_VICTORY = 0x1211     # "<leader> and party are victorious!"


def fight_smart(s, run_below=0.25, mate_below=0.45, limit=200,
                stop_on_level=False, heal_below=0.5, use_techs=True,
                type_techs=None, boss_techs=True, deban=True, shift=False,
                reset_techs=True, stop_on_gameover=False):
    """One fight: FIGHT each round, with orders placed first. Anyone
    left on DEFEND is ordered to ATTACK (CharStartCommandsArray puts Amy,
    Hugh and Shir there when they join, and it lasts: before build 80
    they defended through every fight the harness played). Whoever is
    furthest under `heal_below` gets a mending technique (RES, GIRES,
    NARES -- plan_heal) from a member with the TP, or failing that a mate
    under `mate_below` when someone carries one. RUN (STGY, RUN) once
    anyone alive is below `run_below` and there is still a lot of enemy
    to get through. C through the rest. Returns the scripts shown.

    `type_techs` (default TYPE_TECHS: Kain and Hugh, build 88): those
    members are ordered a damage technique each round when one that
    works on a live group's type (loc_27AA) beats their weapon by enough
    -- plan_type_tech.

    Against a boss (an enemy that cannot be run from, build 94), with
    `boss_techs`: every member may be ordered one, at the lower bar
    BOSS_TECH_MARGIN / BOSS_TECH_GAIN_PER_TP -- there is no way out to
    keep TP for -- and none whose techniques Dark Force's curse sealed.
    With `deban`, a member who knows DEBAN casts it while half or more
    of the living party is without it: it halves the boss's blows and
    Dark Force's party-wide blast (loc_2D08) and lasts the fight. With
    `shift`, a member who knows SHIFT casts it on themself up to
    SHIFT_CASTS times a boss fight: +20 attack, dexterity and agility
    each, for the fight (the agility also moves them off the bottom of
    the order Dark Force's curse picks from).

    A technique command stays stored and is cast again every round
    until the caster cannot pay for it (loc_251C) -- a heal on the same
    member, a second Deban, Shift after Shift. With `reset_techs`
    (build 94) anyone on a technique not planned for them this round is
    put back to ATTACK; before, they went on casting it."""
    if type_techs is None:
        type_techs = TYPE_TECHS
    before = len(s.shown)
    orders_before = len(s.tech_orders)
    entry = None
    ordered = False
    known = techs(s) if use_techs else []
    # What the fight did to each member, from every state seen in it
    # (build 94): TP at its first and last, the curse kinds seen (a kind
    # shows from the curse to the member's next turn; 1-3 stay), sealed.
    tp_first, last_party, curses, sealed = {}, None, {}, set()
    for _ in range(limit):
        st = s.state
        if st.get("screen") == "battle" and st.get("party"):
            last_party = st["party"]
            for m in st["party"]:
                tp_first.setdefault(m["name"], m["tp"])
                if m.get("curse"):
                    curses.setdefault(m["name"], set()).add(m["curse"])
                if m.get("sealed"):
                    sealed.add(m["name"])
        if st.get("screen") == "battle" and st.get("formation") \
                and entry is None:
            entry = (st["tick"], st["formation"], list(st["enemies"]))
        if st.get("screen") != "battle":
            if st.get("screen") == "level" and st.get("kind") == "waiting" \
                    and st.get("wait") == "control":
                break
            # A scene after the fight (Neifirst's): hand back on the level
            # screen rather than pressing through what it says.
            if stop_on_level and entry is not None \
                    and st.get("screen") == "level":
                break
            # A lost fight: hand back on the game over screen rather than
            # pressing on through the title (build 94, boss_trials).
            if stop_on_gameover and entry is not None \
                    and st.get("screen") == "gameover":
                break
            if st.get("kind") == "waiting":
                s.send("press c")
            else:
                s.send("wait idle")
            continue
        if st.get("kind") != "waiting":
            ordered = False
            s.send(s.fight_wait())
            if s.state.get("kind") != "waiting" \
                    and s.state.get("screen") == "battle" and s.may_hurry():
                s.send("press c")           # interrupt the repeat
            continue
        if st.get("wait") == "menu" and st.get("menu") == WIN_BATTLE_OPTS:
            alive = [(i, m) for i, m in enumerate(st["party"]) if not m["dead"]]
            fracs = [(m["hp"] / m["max_hp"], i) for i, m in alive]
            worst, worst_i = min(fracs, default=(1, 0))
            left = sum(e[1] for e in st.get("enemies") or [])
            if use_techs and not ordered:
                ordered = True
                known = techs(s) or known
                for i, m in enumerate(known):
                    if i < len(st["party"]) and not st["party"][i]["dead"] \
                            and m["cmd"] == "defend":
                        battle_order(s, i, CMD_ATTACK)
                heal = plan_heal(st["party"], known, heal_below)
                vs_boss = is_boss_fight(st)
                boss = boss_techs and vs_boss
                shield = plan_deban(st["party"], known) \
                    if vs_boss and deban else None
                if shield and heal and shield[0] == heal[0]:
                    shield = None
                if shield and battle_order(s, shield[0], CMD_TECH, shield[1]):
                    s.tech_orders.append((known[shield[0]]["name"], "DEBAN",
                                          st.get("formation")))
                    known = techs(s) or known
                busy = {x[0] for x in (heal, shield) if x}
                if vs_boss and shift:
                    done = [who for (who, t, f) in s.tech_orders[orders_before:]
                            if t == "SHIFT"]
                    for h, slot in plan_shift(st["party"], known, done, busy):
                        if battle_order(s, h, CMD_TECH, slot):
                            s.tech_orders.append((known[h]["name"], "SHIFT",
                                                  st.get("formation")))
                            known = techs(s) or known
                            busy.add(h)
                for i, m in enumerate(known):
                    if i >= len(st["party"]) or st["party"][i]["dead"] \
                            or st["party"][i].get("sealed") \
                            or i in busy:
                        continue
                    if not boss and m["name"] not in type_techs:
                        continue
                    pick = plan_type_tech(s, i, m, st, boss=boss)
                    if pick and battle_order(s, i, CMD_TECH, pick[0],
                                             group=pick[1]):
                        s.tech_orders.append((m["name"], pick[2],
                                              st.get("formation")))
                        known = techs(s) or known
                        busy.add(i)
                if heal:
                    busy.add(heal[0])
                for i, m in enumerate(known):
                    if reset_techs and i < len(st["party"]) \
                            and not st["party"][i]["dead"] \
                            and m["cmd"] == "tech" and i not in busy:
                        battle_order(s, i, CMD_ATTACK)
                        known = techs(s) or known
                if heal:
                    battle_order(s, heal[0], CMD_TECH, heal[1], heal[2])
                    known = techs(s) or known
                    continue
                ordered = False
            if worst < mate_below and not ordered:
                mate = battle_find_mate(s)
                if mate:
                    # Once a round either way: a failed order is backed
                    # out of rather than tried again until it works.
                    ordered = True
                    if not battle_order_item(s, mate[0], mate[1], worst_i):
                        menu_escape_battle(s)
                    continue
            # Run when it is going badly and there is still a lot of
            # enemy to get through; a fight nearly won is finished. A
            # run_below over 1 is "always run" (Gaira, no weapons): there a
            # 20-HP enemy counted as nearly won, and bare hands fought it
            # to fight_smart's limit (build 98, the playthrough's seed 3).
            if worst < run_below and (run_below > 1.0
                                      or left > 12 * max(1, len(alive))):
                menu_pick(s, 1)                 # STGY
            else:
                menu_pick(s, 0)                 # FIGHT
            continue
        if st.get("wait") == "menu" and st.get("menu") == WIN_BATTLE_OPTS2:
            menu_pick(s, 1)                     # RUN
            continue
        s.answer_page()
    shown = s.shown[before:]
    # (build 101) The victory's pages: from "... are victorious!" ($1211,
    # loc_F27E) to the map.
    won = None
    if SCRIPT_VICTORY in shown:
        won = s.shown_ticks[before + shown.index(SCRIPT_VICTORY)]
    if entry is not None:
        s.fights.append({"tick": entry[0], "formation": entry[1],
                         "enemies": entry[2], "ticks": s.state["tick"] - entry[0],
                         # (build 101) from the last enemy gone to the map
                         "victory_ticks": (s.state["tick"] - won)
                         if won is not None else None,
                         "scripts": shown, "party": s.state.get("party"),
                         "members": {
                             m["name"]: {
                                 "tp_first": tp_first.get(m["name"]),
                                 "tp_last": m["tp"], "max_tp": m["max_tp"],
                                 "dead": m["dead"],
                                 "curses": sorted(curses.get(m["name"], ())),
                                 "sealed": m["name"] in sealed,
                                 "orders": [t for (who, t, f)
                                            in s.tech_orders[orders_before:]
                                            if who == m["name"]]}
                             for m in (last_party or [])}})
    return shown



# ---------------------------------------------------------------------
# Techniques by the enemy's type (build 88)
# ---------------------------------------------------------------------
#
# ProcessTechnique (ps2.asm ~2940) keeps TechniqueData byte 7 & $60 in
# $FFFFF73A; loc_8D4 (667) copies EnemyData byte 43, the type, into 1(a6)
# of each enemy's stats; loc_27AA ANDs the two and, if nothing is left,
# shows $1204 ("Didn't work") with a hit rate of 0. Every damage and
# status technique goes through it (TechAction_Foi, _Zan, loc_2148 for
# _Gra, loc_223C, _Rimit, _Shinb). A rate byte of $FF is 500 and fails
# outright when the battle's escape rate is 0 (the bosses). Bit $20 is a
# biomonster, $40 a robot; Kain's own techniques are $Dx (robots only),
# Hugh's $Bx (biomonsters only), Foi/Zan/Gra and their kin $Fx (both).
# Dezolis's dungeon monsters from Wrestler on, Noah's and Dark Force
# carry both bits.

TYPE_BIO, TYPE_ROBOT, TYPE_BOTH = 0x20, 0x40, 0x60
TYPE_TECHS = ("Kain", "Hugh")
# A technique must beat the weapon by this factor, and gain this much
# damage for each TP, before it is ordered: TP spent on a blow is TP not
# there for RES on the way out.
TECH_MARGIN = 1.25
TECH_GAIN_PER_TP = 2.0
# Against a boss (build 94): the fight is to the end, so TP not spent is
# TP wasted -- a technique need only beat the weapon, and the TP still
# has to buy something.
BOSS_TECH_MARGIN = 1.0
BOSS_TECH_GAIN_PER_TP = 0.5
_BATTLE = None
_TECH_BY_NAME = None


def battle_data():
    global _BATTLE
    if _BATTLE is None:
        _BATTLE = json.loads(Path("assets/data/battle.json").read_text())
        _BATTLE["by_id"] = {e["id"]: e for e in _BATTLE["enemies"]}
    return _BATTLE


def enemy(eid):
    return battle_data()["by_id"].get(eid)


def formation_of(f):
    """EnemyBattleFormationData's record for formation id `f`: entry 0
    is formation 1 (loc_796's subq), the four bosses $100-$103 follow
    the 255."""
    forms = battle_data()["formations"]
    i = f - 1 if f < 0x100 else 255 + (f - 0x100)
    return forms[i] if 0 <= i < len(forms) else None


def tech_record(name):
    """TechniqueData by the battle list's NAME (upper case), with the
    hit rate from TechniqueProperties' first byte (loc_1FDA reads it
    into d2)."""
    global _TECH_BY_NAME
    if _TECH_BY_NAME is None:
        props = battle_data()["tech_props"]
        _TECH_BY_NAME = {}
        for t in _techs()["techniques"]:
            if not t["id"]:
                continue
            rec = dict(t)
            i = t["id"] - 1
            rec["hit"] = props[i]["bytes"][0] if i < len(props) else 0
            _TECH_BY_NAME[t["name"]] = rec
            _TECH_BY_NAME[t["key"]] = rec
    return _TECH_BY_NAME.get(name)


def tech_damage(rec):
    """CalculateTechniqueDamage's mean: (rand&31 + 84) * rate / 100, the
    rate $FF counting as 500."""
    rate = 500 if rec["rate"] == 0xFF else rec["rate"]
    return 99.5 * rate / 100


def live_groups(st):
    """[(group, enemy id, type, [hp of each live one])] for the fight in
    `st`: the state's enemies carry their group's enemy id, and the
    formation says which is the first group."""
    form = formation_of(st.get("formation") or 0)
    if not form:
        return []
    out = []
    for g, eid in enumerate((form["enemy1"], form["enemy2"])):
        if not eid:
            continue
        hps = [hp for (i, hp, mx) in st.get("enemies") or []
               if i == eid and hp > 0]
        if hps:
            e = enemy(eid) or {}
            out.append((g, eid, e.get("type", 0), hps))
    return out


def weapon_blow(s, member, name, level, hps, defense):
    """What `member` (party position) does to one of `hps` with a FIGHT:
    CalculateAttackDamage's mean, (rand&31 + 84 + attack) * (weapon + 2)
    / (defense * 5 + 100), attack the level table's (+$1A, before
    equipment), for each weapon worn, at a 90% hit, capped at the
    mean HP left."""
    cache = getattr(s, "_worn", None)
    if cache is None or cache[0] != len(s.fights):
        cache = (len(s.fights), bags(s))
        s._worn = cache
    worn = cache[1][member] if member < len(cache[1]) else []
    atk = expected_stats(name, level)["attack"]
    cap = sum(hps) / len(hps)
    total = 0.0
    for item, on in worn:
        w = items().get(item, {}).get("attack", 0)
        if on and w:
            total += min(cap, (99.5 + atk) * (w + 2) / (defense * 5 + 100))
    return 0.9 * total


def is_boss_fight(st):
    """A live enemy that cannot be run from (EnemyData's escape rate 0):
    Neifirst, Dark Force, Mother Brain and their like."""
    return any((enemy(eid) or {}).get("escape", 1) == 0
               for _, eid, _, _ in live_groups(st))


def plan_deban(party, known):
    """(caster, list slot) for DEBAN when half or more of the living
    party is without it, by a living member whose techniques are not
    sealed and who has the TP; None otherwise."""
    live = [m for m in party if not m["dead"]]
    if not live or 2 * sum(1 for m in live if m.get("deban")) > len(live):
        return None
    for h, m in enumerate(party):
        if m["dead"] or m.get("sealed") or h >= len(known):
            continue
        for slot, (name, tp, flags) in enumerate(known[h]["techs"]):
            if name == "DEBAN" and m["tp"] >= tp:
                return (h, slot)
    return None


SHIFT_CASTS = 2


def plan_shift(party, known, done, busy):
    """[(member, list slot)] for SHIFT this round: each living member,
    techniques not sealed, not already given another order, who knows it,
    has the TP and has cast it fewer than SHIFT_CASTS times this fight
    (`done`: the names of those who have, once a cast)."""
    out = []
    for h, m in enumerate(party):
        if m["dead"] or m.get("sealed") or h in busy or h >= len(known):
            continue
        if done.count(known[h]["name"]) >= SHIFT_CASTS:
            continue
        for slot, (name, tp, flags) in enumerate(known[h]["techs"]):
            if name == "SHIFT" and m["tp"] >= tp:
                out.append((h, slot))
    return out


def plan_type_tech(s, member, m, st, boss=False):
    """(list slot, enemy group stop, technique) for `m` (the techs line's
    entry for party position `member`), or None: the damage technique
    whose type bits (flags & $60) touch a live group, with the TP for
    it, whose expected damage -- one enemy, the group, or every live
    enemy it can touch, each capped at that enemy's HP, at its hit rate
    -- beats the weapon by TECH_MARGIN and gains TECH_GAIN_PER_TP a TP.
    A member with a mending technique keeps twice the cheapest's TP."""
    me = st["party"][member]
    groups = live_groups(st)
    if not groups:
        return None
    # A rate of $FF fails outright where the battle cannot be run from,
    # whatever the caller's policy.
    unrunnable = any((enemy(eid) or {}).get("escape", 1) == 0
                     for _, eid, _, _ in groups)
    heal_tp = [tp for (n, tp, f) in m["techs"] if n in HEALS]
    budget = me["tp"] - (2 * min(heal_tp) if heal_tp else 0)
    best = None
    for slot, (name, tp, flags) in enumerate(m["techs"]):
        rec = tech_record(name)
        if rec is None or tp > budget or not (flags & 0x80) \
                or not (flags & 7) or rec["rate"] == 0:
            continue
        if unrunnable and rec["rate"] == 0xFF:
            continue
        dmg = tech_damage(rec)
        hit = rec["hit"] / 256
        for g, eid, etype, hps in groups:
            if not (flags & 0x60 & etype):
                continue
            if flags & 4:           # every live enemy (loc_2148)
                hit_hps = [h for gg, ee, tt, hh in groups
                           if flags & 0x60 & tt for h in hh]
            elif flags & 2:         # the group (TechAction_Zan)
                hit_hps = hps
            else:                   # one of the group
                hit_hps = [sum(hps) / len(hps)]
            value = hit * sum(min(dmg, h) for h in hit_hps)
            blow = weapon_blow(s, member, me["name"], me["level"], hps,
                               (enemy(eid) or {}).get("defense", 0))
            gain = value - blow
            margin = BOSS_TECH_MARGIN if boss else TECH_MARGIN
            per_tp = BOSS_TECH_GAIN_PER_TP if boss else TECH_GAIN_PER_TP
            if value < margin * blow or gain < per_tp * tp:
                continue
            if best is None or gain > best[0]:
                best = (gain, (slot, g if len(groups) > 1 else 0, name))
    return best[1] if best else None


def group_mix(group):
    """{type: share of the group's enemy HP}: the eight formations of
    EnemyFormationTable's `group`, each a roll in eight."""
    b = battle_data()
    hp = {TYPE_BIO: 0.0, TYPE_ROBOT: 0.0, TYPE_BOTH: 0.0}
    if not group or group >= len(b["groups"]):
        return hp
    for f in b["groups"][group]:
        form = formation_of(f) if f else None
        if not form:
            continue
        for k in (1, 2):
            e = enemy(form[f"enemy{k}"])
            if e and form[f"count{k}"]:
                hp[e["type"] & 0x60] = hp.get(e["type"] & 0x60, 0) \
                    + form[f"count{k}"] * e["hp"]
    total = sum(hp.values()) or 1
    return {k: v / total for k, v in hp.items()}


def dungeon_mix(levels, late=True):
    """The HP-weighted type shares over `levels` (indices), each level's
    group as the level record has it -- enemy_group_late once $C737 is
    set (battle_encounter_check)."""
    acc = {TYPE_BIO: 0.0, TYPE_ROBOT: 0.0, TYPE_BOTH: 0.0}
    n = 0
    for lv in levels:
        rec = _levels().get(lv) or {}
        g = rec.get("enemy_group_late" if late else "enemy_group", 0)
        if not g:
            continue
        for k, v in group_mix(g).items():
            acc[k] += v
        n += 1
    return {k: v / n for k, v in acc.items()} if n else None


def tech_power(name, level, mix):
    """The best damage a member's battle techniques known at `level` do
    in a fight against `mix`: the mean damage (capped at 120: a fight's
    enemies rarely have more) times the enemies it reaches (one, a group
    of two, all three), times the share of the mix its type bits touch.
    None of it without a mix."""
    if not mix:
        return 0.0
    st = expected_stats(name, level)
    best = 0.0
    for key in _techs()["available"][name]["battle"][:st["techs_a"]]:
        rec = tech_record(key)
        if rec is None or not (rec["flags"] & 0x80) or not rec["rate"]:
            continue
        cover = sum(v for t, v in mix.items() if rec["flags"] & 0x60 & t)
        reach = 3 if rec["flags"] & 4 else (2 if rec["flags"] & 2 else 1)
        best = max(best, min(120.0, tech_damage(rec)) * reach * cover)
    return best

# ---------------------------------------------------------------------
# The C menu on the map: EQP.
# ---------------------------------------------------------------------

WIN_PLAYER_MENU  = 0x01     # ITEM STATE TECH STRNG EQP
WIN_ITEM_LIST2   = 0x1A     # EQP's item page
WIN_RIGHT_LEFT   = 0x39
WIN_STRNG_CHARS  = 0x3A
MENU_EQP = 4


def menu_escape(s, limit=8):
    """B until the party is back under the pad."""
    for _ in range(limit):
        st = s.state
        if st.get("screen") == "level" and st.get("kind") == "waiting" \
                and st.get("wait") == "control":
            return True
        if st.get("kind") == "waiting":
            s.send("press b", "wait idle")
        else:
            s.send("wait idle")
    return False


def list_pick(s, slot):
    """Pick bag slot `slot` in an item list (ITEM's $03, EQP's $1A/$1B):
    up to eight items is one stop each; nine or more is two pages, the
    first with NEXT at stop 0 above slots 0-7, the second (a window of
    its own) with the way back at stop 0 above slots 8-15
    (Win_MenuItemList's $108)."""
    stops = s.state.get("menu_stops") or 0
    if stops != 9:
        return menu_pick(s, slot)
    if slot < 8:
        return menu_pick(s, slot + 1)
    if not menu_pick(s, 0):                     # NEXT
        return False
    s.send("wait idle")
    return menu_pick(s, slot - 8 + 1)


def equip(s, member, slot, hand=0):
    """C, EQP, the member, the item in bag slot `slot` (0-based), and
    the right hand (0) or left (1) for a weapon. Returns the scripts
    shown ($202 put on, $204 taken off, $205/$201/$203 refused)."""
    if not s.ready():
        return None
    before = len(s.shown)
    s.send("press c", "wait idle")
    if s.state.get("menu") != WIN_PLAYER_MENU:
        menu_escape(s)
        return None
    menu_pick(s, MENU_EQP)
    s.send("wait idle")
    if s.state.get("menu") == WIN_STRNG_CHARS:
        menu_pick(s, member)
        s.send("wait idle")
    if s.state.get("menu") == WIN_ITEM_LIST2:
        list_pick(s, slot)
        s.send("wait idle")
    if s.state.get("menu") == WIN_RIGHT_LEFT:
        menu_pick(s, hand)
        s.send("wait idle")
    if s.state.get("wait") == "text":
        s.send("press c", "wait idle")
    menu_escape(s)
    return s.shown[before:]


# ---------------------------------------------------------------------
# Grinding round a town: out by its edge, wander, fight, back to the
# hospital when it gets bad, until the leader reaches a level.
# ---------------------------------------------------------------------

MOTAVIA = 0


def town_exit(s):
    """Walk to the nearest town edge and step off it; True once the
    level changes. A town's edge is two cells deep and goto stops on the
    first of them, so the last step -- away from the town's middle,
    onto the outer one -- is taken here."""
    level = s.state["level"]
    lm = level_map(level)
    edges = lm.town_edges()
    if not goto(s, edges):
        return False
    for _ in range(4):
        if s.state.get("level") != level:
            return True
        if not s.ready():
            return False
        x, y = s.state["x"], s.state["y"]
        dirs = [("down", 0, 1), ("up", 0, -1), ("right", 1, 0), ("left", -1, 0)]
        # outward first: the direction that moves away from the middle
        dirs.sort(key=lambda d: -(d[1] * (x - lm.w / 2) + d[2] * (y - lm.h / 2)))
        for d, dx, dy in dirs:
            n = ((x + dx) % lm.w, (y + dy) % lm.h)
            if n in edges or not lm.open(*n):
                s.send(f"walk {d} 1")
                if s.state.get("kind") != "waiting":
                    s.send("wait idle")
                if s.state.get("level") != level:
                    return True
                break
    return s.state.get("level") != level


def town_arrivals(level):
    """The cells a town's entrances put the party on, from Motavia's
    and DezolisSkure's door records -- inside the town, one step off its edge. Never the
    edge cells themselves: a party standing on one leaves with its
    next step, whichever way it goes."""
    # Dezolis's towns are entered from DezolisSkure (3), not Motavia:
    # without its records the goals fell back to the town's edges, and a
    # party walked to one arrived and left in the same step (build 85).
    return {(e["to_x"], e["to_y"]) for w in (MOTAVIA, 0x03)
            for e in level_doors(w).values()
            if e.get("to_level") == level} or level_map(level).town_edges()


def enter_town(s, level):
    """On Motavia, walk to the town's entrance record and push in."""
    if s.state.get("level") == level:
        return True
    if s.state.get("level") != MOTAVIA:
        return travel(s, level, town_arrivals(level)) \
            or s.state.get("level") == level
    for e in level_doors(MOTAVIA).values():
        if e.get("to_level") == level:
            cells = door_cells(MOTAVIA, e["y"], e["x"])
            if not goto(s, cells):
                # Not on this side of Motavia (Paseo from Arima's gate
                # is through the tube): plan the whole way instead.
                return travel(s, level, town_arrivals(level)) \
                    or s.state.get("level") == level
            if s.state.get("level") == MOTAVIA and s.ready():
                d = cells.get((s.state["x"], s.state["y"]))
                s.send(f"walk {d or 'up'} 1")
                s.ready()
            return s.state.get("level") == level
    return False


def cure_poison(s):
    """Anti from whoever knows it and has the TP (Nei, from the start),
    else an Antidote from anyone's bag, on each living poisoned member.
    needs_healing counts poison, so a poisoned member left alone stops
    every wander before its first step: build 79's Climatrol grind ran
    120 trips of nothing but travel fights that way, Rudo poisoned from
    the save it started from. Returns how many were cured."""
    cured = 0
    for _ in range(4):
        party = s.state.get("party") or []
        sick = [i for i, m in enumerate(party)
                if m.get("poison") and not m["dead"]]
        if not sick or not s.ready():
            break
        tech = find_tech(s, "Anti")
        if tech:
            use_tech(s, tech[0], tech[1], target=sick[0])
        else:
            pill = find_item(s, "Antidote")
            if not pill:
                break
            use_item(s, pill[0], pill[1], target=sick[0])
        s.send("wait idle")
        s.ready()
        if not (s.state.get("party") or [{}] * (sick[0] + 1))[sick[0]] \
                .get("poison"):
            cured += 1
        else:
            break
    return cured


WALK_RE = re.compile(r"walk (up|down|left|right) (\d+)$")
STEP = {"up": (0, -1), "down": (0, 1), "left": (-1, 0), "right": (1, 0)}


def open_walk(s, walk):
    """(build 102) A pattern's walk cut short at the first solid cell ahead
    on the level's own collision, or None when the very next cell is
    solid. Before, the walk went into the wall and the probe held the
    direction there for its whole grace (BLOCK_GRACE, 120 ticks: two
    seconds at 60 Hz) before giving up -- the owner saw the party stand at
    the wall, then go round -- 4,369 times in seed 11's game (build 101b).
    A person in the way is not in the map and still blocks as before."""
    m = WALK_RE.match(walk)
    st = s.state
    if m is None or st.get("level") is None or st.get("x") is None:
        return walk
    lm = level_map(st["level"])
    dx, dy = STEP[m.group(1)]
    x, y = st["x"], st["y"]
    n = 0
    while n < int(m.group(2)):
        x, y = (x + dx) % lm.w, (y + dy) % lm.h
        if not lm.open(x, y):
            break
        n += 1
    return f"walk {m.group(1)} {n}" if n else None


def wander(s, walks, fights, shots=None, name="grind", mates=False):
    """Walk the pattern until `fights` fights have happened or the
    party needs the hospital (with `mates`, Monomates are used first).
    Returns the per-fight script lists."""
    out = []
    i = 0
    level = s.state.get("level")
    shut = 0
    while len(out) < fights:
        if not s.ready():
            break
        cure_poison(s)
        if needs_healing(s) or s.state.get("level") != level:
            break
        walk = open_walk(s, walks[i % len(walks)])
        i += 1
        if walk is None:
            # (build 102) Nothing open that way: the next walk of the
            # pattern, and none open in any of them, the end of it.
            shut += 1
            if shut >= len(walks):
                break
            continue
        shut = 0
        s.send(walk)
        if s.state.get("kind") != "waiting":
            s.send("wait idle")             # the fade into a fight
        if s.state.get("screen") == "battle":
            if shots is not None:
                s.send(f"shot {name}_{len(shots)}_in")
                shots.append(1)
            out.append(fight_smart(s))
            if s.state.get("screen") in ("gameover", "sega", "title"):
                break
            # TP comes back at the hospital for next to nothing: top
            # everyone up with Res after every fight while it lasts, as
            # a player would, and keep the mates for when it has run out.
            cast_res(s, below=0.75)
            if mates and needs_healing(s):
                mend(s)
    return out


def grinding(fn):
    """(build 120) A grind's function, its first argument the Session:
    run inside s.in_grind(), so COMPUTER PLAYS's SPEED AUTO plays it FAST."""
    @functools.wraps(fn)
    def run(s, *args, **kwargs):
        with s.in_grind():
            return fn(s, *args, **kwargs)
    return run


@grinding
def grind(s, town, target_level, fights_per_trip=6, trips=40,
          walks=("walk down 3", "walk up 3", "walk left 3", "walk right 3"),
          log=print):
    """From inside `town`: out, fight, and back to the hospital as
    needed, until the leader is at `target_level` or the party is lost.
    Returns True on reaching the level."""
    for trip in range(trips):
        if s.state.get("level") == town:
            if not town_exit(s):
                log("could not leave town")
                return False
        fights = wander(s, list(walks), fights_per_trip)
        p = s.state.get("party") or []
        log(f"trip {trip}: {len(fights)} fights; "
            + " ".join(f"{m['name']} L{m['level']} {m['hp']}/{m['max_hp']}"
                       + (" dead" if m["dead"] else "") for m in p)
            + f"; meseta {s.state.get('meseta')}")
        if s.state.get("screen") not in ("level", "battle"):
            log("lost: " + s.state.get("screen", "?"))
            return False
        if p and p[0]["level"] >= target_level:
            return True
        if needs_healing(s) or trip == trips - 1:
            if not enter_town(s, town):
                log("could not get back to town")
                return False
            heal(s)
            if s.state.get("meseta") is not None and s.state["meseta"] < 8:
                log("broke")
                return False
    return False


@grinding
def dungeon_grind(s, level, spot, town, target_level, lowest=True,
                  trips=60, fights_per_trip=8,
                  walks=("walk left 3", "walk right 3"), log=print,
                  ground=None, gear=False):
    """Grind on a dungeon floor, where the fights pay more than a town's
    field: stand on `spot` of `level` (an open run three either side),
    fight until hurt, mend with Res and the mates, and go back to
    `town`'s hospital (retreat) when those run out. True once the
    lowest member (the leader without `lowest`) is at target_level.
    (build 103) With `ground` (a Ground on `level`), the floor's long
    walks instead of the spot's pattern: onto the ground, then `roam`
    anchor to anchor for the trip's fights -- the same fights a step
    (the roll counts steps, not where they go), and no party walking
    three steps each way at the entrance for millions of ticks. With
    `gear`, the best gear the money covers at every return to town."""
    def done():
        p = s.state.get("party") or []
        lv = [m["level"] for m in p] if lowest else [p[0]["level"]] if p else []
        return bool(lv) and min(lv) >= target_level
    goals = ground.cells if ground is not None else {spot}
    low_in_town = min((m["level"] for m in s.state.get("party") or []),
                      default=0)
    for trip in range(trips):
        if done():
            return True
        here = (s.state.get("level"), (s.state.get("x"), s.state.get("y")))
        away = ground is None or here[0] != level or here[1] not in goals
        if away and not travel(s, level, goals, log=log):
            log(f"dungeon_grind: could not reach {level_name(level)} "
                f"{spot if ground is None else 'ground'}")
            if s.state.get("screen") not in ("level", "battle"):
                return False
            retreat(s, town, log=log)
            continue
        if ground is None:
            fights = wander(s, list(walks), fights_per_trip, mates=True)
            legs = ""
        else:
            fights, n = roam(s, ground, fights_per_trip, log=log)
            legs = f", {n} legs"
        log(f"trip {trip} on {level_name(level)}: {len(fights)} fights{legs}; "
            + " ".join(f"{m['name']} L{m['level']} {m['hp']}/{m['max_hp']}"
                       + (" dead" if m["dead"] else "")
                       for m in s.state.get("party") or [])
            + f"; meseta {s.state.get('meseta')}")
        if s.state.get("screen") not in ("level", "battle"):
            log("lost: " + str(s.state.get("screen")))
            return False
        dead = any(m["dead"] for m in s.state.get("party") or [])
        low = min((m["level"] for m in s.state.get("party") or []),
                  default=0)
        # (build 103) With `gear`, also back to town when the lowest
        # member's level has risen since the last time there: the new
        # gear, and a save on the way (trip_save) -- the first forced
        # retreat could be two million ticks in, and a wipe-out before it
        # replayed all of them.
        back = gear and MID_SAVES is not None and low > low_in_town
        if dead or (needs_healing(s) and count_mates(s) == 0) or back:
            retreat(s, town, log=log)
            if gear and s.state.get("level") == town:
                gear_up(s, town, log=log)
                trip_save(s, town, log=log)
                low_in_town = min((m["level"] for m in s.state.get("party")
                                   or []), default=0)
    return done() or lean_short(s, town, target_level, log)


# ---------------------------------------------------------------------
# Finding the way across levels: stairs, town doors, town edges.
# ---------------------------------------------------------------------

_MAPS = {}


# The items the party holds that change a map: LoadLevelData's tail
# (loc_9258, game.c's level_map_patches) puts block $63 -- solid, special
# -- at Menobe's, Ikuto's, Guaron's and Naval's doors on DezolisSkure
# when anyone holds the Prism; without it those cells are open ground and
# their door records never match. A caller that knows sets HELD (build 88).
HELD = set()
_PATCHES = None


def item_patches(level):
    """map_patches.json's item-conditioned entries for `level` whose item
    is in HELD: [(plane, block, offsets)]."""
    global _PATCHES
    if _PATCHES is None:
        _PATCHES = json.loads(Path("assets/data/map_patches.json")
                              .read_text())["patches"]
    out = []
    for p in _PATCHES:
        name = (p.get("item_name") or "").replace("ItemID_", "")
        if p.get("level") == level and name and name in HELD \
                and "set" in p:
            out.append(("plane" + p["plane"], p["set"]["block"],
                        p["set"]["at"]))
    return out


def level_map(level):
    key = (level, tuple(sorted(HELD)) if item_patches(level) else ())
    if key not in _MAPS:
        lm = LevelMap(level)
        for plane, block, at in item_patches(level):
            if plane in lm.planes:
                for i in at:
                    if 0 <= i < len(lm.planes[plane]):
                        lm.planes[plane][i] = block
        _MAPS[key] = lm
    return _MAPS[key]


_EXITS = None


def town_exits():
    global _EXITS
    if _EXITS is None:
        d = json.loads(Path("assets/levels/exits.json").read_text())
        _EXITS = {e["from_level"]: e for e in d["entries"]}
    return _EXITS


_WAYS = {}


def ways_out(level):
    """Every way off a level: (cells to stand on, to_level, arrival
    cell). A town's edges use exits.json; anything else is a door
    record with a to_level. (build 89) Kept per map, as level_map is:
    plan() asks for each level many times."""
    key = (level, tuple(sorted(HELD)) if item_patches(level) else ())
    if key not in _WAYS:
        _WAYS[key] = _ways_out(level)
    return _WAYS[key]


# Doors whose record names a building, not a level, and whose building
# is a plain trip somewhere else with no question asked -- walk.c's
# interact() hands them to Building_CheckRoutine. Skure's roof door
# (building 11, Building_Roof's flight home, build 84: route roof_yes
# lands on Paseo 31,16). The other way, the Central Tower's desk asks
# ROOF and YES, which is story.stage_roof's to answer, not plan()'s.
FLIGHTS = {(0x03, 79, 59): (0x04, (31, 16))}


def _ways_out(level):
    lm = level_map(level)
    out = []
    for d in level_doors(level).values():
        dest = FLIGHTS.get((level, d["y"], d["x"]))
        if "to_level" not in d and dest is not None:
            cells = door_cells(level, d["y"], d["x"])
            if cells:
                out.append((cells, dest[0], dest[1]))
    # A hole next to a door's cells is a way down, not a place to stand
    # for the door: Climatrol F5's block $3A floor (a type-5 hole, walk.c's
    # check_underfoot) lies against several of its stairs.
    fall = holes(level)
    for d in level_doors(level).values():
        if "to_level" in d:
            cells = door_cells(level, d["y"], d["x"])
            cells = {c: v for c, v in cells.items() if c not in fall}
            if cells:
                out.append((cells, d["to_level"], (d["to_x"], d["to_y"])))
    e = town_exits().get(level)
    if e is not None and lm.type == 2:
        out.append((lm.town_edges(), e["to_level"], (e["to_x"], e["to_y"])))
    elif e is not None and level >= 0x10:
        # A dungeon's way out (interact(), nothing matched): any solid
        # special cell no door record covers, pushed from any side.
        cells = {}
        for cy in range(lm.h):
            for cx in range(lm.w):
                if lm.bits(cx, cy) != (True, True):
                    continue
                if any(d["y"] - 2 <= cy <= d["y"] and d["x"] - 2 <= cx <= d["x"]
                       for d in level_doors(level).values()):
                    continue
                for d, dx, dy in (("up", 0, 1), ("down", 0, -1),
                                  ("right", -1, 0), ("left", 1, 0)):
                    n = ((cx + dx) % lm.w, (cy + dy) % lm.h)
                    if lm.open(*n) and n not in cells:
                        cells[n] = d
        if cells:
            out.append((cells, e["to_level"], (e["to_x"], e["to_y"])))
    # Holes: stepping onto one is the way (a special cell, so path()
    # only ever takes one as its goal).
    by_dest = {}
    for cell, dest in holes(level).items():
        by_dest.setdefault(dest, {})[cell] = None
    for (to_level, arrive), cells in by_dest.items():
        out.append((cells, to_level, arrive))
    return out


# loc_718A (walk.c's fall_through): a hole's level delta and its y and x
# deltas in 32px blocks, indexed by level_index - $4E, clamped to 0-22.
HOLE_TABLE = [(-1, 0, 0), (-1, 12, 0), (-1, 0, 16), (-1, 0, 11), (-1, 2, 0),
              (-1, 0, 0), (-1, 1, -2), (-1, 3, 13), (2, 0, 0), (2, 0, 0),
              (-3, 0, 0), (-3, 0, 0), (-3, 0, -13), (-1, -2, 16),
              (-1, 3, -3), (2, 0, 0), (2, 0, 0), (-3, 0, 0), (-3, 0, 0),
              (-3, 0, 0), (-1, 0, 0), (-1, 0, 0), (-1, -16, 0)]


def holes(level):
    """Every hole on a level (walk.c's check_underfoot): {cell: (level,
    where the party ends up)}. (build 89) A landing on another hole falls
    again before the pad is read (walk_tick's check_underfoot comes
    first, loc_382A), so a chain is followed to its last landing: IkutoB1
    at 19,18 lands on IkutoB2 41,18, a hole, and so on IkutoB3 73,18."""
    out = {}
    for cell, dest in _holes(level).items():
        for _ in range(8):
            nxt = _holes(dest[0]).get(dest[1]) if dest[0] in _levels_known() \
                else None
            if nxt is None:
                break
            dest = nxt
        out[cell] = dest
    return out


def _levels_known():
    return range(len(_levels()))


_HOLES = {}


def _holes(level):
    key = (level, tuple(sorted(HELD)) if item_patches(level) else ())
    if key not in _HOLES:
        _HOLES[key] = _holes_one(level)
    return _HOLES[key]


def _holes_one(level):
    """One level's holes, each to its first landing. (build 170) In the
    map's order, row by row: plan() takes the first hole to a landing, and
    until 170 they came in the set's order, CPython's table's."""
    lm = level_map(level)
    out = {}
    for (cx, cy) in sorted(lm.specials(), key=lambda c: (c[1], c[0])):
        b = lm.block(lm.coll, cx, cy)
        if (lm.type == 5 and b == 0x3A) or (lm.type == 6 and b == 0x04):
            i = min(max(level - 0x4E, 0), 22)
            dl, dy, dx = HOLE_TABLE[i]
            out[(cx, cy)] = (level + dl, (cx + dx * 2, cy + dy * 2))
        elif lm.type == 3 and 0x50 <= b <= 0x62:
            if level == 0x15:
                out[(cx, cy)] = (0x14, (0x60 // 16, 0xC0 // 16))
            elif level == 0x16:
                out[(cx, cy)] = (0x15, (0x2C0 // 16, 0x220 // 16)
                                 if cy * 16 >= 0x100 else
                                 (0x340 // 16, 0xA0 // 16))
    return out


def plan(level, cell, target_level, goals):
    """A route of (level, cells) legs from (level, cell) to any of
    `goals` on `target_level`, through whatever ways out connect them.
    None if the maps do not connect. Breadth-first over (level, arrival
    cell), fewest level changes first.

    (build 89) Whether a way out can be walked to is LevelMap.reaches --
    the level's pieces, worked out once a map -- not a path() search for
    every pair. Build 88's pruning by level-graph distance
    (level_distances) is gone: DezolisSkure is not one piece, and every
    trip between its towns goes down through Skure B1/B2, levels further
    from the target than DezolisSkure itself, so from Zosa the pruned
    search never found a route, with or without the Prism. The full
    search then called path() for every node and way out, which with the
    Prism's four dungeons (some forty floors) was ~31,700 calls and
    minutes. Same legs as that search, in well under a second."""
    start = (level, cell)
    prev = {start: None}
    q = deque([start])
    while q:
        L, c = q.popleft()
        lm = level_map(L)
        if L == target_level and lm.reaches(c, goals):
            legs = [(L, goals)]
            node = (L, c)
            while prev[node] is not None:
                node, cells = prev[node]
                legs.append((node[0], cells))
            return legs[::-1]
        for cells, to_level, arrive in ways_out(L):
            nxt = (to_level, arrive)
            if nxt in prev or not cells:
                continue
            if lm.reaches(c, cells):
                prev[nxt] = ((L, c), cells)
                q.append(nxt)
    return None


def travel(s, target_level, goals, log=print):
    """Walk the plan: each leg is a goto on its level, then a push up
    if the level has not changed by itself (a town door). Fights on
    the way are fought; True on reaching a goal cell."""
    if not s.ready():
        return False
    st = s.state
    if st.get("riding") and st.get("level") == MOTAVIA:
        # Arrived on the water (out of the Underwater Passage by a pipe,
        # out of a dam): plan() walks, so ashore first, somewhere it can
        # walk from.
        region = landing_region(target_level, goals)
        if not region or not ride_ashore(s, None, log=log, region=region):
            log("travel: riding, and no landing leads to "
                f"{level_name(target_level)}")
            return False
        st = s.state
    if (st["x"], st["y"]) in goals and st["level"] == target_level:
        return True
    # (build 89) Up to three plans: a walk that arrives somewhere its plan
    # did not expect -- a hole the maps get wrong, a fall chained through
    # two floors before build 89 -- plans again from where it stands
    # rather than walking the rest of a plan made for somewhere else.
    for attempt in range(4):
        ride_holes(s, log)
        st = s.state
        legs = plan(st["level"], (st["x"], st["y"]), target_level, goals)
        if legs is None:
            log(f"no way from {level_name(st['level'])} to {level_name(target_level)}")
            return False
        ok = _walk_legs(s, legs, target_level, goals, log)
        if ok or getattr(s, "hurt", False) \
                or s.state.get("screen") not in ("level", "battle"):
            return ok
        if not s.ready() or s.state.get("screen") != "level":
            return False
        log(f"travel: re-planning from {level_name(s.state.get('level'))} "
            f"{(s.state.get('x'), s.state.get('y'))}")
    return False


def ride_holes(s, log=print, limit=12):
    """(build 89) Standing on a hole -- landed on one by a fall -- the next
    move falls again (check_underfoot runs before the pad is read), so a
    plan from here is a plan from somewhere else. Take the falls until
    the party stands on something that is not a hole: Guaron's +2/-3
    floors land on (12,10) five times running."""
    for _ in range(limit):
        st = s.state
        if st.get("screen") != "level" or st.get("level") is None:
            return
        here = (st.get("x"), st.get("y"))
        if here not in holes(st["level"]):
            return
        before = (st["level"], here)
        s.send("walk up 1")
        s.ready()
        st = s.state
        if (st.get("level"), (st.get("x"), st.get("y"))) == before:
            log(f"ride_holes: on a hole at {before} and nothing fell")
            return


def _walk_legs(s, legs, target_level, goals, log):
    for level, cells in legs:
        ride_holes(s, log)
        for _ in range(3):
            if not s.ready():
                log(f"travel: never got control on {level_name(level)}: "
                    f"{s.state}")
                return False
            if s.state.get("level") != level:
                break
            if not goto(s, cells):
                log(f"travel: leg on {level_name(level)} failed: {s.why}")
            if getattr(s, "hurt", False):
                log("travel: stopping, hurt with nothing left")
                return False
            if s.state.get("level") != level:
                break
            here = (s.state.get("x"), s.state.get("y"))
            if here in cells:
                if level == target_level and cells is goals:
                    return True
                d = cells.get(here) if hasattr(cells, "get") else None
                s.send(f"walk {d or 'up'} 1")
                s.ready()
                # A door onto the same level (Uzo's caves, a dungeon's
                # stairs within one map) leaves the level number alone;
                # having been moved off the door's cells is the leg done.
                if (s.state.get("x"), s.state.get("y")) not in cells:
                    break
        if s.state.get("screen") not in ("level", "battle"):
            log(f"travel: off the map: {s.state}")
            return False
    st = s.state
    ok = st.get("level") == target_level and (st.get("x"), st.get("y")) in goals
    if not ok:
        log(f"travel: ended on {level_name(st.get('level'))} at "
            f"{(st.get('x'), st.get('y'))}, wanted {level_name(target_level)} "
            f"{sorted(goals)[:4]} -- screen {st.get('screen')}, "
            f"{st.get('kind')} {st.get('wait')}, menu {st.get('menu')}, "
            f"tick {st.get('tick')}")
    return ok


# ---------------------------------------------------------------------
# The jet scooter on Motavia (README, "The jet scooter"): its own table,
# loc_29E16 -- open over water, solid on land, the dam entrances and
# Uzo's solid and special -- and a 2x2 footprint.
# ---------------------------------------------------------------------

MOTAVIA = 0x00

# ObjJetSctr_Main's probes (walk.c scooter_tick) as cell offsets (dx, dy)
# from the `state` cell, which while riding is the scooter's origin >> 4:
# the pair of cells beyond the edge it is about to cross, the first
# probe's result deciding before the second is looked at.
SCOOTER_PROBES = {"up": ((0, -2), (-1, -2)), "down": ((0, 1), (-1, 1)),
                  "left": ((-2, 0), (-2, -1)), "right": ((1, 0), (1, -1))}
SCOOTER_MOVES = {"up": (0, -1), "down": (0, 1), "left": (-1, 0),
                 "right": (1, 0)}
# JetScooter_CharOffPosOffsets, by facing, as a cell offset: where the
# walking table (loc_29B64, the solid bit) is asked, and where the leader
# stands after `$171D`.
SCOOTER_DISMOUNT = {"up": (0, -2), "down": (0, 1), "left": (-2, 0),
                    "right": (1, 0)}

_SCOOTER = None


class ScooterMap:
    """Motavia as the scooter sees it. `step` is ObjJetSctr_Main's one
    direction: ("move", cell), ("door", record) through loc_6E4E, or None
    when a probe is solid and nothing matches (the scooter only turns)."""

    def __init__(self):
        self.lm = level_map(MOTAVIA)
        self.attr = [0] * 256
        for line in Path("assets/levels/jet_scooter.collision.txt"
                         ).read_text().splitlines():
            if line.startswith("#") or not line.strip():
                continue
            b, solid, special = line.split()
            v = 0
            for q in range(4):
                v |= (1 << q) if solid[q] == "#" else 0
                v |= (0x10 << q) if special[q] == "#" else 0
            self.attr[int(b, 16)] = v
        self.doors = list(level_doors(MOTAVIA).values())

    def wrap(self, c):
        return (c[0] % self.lm.w, c[1] % self.lm.h)

    def bits(self, c):
        cx, cy = self.wrap(c)
        q = ((cy & 1) << 1) + (cx & 1)
        a = self.attr[self.lm.block(self.lm.coll, cx, cy)]
        return bool(a & (1 << q)), bool(a & (0x10 << q))

    def record(self, c):
        """walk.c interact(): the first record within two cells down and
        right of the probed cell."""
        cx, cy = self.wrap(c)
        for d in self.doors:
            if cy <= d["y"] <= cy + 2 and cx <= d["x"] <= cx + 2:
                return d
        return None

    def step(self, c, d):
        for dx, dy in SCOOTER_PROBES[d]:
            p = (c[0] + dx, c[1] + dy)
            solid, special = self.bits(p)
            if solid:
                if special:
                    rec = self.record(p)
                    return ("door", rec) if rec is not None else None
                return None
        mx, my = SCOOTER_MOVES[d]
        return ("move", self.wrap((c[0] + mx, c[1] + my)))

    def plan(self, start, goal):
        """Moves from `start` until goal(cell, direction) says yes:
        [directions] + [the last direction], or None. BFS: every step is
        the same eight frames."""
        prev = {start: None}
        q = deque([start])
        while q:
            c = q.popleft()
            for d in ("up", "down", "left", "right"):
                if goal(self, c, d):
                    out = [d]
                    while prev[c] is not None:
                        c, dd = prev[c]
                        out.append(dd)
                    return out[::-1]
            for d in ("up", "down", "left", "right"):
                r = self.step(c, d)
                if r is None or r[0] != "move" or r[1] in prev:
                    continue
                prev[r[1]] = (c, d)
                q.append(r[1])
        return None


def scooter_map():
    global _SCOOTER
    if _SCOOTER is None:
        _SCOOTER = ScooterMap()
    return _SCOOTER


def walk_region(level, cells):
    """Every open cell on a level connected on foot to any of `cells`,
    crossing no special cell (a town's edge, stairs, a hole)."""
    lm = level_map(level)
    specials = lm.specials() | lm.town_edges()
    seen = set()
    q = deque(c for c in cells if lm.open(*c))
    seen.update(q)
    while q:
        c = q.popleft()
        for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
            n = ((c[0] + dx) % lm.w, (c[1] + dy) % lm.h)
            if n in seen or not lm.open(*n) or n in specials:
                continue
            seen.add(n)
            q.append(n)
    return seen


def board_scooter(s, log=print):
    """On foot on Motavia: walk up to the parked scooter, face it, A and
    C through $1686 (the maker's note, first boarding only) and $171C.
    The scooter is object slot 0 at its parking place ($C656/$C654, a cell
    corner), a 32x32 box over the two cells up and two left of it."""
    if not s.ready():
        return False
    if s.state.get("riding"):
        return True
    # Entering a town with the scooter owned parks it at that town's
    # place (LoadSpritesInLevel's tail, ps2.asm:862-893: Kueri y $5F0 x
    # $0F0 and so on, when $C716 is 2), so a cell remembered from before
    # a town -- a retreat's getting off, most often -- is stale. Out onto
    # Motavia and read the object instead.
    where = None
    if s.state.get("level") != MOTAVIA:
        # Out into Motavia first, where the scooter object is spawned
        # at its parking place and can be looked at.
        e = town_exits().get(s.state.get("level"))
        if e is None or not travel(s, MOTAVIA, {(e["to_x"], e["to_y"])},
                                   log=log):
            log("board: could not get out onto Motavia from",
                level_name(s.state.get("level")))
            return False
    if s.state.get("level") == MOTAVIA:
        # The object itself (slot 0 at $C656/$C654) over anything
        # remembered: ride_ashore records the rider's cell, which is not
        # the object's cell corner, and a box built from it is a cell off
        # -- A there is $1708, "nothing unusual here".
        o = [x for x in objects(s) if x["object"] == 0x22]
        if o:
            where = o[0]["cell"]
            s.scooter_at = where
        else:
            where = getattr(s, "scooter_at", None)
    if where is None:
        log("board: where the scooter is parked is not known here")
        return False
    cx, cy = where
    box = {(cx - 1, cy - 1), (cx, cy - 1), (cx - 1, cy), (cx, cy)}
    cells = facing_cells(MOTAVIA, box)
    if not travel(s, MOTAVIA, cells, log=log):
        log("board: could not reach the scooter from",
            (s.state.get("x"), s.state.get("y")))
        return False
    here = (s.state["x"], s.state["y"])
    s.send(f"press {cells[here]}", "wait idle")
    shown = talk(s)
    # The flag is set on the next idle visit and the scooter's init sees
    # it a frame later; until its main routine runs, the leader's copy
    # (the `state` cell) is still where he stood.
    s.send("wait idle", "wait ticks 4")
    log("board:", scripts_text(shown),
        (s.state.get("x"), s.state.get("y")))
    return bool(s.state.get("riding"))


def ride(s, goal, log=print, tries=40):
    """Ride the scooter until goal(map, cell, direction) holds for the
    cell it is on and the direction it is about to press, fighting on the
    way and re-planning after each fight (a fight on a step's first
    frame leaves the scooter where the step began). Returns the last
    direction, pressed by the caller, or None."""
    sm = scooter_map()
    for _ in range(tries):
        if not s.ready():
            return None
        st = s.state
        if st.get("level") != MOTAVIA or not st.get("riding"):
            log(f"ride: not riding on Motavia: {st}")
            return None
        cur = (st["x"], st["y"])
        moves = sm.plan(cur, goal)
        if moves is None:
            log(f"ride: no way on the water from {cur}")
            return None
        last = moves.pop()
        if not moves:
            return last
        for d, n in _runs(cur, _cells_of(cur, moves, sm), sm.lm.w, sm.lm.h):
            s.send(f"walk {d} {n}")
            if s.state.get("kind") != "waiting":
                s.send("wait idle")
            if s.state.get("screen") == "battle":
                fight_smart(s)
                if needs_healing(s) and s.state.get("screen") == "level":
                    mend(s)
                break
            if s.state.get("kind") != "waiting" \
                    or s.state.get("wait") != "control":
                break
    log("ride: gave up")
    return None


_COMPONENTS = None


def motavia_components():
    """Motavia's ground split into pieces connected on foot: a list of
    cell sets, largest first."""
    global _COMPONENTS
    if _COMPONENTS is None:
        lm = level_map(MOTAVIA)
        specials = lm.specials()
        seen, out = set(), []
        for cy in range(lm.h):
            for cx in range(lm.w):
                c = (cx, cy)
                if c in seen or not lm.open(cx, cy) or c in specials:
                    continue
                comp = walk_region(MOTAVIA, {c})
                seen |= comp
                out.append(comp)
        out.sort(key=len, reverse=True)
        _COMPONENTS = out
    return _COMPONENTS


def landing_region(target_level, goals):
    """The ground on Motavia from which plan() gets on foot to `goals`
    on `target_level`: every piece whose first cell plans there."""
    region = set()
    if target_level == MOTAVIA:
        return walk_region(MOTAVIA, goals)
    # A door on Motavia straight into the target level (a town, a
    # dungeon's ground floor) first: plan() is slow to ask per piece.
    for (ry, rx), d in level_doors(MOTAVIA).items():
        if d.get("to_level") == target_level:
            cells = door_cells(MOTAVIA, ry, rx)
            if cells:
                region |= walk_region(MOTAVIA, set(cells))
    if region:
        return region
    for comp in motavia_components():
        if len(comp) < 4:
            continue
        rep = min(comp)
        if (target_level == MOTAVIA and comp & set(goals)) \
                or plan(MOTAVIA, rep, target_level, goals) is not None:
            region |= comp
    return region


def ride_to_face(s, cells, log=print):
    """Ride until the leader's A box (loc_7278's facing row, from the
    scooter's corner, which the leader's copy holds) overlaps the box of
    an object on `cells`, with the press toward it one that only turns
    the scooter; press it. True when facing it -- for an item used on
    the water (Mruragum on the ocean floor's event $F)."""
    rows = {"up": BOX_ROWS[0], "down": BOX_ROWS[1], "left": BOX_ROWS[2],
            "right": BOX_ROWS[3]}
    boxes = [(x * 16, 15, y * 16, 15) for x, y in cells]

    def goal(sm, c, d):
        if sm.step(c, d) is not None:
            return False
        l, w, t, h = rows[d]
        x0, y0 = c[0] * 16 + l, c[1] * 16 + t
        return any(ox <= x0 + w and ox + ow >= x0
                   and oy <= y0 + h and oy + oh >= y0
                   for ox, ow, oy, oh in boxes)
    d = ride(s, goal, log=log)
    if d is None:
        return False
    s.send(f"press {d}", "wait idle")
    return s.state.get("facing") == d


def _cells_of(start, moves, sm):
    out, c = [], start
    for d in moves:
        mx, my = SCOOTER_MOVES[d]
        c = sm.wrap((c[0] + mx, c[1] + my))
        out.append(c)
    return out


def ride_into(s, to_level, log=print):
    """Ride to the door record for `to_level` that the scooter can push
    into (a dam, Uzo) and push. True once on that level."""
    def goal(sm, c, d):
        r = sm.step(c, d)
        return r is not None and r[0] == "door" and r[1]["to_level"] == to_level
    d = ride(s, goal, log=log)
    if s.state.get("level") == to_level:
        return True                 # the ride's own last push went in
    if d is None:
        return False
    s.send(f"walk {d} 1")
    s.ready()
    return s.state.get("level") == to_level


def ride_ashore(s, targets, log=print, region=None):
    """Ride to a place where getting off lands on ground connected on
    foot to any of `targets` (cells on Motavia) -- or in `region`, when
    given -- turn to face it and get off: A is $171D, "Ok. Let's get off
    and walk from here." True once on foot."""
    if region is None:
        region = walk_region(MOTAVIA, targets)
    lm = level_map(MOTAVIA)

    def goal(sm, c, d):
        # The press has to turn, not move: a probe that is clear would
        # step onto it instead (and the offset cell is water then).
        if sm.step(c, d) is not None:
            return False
        ox, oy = SCOOTER_DISMOUNT[d]
        cell = sm.wrap((c[0] + ox, c[1] + oy))
        return cell in region and lm.open(*cell)
    d = ride(s, goal, log=log)
    if d is None:
        return False
    s.scooter_at = (s.state["x"], s.state["y"])     # parked where it stops
    s.send(f"press {d}", "wait idle")
    shown = talk(s)
    s.send("wait idle")
    s.ready()
    log("ashore:", scripts_text(shown),
        (s.state.get("x"), s.state.get("y")))
    return not s.state.get("riding")


# ---------------------------------------------------------------------
# Story pieces: CONTINUE from a save, Rolf's house, talking to people.
# ---------------------------------------------------------------------

WIN_GAME_SELECT = 0x37
WIN_HOUSE_OPTIONS = 0x29


def continue_game(s, save_file, slot=1, limit=40):
    """Power on with `save_file` in slot `slot`, CONTINUE it, and play
    through Data Memory's welcome to the street. (build 100b) With
    `save_file` None, the slot as the game last saved it (COMPUTER PLAYS
    --no-swap). The probe's `start slot` lands in Data Memory; the game's
    COMPUTER PLAYS resets to the Sega screen instead, and the title's Start
    and CONTINUE are pressed here."""
    if save_file is None:
        s.send("start new", "wait idle")
    else:
        s.send(f"start slot {slot} {save_file}", "wait idle")
    try:
        # A save is made in a Data Memory, so its own location is the
        # town the Telepipe goes back to ($C646 = level_index, loc_B5A4).
        town = map_index(json.loads(Path(save_file).read_text())[
            "location"]["map"])
        if town is not None:
            s.save_town = town
    except (OSError, ValueError, KeyError, TypeError):
        pass
    for _ in range(limit):
        st = s.state
        if st.get("screen") == "level" and st.get("kind") == "waiting" \
                and st.get("wait") == "control":
            return True
        if st.get("kind") != "waiting":
            s.send("wait idle")
        elif st.get("wait") == "menu" and st.get("menu") == WIN_GAME_SELECT:
            menu_pick(s, 1)                     # CONTINUE
        elif st.get("wait") == "menu" and st.get("menu") == WIN_SAVE_SLOTS:
            menu_pick(s, slot - 1)
        elif st.get("wait") == "title":
            s.send("press start")               # (build 100b) after a reset
        else:
            s.send("press c")
    return False


WIN_REGROUP_LIST = 0x33


def house_policy(take=(), regroup=False):
    """Rolf's house. A newcomer is welcomed and keeps their name, and
    then -- since loc_B398 lands on the options menu -- REORGANIZE puts
    them in the party: `take` is the regroup list's stops to pick, in
    order (the list is everyone joined bar Rolf and Nei). With nobody
    new, YES to going on unchanged ($092E, $090F) -- or, with `regroup`, NO and
    REORGANIZE; on the options otherwise, OUTSIDE."""
    todo = list(take)
    seen_knock = [False]
    start = [None]
    done = [False]

    def policy(s):
        st, last = s.state, s.last_script
        if start[0] is None:
            start[0] = max(0, len(s.shown) - 8)
        if any(x in (0x090C, 0x0927) for x in s.shown[start[0]:]):
            seen_knock[0] = True
        if st["menu"] == WIN_YESNO3:
            if last in (0x090C, 0x0927) or 0x0913 <= last <= 0x0926:
                return 1                        # keep their name
            if last == 0x0911:
                return 0 if todo else 1         # anyone else?
            if last in (0x090F, 0x092E) and regroup and todo \
                    and not done[0]:
                return 1                        # not yet: the options
            return 0                            # out again
        if st["menu"] == WIN_HOUSE_OPTIONS:
            if (seen_knock[0] or regroup) and todo and not done[0]:
                seen_knock[0] = False
                done[0] = True
                return 1                        # REORGANIZE
            return 2                            # OUTSIDE
        if st["menu"] == WIN_REGROUP_LIST:
            return todo.pop(0) if todo else "b"
        return None
    return policy


def rolf_house(s, take=(), regroup=False):
    return visit(s, PASEO, PASEO_DOORS["rolf_house"],
                 house_policy(take, regroup))


def talk(s, limit=6):
    """A, then C through whatever answers, until the pad is back.
    Returns the scripts shown (empty when nobody was there)."""
    before = len(s.shown)
    s.send("press a", "wait idle")
    for _ in range(limit):
        st = s.state
        if st.get("screen") == "level" and st.get("kind") == "waiting" \
                and st.get("wait") == "control":
            break
        if st.get("kind") == "waiting":
            s.send("press c", "wait idle")
        else:
            s.send("wait idle")
    return s.shown[before:]


# ---------------------------------------------------------------------
# Things on the map: chests, bodies, anyone standing still.
# ---------------------------------------------------------------------

def neighbours(level, cell):
    """The open cells next to `cell`, with the direction back to it."""
    lm = level_map(level)
    out = {}
    for d, dx, dy in (("up", 0, 1), ("down", 0, -1), ("left", 1, 0),
                      ("right", -1, 0)):
        n = ((cell[0] + dx) % lm.w, (cell[1] + dy) % lm.h)
        if lm.open(*n):
            out[n] = d
    return out


def search(s, level, cell, log=print, key=True, cells=None):
    """Walk next to `cell` on `level` (travelling there first), face
    it, press A and C through what follows -- and, with `key`, the
    Small Key on a chest that says it is locked. Returns the scripts.
    `cells` overrides where to stand ({cell: direction})."""
    if cells is None:
        cells = neighbours(level, cell)
    if not cells:
        return None
    if not travel(s, level, cells, log=log):
        return None
    if not s.ready():
        return None
    here = (s.state["x"], s.state["y"])
    d = cells.get(here)
    if d is None:
        return None
    s.send(f"press {d}", "wait idle")        # turn to face it
    if s.state.get("kind") != "waiting" or s.state.get("wait") != "control":
        s.ready()
    shown = talk(s)
    if 0x1707 in shown and key:
        # "It's locked": the Small Key from the menu, facing it
        # (use_small_key: $01 then $17), and A again.
        k = find_item(s, "SmallKey")
        if k:
            shown += use_item(s, k[0], k[1]) or []
            s.ready()
            shown += talk(s)
    return shown


# loc_7278's rows (npc.c's BOX): left, width, top, height of the box an
# object's $2C names, from its x, y. Obj_InvisibleBlock ($37) sets $2C
# to 9 and Obj_InvisibleBlock2 ($38) to 7 (ps2.asm:7820); a person,
# chest or door is row 4, one cell.
BOX_ROWS = [(-8, 15, -24, 15), (-8, 15, 8, 15), (-24, 15, -8, 15),
            (8, 15, -8, 15), (-8, 15, -8, 15), (-16, 31, -16, 31),
            (-16, 31, -8, 8), (-16, 31, 0, 15), (-8, 63, -48, 95),
            (-32, 63, -64, 63), (-88, 176, -24, 40), (-32, 56, -8, 16),
            (-16, 31, -8, 7), (-16, 31, -8, 15)]
OBJ_BOX = {0x37: 9, 0x38: 7}


def object_cells(obj, x, y):
    """The cells an object's box covers, from its pixel x, y."""
    l, w, t, h = BOX_ROWS[OBJ_BOX.get(obj, 4)]
    return {(cx, cy)
            for cx in range((x + l) // 16, (x + l + w) // 16 + 1)
            for cy in range((y + t) // 16, (y + t + h) // 16 + 1)}


def event_block(level, index):
    """The cells of the invisible block ($37/$38) with ObjType_Events
    index `index` on a level, or None."""
    d = json.loads(Path("assets/data/level_sprites.json").read_text())
    for L in d["levels"]:
        if L["level"] != level:
            continue
        for o in L["objects"]:
            if o["object"] in OBJ_BOX and o["index"] == index:
                return object_cells(o["object"], o["x"], o["y"])
    return None


def facing_cells(level, cells):
    """Open cells outside `cells` next to one of them, with the
    direction that faces it."""
    lm = level_map(level)
    out = {}
    for c in cells:
        for d, dx, dy in (("up", 0, 1), ("down", 0, -1), ("left", 1, 0),
                          ("right", -1, 0)):
            n = ((c[0] + dx) % lm.w, (c[1] + dy) % lm.h)
            if n not in cells and lm.open(*n) and n not in out:
                out[n] = d
    return out


def search_event(s, level, index, log=print):
    """A on the invisible block with that ObjType_Events index."""
    box = event_block(level, index)
    if not box:
        log(f"no event block on {level_name(level)} (index {index})")
        return None
    return search(s, level, min(box), log=log, key=False,
                  cells=facing_cells(level, box))


def level_objects(level):
    """The objects LoadSpritesInLevel places on a level, by cell:
    (object type, interaction, index)."""
    d = json.loads(Path("assets/data/level_sprites.json").read_text())
    for L in d["levels"]:
        if L["level"] == level:
            return {(o["x"] // 16, o["y"] // 16):
                    (o["object"], o["interaction"], o["index"])
                    for o in L["objects"]}
    return {}


def chests(level):
    """Cells of the treasure chests on a level (objects 57 and 58)."""
    return [c for c, (obj, _, _) in level_objects(level).items()
            if obj in (57, 58)]


_CHEST_ITEMS = None


def chest_item(level, cell):
    """(build 106) The item in the chest at `cell` (chests.json's contents
    by the chest's index), or None: meseta, nothing, or no chest."""
    global _CHEST_ITEMS
    if _CHEST_ITEMS is None:
        _CHEST_ITEMS = {c["index"]: c.get("item") for c in json.loads(
            Path("assets/data/chests.json").read_text())["contents"]}
    obj, kind, index = level_objects(level).get(cell, (0, 0, 0))
    if obj not in (57, 58) or kind != 2:
        return None
    return _CHEST_ITEMS.get(index)


def flag_byte(s, addr):
    """(build 106) One byte of the $C700 block, read with the probe's
    `expect flag ADDR 0`: no FAIL is 0, a FAIL says what it is."""
    for ln in s.send(f"expect flag ${addr:04X} 0"):
        m = re.search(r"FAIL.* is \$([0-9A-F]{2})", ln)
        if m:
            return int(m.group(1), 16)
    return 0


def chest_shut(s, level, cell):
    """(build 106) The chest at `cell` is shut: its byte of
    treasure_chest_flags ($C780 + the chest's index) is 0, or $80 while
    it is locked (ObjType_TreasureChests; game.c, level_open_chest). Only
    a chest the A button opens (interaction 2) with an index: index 0 is
    "There is nothing inside" whatever the byte."""
    obj, kind, index = level_objects(level).get(cell, (0, 0, 0))
    if obj not in (57, 58) or kind != 2 or index == 0:
        return False
    return flag_byte(s, 0xC700 | ((0x80 + index) & 0xFF)) in (0x00, 0x80)


# ---------------------------------------------------------------------
# The C menu: ITEM, USE.
# ---------------------------------------------------------------------

WIN_MENU_ITEM_CHAR  = 0x02
WIN_MENU_ITEM_LIST  = 0x03
WIN_MENU_ITEM_LIST2 = 0x04     # the second page, items 8 up
WIN_ITEM_ACTION     = 0x05      # USE GIVE TOSS
WIN_CHOSEN_ITEM_CHAR = 0x06
MENU_ITEM = 0


def use_item(s, holder, slot, target=None):
    """C, ITEM, the holder, bag slot `slot`, USE, on `target` (a party
    position; the holder when None). Returns the scripts shown."""
    if not s.ready():
        return None
    before = len(s.shown)
    s.send("press c", "wait idle")
    if s.state.get("menu") != WIN_PLAYER_MENU:
        menu_escape(s)
        return None
    menu_pick(s, MENU_ITEM)
    s.send("wait idle")
    if s.state.get("menu") == WIN_MENU_ITEM_CHAR:
        menu_pick(s, holder)
        s.send("wait idle")
    if s.state.get("menu") == WIN_MENU_ITEM_LIST:
        list_pick(s, slot)
        s.send("wait idle")
    if s.state.get("menu") == WIN_ITEM_ACTION:
        menu_pick(s, 0)                         # USE
        s.send("wait idle")
    if s.state.get("menu") == WIN_CHOSEN_ITEM_CHAR:
        menu_pick(s, holder if target is None else target)
        s.send("wait idle")
    for _ in range(6):
        if s.state.get("wait") == "text":
            s.send("press c", "wait idle")
        elif s.state.get("kind") != "waiting":
            s.send("wait idle")
        else:
            break
    menu_escape(s)
    return s.shown[before:]


def toss_item(s, holder, slot):
    """(build 99) C, ITEM, the holder, bag slot `slot`, TOSS, YES
    (ItemAction_TossEntry: $000C "Do you really want to drop" for an item
    whose kind byte has bit 7 set, $000B "not a good idea" otherwise, and
    $000E once dropped). Returns the scripts shown."""
    if not s.ready():
        return None
    before = len(s.shown)
    s.send("press c", "wait idle")
    if s.state.get("menu") != WIN_PLAYER_MENU:
        menu_escape(s)
        return None
    menu_pick(s, MENU_ITEM)
    s.send("wait idle")
    if s.state.get("menu") == WIN_MENU_ITEM_CHAR:
        menu_pick(s, holder)
        s.send("wait idle")
    if s.state.get("menu") == WIN_MENU_ITEM_LIST:
        list_pick(s, slot)
        s.send("wait idle")
    if s.state.get("menu") == WIN_ITEM_ACTION:
        menu_pick(s, 2)                         # TOSS
        s.send("wait idle")
    for _ in range(6):
        if s.state.get("menu") == WIN_YESNO:
            menu_pick(s, 0)                     # YES
            s.send("wait idle")
        elif s.state.get("wait") == "text":
            s.send("press c", "wait idle")
        elif s.state.get("kind") != "waiting":
            s.send("wait idle")
        else:
            break
    menu_escape(s)
    return s.shown[before:]


# (build 99) What goes when a chest's find has no room ($1704, "But they
# can't hold any more"): the least of what a bag commonly holds, first.
TOSS_FIRST = ("Monomate", "Dimate", "Antidote", "Telepipe", "Escapipe",
              "Hidapipe", "Trimate")


def make_room(s, log=print, who=None):
    """Toss one of TOSS_FIRST from the fullest living member's bag (or
    party position `who`'s). Returns the name tossed, or None."""
    party = s.state.get("party") or []
    b = bags(s)
    order = sorted((h for h in range(min(len(b), len(party)))
                    if not party[h]["dead"] and (who is None or h == who)),
                   key=lambda h: -len(b[h]))
    for name in TOSS_FIRST:
        for h in order:
            slot = next((i for i in range(len(b[h]) - 1, -1, -1)
                         if b[h][i][0] == name and not b[h][i][1]), None)
            if slot is not None:
                shown = toss_item(s, h, slot) or []
                log(f"room: {party[h]['name']} tosses {name}:",
                    scripts_text(shown))
                return name
    return None


def give_item(s, holder, slot, to):
    """C, ITEM, the holder, bag slot `slot`, GIVE, to party position
    `to` (ItemAction_GiveEntry: $24 or $08 given, $0A full). Returns the
    scripts shown."""
    if not s.ready():
        return None
    before = len(s.shown)
    s.send("press c", "wait idle")
    if s.state.get("menu") != WIN_PLAYER_MENU:
        menu_escape(s)
        return None
    menu_pick(s, MENU_ITEM)
    s.send("wait idle")
    if s.state.get("menu") == WIN_MENU_ITEM_CHAR:
        menu_pick(s, holder)
        s.send("wait idle")
    if s.state.get("menu") == WIN_MENU_ITEM_LIST:
        list_pick(s, slot)
        s.send("wait idle")
    if s.state.get("menu") == WIN_ITEM_ACTION:
        menu_pick(s, 1)                         # GIVE
        s.send("wait idle")
    if s.state.get("menu") == WIN_CHOSEN_ITEM_CHAR:
        menu_pick(s, to)
        s.send("wait idle")
    for _ in range(6):
        if s.state.get("wait") == "text":
            s.send("press c", "wait idle")
        elif s.state.get("kind") != "waiting":
            s.send("wait idle")
        else:
            break
    menu_escape(s)
    return s.shown[before:]


def hands(s):
    """(build 99) What each member holds, by party position: [right,
    left] ItemID_ names, None for an empty hand (a two-hander is in
    both). The probe's `hands`: bags() says a piece is worn, not in which
    hand."""
    out = s.send("hands")
    for ln in out:
        m = re.search(r"ok\s+hands (.*)$", ln)
        if m:
            result = []
            for part in m.group(1).split("; "):
                pair = part.split(":", 1)[1].split()
                result.append([None if n == "-" else n for n in pair[:2]])
            return result
    return []


def bags(s):
    """Each member's bag, by party position: a list of (ItemID_ name,
    worn) lists."""
    out = s.send("bags")
    for ln in out:
        m = re.search(r"ok\s+bags (.*)$", ln)
        if m:
            result = []
            for part in m.group(1).split("; "):
                items = part.split(":", 1)[1].split()
                result.append([(it.rstrip("*"), it.endswith("*"))
                               for it in items])
            return result
    return []


# ---------------------------------------------------------------------
# The C menu: TECH. Map techniques, as Menu_TechEntry lists them: the
# first `techs_b` names of the character's map list, in order.
# ---------------------------------------------------------------------

WIN_LEVEL_TECH_LIST = 0x12
MENU_TECH = 2
_CHARS = None
_TECHS = None


def _chars():
    global _CHARS
    if _CHARS is None:
        _CHARS = json.loads(Path("assets/data/characters.json").read_text())
    return _CHARS


def _techs():
    global _TECHS
    if _TECHS is None:
        _TECHS = json.loads(Path("assets/data/techniques.json").read_text())
    return _TECHS


def map_techs(name, level):
    """The map techniques `name` knows at `level`, as (stop, key, tp)
    in list order: techs_b summed over the level table indexes the
    character's map list (CharAvailableTechniques)."""
    for c in _chars():
        if c["name"].upper() != name.upper():
            continue
        known = sum(r["techs_b"] for r in c["levels"] if r["level"] <= level)
        names = _techs()["available"][c["name"].capitalize()]["map"][:known]
        cost = {tt["key"]: tt["tp"] for tt in _techs()["techniques"]}
        return [(i, k, cost.get(k, 0)) for i, k in enumerate(names)]
    return []


def find_tech(s, key):
    """(party position, list stop, tp cost) of the first living member
    who knows map technique `key` and has the TP for it, or None."""
    for i, m in enumerate(s.state.get("party") or []):
        if m["dead"]:
            continue
        for stop, k, tp in map_techs(m["name"], m["level"]):
            if k == key and m["tp"] >= tp:
                return (i, stop, tp)
    return None


def use_tech(s, caster, stop, target=None):
    """C, TECH, the caster, technique `stop` in the list, ON the target
    (a party position; the caster when None). Returns the scripts."""
    if not s.ready():
        return None
    before = len(s.shown)
    s.send("press c", "wait idle")
    if s.state.get("menu") != WIN_PLAYER_MENU:
        menu_escape(s)
        return None
    menu_pick(s, MENU_TECH)
    s.send("wait idle")
    if s.state.get("menu") == WIN_MENU_ITEM_CHAR:
        menu_pick(s, caster)
        s.send("wait idle")
    if s.state.get("menu") == WIN_LEVEL_TECH_LIST:
        # (build 99) Nine or more map techniques are two pages, NEXT at
        # stop 0 above the first eight (Win_LevelTechList as ported,
        # window.c), as an item list: list_pick. menu_pick(stop) picked
        # the technique above the one meant, and NEXT for the first --
        # Amy at L34 (ten) cast Nares on a dead Rolf for Rever ("is dead",
        # $0102) after Dark Force in seed 11, and her Res was NEXT.
        list_pick(s, stop)
        s.send("wait idle")
    if s.state.get("menu") == WIN_CHOSEN_ITEM_CHAR:
        menu_pick(s, caster if target is None else target)
        s.send("wait idle")
    for _ in range(8):
        if s.state.get("wait") == "text":
            s.send("press c", "wait idle")
        elif s.state.get("kind") != "waiting":
            s.send("wait idle")
        else:
            break
    menu_escape(s)
    return s.shown[before:]


def cast_res(s, below=0.6, limit=20):
    """Res (20 HP for 3 TP, tech_res) from whoever knows it on whoever
    is alive and below `below` of their HP, until nobody is or the TP
    runs out. Returns how many casts."""
    used = 0
    for _ in range(limit):
        if not s.ready():
            break
        party = s.state.get("party") or []
        hurt = sorted((i for i, m in enumerate(party)
                       if not m["dead"] and m["hp"] < m["max_hp"] * below),
                      key=lambda i: party[i]["hp"] / party[i]["max_hp"])
        if not hurt:
            break
        who = find_tech(s, "Gires") if party[hurt[0]]["max_hp"] - \
            party[hurt[0]]["hp"] >= 60 else None
        who = who or find_tech(s, "Res")
        if who is None:
            break
        use_tech(s, who[0], who[1], hurt[0])
        used += 1
    return used


MATES = {"Monomate": 20, "Dimate": 60, "Trimate": 999}

# (build 169) What plan_gear may hand from a full bag to make room for a
# purchase: the consumables, which the driver finds in any bag
# (find_item, carried, count_mates). Never a key item or a piece of gear,
# nor a Moon Dew: which bag holds those matters (story.reserve_dews).
STOWABLE = set(MATES) | {"Antidote", "StarMist", "Telepipe", "Escapipe",
                         "Hidapipe"}


def mend(s, below=0.6):
    """Use Monomates/Dimates from anyone's bag on whoever is alive and
    below `below` of their HP, until nobody is or they run out.
    Returns how many were used."""
    used = cast_res(s, below)            # TP before the bag
    for _ in range(12):
        if not s.ready():
            break
        party = s.state.get("party") or []
        hurt = [i for i, m in enumerate(party)
                if not m["dead"] and m["hp"] < m["max_hp"] * below]
        if not hurt:
            break
        found = None
        for holder, bag in enumerate(bags(s)):
            if holder < len(party) and party[holder]["dead"]:
                continue                        # "{char} is dead."
            for slot, (name, worn) in enumerate(bag):
                if name in MATES and not worn:
                    found = (holder, slot)
                    break
            if found:
                break
        if found is None:
            break
        use_item(s, found[0], found[1], hurt[0])
        used += 1
    return used


def revive(s, limit=4, spend_reserve=False):
    """(build 96c) Bring the dead back on the map: Rever (30 HP, the map
    technique Rolf and Amy learn; tech_rever in menu.c) from whoever knows
    it and has the TP, else a Moon Dew from a living member's bag, on each
    dead member in turn. Before this the harness had no way to revive
    between fights: a member down on a walk stayed down until a hospital
    or Lutz. Returns how many came back.

    (build 99) The last `s.dew_reserve` Moon Dews are left alone unless
    `spend_reserve`: on Noah they are for the Nei Sword's holder, the one
    death the party cannot walk home from (seed 2 won Dark Force with
    nobody able to raise him)."""
    back = 0
    tech_failed = False
    for _ in range(limit):
        if not s.ready():
            break
        party = s.state.get("party") or []
        dead = [i for i, m in enumerate(party) if m["dead"]]
        if not dead:
            break
        tech = None if tech_failed else find_tech(s, "Rever")
        if tech:
            use_tech(s, tech[0], tech[1], target=dead[0])
        else:
            dew = find_item(s, "MoonDew")
            keep = 0 if spend_reserve else getattr(s, "dew_reserve", 0)
            if not dew or carried(s, "MoonDew") <= keep:
                break
            use_item(s, dew[0], dew[1], target=dead[0])
        s.send("wait idle")
        s.ready()
        after = s.state.get("party") or []
        if dead[0] >= len(after) or after[dead[0]]["dead"]:
            if tech and not tech_failed:
                tech_failed = True      # (build 99) a Moon Dew next
                continue
            break
        back += 1
    return back


def count_mates(s):
    return sum(1 for bag in bags(s) for name, worn in bag if name in MATES)


def find_item(s, item):
    """(holder, slot) of the first `item` on a living member's first
    page, or None."""
    party = s.state.get("party") or []
    for holder, bag in enumerate(bags(s)):
        if holder < len(party) and party[holder]["dead"]:
            continue
        for slot, (name, worn) in enumerate(bag):
            if name == item and not worn:
                return (holder, slot)
    return None


# ---------------------------------------------------------------------
# The clone lab, and retreating to town when someone has died.
# ---------------------------------------------------------------------

def clone_policy(s):
    """Revive everyone dead, one at a time: the char list is the party
    in order; $403 asks for the money (YES), $405 whether anyone else
    (YES while someone is still dead)."""
    st, last = s.state, s.last_script
    dead = [i for i, m in enumerate(st.get("party") or []) if m["dead"]]
    if st["menu"] == WIN_STORE_CHARS:
        return dead[0] if dead else "b"
    if st["menu"] == WIN_YESNO2:
        if last == 0x0403:
            return 0
        return 0 if dead else 1
    return None


def clone_lab(s, level=PASEO, door=PASEO_DOORS["clone_lab"]):
    return visit(s, level, door, clone_policy)


# Dungeons with a way out on foot for when Hinas and the Escapipe are
# not there: Climatrol's floors and the Underwater Passage, down to the
# ground floor's door (82,6), through the passage and out by its pipe
# (110,24) onto the water at the ocean-floor spot, riding.
WALK_OUT = {lv: (0x00, (159, 15)) for lv in [0x13] + list(range(0x36, 0x3E))}


def pipe_town(s):
    """(build 105) The town a Telepipe or Ryuka takes the party to from
    the level it is on (ItemAction_Telepipe, loc_9E86; docs/game.md, "The
    $C600 block"): the last save's place when it was made on this planet;
    on Motavia after a save made on Dezolis, Paseo (by the Central Tower's
    door, 31,16). s.save_town alone sent retreat() after a Dezolis town it
    could not walk to from Motavia. The other way -- on Dezolis after a
    Motavia save, the pipe lands on the Skure field by the spaceship -- has
    no town to name and keeps s.save_town, as before."""
    level = s.state.get("level")
    here_motavia = level is not None and level < 0x10 \
        and level not in DEZOLIS_LEVELS         # Motavia's field or a town
    if here_motavia and s.save_town in DEZOLIS_LEVELS:
        return PASEO
    return s.save_town


def retreat(s, town=None, log=print):
    """Back to town for the clone lab and the hospital; True when the
    whole party is alive and full again on the town's street. Getting
    there is done whatever the state of the party: there is nowhere
    else to go. Out of a dungeon by Hinas or the Escapipe (to where it
    was entered, $C64C); on the field Ryuka or the Telepipe, which land
    in the town of the last save ($C646). `town` defaults to that one."""
    explicit = town
    if town is None:
        town = s.save_town
    careful, s.careful, s.hurt = s.careful, False, False
    try:
        if not s.ready():
            return False
        # An Escapipe out of a dungeon, a Telepipe from the field: both
        # $12 then $27 when they cannot work here (use_telepipe,
        # use_escapipe).
        # Hinas / Ryuka (tech_entry shares use_escapipe / use_telepipe)
        # when someone has the TP, else the pipe.
        if s.state.get("level", 0) >= 0x10:
            tech = find_tech(s, "Hinas")
            pipe = find_item(s, "Escapipe")
            if tech:
                use_tech(s, tech[0], tech[1])
                s.ready()
            elif pipe:
                use_item(s, pipe[0], pipe[1])
                s.ready()
        if s.state.get("level") in WALK_OUT:
            # No Hinas and no Escapipe (none is sold before Climatrol, and
            # only Shir learns Hinas): out on foot.
            lv, cell = WALK_OUT[s.state["level"]]
            if not travel(s, lv, {cell}, log=log):
                log("retreat: could not walk out of",
                    level_name(s.state.get("level")))
        if explicit is None:
            # (build 105) Where the pipe lands from here, now that any
            # dungeon is behind: the last save's town on its own planet,
            # Paseo on Motavia after a save on Dezolis.
            town = pipe_town(s)
        if s.state.get("riding") and s.state.get("level") == MOTAVIA:
            # Out of a dungeon onto the water (Climatrol, by way of the
            # Underwater Passage): Ryuka and the Telepipe clear
            # jet_scooter_flag and nothing else (loc_9E86), which would
            # leave the scooter parked at sea with the party in town on
            # foot. Ashore first, on ground that walks to the town.
            # The ground the town's exit lands on -- not the landing
            # cell itself or its neighbours: parked on the cell the exit
            # lands on, the scooter's own footprint is where the party
            # comes out, and A there is $1708, not the scooter.
            e = town_exits().get(town)
            region = set()
            if e is not None:
                landing = (e["to_x"], e["to_y"])
                region = walk_region(MOTAVIA, {landing})
                near = {(landing[0] + dx, landing[1] + dy)
                        for dx in range(-3, 4) for dy in range(-3, 4)}
                if region - near:
                    region = region - near
            if not region:
                region = landing_region(town, set())
            log("retreat: ashore toward", town, "-- region", len(region),
                "cells")
            if region and not ride_ashore(s, None, log=log, region=region):
                log("retreat: could not get ashore")
        if s.state.get("screen") not in ("level", "battle"):
            log("retreat: off the map --", s.state.get("screen"))
            return False
        if (s.state.get("level") or 0) <= 3 and s.state.get("level") != town:
            tech = find_tech(s, "Ryuka")
            pipe = find_item(s, "Telepipe")
            if tech:
                use_tech(s, tech[0], tech[1])
                s.ready()
            elif pipe:
                use_item(s, pipe[0], pipe[1])
                s.ready()
        if not enter_town(s, town):
            log("retreat: could not reach town")
            return False
        b = town_buildings(town)
        if any(m["dead"] for m in s.state.get("party") or []) \
                and BUILDING_CLONE_LAB in b:
            clone_lab(s, level=town, door=b[BUILDING_CLONE_LAB][0])
        heal_here(s, town)
        if BUILDING_ITEM in b:
            resupply(s, door=b[BUILDING_ITEM][0], level=town)
    finally:
        s.careful = careful
    return all(not m["dead"] for m in s.state.get("party") or []) \
        and s.state.get("level") == town


def resupply(s, want=8, door=PASEO_DOORS["item_store"], level=PASEO):
    """Top the party up to `want` mates from the item store -- Dimates
    while the money lasts, Monomates after -- into the bags with room
    on their first page (mend only reaches the first eight slots)."""
    party = s.state.get("party") or []
    have = count_mates(s)
    room = [max(0, 8 - len(bag)) for bag in bags(s)]
    money = s.state.get("meseta") or 0
    todo = []
    m = 0
    # One Escapipe and one Telepipe, the way home, before the mates.
    for name, stock, price in (("Escapipe", 4, 70), ("Telepipe", 3, 130)):
        if find_item(s, name) is None and money >= price + 100:
            for who in range(len(party)):
                if room[who] > 0:
                    room[who] -= 1
                    todo.append((stock, who))
                    money -= price
                    break
    while have + len(todo) < want and any(room) and money >= 20:
        while room[m % len(party)] == 0:
            m += 1
        room[m % len(party)] -= 1
        if money >= 60 + 60 * 2:
            todo.append((1, m % len(party)))    # Dimate
            money -= 60
        else:
            todo.append((0, m % len(party)))    # Monomate
            money -= 20
        m += 1
    if todo:
        buy(s, door, todo, level=level)
    return len(todo)


# ---------------------------------------------------------------------
# Gearing up: what a player does every time they are back in town with
# money -- the best weapon and armour the stores sell that each member
# can wear, bought, put on, and the old piece sold. Then out again.
# ---------------------------------------------------------------------

CHAR_ID = {"Rolf": 0, "Nei": 1, "Rudo": 2, "Amy": 3, "Hugh": 4, "Anna": 5,
           "Kain": 6, "Shir": 7}

# InventoryData's kind byte (bits 0-2 of action_flags): where it is worn.
KIND_HEAD, KIND_HAND, KIND_BOTH, KIND_BODY, KIND_LEGS = 1, 2, 3, 4, 5

_ITEMS = None
_STORES = None


def items():
    """items.json by ItemID_ key."""
    global _ITEMS
    if _ITEMS is None:
        d = json.loads(Path("assets/data/items.json").read_text())
        _ITEMS = {i["key"]: i for i in (d["items"] if isinstance(d, dict)
                                          else d)}
    return _ITEMS


def store_stock(index):
    """A store's shelf (stores.json), in cursor order."""
    global _STORES
    if _STORES is None:
        d = json.loads(Path("assets/data/stores.json").read_text())
        _STORES = {s["index"]: s["items"] for s in d["stores"]}
    return _STORES.get(index, [])


BUILDING_HOSPITAL, BUILDING_WEAPON, BUILDING_ARMOR, BUILDING_ITEM = 4, 5, 6, 7
BUILDING_CLONE_LAB = 3


def town_buildings(level):
    """A town's doors by building number: {building: (door, f764,
    f766)}. For a store f766 is its stores.json index and f764 its
    shelf's length; for the hospital f764 is the price a head."""
    out = {}
    for d in level_doors(level).values():
        b = d.get("building")
        if b is not None and b not in out:
            out[b] = ((d["y"], d["x"]), d.get("f764", 0), d.get("f766", 0))
    return out


def kind_of(name):
    it = items().get(name)
    return (it["action_flags"] & 7) if it else 0


def wearable(name, char_id):
    it = items().get(name)
    return bool(it) and bool(it["equip_mask"] & (1 << char_id))


def hands_score(names):
    """How good a pair of hands is: attack counts double, defence once."""
    a = sum(items()[n]["attack"] for n in names)
    d = sum(items()[n]["defense"] for n in names)
    return 2 * a + d


# (build 99) SetCharEquipment's loc_AD4C adds every worn piece's attack
# and defence, then -- for footwear from Shoes to Garda Boots in the legs
# slot -- AgilityIncreasesArray's agility. The party's agility is read by
# the round's order (loc_F004: agility + 0-15, fastest first) and by Dark
# Force's curse (the slowest living member); nothing rolls an enemy's blow
# against it. An agility point counts half a defence point, as in
# member_value.
AGILITY_WEIGHT = 0.5
_AGILITY = None


def footwear_agility(name):
    """AgilityIncreasesArray's entry for a piece of footwear, else 0."""
    global _AGILITY
    if _AGILITY is None:
        _AGILITY = json.loads(Path("assets/data/agility_bonus.json")
                              .read_text())
    return _AGILITY.get(name, 0)


def piece_score(name):
    """How good a head, body or legs piece is, on the scale hands_score
    uses: attack double, defence once (Knife and Long Boots carry attack
    too), and the footwear's agility at AGILITY_WEIGHT. Up to build 98
    this was the defence alone, so Boots, Knife Boots, Long Boots and
    Shune Boots -- all defence 7 -- tied, and Anna kept her Boots."""
    it = items()[name]
    return (2 * it["attack"] + it["defense"]
            + AGILITY_WEIGHT * footwear_agility(name))


def loadout(bag):
    """What a member wears, from a bags() entry: {kind: [names]} --
    KIND_HAND holds up to two (a two-hander is one name under
    KIND_BOTH)."""
    out = {KIND_HEAD: [], KIND_HAND: [], KIND_BOTH: [], KIND_BODY: [],
           KIND_LEGS: []}
    for name, worn in bag:
        if worn:
            out.setdefault(kind_of(name), []).append(name)
    return out


def sold_anywhere():
    """Every item some store sells: the replaceable kind. Anything else
    in a bag -- a chest's find, something Shir brought back -- is kept
    even when it comes off, since no shop restocks it."""
    global _STORES
    store_stock(0)
    return {n for names in _STORES.values() for n in names}


# Members whose head plan_gear leaves alone (build 85). On Dezolis Rolf's
# cap is what the people (ObjType_People's loc_E584), the shops, the
# hospital and Data Memory (CheckPlanetAndCaps) answer to; a better helmet
# would take it off. A stage sets it: PIN_HEAD.add("Rolf").
PIN_HEAD = set()
DEZOLIS_LEVELS = {0x01, 0x02, 0x03, 0x0A, 0x0B, 0x0C}


def worn_head(s, name):
    """The head piece `name` (a member) wears now, or None."""
    party = s.state.get("party") or []
    for m, bag in enumerate(bags(s)):
        if m < len(party) and party[m]["name"] == name:
            for n, worn in bag:
                if worn and kind_of(n) == KIND_HEAD:
                    return n
    return None


def price_factor(s, level):
    """CheckIfDoublePrice (ps2.asm:19203): on Dezolis a Magic Cap on
    Rolf's head doubles every price; a Mogic Cap does not."""
    if level in DEZOLIS_LEVELS and worn_head(s, "Rolf") == "MagicCap":
        return 2
    return 1


def plan_gear(s, level, reserve=120, log=print, unlimited=False):
    """What to put on in this town, best gain first: [(source, member,
    name, old names)], the source ("bag", slot) for a piece already
    carried -- a chest's, one Shir stole, one bought earlier -- or
    (building, stock index) for a purchase within the money less
    `reserve`. Only upgrades: a piece that beats what is worn in its
    slot, or a pair of hands that beats the pair worn now. What is
    carried counts first and costs nothing, so a unique find is never
    passed over for something weaker from a shelf. A piece in another
    member's bag that this one could wear is ("give", holder, slot):
    GIVE first, then EQP. (build 169) A purchase only into a bag with
    room, and ("stow", holder, slot) to the member with the most room for
    a full buyer's consumable (STOWABLE): GIVE, and nothing put on. With
    `unlimited`, every upgrade the shelves hold, whatever the money or
    the room -- what is still worth grinding for."""
    party = s.state.get("party") or []
    money = (s.state.get("meseta") or 0) - reserve
    if unlimited:
        money = 1 << 30
    b = town_buildings(level)
    shelf = {}
    for building in (BUILDING_WEAPON, BUILDING_ARMOR):
        if building in b:
            _, count, store = b[building]
            shelf[building] = store_stock(store)[:count]
    all_bags = bags(s)

    factor = price_factor(s, level)

    def price(name):
        return items()[name]["price"] * factor

    options = []
    for m, member in enumerate(party):
        cid = CHAR_ID.get(member["name"])
        if cid is None or member["dead"] or m >= len(all_bags):
            continue
        bag = all_bags[m]
        have = loadout(bag)

        def sources(pred):
            """(source, name, cost) for every wearable piece `pred`
            accepts: unworn ones in the bag first, then the shelves."""
            for slot, (name, worn) in enumerate(bag):
                if not worn and name in items() and wearable(name, cid) \
                        and pred(name):
                    yield ("bag", slot), name, 0
            if len(bag) < 16:
                for h, other in enumerate(all_bags):
                    if h == m or h >= len(party) or party[h]["dead"]:
                        continue
                    for slot, (name, worn) in enumerate(other):
                        if not worn and name in items() \
                                and wearable(name, cid) and pred(name):
                            yield ("give", h, slot), name, 0
            for building, names in shelf.items():
                for i, name in enumerate(names):
                    if name in items() and wearable(name, cid) \
                            and pred(name):
                        yield (building, i), name, price(name)

        # head, body, legs: the best piece to be had (piece_score: attack,
        # defence, footwear agility) beats what is worn
        for kind in (KIND_HEAD, KIND_BODY, KIND_LEGS):
            if kind == KIND_HEAD and member["name"] in PIN_HEAD:
                continue
            worn = have.get(kind, [])
            cur = max((piece_score(n) for n in worn), default=0)
            best = None
            for src, name, cost in sources(lambda n: kind_of(n) == kind):
                gain = piece_score(name) - cur
                if gain > 0 and (best is None or gain > best[0]
                                 or (gain == best[0] and cost < best[2])):
                    best = (gain, src, cost, name)
            if best:
                gain, src, cost, name = best
                options.append((gain, cost, src, m, name, worn))

        # hands: the best two-hander, or the best one-hander in each hand
        # (a shield is a one-hander with no attack), against what the two
        # hands hold now. A piece can only be used once, so a pair of the
        # same name needs two of it.
        worn = have.get(KIND_HAND, []) + have.get(KIND_BOTH, [])
        cur = hands_score(worn)
        pool = {}                       # name -> [(src, cost), ...] cheapest first
        for src, name, cost in sources(
                lambda n: kind_of(n) in (KIND_HAND, KIND_BOTH)):
            pool.setdefault(name, []).append((src, cost))
        for name in pool:
            pool[name].sort(key=lambda sc: sc[1])
            shelf_src = [sc for sc in pool[name]
                         if sc[0][0] not in ("bag", "give")]
            if shelf_src:
                pool[name].append(shelf_src[0])     # a shop sells two
        # (build 170) Equal scores by name: they came in the set's order,
        # which is the hash's.
        ones = sorted({n for n in pool if kind_of(n) == KIND_HAND}
                      | {n for n in worn if kind_of(n) == KIND_HAND},
                      key=lambda n: (-hands_score([n]), n))[:6]
        twos = [n for n in pool if kind_of(n) == KIND_BOTH]
        pairs = [[t] for t in twos] + [[a] for a in ones]
        for i, a in enumerate(ones):
            for c in ones[i:]:
                pairs.append([a, c])
        best = None
        for pair in pairs:
            gain = hands_score(pair) - cur
            need = list(pair)
            for n in worn:
                if n in need:
                    need.remove(n)
            if gain <= 0 or not need:
                continue
            picks, cost, ok = [], 0, True
            taken = {}
            for n in need:
                k = taken.get(n, 0)
                if n not in pool or k >= len(pool[n]):
                    ok = False
                    break
                src, c = pool[n][k]
                taken[n] = k + 1
                picks.append((src, n))
                cost += c
            if not ok:
                continue
            if best is None or gain / max(1, cost) > best[0] / max(1, best[1]):
                best = (gain, cost, picks, pair)
        if best:
            gain, cost, picks, pair = best
            old = [n for n in worn if n not in pair]
            for src, n in picks:
                options.append((gain, price(n) if src[0] not in ("bag", "give")
                                else 0, src, m, n, old))
                old = []                        # sold once

    options.sort(key=lambda o: (-1 if o[1] == 0 else 0,
                                -o[0] / max(1, o[1])))
    todo = []
    claimed = set()                 # a carried piece goes to one member

    # (build 169) Room in the bags. A store hands a purchase for a full
    # bag -- sixteen -- "no room" and shuts (equipment_store, EQ_CHOSEN:
    # $0609, $0709), so the rest of the list went unbought ("not
    # bought?"): Rolf's two Laser Knives filled his bag in Zema and his
    # Boots shut the armour store on Hugh's and Rudo's too; Kain's Trimates
    # kept the Garda Boots off him in Ryuon at every visit. gear_up GIVEs
    # before it buys, so a piece given away frees its slot for a purchase,
    # and a buyer still full hands a consumable to the member with the
    # most room first (a "stow"). Receivers are counted without the slots
    # they free, as a GIVE runs before or after another. Not with
    # `unlimited`: what is worth grinding for is as it was.
    free = [16 - len(bag) for bag in all_bags]
    taken_in = [0] * len(all_bags)      # given, stowed and bought in
    given_out = [0] * len(all_bags)     # given and stowed away

    def can_take(r):
        return free[r] - taken_in[r] > 0

    def stow_from(b):
        """A GIVE that makes room in member b's bag: b's unworn
        consumable (STOWABLE; a mate last, as mend reaches only the first
        eight slots) to the living member with the most room, or None."""
        to = [r for r in range(min(len(party), len(all_bags)))
              if r != b and not party[r]["dead"] and can_take(r)]
        picks = [k for k, (n, w) in enumerate(all_bags[b])
                 if not w and n in STOWABLE and (b, k) not in claimed]
        if not to or not picks:
            return None
        r = max(to, key=lambda r: (free[r] - taken_in[r], -r))
        k = max(picks, key=lambda k: (all_bags[b][k][0] not in MATES, k))
        claimed.add((b, k))
        taken_in[r] += 1
        given_out[b] += 1
        return (("stow", b, k), r, all_bags[b][k][0], [])

    for gain, cost, src, m, name, old in options:
        if src[0] == "give":
            key = (src[1], src[2])
        elif src[0] == "bag":
            key = (m, src[1])
        else:
            key = None
        if key is not None and key in claimed:
            continue
        if cost > money:
            continue
        if not unlimited and src[0] == "give" and not can_take(m):
            continue
        if not unlimited and src[0] not in ("bag", "give") \
                and free[m] - taken_in[m] + given_out[m] <= 0:
            stow = stow_from(m)
            if stow is None:
                continue
            todo.append(stow)
        money -= cost
        todo.append((src, m, name, old))
        if src[0] == "give":
            taken_in[m] += 1
            given_out[src[1]] += 1
        elif src[0] != "bag":
            taken_in[m] += 1
        if key is not None:
            claimed.add(key)
    if todo and not unlimited:
        log("gear: " + ", ".join(
            f"{party[m]['name']} {name}"
            + (" (carried)" if src[0] == "bag" else
               f" (from {party[src[1]]['name']})" if src[0] == "give" else
               f" (from {party[src[1]]['name']}, for room)"
               if src[0] == "stow" else "")
            for src, m, name, _ in todo))
    return todo


def sell_policy(sales):
    """sales: [(party member, bag slot), ...], each member's slots in
    descending order -- selling closes the gap in the bag. SELL, whose,
    what, YES to the offer ($0803), and YES to \"anything else?\"
    ($080C) while there is more."""
    todo = list(sales)

    def policy(s):
        st, last = s.state, s.last_script
        if st["menu"] == WIN_BUYSELL:
            return 1 if todo else "b"           # SELL
        if st["menu"] == WIN_STORE_STOCK:
            return "b"                          # a shop that only sells
        if st["menu"] == WIN_STORE_CHARS:
            return todo[0][0] if todo else "b"
        if st["menu"] in (WIN_MENU_ITEM_LIST, WIN_MENU_ITEM_LIST2) \
                and last in (0x0805, 0x080E) and todo:
            # loc_BE8A: bit 7 of the item's kind byte clear -- a key
            # item -- is "I can't give you a price ..." ($0805; Teim
            # $080E) and the list again. Skip it rather than offer it
            # for ever (build 73 did, in Zema's item store).
            todo.pop(0)
            s.last_script = 0
        if st["menu"] == WIN_MENU_ITEM_LIST:
            # Win_MenuItemList: eight or fewer, one stop each; nine or
            # more, NEXT at stop 0 above slots 0-7. Slot 8 and up is on
            # the second page (list_pick does the same in the C menu).
            if not todo:
                return "b"
            slot = todo[0][1]
            if st.get("menu_stops") != 9:
                return slot
            return slot + 1 if slot < 8 else 0          # 0: NEXT
        if st["menu"] == WIN_MENU_ITEM_LIST2:
            # Win_MenuItemList2 (23095): stop 0 back, slots 8 up after.
            if not todo:
                return "b"
            slot = todo[0][1]
            return slot - 8 + 1 if slot >= 8 else 0
        if st["menu"] == WIN_YESNO2:
            if last == 0x0803:
                return 0                        # the offer: yes
            if todo:
                todo.pop(0)
            return 0 if todo else 1             # anything else?
        return None
    return policy


def sell(s, door, sales, level=PASEO):
    # Each sale is some eight menu and text steps: visit's default of 80
    # ran out part way through eleven in Zema, and what came next pressed
    # C blind in the store (see ready).
    return visit(s, level, door, sell_policy(sales), limit=40 + 12 * len(sales))


def rolf_alive_for_shops(s, level):
    """CheckPlanetAndCaps (ps2.asm:19188): on Dezolis every shop and the
    Data Memory refuse a dead Rolf ($810). The port has honoured it
    since build 104 (the planet set on every level load); before, it
    took Dezolis for Motavia and a dead Rolf shopped. So on Dezolis,
    Rolf dead, the town's clone lab first. True when the shops will
    trade (the cap is the caller's)."""
    if level not in DEZOLIS_LEVELS:
        return True
    party = s.state.get("party") or []
    if not party or not any(m["name"] == "Rolf" and m["dead"] for m in party):
        return True
    b = town_buildings(level)
    if BUILDING_CLONE_LAB in b:
        clone_lab(s, level=level, door=b[BUILDING_CLONE_LAB][0])
    party = s.state.get("party") or []
    return not any(m["name"] == "Rolf" and m["dead"] for m in party)


def gear_up(s, level, reserve=120, log=print):
    """Buy the town's upgrades the money allows, put on the best pieces
    carried -- bought, found in a chest, stolen by Shir -- and sell
    what they replaced, if a store sells its like. Returns what was put
    on."""
    if not s.ready():
        return []
    rolf_alive_for_shops(s, level)
    todo = plan_gear(s, level, reserve, log)
    b = town_buildings(level)
    # Pieces carried by someone else, and (build 169) the consumables
    # stowed to make room: GIVE, before the purchases (a full bag is sold
    # nothing), highest slot first per holder so the lower slots do not
    # move under the next one.
    gives = sorted(((src[1], src[2], m, name) for src, m, name, _ in todo
                    if src[0] in ("give", "stow")),
                   key=lambda g: (g[0], -g[1]))
    for holder, slot, m, name in gives:
        shown = give_item(s, holder, slot, m) or []
        if not ({0x24, 0x08} & set(shown)):
            log(f"gear: could not give {name} to "
                f"{s.state['party'][m]['name']}: "
                + scripts_text(shown))
    for building in (BUILDING_WEAPON, BUILDING_ARMOR):
        purchases = [(src[1], m) for src, m, _, _ in todo
                     if src[0] == building]
        if purchases:
            with s.buying_gear():
                buy(s, b[building][0], purchases, level=level)
    # Put each piece on: the last unworn one of its name in the bag (a
    # purchase lands at the end).
    for src, m, name, old in todo:
        if src[0] == "stow":
            continue
        bag = bags(s)
        if m >= len(bag):
            continue
        slots = [i for i, (n, worn) in enumerate(bag[m]) if n == name
                 and not worn]
        if not slots:
            planned = sum(1 for src2, m2, n2, _ in todo
                          if m2 == m and n2 == name and src2[0] != "stow")
            if sum(1 for n, w in bag[m] if w and n == name) < planned:
                log(f"gear: {name} is not in {s.state['party'][m]['name']}'s "
                    "bag -- not bought?")
            continue                # already on: the other hand took it
        hand = 0
        if kind_of(name) == KIND_HAND:
            hand = hand_for(s, m, todo, bag[m])
        shown = equip(s, m, slots[-1], hand) or []
        if 0x202 not in shown:
            log(f"gear: {s.state['party'][m]['name']} could not put on "
                f"{name}: " + scripts_text(shown))
            continue
        if kind_of(name) == KIND_HAND:
            # A safety net from before hand_for knew the hands: the piece
            # that was to come off is still worn, or -- a second of the
            # same name put over the first -- nothing changed at all
            # (loc_AC82 clears the first worn entry of the old id, which
            # is the new one). Put it on again, into the other hand.
            worn_now = sum(1 for n, w in bag[m] if w and n == name)
            bag = bags(s)
            still_old = [n for n, w in bag[m] if w and n in old]
            again = [i for i, (n, w) in enumerate(bag[m]) if n == name
                     and not w]
            gained = sum(1 for n, w in bag[m] if w and n == name) > worn_now
            if again and (still_old or not gained):
                equip(s, m, again[-1], 1 - hand)
    sales = surplus(s)
    if sales:
        # Only the item store buys (the equipment stores have no SELL:
        # equipment_store, loc_BAA8 on). A town without one -- Arima --
        # keeps the old pieces until the next town that has one.
        store = b.get(BUILDING_ITEM)
        if store:
            sell(s, store[0], sales, level=level)
    return todo


def hand_for(s, m, todo, bag):
    """(build 99) The hand (0 right, 1 left) a one-hander in gear_up's
    `todo` goes into for party position m: an empty hand, else the one
    holding a piece the plan takes off (the `old` of m's hand entries),
    else the right.

    Up to build 98 it was "the empty hand, else the first", so a pair
    that kept one piece could lose the wrong one: Anna, with the Nei Emel
    and an Ac Slasher on and the Nei Slasher carried, was planned the Nei
    Slasher and the Nei Emel; the Nei Slasher went over the Nei Emel (the
    right hand), the next town put the Nei Emel back over the Nei Slasher,
    and so on at every town."""
    held = hands(s)
    if m >= len(held):
        worn = [n for n, w in bag if w and kind_of(n) in (KIND_HAND, KIND_BOTH)]
        return 1 if len(worn) == 1 else 0
    right, left = held[m]
    if right is None:
        return 0
    if left is None:
        return 1
    olds = [n for _, m2, n2, old in todo
            if m2 == m and kind_of(n2) in (KIND_HAND, KIND_BOTH) for n in old]
    for h, n in ((0, right), (1, left)):
        if n in olds:
            return h
    return 0


def surplus(s):
    """Unworn equipment nobody in the party has a use for -- a store
    sells its like, and it beats nothing any member who could wear it
    has on in that slot -- as [(member, slot)], highest slot first, for
    sell(). Anything no store sells is kept: a chest's find may be for
    someone who has not joined yet."""
    party = s.state.get("party") or []
    all_bags = bags(s)
    common = sold_anywhere()
    worn = []
    for m, member in enumerate(party):
        worn.append(loadout(all_bags[m]) if m < len(all_bags) else {})
    out = []
    for m, bag in enumerate(all_bags):
        for slot in range(len(bag) - 1, -1, -1):
            name, is_worn = bag[slot]
            kind = kind_of(name)
            if is_worn or name not in common or kind not in (
                    KIND_HEAD, KIND_HAND, KIND_BOTH, KIND_BODY, KIND_LEGS):
                continue
            useful = False
            for mm, member in enumerate(party):
                cid = CHAR_ID.get(member["name"])
                if cid is None or not wearable(name, cid) or mm >= len(worn):
                    continue
                have = worn[mm]
                if kind in (KIND_HAND, KIND_BOTH):
                    hands = have.get(KIND_HAND, []) + have.get(KIND_BOTH, [])
                    weakest = min((hands_score([n]) for n in hands),
                                  default=0)
                    if len(hands) < 2 or hands_score([name]) > weakest:
                        useful = True
                else:
                    cur = max((piece_score(n) for n in have.get(kind, [])),
                              default=0)
                    if piece_score(name) > cur:
                        useful = True
            if not useful:
                out.append((m, slot))
    return out


def cure_policy(target):
    """(build 89) The hospital's CURE for party position `target`:
    loc_B8AE takes $FFFFDEC0 (the HEAL/CURE stop) to $507 and the
    character list (loc_B90E), loc_B924 asks $503 for a poisoned one, and
    loc_B9B0 clears bit 15 of the HP word -- the only thing that does.
    HEAL (loc_B9D8) sets HP and TP to their maxima and leaves poison."""
    def policy(s):
        st, last = s.state, s.last_script
        if st["menu"] == WIN_HEAL_CURE:
            return 1                            # CURE
        if st["menu"] == WIN_STORE_CHARS:
            return target
        if st["menu"] == WIN_YESNO2:
            return 0 if last == 0x0503 else 1   # pay; nothing else
        return None
    return policy


def cure_poisoned(s, level, door):
    """CURE for every living poisoned member, one visit each."""
    for _ in range(4):
        party = s.state.get("party") or []
        sick = [i for i, m in enumerate(party)
                if m.get("poison") and not m["dead"]]
        if not sick:
            return
        visit(s, level, door, cure_policy(sick[0]))
        s.ready()
        if (s.state.get("party") or [{}] * (sick[0] + 1))[sick[0]] \
                .get("poison"):
            return                              # not cured: stop asking


def heal_here(s, level):
    """The town's own hospital, whichever town this is: HEAL, then CURE
    for anyone poisoned (build 89)."""
    b = town_buildings(level)
    if BUILDING_HOSPITAL not in b:
        return None
    shown = visit(s, level, b[BUILDING_HOSPITAL][0], hospital_policy)
    cure_poisoned(s, level, b[BUILDING_HOSPITAL][0])
    return shown


def stock_mates(s, level, want=4, log=print, trimates=False):
    """A few Monomates (Dimates once they are cheap) from the town's
    item store, into first-page slots, keeping the rest of the money
    for gear. With `trimates`, Trimates where the store has them (Kueri,
    Piata, Ryuon) and the money is there. Returns how many were
    bought."""
    rolf_alive_for_shops(s, level)
    b = town_buildings(level)
    if BUILDING_ITEM not in b:
        return 0
    door, count, store = b[BUILDING_ITEM]
    shelf = store_stock(store)[:count]
    party = s.state.get("party") or []
    have = count_mates(s)
    room = [max(0, 8 - len(bag)) for bag in bags(s)]
    money = s.state.get("meseta") or 0
    todo, m = [], 0
    while have + len(todo) < want and any(room) and money >= 40:
        while room[m % len(party)] == 0:
            m += 1
        room[m % len(party)] -= 1
        if trimates and "Trimate" in shelf \
                and money >= 10 * items()["Trimate"]["price"]:
            todo.append((shelf.index("Trimate"), m % len(party)))
            money -= items()["Trimate"]["price"]
        elif "Dimate" in shelf and money >= 400:
            todo.append((shelf.index("Dimate"), m % len(party)))
            money -= 60
        elif "Monomate" in shelf:
            todo.append((shelf.index("Monomate"), m % len(party)))
            money -= 20
        else:
            break
        m += 1
    if todo:
        buy(s, door, todo, level=level)
    return len(todo)


def stock_item(s, level, item, want, log=print):
    """Up to `want` of `item` carried by the party, bought at `level`'s
    item store into whichever bags have room -- Escapipes for a
    dungeon, where a retreat has no other way out once the TP for Hinas
    is gone. Returns how many were bought."""
    rolf_alive_for_shops(s, level)
    b = town_buildings(level)
    if BUILDING_ITEM not in b:
        return 0
    door, count, store = b[BUILDING_ITEM]
    shelf = store_stock(store)[:count]
    if item not in shelf:
        return 0
    party = s.state.get("party") or []
    have = sum(1 for bag in bags(s) for (n, w) in bag if n == item)
    room = [max(0, 16 - len(bag)) for bag in bags(s)]
    money = s.state.get("meseta") or 0
    price = items()[item]["price"]
    todo = []
    m = 0
    while have + len(todo) < want and any(room) and money >= price:
        while room[m % len(party)] == 0:
            m += 1
        room[m % len(party)] -= 1
        todo.append((shelf.index(item), m % len(party)))
        money -= price
        m += 1
    if todo:
        buy(s, door, todo, level=level)
        log(f"stock: {len(todo)} {item}")
    return len(todo)


def geared(s, town):
    """Nothing left on `town`'s shelves (or in the party's bags) that
    would be an upgrade for anyone, whatever the money."""
    return not plan_gear(s, town, unlimited=True, log=lambda *a: None)


@grinding
def grind_and_gear(s, town, target_level, fights_per_trip=5, trips=60,
                   walks=("walk down 3", "walk left 3", "walk up 3",
                          "walk right 3"), reserve=120, log=print,
                   until_geared=False, lowest=False, spot=None):
    """The grind a player actually does: from `town`, out by its edge
    and fight, Monomates between fights, back to the hospital when it
    is going badly -- and every time through town, the best gear the
    stores sell that the money covers, then out again. Until the leader
    is at `target_level` -- and, with `until_geared`, until the town has
    nothing left worth buying. Returns True on reaching that."""
    for trip in range(trips):
        if s.state.get("level") == town:
            if needs_healing(s, frac=0.99):
                heal_here(s, town)
            gear_up(s, town, reserve=reserve, log=log)
            stock_mates(s, town, log=log)
            trip_save(s, town, log=log)
            if not town_exit(s):
                log("could not leave town")
                return False
            if spot is not None:
                # (build 88) Open ground to walk the pattern on: outside
                # Zosa the gate's cell is hemmed in, and trips of blocked
                # walks fought nothing.
                goto(s, {spot})
        fights = wander(s, list(walks), fights_per_trip, mates=True)
        p = s.state.get("party") or []
        log(f"trip {trip}: {len(fights)} fights; "
            + " ".join(f"{m['name']} L{m['level']} {m['hp']}/{m['max_hp']}"
                       + (" dead" if m["dead"] else "") for m in p)
            + f"; meseta {s.state.get('meseta')}")
        if s.state.get("screen") not in ("level", "battle"):
            log("lost: " + s.state.get("screen", "?"))
            return False
        if any(m["dead"] for m in p):
            if not retreat(s, town, log=log):
                return False
            continue
        lvl = (min(m["level"] for m in p) if lowest and p
               else (p[0]["level"] if p else 0))
        if p and lvl >= target_level:
            enter_town(s, town)
            heal_here(s, town)
            gear_up(s, town, reserve=reserve, log=log)
            if not until_geared or geared(s, town):
                return True
            if not town_exit(s):
                log("could not leave town")
                return False
            continue
        if not enter_town(s, town):
            log("could not get back to town")
            return False
    return lean_short(s, town, target_level, log)


# ---------------------------------------------------------------------
# (build 102) Grinding by long walks: a stretch of one encounter level.
# ---------------------------------------------------------------------

_REGIONS = None
DEZOLIS_SKURE = 0x03


def encounter_group(level, cell, riding=False, late=False):
    """The random encounters' group where the party stands, as the roll
    finds it (ProcessRandomBattle, ps2.asm:26535, read in full in build
    101b-102 and again in build 103): on Motavia one byte a region of
    256x256 pixels -- 16x16 cells, twelve across and eight down, row
    (y >> 8 & 7) * 12 plus column (x >> 8 & $F) -- from the table on foot
    (loc_23C3A), on the jet scooter (loc_23C9A), after Climatrol
    (loc_23CFA, flag $C737) or both. (build 103) On Skure one byte a
    region of 8x8 (loc_23DBA: row (y >> 8 & 7) * 8 plus column (x >> 8 &
    7)); on every other level the level's own byte, one for the whole
    level, which the level's load put in the enemy buffer (ps2.asm:13259:
    the high byte of the level record's plane-B pointer, or plane A's
    after Climatrol) -- 0 there means no encounters at all."""
    global _REGIONS
    if _REGIONS is None:
        _REGIONS = json.loads(
            Path("assets/data/battle.json").read_text())["regions"]
    x, y = cell
    if level == MOTAVIA:
        name = "motavia" + ("_late" if late else "") + \
            ("_scooter" if riding else "")
        return _REGIONS[name][((y // 16) & 7) * 12 + ((x // 16) & 15)]
    if level == DEZOLIS_SKURE:
        return _REGIONS["skure"][((y // 16) & 7) * 8 + ((x // 16) & 7)]
    rec = _levels().get(level) or {}
    return rec.get("enemy_group_late" if late else "enemy_group", 0)


class Ground:
    """(build 102) Open cells on one level where the encounters are one
    level of creature: the piece reached from `start` through regions whose
    group is in `groups`, on foot, before Climatrol (build 103: or a
    dungeon floor's cells reached from `start`, its one group). `anchors` are points
    far apart on it (snapped to its nearest cell); the long walks go from
    one to the next round the list. `keep_off` is every other open cell of
    the level, so no walk's plan leaves the ground."""

    def __init__(self, level, groups, start, anchors):
        lm = level_map(level)
        self.level = level
        seed = min(((x, y) for x in range(start[0] - 3, start[0] + 4)
                    for y in range(start[1] - 3, start[1] + 4)
                    if lm.open(x % lm.w, y % lm.h)
                    and encounter_group(level, (x, y)) in groups),
                   key=lambda c: abs(c[0] - start[0]) + abs(c[1] - start[1]))
        # (build 103) Off Motavia -- a dungeon's floor, one group -- the
        # stairs, holes and damage floors are no part of the ground: a
        # step onto the first two leaves the floor. Motavia's grounds
        # (mild_ground) are as build 102a made them.
        bar = set() if level == MOTAVIA else \
            lm.specials() | lm.damage_floors()
        cells = {seed}
        q = deque([seed])
        while q:
            x, y = q.popleft()
            for dx, dy in STEP.values():
                n = ((x + dx) % lm.w, (y + dy) % lm.h)
                if n in cells or not lm.open(*n) or n in bar \
                        or encounter_group(level, n) not in groups:
                    continue
                cells.add(n)
                q.append(n)
        self.cells = cells
        # (build 170) Two cells as near by the lesser (x, y): the first in
        # the set's order until 170 (the mild ground's (73,46) is a tie,
        # and that order took (73,45) too).
        self.anchors = [min(cells, key=lambda c: ((c[0] - a[0]) ** 2
                                                  + (c[1] - a[1]) ** 2, c))
                        for a in anchors]
        self.keep_off = {(x, y) for x in range(lm.w) for y in range(lm.h)
                         if lm.open(x, y)} - cells


def roam(s, ground, fights, log=print):
    """(build 102) Walk the ground's long legs, anchor to anchor round the
    list from the one after the nearest, fighting what comes (fight_smart),
    Res after each fight while it lasts, mates when hurt -- until `fights`
    fights, a leg's end past that, or hurt with nothing left to mend with
    (s.careful for the walk). Returns the fights' results and the legs
    walked."""
    out = []
    legs = 0
    here = (s.state.get("x"), s.state.get("y"))
    i = min(range(len(ground.anchors)),
            key=lambda k: abs(ground.anchors[k][0] - here[0])
            + abs(ground.anchors[k][1] - here[1]))
    careful, s.careful = s.careful, True
    try:
        while len(out) < fights and legs < 3 * len(ground.anchors):
            if not s.ready() or s.state.get("level") != ground.level:
                break
            cure_poison(s)
            if needs_healing(s):
                mend(s)
                if needs_healing(s):
                    break
            i = (i + 1) % len(ground.anchors)
            goto(s, {ground.anchors[i]}, fights=out,
                 keep_off=ground.keep_off,
                 after_fight=lambda ss: cast_res(ss, below=0.75))
            legs += 1
            if s.state.get("screen") in ("gameover", "sega", "title") \
                    or getattr(s, "hurt", False):
                break
    finally:
        s.careful = careful
    return out, legs


@grinding
def roam_and_gear(s, town, target_level, ground, fights_per_trip=8,
                  trips=60, reserve=120, log=print, until_geared=False,
                  lowest=False):
    """(build 102) grind_and_gear with long walks over `ground` in place of
    a little square by the town's gate (The owner: watching the party go
    round in circles is boring). As grind_and_gear: in town the hospital
    when hurt, the best gear the money covers and mates; out onto the
    ground (through Paseo's tube from Paseo); `roam` for the trip's
    fights; back to town, fighting what comes on the way. Until the leader
    (or, `lowest`, the lowest) is at `target_level` and, `until_geared`,
    the town has nothing left worth buying."""
    for trip in range(trips):
        if s.state.get("level") == town:
            if needs_healing(s, frac=0.99):
                heal_here(s, town)
            gear_up(s, town, reserve=reserve, log=log)
            stock_mates(s, town, log=log)
            trip_save(s, town, log=log)
            if not town_exit(s):
                log("could not leave town")
                return False
        if (s.state.get("level"), (s.state.get("x"), s.state.get("y"))) \
                not in {(ground.level, c) for c in ground.cells}:
            careful, s.careful = s.careful, True
            try:
                travel(s, ground.level, ground.cells, log=log)
            finally:
                s.careful = careful
        fights, legs = ([], 0)
        if s.state.get("level") == ground.level and not needs_healing(s):
            fights, legs = roam(s, ground, fights_per_trip, log=log)
        p = s.state.get("party") or []
        log(f"trip {trip}: {len(fights)} fights, {legs} legs; "
            + " ".join(f"{m['name']} L{m['level']} {m['hp']}/{m['max_hp']}"
                       + (" dead" if m["dead"] else "") for m in p)
            + f"; meseta {s.state.get('meseta')}")
        if s.state.get("screen") not in ("level", "battle"):
            log("lost: " + s.state.get("screen", "?"))
            return False
        if any(m["dead"] for m in p):
            if not retreat(s, town, log=log):
                return False
            continue
        lvl = (min(m["level"] for m in p) if lowest and p
               else (p[0]["level"] if p else 0))
        if p and lvl >= target_level:
            enter_town(s, town)
            heal_here(s, town)
            gear_up(s, town, reserve=reserve, log=log)
            if not until_geared or geared(s, town):
                return True
            continue
        if not enter_town(s, town):
            log("could not get back to town")
            return False
    return lean_short(s, town, target_level, log)


# ---------------------------------------------------------------------
# (build 103) Saves on the way: a trip back to a town saves at its Data
# Memory when something worth keeping happened since the last save.
# ---------------------------------------------------------------------

MID_SAVES = None        # a directory: trip saves on (playthrough, COMPUTER
                        # PLAYS); None: off (story.py, routes, trials)
ON_SAVE = None          # callable(kind, stage, path): COMPUTER PLAYS's note
                        # of where its last run can go on from
# The stages a save on the way can land in, each resumable from a save in
# its own town (docs/computer_plays.md, Saves on the way): the grinds.
TRIP_SAVE_STAGES = frozenset({"arima", "oputa", "amy", "lab_in", "zema",
                              "climatrol", "dezolis_grind", "kain_grind"})
DATA_MEMORY = 2         # BuildingID_DataMemory: town_buildings' key
CAPS = ("MagicCap", "MogicCap")


def trip_marks(s):
    """What the party is, for 'something worth keeping happened': each
    member's level by name."""
    return {m["name"]: m["level"] for m in s.state.get("party") or []}


def mark_story(s):
    """A story point passed mid-stage (the lab's door blown): the next trip
    back to a town saves."""
    s.story_point = True


def mid_note(stage):
    """(build 103) The note beside a save on the way: what the stage had
    done by then that its code cannot read back from the save -- s.progress
    (climatrol: the chests looted)."""
    return Path(MID_SAVES) / f"mid_{stage}.note.json"


def trip_save(s, town, log=print, saved=False):
    """On a trip back to `town`, in a stage of TRIP_SAVE_STAGES: save at
    the town's Data Memory when, since the last save, a member's level
    rose, a member joined, or a story point passed (mark_story), and keep
    a copy as MID_SAVES/mid_<stage>.json. `saved`: the caller has just
    saved there itself (back_to_climatrol's Kueri save): only the copy.
    Arima has no Data Memory; on
    Dezolis one saves only for a live Rolf in the Magic Cap or the Mogic
    Cap (CheckPlanetAndCaps, ps2.asm 19188). True when a save was made."""
    stage = getattr(s, "stage", None)
    if MID_SAVES is None or stage not in TRIP_SAVE_STAGES:
        return False
    st = s.state
    if st.get("screen") != "level" or st.get("level") != town:
        return False
    b = town_buildings(town)
    if DATA_MEMORY not in b:
        return False
    party = st.get("party") or []
    if town in DEZOLIS_LEVELS:
        rolf = next((m for m in party if m["name"] == "Rolf"), None)
        if rolf is None or rolf["dead"] or worn_head(s, "Rolf") not in CAPS:
            return False
    if any(m["dead"] for m in party):
        # (build 118) A retry resumes from this save: never with the dead
        # in it. Seed 4's Paseo grind saved with Nei dead and 68 meseta, the
        # clone lab out of reach, and every retry came back to that.
        return False
    last = getattr(s, "save_marks", None) or {}
    now = trip_marks(s)
    rose = [n for n, lv in now.items() if n in last and lv > last[n]]
    joined = [n for n in now if n not in last]
    story = getattr(s, "story_point", False)
    if not (rose or joined or story):
        return False
    t0 = time.time()
    if not saved:
        save(s, 1, level=town, door=b[DATA_MEMORY][0])
    src = s.saves_dir / "slot1.json"
    if not src.exists() or (not saved and src.stat().st_mtime < t0):
        log("trip save: none written in", town)
        return False
    Path(MID_SAVES).mkdir(parents=True, exist_ok=True)
    dst = Path(MID_SAVES) / f"mid_{stage}.json"
    shutil.copy(src, dst)
    write_lf(mid_note(stage), json.dumps(
        {"progress": sorted(getattr(s, "progress", set()))}) + "\n")
    s.save_marks = now
    s.story_point = False
    why = ", ".join(([f"{', '.join(rose)} up"] if rose else [])
                    + ([f"{', '.join(joined)} joined"] if joined else [])
                    + (["a story point"] if story else []))
    log(f"trip save: {stage} in {level_name(town)} ({why}) -- "
        + " ".join(f"{n} L{lv}" for n, lv in now.items()))
    if ON_SAVE is not None:
        ON_SAVE("mid", stage, dst)
    return True


def lean_short(s, town, target_level, log=print):
    """(build 98) The end of a grind out of trips: False, as ever -- or,
    with LEAN, the party in `town` healed and geared and True, so the story
    goes on with the levels it has."""
    if not LEAN:
        return False
    if s.state.get("screen") not in ("level", "building"):
        return False
    if any(m["dead"] for m in s.state.get("party") or []) \
            and not retreat(s, town, log=log):
        return False
    if s.state.get("level") != town and not enter_town(s, town):
        return False
    heal_here(s, town)
    gear_up(s, town, log=log)
    levels = " ".join(f"{m['name']} L{m['level']}"
                      for m in s.state.get("party") or [])
    if target_level:
        log(f"lean: short of L{target_level}, going on --", levels)
    else:
        log("lean: out of trips with gear still to buy, going on --", levels)
    return True


# ---------------------------------------------------------------------
# Someone who wanders: find them with `objects`, walk up, face, A.
# ---------------------------------------------------------------------

OBJ_RE = re.compile(r"(\d+):\$(\w+):(\d+)/\$(\w+):(\d+),(\d+)(m?)")


def objects(s):
    """Every live object slot on the level: a list of dicts with slot,
    object, interaction, index, cell and whether it is mid-step."""
    out = s.send("objects")
    for ln in out:
        m = re.search(r"ok\s+objects ?(.*)$", ln)
        if m:
            return [{"slot": int(a), "object": int(b, 16), "type": int(c),
                     "index": int(d, 16), "cell": (int(x), int(y)),
                     "moving": bool(mv)}
                    for a, b, c, d, x, y, mv in OBJ_RE.findall(m.group(1))]
    return []


def talk_to(s, index, want=None, tries=12, log=print):
    """Walk up to the person with dialogue number `index` on this level,
    face them and press A, retrying while they wander off. Returns the
    scripts shown once one of `want` (or anything, without it) is."""
    for _ in range(tries):
        if not s.ready():
            return None
        who = [o for o in objects(s) if o["type"] == 1 and o["index"] == index]
        if not who:
            log(f"talk_to: {person_name(s.state.get('level'), index)} is not here")
            return None
        if who[0]["moving"]:
            s.send("wait ticks 16")
            continue
        level = s.state["level"]
        cells = neighbours(level, who[0]["cell"])
        here = (s.state["x"], s.state["y"])
        if here not in cells:
            if not goto(s, set(cells)):
                continue
            if not s.ready():
                return None
            here = (s.state["x"], s.state["y"])
            if here not in cells:
                continue
        # still there?
        now = [o for o in objects(s) if o["type"] == 1 and o["index"] == index]
        if not now or now[0]["cell"] != who[0]["cell"] or now[0]["moving"]:
            continue
        s.send(f"press {cells[here]}", "wait idle")
        if s.state.get("kind") != "waiting" or s.state.get("wait") != "control":
            s.ready()
        shown = talk(s)
        if shown and (want is None or any(x in want for x in shown)):
            return shown
    return None


# ---------------------------------------------------------------------
# Who goes: the roster, and a choice of the party by the numbers.
# ---------------------------------------------------------------------

ROSTER_RE = re.compile(r"(\w+):L(\d+):(\d+)/(\d+):(\d+)/(\d+)(\*?)")
CHAR_NAMES = ["Rolf", "Nei", "Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir"]
# Healing a member can cast outside a fight -- what keeps a party
# walking between hospitals -- weighted by how much it mends.
HEAL_WEIGHT = {"Res": 10, "Gires": 20, "Nares": 30, "Sar": 20, "Gisar": 30,
               "Nasar": 40, "Rever": 25, "Anti": 5}
# How far behind the best candidate a member may fall before the flexible
# place goes to them for a while: EXP is split among the living, and the
# tables are absolute, so a low member catches up cheaply.
CATCH_UP_GAP = 5


def roster(s):
    """Everyone who has joined: {name: {level, hp, max_hp, tp, max_tp,
    in_party}} in join order (character id order)."""
    out = s.send("roster")
    for ln in out:
        m = re.search(r"ok\s+roster (.*)$", ln)
        if m:
            return {n: {"level": int(lv), "hp": int(hp), "max_hp": int(mh),
                        "tp": int(tp), "max_tp": int(mt), "in_party": bool(p)}
                    for n, lv, hp, mh, tp, mt, p in ROSTER_RE.findall(m.group(1))}
    return {}


def expected_stats(name, level):
    """A member's stats at `level` from the level table: the first row
    is absolute, every later one what the level-up adds, and
    GenerateRandStatIncrease's (rand&$FF + $80)/256 averages 1."""
    rows = _chars()[CHAR_ID[name]]["levels"][:max(1, level)]
    keys = ("hp", "tp", "attack", "defense", "agility", "dexterity",
            "techs_a", "techs_b")
    return {k: sum(r[k] for r in rows) for k in keys}


# How much a point of tech_power counts against a point of HP: a
# technique is cast a few times a trip, a weapon swung every round.
TECH_WEIGHT = 0.5


def member_value(name, level, mix=None):
    """One number for how much a member adds at `level`: HP (staying up),
    attack before weapons (the weapon a member can wear is bought for them
    either way), defense, agility (acting first), and the healing their
    map techniques give between fights. With `mix` (dungeon_mix of the
    levels ahead, build 88), the best damage technique they know, by the
    share of those enemies its type bits touch: Kain's are robots only,
    Hugh's biomonsters only (loc_27AA)."""
    st = expected_stats(name, level)
    heals = [t for t in _techs()["available"][name]["map"][:st["techs_b"]]
             if t in HEAL_WEIGHT]
    return (st["hp"] + 1.5 * st["attack"] + st["defense"]
            + 0.5 * st["agility"] + sum(HEAL_WEIGHT[t] for t in heals)
            + TECH_WEIGHT * tech_power(name, level, mix))


def choose_party(members, places=4, core=("Rolf", "Nei", "Rudo"),
                 catch_up=True, mix=None):
    """The party to take from `members` ({name: level}, everyone joined):
    the core first (Rolf always; Nei and Rudo while they are there), then
    the flexible places by member_value at each one's own level -- unless
    someone has fallen CATCH_UP_GAP levels or more behind the best of the
    others, in which case the lowest takes a place to catch up. Returns
    (names in party order, the reason)."""
    party = [n for n in core if n in members][:places]
    rest = [n for n in members if n not in party]
    reason = "core " + ", ".join(party)
    while len(party) < places and rest:
        ranked = sorted(rest, key=lambda n: -member_value(n, members[n],
                                                          mix))
        low = min(rest, key=lambda n: members[n])
        best = ranked[0]
        if catch_up and low != best \
                and members[best] - members[low] >= CATCH_UP_GAP:
            pick = low
            reason += (f"; {low} L{members[low]} to catch up with "
                       f"{best} L{members[best]}")
        else:
            pick = best
            reason += (f"; {best} by value "
                       + ", ".join(f"{n} {member_value(n, members[n], mix):.0f}"
                                   for n in ranked))
        party.append(pick)
        rest.remove(pick)
    return party, reason


def nei_available(s):
    """(build 90) Whether Nei can be in the party: only while $C737 is
    clear. RolfHouseOption_Reorganize (ps2.asm 16318) puts her in slot 1
    itself then (loc_B1A0: party_members_num and $C60A to 1), and
    Win_RegroupCharList (24423) lists character ids from 2 up -- never
    Rolf, never Nei. After Climatrol $C737 is set and the roster still
    names her; nothing in the game can put her back."""
    out = s.send("expect flag $C737 0")
    return not any("FAIL" in ln for ln in out)


def pickable(s, r=None):
    """{name: level} of everyone who can be taken: the roster, less Nei
    once $C737 is set (nei_available). What choose_party is given."""
    r = roster(s) if r is None else r
    members = {n: v["level"] for n, v in r.items()}
    if "Nei" in members and not nei_available(s):
        del members["Nei"]
    return members


def regroup_stops(members, want):
    """The regroup list's stops for `want` (beyond Rolf and Nei), in
    order: the list is everyone joined bar Rolf and Nei in id order, and
    each pick leaves it (loc_A4E4)."""
    left = [n for n in CHAR_NAMES if n in members and n not in ("Rolf", "Nei")]
    stops = []
    for n in want:
        if n in ("Rolf", "Nei") or n not in left:
            continue
        stops.append(left.index(n))
        left.remove(n)
    return stops


def pick_party(s, catch_up=True, log=print, want=None, ahead=None,
               core=("Rolf", "Nei", "Rudo")):
    """Choose the party and make it so at Rolf's house (Paseo). Returns
    the party's names; nothing is changed when it is already that one.
    `want`: the party itself, as names in order, instead of choosing
    (tools/party_trials.py's answer, say). `ahead`: the levels of the
    dungeon to come, whose enemy mix weighs Kain's and Hugh's
    techniques (build 88)."""
    r = roster(s)
    members = pickable(s, r)            # (build 90) no Nei after Climatrol
    if want is not None:
        party, why = list(want), "chosen by the caller"
    else:
        mix = dungeon_mix(ahead) if ahead else None
        party, why = choose_party(members, catch_up=catch_up, mix=mix,
                                  core=core)
        if mix:
            why += " [mix " + " ".join(f"{ENEMY_TYPE_NAME.get(k, k)}:{v:.2f}"
                                        for k, v in mix.items() if v) + "]"
    now = [n for n in r if r[n]["in_party"]]
    log(f"party: {' '.join(party)} ({why})")
    if sorted(now) == sorted(party):
        return party
    if not enter_town(s, PASEO):
        log("party: could not get to Paseo")
        return now
    # RolfHouseOption_Reorganize (ps2.asm:16330) puts Nei in slot 1 itself
    # only while $C737 is clear; after Climatrol all three are picked.
    picks = party[2:] if "Nei" in party else party[1:]
    rolf_house(s, take=regroup_stops(members, picks), regroup=True)
    return [n for n, v in roster(s).items() if v["in_party"]]


# ---------------------------------------------------------------------
# Shir's thefts (build 87a)
# ---------------------------------------------------------------------
#
# ProcessStealItem (ps2.asm:19093) runs as the party walks out of a
# weapon, armour or item store (loc_BA66 and its twins) or the Central
# Tower's room (loc_C148): with Shir in the party, alive, her bag not full
# and -- for the room only -- her level 10 or more, one call in sixteen
# (GenerateRandomNumber & $F == 0) she takes StealItemArray[list + (a
# second number & 7)], joins the item to her own bag and leaves the party;
# party_member_join_next + 1, so Rolf's house says "Hey, Shir is coming
# back!" ($0927) and puts her back ($0928). The lists (steal.json):
# weapon FireStaff/Dagger/LaserKnife/LaserShot, armor GrSleeves/SilCrown/
# JwlCrown/ShuneBoots, item Trimate x4 Hidapipe x2 StarMist MoonDew, room
# Visiphone x8. No shop sells the Visiphone.

WIN_TOWER_OPTIONS  = 0x35
WIN_TOWER_OPTIONS2 = 0x36
STEAL_ODDS = 1 / 16
STEAL_DOORS = {"item": "item_store", "weapon": "weapon_store",
               "armor": "armor_store", "room": "tower"}


def steal_lists():
    return json.loads(Path("assets/data/steal.json").read_text())["lists"]


def steal_source(item):
    """The list (and so the Paseo door) an item can be stolen from."""
    for name, items in steal_lists().items():
        if item in items:
            return name
    return None


def room_policy(s):
    """The Central Tower: ROOM at the desk, then B out of whatever the
    keeper asks -- the way out (event_routine_sub 1) is the theft's."""
    st = s.state
    if st.get("menu") in (WIN_TOWER_OPTIONS, WIN_TOWER_OPTIONS2):
        return 0 if st.get("building") == 8 else "b"
    if st.get("menu") is not None:
        return "b"
    return None


def shir_in_party(s):
    return any(m["name"] == "Shir" for m in s.state.get("party") or [])


def carried(s, item):
    """How many of `item` the party carries (Shir's bag only while she is
    in the party)."""
    return sum(1 for member in bags(s) for name, _ in member
               if name == item)


def shir_bag(s):
    names = [m["name"] for m in s.state.get("party") or []]
    b = bags(s)
    return b[names.index("Shir")] if "Shir" in names and \
        names.index("Shir") < len(b) else []


def fetch_shir(s, log=print):
    """Rolf's house after a theft: "Hey, Shir is coming back!" and her
    return ($0927, $0928), Shir back in the party
    in the place the last member took."""
    shown = rolf_house(s) or []
    back = shir_in_party(s)
    log(f"shir: fetched from Rolf's house ({'back' if back else 'NOT back'};"
        f" {scripts_text(x for x in shown if 0x0927 <= x <= 0x0928)})")
    return back


def shir_join(s, keep=3, log=print):
    """Shir into the party: the knock at Rolf's house if she has not come
    yet (party_member_join_next 7 after Piata), then REORGANIZE with the
    best `keep` of the others by member_value."""
    r = roster(s)
    if "Shir" not in r:
        rolf_house(s)
        r = roster(s)
        if "Shir" not in r:
            log("shir: not in the roster after Rolf's house")
            return False
    if r["Shir"]["in_party"]:
        return True
    # The party as it stands, less its weakest by member_value when there
    # are already four: Nei is in the roster after Climatrol but never in
    # the party again, so choose_party's core is not the thing here.
    now = [n for n, v in r.items() if v["in_party"]]
    while len(now) > keep:
        drop = min((n for n in now if n != "Rolf"),
                   key=lambda n: member_value(n, r[n]["level"]))
        now.remove(drop)
    got = pick_party(s, want=now + ["Shir"], log=log)
    return "Shir" in got


def raise_member(s, name, target, town=PASEO, log=print, trips=60):
    """Fights near `town` until `name` is at `target`: EXP is shared by
    the living (loc_F2A4), so a low member climbs fast beside strong ones.
    Heals at the hospital between trips."""
    for trip in range(trips):
        lv = next((m["level"] for m in s.state.get("party") or []
                   if m["name"] == name), None)
        if lv is None:
            return False
        if lv >= target:
            return True
        if s.state.get("level") == town and not town_exit(s):
            return False
        wander(s, ["walk down 3", "walk up 3", "walk left 3",
                   "walk right 3"], 6)
        if s.state.get("screen") not in ("level", "battle"):
            log("lost: " + str(s.state.get("screen")))
            return False
        if needs_healing(s) or trip % 4 == 3:
            if not enter_town(s, town):
                return False
            heal(s)
    return False


def shir_thefts(s, wants, log=print, max_exits=600, max_seconds=None,
                on_full=None):
    """Walk Shir in and out of Paseo's shops (and the Central Tower's
    room, for the Visiphone) until the party carries every item in
    `wants` ({item: count}), fetching her from Rolf's house after each
    theft. Returns the tally: exits she could have stolen on, thefts, and
    what she took, by list. `max_seconds` is a wall clock (a run under it
    replays only until it runs out); story.py passes exits instead (build
    99). `on_full` empties her bag when it fills (story.empty_shir: what
    she stole to whoever has room) and the thefts go on; without it, or
    when it frees nothing, a full bag ends them -- seed 2's thefts before
    Noah stopped there with no Moon Dew (build 98)."""
    tally = {"exits": {}, "thefts": {}, "items": {}}
    if not shir_join(s, log=log):
        return tally
    if any(steal_source(i) == "room" for i in wants):
        lv = next(m["level"] for m in s.state["party"] if m["name"] == "Shir")
        if lv < 10:
            log(f"shir: L{lv}, the room wants 10")
            if not raise_member(s, "Shir", 10, log=log):
                log("shir: could not raise her to 10")
                return tally
    total = 0
    t0 = time.time()
    while total < max_exits:
        if max_seconds is not None and time.time() - t0 > max_seconds:
            log(f"shir: out of time after {total} exits")
            break
        todo = [i for i, n in wants.items() if carried(s, i) < n]
        if not todo:
            break
        lst = steal_source(todo[0])
        door = PASEO_DOORS[STEAL_DOORS[lst]]
        full = len(shir_bag(s)) >= 16
        if full and on_full is not None:
            on_full()
            full = len(shir_bag(s)) >= 16
            if not full:
                log("shir: her bag was full; what she stole handed on")
        if full:
            log("shir: her bag is full")
            break
        before = len(s.shown)
        if lst == "room":
            visit(s, PASEO, door, room_policy)
        else:
            buy(s, door, [])
        total += 1
        tally["exits"][lst] = tally["exits"].get(lst, 0) + 1
        s.ready()
        if 0x1722 in s.shown[before:] or not shir_in_party(s):
            tally["thefts"][lst] = tally["thefts"].get(lst, 0) + 1
            if not fetch_shir(s, log=log):
                break
            got = [n for n, _ in shir_bag(s)]
            took = got[-1] if got else "?"
            tally["items"][took] = tally["items"].get(took, 0) + 1
            log(f"shir: exit {total} ({lst}): took {took}")
    log("shir: " + json.dumps(tally))
    return tally


def field_spot(level, near, reach=3):
    """The open cell nearest `near` on `level` with `reach` open cells
    each way across and down -- somewhere wander's walks are not
    blocked -- on the side of the map `near` can walk to."""
    lm = level_map(level)
    best = None
    for cy in range(lm.h):
        for cx in range(lm.w):
            if not all(lm.open(cx + d, cy) and lm.open(cx, cy + d)
                       for d in range(-reach, reach + 1)):
                continue
            d2 = (cx - near[0]) ** 2 + (cy - near[1]) ** 2
            if best is None or d2 < best[0]:
                if lm.path(near, {(cx, cy)}) is not None:
                    best = (d2, (cx, cy))
    return best[1] if best else None


# (build 113a) At the end, where everything it needs is defined: above the
# rest of the module it ran before PASEO existed, and failed on every OS.
if __name__ == "__main__":
    s = Session(sys.argv[1] if len(sys.argv) > 1 else "play")
    s.new_game()
    print("after the intro:", s.state)
    s.close()
