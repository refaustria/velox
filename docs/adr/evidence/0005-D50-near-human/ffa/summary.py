"""04g (copy of post04f/summary.py): D36 per-run caps, pocket strikes, VeloxBotRepath count, strike list; ramp.py dropped;
writes <log>.json for set.py (set-level sums). Original: 04f FFA soak summary of one server log: the D20 bands, the D34 dwell band, the 04f link / strike bands and the
report-only diagnostics. Copy of a04d/summary.py + the stall.py dwell scan + ramp.py max_dwell_s.
Usage: summary.py <log> [label]"""
import collections
import datetime
import os
import re
import json
import sys

path = sys.argv[1]
label = sys.argv[2] if len(sys.argv) > 2 else path
MAJOR = re.compile(r'item=DA_Item_(Health_Mega|Armor_Red|Armor_Yellow|Powerup_[A-Za-z]+)')
TS = re.compile(r'^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\]')
POS = re.compile(r"VeloxBot name=(\S+) (.*?) pos=V\(X=([-\d.]+), Y=([-\d.]+), Z=([-\d.]+)\)")
RADIUS = 150.0


def ts(line):
    m = TS.match(line)
    if not m:
        return None
    y, mo, d, h, mi, s, ms = (int(x) for x in m.groups())
    return datetime.datetime(y, mo, d, h, mi, s, ms * 1000).timestamp()


pickups = weapon_pickups = 0
mega_times = []
kills = suicides = deaths = 0
stuck = refused = errors = giveup = 0
goals = collections.Counter()
vis_armed = vis_armed_fight = 0
allun = collections.Counter()
budget_means = []
run_mean = None
rules = collections.Counter()
reasons = collections.Counter()
link_max_dt, link_max_desc = 0.0, '-'
long_not_timeout = []
strike_times = collections.defaultdict(list)
supp1 = debug_lines = 0
live = False
camp = {}
dwell = {}
max_dwell, max_dwell_at = 0, '-'
pocket = 0
strike_list = []
repaths = collections.Counter()
repath_targets = collections.defaultdict(list)
STUCK_AT = re.compile(r'VeloxBotStuck bot=(\S+) at=V\(X=([-\d.]+), Y=([-\d.]+), Z=([-\d.]+)\)')

with open(path, 'rb') as f:
    for raw in f:
        line = raw.decode('utf-8', 'replace').rstrip()
        if 'Error:' in line or 'Ensure condition' in line or 'ensure(' in line.lower() and 'LogOutputDevice' in line:
            errors += 1
        if 'VeloxMatch phase=Live' in line:
            live = True
            dwell.clear()
            continue
        if 'VeloxPickup item=' in line:
            m = re.search(r'by=(\S+)', line)
            if m and m.group(1) not in ('-', 'none'):
                pickups += 1
                if 'item=DA_Item_Weapon_' in line:
                    weapon_pickups += 1
                if 'item=DA_Item_Health_Mega' in line:
                    t = re.search(r' t=([\d.]+)', line)
                    mega_times.append(f"{t.group(1) if t else '?'}:{m.group(1)}")
            continue
        if 'VeloxKill victim=' in line and 'ignored' not in line:
            deaths += 1
            if 'suicide=0' in line:
                kills += 1
            elif 'suicide=1' in line:
                suicides += 1
            continue
        if 'VeloxBotStuck' in line:
            stuck += 1
            r = re.search(r'rule=(\S+)', line)
            rules[r.group(1) if r else '?'] += 1
            b = re.search(r'VeloxBotStuck bot=(\S+)', line)
            t = ts(line)
            if b and t is not None:
                strike_times[b.group(1)].append(t)
            a = STUCK_AT.search(line)
            if a:
                sx, sy = float(a.group(2)), float(a.group(3))
                if abs(sx) < 150 and abs(sy) < 150:
                    pocket += 1
                hm = TS.match(line)
                strike_list.append(f"{a.group(1)}@({sx:.0f},{sy:.0f}):{r.group(1) if r else '?'}" + (f"@{hm.group(4)}:{hm.group(5)}:{hm.group(6)}" if hm else ''))
            continue
        m = re.search(r'VeloxBotRepath bot=(\S+) reason=(\S+) at=(V\(.*?\)) target=(V\(.*?\))', line)
        if m:
            repaths[m.group(1)] += 1
            repath_targets[m.group(1)].append((m.group(3), m.group(4)))
            continue
        if 'giveup' in line and 'LogVeloxBots' in line:
            giveup += 1
        m = re.search(r'VeloxBotLink bot=(\S+) .*result=(\S+) reason=(\S+) dt=([\d.]+) link=(\S+)', line)
        if m:
            if m.group(2) != 'ok':
                reasons[m.group(3)] += 1
            dt = float(m.group(4))
            if dt > link_max_dt:
                link_max_dt, link_max_desc = dt, f"{m.group(1)}:{m.group(5)}:{m.group(3)}"
            if dt > 6.0 and m.group(3) != 'timeout':
                long_not_timeout.append(f"{m.group(1)}:{m.group(5)}:{m.group(3)}:{dt:.2f}")
            continue
        m = POS.search(line)
        if m:
            name, body = m.group(1), m.group(2)
            x, y, z = float(m.group(3)), float(m.group(4)), float(m.group(5))
            gm = re.search(r'goal=(\S+)', body)
            g = gm.group(1).lower() if gm else '?'
            goals[g] += 1
            debug_lines += 1
            if ' supp=1' in line:
                supp1 += 1
            w = re.search(r'wstr=(-?[\d.]+)', body)
            if ' vis=1' in body and w and float(w.group(1)) >= 0.5:
                vis_armed += 1
                if g == 'fight':
                    vis_armed_fight += 1
            if live:
                # D34: consecutive samples within 150 cm (2-D) of the run's first sample; camp=1 samples excluded.
                if camp.get(name, 0):
                    dwell.pop(name, None)
                else:
                    d = dwell.get(name)
                    if d and ((x - d[0]) ** 2 + (y - d[1]) ** 2) ** 0.5 <= RADIUS:
                        d[2] += 1
                    else:
                        d = dwell[name] = [x, y, 1]
                    if d[2] > max_dwell:
                        max_dwell, max_dwell_at = d[2], f"{name}@({d[0]:.0f},{d[1]:.0f}),goal={g}"
            continue
        m = re.search(r'VeloxBotRoute bot=(\S+) (.*)', line)
        if m:
            if 'refused' in line:
                refused += 1
            cm = re.search(r' camp=(\d)', line)
            if cm:
                camp[m.group(1)] = int(cm.group(1))
            c = re.search(r'candidates=\[(.*)\]', line)
            if c:
                st = re.search(r'start=(\w+)', line)
                start = st.group(1) if st else '-'
                cands = [x for x in c.group(1).split(',') if x]
                if cands and all('!unreachable' in x for x in cands):
                    allun[start] += 1
            continue
        m = re.search(r'VeloxBotBudget bots=.*? mean_us=([\d.]+).*?run_mean_us=([\d.]+)', line)
        if m:
            budget_means.append(float(m.group(1)))
            run_mean = float(m.group(2))

