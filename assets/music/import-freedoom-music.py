#!/usr/bin/env python3
"""Fetch Freedoom's music and convert it into this project's note streams.

WHY A CONVERTER AND NOT A PLAYER. Freedoom's music is ~130 MIDI files, and a
MIDI file is a score rather than a sound: hearing one needs a synthesiser with
an instrument bank, and this engine has four oscillators and no bank. Rendering
the tracks to audio is not an option either -- there is no Vorbis or MP3
decoder here, only DEFLATE, and one rendered track would eat a third of what is
left of the floppy.

So the parsing happens HERE, once, and the engine never sees a MIDI file. What
it gets is a flat list of notes with absolute times, which its existing
oscillators can play directly. The whole runtime cost is walking a sorted list.

WHAT IS LOST, said plainly: the instruments. A Freedoom track played through
four oscillators is a chiptune of itself -- the melody, the harmony and the
structure survive, the timbre does not. That is the trade the 1.44MB budget
forces, and it suits a game that already quantises its colour to 15 bits.

POLYPHONY IS REDUCED HERE TOO, not at runtime. The engine has twelve voices
and the sound effects need most of them, so music gets MUSIC_VOICES of them.
Choosing which notes survive is a decision with a lot of context -- which
channel carries the melody, which is bass, what is percussion -- and none of
that context exists at runtime. Doing it at bake time also means the cost is
paid once rather than every frame.

LICENCE. Freedoom is 3-clause BSD and its music was written for the project
specifically so that it could be freely licensed; it is not id Software's Doom
music, which is copyrighted and is NOT what this fetches. The same reasoning
that lets import-freedoom.py pull sprites applies unchanged.

Usage:
    python import-freedoom-music.py            # fetch, convert, write music.txt
    python import-freedoom-music.py --report   # ...and print what it did
"""

import argparse
import os
import struct
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = 'https://raw.githubusercontent.com/freedoom/freedoom/master/musics/'

# ours -> Freedoom lump. Three, because three is what fits: a track is a few KB
# of notes after reduction, and the budget has room for that but not for the
# other hundred and thirty.
TRACKS = [
    ('title', 'd_intro.mid'),   # the title screen
    ('level', 'd_e1m1.mid'),    # ordinary play
    ('boss',  'd_e1m8.mid'),    # the episode-boss slot, for when a brute is up
]

VOICES = 4          # keep in step with MUSIC_VOICES in music.h
DRUM_CHANNEL = 9    # MIDI channel 10, zero-based


# --------------------------------------------------------------- MIDI reading

def read_varlen(b, i):
    """A MIDI variable-length quantity: seven bits a byte, high bit continues."""
    v = 0
    while True:
        c = b[i]
        i += 1
        v = (v << 7) | (c & 0x7F)
        if not (c & 0x80):
            return v, i


def parse_midi(data):
    """Returns (notes, length_ms).

    notes are (start_ms, dur_ms, midi_note, velocity, channel, program).
    Tracks are merged, because what matters downstream is when a note sounds
    and not which track it was written on.
    """
    if data[:4] != b'MThd':
        raise ValueError('not a MIDI file')
    _, fmt, ntrks, division = struct.unpack('>IHHH', data[4:14])
    if division & 0x8000:
        raise ValueError('SMPTE timing is not supported')

    # Tempo defaults to 120bpm until a tempo meta says otherwise. Collected as
    # (tick, usec_per_quarter) so the tick->ms walk below can be exact rather
    # than assuming one tempo for the whole piece.
    tempos = []
    events = []          # (tick, channel, kind, a, b)  kind: 'on'|'off'|'prog'

    pos = 14
    for _ in range(ntrks):
        if data[pos:pos + 4] != b'MTrk':
            raise ValueError('expected MTrk')
        length = struct.unpack('>I', data[pos + 4:pos + 8])[0]
        i = pos + 8
        end = i + length
        tick = 0
        status = 0
        while i < end:
            delta, i = read_varlen(data, i)
            tick += delta
            b0 = data[i]
            if b0 & 0x80:
                status = b0
                i += 1
            # else: running status -- the previous status byte still applies

            if status == 0xFF:                      # meta
                mtype = data[i]; i += 1
                mlen, i = read_varlen(data, i)
                if mtype == 0x51 and mlen == 3:
                    tempos.append((tick, (data[i] << 16) | (data[i+1] << 8) | data[i+2]))
                i += mlen
            elif status in (0xF0, 0xF7):            # sysex
                mlen, i = read_varlen(data, i)
                i += mlen
            else:
                hi = status & 0xF0
                ch = status & 0x0F
                if hi in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                    a, b = data[i], data[i+1]; i += 2
                    if hi == 0x90 and b > 0:
                        events.append((tick, ch, 'on', a, b))
                    elif hi == 0x80 or (hi == 0x90 and b == 0):
                        events.append((tick, ch, 'off', a, 0))
                elif hi in (0xC0, 0xD0):
                    a = data[i]; i += 1
                    if hi == 0xC0:
                        events.append((tick, ch, 'prog', a, 0))
                else:
                    raise ValueError('unknown status 0x%02X' % status)
        pos = end

    if not tempos:
        tempos = [(0, 500000)]
    tempos.sort()
    events.sort(key=lambda e: e[0])

    def tick_to_ms(t):
        ms = 0.0
        prev_tick, prev_us = 0, tempos[0][1]
        for tt, us in tempos:
            if tt >= t:
                break
            ms += (tt - prev_tick) * prev_us / division / 1000.0
            prev_tick, prev_us = tt, us
        ms += (t - prev_tick) * prev_us / division / 1000.0
        return ms

    # Pair note-ons with their offs. A second on for a pitch already sounding
    # ends the first, which is what a sequencer does and what several of these
    # files rely on.
    program = [0] * 16
    open_note = {}
    notes = []
    for tick, ch, kind, a, b in events:
        if kind == 'prog':
            program[ch] = a
            continue
        key = (ch, a)
        if kind == 'on':
            if key in open_note:
                st, vel = open_note.pop(key)
                notes.append((st, tick, a, vel, ch, program[ch]))
            open_note[key] = (tick, b)
        else:
            if key in open_note:
                st, vel = open_note.pop(key)
                notes.append((st, tick, a, vel, ch, program[ch]))

    last = max((t for t, _, _, _, _ in events), default=0)
    for (ch, a), (st, vel) in open_note.items():
        notes.append((st, last, a, vel, ch, program[ch]))

    out = []
    for st, en, note, vel, ch, prog in notes:
        s_ms = tick_to_ms(st)
        e_ms = tick_to_ms(en)
        if e_ms - s_ms < 12:        # nothing shorter than a click survives
            e_ms = s_ms + 12
        out.append((int(round(s_ms)), int(round(e_ms - s_ms)), note, vel, ch, prog))
    out.sort(key=lambda n: (n[0], -n[3]))
    return out, int(round(tick_to_ms(last)))


