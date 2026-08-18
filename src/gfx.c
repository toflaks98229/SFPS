/**
 * @file gfx.c
 * @brief The two tables the settings menu indexes into, and what they set.
 *
 * ENGLISH: Both of these were inline in main.c, one beside the other, and
 * neither is about a window -- they are what a preset MEANS. See gfx.h for why
 * this file is portable and what stayed behind.
 *
 * 한국어: 둘 다 main.c 안에 나란히 인라인으로 있었고, 어느 것도 창에 대한 것이 아닙니다.
 * 프리셋이 무엇을 *뜻하는지*입니다. 이 파일이 이식 가능한 이유와 무엇이 남았는지는 gfx.h를
 * 참조하십시오.
 */

#include "gfx.h"
#include "post.h"   /* the pass these settings configure */
#include "menu.h"   /* GfxPixelPreset, GfxDither, GfxPattern and the settings block */

/**
 * @brief Art-resolution target for each ::GfxPixelPreset, in pixels of height.
 *
 * ENGLISH
 * -------
 * Indexed by the preset, so a new preset is a row here and an enum value in
 * menu.h -- nothing else. ::POST_HEIGHT remains the compiled-in default and is
 * what ::GFX_PIXEL_NORMAL reproduces exactly, so shipping with the menu
 * untouched renders precisely the frame the game rendered before it existed.
 *
 * 한국어
 * ------
 * @brief 각 ::GfxPixelPreset의 아트 해상도 목표 높이(픽셀)입니다.
 *
 * 프리셋으로 인덱싱되므로 새 프리셋은 이곳의 행 하나와 menu.h의 열거형 값 하나가
 * 전부입니다. ::POST_HEIGHT는 컴파일 시점 기본값으로 남으며 ::GFX_PIXEL_NORMAL이 그것을
 * 정확히 재현하므로, 메뉴를 건드리지 않고 실행하면 이 기능이 없던 때와 정확히 같은
 * 프레임이 그려집니다.
 */
static const int PIXEL_HEIGHTS[GFX_PIXEL_COUNT] = {
    240,           /* GFX_PIXEL_CHUNKY -- roughly the PlayStation's own */
    POST_HEIGHT,   /* GFX_PIXEL_NORMAL -- the shipped default, 360 */
    540            /* GFX_PIXEL_FINE   -- close to unpixelised at 1080p */
};

_Static_assert(sizeof(PIXEL_HEIGHTS) / sizeof(PIXEL_HEIGHTS[0]) == GFX_PIXEL_COUNT,
               "PIXEL_HEIGHTS needs exactly one entry per GfxPixelPreset");

void gfx_apply_pixel_preset(int preset, int client_w, int client_h) {
    if (preset < 0 || preset >= GFX_PIXEL_COUNT) preset = GFX_PIXEL_NORMAL;

    /* Clamped HERE rather than at the call site, so a caller may hand over a
       raw client rect. A minimised window reports a zero-height client area and
       the division below would be by zero.
       호출 지점이 아니라 *이곳*에서 보정하므로 호출자는 클라이언트 사각형을 그대로 넘겨도
       됩니다. 최소화된 창은 높이 0인 클라이언트 영역을 보고하며, 아래의 나눗셈이 0으로
       나누기가 됩니다. */
    if (client_w < 1) client_w = 1;
    if (client_h < 1) client_h = 1;

    int target = PIXEL_HEIGHTS[preset];
    int scale  = client_h / target;
    if (scale < 1) scale = 1;

    /* Preserve whether the effect was switched off, so changing the pixel size
       does not quietly switch the whole pass back on.
       효과가 꺼져 있었는지를 보존합니다. 픽셀 크기를 바꾸는 것이 패스 전체를 조용히 다시
       켜서는 안 됩니다. */
    int was_on = post_enabled();

    post_shutdown();
    post_init(client_w / scale, client_h / scale);
    post_set_enabled(was_on);
}

void gfx_apply_live_settings(void) {
    post_set_enabled(menu_settings()->post_on);
    post_set_scanline(menu_settings()->scanlines ? POST_SCANLINE_DEFAULT : 0.0f);

    /* Steps per channel, and the grain that rides on them. A table rather
       than arithmetic on the enum, because these are four points chosen by
       eye and the spacing between them is not regular -- HEAVY to NORMAL
       is the jump that matters and the rest is fine tuning.
       OFF is not zero dither: it is thirty-two steps, the PlayStation's own
       five bits a channel, where the pattern stops being visible on its own
       rather than being switched off.
       채널당 단계 수와 그 위에 얹히는 그레인입니다. 열거형에 대한 산술이 아니라
       표인 이유는, 이 넷이 눈으로 고른 지점이고 간격이 규칙적이지 않기 때문입니다.
       HEAVY에서 NORMAL로 가는 것이 중요한 도약이고 나머지는 미세 조정입니다.
       OFF는 디더 0이 아니라 32단계, 즉 플레이스테이션 자신의 채널당 5비트이며,
       패턴이 꺼지는 것이 아니라 그 자체로는 보이지 않게 되는 지점입니다. */
    static const struct { float levels, grain; } DITHER[GFX_DITHER_COUNT] = {
        {  4.0f, 0.050f },   /* HEAVY  -- what this shipped with */
        { 12.0f, 0.015f },   /* NORMAL */
        { 20.0f, 0.008f },   /* LIGHT  */
        { 32.0f, 0.000f },   /* OFF    -- the PlayStation's 15-bit colour */
    };
    int di = menu_settings()->dither;
    if (di < 0 || di >= GFX_DITHER_COUNT) di = GFX_DITHER_NORMAL;
    int pat = menu_settings()->pattern;
    if (pat < 0 || pat >= GFX_PATTERN_COUNT) pat = GFX_PATTERN_BAYER;
    post_set_dither(DITHER[di].levels, DITHER[di].grain,
                    pat == GFX_PATTERN_NOISE ? 1.0f : 0.0f);
}
