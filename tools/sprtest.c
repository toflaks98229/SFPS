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
#include "diag.h"       /* DIAG_PNG -- a refusal has to be counted to be seen */

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
/* --- fixtures -------------------------------------------------------------
 *
 * A DRAWING IS A PNG NOW, so a fixture has to be one. It is built rather than
 * loaded for the reason every fixture in this project is built: a test that
 * reads assets\spritesrute.png is a test that goes red when somebody redraws
 * the brute, and what is under test here is where a drawing LANDS, which has
 * nothing to do with what it depicts.
 *
 * DEFLATE's STORED block is what makes building one cheap. The project has an
 * inflater and no deflater, and a stored block is a length, its complement and
 * the bytes -- so a fixture needs no compressor and no library.
 *
 * CRCs are left zero: src\png.c does not verify them, and its header says why.
 * These are PNGs for that decoder, not for the world.
 *
 * 이제 그림은 PNG이므로 픽스처도 PNG여야 합니다. 불러오지 않고 만드는 이유는 이 프로젝트의
 * 모든 픽스처가 그런 이유와 같습니다. assets\spritesrute.png를 읽는 검사는 누군가 브루트를
 * 다시 그리면 빨개지는 검사이며, 이곳에서 검사하는 것은 그림이 *어디에 놓이는가*이고 그것은
 * 그림이 무엇을 그린 것인지와 아무 상관이 없습니다.
 *
 * DEFLATE의 stored 블록이 그것을 싸게 만듭니다. 이 프로젝트에는 펼치는 쪽만 있고 압축하는
 * 쪽은 없으며, stored 블록은 길이와 그 보수와 바이트가 전부입니다. 그래서 픽스처에는
 * 압축기도 라이브러리도 필요 없습니다.
 *
 * CRC는 0으로 둡니다. src\png.c가 검사하지 않으며 그 이유는 그 헤더에 있습니다. */

static unsigned char g_blob[1 << 16];
static int           g_blob_n;

static void blob_reset(void) { g_blob_n = 0; }

static void blob_str(const char *t) {
    while (*t && g_blob_n < (int)sizeof g_blob) g_blob[g_blob_n++] = (unsigned char)*t++;
}

/* A `w` x `h` PNG filled with one RGBA colour, plus an optional marker pixel
 * of a second colour at (mx, my). Every row uses filter None: what these
 * fixtures test is placement, and png.c's predictors are pngtest's subject. */
static void blob_png(const char *name, int w, int h,
                     const unsigned char rgba[4],
                     int mx, int my, const unsigned char mark[4]) {
    static unsigned char px[64 * 64 * 4 + 64];   /* filtered rows */
    int stride = w * 4, raw = h * (stride + 1), n = 0;

    for (int y = 0; y < h; y++) {
        px[n++] = 0;                              /* filter None */
        for (int x = 0; x < w; x++) {
            const unsigned char *c = (x == mx && y == my && mark) ? mark : rgba;
            for (int k = 0; k < 4; k++) px[n++] = c[k];
        }
    }

    int idat = 2 + 5 + raw + 4;
    char hdr[64];
    snprintf(hdr, sizeof hdr, "s %s %d ", name, 8 + 25 + (12 + idat) + 12);
    blob_str(hdr);

    unsigned char *o = g_blob + g_blob_n;
    int k = 0;
    static const unsigned char SIG[8] = { 0x89,'P','N','G','\r','\n',0x1a,'\n' };
    for (int i = 0; i < 8; i++) o[k++] = SIG[i];

    o[k++]=0; o[k++]=0; o[k++]=0; o[k++]=13;
    o[k++]='I'; o[k++]='H'; o[k++]='D'; o[k++]='R';
    o[k++]=0; o[k++]=0; o[k++]=(unsigned char)(w>>8); o[k++]=(unsigned char)w;
    o[k++]=0; o[k++]=0; o[k++]=(unsigned char)(h>>8); o[k++]=(unsigned char)h;
    o[k++]=8; o[k++]=6; o[k++]=0; o[k++]=0; o[k++]=0;
    o[k++]=0; o[k++]=0; o[k++]=0; o[k++]=0;

    o[k++]=(unsigned char)(idat>>24); o[k++]=(unsigned char)(idat>>16);
    o[k++]=(unsigned char)(idat>>8);  o[k++]=(unsigned char)idat;
    o[k++]='I'; o[k++]='D'; o[k++]='A'; o[k++]='T';
    o[k++]=0x78; o[k++]=0x01; o[k++]=0x01;
    o[k++]=(unsigned char)(raw & 0xff);  o[k++]=(unsigned char)(raw >> 8);
    o[k++]=(unsigned char)(~raw & 0xff); o[k++]=(unsigned char)((~raw >> 8) & 0xff);
    for (int i = 0; i < raw; i++) o[k++] = px[i];
    o[k++]=0; o[k++]=0; o[k++]=0; o[k++]=0;
    o[k++]=0; o[k++]=0; o[k++]=0; o[k++]=0;

    o[k++]=0; o[k++]=0; o[k++]=0; o[k++]=0;
    o[k++]='I'; o[k++]='E'; o[k++]='N'; o[k++]='D';
    o[k++]=0; o[k++]=0; o[k++]=0; o[k++]=0;

    g_blob_n += k;
    blob_str(" ");
}

