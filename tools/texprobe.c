/* texprobe -- a wall drawing fills the cell it is placed in.
 *
 * WHAT THIS CAUGHT, AND WHY NOTHING ELSE COULD. sprite.c places a drawing into
 * a cell and centres it when it is smaller, which is right for a creature and
 * wrong for a wall: a wall is a patch of surface whose whole job is to repeat.
 * A 64x64 wall in a 128x128 cell came out as the drawing plus a cleared border,
 * and tex.c's fill_from_image then tiled the BORDER along with the drawing.
 *
 * Measured on `wall_meat`, the one 64x64 surface this project shipped before
 * the LibreQuake import: 75.0% of its material was pure black -- exactly
 * (128^2 - 64^2)/128^2. Nothing pointed at it because no shipped map used that
 * material, and the number that would have said so is one nobody had asked for.
 * An imported map naming a 64x64 surface wears it on every face.
 *
 * `wall_meat` HAS SINCE BEEN DELETED, along with `black`, for the reason it was
 * invisible in the first place: no map ever placed either one. What replaced it
 * as this file's subject is better than it was -- the LibreQuake import brought
 * six 64x64 surfaces and a 16x16 that maps DO place, so the placement path is
 * now walked by content that ships rather than by the one drawing nobody
 * used. The list below is that content.
 *
 * WHAT IT ASSERTS, and why not the obvious thing. The first version of this
 * file measured how much of the MATERIAL came out black, which is what the bug
 * looks like -- and it was wrong twice on the first run: `black` is a texture
 * that is black, and `sky5_blu` is a night sky that is 22% dark. Darkness is
 * content. A cleared cell is not dark, it is UNWRITTEN, and the two are only
 * the same colour by coincidence.
 *
 * So this asks the sprite layer instead: ::sprite_wall fills a SPR_WALL square
 * from a drawing, and ::clear_cell zeroes what the drawing does not cover. A
 * pixel with alpha 0 is a pixel nothing was placed on. Every wall this project
 * ships is opaque, so "no alpha 0 anywhere" is exactly "the drawing reached the
 * whole cell" and says nothing at all about how dark the art is.
 *
 * 이것이 무엇을 잡았고 왜 다른 무엇도 잡을 수 없었는가. sprite.c는 그림을 셀에 놓고 더 작으면
 * 가운데에 둡니다. 생물에게는 옳고 벽에게는 틀립니다. 128x128 셀 안의 64x64 벽은 그림에 지워진
 * 테두리가 붙은 채로 나왔고, tex.c의 fill_from_image가 그림과 함께 *테두리*를 타일링했습니다.
 *
 * *무엇을 단언하며, 왜 자명한 쪽이 아닌가.* 이 파일의 첫 판은 *재질*이 얼마나 검게 나오는지를
 * 쟀습니다. 결함이 그렇게 보이기 때문입니다. 그리고 첫 실행에서 두 번 틀렸습니다. `black`은
 * 검은 텍스처이고 `sky5_blu`는 22%가 어두운 밤하늘입니다. 어두움은 콘텐츠입니다. 지워진 셀은
 * 어두운 것이 아니라 *쓰이지 않은* 것이며, 둘이 같은 색인 것은 우연일 뿐입니다.
 *
 * 그래서 스프라이트 계층에 묻습니다. ::sprite_wall은 그림으로 SPR_WALL 정사각형을 채우고,
 * ::clear_cell은 그림이 덮지 않은 곳을 0으로 만듭니다. 알파가 0인 픽셀은 아무것도 놓이지 않은
 * 픽셀입니다. 이 프로젝트가 출하하는 모든 벽은 불투명하므로 "어디에도 알파 0이 없다"는 곧
 * "그림이 셀 전체에 닿았다"이며, 아트가 얼마나 어두운지에 대해서는 아무 말도 하지 않습니다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sprite.h"
#include "tex.h"     /* TEX_SIZE -- how wide a material is */
#include "brush.h"   /* BRUSH_TEXELS -- what a UV of 1.0 is claimed to cross */
#include "data.h"    /* the material library, read as the text it is */
#include "txt.h"

static int fails;

static void fills(const char *name) {
    unsigned char *px = malloc(SPR_WALL * SPR_WALL * 4);
    if (!px) { printf("  out of memory\n"); fails++; return; }

    if (!sprite_wall(name, px)) {
        printf("  %-18s %s\n", name, "NO DRAWING -- FAIL");
        fails++;
        free(px);
        return;
    }

    int unwritten = 0;
    for (int i = 0; i < SPR_WALL * SPR_WALL; i++)
        if (px[i * 4 + 3] == 0) unwritten++;

    int ok = (unwritten == 0);
    printf("  %-18s %6d unwritten of %d   %s\n", name, unwritten,
           SPR_WALL * SPR_WALL, ok ? "ok" : "FAIL");
    if (!ok) fails++;
    free(px);
}

