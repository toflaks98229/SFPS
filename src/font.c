/**
 * @file font.c
 * @brief Bakes the glyph table into an atlas and lays strings out as quads.
 *
 * ENGLISH
 * -------
 * Two halves that share nothing but the cell arithmetic. ::font_init runs once
 * and turns 480 bytes of column bitmaps into a texture; ::font_text runs every
 * frame and turns a string into triangles. Neither knows what the other is
 * for, which is why a headless tool can call ::font_width without ever
 * creating a GL context.
 *
 * The atlas is a fixed grid rather than a packed one. Packing would save
 * memory this font does not need and would cost a per-glyph lookup table --
 * with every cell the same size, a glyph's position is two divisions and
 * nothing has to be stored.
 *
 * @note The public contract lives in font.h and is not repeated here. What is
 *       documented below is what the header cannot say: why the atlas is built
 *       the way it is.
 *
 * 한국어
 * ------
 * 셀 계산 외에는 아무것도 공유하지 않는 두 부분으로 이루어집니다. ::font_init은 한 번
 * 실행되어 480바이트의 열 비트맵을 텍스처로 바꾸고, ::font_text는 매 프레임 실행되어
 * 문자열을 삼각형으로 바꿉니다. 서로가 무엇을 위한 것인지 알지 못하며, 그래서 헤드리스
 * 도구가 GL 컨텍스트를 만들지 않고도 ::font_width를 호출할 수 있습니다.
 *
 * 아틀라스는 조밀하게 채운 것이 아니라 고정 격자입니다. 조밀하게 채우면 이 폰트에는
 * 필요하지 않은 메모리를 아끼는 대신 글리프마다 조회 표가 필요해집니다. 모든 셀의 크기가
 * 같으면 글리프의 위치는 나눗셈 두 번이며 저장할 것이 없습니다.
 *
 * @note 공개 계약은 font.h에 있으며 이곳에서 되풀이하지 않습니다. 아래에 문서화한 것은
 *       헤더가 말할 수 없는 것, 즉 아틀라스를 왜 이렇게 만드는가입니다.
 */

#include "font.h"
#include <stdlib.h>   /* malloc/calloc/free: this file used to reach these through windows.h */

/* --- Glyph geometry / 글리프 형상 --- */

#define GLYPH_W  5      /**< @brief Glyph width in pixels. / 글리프 너비 (픽셀). */
#define GLYPH_H  7      /**< @brief Glyph height in pixels. / 글리프 높이 (픽셀). */

/**
 * @brief Atlas cell size, glyph plus padding.
 *
 * ENGLISH
 * -------
 * Eight for a 5x7 glyph, so every cell carries three blank columns and one
 * blank row of its own. ::font_text samples a ::FONT_CW by ::FONT_CH window
 * out of that, which lands one transparent column and one transparent row
 * inside the cell -- the gutter between characters is baked into the texture
 * rather than opened up by the layout.
 *
 * @note A power of two, which is what keeps the atlas dimensions powers of two
 *       as well. Nothing here requires that of GL 3.3, but it costs nothing
 *       and keeps the divisions below exact.
 *
 * 한국어
 * ------
 * @brief 아틀라스 셀 크기. 글리프에 여백을 더한 값입니다.
 *
 * 5x7 글리프에 대해 8이므로, 모든 셀은 자기 몫의 빈 열 세 개와 빈 행 하나를 지닙니다.
 * ::font_text는 그중 ::FONT_CW × ::FONT_CH 크기의 창을 표본으로 삼으며, 그 창은 셀 안의
 * 투명한 열 하나와 투명한 행 하나를 포함합니다. 문자 사이의 간격은 레이아웃이 벌리는 것이
 * 아니라 텍스처에 구워져 있습니다.
 *
 * @note 2의 거듭제곱이며, 그래서 아틀라스 치수도 2의 거듭제곱으로 유지됩니다. GL 3.3이
 *       그것을 요구하지는 않지만 비용이 들지 않고 아래의 나눗셈을 정확하게 유지합니다.
 */
