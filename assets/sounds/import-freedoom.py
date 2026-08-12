#!/usr/bin/env python3
"""Fetch Freedoom's sounds and prepare them for this project's mixer.

    python import-freedoom.py

THIS IS AN AUTHORING TOOL AND THE BUILD DOES NOT RUN IT, exactly as
assets/sprites/import-freedoom.py is not run. It writes the WAVs next to
itself and those are committed; bake.ps1 is what turns them into the ADPCM the
game carries. Kept because the WAVs here are the RESULT of a conversion --
resampled, trimmed, level-matched -- and this file is the recipe for it.

Freedoom is BSD-3 licensed. Running this puts its audio in the tree, and
bake.ps1 then requires the attribution notice in src/scene.c to match
docs/LICENSE-Freedoom.txt word for word, the same way it does for the artwork.

WHAT THE CONVERSION HAS TO GET RIGHT
------------------------------------
RATE. The mixer runs at 44100 and Doom's sounds are 11025, which is exactly
4:1 -- so everything lands here at 11025 and the mixer steps one source sample
every four output samples with no resampler and no phase error. Half of
Freedoom's lumps are 22050, and those are decimated by two; done by averaging
pairs rather than dropping every other sample, because dropping is a
brick-wall decimation that folds everything above 5.5kHz back down as aliasing
and a shotgun turns into a hiss.

TRIMMING. Doom's lumps carry a lot of trailing near-silence, and silence costs
the same per sample as a gunshot. Trimmed at a threshold rather than exactly
zero, because 8-bit audio idles at 128 +/- 1 and never reaches it.

DEDUPING is bake's job, not this script's: several of our sounds legitimately
want the same lump, and one file on disk per lump is what makes that visible.
"""
import os
import struct
import sys
import urllib.request
import wave

RAW = 'https://raw.githubusercontent.com/freedoom/freedoom/master/sounds/%s.wav'
HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, '.freedoom-cache')

TARGET_RATE = 11025
SILENCE = 3          # 8-bit units either side of the 128 midpoint
MAX_SECONDS = 1.1    # nothing here needs to be longer; see TRIMMING above

# our sound name -> the Freedoom lump it comes from.
#
# Names not listed keep the synthesised recipe in ../sounds.txt, and that is
# the point rather than an omission: `pump`, `hook` and `hreel` have no Doom
# equivalent because Doom has no pump-action rack, no grapple and no reel. A
# sound is a recipe OR a sample, whichever exists, so the two kinds live side
# by side and a gap in one is covered by the other.
SOUNDS = {
    'shot':   'dsshotgn',   # the shotgun itself
    'impact': 'dspunch',    # something taking a hit
    'sight':  'dsposit1',   # a monster noticing you
    'eatt':   'dssgtatk',   # and lunging
    'epain':  'dspopain',
    'edie':   'dspodth1',
    'phurt':  'dsplpain',
    'pdie':   'dspldeth',
    'pammo':  'dsitemup',
    'pmed':   'dsgetpow',
    'dry':    'dsnoway',    # the trigger that does nothing
    'exit':   'dsswtchx',
    'win':    'dswpnup',
    'ecast':  'dsfirsht',   # the caster's bolt leaving
    'ehit':   'dspunch',
    'hland':  'dsoof',      # the axe's slam landing
    'hbite':  'dspunch',
    'hbiteb': 'dspunch',
    'door':   'dsdoropn',
    'switch': 'dsswtchn',
    'key':    'dsitemup',
}


def fetch(lump):
    os.makedirs(CACHE, exist_ok=True)
    p = os.path.join(CACHE, lump + '.wav')
    if not os.path.exists(p):
        with urllib.request.urlopen(RAW % lump, timeout=60) as r:
            open(p, 'wb').write(r.read())
    return p


def load(path):
    w = wave.open(path, 'rb')
    if w.getsampwidth() != 1 or w.getnchannels() != 1:
        sys.exit('%s is not 8-bit mono; this importer only handles Doom lumps'
                 % path)
    rate = w.getframerate()
    data = list(w.readframes(w.getnframes()))
    w.close()
    return rate, data


def to_target(rate, s):
    while rate > TARGET_RATE:
        if rate // 2 < TARGET_RATE:
            sys.exit('rate %d is not a power-of-two multiple of %d'
                     % (rate, TARGET_RATE))
        # Average pairs. Dropping every other sample instead aliases
        # everything above the new Nyquist straight back into the audible band.
        s = [(s[i] + s[i + 1] + 1) // 2 for i in range(0, len(s) - 1, 2)]
        rate //= 2
    return rate, s


def trim(s):
    lo = hi = None
    for i, v in enumerate(s):
        if abs(v - 128) > SILENCE:
            if lo is None:
                lo = i
            hi = i
    if lo is None:
        return s[:1]
    s = s[lo:hi + 1]
    cap = int(TARGET_RATE * MAX_SECONDS)
    if len(s) > cap:
        # Fade the last 5ms so a hard cut does not click.
        s = s[:cap]
        n = min(55, len(s))
        for i in range(n):
            k = i / float(n)
            s[len(s) - n + i] = int(round(128 + (s[len(s) - n + i] - 128) * (1 - k)))
    return s


def main():
    lumps = sorted(set(SOUNDS.values()))
    print('%-11s %6s %8s %8s %8s' % ('lump', 'rate', 'source', 'at 11k', 'trimmed'))
    total = 0
    for lump in lumps:
        rate, s = load(fetch(lump))
        n0 = len(s)
        rate2, s = to_target(rate, s)
        n1 = len(s)
        s = trim(s)
        total += len(s)
        print('%-11s %6d %8d %8d %8d' % (lump, rate, n0, n1, len(s)))

        out = os.path.join(HERE, lump + '.wav')
        w = wave.open(out, 'wb')
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(TARGET_RATE)
        w.writeframes(bytes(s))
        w.close()

    print('\n%d lumps, %d samples at %dHz' % (len(lumps), total, TARGET_RATE))
    print('as 4-bit ADPCM in this project\'s 6-bit alphabet: %d bytes'
          % int(total * 2 / 3))

    # The mapping is data the bake needs, and writing it here keeps it beside
    # the choice of lump rather than in a second list somewhere else.
    idx = os.path.join(HERE, 'sounds.map')
    with open(idx, 'w', newline='\n') as f:
        f.write('# <our sound name> <freedoom lump>, written by '
                'import-freedoom.py -- do not edit by hand.\n')
        for k in sorted(SOUNDS):
            f.write('%s %s\n' % (k, SOUNDS[k]))
    print('wrote', idx)


if __name__ == '__main__':
    main()
