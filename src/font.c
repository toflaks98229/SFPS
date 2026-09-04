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
#include "font_hangul.h"  /* the 360 johab jamo; generated, see tools/fnt2c.py */
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

/* --- Hangul geometry / 한글 형상 --- */

/**
 * @brief Side of one 둥근모꼴 cell, in pixels.
 *
 * ENGLISH
 * -------
 * Sixteen, which is the whole of the source font: it has no header and no
 * metrics, only 360 squares of this size laid end to end. See
 * docs/LICENSE-Dunggeunmo.txt.
 *
 * 한국어
 * ------
 * @brief 둥근모꼴 셀 하나의 한 변. 픽셀 단위입니다.
 *
 * 16이며, 그것이 원본 글꼴의 전부입니다. 머리글도 치수 정보도 없이 이 크기의 정사각형
 * 360개가 잇달아 있을 뿐입니다. docs/LICENSE-Dunggeunmo.txt를 참조하십시오.
 */
#define KCELL    16

#define KCOUNT   360    /**< @brief Jamo cells: 8*20 초성 + 4*22 중성 + 4*28 종성. / 자모 칸 수. */
#define KCOLS    16     /**< @brief Hangul cells per atlas row. / 아틀라스 한 행의 한글 셀 수. */
#define KROWS    23     /**< @brief Rows needed for ::KCOUNT cells. / ::KCOUNT개의 칸에 필요한 행 수. */

/**
 * @brief Top edge of the Hangul region in the atlas, in pixels.
 *
 * ENGLISH
 * -------
 * The ASCII grid ends at `ROWS * CELL` = 48; this starts at 64 rather than
 * abutting it, so both regions begin on a power-of-two boundary and neither
 * one's cell arithmetic has to know the other exists. The sixteen blank rows
 * between them cost nothing that is not already paid by rounding the atlas up.
 *
 * 한국어
 * ------
 * @brief 아틀라스에서 한글 영역의 위쪽 가장자리. 픽셀 단위입니다.
 *
 * ASCII 격자는 `ROWS * CELL` = 48에서 끝나지만, 이 영역은 거기에 맞붙지 않고 64에서
 * 시작합니다. 그래야 두 영역 모두 2의 거듭제곱 경계에서 시작하고, 어느 쪽의 셀 계산도
 * 다른 쪽의 존재를 알 필요가 없습니다. 사이의 빈 16행은 아틀라스를 올림한 대가 외에 따로
 * 드는 비용이 없습니다.
 */
#define KY       64

#define ATLAS_W  (KCOLS * KCELL) /**< @brief Atlas width in pixels. / 아틀라스 너비 (픽셀). */
#define ATLAS_H  512             /**< @brief Atlas height in pixels; `KY + KROWS * KCELL` rounded up to a power of two. / 아틀라스 높이 (픽셀). `KY + KROWS * KCELL`을 2의 거듭제곱으로 올린 값입니다. */

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

/* --- Hangul composition / 한글 조합 --- */

/*
 * WHAT THE 360 GLYPHS ARE. A syllable is not stored; its three parts are, and
 * each part is drawn several times over so that one can be picked to suit the
 * company it keeps. ㄱ before ㅏ is tall and narrow, ㄱ above ㅗ is short and
 * wide, and ㄱ in 각 is shorter still because a 받침 wants the bottom of the
 * square. Eight ㄱs, four ㅏs, four ㄱ-as-받침: 8*20 + 4*22 + 4*28 = 360 cells,
 * 11,520 bytes, and every one of the 11,172 syllables falls out of them.
 *
 * That is the same bargain the rest of this project makes -- textures, sounds
 * and models all store the recipe rather than the result -- except that here
 * it was struck in 1990 by someone drawing for a DOS terminal, and we are only
 * reading the recipe back out.
 *
 * WHY THE TABLES BELOW ARE MEASURED AND NOT CITED. The 8x4x4 scheme is common
 * property, but which 벌 goes with which vowel is a property of the font, and
 * 둥근모꼴 ships no documentation of its own. These three tables were derived
 * from the bitmaps -- by pairing every jamo with every other and keeping the
 * combinations whose ink does not collide -- and then checked by rendering.
 *
 * 360개의 글리프가 무엇인가. 음절은 저장되지 않습니다. 저장되는 것은 세 부분이며, 각
 * 부분은 곁에 오는 것에 맞춰 고를 수 있도록 여러 벌로 그려져 있습니다. ㅏ 앞의 ㄱ은 높고
 * 좁고, ㅗ 위의 ㄱ은 낮고 넓으며, 각의 ㄱ은 받침이 네모의 아래를 원하기에 더 낮습니다.
 * ㄱ 여덟 벌, ㅏ 네 벌, 받침 ㄱ 네 벌로 8*20 + 4*22 + 4*28 = 360칸, 11,520바이트이고,
 * 11,172개 음절이 모두 여기서 나옵니다.
 *
 * 아래 표를 인용하지 않고 측정한 이유. 8x4x4 방식 자체는 공유 재산이지만 어느 벌이 어느
 * 모음과 짝하는지는 글꼴마다의 성질이며, 둥근모꼴은 자체 문서를 함께 배포하지 않습니다.
 * 이 세 표는 비트맵에서 유도한 뒤 실제로 그려서 확인한 것입니다.
 */

