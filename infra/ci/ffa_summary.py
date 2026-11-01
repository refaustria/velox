"""04g (copy of post04f/summary.py): D36 per-run caps, pocket strikes, VeloxBotRepath count, strike list; ramp.py dropped;
writes <log>.json for set.py (set-level sums). Original: 04f FFA soak summary of one server log: the D20 bands, the D34 dwell band, the 04f link / strike bands and the
report-only diagnostics. Copy of a04d/summary.py + the stall.py dwell scan + ramp.py max_dwell_s.
ADR 0006 section 7: per-map bands come from BAND_FFA_* environment variables (infra/ci/bands/<Map>.sh via lib.sh ci_load_bands; unset =
report-only), the map-independent invariants are fixed here: left_arena, world_kill, no_nav_point, giveup, bursts, max dwell <= 5 s, nav
build <= 2 s; the json also carries the item and link figures for ffa_set.py (item coverage, link success per type).
Link outcomes are judged by reason= (ADR 0006 E0.3 b): failed = missed, timeout, noimpulse, tooslow (and any unknown reason, printed by
ffa_set.py); not an attempt = replaced, below_start, ineligible, air_start, relaunched (task m5-06b; counted as aborted per type). link_types[type] = [ok, failed, aborted].
Pickup lines carry a trailing at=<X,Y,Z> (E0.3 c): resets_at / takes_at list [definition, at] pairs so ffa_set.py can judge every pickup
instance; a log without at= leaves at = null and ffa_set.py falls back to per definition.
Dwell (ADR 0006 section 7, E2): the 5 s run rule excludes camp=1 samples and timed-wait samples (a bot standing at the spawn point of an
unavailable item it chose as an Item goal, ADR 0005 section 7 item timing); the run line prints timed_waits / wait_samples / wait_max_s /
wait_capped and three FLAG lines, the json carries the same keys. Same definition and constants as infra/ci/nettest.sh; infra/ci/dwellwhy.py triages.
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
RUN_STUCK_MAX = 7  # D36/D51 (7): per-run stuck ceiling
DWELL_MAX = 5      # ADR 0006 section 7 (was 8, D34)
WAIT_RADIUS = 300.0      # E2: a sample counts as "at the item" within this 2-D distance of the VeloxPickupSpawn position
WAIT_MAX_S = 20.0        # E2: an episode never lasts longer than min(respawn_in, this) + WAIT_SLACK_S (the D39 camp horizon)
WAIT_SLACK_S = 1.0       # E2: slack on the episode end, and the tolerance of the "capped" count (respawn later than R + this)
WAIT_LEAD_FLAG_S = 16.0  # E2: FLAG when a route line's lead exceeds the router gate (ADR 0005 section 7 / D16: 8 + 8)
NAV_BUILD_MAX_S = 2.0
ARENA_FLOOR_Z = float(os.environ.get('VELOX_ARENA_FLOOR_Z', '0'))
LEFT_ARENA_DZ = 1500.0


def band(key):
    v = os.environ.get('BAND_' + key, '')
    return int(v) if v.strip() else None


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
fight_yields = 0   # task m5-06b: VeloxBotFightYield lines (fight stand-off rule; report-only)
live = False
camp = {}
dwell = {}
max_dwell, max_dwell_at = 0, '-'
pocket = 0
nav_secs = []
no_nav = world_kills = not_at_goal = 0
left_arena = None
LINK_FAILED_REASONS = ('missed', 'timeout', 'noimpulse', 'tooslow')       # ADR 0006 E0.3 (b)
# Task m5-06b: air_start / relaunched (a flight that was not the link's: begun in the air, or re-launched away from the end).
LINK_NOT_ATTEMPT_REASONS = ('replaced', 'below_start', 'ineligible', 'air_start', 'relaunched')      # re-plans (D28): not attempts
link_types = collections.defaultdict(lambda: [0, 0, 0])   # type -> [ok, failed, aborted]
link_unknown = collections.Counter()   # result/reason outside both lists: counted failed
resets = []
resets_at = []
takes_at = []
AT = re.compile(r' at=(V\(.*?\))')
spawns = []
taken = collections.Counter()
strike_list = []
repaths = collections.Counter()
repath_targets = collections.defaultdict(list)
# ADR 0006 E2 (b): wait_ep[bot] = open episode {X: DA name, t0, R, lead, samples}
wait_ep = {}
wait_spawn = {}
wait_eps_done = []
wait_samples = wait_capped = avail0_routes = 0
wait_max = (0.0, '-')
wait_lead_flags = []
wait_nopos = set()
easy_seen = False


def vec2(txt):
    """(X, Y) of a ToCompactString vector body; zero components are omitted (V(X=500.00) is (500, 0, 0))."""
    d = dict(re.findall(r'([XYZ])=([-\d.]+)', txt))
    return float(d.get('X', 0)), float(d.get('Y', 0))


def wait_close(name, now, respawn):
    """Closes the bot's episode; returns 1 when closed by a respawn line later than R + WAIT_SLACK_S after t0 (capped)."""
    w = wait_ep.pop(name, None)
    if not w:
        return 0
    wait_eps_done.append(w['samples'])
    return int(bool(respawn and now is not None and w['t0'] is not None and now - w['t0'] > w['R'] + WAIT_SLACK_S))


