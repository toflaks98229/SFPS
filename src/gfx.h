/**
 * @file gfx.h
 * @brief Turning a menu setting into the state the post-process pass runs in.
 *
 * ENGLISH
 * -------
 * The settings menu holds NUMBERS -- a pixel preset, a dither step, whether the
 * scanlines are on. post.h holds the pass those numbers configure. This is the
 * translation between them, and it was inline in main.c because that is where
 * the menu was read and the render target was sized.
 *
 * WHY IT IS NOT A PLATFORM FILE, which is the interesting half. Sizing the
 * offscreen target obviously needs to know how big the window is -- and that
 * is exactly one fact, so it arrives as two `int` parameters rather than as an
 * `HWND` this file would then have to ask. The same move ::Input makes: the
 * platform reads the hardware and hands the result over already in the terms
 * the receiver thinks in, so the receiver stays testable and the window stays
 * main.c's.
 *
 * WHAT IS DELIBERATELY NOT HERE: switching between windowed and borderless.
 * That one is irreducibly a window operation -- it restyles an `HWND` in place,
 * and there is no parameter that makes it otherwise -- so it stayed in main.c,
 * whose header comment already claims the window. Moving it here would have
 * bought a tidier call site and cost the portability of this whole file.
 *
 * @note The integer magnification is picked FIRST and the buffer derived from
 *       it, never the other way round. A buffer sized by rounding the width
 *       lets the magnification differ per axis, which stretches the image.
 *
 * 한국어
 * ------
 * @brief 메뉴 설정 하나를 포스트 프로세스 패스가 돌아갈 상태로 바꾸는 일.
 *
 * 설정 메뉴는 *숫자*를 담습니다. 픽셀 프리셋, 디더 단계, 스캔라인 사용 여부입니다. post.h는
 * 그 숫자들이 설정하는 패스를 담습니다. 이 파일은 그 사이의 번역이며, 메뉴를 읽는 곳과 렌더
 * 타깃 크기를 정하는 곳이 그곳이었기 때문에 main.c 안에 인라인으로 있었습니다.
 *
 * *왜 플랫폼 파일이 아닌가*가 흥미로운 절반입니다. 오프스크린 타깃의 크기를 정하려면 당연히
 * 창이 얼마나 큰지 알아야 합니다. 그런데 그것은 정확히 사실 하나이므로, 이 파일이 물어봐야 할
 * `HWND`가 아니라 `int` 두 개로 도착합니다. ::Input이 하는 것과 같은 동작입니다. 플랫폼이
 * 하드웨어를 읽고 그 결과를 받는 쪽이 생각하는 용어로 이미 바꾸어 건네므로, 받는 쪽은 검사
 * 가능한 상태로 남고 창은 main.c의 것으로 남습니다.
 *
 * *의도적으로 이곳에 없는 것*: 창 모드와 테두리 없는 전체 화면 사이의 전환입니다. 그것은
 * 환원 불가능하게 창에 대한 작업입니다. `HWND`를 제자리에서 다시 꾸미는 일이고 그것을 달리
 * 만들어 줄 매개변수가 없습니다. 그래서 이미 창을 자기 것이라 주장하는 헤더 주석을 가진
 * main.c에 남았습니다. 이곳으로 옮겼다면 호출 지점이 조금 깔끔해지는 대가로 이 파일 전체의
 * 이식성을 잃었을 것입니다.
 *
 * @note 정수 확대 배율을 *먼저* 정하고 버퍼를 그로부터 유도하며, 결코 그 반대가 아닙니다.
 *       너비를 반올림해 크기를 정한 버퍼는 축마다 배율이 달라질 수 있고, 그러면 이미지가
 *       늘어납니다.
 */
#ifndef GFX_H
#define GFX_H

/**
 * @brief (Re)creates the offscreen target to match the window and the preset.
 *
 * ENGLISH
 * -------
 * @param[in] preset  A ::GfxPixelPreset. Out-of-range values fall back to
 *                    ::GFX_PIXEL_NORMAL rather than being refused, because this
 *                    is read from settings that a future build may have written.
 * @param[in] client_w Client area width in pixels. Clamped to at least 1 here,
 *                     so a caller may hand over a raw client rect.
 * @param[in] client_h Client area height in pixels. Clamped to at least 1.
 *
 * @note ::post_init allocates unconditionally, so an existing target has to be
 *       torn down first or its GL objects leak. ::post_shutdown fully resets
 *       the module, which is what makes shutdown-then-init a safe resize.
 * @note Preserves whether the effect was switched off, so changing the pixel
 *       size does not quietly switch the whole pass back on.
 * @warning Requires a current GL context.
 *
 * 한국어
 * ------
 * @brief 창과 프리셋에 맞추어 오프스크린 타깃을 (재)생성합니다.
 * @param[in] preset   ::GfxPixelPreset 값. 범위를 벗어나면 거절하지 않고
 *                     ::GFX_PIXEL_NORMAL로 물러납니다. 이후 빌드가 기록했을 수도 있는
 *                     설정에서 읽어 오기 때문입니다.
 * @param[in] client_w 클라이언트 영역 너비 (픽셀). 이곳에서 최소 1로 보정하므로 호출자는
 *                     클라이언트 사각형 값을 그대로 넘겨도 됩니다.
 * @param[in] client_h 클라이언트 영역 높이 (픽셀). 최소 1로 보정합니다.
 *
 * @note ::post_init은 무조건 할당하므로 기존 타깃을 먼저 해제하지 않으면 GL 객체가
 *       누수됩니다. ::post_shutdown이 모듈을 완전히 초기화하므로 해제 후 재생성이
 *       안전한 크기 변경 방법입니다.
 * @note 효과가 꺼져 있었는지를 보존하므로, 픽셀 크기를 바꾸는 것이 패스 전체를 조용히 다시
 *       켜지 않습니다.
 * @warning 활성 GL 컨텍스트가 필요합니다.
 */
void gfx_apply_pixel_preset(int preset, int client_w, int client_h);

/**
 * @brief Applies every menu setting that needs no signal to change.
 *
 * ENGLISH
 * -------
 * Read straight from the menu every frame, so toggling one is visible on the
 * very next frame and nothing has to notify anything. The two settings that
 * cannot work this way -- the display mode and the pixel size -- reallocate a
 * render target, so they arrive as menu ACTIONS instead.
 *
 * 한국어
 * ------
 * @brief 변경에 신호가 필요 없는 모든 메뉴 설정을 적용합니다.
 *
 * 매 프레임 메뉴에서 직접 읽으므로 전환하면 바로 다음 프레임에 반영되며 무엇도 무엇에게
 * 알릴 필요가 없습니다. 이 방식이 통하지 않는 두 설정(디스플레이 모드와 픽셀 크기)은 렌더
 * 타깃을 재할당하므로 메뉴 *액션*으로 도착합니다.
 */
void gfx_apply_live_settings(void);

#endif /* GFX_H */
