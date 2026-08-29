/**
 * @file font.h
 * @brief A 5x7 bitmap font baked to a texture atlas, and the quads that draw it.
 *
 * ENGLISH
 * -------
 * The map editor has to show material names, coordinates, sector lists and
 * tool state, and none of that fits in a title bar. The game needs the same
 * thing for an ammo counter. One font serves both.
 *
 * Ninety-six glyphs at 5x7, one byte per column, is 480 bytes of source data.
 * That is baked into a texture atlas once at startup and every character is
 * then one quad -- the same trade the materials and the sounds make, storing
 * the recipe rather than the result. A .ttf would cost more in the binary than
 * the renderer does.
 *
 * @note Every call here needs a current GL context, ::font_width excepted.
 * @note This module owns exactly one GL texture and never frees it; it lives
 *       for the process. See ::font_init.
 *
 * 한국어
 * ------
 * 맵 에디터는 재질 이름, 좌표, 섹터 목록, 도구 상태를 표시해야 하며 그중 어느 것도 타이틀
 * 바에 담기지 않습니다. 게임 역시 탄약 카운터를 위해 같은 것이 필요합니다. 폰트 하나가 둘
 * 다를 감당합니다.
 *
 * 5x7 크기의 글리프 96개를 열당 1바이트로 담으면 원본 데이터가 480바이트입니다. 이것을
 * 시작 시 한 번 텍스처 아틀라스로 구우면 이후 각 문자는 쿼드 하나가 됩니다. 결과 대신
 * 레시피를 저장하는, 재질과 사운드가 택한 것과 동일한 절충입니다. .ttf 하나면 렌더러보다
 * 많은 바이너리 용량을 차지했을 것입니다.
 *
 * @note ::font_width를 제외한 이곳의 모든 호출은 현재 GL 컨텍스트를 필요로 합니다.
 * @note 이 모듈은 GL 텍스처를 정확히 하나 소유하며 결코 해제하지 않습니다. 프로세스와
 *       수명을 같이합니다. ::font_init을 참조하십시오.
 */
#ifndef FONT_H
#define FONT_H

#include "render.h"

/* --- Cell metrics / 셀 치수 --- */

/**
 * @brief How far the pen advances per character, in glyph pixels.
 *
 * ENGLISH
 * -------
 * Six, not the glyph's five: the extra column is the gutter between letters,
 * and it exists in the atlas as transparent pixels rather than being skipped
 * by the layout. That is what lets a run of quads abut edge to edge and still
 * read as spaced text.
 *
 * @note Also the width of the UV window ::font_text samples, so changing this
 *       changes what is sampled as well as where the pen lands.
 *
 * 한국어
 * ------
 * @brief 문자 하나당 펜이 전진하는 폭. 글리프 픽셀 단위입니다.
 *
 * 글리프의 5가 아니라 6입니다. 여분의 한 열은 글자 사이의 간격이며, 레이아웃이 건너뛰는
 * 것이 아니라 아틀라스 안에 투명한 픽셀로 존재합니다. 그래서 쿼드들이 가장자리를 맞대고
 * 이어져도 간격이 있는 텍스트로 읽힙니다.
 *
 * @note ::font_text가 표본으로 삼는 UV 창의 너비이기도 합니다. 따라서 이 값을 바꾸면 펜이
 *       놓이는 위치뿐 아니라 표본으로 삼는 영역도 바뀝니다.
 */
#define FONT_CW 6

/**
 * @brief Line height, in glyph pixels.
 *
 * ENGLISH
 * -------
 * Eight against the glyph's seven, for the same reason ::FONT_CW is six: the
 * spare row is the gap between lines. It is also the full atlas cell height,
 * so a quad drawn this tall samples exactly one cell vertically.
 *
 * 한국어
 * ------
 * @brief 줄 높이. 글리프 픽셀 단위입니다.
 *
 * 글리프의 7에 대해 8인 이유는 ::FONT_CW가 6인 것과 같습니다. 남는 한 행이 줄 사이의
 * 간격입니다. 아틀라스 셀의 전체 높이이기도 하므로, 이 높이로 그린 쿼드는 세로로 정확히
 * 셀 하나를 표본으로 삼습니다.
 */
#define FONT_CH 8

/* --- Lifecycle / 수명 주기 --- */

/**
 * @brief Rasterises the glyph table into a GL texture atlas.
 *
 * ENGLISH
 * -------
 * @note Idempotent. A second call returns immediately, which is what lets the
 *       game and a tool that both draw text each call it without either having
 *       to know the other did.
 * @note Requires a current GL context. Call it after the context exists and
 *       before the first ::font_text.
 * @note On allocation failure it returns having built nothing; ::font_texture
 *       then reports 0 and text simply does not appear. Nothing here aborts.
 *
 * 한국어
 * ------
 * @brief 글리프 표를 GL 텍스처 아틀라스로 래스터화합니다.
 *
 * @note 멱등입니다. 두 번째 호출은 즉시 반환하므로, 텍스트를 그리는 게임과 도구가 서로
 *       상대가 호출했는지 알 필요 없이 각자 호출할 수 있습니다.
 * @note 현재 GL 컨텍스트가 필요합니다. 컨텍스트가 생긴 뒤, 첫 ::font_text 전에
 *       호출하십시오.
 * @note 할당에 실패하면 아무것도 만들지 않은 채 반환합니다. 그러면 ::font_texture가 0을
 *       보고하고 텍스트는 그저 나타나지 않습니다. 이곳에서 중단되는 것은 없습니다.
 */
void font_init(void);

