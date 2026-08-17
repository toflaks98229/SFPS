/**
 * @file wgl.h
 * @brief Creating an OpenGL context on Win32. The platform half of gl.h.
 *
 * ENGLISH
 * -------
 * This is the only header in src/ besides the ones main.c owns that includes
 * windows.h, and that is the point of it existing. gl.h used to declare these
 * three functions itself, which meant every file that wanted to draw -- eleven
 * of them, the whole renderer -- pulled in windows.h and <GL/wglext.h> to get
 * at glDrawArrays. Splitting them apart cost one file and moved eleven
 * translation units to the portable side of the line.
 *
 * WHO INCLUDES THIS: main.c, gl.c, and the tools that open a real window
 * (mapview, modelview, mapedit, modeledit, dithershot, and the four suites
 * that test real GL output). Nothing else should, and if something else needs
 * to, that is worth a second look rather than an include.
 *
 * Bringing up a modern context on Win32 requires two passes, and the order
 * matters: call ::gl_bootstrap first to resolve the WGL extensions through a
 * throwaway window, then ::gl_make_context on the real window's device
 * context. A window's pixel format can only ever be set once, which is why
 * the first pass cannot reuse the window the game will actually draw into.
 *
 * 한국어
 * ------
 * main.c가 소유한 것들을 제외하면 src/에서 windows.h를 포함하는 유일한 헤더이며,
 * 그것이 이 파일이 존재하는 이유입니다. 이전에는 gl.h가 이 세 함수를 직접 선언했고,
 * 그 탓에 그리기를 원하는 모든 파일(열한 개, 렌더러 전체)가 glDrawArrays에 닿기 위해
 * windows.h와 <GL/wglext.h>를 끌어와야 했습니다. 둘을 분리한 비용은 파일 하나였고,
 * 그 대가로 열한 개의 번역 단위가 경계의 이식 가능한 쪽으로 옮겨갔습니다.
 *
 * 이것을 포함하는 곳: main.c, gl.c, 그리고 실제 창을 여는 툴들(mapview, modelview,
 * mapedit, modeledit, dithershot, 그리고 실제 GL 출력을 검사하는 네 스위트).
 * 그 밖에는 없어야 하며, 다른 무언가가 필요로 한다면 include를 추가하기보다
 * 한 번 더 들여다볼 일입니다.
 *
 * Win32에서 최신 컨텍스트를 준비하려면 두 단계가 필요하며 순서가 중요합니다.
 * 먼저 ::gl_bootstrap을 호출하여 임시 창을 통해 WGL 확장을 로드한 뒤, 실제
 * 창의 장치 컨텍스트에 대해 ::gl_make_context를 호출합니다. 창의 픽셀 형식은
 * 단 한 번만 설정할 수 있으므로, 첫 단계에서 게임이 실제로 그릴 창을 재사용할
 * 수 없습니다.
 */
#ifndef WGL_H
#define WGL_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* gl.h BEFORE wglext.h, and the order is load-bearing: <GL/wglext.h> is
   written in terms of GLenum, GLint and friends and does not include the
   header that defines them. Swap these two lines and every WGL prototype
   fails on an unknown type.
   wglext.h보다 gl.h가 *먼저*이며 순서가 중요합니다. <GL/wglext.h>는 GLenum, GLint 등을
   써서 작성되었으면서 그것들을 정의하는 헤더를 포함하지 않습니다. 이 두 줄을 바꾸면 모든
   WGL 프로토타입이 알 수 없는 타입에서 실패합니다. */
#include "gl.h"   /* the portable half: GL_FUNCS, and the entry points these resolve */
#include <GL/wglext.h>

/* --- Public function prototypes / 공개 함수 프로토타입 --- */