#define KCHO     0      /**< @brief First 초성 cell. / 첫 초성 칸. */
#define KJUNG    160    /**< @brief First 중성 cell, past 8 sets of 20. / 첫 중성 칸. 20개짜리 8벌 다음입니다. */
#define KJONG    248    /**< @brief First 종성 cell, past 4 sets of 22. / 첫 종성 칸. 22개짜리 4벌 다음입니다. */

/**
 * @brief Which 초성 set to draw, by 중성 and whether a 종성 follows.
 *
 * ENGLISH
 * -------
 * Five shapes when nothing sits underneath and three when something does, not
 * four and four: with a 받침 the square is short enough that ㅗ and ㅜ stop
 * needing to be told apart, and the set that would have separated them is
 * spent on the no-받침 side instead.
 *
 * 한국어
 * ------
 * @brief 어떤 초성 벌을 그릴지. 중성과 종성의 유무로 정합니다.
 *
 * 아래에 아무것도 없을 때 다섯 벌, 있을 때 세 벌입니다. 넷과 넷이 아닙니다. 받침이 있으면
 * 네모가 충분히 낮아져 ㅗ와 ㅜ를 구별할 필요가 사라지고, 그 둘을 갈랐을 벌이 받침 없는
 * 쪽으로 돌아갑니다.
 */
static const unsigned char CHO_SET[2][21] = {
    /*        ㅏ ㅐ ㅑ ㅒ ㅓ ㅔ ㅕ ㅖ ㅗ ㅘ ㅙ ㅚ ㅛ ㅜ ㅝ ㅞ ㅟ ㅠ ㅡ ㅢ ㅣ */
    /* 없음 */ { 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 3, 1, 2, 4, 4, 4, 2, 1, 3, 0 },
    /* 있음 */ { 5, 5, 5, 5, 5, 5, 5, 5, 6, 7, 7, 7, 6, 6, 7, 7, 7, 6, 6, 7, 5 },
};

/**
 * @brief Which 종성 set to draw, by 중성.
 *
 * ENGLISH
 * -------
 * Width, not height: a 받침 always sits on the same rows, and what changes is
 * how far it may spread under the vowel above it.
 *
 * 한국어
 * ------
 * @brief 어떤 종성 벌을 그릴지. 중성으로 정합니다.
 *
 * 높이가 아니라 너비입니다. 받침은 언제나 같은 행에 앉으며, 달라지는 것은 위의 모음 아래로
 * 얼마나 퍼질 수 있는가입니다.
 */
static const unsigned char JONG_SET[21] = {
    /* ㅏ ㅐ ㅑ ㅒ ㅓ ㅔ ㅕ ㅖ ㅗ ㅘ ㅙ ㅚ ㅛ ㅜ ㅝ ㅞ ㅟ ㅠ ㅡ ㅢ ㅣ */
        0, 2, 0, 2, 1, 2, 1, 2, 3, 0, 2, 1, 3, 3, 1, 2, 1, 3, 3, 1, 1
};

