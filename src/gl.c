/**
 * @file gl.c
 * @brief Implements the two-pass WGL bring-up and resolves the GL entry points.
 *
 * ENGLISH
 * -------
 * Requesting an OpenGL 3.3 core context on Win32 is circular: the functions
 * that create a modern context are themselves GL extensions, and extensions
 * can only be resolved while some context is already current. The way out is
 * to create a legacy context on a disposable window, resolve the two WGL
 * extensions through it, then tear the whole thing down before touching the
 * window the game will actually use. See ::gl_bootstrap.
 *
 * 한국어
 * ------
 * Win32에서 OpenGL 3.3 코어 컨텍스트를 요청하는 과정은 순환적입니다. 최신
 * 컨텍스트를 생성하는 함수 자체가 GL 확장이며, 확장은 이미 어떤 컨텍스트가
 * 활성화된 상태에서만 로드할 수 있기 때문입니다. 해결책은 일회용 창에 레거시
 * 컨텍스트를 만들고 이를 통해 두 개의 WGL 확장을 로드한 뒤, 게임이 실제로 사용할
 * 창에 손대기 전에 모든 것을 정리하는 것입니다. ::gl_bootstrap을 참조하십시오.
 */

#include "gl.h"

/* --- Global variable definitions / 전역 변수 정의 --- */

/* Storage for the pointers gl.h declares extern. Assigned by
   gl_make_context; null before that.
   gl.h가 extern으로 선언한 포인터들의 실제 저장 공간입니다. gl_make_context가
   값을 할당하며, 그 전까지는 널입니다. */
#define X(type, name) type name;
GL_FUNCS
#undef X

/* --- Static variable definitions / 정적 변수 정의 --- */

/** @brief Chooses a pixel format by attribute list. Resolved by ::gl_bootstrap. / 속성 목록으로 픽셀 형식을 선택합니다. ::gl_bootstrap이 로드합니다. */
static PFNWGLCHOOSEPIXELFORMATARBPROC    wglChoosePixelFormatARB;
/** @brief Creates a versioned, profiled context. Resolved by ::gl_bootstrap. / 버전과 프로파일이 지정된 컨텍스트를 생성합니다. ::gl_bootstrap이 로드합니다. */
static PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;
/** @brief Sets the swap interval. Optional: may remain null. / 교체 간격을 설정합니다. 선택 사항이므로 널로 남을 수 있습니다. */
static PFNWGLSWAPINTERVALEXTPROC         wglSwapIntervalEXT;

/* --- Static function prototypes / 정적 함수 프로토타입 --- */

static void *gl_proc(const char *name);

/* --- Public function definitions / 공개 함수 정의 --- */

int gl_bootstrap(HINSTANCE inst) {
    /* A window's pixel format is write-once, so the legacy context we need in
       order to ask for a modern one has to live on a window we then throw
       away. This is the standard WGL chicken-and-egg dance. */
    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = inst;
    wc.lpszClassName = "qgl_boot";
    if (!RegisterClassA(&wc)) return 0;

    HWND wnd = CreateWindowExA(0, "qgl_boot", "", 0, 0, 0, 1, 1,
                               0, 0, inst, 0);
    if (!wnd) return 0;

    HDC dc = GetDC(wnd);
    /* A deliberately unremarkable format: this context exists only to make
       extension resolution legal, and never renders anything.
       의도적으로 평범한 형식입니다. 이 컨텍스트는 확장 로드를 가능하게 하려는
       목적으로만 존재하며 아무것도 렌더링하지 않습니다. */
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;

    int pf = ChoosePixelFormat(dc, &pfd);
    HGLRC rc = 0;
    /* Short-circuit chain: each step is a precondition for the next, and the
       assignment to `rc` inside the condition keeps the handle available for
       cleanup regardless of where the chain stops.
       단락 평가 연쇄입니다. 각 단계는 다음 단계의 전제 조건이며, 조건문 안에서
       `rc`에 대입함으로써 연쇄가 어디서 멈추든 정리 작업에 핸들을 사용할 수
       있게 합니다. */
    if (pf && SetPixelFormat(dc, pf, &pfd) && (rc = wglCreateContext(dc)) &&
        wglMakeCurrent(dc, rc)) {
        wglChoosePixelFormatARB    = (PFNWGLCHOOSEPIXELFORMATARBPROC)   gl_proc("wglChoosePixelFormatARB");
        wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)gl_proc("wglCreateContextAttribsARB");
        wglSwapIntervalEXT         = (PFNWGLSWAPINTERVALEXTPROC)        gl_proc("wglSwapIntervalEXT");
    }

    /* Unconditional teardown: the resolved pointers stay valid for the
       process, so nothing here needs the temporary context to survive.
       무조건적인 정리입니다. 로드된 포인터는 프로세스 전체에서 유효하게
       유지되므로, 임시 컨텍스트가 살아남을 필요는 없습니다. */
    wglMakeCurrent(0, 0);
    if (rc) wglDeleteContext(rc);
    ReleaseDC(wnd, dc);
    DestroyWindow(wnd);
    UnregisterClassA("qgl_boot", inst);

    /* Swap control is optional and deliberately excluded from this test.
       교체 간격 제어는 선택 사항이므로 의도적으로 이 검사에서 제외됩니다. */
    return wglChoosePixelFormatARB && wglCreateContextAttribsARB;
}

