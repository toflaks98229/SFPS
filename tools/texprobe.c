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
#include "sprite.h"

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

int main(void) {
    printf("texprobe -- every wall drawing fills its %dx%d cell\n\n",
           SPR_WALL, SPR_WALL);

    /* The project's own, including the 64x64 one this test exists for. */
    fills("wall_meat");
    fills("wall_stone");
    fills("wall_brick");
    fills("wall_rough");
    fills("wall_metal");
    fills("wall_marble");
    fills("wall_plain");
    fills("wall_door");
    fills("wall_track");

    /* LibreQuake's, which arrive at 16, 64 and 128 on a side -- so every path
       through the placement is walked by something that ships.
       LibreQuake의 것들이며 변이 16, 64, 128로 도착합니다. 그러므로 배치의 모든 경로를
       출하되는 무언가가 밟습니다. */
    fills("black");             /*  16 -- eight tilings deep */
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

    printf("\n%s\n", fails ? "SOME WALL DRAWINGS LEAVE THEIR CELL UNWRITTEN"
                           : "every wall drawing fills its cell");
    return fails != 0;
}
