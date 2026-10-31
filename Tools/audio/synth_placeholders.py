#!/usr/bin/env python3
"""Velox placeholder audio (ADR 0006 section 9, task M5-10).

Synthesises every sound of the minimal set with the standard library only (wave, math, random, struct): mono 16-bit
WAVs at SAMPLE_RATE. Each weapon gets a distinct timbre (noise burst, chirp, square, saw, sine plus envelope) so a tester
can tell them apart. Announcer lines come from espeak-ng when `which espeak-ng` succeeds, else two-tone stingers.

  python3 -I Tools/audio/synth_placeholders.py --out ArtSource/Audio/placeholders

Deterministic (fixed random seed). The file names are the contract with Tools/editor/create_audio_content.py and
docs/art/audio-list.md. Replace a WAV by a real one of the same name and re-run the importer.
"""
import argparse
import math
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

SAMPLE_RATE = 22050  # mono placeholders; espeak-ng's native rate. Keeps the whole set far below the LFS budget.
TWO_PI = 2.0 * math.pi
LIMIT_BYTES = 15 * 1024 * 1024


def n(seconds):
    return max(1, int(seconds * SAMPLE_RATE))


class Rng:
    def __init__(self, seed):
        self.r = random.Random(seed)

    def noise(self, count):
        return [self.r.uniform(-1.0, 1.0) for _ in range(count)]


# ---------------------------------------------------------------------------------------------- oscillators

def sine(freq, dur, phase=0.0):
    return [math.sin(TWO_PI * freq * i / SAMPLE_RATE + phase) for i in range(n(dur))]


def square(freq, dur):
    return [1.0 if math.sin(TWO_PI * freq * i / SAMPLE_RATE) >= 0 else -1.0 for i in range(n(dur))]


def saw(freq, dur):
    return [2.0 * ((freq * i / SAMPLE_RATE) % 1.0) - 1.0 for i in range(n(dur))]


def chirp(f0, f1, dur, wave_fn="sine"):
    """Frequency sweep f0 -> f1 (linear), phase-accumulated."""
    out, ph = [], 0.0
    count = n(dur)
    for i in range(count):
        f = f0 + (f1 - f0) * i / max(count - 1, 1)
        ph += TWO_PI * f / SAMPLE_RATE
        if wave_fn == "sine":
            out.append(math.sin(ph))
        elif wave_fn == "square":
            out.append(1.0 if math.sin(ph) >= 0 else -1.0)
        else:
            out.append(2.0 * ((ph / TWO_PI) % 1.0) - 1.0)
    return out


# ---------------------------------------------------------------------------------------------- shaping

def env_exp(sig, decay_seconds, attack=0.002):
    """Fast linear attack, exponential decay (time constant decay_seconds)."""
    a = n(attack)
    return [s * min(1.0, i / a) * math.exp(-i / (decay_seconds * SAMPLE_RATE)) for i, s in enumerate(sig)]


def env_adsr(sig, attack, release):
    count = len(sig)
    a, r = n(attack), n(release)
    return [s * min(1.0, i / a) * min(1.0, (count - i) / r) for i, s in enumerate(sig)]


def lowpass(sig, cutoff):
    a = 1.0 - math.exp(-TWO_PI * cutoff / SAMPLE_RATE)
    y, out = 0.0, []
    for s in sig:
        y += a * (s - y)
        out.append(y)
    return out


def highpass(sig, cutoff):
    lp = lowpass(sig, cutoff)
    return [s - v for s, v in zip(sig, lp)]


def mix(*parts):
    """Sum signals; a part is a signal or a (signal, gain, offset_seconds) tuple."""
    items = [p if isinstance(p, tuple) else (p, 1.0, 0.0) for p in parts]
    length = max(len(s) + (n(o) if o else 0) for s, _, o in items)
    out = [0.0] * length
    for s, g, o in items:
        off = n(o) if o else 0
        for i, v in enumerate(s):
            out[off + i] += v * g
    return out


def gain(sig, g):
    return (sig, g, 0.0)


def concat(*parts):
    out = []
    for p in parts:
        out.extend(p)
    return out


def silence(dur):
    return [0.0] * n(dur)


def tremolo(sig, rate, depth):
    return [s * (1.0 - depth + depth * 0.5 * (1.0 + math.sin(TWO_PI * rate * i / SAMPLE_RATE))) for i, s in enumerate(sig)]


def loop_fade(sig, fade=0.01):
    """Make a loop click-free: cross-fade the tail into the head."""
    f = n(fade)
    out = list(sig)
    for i in range(f):
        t = i / f
        out[i] = sig[i] * t + sig[len(sig) - f + i] * (1.0 - t)
    return out[: len(sig) - f]


def normalise(sig, peak=0.8):
    m = max((abs(s) for s in sig), default=0.0)
    if m < 1e-9:
        return sig
    return [s * peak / m for s in sig]