/* --- the tiling rate, which is a different question from the placement -----
 *
 * ENGLISH
 * -------
 * WHAT THIS CAUGHT. Reported as "the tiles in game repeat about twice as often
 * as they do in TrenchBroom", on `lqdm1`. Two numbers decide that and they were
 * being chosen by two different rules:
 *
 *   ::BRUSH_TEXELS is what one material spans, in the texels a Valve 220 face
 *   measures its offsets and scales in. Every UV in a brush level is a map
 *   coordinate divided by it.
 *   `image <name> <n>` in assets/textures.txt repeats a ::SPR_WALL drawing cell
 *   `n` times across the ::TEX_SIZE material.
 *
 * A cell always holds exactly SPR_WALL texels of the source's own grid --
 * sprite.c tiles a drawing smaller than its cell, which is what the check
 * above is about -- so a material spans `SPR_WALL * n` texels and that is what
 * BRUSH_TEXELS has to be. It was 128 while the counts were being picked as
 * "TEX_SIZE / the source's side", which is the count that FILLS a material
 * rather than the count that matches an editor: 2 for a 128 drawing, 4 for a
 * 64, 16 for a 16. So a 128 surface tiled twice as often in game as in
 * TrenchBroom, a 64 four times, `black` sixteen.
 *
 * WHY NEITHER HALF COULD SEE IT ALONE. A static assert in brush.h cannot read
 * a text asset, and the material library cannot see the divisor -- and the
 * failure is invisible in both: a wall tiled at twice its intended rate is a
 * wall, and it reads as an authoring mistake in the map rather than as a
 * constant that disagrees with a number in a .txt file. This is the one place
 * that can hold both, so it does.
 *
 * @note The count has to be the SAME for every wall material, which is a real
 *       constraint rather than tidiness: BRUSH_TEXELS is one number and every
 *       brush face divides by it, so a material tiling its cell a different
 *       number of times is a material at a different scale from its
 *       neighbours on the same wall.
 *
 * 한국어
 * ------
 * *이것이 무엇을 잡았는가.* `lqdm1`에서 "게임 안의 타일이 TrenchBroom보다 두 배쯤 자주
 * 반복된다"고 신고되었습니다. 그것을 정하는 수는 둘이고, 서로 다른 규칙으로 골라지고
 * 있었습니다.
 *
 *   ::BRUSH_TEXELS는 재질 하나가 걸치는 텍셀 수이며, Valve 220 면이 오프셋과 배율을 재는
 *   단위입니다. 브러시 레벨의 모든 UV가 맵 좌표를 이 값으로 나눈 것입니다.
 *   assets/textures.txt의 `image <name> <n>`은 ::SPR_WALL 그림 셀을 ::TEX_SIZE 재질에 걸쳐
 *   `n`번 반복합니다.
 *
 * 셀 하나는 언제나 원본 격자의 SPR_WALL 텍셀에 정확히 해당하므로(sprite.c가 셀보다 작은 그림을
 * 타일링하며, 위의 검사가 바로 그것에 관한 것입니다) 재질 하나는 `SPR_WALL * n` 텍셀에 걸치고,
 * 그것이 BRUSH_TEXELS여야 하는 값입니다. 그 값은 128이었고 그동안 개수는 "TEX_SIZE를 원본의
 * 변으로 나눈 값"으로 골라지고 있었습니다. 그것은 재질을 *채우는* 수이지 에디터와 맞추는 수가
 * 아닙니다. 128 그림에 2, 64에 4, 16에 16입니다. 그래서 128 표면은 게임에서 TrenchBroom보다
 * 두 배, 64는 네 배, `black`은 열여섯 배로 반복했습니다.
 *
 * *어느 절반도 혼자서는 볼 수 없던 이유.* brush.h의 정적 검사는 텍스트 에셋을 읽을 수 없고,
 * 재질 라이브러리는 제수를 볼 수 없습니다. 그리고 실패는 양쪽 모두에서 보이지 않습니다. 의도한
 * 배율의 두 배로 타일링된 벽도 벽이며, 그것은 .txt 파일의 수와 어긋나는 상수가 아니라 맵의 제작
 * 실수처럼 읽힙니다. 둘 다 쥘 수 있는 곳이 이곳뿐이므로 이곳이 쥡니다.
 *
 * @note 개수는 모든 벽 재질에 대해 *같아야* 하며, 이는 정돈이 아니라 실제 제약입니다.
 *       BRUSH_TEXELS는 하나의 수이고 모든 브러시 면이 그것으로 나누므로, 셀을 다른 횟수로
 *       타일링하는 재질은 같은 벽 위의 이웃과 다른 배율을 가진 재질입니다. */
