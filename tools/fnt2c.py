#!/usr/bin/env python3
"""Convert a Dokkaebi 8x4x4 johab .FNT into the C table src/font_hangul.h.

The input is 360 headerless 16x16 glyphs, 32 bytes each, two bytes per row,
most significant bit leftmost -- the format 김중태's fonts ship in and the one
his readme describes ("헤더가 없으니 ... 16x16 비트 단위로 끊어서"). The output
array is byte-identical to the input file, so the table can be verified against
the original by hash rather than by eye.

Usage: python tools/fnt2c.py H04.FNT src/font_hangul.h
"""
import sys, hashlib, textwrap

CHO, JUNG, JONG = 8 * 20, 4 * 22, 4 * 28          # 160 + 88 + 112 = 360

def main(src, dst):
    d = open(src, 'rb').read()
    n = len(d) // 32
    if n != CHO + JUNG + JONG or len(d) % 32:
        sys.exit(f"{src}: expected {(CHO+JUNG+JONG)*32} bytes, got {len(d)}")
    sha = hashlib.sha256(d).hexdigest()

    out = [HEADER.format(src=src.replace(chr(92), '/'), sha=sha, n=n, bytes=len(d))]
    out.append("static const unsigned char HANGUL[%d * 32] = {\n" % n)
    for g in range(n):
        glyph = d[g*32:(g+1)*32]
        rows = ['0x%02X,0x%02X,' % (glyph[r*2], glyph[r*2+1]) for r in range(16)]
        out.append("    /* %3d */ %s\n              %s\n"
                   % (g, ''.join(rows[:8]), ''.join(rows[8:])))
    out.append("};\n\n#endif\n")
    open(dst, 'w', encoding='utf-8', newline='\n').write(''.join(out))
    print(f"{dst}: {n} glyphs, {len(d)} bytes of table data")

HEADER = '''/**
 * @file font_hangul.h
 * @brief GENERATED. The 둥근모꼴 johab glyph table. Do not edit by hand.
 *
 * Produced by `python tools/fnt2c.py {src} src/font_hangul.h` from the
 * original DOS bitmap font, sha256 {sha}.
 * The array is byte-identical to that file: {n} glyphs, {bytes} bytes.
 *
 * 둥근모꼴 is 김중태's, released with no restrictions of any kind -- see
 * docs/LICENSE-Dunggeunmo.txt for the grant in his own words. Nothing here
 * needs attribution; the notice is kept because the provenance of a table
 * nobody can read is worth writing down.
 *
 * 생성된 파일입니다. 손으로 고치지 마십시오. 원본 도스 비트맵 글꼴에서
 * tools/fnt2c.py가 만들어 내며, 배열은 그 파일과 바이트 단위로 동일합니다.
 */
#ifndef FONT_HANGUL_H
#define FONT_HANGUL_H

'''

if __name__ == '__main__':
    if len(sys.argv) != 3: sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