#define CELL     8

#define COLS     16     /**< @brief Cells per atlas row. / 아틀라스 한 행의 셀 수. */
#define ROWS     6      /**< @brief Cell rows in the atlas. / 아틀라스의 셀 행 수. */
#define ATLAS_W  (COLS * CELL) /**< @brief Atlas width in pixels. / 아틀라스 너비 (픽셀). */
#define ATLAS_H  (ROWS * CELL) /**< @brief Atlas height in pixels. / 아틀라스 높이 (픽셀). */

/* --- Character range / 문자 범위 --- */

#define FIRST    32     /**< @brief ASCII code of the first glyph, space. / 첫 글리프의 ASCII 코드. 공백입니다. */
#define COUNT    96     /**< @brief Printable ASCII glyphs, space through DEL. / 출력 가능한 ASCII 글리프 수. 공백부터 DEL까지. */

/* --- Static data / 정적 데이터 --- */

/**
 * @brief Column bitmaps for the 96 printable ASCII glyphs.
 *
 * ENGLISH
 * -------
 * Five bytes per character, one per column left to right, bit 0 at the top.
 * Stored by column rather than by row because a 7-pixel row would waste a bit
 * per byte and a 5-pixel column does not -- 480 bytes total, which is the
 * whole font.
 *
 * @note Indexed as `GLYPHS[(c - FIRST) * GLYPH_W + column]`. The table must
 *       hold exactly ::COUNT entries or the atlas walk below reads past it;
 *       the array bound states that rather than trusting the literal count.
 *
 * 한국어
 * ------
 * @brief 출력 가능한 ASCII 글리프 96개의 열 비트맵.
 *
 * 문자당 5바이트이며, 왼쪽에서 오른쪽으로 한 열에 1바이트씩, 비트 0이 맨 위입니다. 행이
 * 아니라 열로 저장하는 이유는 7픽셀짜리 행은 바이트마다 1비트를 버리지만 5픽셀짜리 열은
 * 그렇지 않기 때문입니다. 전체 480바이트이며, 그것이 폰트의 전부입니다.
 *
 * @note `GLYPHS[(c - FIRST) * GLYPH_W + 열]`로 인덱싱합니다. 표는 정확히 ::COUNT개의
 *       항목을 담아야 하며, 그렇지 않으면 아래의 아틀라스 순회가 범위를 넘어 읽습니다.
 *       리터럴 개수를 믿는 대신 배열 크기가 그것을 명시합니다.
 */