static void tiling_rate(void) {
    const int want = TEX_SIZE / SPR_WALL;

    printf("\n  a material is %d wide and a cell is %d, so every wall material\n"
           "  must tile its cell %d times, and BRUSH_TEXELS must be %d\n",
           TEX_SIZE, SPR_WALL, want, SPR_WALL * want);

    int n = 0, wrong = 0;
    const char *p = data_text(DATA_RECIPES);

    /* Walks the material library looking for `image <name> <n>`. Reading the
       asset rather than asking tex.c, because what is under test is a number
       an author types and tex.c only ever consumes.
       재질 라이브러리를 훑으며 `image <name> <n>`을 찾습니다. tex.c에 묻지 않고 에셋을 직접
       읽는 이유는, 검사 대상이 제작자가 타이핑하는 수이고 tex.c는 그것을 소비하기만 하기
       때문입니다. */
    while (p && *p) {
        const char *line = p;
        while (*p && *p != '\n') p++;
        int len = (int)(p - line);
        if (*p) p++;

        while (len && (*line == ' ' || *line == '\t')) { line++; len--; }
        if (len < 6 || !txt_is(line, 5, "image")) continue;

        const char *q = line + 5;
        int rest = len - 5;
        while (rest && (*q == ' ' || *q == '\t')) { q++; rest--; }
        const char *nm = q;
        int nlen = 0;
        while (nlen < rest && q[nlen] != ' ' && q[nlen] != '\t') nlen++;
        q += nlen; rest -= nlen;
        while (rest && (*q == ' ' || *q == '\t')) { q++; rest--; }

        int ok_num = 0, tiles = 0;
        txt_read_int(q, &tiles, &ok_num);
        if (!ok_num) continue;

        n++;
        if (tiles != want) {
            wrong++;
            printf("    %.*s tiles its cell %d times, wants %d  FAIL\n",
                   nlen, nm, tiles, want);
        }
    }

    printf("    %d wall materials, %d at the wrong rate\n", n, wrong);
    if (n < 1) { printf("    no `image` op found at all -- FAIL\n"); fails++; }
    if (wrong) fails++;

    int span = SPR_WALL * want;
    int ok = ((int)BRUSH_TEXELS == span);
    printf("    BRUSH_TEXELS %d, a material spans %d   %s\n",
           (int)BRUSH_TEXELS, span, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

int main(void) {
    printf("texprobe -- every wall drawing fills its %dx%d cell\n\n",
           SPR_WALL, SPR_WALL);

    /* The project's own. `wall_meat` was here and was the 64x64 drawing this
       test exists for; it is deleted, and the LibreQuake block below carries
       that duty now.
       이 프로젝트 자신의 것들입니다. `wall_meat`이 이곳에 있었고 이 테스트가 존재하는 이유인
       64x64 그림이었습니다. 그것은 삭제되었고, 아래의 LibreQuake 블록이 그 역할을
       이어받습니다. */
    fills("wall_stone");
    fills("wall_brick");
    fills("wall_rough");
    fills("wall_metal");
    fills("wall_marble");
    fills("wall_plain");
    fills("wall_door");
    fills("wall_track");

    /* LibreQuake's, which arrive at 64 and 128 on a side -- so both paths
       through the placement are walked by something a shipped map places.
       `black` was the 16x16 that made it three, and it went with `wall_meat`:
       it was in the library and on no face.
       LibreQuake의 것들이며 변이 64와 128로 도착합니다. 그러므로 배치의 두 경로 모두를
       출하되는 맵이 배치하는 무언가가 밟습니다. `black`이 그것을 셋으로 만들던 16x16이었고
       `wall_meat`과 함께 갔습니다. 라이브러리에는 있었고 어떤 면에도 없었습니다. */
    fills("met_brn_flat");      /*  64 */
    fills("met_brn_trim16");    /*  64 */
    fills("sq_trim1_2_s");      /*  64 */
    fills("star_water0");       /*  64 */
    fills("grk_leaf1_1");       /*  64 */
    fills("med_glass5");        /*  64 */
    fills("med_csl_brk14b");    /* 128 */
    fills("snow1");             /* 128 */
    fills("sky5_blu");          /* 128 */
    fills("med_tanwall4");      /* 128 */

    tiling_rate();

    printf("\n%s\n", fails ? "WALL SURFACES DO NOT REACH THE SCREEN AS AUTHORED"
                           : "every wall drawing fills its cell, at the rate the"
                             " editor shows it");
    return fails != 0;
}