# Task m5-06b: engine noise, not a Velox error: UE 5.8 prints this once on the first launch of a fresh binary (the Dataflow
# plugin's reflected struct). Exactly this message is not counted in errors=.
ENGINE_NOISE_RE = re.compile(r'LogClass: Error: StructProperty FDataflowToolNodeSnapshot::Date ')

STUCK_AT = re.compile(r'VeloxBotStuck bot=(\S+) at=V\(X=([-\d.]+), Y=([-\d.]+), Z=([-\d.]+)\)')

with open(path, 'rb') as f:
    for raw in f:
        line = raw.decode('utf-8', 'replace').rstrip()
        if ('Error:' in line or 'Ensure condition' in line or 'ensure(' in line.lower() and 'LogOutputDevice' in line) and not ENGINE_NOISE_RE.search(line):
            errors += 1
        if 'VeloxMatch phase=Live' in line:
            live = True
            dwell.clear()
            continue
        m = re.search(r'VeloxPickupRespawn item=(\S+)', line)
        if m:                                       # E2 (b): closes the episodes of that item
            for n_, w_ in list(wait_ep.items()):
                if w_['X'] == m.group(1):
                    wait_capped += wait_close(n_, ts(line), True)
            continue
        m = re.search(r'VeloxNav built .*?seconds=([\d.]+)', line)
        if m:
            nav_secs.append(float(m.group(1)))
            continue
        m = re.search(r'VeloxPickupReset item=(\S+)', line)
        if m:
            resets.append(m.group(1))
            am = AT.search(line)
            resets_at.append([m.group(1), am.group(1) if am else None])
            continue
        m = re.search(r'VeloxPickupSpawn item=(\S+) at=(V\(.*?\))', line)
        if m:
            spawns.append([m.group(1), m.group(2)])
            wait_spawn[m.group(1)] = vec2(m.group(2))
            continue
        if 'VeloxPickup item=' in line:
            m = re.search(r'by=(\S+)', line)
            if m and m.group(1) not in ('-', 'none'):
                im = re.search(r'item=(\S+)', line)
                taken[im.group(1)] += 1
                am = AT.search(line)
                takes_at.append([im.group(1), am.group(1) if am else None])
                pickups += 1
                if 'item=DA_Item_Weapon_' in line:
                    weapon_pickups += 1
                if 'item=DA_Item_Health_Mega' in line:
                    t = re.search(r' t=([\d.]+)', line)
                    mega_times.append(f"{t.group(1) if t else '?'}:{m.group(1)}")
            continue
        if 'VeloxKill victim=' in line and 'ignored' not in line:
            if re.search(r'tags=\[[^\]]*Damage\.Environment', line):
                world_kills += 1
            deaths += 1
            if 'suicide=0' in line:
                kills += 1
            elif 'suicide=1' in line:
                suicides += 1
            continue
        if 'VeloxBotFightYield ' in line:
            fight_yields += 1
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
        if 'result=no_nav_point' in line:
            no_nav += 1
        if 'VeloxBotPath' in line and 'result=not_at_goal' in line:
            not_at_goal += 1  # ADR 0006 E3 / 06b D4: 3-D arrival refused a stacked-item goal; report-only, expected 0 on DM1
        if 'giveup' in line and 'LogVeloxBots' in line:
            giveup += 1
        m = re.search(r'VeloxBotLink bot=(\S+) .*result=(\S+) reason=(\S+) dt=([\d.]+) link=(\S+)', line)
        if m:
            tm = re.search(r' type=(\S+)', line)
            lt = tm.group(1) if tm else '?'
            if m.group(2) == 'ok':
                link_types[lt][0] += 1
            elif m.group(3) in LINK_FAILED_REASONS:
                link_types[lt][1] += 1
            elif m.group(3) in LINK_NOT_ATTEMPT_REASONS:
                link_types[lt][2] += 1
            else:
                link_types[lt][1] += 1
                link_unknown[f'{m.group(2)}/{m.group(3)}'] += 1
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
            if left_arena is None and z < ARENA_FLOOR_Z - LEFT_ARENA_DZ:
                left_arena = name
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
            if 'difficulty=DA_Bot_Easy' in line:
                easy_seen = True
            if live:
                # D34: consecutive samples within 150 cm (2-D) of the run's first sample; camp=1 and E2 timed-wait samples excluded.
                excl = bool(camp.get(name, 0))
                w = wait_ep.get(name)
                t = ts(line)
                if w and not excl and t is not None and w['t0'] is not None:        # E2 (b): timed-wait sample
                    if t - w['t0'] > min(w['R'], WAIT_MAX_S) + WAIT_SLACK_S:
                        wait_close(name, t, False)
                    else:
                        wim = re.search(r' goal=item .*?item=(\S+)', line)
                        at = wait_spawn.get(w['X'])
                        if at is None:
                            wait_nopos.add(w['X'])
                        if wim and wim.group(1) == w['X'] and (at is None or ((x - at[0]) ** 2 + (y - at[1]) ** 2) ** 0.5 <= WAIT_RADIUS):
                            excl = True
                            wait_samples += 1
                            w['samples'] += 1
                            if t - w['t0'] > wait_max[0]:
                                wait_max = (t - w['t0'], f"{name}:{w['X'].replace('DA_Item_', '')}:R={w['R']:.1f},lead={w['lead']:.1f}")
                if excl:
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
            if re.search(r'VeloxBotRoute bot=\S+ goal=', line):          # E2 (b): any goal= line closes the episode, an Item one may open one
                wait_capped += wait_close(m.group(1), ts(line), False)
                if ' goal=Item ' in line and ' available=0 ' in line:
                    wi, wr, wl = (re.search(q, line) for q in (r' item=(\S+)', r' respawn_in=(-?[\d.]+)', r' lead=(-?[\d.]+)'))
                    if wi and wr:
                        avail0_routes += 1
                        lead = float(wl.group(1)) if wl else 0.0
                        wait_ep[m.group(1)] = {'X': 'DA_Item_' + wi.group(1).replace('.', '_'), 't0': ts(line), 'R': float(wr.group(1)),
                                               'lead': lead, 'samples': 0}
                        if lead > WAIT_LEAD_FLAG_S:
                            wait_lead_flags.append(f'{m.group(1)}:{wi.group(1)}:lead={lead:.1f}')
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