bursts = []
for bot, times in strike_times.items():
    times.sort()
    for i in range(len(times) - 2):
        if times[i + 2] - times[i] <= 10.0:
            bursts.append(bot)
            break

total_goals = sum(goals.values())
fight_share = 100.0 * goals.get('fight', 0) / total_goals if total_goals else 0.0
armed_share = 100.0 * vis_armed_fight / vis_armed if vis_armed else 0.0
supp_share = 100.0 * supp1 / debug_lines if debug_lines else 0.0
budget = sum(budget_means) / len(budget_means) if budget_means else 0.0
repath_total = sum(repaths.values())
fails = []
# D36 per-run caps (the set sums are judged by set.py).
for cond, name in ((stuck > 7, 'stuck>7'), (len(bursts) > 0, 'bursts'), (max_dwell > 8, 'dwell>8'), (link_max_dt > 8.0, 'link_dt>8'),
                   (kills < 2, 'kills<2'), (pickups < 20, 'pickups<20'), (len(mega_times) < 1, 'mega<1'), (refused > 0, 'refused'),
                   (giveup > 0, 'giveup'), (errors > 0, 'errors'), (budget > 300, 'budget>300'), (allun.get('air', 0) > 0, 'allunreach_air'),
                   (len(long_not_timeout) > 0, 'long_links_not_timeout')):
    if cond:
        fails.append(name)
print(f"{label}: {'PASS' if not fails else 'FAIL(' + ','.join(fails) + ')'} stuck={stuck} pocket={pocket} "
      f"repaths={repath_total}{'(FLAG>120)' if repath_total > 120 else ''} [{' '.join(f'{k}={v}' for k, v in sorted(repaths.items()))}] "
      f"rules=[{' '.join(f'{k}={v}' for k, v in sorted(rules.items()))}] max_dwell_s={max_dwell} [{max_dwell_at}] "
      f"link_max_dt={link_max_dt:.2f} [{link_max_desc}] link_fail_reasons=[{' '.join(f'{k}={v}' for k, v in sorted(reasons.items()))}] "
      f"long_links_not_timeout=[{','.join(long_not_timeout)}] bursts=[{','.join(bursts)}] pickups={pickups} mega={len(mega_times)} kills={kills} "
      f"suicides={suicides} refused={refused} giveup={giveup} errors={errors} supp1={supp_share:.1f}% fight_share={fight_share:.1f}% "
      f"armed_fight_share={armed_share:.1f}% ({vis_armed_fight}/{vis_armed}) allunreach_air={allun.get('air', 0)} allunreach_ground={allun.get('ground', 0)} "
      f"budget_mean_us={budget:.1f} run_mean_us={run_mean}")
print(f"  strikes: {' '.join(strike_list) if strike_list else '-'}")
for bot, n in repaths.items():
    if n > 120:
        tg = collections.Counter(t for _, t in repath_targets[bot]).most_common(4)
        print(f"  REPATH FLAG {bot}: {n} repaths, top targets {tg}, last at {repath_targets[bot][-1][0]}")
with open(path + '.json', 'w') as jf:
    json.dump({'label': label, 'stuck': stuck, 'pocket': pocket, 'bursts': len(bursts), 'max_dwell': max_dwell, 'link_max_dt': link_max_dt,
               'kills': kills, 'pickups': pickups, 'mega': len(mega_times), 'repaths': repath_total, 'fails': fails,
               'rules': dict(rules), 'speed_strikes': [x for x in strike_list if ':speed' in x]}, jf)
