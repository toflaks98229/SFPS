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
/* THE HOST THAT HAS NOTHING TO LOAD.
 *
 * A browser's GL is not a DLL to be asked for entry points; Emscripten links
 * every ES 3.0 function directly, so <GLES3/gl3.h> declares them as FUNCTIONS.
 * That is the whole conflict, and it is the first thing the compiler said when
 * emcc was pointed at this tree:
 *
 *     error: redefinition of 'glActiveTexture' as different kind of symbol
 *
 * The X-macro below declares `extern PFNGLACTIVETEXTUREPROC glActiveTexture` --
 * a POINTER by that name -- and the two cannot both be true. Neither side is
 * wrong; the loader simply has no work on this host, so it is not declared at
 * all and calls go straight to the linked symbol.
 *
 * The Win32 side keeps the two macro definitions it always had, and the note
 * below them still applies there: mingw's <GL/gl.h> includes <windows.h>
 * itself, and defining these two first is what keeps the entire Win32 API out
 * of eleven files that only want to draw.
 *
 * *로드할 것이 없는 호스트.*
 *
 * 브라우저의 GL은 진입점을 물어볼 DLL이 아닙니다. Emscripten은 모든 ES 3.0 함수를 직접
 * 링크하므로 <GLES3/gl3.h>는 그것들을 *함수*로 선언합니다. 그것이 충돌의 전부이며, emcc를
 * 이 트리에 겨눴을 때 컴파일러가 가장 먼저 한 말입니다.
 *
 *     error: redefinition of 'glActiveTexture' as different kind of symbol
 *
 * 아래의 X-매크로는 `extern PFNGLACTIVETEXTUREPROC glActiveTexture`, 즉 그 이름의 *포인터*를
 * 선언하며 둘이 함께 참일 수는 없습니다. 어느 쪽도 틀리지 않았습니다. 이 호스트에서 로더는
 * 할 일이 없을 뿐이므로 아예 선언하지 않고, 호출은 링크된 심볼로 곧장 갑니다.
 *
 * Win32 쪽은 늘 가지고 있던 매크로 정의 둘을 그대로 두며, 그 아래의 주석도 그곳에서는 여전히
 * 유효합니다. mingw의 <GL/gl.h>는 <windows.h>를 스스로 포함하고, 이 둘을 먼저 정의하는 것이
 * 그리기만 원하는 열한 개 파일에서 Win32 API 전체를 몰아내는 방법입니다. */
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
/* AND THE EXTENSION ENUMS, which ES keeps in a header of their own. tex.c asks
   for anisotropic filtering by name -- GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT -- and
   desktop glext.h has it where gl3.h does not. This is the upstream header that
   does, rather than two hand-copied numbers whose provenance the next reader
   would have to take on trust.
   WHETHER THE EXTENSION IS THERE IS A RUNTIME QUESTION, and tex.c already asks
   it the way it has to be asked: query the maximum, clear the error an
   unsupported enum raises, and leave the parameter alone if nothing came back.
   A browser without EXT_texture_filter_anisotropic therefore degrades to plain
   mipmapping exactly as a desktop driver without it does. What gl_web.c owes
   this is `enableExtensionsByDefault`, so the extension is live on the context
   before the query runs.
   *그리고 확장 열거값들이며*, ES는 그것들을 별도 헤더에 둡니다. tex.c는 비등방성 필터링을
   이름으로 요구하고(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT) 데스크톱 glext.h에는 그것이 있지만
   gl3.h에는 없습니다. 이것은 그것을 가진 상류 헤더이며, 다음 읽는 사람이 출처를 믿고 넘어가야
   할 손으로 옮겨 적은 숫자 둘이 아닙니다.
   *확장이 있는지는 런타임 질문이고*, tex.c는 그것을 물어야 하는 방식으로 이미 묻고 있습니다.
   최댓값을 조회하고, 지원되지 않는 열거값이 일으킨 오류를 지우고, 아무것도 돌아오지 않으면
   파라미터를 건드리지 않습니다. 따라서 EXT_texture_filter_anisotropic이 없는 브라우저는 그것이
   없는 데스크톱 드라이버와 똑같이 단순 밉매핑으로 내려앉습니다. gl_web.c가 이것에 빚지는 것은
   `enableExtensionsByDefault`이며, 조회가 돌기 전에 컨텍스트에 확장이 살아 있게 하는 것입니다. */
#include <GLES2/gl2ext.h>
#else
#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#ifndef WINGDIAPI
#define WINGDIAPI __declspec(dllimport)
#endif

#include <GL/gl.h>
#include <GL/glext.h>
#endif

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
/* Not on a host that links them. ::GL_FUNCS stays DEFINED either way -- it is
   the list of what this project uses, which is worth reading on any host -- but
   expanding it here would declare forty-one pointers over forty-one functions.
   gl.c is the only other place it expands, and gl.c is a declared Win32 file.
   그것들을 링크하는 호스트에서는 하지 않습니다. ::GL_FUNCS는 어느 쪽이든 *정의된 채로*
   남습니다. 이 프로젝트가 무엇을 쓰는지의 목록이며 어느 호스트에서든 읽을 가치가 있기
   때문입니다. 다만 이곳에서 확장하면 함수 마흔한 개 위에 포인터 마흔한 개를 선언하게 됩니다.
   그것이 확장되는 다른 유일한 곳은 gl.c이고, gl.c는 선언된 Win32 파일입니다. */
#ifndef __EMSCRIPTEN__
#define X(type, name) extern type name;
GL_FUNCS
#undef X
#endif

/* The prototypes that used to be here -- gl_bootstrap, gl_make_context,
   gl_set_vsync -- are in wgl.h now. They take HINSTANCE and HDC and HGLRC,
   so declaring them here meant declaring windows.h here, for the benefit of
   two callers out of eleven includers.
   여기에 있던 프로토타입들(gl_bootstrap, gl_make_context, gl_set_vsync)은 이제 wgl.h에
   있습니다. HINSTANCE와 HDC와 HGLRC를 받으므로, 이곳에 선언한다는 것은 곧 이곳에
   windows.h를 선언한다는 뜻이었습니다. 열한 개의 포함자 중 둘을 위해서 말입니다. */

#endif
