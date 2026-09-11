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
#include "txt.h"
#include "plat.h"
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
    /* Big enough for a fixture that OVERFLOWS a cell, which is what the fitting
       check needs: the largest cell here is the weapon's 192x104, so a drawing
       past it has to fit in this buffer before ::fit_to_cell can be asked what
       it does with one.
       셀을 *넘치는* 픽스처를 담을 만큼 큽니다. 맞춤 검사가 필요로 하는 것이 그것입니다.
       이곳에서 가장 큰 셀은 무기의 192x104이므로, 그것을 넘는 그림이 먼저 이 버퍼에 들어가야
       ::fit_to_cell이 그런 그림을 어떻게 다루는지 물을 수 있습니다. */
    static unsigned char px[208 * 208 * 4 + 208];   /* filtered rows */
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

    /* --- 4. how many frames a name asks for -------------------------------
     *
     * THREE ANSWERS AND THE NAME IS THE ONLY THING THAT SAYS WHICH. A trailing
     * digit is one frame; a `_idle` suffix is the two walk frames AND every
     * frame no other drawing named; a name that is just the creature is every
     * frame it has. All three are decided in ::sprite_slot_for, and none of
     * them is visible anywhere but in the atlas afterwards.
     *
     * THE MIDDLE ONE USED TO STOP AT THE WALK CYCLE, and the ward paid for it.
     * It ships an `_idle` and a `_down` and no `_attack`, so its attack cell
     * stayed the generated pillar -- and a ward is in ::E_ATTACK for the whole
     * wind-up every time a hit makes it summon. The drawn column turned into a
     * generated one at the one moment anybody was looking at it. So the spill
     * is asserted here, and so is the thing that makes it safe: a frame some
     * drawing NAMED is not something the spill may paint over.
     *
     * THIS EXISTS BECAUSE THE MIDDLE ONE SILENTLY DID NOTHING. ::name_state
     * answers ::SPR_WALK_BOTH -- which is -2 -- when it matches `_idle`, and
     * the caller tested `f < 0` for "no suffix found", so every matched `_idle`
     * was thrown away and the whole name went to the monster lookup instead.
     * `maw_idle` asked for a creature called "maw_idle", there is none, and the
     * drawing was dropped without a diagnostic. It shipped: the boss walked as
     * generated art with its own drawing appearing only on the attack and the
     * corpse, and the ward did the same.
     *
     * ASKED THROUGH THE DECODER, not by calling the name parser. The parser
     * gave the right answer the whole time -- what was broken was what the
     * caller did with it -- so a test that asks the parser passes while the
     * atlas stays wrong.
     *
     * *답은 셋이고 어느 것인지를 말하는 것은 이름뿐입니다.* 끝의 숫자는 한 프레임,
     * `_idle` 접미사는 걷기 두 프레임과 *다른 어떤 그림도 지목하지 않은 모든 프레임*, 생물
     * 이름만 있는 것은 그것이 가진 모든 프레임입니다. 셋 다 ::sprite_slot_for에서 결정되며,
     * 어느 것도 이후의 아틀라스 말고는 어디에서도 보이지 않습니다.
     * *가운데 것은 예전에 걷기 주기에서 멈췄고* 그 값을 결계핵이 치렀습니다. 결계핵은
     * `_idle`과 `_down`을 싣고 `_attack`은 싣지 않으므로 공격 칸이 생성된 기둥으로 남았습니다.
     * 그리고 결계핵은 피격이 소환을 일으킬 때마다 준비동작 내내 ::E_ATTACK에 있습니다. 그려진
     * 기둥이, 누군가 그것을 보고 있는 단 한 순간에 생성된 기둥으로 바뀌었습니다. 그래서 번짐을
     * 이곳에서 단언하며, 그것을 안전하게 만드는 것도 함께 단언합니다. 어떤 그림이 *지목한*
     * 프레임은 번짐이 덮어써도 되는 것이 아닙니다.
     * *이것이 존재하는 이유는 가운데 것이 조용히 아무 일도 하지 않았기 때문입니다.*
     * ::name_state는 `_idle`에 일치하면 -2인 ::SPR_WALK_BOTH로 답하는데, 호출자는 "접미사
     * 없음"을 `f < 0`으로 검사했으므로 일치한 `_idle`이 전부 버려지고 이름 전체가 몬스터
     * 조회로 넘어갔습니다. `maw_idle`은 "maw_idle"이라는 생물을 찾았고 그런 것은 없으며,
     * 그림은 진단 한 줄 없이 버려졌습니다. 그대로 출하되었습니다. 보스는 생성된 그림으로
     * 걸었고 자기 그림은 공격과 시체에서만 나타났으며, 결계석도 마찬가지였습니다.
     * *이름 해석기를 호출하지 않고 디코더를 통해 묻습니다.* 해석기는 내내 옳은 답을 냈고
     * 망가진 것은 호출자가 그 답으로 한 일이므로, 해석기에 묻는 검사는 아틀라스가 틀린 채로
     * 통과합니다. */
    printf("\na name says how many frames it fills\n");
    {
        static const unsigned char RED[4]   = { 255,   0,   0, 255 };
        static const unsigned char BLUE[4]  = {   0,   0, 255, 255 };
        static const unsigned char GREEN[4] = {   0, 255,   0, 255 };

        /* Filled with a stand-in for the generated creature, so a cell the
           drawing did not claim is visibly the one it did not claim.
           생성된 생물의 대역으로 채웁니다. 그래야 그림이 차지하지 않은 칸이 차지하지 않은
           칸으로 눈에 보입니다. */
        for (int i = 0; i < AW * AH; i++) {
            g_atlas[i*4+0] = 9; g_atlas[i*4+1] = 9;
            g_atlas[i*4+2] = 9; g_atlas[i*4+3] = 255;
        }
        blob_reset();
        /* IN THE ORDER bake.ps1 EMITS, which is name order: `_down` before
           `_idle` because `d` sorts before `i`. The ward's two files arrive
           exactly this way round.
           bake.ps1이 내보내는 순서, 곧 이름 순서입니다. `d`가 `i`보다 앞서므로 `_down`이
           `_idle`보다 먼저입니다. 결계핵의 두 파일이 정확히 이 순서로 도착합니다. */
        blob_png("water_spirit_down", 2, 1, GREEN, -1, -1, 0);
        blob_png("water_spirit_idle", 2, 1, RED,   -1, -1, 0);
        blob_png("brute",             2, 1, BLUE,  -1, -1, 0);
        sprite_decode_blob(blob(), g_atlas, AW, AH, 0);

        int idle_hit = 0, idle_kept = 0, all_hit = 0;
        for (int fr = 0; fr < SPR_FRAMES; fr++) {
            const unsigned char *a = at(0, fr, 2, 1, 0, 0);
            const unsigned char *b = at(1, fr, 2, 1, 0, 0);
            if (a[0] == 255 && a[3] == 255) idle_hit++;
            if (a[0] == 9   && a[3] == 255) idle_kept++;
            if (b[2] == 255 && b[3] == 255) all_hit++;
        }
        const unsigned char *corpse = at(0, SPR_DEAD, 2, 1, 0, 0);
        printf("      `_idle` filled %d of %d frames and left %d generated; "
               "the bare name filled %d\n",
               idle_hit, SPR_FRAMES, idle_kept, all_hit);

        ok(idle_hit == SPR_FRAMES - 1,
           "a single `_idle` fills every frame no other drawing named");
        ok(idle_kept == 0, "and leaves no generated cell behind on a drawn row");
        ok(corpse[1] == 255 && corpse[0] == 0,
           "while the corpse is still the drawing that named it");
        ok(all_hit == SPR_FRAMES,
           "a name with no suffix at all fills every frame it has");
    }

    /* --- 5. the boss is a drawing, and there is nothing behind it ---------
     *
     * EVERY OTHER CREATURE HAS A GENERATOR UNDERNEATH IT. sprite.c draws all of
     * them and a PNG replaces the cells it covers, which is what lets the
     * bestiary be half drawn and still show creatures. The boss is the one that
     * gave that up: its generated art was deleted when a single drawing took
     * over all five frames, so a `maw.png` that goes missing, gets renamed, or
     * fails to decode is not a boss in the old style -- it is no boss at all,
     * an empty cell walking around the arena, and nothing in the build says so.
     *
     * SO THIS ASKS THE SHIPPED ATLAS, not a fixture. ::sprite_dump_ppm builds
     * the same buffer ::sprite_atlas uploads, overlay included, and it is the
     * only headless view of it -- the tool exists so a human can eyeball the
     * sheet, and this turns the eyeballing into an assertion. Transparent
     * pixels come back as the dump's own checkerboard, 40 and 60 grey, so "the
     * cell has art" is "the cell is not entirely those two values".
     *
     * TWO PROPERTIES: there IS art, and every frame is the SAME art. The second
     * is what "the boss is a single sprite" means, and it is the one that
     * catches a per-frame drawing creeping back in beside the whole-name one.
     *
     * *다른 모든 생물은 아래에 생성기를 두고 있습니다.* sprite.c가 그것들을 전부 그리고 PNG가
     * 자기가 덮는 칸을 대체하며, 그래서 도감이 절반만 그려져도 생물을 보여 줄 수 있습니다.
     * 보스는 그것을 포기한 하나입니다. 그림 하나가 다섯 프레임 전부를 가져가면서 생성된
     * 아트를 지웠으므로, `maw.png`가 사라지거나 이름이 바뀌거나 디코딩에 실패하면 그것은 옛
     * 모습의 보스가 아니라 보스가 아예 없는 것입니다. 투기장을 걸어 다니는 빈 칸이며, 빌드의
     * 무엇도 그렇다고 말하지 않습니다.
     * *그래서 픽스처가 아니라 출하되는 아틀라스에 묻습니다.* ::sprite_dump_ppm은
     * ::sprite_atlas가 업로드하는 그 버퍼를 오버레이까지 포함해 만들며, 그것을 헤드리스로 보는
     * 유일한 창입니다. 이 도구는 사람이 시트를 눈으로 보라고 있는 것이고, 이 검사는 그 눈으로
     * 보기를 단언으로 바꿉니다. 투명 픽셀은 덤프 자신의 체크무늬인 40과 60 회색으로 돌아오므로,
     * "칸에 그림이 있다"는 "칸이 그 두 값만으로 이루어져 있지 않다"입니다.
     * *두 성질입니다.* 그림이 *있고*, 모든 프레임이 *같은* 그림입니다. 둘째가 "보스는 단일
     * 스프라이트"의 뜻이며, 이름 전체짜리 그림 옆으로 프레임별 그림이 다시 기어드는 것을
     * 잡아내는 쪽입니다. */
    printf("\nthe boss is one drawing and it is all of him\n");
    {
        /* BESIDE THE BINARY, not beside the caller. The first cut of this
           wrote "build/..." and passed under -Tool and failed under -Test,
           because the suite runs the exe from build\\ and a relative path is
           a claim about the CALLER's directory. ::plat_exe_dir is the answer
           the rest of the tools already use, and it ends in a separator so the
           name appends with no logic here.
           *호출자 옆이 아니라 바이너리 옆입니다.* 이것의 첫 판은 "build/..."에 썼고 -Tool에서
           통과했다가 -Test에서 실패했습니다. 스위트는 exe를 build\\에서 실행하며, 상대 경로는
           *호출자*의 디렉토리에 대한 주장이기 때문입니다. ::plat_exe_dir이 나머지 도구들이 이미
           쓰는 답이고, 구분자로 끝나므로 이곳에서 이름을 붙이는 데 별도 처리가 없습니다. */
        char pbuf[512];
        int pn = plat_exe_dir(pbuf, (int)sizeof pbuf);
        txt_copy(pbuf + pn, (int)sizeof pbuf - pn, "sprtest_boss.ppm", -1);
        const char *path = pbuf;
        if (!sprite_dump_ppm(path)) {
            ok(0, "the atlas could be dumped");
        } else {
            FILE *f = fopen(path, "rb");
            int W = 0, H = 0, maxv = 0;
            if (f && fscanf(f, "P6 %d %d %d", &W, &H, &maxv) == 3 &&
                W == SPR_CW * SPR_FRAMES && H == SPR_CH * MON_TYPES) {
                fgetc(f);                          /* the single byte after 255 */
                static unsigned char img[SPR_CW * SPR_FRAMES *
                                         SPR_CH * MON_TYPES * 3];
                size_t got = fread(img, 1, (size_t)W * H * 3, f);
                fclose(f);
                f = 0;
                ok(got == (size_t)W * H * 3, "the dumped atlas is a whole image");

                int inked = 0, same = 0;
                for (int fr = 0; fr < SPR_FRAMES; fr++) {
                    int differs = 0;
                    for (int y = 0; y < SPR_CH; y++)
                      for (int x = 0; x < SPR_CW; x++) {
                        const unsigned char *p =
                            &img[(((MON_MAW * SPR_CH + y) * W) +
                                  fr * SPR_CW + x) * 3];
                        const unsigned char *q =
                            &img[(((MON_MAW * SPR_CH + y) * W) + x) * 3];
                        if (fr == 0 && !(p[0] == 40 || p[0] == 60) ) inked++;
                        if (p[0] != q[0] || p[1] != q[1] || p[2] != q[2])
                            differs = 1;
                      }
                    if (!differs) same++;
                }
                printf("      frame 0 has %d drawn pixel(s); %d of %d frames "
                       "match it\n", inked, same, SPR_FRAMES);
                ok(inked > 500,
                   "the boss's cell holds a drawing and not the empty grid");
                ok(same == SPR_FRAMES,
                   "and every frame of him is that same one drawing");
            } else {
                if (f) fclose(f);
                ok(0, "the dumped atlas has the header the atlas has");
            }
            remove(path);
        }
    }

    /* --- 6. a monster's marker becomes its anchor -------------------------
     *
     * THE SAME MAGENTA PIXEL THE MUZZLE USES, on a creature. The ward carries
     * one at its gem and the boss's beam attaches there, so the point the beam
     * lands on is a fact about the drawing rather than a number beside it: a
     * taller ward or a redrawn pillar moves the beam with no edit anywhere.
     *
     * Three things, through the decoder: the anchor is where the marker was
     * placed, the marker itself is never painted, and a kind with no marker
     * says so rather than pointing at a corner.
     *
     * *총구가 쓰는 것과 같은 자홍색 픽셀*을 생물에 씁니다. 결계석은 보석에 하나를 지니고
     * 보스의 빔이 그곳에 붙으므로, 빔이 닿는 점은 그림 옆의 숫자가 아니라 그림에 대한
     * 사실입니다. 더 큰 결계석이나 다시 그린 기둥은 어디도 고치지 않고 빔을 옮깁니다.
     * 디코더를 통해 셋을 봅니다. 앵커는 표식이 놓인 자리이고, 표식 자체는 결코 칠해지지
     * 않으며, 표식 없는 종류는 구석을 가리키는 대신 없다고 말합니다. */
    printf("\na monster's marker becomes its anchor\n");
    {
        static const unsigned char RED[4]  = { 255, 0,   0, 255 };
        static const unsigned char MARK[4] = { 255, 0, 255, 255 };
        for (int i = 0; i < AW * AH; i++) {
            g_atlas[i*4+0] = 9; g_atlas[i*4+1] = 9;
            g_atlas[i*4+2] = 9; g_atlas[i*4+3] = 255;
        }
        blob_reset();
        blob_png("water_spirit_idle", 3, 3, RED, 1, 0, MARK);   /* marked at (1,0) */
        blob_png("brute",             3, 3, RED, -1, -1, 0);     /* no marker      */
        sprite_decode_blob(blob(), g_atlas, AW, AH, 0);

        /* Where the decoder seats a 3x3: centred, on the cell floor -- the
           same arithmetic at() uses, so the expectation and the readback
           cannot disagree about placement. */
        int ex = (SPR_CW - 3) / 2 + 1, ey = (SPR_CH - 3) + 0;
        int ax = -1, ay = -1;
        int has = sprite_mon_anchor_px(0, &ax, &ay);
        printf("      anchor at (%d,%d), expected (%d,%d)\n", ax, ay, ex, ey);
        ok(has && ax == ex && ay == ey, "the anchor is where the marker was placed");

        const unsigned char *mk = at(0, 0, 3, 3, 1, 0);
        ok(mk[3] == 0, "and the marker pixel itself was cut out, not painted");

        float u = -1.0f, v = -1.0f;
        ok(sprite_anchor(0, &u, &v) && v > 0.0f && v < 0.05f && u > 0.4f && u < 0.6f,
           "as a fraction: near the feet and across the middle");
        ok(!sprite_anchor(1, &u, &v), "a kind with no marker has no anchor");
    }

    /* --- 7. a drawing bigger than its cell is fitted, not cropped ----------
     *
     * WHAT OVERSIZED ART USED TO MEAN WAS "CROPPED", and nobody decided that.
     * Every picture this project had shipped was authored at exactly its cell,
     * so the blit's bounds check was only ever a safety net -- until the ward
     * was redrawn 160x688 for a 64x96 cell and walked into it. What reached the
     * atlas was the top-left corner of the file: the gem on top of the column
     * and none of the column.
     * ONE SCALE FOR BOTH AXES is the half that has to be asserted. Filling the
     * cell instead would also have got the whole drawing in, and would have
     * stored a picture nobody drew -- right only while some creature's `aspect`
     * happened to undo the squash exactly. So this asks the shape: a 128x96
     * drawing is 4:3, and 4:3 in a 64-wide cell is 48 tall and not 96.
     *
     * 한국어
     * ------
     * *셀보다 큰 그림이 뜻하던 것은 "잘림"이었고* 아무도 그렇게 정하지 않았습니다. 이
     * 프로젝트가 출하한 모든 그림은 정확히 자기 셀로 저작되었으므로 블릿의 경계 검사는 언제나
     * 안전망일 뿐이었습니다. 결계핵이 64x96 셀을 위해 160x688로 다시 그려져 그 안으로 걸어
     * 들어가기 전까지는 그랬습니다. 아틀라스에 도착한 것은 파일의 좌상단, 곧 기둥 위의 보석이었고
     * 기둥은 없었습니다.
     * *두 축에 같은 배율*이 단언해야 하는 나머지 절반입니다. 셀을 채워도 그림 전체는 들어갔을
     * 것이고, 아무도 그리지 않은 그림을 저장했을 것입니다. 어느 생물의 `aspect`가 그 찌그러짐을
     * 정확히 되돌릴 때에만 옳은 그림입니다. 그래서 이곳에서는 *모양*을 묻습니다. 128x96 그림은
     * 4:3이고, 64폭 셀 안의 4:3은 96이 아니라 48 높이입니다. */
    printf("\na drawing bigger than its cell is fitted, not cropped\n");
    {
        static const unsigned char RED[4] = { 255, 0, 0, 255 };
        clear(g_atlas, AW * AH);

        blob_reset();
        blob_png("water_spirit0", 128, 96, RED, -1, -1, 0);
        sprite_decode_blob(blob(), g_atlas, AW, AH, 0);

        /* The ink's own box inside cell (0,0), measured rather than assumed:
           what is under test is where the decoder put it.
           (0,0) 칸 안 잉크의 상자이며, 가정하지 않고 측정합니다. 검사 대상은 디코더가 그것을
           어디에 두었는가입니다. */
        int x0 = SPR_CW, x1 = -1, y0 = SPR_CH, y1 = -1;
        for (int y = 0; y < SPR_CH; y++)
            for (int x = 0; x < SPR_CW; x++) {
                const unsigned char *p = &g_atlas[(y * AW + x) * 4];
                if (p[3] == 0) continue;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        int iw = x1 - x0 + 1, ih = y1 - y0 + 1;
        printf("      a 128x96 drawing landed %dx%d at (%d,%d) in a %dx%d cell\n",
               iw, ih, x0, y0, SPR_CW, SPR_CH);

        okd(iw == SPR_CW, "it is as wide as the cell, so nothing was cropped away",
            iw, SPR_CW);
        okd(ih == SPR_CH / 2,
            "and half as tall, which is the 4:3 it was drawn at", ih, SPR_CH / 2);
        okd(y1 == SPR_CH - 1,
            "standing on the cell floor rather than floating at the top",
            y1, SPR_CH - 1);
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