static const unsigned char GLYPHS[COUNT * GLYPH_W] = {
    0x00,0x00,0x00,0x00,0x00,  /*   */  0x00,0x00,0x5F,0x00,0x00,  /* ! */
    0x00,0x07,0x00,0x07,0x00,  /* " */  0x14,0x7F,0x14,0x7F,0x14,  /* # */
    0x24,0x2A,0x7F,0x2A,0x12,  /* $ */  0x23,0x13,0x08,0x64,0x62,  /* % */
    0x36,0x49,0x55,0x22,0x50,  /* & */  0x00,0x05,0x03,0x00,0x00,  /* ' */
    0x00,0x1C,0x22,0x41,0x00,  /* ( */  0x00,0x41,0x22,0x1C,0x00,  /* ) */
    0x14,0x08,0x3E,0x08,0x14,  /* * */  0x08,0x08,0x3E,0x08,0x08,  /* + */
    0x00,0x50,0x30,0x00,0x00,  /* , */  0x08,0x08,0x08,0x08,0x08,  /* - */
    0x00,0x60,0x60,0x00,0x00,  /* . */  0x20,0x10,0x08,0x04,0x02,  /* / */
    0x3E,0x51,0x49,0x45,0x3E,  /* 0 */  0x00,0x42,0x7F,0x40,0x00,  /* 1 */
    0x42,0x61,0x51,0x49,0x46,  /* 2 */  0x21,0x41,0x45,0x4B,0x31,  /* 3 */
    0x18,0x14,0x12,0x7F,0x10,  /* 4 */  0x27,0x45,0x45,0x45,0x39,  /* 5 */
    0x3C,0x4A,0x49,0x49,0x30,  /* 6 */  0x01,0x71,0x09,0x05,0x03,  /* 7 */
    0x36,0x49,0x49,0x49,0x36,  /* 8 */  0x06,0x49,0x49,0x29,0x1E,  /* 9 */
    0x00,0x36,0x36,0x00,0x00,  /* : */  0x00,0x56,0x36,0x00,0x00,  /* ; */
    0x08,0x14,0x22,0x41,0x00,  /* < */  0x14,0x14,0x14,0x14,0x14,  /* = */
    0x00,0x41,0x22,0x14,0x08,  /* > */  0x02,0x01,0x51,0x09,0x06,  /* ? */
    0x32,0x49,0x79,0x41,0x3E,  /* @ */  0x7E,0x11,0x11,0x11,0x7E,  /* A */
    0x7F,0x49,0x49,0x49,0x36,  /* B */  0x3E,0x41,0x41,0x41,0x22,  /* C */
    0x7F,0x41,0x41,0x22,0x1C,  /* D */  0x7F,0x49,0x49,0x49,0x41,  /* E */
    0x7F,0x09,0x09,0x09,0x01,  /* F */  0x3E,0x41,0x49,0x49,0x7A,  /* G */
    0x7F,0x08,0x08,0x08,0x7F,  /* H */  0x00,0x41,0x7F,0x41,0x00,  /* I */
    0x20,0x40,0x41,0x3F,0x01,  /* J */  0x7F,0x08,0x14,0x22,0x41,  /* K */
    0x7F,0x40,0x40,0x40,0x40,  /* L */  0x7F,0x02,0x0C,0x02,0x7F,  /* M */
    0x7F,0x04,0x08,0x10,0x7F,  /* N */  0x3E,0x41,0x41,0x41,0x3E,  /* O */
    0x7F,0x09,0x09,0x09,0x06,  /* P */  0x3E,0x41,0x51,0x21,0x5E,  /* Q */
    0x7F,0x09,0x19,0x29,0x46,  /* R */  0x46,0x49,0x49,0x49,0x31,  /* S */
    0x01,0x01,0x7F,0x01,0x01,  /* T */  0x3F,0x40,0x40,0x40,0x3F,  /* U */
    0x1F,0x20,0x40,0x20,0x1F,  /* V */  0x3F,0x40,0x38,0x40,0x3F,  /* W */
    0x63,0x14,0x08,0x14,0x63,  /* X */  0x07,0x08,0x70,0x08,0x07,  /* Y */
    0x61,0x51,0x49,0x45,0x43,  /* Z */  0x00,0x7F,0x41,0x41,0x00,  /* [ */
    0x02,0x04,0x08,0x10,0x20,  /* \ */  0x00,0x41,0x41,0x7F,0x00,  /* ] */
    0x04,0x02,0x01,0x02,0x04,  /* ^ */  0x40,0x40,0x40,0x40,0x40,  /* _ */
    0x00,0x01,0x02,0x04,0x00,  /* ` */  0x20,0x54,0x54,0x54,0x78,  /* a */
    0x7F,0x48,0x44,0x44,0x38,  /* b */  0x38,0x44,0x44,0x44,0x20,  /* c */
    0x38,0x44,0x44,0x48,0x7F,  /* d */  0x38,0x54,0x54,0x54,0x18,  /* e */
    0x08,0x7E,0x09,0x01,0x02,  /* f */  0x0C,0x52,0x52,0x52,0x3E,  /* g */
    0x7F,0x08,0x04,0x04,0x78,  /* h */  0x00,0x44,0x7D,0x40,0x00,  /* i */
    0x20,0x40,0x44,0x3D,0x00,  /* j */  0x7F,0x10,0x28,0x44,0x00,  /* k */
    0x00,0x41,0x7F,0x40,0x00,  /* l */  0x7C,0x04,0x18,0x04,0x78,  /* m */
    0x7C,0x08,0x04,0x04,0x78,  /* n */  0x38,0x44,0x44,0x44,0x38,  /* o */
    0x7C,0x14,0x14,0x14,0x08,  /* p */  0x08,0x14,0x14,0x18,0x7C,  /* q */
    0x7C,0x08,0x04,0x04,0x08,  /* r */  0x48,0x54,0x54,0x54,0x20,  /* s */
    0x04,0x3F,0x44,0x40,0x20,  /* t */  0x3C,0x40,0x40,0x20,0x7C,  /* u */
    0x1C,0x20,0x40,0x20,0x1C,  /* v */  0x3C,0x40,0x30,0x40,0x3C,  /* w */
    0x44,0x28,0x10,0x28,0x44,  /* x */  0x0C,0x50,0x50,0x50,0x3C,  /* y */
    0x44,0x64,0x54,0x4C,0x44,  /* z */  0x00,0x08,0x36,0x41,0x00,  /* { */
    0x00,0x00,0x7F,0x00,0x00,  /* | */  0x00,0x41,0x36,0x08,0x00,  /* } */
    0x08,0x04,0x08,0x10,0x08,  /* ~ */  0x00,0x00,0x00,0x00,0x00,  /* del */
};