# ---------------------------------------------------------------------------------------------- the set

def voice(f0, dur, vib=6.0):
    out, ph = [], 0.0
    for i in range(n(dur)):
        f = f0 * (1.0 + 0.04 * math.sin(TWO_PI * vib * i / SAMPLE_RATE))
        ph += TWO_PI * f / SAMPLE_RATE
        out.append(2.0 * ((ph / TWO_PI) % 1.0) - 1.0)
    return lowpass(out, 1400)


def blip(freq, dur=0.09):
    return env_exp(sine(freq, dur), dur * 0.5)


def build(rng):
    S = {}

    # Weapon fire: one distinct timbre each.
    S["wpn_machinegun_fire"] = mix(env_exp(highpass(rng.noise(n(0.09)), 900), 0.02), gain(env_exp(sine(180, 0.05), 0.015), 0.7))
    S["wpn_shotgun_fire"] = mix(env_exp(lowpass(rng.noise(n(0.35)), 3500), 0.08), env_exp(sine(85, 0.25), 0.08))
    S["wpn_plasmagun_fire"] = mix(env_exp(chirp(2200, 500, 0.14, "sine"), 0.05), gain(env_exp(chirp(1100, 250, 0.14, "square"), 0.04), 0.35))
    S["wpn_rocketlauncher_fire"] = mix(env_exp(lowpass(rng.noise(n(0.45)), 900), 0.14), gain(env_exp(saw(110, 0.4), 0.12), 0.6))
    S["wpn_railgun_fire"] = mix(env_exp(chirp(250, 3200, 0.35, "sine"), 0.12), gain(env_exp(sine(4800, 0.5), 0.2), 0.3), gain(env_exp(rng.noise(n(0.05)), 0.01), 0.5))
    # Lightning gun: start, seamless loop, stop.
    S["wpn_lightninggun_start"] = env_adsr(mix(saw(95, 0.12), gain(highpass(rng.noise(n(0.12)), 1500), 0.5)), 0.004, 0.04)
    loop = mix(saw(120, 0.52), gain(sine(240, 0.52), 0.5), gain(tremolo(highpass(rng.noise(n(0.52)), 2500), 40, 0.8), 0.45))
    S["wpn_lightninggun_loop"] = loop_fade(loop)
    S["wpn_lightninggun_stop"] = mix(env_exp(saw(100, 0.2), 0.05), gain(env_exp(highpass(rng.noise(n(0.2)), 2000), 0.04), 0.5))

    # Impacts and hit confirm.
    S["imp_bullet"] = env_exp(highpass(rng.noise(n(0.06)), 1500), 0.012)
    S["imp_plasma"] = mix(env_exp(highpass(rng.noise(n(0.18)), 3000), 0.05), gain(env_exp(chirp(900, 2400, 0.1), 0.03), 0.5))
    S["imp_rocket_explosion"] = mix(env_exp(lowpass(rng.noise(n(0.9)), 700), 0.25), env_exp(sine(55, 0.8), 0.3), gain(env_exp(rng.noise(n(0.08)), 0.015), 0.6))
    S["imp_rail_hit"] = mix(env_exp(sine(2100, 0.45), 0.12), gain(env_exp(sine(3150, 0.45), 0.09), 0.5))
    S["imp_lightning_hit"] = env_exp(tremolo(highpass(rng.noise(n(0.14)), 2200), 70, 0.9), 0.04)
    S["hit_confirm"] = concat(env_exp(sine(1300, 0.05), 0.02), env_exp(sine(1950, 0.07), 0.025))

    # Player: pain by band (Q3 25/50/100), death, jump, landing, footsteps.
    S["ply_pain_25"] = env_adsr(voice(250, 0.22), 0.01, 0.1)
    S["ply_pain_50"] = env_adsr(voice(200, 0.28), 0.01, 0.12)
    S["ply_pain_100"] = env_adsr(voice(150, 0.34), 0.01, 0.14)
    S["ply_death"] = env_adsr(lowpass(chirp(380, 70, 0.7, "saw"), 1200), 0.01, 0.2)
    S["ply_jump"] = env_adsr(lowpass(chirp(140, 300, 0.14, "saw"), 1100), 0.01, 0.05)
    S["ply_land_soft"] = env_exp(lowpass(rng.noise(n(0.1)), 600), 0.03)
    S["ply_land_hard"] = mix(env_exp(lowpass(rng.noise(n(0.22)), 450), 0.07), gain(env_exp(sine(70, 0.2), 0.07), 0.9))
    for i, cutoff in enumerate((900, 1200, 750, 1050), start=1):
        S["ply_step_%d" % i] = env_exp(lowpass(rng.noise(n(0.09)), cutoff), 0.02)

    S["world_jumppad"] = mix(env_exp(chirp(220, 1400, 0.28, "sine"), 0.14), gain(env_exp(chirp(110, 700, 0.28, "square"), 0.1), 0.3))

    # Pickups.
    S["pickup_health_small"] = blip(880)
    S["pickup_health_medium"] = concat(blip(660), blip(880))
    S["pickup_health_large"] = concat(blip(550), blip(740), blip(990))
    S["pickup_health_mega"] = concat(blip(440, 0.1), blip(587, 0.1), blip(740, 0.1), blip(880, 0.1), env_exp(sine(1175, 0.35), 0.15))
    S["pickup_armor_shard"] = mix(env_exp(sine(2600, 0.12), 0.04), gain(env_exp(sine(3900, 0.12), 0.03), 0.4))
    S["pickup_armor"] = mix(env_exp(square(420, 0.2), 0.06), gain(env_exp(highpass(rng.noise(n(0.2)), 2500), 0.05), 0.5))
    S["pickup_weapon"] = mix(env_exp(square(300, 0.2), 0.07), gain(env_exp(rng.noise(n(0.03)), 0.008), 0.8), (env_exp(square(450, 0.15), 0.06), 0.8, 0.07))
    S["pickup_ammo"] = concat(env_exp(highpass(rng.noise(n(0.04)), 1800), 0.01), silence(0.03), env_exp(highpass(rng.noise(n(0.05)), 900), 0.015))
    S["pickup_powerup"] = mix(*[(env_exp(sine(f, 0.9), 0.35), 0.5, 0.12 * i) for i, f in enumerate((330, 440, 554, 660, 880))])
    S["world_item_respawn"] = tremolo(env_adsr(mix(sine(520, 0.45), gain(sine(780, 0.45), 0.5)), 0.12, 0.25), 14, 0.5)

    # Weapon handling, UI.
    S["wpn_switch"] = concat(env_exp(rng.noise(n(0.025)), 0.006), silence(0.05), env_exp(lowpass(rng.noise(n(0.03)), 2500), 0.008))
    S["wpn_dryfire"] = env_exp(mix(rng.noise(n(0.03)), gain(sine(160, 0.03), 0.8)), 0.008)
    S["ui_click"] = env_exp(sine(1100, 0.04), 0.012)
    return S


