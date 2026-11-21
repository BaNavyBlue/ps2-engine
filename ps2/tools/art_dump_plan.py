"""Writes the job list tools/art_dump reads: every enemy (with a formation
it appears in), every weapon (with a character who can hold it) and every
technique with a battle animation (with a character who knows it in
battle). Run from ps2/:  python3 tools/art_dump_plan.py > plan.txt"""

import json
import re
import sys

CHARS = ['rolf', 'nei', 'rudo', 'amy', 'hugh', 'anna', 'kain', 'shir']


def slug(s):
    s = s.strip().lower().replace('&', 'and')
    s = re.sub(r'[^a-z0-9]+', '_', s)
    return s.strip('_')


def main():
    battle = json.load(open('assets/data/battle.json'))
    items = json.load(open('assets/data/items.json'))
    techs = json.load(open('assets/data/techniques.json'))

    forms = battle['formations']
    out = []

    # Monsters: a formation it heads, alone if there is one; else one it
    # is the second group of.
    for e in battle['enemies']:
        eid = e['id']
        if eid == 0 or not e.get('art_file'):
            continue
        best = None
        for i, f in enumerate(forms):
            if f['enemy1'] == eid and f['count1'] > 0:
                key = (0, f['enemy2'] != 0, f['count1'])
                cand = (key, i + 1, 0)
            elif f['enemy2'] == eid and f['count2'] > 0 and f['enemy1'] != 0:
                key = (1, 0, f['count2'])
                cand = (key, i + 1, f['count1'])
            else:
                continue
            if best is None or cand[0] < best[0]:
                best = cand
        name = f"{eid:03d}_{slug(e['name'])}"
        if best is None:
            print(f'# no formation for enemy {eid} {e["name"]}',
                  file=sys.stderr)
            continue
        out.append(f'monster {eid} {best[1]} {best[2]} {name}')

    # Weapons: WeaponProperties runs from the Knife, one entry an item.
    by_id = {i['id']: i for i in items}
    knife = next(i['id'] for i in items if i['key'] == 'Knife')
    for w, prop in enumerate(battle['weapon_props']):
        it = by_id.get(knife + w)
        if it is None:
            continue
        mask = it['equip_mask']
        c = next((b for b in range(8) if mask & (1 << b)), 0)
        name = f"{w:02d}_{slug(it['name'])}"
        out.append(f'weapon {c} {it["id"]} {name}')

    # Techniques: the first character with it on their battle list.
    avail = techs['available']
    order = ['Rolf', 'Nei', 'Rudo', 'Amy', 'Hugh', 'Anna', 'Kain', 'Shir']
    for t in techs['techniques']:
        tid, key = t['id'], t['key']
        if tid == 0 or tid > len(battle['tech_props']):
            continue
        who = next((ci for ci, cn in enumerate(order)
                    if key in avail[cn]['battle']), None)
        if who is None:
            who = next((ci for ci, cn in enumerate(order)
                        if key in avail[cn]['map']), None)
        if who is None:
            continue
        name = f"{tid:02d}_{slug(t['name'])}"
        out.append(f'tech {who} {tid} {name}')

    print('\n'.join(out))


if __name__ == '__main__':
    main()