HGLRC gl_make_context(HDC dc) {
    const int fmt_attribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
        WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB,     32,
        WGL_DEPTH_BITS_ARB,     24,
        WGL_STENCIL_BITS_ARB,   8,
        0
    };
    int pf; UINT n;
    /* Ask for a single best match. `n` reports how many were returned, and a
       successful call that matched nothing still leaves `pf` unset.
       가장 적합한 형식 하나를 요청합니다. `n`은 반환된 개수를 알려 주며, 호출이
       성공했더라도 일치하는 것이 없으면 `pf`는 설정되지 않은 상태로 남습니다. */
    if (!wglChoosePixelFormatARB(dc, fmt_attribs, 0, 1, &pf, &n) || !n) return 0;

    /* SetPixelFormat still requires a descriptor, so recover one from the
       format the ARB path just chose.
       SetPixelFormat은 여전히 서술자를 요구하므로, 방금 ARB 경로가 선택한
       형식으로부터 서술자를 복원합니다. */
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    DescribePixelFormat(dc, pf, sizeof(pfd), &pfd);
    if (!SetPixelFormat(dc, pf, &pfd)) return 0;

    const int ctx_attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };
    HGLRC rc = wglCreateContextAttribsARB(dc, 0, ctx_attribs);
    if (!rc || !wglMakeCurrent(dc, rc)) return 0;

    /* Resolve every entry point up front and fail on the first missing one.
       Discovering a null pointer mid-frame would be far harder to diagnose
       than refusing to start.
       모든 진입점을 미리 로드하고 누락된 첫 항목에서 실패 처리합니다. 프레임
       도중에 널 포인터를 발견하는 것은 아예 시작을 거부하는 것보다 훨씬
       진단하기 어렵습니다. */
#define X(type, name) name = (type)gl_proc(#name); if (!name) return 0;
    GL_FUNCS
#undef X

    return rc;
}

void gl_set_vsync(int on) {
    /* Absent extension means no vsync control, which is not an error.
       확장이 없으면 수직 동기화를 제어할 수 없으며, 이는 오류가 아닙니다. */
    if (wglSwapIntervalEXT) wglSwapIntervalEXT(on);
}

/* --- Static helper function definitions / 정적 헬퍼 함수 정의 --- */

/**
 * @brief Resolves one GL entry point, working around inconsistent driver failure values.
 *
 * ENGLISH
 * -------
 * @brief Resolves one GL entry point, working around inconsistent driver failure values.
 * @param[in] name Null-terminated entry point name.
 * @return The function pointer, or NULL when the entry point is unavailable.
 * @note `wglGetProcAddress` is specified to return NULL on failure, but some
 *       drivers return 1, 2, 3 or -1 instead. All of those are treated as
 *       failure here and retried against opengl32.dll directly, which is
 *       where GL 1.1 entry points live.
 * @warning Requires a current GL context; calling it without one resolves
 *          nothing.
 *
 * 한국어
 * ------
 * @brief GL 진입점 하나를 로드하며, 드라이버마다 다른 실패 반환값을 함께 처리합니다.
 * @param[in] name 널로 끝나는 진입점 이름.
 * @return 함수 포인터. 진입점을 사용할 수 없으면 NULL입니다.
 * @note `wglGetProcAddress`는 실패 시 NULL을 반환하도록 규정되어 있으나, 일부
 *       드라이버는 대신 1, 2, 3 또는 -1을 반환합니다. 여기서는 이 모든 값을
 *       실패로 간주하고 GL 1.1 진입점이 존재하는 opengl32.dll에 직접 재시도합니다.
 * @warning 활성화된 GL 컨텍스트가 필요합니다. 컨텍스트 없이 호출하면 아무것도
 *          로드되지 않습니다.
 */
/* Some drivers signal "not supported" with 1/2/3/-1 rather than NULL. */
static void *gl_proc(const char *name) {
    void *p = (void *)wglGetProcAddress(name);
    if (p == 0 || p == (void *)1 || p == (void *)2 || p == (void *)3 ||
        p == (void *)-1) {
        /* Loaded once and cached for the process lifetime; the module handle
           is intentionally never freed.
           프로세스 수명 동안 한 번만 로드하여 캐시합니다. 모듈 핸들은 의도적으로
           해제하지 않습니다. */
        static HMODULE lib;
        if (!lib) lib = LoadLibraryA("opengl32.dll");
        p = (void *)GetProcAddress(lib, name);
    }
    return p;
}