wait_eps_done += [w_['samples'] for w_ in wait_ep.values()]
timed_waits = sum(1 for e_ in wait_eps_done if e_ > 0)
wait_flags = []
if avail0_routes and wait_samples == 0 and not easy_seen:
    wait_flags.append(f'FLAG no timed-wait sample although {avail0_routes} Item routes had available=0 (log-format drift? ADR 0006 E2)')
for f_ in wait_lead_flags:
    wait_flags.append(f'FLAG timed wait beyond the gate {f_} > {int(WAIT_LEAD_FLAG_S)} s (router gate ADR 0005 section 7/D16)')
if wait_capped:
    wait_flags.append(f'FLAG timed wait capped x{wait_capped}')
wait_txt = (f"timed_waits={timed_waits}{' (Easy)' if (avail0_routes and wait_samples == 0 and easy_seen) else ''} wait_samples={wait_samples} "
            f"wait_max_s={wait_max[0]:.1f} [{wait_max[1]}] wait_capped={wait_capped}" + (' wait_pos=n/a' if (not wait_spawn or wait_nopos) else ''))

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
# Per-map bands (BAND_FFA_RUN_*): judged only when the map's band file has them, else report-only.
rk, rp_, rm = band('FFA_RUN_KILLS_MIN'), band('FFA_RUN_PICKUPS_MIN'), band('FFA_RUN_MEGA_MIN')
for cond, name in ((stuck > RUN_STUCK_MAX, f'stuck>{RUN_STUCK_MAX}'), (len(bursts) > 0, 'bursts'), (max_dwell > DWELL_MAX, f'dwell>{DWELL_MAX}'), (link_max_dt > 8.0, 'link_dt>8'),
                   (left_arena is not None, 'left_arena'), (world_kills > 0, 'world_kill'), (no_nav > 0, 'no_nav_point'),
                   (bool(nav_secs) and max(nav_secs) > NAV_BUILD_MAX_S, f'nav>{NAV_BUILD_MAX_S}s'),
                   (rk is not None and kills < rk, f'kills<{rk}'), (rp_ is not None and pickups < rp_, f'pickups<{rp_}'),
                   (rm is not None and len(mega_times) < rm, f'mega<{rm}'), (refused > 0, 'refused'),
                   (giveup > 0, 'giveup'), (errors > 0, 'errors'), (budget > 300, 'budget>300'), (allun.get('air', 0) > 0, 'allunreach_air'),
                   (len(long_not_timeout) > 0, 'long_links_not_timeout')):
    if cond:
        fails.append(name)