/**
 * @brief Decodes one UTF-8 sequence, advancing `*i` past it.
 *
 * ENGLISH
 * -------
 * @param[in]     s NUL-terminated string.
 * @param[in,out] i Index to read at; left one past the sequence consumed.
 * @return The code point, or `'?'` for any byte that does not begin a
 *         well-formed sequence.
 *
 * @note Only what this font can draw is decoded properly -- one, two and three
 *       byte forms. A four-byte lead is consumed as one bad byte rather than
 *       four, which keeps a truncated string from swallowing the character
 *       after it.
 * @note Always advances by at least one, so a caller looping on this cannot
 *       hang on malformed input.
 *
 * 한국어
 * ------
 * @brief UTF-8 시퀀스 하나를 해독하고 `*i`를 그 뒤로 옮깁니다.
 *
 * @param[in]     s NUL 종료 문자열.
 * @param[in,out] i 읽을 위치. 소비한 시퀀스의 바로 뒤에 남습니다.
 * @return 코드 포인트. 올바른 시퀀스를 시작하지 않는 바이트에 대해서는 `'?'`입니다.
 *
 * @note 이 폰트가 그릴 수 있는 것만 제대로 해독합니다. 1·2·3바이트 형식입니다. 4바이트
 *       선두 바이트는 넷이 아니라 하나의 잘못된 바이트로 소비하며, 그래야 잘린 문자열이
 *       그 뒤의 문자를 삼키지 않습니다.
 * @note 언제나 최소 하나는 전진하므로, 이 함수로 도는 호출자가 잘못된 입력에 멈춰 서는
 *       일은 없습니다.
 */
static unsigned utf8_next(const char *s, int *i) {
    unsigned char c = (unsigned char)s[*i];

    /* A continuation byte is only meaningful after its lead. Reaching one here
       means the lead was missing or malformed, so it is consumed alone.
       이어지는 바이트는 자기 선두 바이트 뒤에서만 뜻이 있습니다. 이곳에 닿았다는 것은
       선두가 없거나 잘못되었다는 뜻이므로 홀로 소비합니다. */
    if (c < 0x80) { (*i)++; return c; }

    int n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 0;
    if (!n) { (*i)++; return '?'; }

    /* Check the whole sequence is present and well formed before consuming any
       of it: a lead byte followed by the wrong thing must not eat that thing.
       하나라도 소비하기 전에 시퀀스 전체가 있고 형식이 맞는지 확인합니다. 선두 바이트
       뒤에 엉뚱한 것이 왔다면 그것을 먹어서는 안 됩니다. */
    for (int k = 1; k < n; k++)
        if (((unsigned char)s[*i + k] & 0xC0) != 0x80) { (*i)++; return '?'; }

    unsigned cp = c & (n == 2 ? 0x1Fu : 0x0Fu);
    for (int k = 1; k < n; k++) cp = (cp << 6) | ((unsigned char)s[*i + k] & 0x3F);
    *i += n;
    return cp;
}

/**
 * @brief Splits a precomposed syllable into the cells that draw it.
 *
 * ENGLISH
 * -------
 * @param[in]  cp   Code point in U+AC00..U+D7A3. Not checked.
 * @param[out] cell Receives two or three cell indices.
 * @return How many were written: three with a 종성, two without.
 *
 * @note The 중성 set is the one rule here that reads the 초성 rather than the
 *       중성: ㄱ and ㅋ are the only initials whose stroke comes down into the
 *       vowel's space, so they get a vowel with a shortened stem. Five vowels
 *       actually differ -- ㅗ ㅘ ㅙ ㅚ ㅛ -- and the other sixteen pairs are
 *       identical bitmaps kept for the sake of the arithmetic.
 *
 * 한국어
 * ------
 * @brief 미리 조합된 음절을 그것을 그리는 칸들로 나눕니다.
 *
 * @param[in]  cp   U+AC00..U+D7A3 범위의 코드 포인트. 검사하지 않습니다.
 * @param[out] cell 칸 번호 두세 개를 받습니다.
 * @return 기록한 개수. 종성이 있으면 셋, 없으면 둘입니다.
 *
 * @note 이곳에서 중성 벌만이 중성이 아니라 초성을 보고 정해지는 규칙입니다. ㄱ과 ㅋ만이
 *       모음의 자리로 획을 내리긋는 초성이므로, 그 둘에는 줄기를 줄인 모음이 갑니다. 실제로
 *       달라지는 모음은 ㅗ ㅘ ㅙ ㅚ ㅛ 다섯이고, 나머지 열여섯 쌍은 계산을 위해 같은
 *       비트맵을 그대로 둔 것입니다.
 */
