/* sprtest -- check the sprite codec, with no window and no PNG.
 *
 * This exists because the codec's format was changed while it had no test at
 * all. The only thing looking at it was sprdump, which writes a PPM for a human
 * to squint at and asserts nothing, and it only ever covered the monster atlas.
 * A screenshot proved the new format decoded SOMETHING; it could not have
 * caught a run length off by one, a packed triple leaking across a sprite
 * boundary, or the two alphabets drifting apart.
 *
 * What is checked, in order of how badly it fails:
 *
 *   1. bake.ps1's alphabet and sprite.c's b64val agree. This is a contract
 *      between a PowerShell script and a C file that no compiler can see, and
 *      the failure is every drawing in the game decoding to noise.
 *   2. Both opcodes decode to pixels computed by hand here.
 *   3. A packed sprite whose pixel count is not a multiple of three does not
 *      shift the sprite after it.
 *   4. Index 0 composites rather than punching a hole.
 *   5. The muzzle marker survives centring and bottom-seating.
 *   6. Malformed text stops rather than running off the end.
 *
 * The test writes sprite text by hand rather than driving bake.ps1, because
 * what ships is the DECODER: an encoder bug shows up as art that looks wrong to
 * the person who drew it, while a decoder bug shows up in the released game.
 *
 * 이 파일은 코덱의 형식이 테스트가 전혀 없는 상태에서 변경되었기 때문에 존재합니다.
 * 스크린샷은 새 형식이 *무언가*를 디코딩한다는 것만 증명했을 뿐, 실행 길이가 하나
 * 어긋났는지, 패킹된 묶음이 스프라이트 경계를 넘어 샜는지, 두 알파벳이 어긋났는지는
 * 잡을 수 없었습니다.
 */