/* --- Module state / 모듈 상태 --- */

/**
 * @brief The baked atlas, or 0 before ::font_init has built it.
 *
 * ENGLISH
 * -------
 * Doubles as the "already built" flag, which is what makes ::font_init
 * idempotent without a second variable. Zero is both "not built" and "GL name
 * that binds nothing", so the failure path needs no special case: a caller
 * that binds it draws nothing rather than drawing garbage.
 *
 * @note Never deleted. The atlas lives for the process; see font.h.
 *
 * 한국어
 * ------
 * @brief 구워진 아틀라스. ::font_init이 만들기 전에는 0입니다.
 *
 * "이미 만들었음" 표시를 겸하며, 그래서 두 번째 변수 없이 ::font_init이 멱등해집니다.
 * 0은 "만들지 않았음"인 동시에 "아무것도 바인딩하지 않는 GL 이름"이므로 실패 경로에
 * 특별한 처리가 필요 없습니다. 이것을 바인딩한 호출자는 쓰레기를 그리는 대신 아무것도
 * 그리지 않습니다.
 *
 * @note 결코 삭제하지 않습니다. 아틀라스는 프로세스와 수명을 같이합니다. font.h를
 *       참조하십시오.
 */
static GLuint g_tex;

/* --- Public function definitions / 공개 함수 정의 --- */