# ------------------------------------------------------------- voice reduction

def wave_for(channel, program):
    """MIDI programme -> one of the engine's four oscillators.

    Coarse on purpose. There are 128 programmes and four waves, so this is not
    a mapping anybody could make faithful; what it does instead is keep the
    ROLES apart -- bass reads as bass, lead as lead, drums as noise -- which is
    what makes a reduced arrangement still sound like the piece.
    """
    if channel == DRUM_CHANNEL:
        return 3                      # noise
    if 32 <= program <= 39:           # basses
        return 1                      # saw: more body low down
    if 80 <= program <= 87:           # synth leads
        return 0                      # square
    if 24 <= program <= 31:           # guitars
        return 1
    if 0 <= program <= 7:             # pianos
        return 2                      # sine
    return 0


def reduce_polyphony(notes, voices):
    """Keeps at most `voices` notes sounding at once.

    Later arrivals lose to earlier ones rather than the reverse: a note already
    sounding is part of what the listener is hearing, and cutting it to admit a
    new one is audible in a way that dropping the newcomer is not. Ties go to
    the louder note, which the sort in parse_midi already arranged.
    """
    ends = []           # end-time per busy voice
    kept = []
    dropped = 0
    for st, dur, note, vel, ch, prog in notes:
        ends = [e for e in ends if e > st]
        if len(ends) >= voices:
            dropped += 1
            continue
        ends.append(st + dur)
        kept.append((st, dur, note, vel, ch, prog))
    return kept, dropped


# ------------------------------------------------------------------ emit

def emit(name, notes, length_ms):
    lines = ['t %s %d' % (name, length_ms)]
    for st, dur, note, vel, ch, prog in notes:
        lines.append('n %d %d %d %d %d' % (st, dur, note, vel, wave_for(ch, prog)))
    return '\n'.join(lines)


def fetch(lump, cache):
    os.makedirs(cache, exist_ok=True)
    dst = os.path.join(cache, lump)
    if not os.path.exists(dst):
        with urllib.request.urlopen(RAW + lump, timeout=60) as r:
            data = r.read()
        with open(dst, 'wb') as f:
            f.write(data)
    return open(dst, 'rb').read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cache', default=os.path.join(HERE, '.freedoom-cache'))
    ap.add_argument('--report', action='store_true')
    args = ap.parse_args()

    out = ['# Generated by import-freedoom-music.py -- do not edit by hand.',
           '# Source: Freedoom (3-clause BSD), musics/*.mid',
           '#',
           '#   t <name> <length_ms>          a track, and how long before it loops',
           '#   n <start_ms> <dur_ms> <midi_note> <velocity> <wave>',
           '#',
           '# Times are milliseconds from the start of the track. Notes are sorted by',
           '# start time, so the sequencer only ever walks forward.']

    for name, lump in TRACKS:
        raw = fetch(lump, args.cache)
        notes, length = parse_midi(raw)
        kept, dropped = reduce_polyphony(notes, VOICES)
        out.append('')
        out.append(emit(name, kept, length))
        if args.report:
            print('%-6s %-12s %5d notes -> %5d kept (%d dropped), %.1fs'
                  % (name, lump, len(notes), len(kept), dropped, length / 1000.0))

    text = '\n'.join(out) + '\n'
    path = os.path.join(HERE, 'music.txt')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    if args.report:
        print('wrote %s, %d bytes' % (path, len(text)))


if __name__ == '__main__':
    sys.exit(main())