print(f"{label}: {'PASS' if not fails else 'FAIL(' + ','.join(fails) + ')'} stuck={stuck} pocket={pocket} "
      f"repaths={repath_total}{'(FLAG>120)' if repath_total > 120 else ''} [{' '.join(f'{k}={v}' for k, v in sorted(repaths.items()))}] "
      f"rules=[{' '.join(f'{k}={v}' for k, v in sorted(rules.items()))}] max_dwell_s={max_dwell} [{max_dwell_at}] {wait_txt} "
      f"link_max_dt={link_max_dt:.2f} [{link_max_desc}] link_fail_reasons=[{' '.join(f'{k}={v}' for k, v in sorted(reasons.items()))}] "
      f"long_links_not_timeout=[{','.join(long_not_timeout)}] bursts=[{','.join(bursts)}] pickups={pickups} mega={len(mega_times)} kills={kills} "
      f"suicides={suicides} refused={refused} giveup={giveup} errors={errors} supp1={supp_share:.1f}% fight_yields={fight_yields} fight_share={fight_share:.1f}% "
      f"armed_fight_share={armed_share:.1f}% ({vis_armed_fight}/{vis_armed}) allunreach_air={allun.get('air', 0)} allunreach_ground={allun.get('ground', 0)} "
      f"budget_mean_us={budget:.1f} run_mean_us={run_mean} nav_s={max(nav_secs) if nav_secs else 'n/a'} world_kills={world_kills} no_nav_point={no_nav} not_at_goal={not_at_goal} left_arena={left_arena or 'none'}")
print(f"  strikes: {' '.join(strike_list) if strike_list else '-'}")
for fl_ in wait_flags:
    print('  ' + fl_)
for bot, n in repaths.items():
    if n > 120:
        tg = collections.Counter(t for _, t in repath_targets[bot]).most_common(4)
        print(f"  REPATH FLAG {bot}: {n} repaths, top targets {tg}, last at {repath_targets[bot][-1][0]}")
with open(path + '.json', 'w') as jf:
    json.dump({'label': label, 'stuck': stuck, 'pocket': pocket, 'bursts': len(bursts), 'max_dwell': max_dwell, 'timed_waits': timed_waits, 'wait_samples': wait_samples,
               'wait_max_s': round(wait_max[0], 1), 'wait_capped': wait_capped, 'wait_flags': len(wait_flags), 'link_max_dt': link_max_dt,
               'kills': kills, 'pickups': pickups, 'mega': len(mega_times), 'repaths': repath_total, 'fails': fails,
               'rules': dict(rules), 'speed_strikes': [x for x in strike_list if ':speed' in x],
               'nav_seconds': max(nav_secs) if nav_secs else None, 'world_kills': world_kills, 'left_arena': left_arena, 'no_nav_point': no_nav, 'not_at_goal': not_at_goal,
               'giveup': giveup, 'link_types': dict(link_types), 'link_unknown': dict(link_unknown), 'resets': resets, 'spawns': spawns, 'taken': dict(taken),
               'resets_at': resets_at, 'takes_at': takes_at}, jf)