void font_init(void) {
    /* The texture name doubles as the built flag, so a second call is a no-op
       and neither the game nor a tool has to know whether the other ran first.
       텍스처 이름이 생성 여부 표시를 겸하므로 두 번째 호출은 아무 일도 하지 않으며, 게임과
       도구 중 어느 쪽이 먼저 실행되었는지 서로 알 필요가 없습니다. */
    if (g_tex) return;

    /* calloc, not malloc: the atlas is drawn glyph by glyph and every pixel no
       glyph touches has to be transparent black already. malloc would leave the
       gutters holding whatever was in the heap, and those gutters are sampled
       -- FONT_CW is wider than GLYPH_W by exactly one column.
       malloc이 아니라 calloc입니다. 아틀라스는 글리프 단위로 그려지므로 어떤 글리프도
       건드리지 않는 픽셀은 이미 투명한 검정이어야 합니다. malloc이었다면 여백에 힙에 있던
       값이 남고, 그 여백은 표본으로 쓰입니다. FONT_CW는 GLYPH_W보다 정확히 한 열 넓습니다. */
    unsigned char *px = calloc((size_t)ATLAS_W * ATLAS_H * 4, 1);
    if (!px) return;

    for (int i = 0; i < COUNT; i++) {
        /* Cell origin from the linear glyph index. Every cell being the same
           size is what makes this two divisions instead of a lookup table.
           선형 글리프 인덱스로부터 셀의 원점을 구합니다. 모든 셀의 크기가 같기에 조회 표
           대신 나눗셈 두 번으로 끝납니다. */
        int cx = (i % COLS) * CELL, cy = (i / COLS) * CELL;

        for (int gx = 0; gx < GLYPH_W; gx++) {
            unsigned char col = GLYPHS[i * GLYPH_W + gx];

            for (int gy = 0; gy < GLYPH_H; gy++) {
                /* Bit gy of the column byte is the pixel at row gy, bit 0 at
                   the top. A clear bit is left as the calloc'd transparent
                   black rather than written, which is why only set bits cost
                   anything here.
                   열 바이트의 gy번째 비트가 gy행의 픽셀이며 비트 0이 맨 위입니다. 0인
                   비트는 기록하지 않고 calloc이 남긴 투명한 검정 그대로 둡니다. 그래서
                   이곳에서 비용이 드는 것은 켜진 비트뿐입니다. */
                if (!(col & (1u << gy))) continue;

                unsigned char *p = &px[((cy + gy) * ATLAS_W + cx + gx) * 4];

                /* White, because the text shader multiplies its own colour by
                   this. Storing the colour here instead would need one atlas
                   per colour; storing white makes the atlas a pure alpha mask
                   that any colour can be drawn through.
                   흰색입니다. 텍스트 셰이더가 자신의 색을 이 값에 곱하기 때문입니다. 색을
                   이곳에 저장했다면 색마다 아틀라스가 하나씩 필요했을 것입니다. 흰색으로
                   저장하면 아틀라스는 어떤 색으로든 그릴 수 있는 순수한 알파 마스크가
                   됩니다. */
                p[0] = p[1] = p[2] = 255;
                p[3] = 255;
            }
        }
    }

    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ATLAS_W, ATLAS_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);

    /* NEAREST because a bitmap font that is filtered is a blurred bitmap font;
       the pixels are the design. CLAMP_TO_EDGE because the alternative pulls a
       neighbouring glyph in at the seam -- the cells touch, so a UV that
       wrapped or that bled by half a texel would sample the letter next door.
       비트맵 폰트를 필터링하면 뭉개진 비트맵 폰트가 되므로 NEAREST입니다. 픽셀 자체가
       디자인입니다. CLAMP_TO_EDGE인 이유는 그러지 않으면 이음새에서 옆 글리프가 끌려오기
       때문입니다. 셀들이 맞닿아 있어 순환하거나 반 텍셀만큼 번지는 UV는 옆 글자를 표본으로
       삼게 됩니다. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* The pixels have been copied into the texture object; the staging buffer
       is dead the moment glTexImage2D returns.
       픽셀은 텍스처 객체로 복사되었습니다. glTexImage2D가 반환하는 순간 준비 버퍼는 쓸모를
       다합니다. */
    free(px);
}

GLuint font_texture(void) { return g_tex; }

float font_width(float size, const char *s) {
    /* Counted rather than measured: the font is fixed-pitch, so the width
       depends on how many characters there are and not on which ones. That is
       also what lets this run with no atlas and no GL context.
       재는 것이 아니라 세는 것입니다. 이 폰트는 고정폭이므로 너비는 문자가 몇 개인지에
       달려 있을 뿐 어떤 문자인지와는 무관합니다. 그 덕분에 아틀라스도 GL 컨텍스트도 없이
       실행할 수 있습니다. */
    int n = 0;
    while (s[n]) n++;
    return n * FONT_CW * size;
}