def read_wav(path):
    with wave.open(path, "rb") as w:
        assert w.getsampwidth() == 2 and w.getnchannels() == 1, "espeak-ng output must be mono 16-bit"
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())
    samples = [v / 32768.0 for v in struct.unpack("<%dh" % (len(raw) // 2), raw)]
    if rate != SAMPLE_RATE:  # nearest-neighbour is enough for a placeholder
        ratio = rate / SAMPLE_RATE
        samples = [samples[min(int(i * ratio), len(samples) - 1)] for i in range(int(len(samples) / ratio))]
    return samples


def announcer(lines_path):
    """name -> samples; espeak-ng when installed, else a two-tone stinger per line."""
    out = {}
    lines = []
    with open(lines_path, encoding="utf-8") as f:
        for raw in f:
            raw = raw.strip()
            if raw and not raw.startswith("#"):
                name, text = raw.split("|", 1)
                lines.append((name.strip(), text.strip()))
    use_espeak = shutil.which("espeak-ng") is not None
    tmp = tempfile.mkdtemp(prefix="velox_announcer_")
    try:
        for idx, (name, text) in enumerate(lines):
            if use_espeak:
                wav = os.path.join(tmp, name + ".wav")
                subprocess.run(["espeak-ng", "-v", "en-us", "-s", "150", "-p", "40", "-w", wav, text], check=True)
                sig = read_wav(wav)
            else:
                f0 = 440.0 * 2 ** (idx / 12.0)
                sig = concat(env_exp(sine(f0, 0.25), 0.1), env_exp(sine(f0 * 1.5, 0.3), 0.12))
            out["ann_" + name] = sig
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return out, use_espeak


def write_wav(path, sig):
    sig = normalise(sig)
    pcm = struct.pack("<%dh" % len(sig), *[int(max(-1.0, min(1.0, s)) * 32767) for s in sig])
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="ArtSource/Audio/placeholders")
    ap.add_argument("--lines", default=os.path.join(here, "announcer_lines.txt"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    sounds = build(Rng(1337))
    ann, used_espeak = announcer(args.lines)
    sounds.update(ann)
    total = 0
    for name in sorted(sounds):
        path = os.path.join(args.out, name + ".wav")
        write_wav(path, sounds[name])
        size = os.path.getsize(path)
        total += size
        print("%-28s %7d bytes %5.2f s" % (name + ".wav", size, len(sounds[name]) / SAMPLE_RATE))
    print("%d files, %.2f MB total (announcer: %s)" % (len(sounds), total / 1048576.0, "espeak-ng" if used_espeak else "tone stingers"))
    if total > LIMIT_BYTES:
        print("ERROR: over the %d MB budget" % (LIMIT_BYTES // 1048576), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