/**
 * @brief Returns the atlas texture to bind before drawing the quads.
 *
 * ENGLISH
 * -------
 * @return The GL texture name, or 0 if ::font_init has not run or its
 *         allocation failed.
 *
 * @note The texture is owned by this module and outlives every caller. Do not
 *       delete it.
 *
 * 한국어
 * ------
 * @brief 쿼드를 그리기 전에 바인딩할 아틀라스 텍스처를 반환합니다.
 *
 * @return GL 텍스처 이름. ::font_init이 실행되지 않았거나 그 할당이 실패했으면 0입니다.
 *
 * @note 텍스처는 이 모듈이 소유하며 모든 호출자보다 오래 살아남습니다. 삭제하지
 *       마십시오.
 */
GLuint font_texture(void);

/* --- Measurement and emission / 측정과 생성 --- */

/**
 * @brief Computes the width a string will occupy without emitting geometry.
 *
 * ENGLISH
 * -------
 * @param[in] size Size of one glyph pixel, in the caller's units.
 * @param[in] s    NUL-terminated string to measure. Must not be NULL.
 * @return Width in the caller's units, equal to what ::font_text would return
 *         for the same arguments.
 *
 * @note Needs no GL context and no atlas, so a layout pass may run before
 *       ::font_init. Every character advances by ::FONT_CW whichever glyph it
 *       is -- the font is fixed-pitch, and an unmappable character still
 *       occupies its cell.
 *
 * 한국어
 * ------
 * @brief 지오메트리를 생성하지 않고 문자열이 차지할 너비를 계산합니다.
 *
 * @param[in] size 글리프 픽셀 하나의 크기. 호출자의 단위를 따릅니다.
 * @param[in] s    측정할 NUL 종료 문자열. NULL이면 안 됩니다.
 * @return 호출자의 단위로 나타낸 너비. 같은 인자에 대해 ::font_text가 반환하는 값과
 *         같습니다.
 *
 * @note GL 컨텍스트도 아틀라스도 필요하지 않으므로 ::font_init 이전에 레이아웃 패스를
 *       수행할 수 있습니다. 어떤 글리프인지와 무관하게 모든 문자가 ::FONT_CW만큼
 *       전진합니다. 이 폰트는 고정폭이며, 대응되지 않는 문자도 자기 셀을 차지합니다.
 */
float font_width(float size, const char *s);

/**
 * @brief Appends the quads for `s` to `b`.
 *
 * ENGLISH
 * -------
 * @param[in,out] b    Buffer the quads are appended to. Must not be NULL.
 * @param[in]     x    Left edge of the first glyph.
 * @param[in]     y    TOP edge of the line.
 * @param[in]     size Size of one glyph pixel, in the caller's units.
 * @param[in]     s    NUL-terminated string to draw. Must not be NULL.
 * @return Width consumed, so a caller can lay out what follows without a
 *         second pass. Equal to ::font_width for the same arguments.
 *
 * @note Emits into whatever space the caller's MVP defines; this function
 *       knows nothing about screens. Text grows DOWNWARD in y, which is what
 *       makes it upright under the y-down projection the UI uses.
 * @note A line is `FONT_CH * size` tall.
 * @note Characters outside the printable ASCII range are drawn as `?` rather
 *       than skipped, so a bad byte is visible instead of silently shortening
 *       the line.
 * @note Appends only -- it does not bind the texture, set a shader or upload
 *       anything. Bind ::font_texture and flush `b` yourself.
 * @warning ::mb_vtx drops vertices once `b` is full and raises
 *          ::DIAG_VERTEX_BUF. The return value is derived from the string
 *          rather than from what was written, so it still reports the full
 *          width when the tail was dropped.
 *
 * 한국어
 * ------
 * @brief `s`에 해당하는 쿼드들을 `b`에 덧붙입니다.
 *
 * @param[in,out] b    쿼드를 덧붙일 버퍼. NULL이면 안 됩니다.
 * @param[in]     x    첫 글리프의 왼쪽 가장자리.
 * @param[in]     y    줄의 *위쪽* 가장자리.
 * @param[in]     size 글리프 픽셀 하나의 크기. 호출자의 단위를 따릅니다.
 * @param[in]     s    그릴 NUL 종료 문자열. NULL이면 안 됩니다.
 * @return 소비한 너비. 호출자가 두 번째 패스 없이 뒤따르는 것을 배치할 수 있습니다. 같은
 *         인자에 대한 ::font_width와 같습니다.
 *
 * @note 호출자의 MVP가 정의하는 공간에 생성합니다. 이 함수는 화면에 대해 아무것도 알지
 *       못합니다. 텍스트는 y의 *아래* 방향으로 자라며, 그래서 UI가 쓰는 y-down 투영
 *       아래에서 똑바로 서게 됩니다.
 * @note 한 줄의 높이는 `FONT_CH * size`입니다.
 * @note 출력 가능한 ASCII 범위를 벗어난 문자는 건너뛰지 않고 `?`로 그립니다. 그래야 잘못된
 *       바이트가 줄을 조용히 짧게 만드는 대신 눈에 보입니다.
 * @note 덧붙이기만 합니다. 텍스처를 바인딩하거나 셰이더를 설정하거나 무엇을 업로드하지
 *       않습니다. ::font_texture 바인딩과 `b`의 플러시는 직접 하십시오.
 * @warning `b`가 가득 차면 ::mb_vtx가 정점을 버리고 ::DIAG_VERTEX_BUF를 올립니다. 반환값은
 *          기록된 것이 아니라 문자열로부터 유도하므로, 뒷부분이 버려졌더라도 여전히 전체
 *          너비를 보고합니다.
 */
float font_text(MeshBuf *b, float x, float y, float size, const char *s);

#endif
