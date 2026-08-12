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
import math
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

    # --- weapons that had been firing the shotgun --------------------------
    #
    # Every weapon played `shot` because the shotgun was the only gun when the
    # table was written and a row needs SOME sound. That is a placeholder that
    # stopped being one the moment there were four weapons, and it was actively
    # misleading: a chainsaw that goes off like a 12-gauge tells the player
    # their weapon is something it is not, which is the same fault the axe's
    # muzzle flash was.
    #
    # 모든 무기가 `shot`을 재생했습니다. 표를 쓸 당시 총이 샷건뿐이었고 행에는 *어떤*
    # 소리든 필요했기 때문입니다. 무기가 넷이 된 순간 그것은 임시방편이기를 그만두었고,
    # 적극적으로 오해를 부릅니다. 12게이지처럼 터지는 전기톱은 플레이어에게 자기 무기가
    # 아닌 것을 말하며, 도끼의 총구 섬광과 같은 결함입니다.
    'launch': 'dsrlaunc',   # the grenade leaving the tube
    'plasma': 'dsplasma',   # the rapid gun's bolts

    # THE SAW IS THREE SOUNDS, not one, and that is Doom's design rather than
    # ours: DSSAWUP when it is drawn, DSSAWFUL while it swings, DSSAWHIT when
    # it bites. Idle (DSSAWIDL) is deliberately left out -- Doom loops it every
    # tic while the saw is merely held, and a sound with no event behind it is
    # one this mixer would have to gain a looping voice to carry.
    # 톱은 하나가 아니라 세 소리이며, 우리가 아니라 Doom의 설계입니다. 뽑을 때 DSSAWUP,
    # 휘두르는 동안 DSSAWFUL, 물어뜯을 때 DSSAWHIT입니다. 대기음(DSSAWIDL)은 일부러
    # 뺐습니다. Doom은 톱을 들고만 있어도 매 틱 반복 재생하는데, 뒤에 사건이 없는 소리를
    # 위해 이 믹서가 루프 보이스를 갖춰야 합니다.
    'sawup':  'dssawup',    # drawing it
    'saw':    'dssawful',   # a swing
    'sawhit': 'dssawhit',   # a swing that connected

    # The blast. Doom's barrel and its rocket share this lump, which is the
    # right one for a grenade for the same reason: it is the sound of a thing
    # bursting rather than of a gun going off.
    # 폭발음입니다. Doom의 폭발통과 로켓이 이 럼프를 공유하며, 유탄에도 같은 이유로
    # 맞습니다. 총이 발사되는 소리가 아니라 무언가가 터지는 소리이기 때문입니다.
    'blast':  'dsbarexp',
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


def resample_fractional(rate, s):
    """Any rate down to TARGET_RATE, for the lumps that are not a clean halving.

    Freedoom is nearly all 11025 and 22050, and DSRLAUNC is 16000 -- 1.451:1,
    which no amount of pair-averaging reaches. This is the general path and it
    is used ONLY when the halving path cannot finish, so every lump that was
    already imported still goes through exactly the code that produced the WAV
    committed beside this script, and re-running reproduces them byte for byte.
    A single general resampler would have been tidier and would have silently
    rewritten seventeen files whose current contents are the reference.

    Lowpass first, then interpolate. The order is the whole point: the new
    Nyquist is 5512Hz and 16000 carries content up to 8000, so interpolating
    first would fold that band down into the audible one -- the same aliasing
    the pair-averaging above exists to avoid, arriving by a different route.

    Freedoom은 대부분 11025와 22050이고 DSRLAUNC는 16000, 즉 1.451:1이라 짝 평균으로는
    닿을 수 없습니다. 이것이 일반 경로이며 반감 경로가 끝내지 못할 때만 쓰이므로, 이미
    가져온 모든 럼프는 이 스크립트 옆에 커밋된 WAV를 만들어 낸 바로 그 코드를 그대로
    거치고 재실행하면 바이트 단위로 재현됩니다. 일반 리샘플러 하나로 통일하는 편이
    깔끔했겠지만, 현재 내용이 기준인 파일 열일곱 개를 조용히 다시 썼을 것입니다.

    먼저 저역통과, 그다음 보간입니다. 순서가 핵심입니다. 새 나이퀴스트는 5512Hz이고
    16000은 8000까지 담고 있으므로, 먼저 보간하면 그 대역이 가청 대역으로 접혀 내려옵니다.
    위의 짝 평균이 피하려는 바로 그 앨리어싱이 다른 경로로 도착하는 것입니다.
    """
    # Windowed-sinc lowpass at the destination Nyquist, with a little margin so
    # the transition band lands below it rather than straddling it.
    cutoff = TARGET_RATE * 0.45 / rate      # cycles per source sample
    half   = 32                             # taps either side; 65 total
    taps = []
    for i in range(-half, half + 1):
        if i == 0:
            h = 2.0 * cutoff
        else:
            x = 2.0 * math.pi * cutoff * i
            h = math.sin(x) / (math.pi * i)
        # Hann window: the rectangular truncation of a sinc rings, and ringing
        # on a transient is a click.
        h *= 0.5 - 0.5 * math.cos(2.0 * math.pi * (i + half) / (2 * half))
        taps.append(h)
    norm = sum(taps)
    taps = [t / norm for t in taps]

    # Around the 128 midpoint, so the filter's edges do not pull towards zero
    # and put a click at each end of an 8-bit lump that idles at 128.
    centred = [v - 128.0 for v in s]
    filtered = []
    n = len(centred)
    for i in range(n):
        acc = 0.0
        for k, t in enumerate(taps):
            j = i + k - half
            if j < 0: j = 0
            elif j >= n: j = n - 1
            acc += centred[j] * t
        filtered.append(acc)

    # Linear interpolation onto the new grid. The lowpass above is what makes
    # linear adequate here: there is nothing left near Nyquist for its gentle
    # rolloff to get wrong.
    step = rate / float(TARGET_RATE)
    out = []
    pos = 0.0
    while pos < n - 1:
        i = int(pos)
        f = pos - i
        v = filtered[i] * (1.0 - f) + filtered[i + 1] * f
        v = int(round(v + 128.0))
        out.append(0 if v < 0 else (255 if v > 255 else v))
        pos += step
    return TARGET_RATE, out


def to_target(rate, s):
    while rate > TARGET_RATE:
        if rate // 2 < TARGET_RATE:
            return resample_fractional(rate, s)
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