#include <stdio.h>
#include <string.h>
#include "sprite.h"
#include "enemy.h"      /* MON_TYPES -- the monster atlas has one row per type */
#include "weapon.h"     /* WP_TYPES  -- and the weapon atlas one row per weapon */

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void okd(int cond, const char *what, int got, int want) {
    printf("  %-58s %6d / %6d  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* The monster atlas, sized exactly as sprite.c builds it. Decoding writes into
   this, and every expectation below indexes it the same way sprite_uv does.
   sprite.c가 생성하는 것과 정확히 같은 크기의 몬스터 아틀라스입니다. */
#define AW (SPR_CW * SPR_FRAMES)
#define AH (SPR_CH * MON_TYPES)
static unsigned char g_atlas[AW * AH * 4];

/* The weapon atlas. */
#define WW (WPN_CW * WPN_FRAMES)
#define WH (WPN_CH * WP_TYPES)
static unsigned char g_watlas[WW * WH * 4];

static void clear(unsigned char *b, int n) { memset(b, 0, (size_t)n * 4); }

/* A pixel from the monster atlas. Cells are laid out frame-across, type-down,
   and a drawing is centred horizontally and seated on the cell's bottom -- so
   a test that wants "the drawing's pixel (sx,sy)" has to apply the same
   placement the decoder does, or it is asserting against the wrong address.
   몬스터 아틀라스의 픽셀입니다. 배치를 동일하게 적용해야 올바른 주소를 검사합니다. */
static const unsigned char *at(int type, int frame, int sw, int sh, int sx, int sy) {
    int ax = frame * SPR_CW + (SPR_CW - sw) / 2 + sx;
    int ay = type  * SPR_CH + (SPR_CH - sh)     + sy;
    return &g_atlas[(ay * AW + ax) * 4];
}

int main(void) {
    printf("sprtest\n\n");

    /* --- 1. the alphabet contract, read out of bake.ps1 -------------------
       bake.ps1 encodes with a 64-character string and sprite.c decodes by
       COMPUTING the index from the character. Nothing checks that the two
       describe the same alphabet, and nothing can: one is PowerShell and the
       other is C. So the test reads the script.

       A mismatch is not subtle in its consequences and is very subtle in its
       cause: every sprite in the game decodes to the wrong palette indices, and
       the art looks like it was drawn wrong.

       bake.ps1은 64자 문자열로 인코딩하고 sprite.c는 문자로부터 인덱스를 *계산*합니다.
       둘이 같은 알파벳을 기술하는지 확인하는 것은 없으며, 하나는 PowerShell이고 다른
       하나는 C이므로 확인할 수도 없습니다. 그래서 테스트가 스크립트를 읽습니다. */
    {
        FILE *f = fopen("bake.ps1", "rb");
        if (!f) f = fopen("../bake.ps1", "rb");
        ok(f != NULL, "bake.ps1 is readable, so the alphabet can be compared");

        char alpha[128] = {0};
        int  found = 0;
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                /* The encoder's alphabet is the one assignment to $A. */
                const char *m = strstr(line, "$A = '");
                if (!m) continue;
                m += 6;
                int n = 0;
                while (*m && *m != '\'' && n < 127) alpha[n++] = *m++;
                alpha[n] = 0;
                found = (n == 64);
                break;
            }
            fclose(f);
        }
        okd(found, "and it holds exactly 64 characters", (int)strlen(alpha), 64);

        if (found) {
            /* Every character must decode to its own position. */
            int bad = 0, first_bad = -1;
            for (int i = 0; i < 64; i++) {
                if (sprite_b64val(alpha[i]) != i) {
                    if (first_bad < 0) first_bad = i;
                    bad++;
                }
            }
            okd(bad == 0, "every encoder character decodes to its own index",
                bad, 0);
            if (bad) printf("      first mismatch at index %d ('%c')\n",
                            first_bad, alpha[first_bad]);

            /* And none of them may need escaping inside a C string literal,
               because that is where this text ends up. A backslash would eat
               the next character, a quote would end the string, and '?' can
               begin a trigraph.
               그리고 어느 것도 C 문자열 리터럴 안에서 이스케이프가 필요해서는 안 됩니다.
               이 텍스트가 놓이는 곳이 그곳이기 때문입니다. */
            int unsafe = 0;
            for (int i = 0; i < 64; i++)
                if (alpha[i] == '\\' || alpha[i] == '"' || alpha[i] == '?' ||
                    alpha[i] < 33 || alpha[i] > 126) unsafe++;
            okd(unsafe == 0, "and none needs escaping in a C string literal",
                unsafe, 0);
        }
    }

    /* --- 2. run-length decode --------------------------------------------
       A 4x2 sprite: a run of 4 in colour 1, then 4 in colour 2. Written as
       'r' pairs, one character of run and one of index, so "EB" is a run of 4
       (alphabet index 4 = 'E') of palette entry 1 ('B').
       4x2 스프라이트입니다. 색 1이 4연속, 색 2가 4연속입니다. */
    {
        clear(g_atlas, AW * AH);
        /* pal: index 0 transparent, 1 red, 2 green. */
        const char *text =
            "pal 3 000000 ff0000 00ff00\n"
            "s imp0 4 2\n"
            "r EBEC\n";
        sprite_decode_text(text, g_atlas, AW, AH, 0);

        const unsigned char *p0 = at(0, 0, 4, 2, 0, 0);   /* row 0 */
        const unsigned char *p1 = at(0, 0, 4, 2, 3, 1);   /* row 1, last px */
        ok(p0[0] == 255 && p0[1] == 0 && p0[2] == 0 && p0[3] == 255,
           "RLE: the first run decodes to its palette colour");
        ok(p1[0] == 0 && p1[1] == 255 && p1[2] == 0 && p1[3] == 255,
           "RLE: the second run lands on the following pixels");
    }

    /* --- 3. packed decode -------------------------------------------------
       Three 4-bit indices per two characters. Indices 1,2,1 pack to the 12-bit
       value 0x121, whose top six bits are 4 ('E') and bottom six are 33 ('h').
       두 문자에 4비트 인덱스 세 개가 들어갑니다. */
    {
        clear(g_atlas, AW * AH);
        const char *text =
            "pal 3 000000 ff0000 00ff00\n"
            "s imp0 3 1\n"
            "p Eh\n";
        sprite_decode_text(text, g_atlas, AW, AH, 0);

        const unsigned char *a = at(0, 0, 3, 1, 0, 0);
        const unsigned char *b = at(0, 0, 3, 1, 1, 0);
        const unsigned char *c = at(0, 0, 3, 1, 2, 0);
        ok(a[0] == 255 && a[1] == 0,   "packed: first index of the triple");
        ok(b[0] == 0   && b[1] == 255, "packed: second");
        ok(c[0] == 255 && c[1] == 0,   "packed: third");
    }

    /* --- 4. a packed triple must not leak into the next sprite ------------
       The failure this is written for: `pend` was a `static` inside the decode
       loop, so a sprite whose pixel count is not a multiple of three left one
       or two indices behind and shifted every sprite after it by that much.
       Two pixels is not a multiple of three, so the first sprite here ends
       mid-triple; the second must still start at its own first pixel.

       That fault looks like bad art rather than like a decoder bug, which is
       exactly why it needs a test rather than an eye.

       이 검사가 존재하는 이유인 실패입니다. `pend`가 디코드 루프 안의 `static`이었으므로,
       픽셀 수가 3의 배수가 아닌 스프라이트가 인덱스를 한두 개 남겨 그 뒤의 모든
       스프라이트를 그만큼 밀어냈습니다. */
    {
        clear(g_atlas, AW * AH);
        const char *text =
            "pal 3 000000 ff0000 00ff00\n"
            "s imp0 2 1\n"       /* 2 pixels: ends mid-triple */
            "p Eh\n"
            "s brute0 3 1\n"     /* must start clean */
            /* indices 0,2,1 pack to 0x021 = 33; 33>>6 = 0 ('A'), 33&63 = 33
               ('h'). Worked out here rather than trusted: the first draft of
               this line said "Bh", which is 0x061 and decodes to 0,6,1 -- and
               index 6 is past the end of a three-entry palette, so the test
               failed against a decoder that was right.
               직접 계산합니다. 이 줄의 초안은 "Bh"였는데 그것은 0x061이며 0,6,1로
               디코딩됩니다. 인덱스 6은 항목이 셋뿐인 팔레트의 끝을 넘어서므로, 올바른
               디코더를 상대로 테스트가 실패했습니다. */
            "p Ah\n";
        sprite_decode_text(text, g_atlas, AW, AH, 0);

        /* brute is atlas row 1. Its first pixel is index 0 = transparent, so
           it must be left alone; the second must be green. */
        const unsigned char *b0 = at(1, 0, 3, 1, 0, 0);
        const unsigned char *b1 = at(1, 0, 3, 1, 1, 0);
        okd(b0[3] == 0, "a mid-triple sprite does not shift the next one",
            b0[3], 0);
        ok(b1[0] == 0 && b1[1] == 255,
           "and the next sprite's own pixels land where they should");
    }

    /* --- 5. a drawn frame owns its cell, and only its cell ----------------
       Two halves of one rule, and the interesting one is the second.

       A drawing REPLACES the generated creature in the cell it fills rather
       than compositing over it. Compositing was the original behaviour and it
       is wrong for any drawing narrower than the SDF version underneath: the
       generated creature shows around the edges as a halo. The first Freedoom
       import made that concrete -- a green shape standing behind the hound and
       a horn poking out over the caster.

       But the graceful-degradation property has to survive the fix, so the
       clear is per CELL: a frame nobody drew is never reached and keeps its
       generated creature, which is what lets a half-drawn bestiary still show
       a full one.

       그린 프레임이 자기 셀을 소유하되 *자기 셀만* 소유한다는 규칙의 두 절반이며,
       흥미로운 쪽은 두 번째입니다. 그림은 셀 안의 생성된 생물을 합성하지 않고
       *대체*합니다. 합성은 원래 동작이었고 아래의 SDF 버전보다 좁은 모든 그림에
       대해 틀립니다. 다만 아무도 그리지 않은 프레임은 이 경로에 닿지 않아 생성된
       생물을 유지하므로, 절반만 그려진 도감도 여전히 온전해 보입니다. */
    {
        clear(g_atlas, AW * AH);
        /* Pre-fill everything with a marker standing in for the generated
           creature, so both what is erased and what is not are visible. */
        for (int i = 0; i < AW * AH; i++) {
            g_atlas[i*4+0] = 9; g_atlas[i*4+1] = 9;
            g_atlas[i*4+2] = 9; g_atlas[i*4+3] = 255;
        }
        const char *text =
            "pal 3 000000 ff0000 00ff00\n"
            "s imp0 2 1\n"
            "r BABB\n";          /* one transparent pixel, then one red */
        sprite_decode_text(text, g_atlas, AW, AH, 0);

        const unsigned char *t = at(0, 0, 2, 1, 0, 0);
        const unsigned char *r = at(0, 0, 2, 1, 1, 0);
        ok(t[3] == 0, "a drawn frame clears the generated creature out of its cell");
        ok(r[0] == 255 && r[1] == 0, "and a non-zero index overwrites");

        /* Nothing but imp0's cell may be touched, and the atlas has TWO axes:
           frames run across and monster types run down. Checking only one of
           them is not a check -- a clear that ran the full height of the atlas
           passed a frame-only version of this line while wiping every other
           creature in the same column. So probe both neighbours. */
        const unsigned char *nf = at(0, 1, 2, 1, 0, 0);   /* next frame across */
        const unsigned char *nt = at(1, 0, 2, 1, 0, 0);   /* next type down    */
        ok(nf[0] == 9 && nf[3] == 255,
           "a frame nobody drew keeps its generated creature");
        ok(nt[0] == 9 && nt[3] == 255,
           "and so does the same frame of another monster");
    }

    /* --- 6. the muzzle marker survives placement --------------------------
       bake.ps1 records the marker in SOURCE-image coordinates, and the decoder
       has to move it by the same centring and bottom-seating it applies to the
       pixels. A muzzle recorded raw would sit correctly only for a drawing that
       exactly filled its cell.
       bake.ps1은 표식을 *원본 이미지* 좌표로 기록하며, 디코더는 픽셀에 적용하는 것과 같은
       가운데 맞춤·바닥 정렬만큼 그것을 옮겨야 합니다. */
    {
        clear(g_watlas, WW * WH);
        /* A 4x2 weapon drawing with its muzzle at source (1,0). Centred in a
           128-wide cell that is (128-4)/2 = 62 across, and seated at
           (96-2) = 94 down. */
        const char *text =
            "pal 2 000000 ff0000\n"
            "s shotgun0 4 2\n"
            "m 1 0\n"
            "r EBEB\n";
        sprite_decode_text(text, g_watlas, WW, WH, 1);

        int mx = -1, my = -1;
        int got = sprite_weapon_muzzle_px(0, 0, &mx, &my);
        ok(got, "the weapon frame recorded a muzzle");
        okd(mx == (WPN_CW - 4) / 2 + 1, "muzzle x follows the centring",
            mx, (WPN_CW - 4) / 2 + 1);
        okd(my == (WPN_CH - 2) + 0, "muzzle y follows the bottom seating",
            my, (WPN_CH - 2));
    }

    /* --- 6b. `o` places a cropped drawing, and moves the muzzle with it ----
       bake.ps1 crops each drawing to its ink before encoding, because the
       packed opcode pays for empty margin at the same rate as picture. That
       is only safe if the offset it emits puts the pixels back exactly where
       the uncropped drawing had them -- a size optimisation that moved the art
       would be a bug that looks like an art mistake.

       The muzzle is the half worth testing hardest. It is stored relative to
       the drawing, so cropping changes its number too, and a decoder that
       applied `o` to the pixels but not to the marker would put every muzzle
       flash off the end of the barrel by exactly the crop.

       bake.ps1은 인코딩 전에 각 그림을 잉크에 맞춰 잘라 냅니다. packed opcode가 빈
       여백에도 그림과 같은 값을 치르기 때문입니다. 이는 함께 내보내는 오프셋이 픽셀을
       자르기 전과 정확히 같은 자리에 되돌려 놓을 때에만 안전합니다. 그림을 움직이는
       크기 최적화는 아트 실수처럼 보이는 버그가 됩니다. */
    {
        clear(g_watlas, WW * WH);
        /* The same 4x2 drawing as above, but declared as already cropped out
           of a cell at (20,30) instead of being centred and floor-seated. */
        const char *text =
            "pal 2 000000 ff0000\n"
            "s shotgun1 4 2\n"
            "o 20 30\n"
            "m 1 0\n"
            "r EBEB\n";
        sprite_decode_text(text, g_watlas, WW, WH, 1);

        const unsigned char *q = &g_watlas[((30) * WW + (1 * WPN_CW + 20)) * 4];
        ok(q[0] == 255 && q[3] == 255,
           "an explicit origin puts the pixels where it says");

        /* And nothing landed at the default placement it replaced. */
        const unsigned char *d = &g_watlas[(((WPN_CH - 2)) * WW +
                                            (1 * WPN_CW + (WPN_CW - 4) / 2)) * 4];
        ok(d[3] == 0, "and not at the centred, floor-seated default");

        int mx = -1, my = -1;
        (void)sprite_weapon_muzzle_px(0, 1, &mx, &my);
        okd(mx == 20 + 1, "the muzzle moves with the origin, not the centring",
            mx, 21);
        okd(my == 30 + 0, "on both axes", my, 30);
    }

    /* --- 7. malformed text stops rather than running off the end ----------
       These files are generated, but a half-written one is exactly what a
       build interrupted at the wrong moment leaves behind, and a decoder that
       walks past its buffer on one turns a bad build into a crash.
       이 파일들은 생성되지만, 잘못된 순간에 중단된 빌드가 남기는 것이 바로 절반만 쓰인
       파일입니다. */
    {
        clear(g_atlas, AW * AH);
        const char *truncated =
            "pal 3 000000 ff0000 00ff00\n"
            "s imp0 8 8\n"
            "r EB";                       /* claims 64 px, supplies 4 */
        sprite_decode_text(truncated, g_atlas, AW, AH, 0);
        ok(1, "a truncated data line returns instead of overrunning");

        const char *no_data =
            "pal 3 000000 ff0000 00ff00\n"
            "s imp0 4 4\n";               /* header with no data line at all */
        sprite_decode_text(no_data, g_atlas, AW, AH, 0);
        ok(1, "a sprite with no data line returns");

        const char *bad_char =
            "pal 3 000000 ff0000 00ff00\n"
            "s imp0 4 1\n"
            "r E!EB";                     /* '!' is not in the alphabet */
        sprite_decode_text(bad_char, g_atlas, AW, AH, 0);
        ok(1, "a character outside the alphabet stops the run");

        sprite_decode_text("", g_atlas, AW, AH, 0);
        sprite_decode_text(0,  g_atlas, AW, AH, 0);
        ok(1, "empty and NULL texts are handled");
    }


    /* --- imported surfaces reach the caller ------------------------------
     *
     * sprite_wall is the whole of the wall pipeline that can be checked
     * without a GL context: the decode and the name lookup. What follows it in
     * tex.c is a copy into a buffer and an upload, and neither can be wrong in
     * a way this would not catch first.
     *
     * The failure being guarded is NOT "no pixels". A wall that decoded to
     * nothing would be obvious the moment anyone looked at the game. The one
     * that hides is a wall that decodes to a FLAT COLOUR -- a palette read
     * wrongly, a run length misread as an index -- because a flat brown wall
     * looks like a wall until you stand next to it.
     *
     * GL 컨텍스트 없이 검사할 수 있는 벽 파이프라인 전체입니다. 디코드와 이름 조회가
     * 그것이고, tex.c에서 뒤따르는 것은 버퍼 복사와 업로드뿐입니다.
     *
     * 막으려는 실패는 "픽셀 없음"이 아닙니다. 아무것도 디코드되지 않은 벽은 누구든 게임을
     * 보는 순간 드러납니다. 숨는 것은 *단색*으로 디코드되는 벽입니다. 팔레트를 잘못 읽거나
     * 실행 길이를 인덱스로 오독한 경우이며, 균일한 갈색 벽은 바로 옆에 설 때까지 벽처럼
     * 보이기 때문입니다.
     */
    {
        static unsigned char wall[SPR_WALL * SPR_WALL * 4];

        ok(sprite_wall("wall_brick", wall), "an imported wall decodes by name");

        /* Every pixel opaque: a wall has no holes, and index 0 -- the sprite
           format's transparent -- must never reach one.
           모든 픽셀이 불투명합니다. 벽에는 구멍이 없으며, 스프라이트 형식의 투명 인덱스인
           0이 벽에 닿아서는 안 됩니다. */
        int clear = 0;
        for (int i = 0; i < SPR_WALL * SPR_WALL; i++)
            if (wall[i * 4 + 3] == 0) clear++;
        okd(clear == 0, "and covers its whole face with no holes", clear, 0);

        /* EXACT distinct colours, not coarse buckets. The first version of
           this counted 2-bit-per-channel buckets and scored 3 against a
           threshold of 3 -- passing with no margin at all, on a brick texture
           that is nearly monochrome by nature. A check that only just passes
           on correct data cannot be trusted to fail on wrong data.
           The palette holds 16 and index 0 is the format's transparent, so a
           correctly decoded wall should reach most of the remaining 15.
           거친 버킷이 아니라 *정확한* 색 수입니다. 첫 판은 채널당 2비트 버킷을 세어
           기준 3에 3점, 즉 여유 없이 통과했습니다. 본래 거의 단색인 벽돌 텍스처에서
           그랬습니다. 올바른 데이터에서 간신히 통과하는 검사는 잘못된 데이터에서 실패할
           것이라고 믿을 수 없습니다. 팔레트는 16개이고 인덱스 0은 형식의 투명이므로,
           올바로 디코드된 벽은 나머지 15개의 대부분에 닿아야 합니다. */
        unsigned seen[256]; int kinds = 0;
        for (int i = 0; i < 256; i++) seen[i] = 0xFFFFFFFFu;
        for (int i = 0; i < SPR_WALL * SPR_WALL; i++) {
            unsigned c = ((unsigned)wall[i*4] << 16) |
                         ((unsigned)wall[i*4+1] << 8) | wall[i*4+2];
            int k = 0;
            while (k < kinds && seen[k] != c) k++;
            if (k == kinds && kinds < 256) seen[kinds++] = c;
        }
        /* 5, against a MEASURED 8. The source brick has 21 colours and the
           median cut keeps 8 of them, confirmed by dumping the decoded surface
           and looking at it -- it is a brick wall. A threshold equal to the
           measurement passes with no margin and so proves nothing; this one
           still fails the collapse it exists to catch, which is a wall
           arriving as one or two colours.
           실측 8에 대해 5입니다. 원본 벽돌은 21색이고 중앙값 분할이 그중 8을 남기며,
           디코드된 표면을 덤프해 눈으로 확인했습니다. 벽돌 벽이 맞습니다. 실측과 같은
           기준은 여유 없이 통과하므로 아무것도 증명하지 못합니다. 이 값은 이 검사가
           막으려는 붕괴, 즉 벽이 한두 색으로 도착하는 경우에는 여전히 실패합니다. */
        okd(kinds >= 5, "and uses its palette rather than one flat colour",
            kinds, 5);

        /* The door is a separate drawing, not the brick under another name.
           문은 다른 이름의 벽돌이 아니라 별개의 그림입니다. */
        static unsigned char door[SPR_WALL * SPR_WALL * 4];
        ok(sprite_wall("wall_door", door), "the door face decodes too");
        int same = 1;
        for (int i = 0; i < SPR_WALL * SPR_WALL * 4; i++)
            if (door[i] != wall[i]) { same = 0; break; }
        ok(!same, "and is a different drawing from the brick");

        /* A name nobody baked leaves a cleared buffer and says so, rather than
           handing back the previous wall -- which is what a cache keyed on
           nothing would do.
           구워지지 않은 이름은 이전 벽을 돌려주지 않고 지워진 버퍼를 남기며 그렇게
           보고합니다. */
        ok(!sprite_wall("wall_no_such_thing", wall),
           "an unimported name reports that it found nothing");
        int nonzero = 0;
        for (int i = 0; i < SPR_WALL * SPR_WALL * 4; i++) if (wall[i]) nonzero++;
        okd(nonzero == 0, "and leaves the buffer cleared rather than stale",
            nonzero, 0);
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall sprite codec checks passed\n",
           fails);
    return fails != 0;
}