float font_text(MeshBuf *b, float x, float y, float size, const char *s) {
    float pen = x;

    /* One normal for every vertex of every glyph. Text is a flat billboard in
       the caller's space, so the value is constant and hoisted out of the loop
       rather than rebuilt per character.
       모든 글리프의 모든 정점이 쓰는 법선 하나입니다. 텍스트는 호출자 공간에서 평평한
       빌보드이므로 값이 일정하며, 문자마다 다시 만들지 않고 루프 밖으로 끌어냈습니다. */
    v3 n = v3f(0, 0, 1);

    for (int i = 0; s[i]; i++) {
        /* Cast through unsigned char before comparing: a plain char is signed
           here, so a byte above 127 would compare as negative and fail the
           lower bound instead of the upper one. Either way it becomes '?' --
           an unmappable byte is shown rather than dropped, so a bad string is
           visible instead of silently short.
           비교하기 전에 unsigned char로 캐스트합니다. 이곳의 char는 부호가 있으므로 127을
           넘는 바이트는 음수로 비교되어 위쪽이 아니라 아래쪽 경계에 걸립니다. 어느 쪽이든
           '?'가 됩니다. 대응되지 않는 바이트를 버리지 않고 보여 주므로, 잘못된 문자열이
           조용히 짧아지는 대신 눈에 보입니다. */
        unsigned char c = (unsigned char)s[i];
        if (c < FIRST || c >= FIRST + COUNT) c = '?';

        int g  = c - FIRST;
        int cx = (g % COLS) * CELL, cy = (g / COLS) * CELL;

        /* The UV window is FONT_CW by FONT_CH out of a CELL-sized cell, so it
           takes the glyph plus the transparent gutter to its right and below.
           That gutter is what keeps abutting quads from running their letters
           together.
           UV 창은 CELL 크기의 셀에서 FONT_CW × FONT_CH만큼을 잘라내므로, 글리프와 그
           오른쪽·아래의 투명한 여백을 함께 가져옵니다. 맞닿은 쿼드들의 글자가 서로 붙지
           않게 하는 것이 바로 그 여백입니다. */
        float u0 = cx / (float)ATLAS_W;
        float u1 = (cx + FONT_CW) / (float)ATLAS_W;
        float v0 = cy / (float)ATLAS_H;
        float v1 = (cy + FONT_CH) / (float)ATLAS_H;

        /* y1 is below y0, not above it: the quad grows in +y from its top
           edge, which is upright under the UI's y-down projection.
           y1은 이름이 주는 인상과 달리 아래쪽입니다. 쿼드는 위쪽 가장자리에서 +y 방향으로
           자라며, 이는 UI의 y-down 투영 아래에서 똑바로 선 모양입니다. */
        float x0 = pen, x1 = pen + FONT_CW * size;
        float y0 = y,   y1 = y + FONT_CH * size;

        /* Two triangles sharing the x0y0-x1y1 diagonal, wound so both face the
           +z normal above. Emitted as six independent vertices because MeshBuf
           carries no index buffer.
           x0y0-x1y1 대각선을 공유하는 삼각형 두 개이며, 둘 다 위의 +z 법선을 향하도록
           감았습니다. MeshBuf에는 인덱스 버퍼가 없으므로 독립된 정점 여섯 개로
           내보냅니다. */
        mb_vtx(b, v3f(x0, y0, 0), n, u0, v0);
        mb_vtx(b, v3f(x1, y0, 0), n, u1, v0);
        mb_vtx(b, v3f(x1, y1, 0), n, u1, v1);

        mb_vtx(b, v3f(x0, y0, 0), n, u0, v0);
        mb_vtx(b, v3f(x1, y1, 0), n, u1, v1);
        mb_vtx(b, v3f(x0, y1, 0), n, u0, v1);

        /* The pen lands where this quad ended, so the gutter is inside the
           quad rather than added between quads.
           펜은 이 쿼드가 끝난 자리에 놓입니다. 따라서 여백은 쿼드 사이에 더해지는 것이
           아니라 쿼드 안에 있습니다. */
        pen = x1;
    }

    /* Derived from the pen rather than from what mb_vtx accepted, so a full
       buffer still reports the width the caller asked for and the layout after
       it does not shift. See the warning in font.h.
       mb_vtx가 받아들인 것이 아니라 펜으로부터 유도합니다. 그래서 버퍼가 가득 찼더라도
       호출자가 요청한 너비를 그대로 보고하며 뒤따르는 배치가 어긋나지 않습니다. font.h의
       경고를 참조하십시오. */
    return pen - x;
}
