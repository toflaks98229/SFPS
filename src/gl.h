/**
 * @file gl.h
 * @brief Minimal OpenGL 3.3 core loader. No GLEW, no GLAD, no external deps.
 *
 * ENGLISH
 * -------
 * Only the entry points the game actually uses are resolved -- every unused
 * loader stub is bytes we don't get to spend on content.
 *
 * WHAT IS NOT HERE: creating the context. That is WGL, it is Win32, and it
 * lives in wgl.h -- which this header deliberately does not include. Eleven
 * files include gl.h to draw with, and exactly two need to create a context,
 * so putting both in one header made the other nine pay for the whole Win32
 * API. See the note on the includes below.
 *
 * 한국어
 * ------
 * 게임이 실제로 사용하는 진입점만 로드합니다. 사용되지 않는 로더 스텁 하나하나가
 * 콘텐츠에 쓸 수 없게 되는 바이트이기 때문입니다.
 *
 * 여기에 *없는* 것: 컨텍스트 생성. 그것은 WGL이고 Win32이며 wgl.h에 있습니다. 이 헤더는
 * 그것을 의도적으로 포함하지 않습니다. 열한 개 파일이 그리기 위해 gl.h를 포함하지만
 * 컨텍스트를 만들어야 하는 것은 정확히 둘뿐이므로, 둘을 한 헤더에 두면 나머지 아홉이
 * Win32 API 전체의 비용을 치르게 됩니다. 아래 include에 관한 주석을 참조하십시오.
 */
#ifndef GL_H
#define GL_H

/* mingw's <GL/gl.h> includes <windows.h> ITSELF, and only to get these two
   macros -- the calling convention and the import decoration. It guards that
   include on both already being defined, which is the escape hatch, and
   defining them here is what keeps the entire Win32 API out of every file
   that just wants to draw a triangle. Elsewhere (Mesa, and any GL header on a
   platform that has no windows.h) the definitions below are inert: the header
   defines APIENTRY for itself, and WINGDIAPI is not used at all.

   This is a five-line change with an eleven-file blast radius, and it is the
   whole reason the renderer is now platform-free. Deleting it does not break
   the Windows build -- windows.h simply comes back in through the side door,
   silently, into everything.

   mingw의 <GL/gl.h>는 <windows.h>를 *스스로* 포함하며, 오직 이 두 매크로(호출 규약과
   임포트 장식)를 얻기 위해서입니다. 그 include는 둘 다 이미 정의되어 있는지로 보호되어
   있고 그것이 탈출구이며, 여기서 정의해 두는 것이 삼각형 하나 그리려는 모든 파일에서
   Win32 API 전체를 몰아내는 방법입니다. 다른 곳(Mesa 및 windows.h가 없는 플랫폼의 모든
   GL 헤더)에서는 아래 정의가 무해합니다. 그 헤더는 APIENTRY를 스스로 정의하고
   WINGDIAPI는 아예 사용하지 않기 때문입니다.

   다섯 줄짜리 변경이지만 영향 범위는 열한 개 파일이며, 렌더러가 플랫폼에서 자유로워진
   이유가 바로 이것입니다. 이를 지워도 Windows 빌드는 깨지지 않습니다. windows.h가 옆문으로
   조용히, 모든 것 안으로 되돌아올 뿐입니다. */
#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#ifndef WINGDIAPI
#define WINGDIAPI __declspec(dllimport)
#endif

#include <GL/gl.h>
#include <GL/glext.h>

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
    X(PFNGLBUFFERSUBDATAPROC,          glBufferSubData) \
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

/* The prototypes that used to be here -- gl_bootstrap, gl_make_context,
   gl_set_vsync -- are in wgl.h now. They take HINSTANCE and HDC and HGLRC,
   so declaring them here meant declaring windows.h here, for the benefit of
   two callers out of eleven includers.
   여기에 있던 프로토타입들(gl_bootstrap, gl_make_context, gl_set_vsync)은 이제 wgl.h에
   있습니다. HINSTANCE와 HDC와 HGLRC를 받으므로, 이곳에 선언한다는 것은 곧 이곳에
   windows.h를 선언한다는 뜻이었습니다. 열한 개의 포함자 중 둘을 위해서 말입니다. */

#endif