/**
 * @brief Resolves the WGL extensions needed to request a modern context.
 *
 * ENGLISH
 * -------
 * @brief Resolves the WGL extensions needed to request a modern context.
 * @param[in] inst Module instance used to register the temporary window class.
 * @return 1 when both required WGL extensions were resolved, 0 otherwise.
 * @retval 0 The driver is too old, or the throwaway window could not be
 *           created. The caller should abort rather than proceed to
 *           ::gl_make_context.
 * @warning Must run BEFORE the real window is given its pixel format, because
 *          a window's pixel format can only ever be set once. This is why the
 *          bootstrap uses a throwaway window and context of its own.
 * @note Creates, uses and destroys its own window, device context, GL context
 *       and window class; nothing is left registered or current on return.
 *
 * 한국어
 * ------
 * @brief 최신 컨텍스트를 요청하는 데 필요한 WGL 확장을 로드합니다.
 * @param[in] inst 임시 창 클래스를 등록하는 데 사용할 모듈 인스턴스.
 * @return 필요한 두 WGL 확장이 모두 로드되면 1, 그렇지 않으면 0.
 * @retval 0 드라이버가 너무 오래되었거나 임시 창을 생성하지 못한 경우.
 *           호출자는 ::gl_make_context로 진행하지 말고 중단해야 합니다.
 * @warning 실제 창에 픽셀 형식이 설정되기 전에 반드시 실행해야 합니다. 창의
 *          픽셀 형식은 단 한 번만 설정할 수 있기 때문입니다. 부트스트랩이
 *          자체적인 임시 창과 컨텍스트를 사용하는 이유가 바로 이것입니다.
 * @note 자체 창, 장치 컨텍스트, GL 컨텍스트, 창 클래스를 생성하고 사용한 뒤
 *       파괴합니다. 반환 시점에 등록되거나 활성 상태로 남는 자원은 없습니다.
 */
int  gl_bootstrap(HINSTANCE inst);

/**
 * @brief Creates a 3.3 core profile context and resolves every GL_FUNCS entry point.
 *
 * ENGLISH
 * -------
 * @brief Creates a 3.3 core profile context and resolves every GL_FUNCS entry point.
 * @param[in] dc Device context of the real window. Its pixel format is set
 *               here and cannot be changed afterward.
 * @return The new rendering context, made current on success, or 0 on failure.
 * @retval 0 No suitable pixel format was found, the format could not be set,
 *           context creation failed, or an entry point was missing.
 * @warning ::gl_bootstrap must have returned success first; this function
 *          calls the WGL extensions that pass resolved.
 * @warning On failure the context may already have been created and left
 *          current. The caller is expected to treat this as fatal and exit
 *          rather than attempt recovery.
 * @note The caller owns the returned context and is responsible for
 *       `wglDeleteContext` at shutdown.
 *
 * 한국어
 * ------
 * @brief 3.3 코어 프로파일 컨텍스트를 생성하고 모든 GL_FUNCS 진입점을 로드합니다.
 * @param[in] dc 실제 창의 장치 컨텍스트. 여기에서 픽셀 형식이 설정되며 이후에는
 *               변경할 수 없습니다.
 * @return 새로 생성된 렌더링 컨텍스트. 성공 시 활성 상태가 되며, 실패 시 0입니다.
 * @retval 0 적합한 픽셀 형식을 찾지 못했거나, 형식 설정에 실패했거나, 컨텍스트
 *           생성에 실패했거나, 진입점이 누락된 경우.
 * @warning ::gl_bootstrap이 먼저 성공을 반환해야 합니다. 이 함수는 해당 단계에서
 *          로드된 WGL 확장을 호출합니다.
 * @warning 실패 시 컨텍스트가 이미 생성되어 활성 상태로 남아 있을 수 있습니다.
 *          호출자는 이를 치명적 오류로 간주하고 복구를 시도하기보다 종료해야
 *          합니다.
 * @note 반환된 컨텍스트는 호출자의 소유이며, 종료 시 `wglDeleteContext`를
 *       호출할 책임이 있습니다.
 */
HGLRC gl_make_context(HDC dc);

/**
 * @brief Sets the buffer swap interval.
 *
 * ENGLISH
 * -------
 * @brief Sets the buffer swap interval.
 * @param[in] on 0 to present as fast as possible, 1 to synchronise with the
 *               display's refresh.
 * @note A silent no-op when WGL_EXT_swap_control is unavailable, so vsync is
 *       a preference rather than a requirement and never blocks startup.
 *
 * 한국어
 * ------
 * @brief 버퍼 교체 간격을 설정합니다.
 * @param[in] on 0이면 가능한 한 빠르게 출력하고, 1이면 디스플레이 주사율에
 *               동기화합니다.
 * @note WGL_EXT_swap_control을 사용할 수 없으면 아무 동작도 하지 않습니다.
 *       따라서 수직 동기화는 필수가 아닌 선택 사항이며 시작을 방해하지 않습니다.
 */
void gl_set_vsync(int on);
#endif /* WGL_H */