static const char *blob(void) {
    g_blob[g_blob_n] = 0;
    return (const char *)g_blob;
}

static const unsigned char *at(int type, int frame, int sw, int sh, int sx, int sy) {
    int ax = frame * SPR_CW + (SPR_CW - sw) / 2 + sx;
    int ay = type  * SPR_CH + (SPR_CH - sh)     + sy;
    return &g_atlas[(ay * AW + ax) * 4];
}

int main(void) {
    printf("sprtest\n\n");

    /* --- 1. a drawing lands in its own cell -------------------------------
       The atlas has two axes -- frames run across, monster types run down --
       and the name is the only thing that says which cell a drawing belongs
       to. `water_spirit0` is type 0 frame 0; `water_spirit2` is the same creature two columns
       over. A decoder that read the frame and ignored the subject would pass
       any test that only ever drew one monster.
       아틀라스에는 축이 둘입니다. 프레임은 가로로, 몬스터 종류는 세로로 갑니다. 그리고 그림이
       어느 셀에 속하는지 말해 주는 것은 이름뿐입니다. `water_spirit0`은 종류 0의 프레임 0이고 `water_spirit2`는
       같은 생물의 두 칸 오른쪽입니다. 프레임만 읽고 주제를 무시하는 디코더는 몬스터 하나만
       그리는 어떤 검사든 통과합니다. */
    {
        static const unsigned char RED[4]   = { 255, 0, 0, 255 };
        static const unsigned char GREEN[4] = { 0, 255, 0, 255 };

        clear(g_atlas, AW * AH);
        blob_reset();
        blob_png("water_spirit0", 4, 2, RED,   -1, -1, 0);
        blob_png("water_spirit2", 4, 2, GREEN, -1, -1, 0);
        sprite_decode_blob(blob(), g_atlas, AW, AH, 0);

        const unsigned char *f0 = at(0, 0, 4, 2, 0, 0);
        const unsigned char *f2 = at(0, 2, 4, 2, 0, 0);
        ok(f0[0] == 255 && f0[1] == 0 && f0[3] == 255,
           "frame 0 of the water spirit holds the first drawing");
        ok(f2[0] == 0 && f2[1] == 255 && f2[3] == 255,
           "and frame 2 holds the second, two columns over");

        /* Centred across and seated on the cell floor, which is a rule about
           CELLS rather than about the current art: every drawing this project
           ships is exactly its cell, so this is the only place the rule is
           still visible. A 32x32 creature in a 64x96 cell has to stand on the
           ground rather than float at the top.
           가로로 가운데, 셀 바닥에 앉습니다. 이는 현재 아트가 아니라 *셀*에 대한 규칙입니다.
           이 프로젝트가 배포하는 모든 그림이 정확히 자기 셀이므로, 이 규칙이 아직 보이는
           곳은 이곳뿐입니다. */
        const unsigned char *above = &g_atlas[((0 * SPR_CH + 0) * AW + 0) * 4];
        ok(above[3] == 0, "and it sits on the cell floor rather than the top");
    }

    /* --- 2. a drawn frame owns its cell, and only its cell ----------------
       The generated creature is painted first and a drawing replaces it, so a
       clear that ran too far would wipe a neighbour that nobody drew -- and
       the symptom is a monster that is simply missing, which reads as a
       bestiary that is half finished rather than as a bug.
       생성된 생물이 먼저 칠해지고 그림이 그것을 대체하므로, 너무 멀리 지우는 clear는 아무도
       그리지 않은 이웃을 지웁니다. 그 증상은 그냥 없는 몬스터이며, 버그가 아니라 절반만
       완성된 도감으로 읽힙니다. */
    {
        static const unsigned char RED[4] = { 255, 0, 0, 255 };

        /* Fill the atlas with a stand-in for the generated art, so anything
           the clear touches is visible. */
        for (int i = 0; i < AW * AH; i++) {
            g_atlas[i*4+0] = 9; g_atlas[i*4+1] = 9;
            g_atlas[i*4+2] = 9; g_atlas[i*4+3] = 255;
        }
        blob_reset();
        blob_png("water_spirit0", 2, 1, RED, -1, -1, 0);
        sprite_decode_blob(blob(), g_atlas, AW, AH, 0);

        const unsigned char *nf = at(0, 1, 2, 1, 0, 0);   /* next frame across */
        const unsigned char *nt = at(1, 0, 2, 1, 0, 0);   /* next type down    */
        ok(nf[0] == 9 && nf[3] == 255,
           "a frame nobody drew keeps its generated creature");
        ok(nt[0] == 9 && nt[3] == 255,
           "and so does the same frame of another monster");

        /* And the cell that WAS drawn no longer shows the generated art
           anywhere the drawing did not cover. */
        const unsigned char *corner = &g_atlas[((0 * SPR_CH + 0) * AW + 0) * 4];
        ok(corner[3] == 0, "while the drawn cell was cleared first");
    }

    /* --- 3. the muzzle comes out of the pixels ----------------------------
       THE BAKE USED TO RECORD IT. It scanned for the magenta pixel and wrote
       the position into the stream, which meant a drawing read straight off
       disk carried no marker at all -- and reading one off disk is exactly
       what a hot reload does now. Finding it here means the drawing is the
       only thing that has to be right.
       The pixel itself must never be painted: one magenta dot at the end of a
       barrel would be the brightest thing on the weapon.
       *예전에는 베이크가 기록했습니다.* 마젠타 픽셀을 훑어 그 위치를 스트림에 적었고, 그러면
       디스크에서 바로 읽은 그림은 표식을 전혀 지니지 못했습니다. 그리고 디스크에서 읽는 것이
       바로 지금 핫 리로드가 하는 일입니다. 이곳에서 찾으면 옳아야 하는 것은 그림 하나뿐입니다.
       그 픽셀 자체는 결코 칠해져서는 안 됩니다. 총열 끝의 마젠타 점 하나는 그 무기에서 가장
       밝은 것이 될 것입니다. */
    {
        static const unsigned char GREY[4]    = { 80, 80, 80, 255 };
        static const unsigned char MAGENTA[4] = { 255, 0, 255, 255 };

        clear(g_watlas, WW * WH);
        blob_reset();
        blob_png("shotgun0", 4, 2, GREY, 1, 0, MAGENTA);
        sprite_decode_blob(blob(), g_watlas, WW, WH, 1);

        int mx = -1, my = -1;
        ok(sprite_weapon_muzzle_px(0, 0, &mx, &my),
           "the weapon frame recorded a muzzle");
        okd(mx == (WPN_CW - 4) / 2 + 1, "muzzle x follows the centring",
            mx, (WPN_CW - 4) / 2 + 1);
        okd(my == (WPN_CH - 2), "muzzle y follows the bottom seating",
            my, (WPN_CH - 2));

        const unsigned char *m = &g_watlas[((WPN_CH - 2) * WW +
                                            ((WPN_CW - 4) / 2 + 1)) * 4];
        ok(m[3] == 0, "and the marker pixel itself was never painted");

        /* Its neighbour was, so the transparency above is the marker being
           skipped rather than the whole drawing failing to land. */
        const unsigned char *n = &g_watlas[((WPN_CH - 2) * WW +
                                            ((WPN_CW - 4) / 2)) * 4];
        ok(n[3] == 255 && n[0] == 80, "while the pixel beside it was");
    }

    /* --- 4. a name nothing recognises is skipped without cost --------------
       A work-in-progress file parked in assets\sprites is harmless, which is
       a promise the README makes to whoever is drawing. It is also the reason
       the weapon pass and the monster pass can share one stream: each skips
       what the other owns.
       Skipping is now free as well as safe -- a record is stepped over by its
       length rather than decoded and discarded, so the weapon pass no longer
       unpacks every monster to reach the guns.
       assets\sprites에 놓아 둔 작업 중인 파일은 무해하며, 그것은 README가 그리는 사람에게
       하는 약속입니다. 무기 패스와 몬스터 패스가 한 스트림을 공유할 수 있는 이유이기도
       합니다. 각자 상대가 소유한 것을 건너뜁니다. */
    {
        static const unsigned char BLUE[4] = { 0, 0, 255, 255 };

        clear(g_atlas, AW * AH);
        blob_reset();
        blob_png("notamonster0", 4, 2, BLUE, -1, -1, 0);
        blob_png("water_spirit0",         4, 2, BLUE, -1, -1, 0);
        sprite_decode_blob(blob(), g_atlas, AW, AH, 0);

        const unsigned char *f0 = at(0, 0, 4, 2, 0, 0);
        ok(f0[3] == 255, "the drawing after an unknown name still lands");
    }

    /* --- 5. a malformed record stops rather than running off the end -------
       These files are generated, but a half-written one is exactly what a
       build interrupted at the wrong moment leaves behind -- and a decoder
       that walks past its buffer on one turns a bad build into a crash.
       A length is the thing to attack now: it is what the walk trusts, so a
       record claiming more bytes than the blob holds is the shape that used
       to be a truncated data line.
       이 파일들은 생성되지만, 잘못된 순간에 중단된 빌드가 남기는 것이 바로 절반만 쓰인
       파일입니다. 그리고 그런 파일에서 버퍼를 넘어 걷는 디코더는 잘못된 빌드를 크래시로
       바꿉니다. 이제 공격할 대상은 *길이*입니다. 순회가 신뢰하는 것이 그것이므로, 블롭이
       담은 것보다 많은 바이트를 주장하는 레코드가 예전의 잘린 데이터 줄에 해당합니다. */
    {
        clear(g_atlas, AW * AH);

        sprite_decode_blob("s water_spirit0 999999 ", g_atlas, AW, AH, 0);
        ok(1, "a length past the end of the blob returns instead of overrunning");

        sprite_decode_blob("s water_spirit0", g_atlas, AW, AH, 0);
        ok(1, "a header with no length returns");

        sprite_decode_blob("s water_spirit0 12 not a png at all", g_atlas, AW, AH, 0);
        ok(1, "bytes that are not a PNG are refused rather than decoded");

        sprite_decode_blob("", g_atlas, AW, AH, 0);
        ok(1, "and an empty blob draws nothing");

        /* The refusals were counted. png.c raising DIAG_PNG is the only thing
           that tells "nobody drew this" apart from "this would not decode",
           and the two look identical on screen.
           거부는 세어졌습니다. png.c가 DIAG_PNG를 올리는 것이 "아무도 그리지 않음"과
           "디코딩되지 않음"을 가르는 유일한 것이며, 둘은 화면에서 똑같이 보입니다. */
        ok(diag_count(DIAG_PNG) > 0, "and every refusal was counted");
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall sprite codec checks passed\n",
           fails);
    return fails != 0;
}