static int hangul_cells(unsigned cp, int cell[3]) {
    unsigned s = cp - 0xAC00u;
    int cho = (int)(s / 588u), jung = (int)((s / 28u) % 21u), jong = (int)(s % 28u);
    int bat = jong != 0;

    cell[0] = KCHO  + CHO_SET[bat][jung] * 20 + cho + 1;
    cell[1] = KJUNG + (bat * 2 + (cho == 0 || cho == 15)) * 22 + jung + 1;
    if (!bat) return 2;

    cell[2] = KJONG + JONG_SET[jung] * 28 + jong;
    return 3;
}

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

    /* The jamo, into their own grid lower down the same texture. Stored by row
       rather than by column -- 16 pixels is two whole bytes, so a row wastes
       nothing, which is the argument that put the ASCII font on its side and
       leaves this one upright.
       자모를 같은 텍스처의 아래쪽에 있는 자기 격자에 넣습니다. 열이 아니라 행으로
       저장되어 있습니다. 16픽셀은 온전한 2바이트이므로 행이 버리는 비트가 없으며, ASCII
       폰트를 옆으로 눕혔던 바로 그 논리가 이 폰트는 바로 세워 둡니다. */
    for (int i = 0; i < KCOUNT; i++) {
        int cx = (i % KCOLS) * KCELL, cy = KY + (i / KCOLS) * KCELL;

        for (int gy = 0; gy < KCELL; gy++) {
            /* Two bytes per row, the left half first, bit 7 leftmost -- the
               opposite end of the byte from the ASCII table's bit 0 at the
               top, because one is a row and the other is a column.
               행마다 2바이트이고 왼쪽 절반이 먼저이며 비트 7이 가장 왼쪽입니다. ASCII
               표에서 비트 0이 맨 위인 것과 반대쪽 끝인데, 한쪽은 행이고 다른 쪽은 열이기
               때문입니다. */
            unsigned row = ((unsigned)HANGUL[i * 32 + gy * 2] << 8)
                         |  (unsigned)HANGUL[i * 32 + gy * 2 + 1];
            if (!row) continue;

            for (int gx = 0; gx < KCELL; gx++) {
                if (!(row & (0x8000u >> gx))) continue;

                unsigned char *p = &px[((cy + gy) * ATLAS_W + cx + gx) * 4];
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
    /* Two pitches now, not one, so this counts characters rather than bytes
       and asks each what it costs. Still no atlas and no GL context: the
       advance is a property of the code point, not of anything baked.
       이제 두 가지 폭이 있으므로 바이트가 아니라 문자를 세고, 각 문자에 폭을 묻습니다.
       여전히 아틀라스도 GL 컨텍스트도 필요 없습니다. 전진 폭은 코드 포인트의 성질이지
       구워진 무엇의 성질이 아니기 때문입니다. */
    float w = 0;
    for (int i = 0; s[i]; ) {
        unsigned cp = utf8_next(s, &i);
        w += (cp >= 0xAC00u && cp <= 0xD7A3u ? FONT_KW : FONT_CW) * size;
    }
    return w;
}

/**
 * @brief Appends one textured quad covering an atlas rectangle.
 *
 * ENGLISH
 * -------
 * Pulled out of ::font_text because a syllable needs two or three quads in the
 * same place and a letter needs one, and the only thing that differs between
 * them is which rectangle of the atlas is sampled.
 *
 * @note Emits six independent vertices; ::MeshBuf carries no index buffer.
 *
 * 한국어
 * ------
 * @brief 아틀라스의 사각형 하나를 덮는 텍스처 쿼드를 덧붙입니다.
 *
 * ::font_text에서 떼어냈습니다. 음절은 같은 자리에 쿼드가 두세 개 필요하고 글자는 하나가
 * 필요한데, 둘 사이에 다른 것이라고는 아틀라스의 어느 사각형을 표본으로 삼는가뿐이기
 * 때문입니다.
 *
 * @note 독립된 정점 여섯 개를 생성합니다. ::MeshBuf에는 인덱스 버퍼가 없습니다.
 */
static void emit_quad(MeshBuf *b, v3 n, float x0, float y0, float x1, float y1,
                      int cx, int cy, int cw, int ch) {
    float u0 = cx / (float)ATLAS_W, u1 = (cx + cw) / (float)ATLAS_W;
    float v0 = cy / (float)ATLAS_H, v1 = (cy + ch) / (float)ATLAS_H;

    /* Two triangles sharing the x0y0-x1y1 diagonal, wound so both face n.
       x0y0-x1y1 대각선을 공유하는 삼각형 두 개이며, 둘 다 n을 향하도록 감았습니다. */
    mb_vtx(b, v3f(x0, y0, 0), n, u0, v0);
    mb_vtx(b, v3f(x1, y0, 0), n, u1, v0);
    mb_vtx(b, v3f(x1, y1, 0), n, u1, v1);

    mb_vtx(b, v3f(x0, y0, 0), n, u0, v0);
    mb_vtx(b, v3f(x1, y1, 0), n, u1, v1);
    mb_vtx(b, v3f(x0, y1, 0), n, u0, v1);
}

float font_text(MeshBuf *b, float x, float y, float size, const char *s) {
    float pen = x;

    /* One normal for every vertex of every glyph. Text is a flat billboard in
       the caller's space, so the value is constant and hoisted out of the loop
       rather than rebuilt per character.
       모든 글리프의 모든 정점이 쓰는 법선 하나입니다. 텍스트는 호출자 공간에서 평평한
       빌보드이므로 값이 일정하며, 문자마다 다시 만들지 않고 루프 밖으로 끌어냈습니다. */
    v3 n = v3f(0, 0, 1);

    for (int i = 0; s[i]; ) {
        unsigned cp = utf8_next(s, &i);

        if (cp >= 0xAC00u && cp <= 0xD7A3u) {
            /* A syllable is two or three quads in one place rather than one
               quad of its own. Nothing is composited into a scratch bitmap and
               nothing is cached: the parts are already in the atlas, they are
               white on transparent, and laying them over each other is what
               the blend the caller has set up does anyway. That is the whole
               reason 11,520 bytes can stand in for 11,172 syllables.
               음절은 자기 몫의 쿼드 하나가 아니라 한자리에 겹친 쿼드 두세 개입니다. 임시
               비트맵으로 합성하지도, 결과를 캐시하지도 않습니다. 부분들은 이미 아틀라스에
               있고, 투명한 바탕에 흰색이며, 서로 겹쳐 놓는 일은 호출자가 이미 설정해 둔
               블렌드가 하는 일이기 때문입니다. 11,520바이트가 11,172개 음절을 대신할 수
               있는 이유가 바로 이것입니다. */
            int cell[3], parts = hangul_cells(cp, cell);
            float x1 = pen + FONT_KW * size, y1 = y + FONT_CH * size;

            for (int k = 0; k < parts; k++)
                emit_quad(b, n, pen, y, x1, y1,
                          (cell[k] % KCOLS) * KCELL,
                          KY + (cell[k] / KCOLS) * KCELL, KCELL, KCELL);

            pen = x1;
            continue;
        }

        /* Anything else is ASCII or it is nothing. An unmappable code point is
           drawn as '?' rather than skipped, so a bad string is visible instead
           of silently short -- the same bargain the byte version made, moved
           up to the code point now that a byte is no longer a character.
           그 밖의 것은 ASCII이거나 아무것도 아닙니다. 대응되지 않는 코드 포인트는 건너뛰지
           않고 '?'로 그립니다. 잘못된 문자열이 조용히 짧아지는 대신 눈에 보이게 하려는
           것으로, 바이트 판이 하던 것과 같은 절충을 바이트가 더는 문자가 아니게 된 지금
           코드 포인트 수준으로 옮긴 것입니다. */
        if (cp < FIRST || cp >= FIRST + COUNT) cp = '?';

        int g = (int)cp - FIRST;
        float x1 = pen + FONT_CW * size, y1 = y + FONT_CH * size;

        /* The UV window is FONT_CW by FONT_CH out of a CELL-sized cell, so it
           takes the glyph plus the transparent gutter to its right and below.
           That gutter is what keeps abutting quads from running their letters
           together. The Hangul cells above have no such margin -- the DOS font
           they come from filled its square, and a syllable is wide enough that
           the next one does not crowd it.
           UV 창은 CELL 크기의 셀에서 FONT_CW × FONT_CH만큼을 잘라내므로, 글리프와 그
           오른쪽·아래의 투명한 여백을 함께 가져옵니다. 맞닿은 쿼드들의 글자가 서로 붙지
           않게 하는 것이 바로 그 여백입니다. 위의 한글 칸에는 그런 여백이 없습니다. 그
           도스 글꼴은 네모를 가득 채워 그렸고, 음절은 다음 음절이 비집고 들지 않을 만큼
           넓습니다. */
        emit_quad(b, n, pen, y, x1, y1,
                  (g % COLS) * CELL, (g / COLS) * CELL, FONT_CW, FONT_CH);

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
