/**
 * @file gl.h
 * @brief Minimal OpenGL 3.3 core loader. No GLEW, no GLAD, no external deps.
 *
 * ENGLISH
 * -------
 * Only the entry points the game actually uses are resolved -- every unused
 * loader stub is bytes we don't get to spend on content.
 *
 * Bringing up a modern context on Win32 requires two passes, and the order
 * matters: call ::gl_bootstrap first to resolve the WGL extensions through a
 * throwaway window, then ::gl_make_context on the real window's device
 * context. A window's pixel format can only ever be set once, which is why
 * the first pass cannot reuse the window the game will actually draw into.
 *
 * 한국어
 * ------
 * 게임이 실제로 사용하는 진입점만 로드합니다. 사용되지 않는 로더 스텁 하나하나가
 * 콘텐츠에 쓸 수 없게 되는 바이트이기 때문입니다.
 *
 * Win32에서 최신 컨텍스트를 준비하려면 두 단계가 필요하며 순서가 중요합니다.
 * 먼저 ::gl_bootstrap을 호출하여 임시 창을 통해 WGL 확장을 로드한 뒤, 실제
 * 창의 장치 컨텍스트에 대해 ::gl_make_context를 호출합니다. 창의 픽셀 형식은
 * 단 한 번만 설정할 수 있으므로, 첫 단계에서 게임이 실제로 그릴 창을 재사용할
 * 수 없습니다.
 */
#ifndef GL_H
#define GL_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>

/* --- Macros and constants / 매크로 및 상수 --- */

/**
 * @brief X-macro list of every GL entry point the game resolves at runtime.
 *
 * ENGLISH
 * -------
 * @brief X-macro list of every GL entry point the game resolves at runtime.
 *
 * GL 1.1 entry points (glClear, glEnable, glDrawArrays, glGenTextures, ...)
 * are exported straight out of opengl32.dll and link normally, so they are
 * deliberately absent here -- wglGetProcAddress is not even required to
 * return them. Everything below is GL 1.3+ and must be resolved at runtime.
 *
 * @note Expanded twice: once in gl.h to declare each pointer `extern`, and
 *       once in gl.c to define it and again to assign it. Adding a function
 *       here is therefore the only edit needed to make it available.
 *
 * 한국어
 * ------
 * @brief 게임이 런타임에 로드하는 모든 GL 진입점의 X-매크로 목록입니다.
 *
 * GL 1.1 진입점(glClear, glEnable, glDrawArrays, glGenTextures 등)은
 * opengl32.dll에서 직접 내보내지며 정상적으로 링크되므로 여기에서 의도적으로
 * 제외되었습니다. wglGetProcAddress가 이들을 반환할 의무조차 없습니다.
 * 아래 항목은 모두 GL 1.3 이상이며 런타임에 로드해야 합니다.
 *
 * @note 두 번 확장됩니다. gl.h에서는 각 포인터를 `extern`으로 선언하고,
 *       gl.c에서는 이를 정의한 뒤 다시 값을 할당합니다. 따라서 여기에 함수를
 *       추가하는 것만으로 해당 함수를 사용할 수 있게 됩니다.
 */
#define GL_FUNCS \
    X(PFNGLCREATESHADERPROC,           glCreateShader) \
    X(PFNGLSHADERSOURCEPROC,           glShaderSource) \
    X(PFNGLCOMPILESHADERPROC,          glCompileShader) \
    X(PFNGLGETSHADERIVPROC,            glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC,       glGetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC,           glDeleteShader) \
    X(PFNGLCREATEPROGRAMPROC,          glCreateProgram) \
    X(PFNGLATTACHSHADERPROC,           glAttachShader) \
    X(PFNGLLINKPROGRAMPROC,            glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC,           glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC,      glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC,             glUseProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC,     glGetUniformLocation) \
    X(PFNGLUNIFORMMATRIX4FVPROC,       glUniformMatrix4fv) \
    X(PFNGLUNIFORM1IPROC,              glUniform1i) \
    X(PFNGLUNIFORM1FPROC,              glUniform1f) \
    X(PFNGLUNIFORM3FVPROC,             glUniform3fv) \
    X(PFNGLUNIFORM4FVPROC,             glUniform4fv) \
    X(PFNGLGENVERTEXARRAYSPROC,        glGenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC,        glBindVertexArray) \
    X(PFNGLGENBUFFERSPROC,             glGenBuffers) \
    X(PFNGLBINDBUFFERPROC,             glBindBuffer) \
    X(PFNGLBUFFERDATAPROC,             glBufferData) \
    X(PFNGLVERTEXATTRIBPOINTERPROC,    glVertexAttribPointer) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC,glEnableVertexAttribArray) \
    X(PFNGLACTIVETEXTUREPROC,          glActiveTexture) \
    X(PFNGLGENERATEMIPMAPPROC,         glGenerateMipmap) \
    X(PFNGLGENFRAMEBUFFERSPROC,        glGenFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC,        glBindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC,   glFramebufferTexture2D) \
    X(PFNGLGENRENDERBUFFERSPROC,       glGenRenderbuffers) \
    X(PFNGLBINDRENDERBUFFERPROC,       glBindRenderbuffer) \
    X(PFNGLRENDERBUFFERSTORAGEPROC,    glRenderbufferStorage) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC,glFramebufferRenderbuffer) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLDELETEFRAMEBUFFERSPROC,     glDeleteFramebuffers) \
    X(PFNGLDELETERENDERBUFFERSPROC,    glDeleteRenderbuffers) \
    X(PFNGLUNIFORM2FPROC,              glUniform2f) \
    X(PFNGLDELETEVERTEXARRAYSPROC,     glDeleteVertexArrays) \
    X(PFNGLDELETEPROGRAMPROC,          glDeleteProgram)

/* --- Global variable declarations / 전역 변수 선언 --- */

/* One `extern` pointer per resolved entry point. Null until gl_make_context
   succeeds; calling through one before then dereferences a null pointer.
   로드된 진입점마다 하나의 `extern` 포인터가 선언됩니다. gl_make_context가
   성공하기 전까지는 널이며, 그 전에 호출하면 널 포인터를 역참조하게 됩니다. */
#define X(type, name) extern type name;
GL_FUNCS
#undef X

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

#endif
