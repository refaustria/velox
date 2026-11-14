#!/usr/bin/env python3
"""Per-map idle-client park point of the BotRun soak (M7 H9, ADR 0008 5.3).

Usage: botwatch_park.py <MapName> <BotWatch.json> <out.json>
BotWatch.json parks the idle human at (-2700, -1700, 100), the south-wall start of L_ItemTest. That point is outside the footprint of
the M7 maps (Nemus, Fornax: 5 world kills per round). For a map with a layout module (Tools/editor/maps/<slug>_layout.py) the point
is read from the map spec: the spawn farthest from every item (the quietest floor spot), at the spawn's floor Z + 100. Prints
"x y z" and writes a copy of BotWatch.json with that startLocation to <out.json>. Exit 3 = keep BotWatch.json unchanged (L_ItemTest,
the DM1 maps, a map without a layout module): nothing is written, so DM1 is unchanged by construction.
"""
import importlib
import json
import math
import os
import re
import sys

if len(sys.argv) != 4:
    sys.exit('usage: botwatch_park.py <MapName> <BotWatch.json> <out.json>')
name, src, dst = sys.argv[1:]
m = re.fullmatch(r'L_([A-Za-z0-9]+?)(?:_Blockout)?', name)
slug = m.group(1).lower() if m else ''
if not slug or slug in ('dm1', 'itemtest'):
    sys.exit(3)
editor = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Tools', 'editor')
sys.path.insert(0, editor)
try:
    layout = importlib.import_module('maps.%s_layout' % slug)
except ImportError:
    sys.exit(3)
spawns, items = list(layout.SPAWNS), list(layout.ITEMS)
if not spawns:
    sys.exit(3)
best = max(spawns, key=lambda s: min((math.hypot(s.x - i.x, s.y - i.y) for i in items), default=0.0))
loc = [best.x, best.y, (best.z or 0.0) + 100.0]
doc = json.load(open(src))
doc['startLocation'] = loc
doc['note'] = doc.get('note', '') + ' [parked at %s for %s by infra/ci/botwatch_park.py (M7 H9)]' % (best.id, name)
json.dump(doc, open(dst, 'w'), indent=1)
print('%g %g %g' % tuple(loc))
