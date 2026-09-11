/**
 * @file webgl.h
 * @brief Creating a GL context in a browser. The platform half of gl.h, the
 *        way wgl.h is on Windows.
 *
 * ENGLISH
 * -------
 * wgl.h exists so that eleven files which only want to draw do not each pull
 * in windows.h to reach glDrawArrays. This is the same split for the same
 * reason, and the reason survives the change of host: gl.h is what a renderer
 * includes, and creating a context is not a renderer's business.
 *
 * IT IS ONE FUNCTION WHERE WGL NEEDS THREE, and the difference is not
 * tidiness. Win32 has to bring a context up in two passes because the calls
 * that make a modern context are themselves extensions, which can only be
 * resolved while some context is already current -- so gl.c builds a throwaway
 * window, resolves through it, tears it down, and only then touches the real
 * one. A browser has no such circle. It is asked for a version and either
 * gives it or does not.
 *
 * WHO INCLUDES THIS: main_web.c, and nothing else. If something else needs to,
 * that is worth a second look rather than an include -- wgl.h's own rule.
 *
 * 한국어
 * ------
 * @brief 브라우저에서 GL 컨텍스트를 만드는 일. Windows에서 wgl.h가 그러하듯, gl.h의 플랫폼
 *        절반입니다.
 *
 * wgl.h는 그리기만 원하는 열한 개 파일이 glDrawArrays에 닿으려고 저마다 windows.h를 끌어오지
 * 않게 하려고 존재합니다. 이것은 같은 이유의 같은 분리이며, 그 이유는 호스트가 바뀌어도
 * 살아남습니다. gl.h는 렌더러가 포함하는 것이고, 컨텍스트를 만드는 일은 렌더러의 일이
 * 아닙니다.
 *
 * *WGL이 셋을 필요로 하는 곳에서 이것은 하나이며*, 그 차이는 단정함의 문제가 아닙니다. Win32는
 * 컨텍스트를 두 번에 걸쳐 세워야 합니다. 최신 컨텍스트를 만드는 호출 자체가 확장이고, 확장은
 * 이미 어떤 컨텍스트가 활성인 동안에만 로드되기 때문입니다. 그래서 gl.c는 일회용 창을 만들고,
 * 그것을 통해 로드하고, 헐고, 그러고 나서야 진짜 창에 손댑니다. 브라우저에는 그런 순환이
 * 없습니다. 버전을 요구받고 주거나 주지 않을 뿐입니다.
 *
 * *누가 이것을 포함하는가:* main_web.c이며 그 외에는 없습니다. 다른 무언가가 필요로 한다면
 * 그것은 include가 아니라 한 번 더 들여다볼 일입니다. wgl.h 자신의 규칙입니다.
 */
#ifndef WEBGL_H
#define WEBGL_H

/**
 * @brief Creates a WebGL 2 context on a canvas and makes it current.
 *
 * ENGLISH
 * -------
 * @param[in] canvas CSS selector for the canvas element, e.g. "#canvas".
 * @return Non-zero when a context is current and the renderer may be started,
 *         0 when the browser would not give one.
 * @note WEBGL 2 OR NOTHING. There is no fallback to WebGL 1 and asking for one
 *       would be worse than failing: ES 2.0 has no vertex array objects, no
 *       `textureLod` and no integer bit operations, and this renderer's
 *       full-screen triangle and auto-exposure use all three. A context that
 *       came back as WebGL 1 would fail at the first shader compile instead,
 *       which is the same outcome reported further from its cause.
 * @note Resolves no entry points, because there are none to resolve -- see the
 *       note at the top of gl.h. A browser links them.
 *
 * 한국어
 * ------
 * @brief 캔버스에 WebGL 2 컨텍스트를 만들고 활성화합니다.
 * @param[in] canvas 캔버스 요소의 CSS 선택자. 예: "#canvas".
 * @return 컨텍스트가 활성화되어 렌더러를 시작해도 되면 0이 아닌 값, 브라우저가 주지 않으면 0.
 * @note *WebGL 2가 아니면 아무것도 아닙니다.* WebGL 1로 내려가는 폴백은 없으며, 그것을
 *       요구하는 편이 실패보다 나쁩니다. ES 2.0에는 정점 배열 객체도, `textureLod`도, 정수 비트
 *       연산도 없는데 이 렌더러의 전체 화면 삼각형과 자동 노출이 그 셋을 모두 씁니다. WebGL 1로
 *       돌아온 컨텍스트는 대신 첫 셰이더 컴파일에서 실패하며, 그것은 같은 결과를 원인에서 더 먼
 *       곳에서 보고하는 일입니다.
 * @note 진입점을 로드하지 않습니다. 로드할 것이 없기 때문입니다. gl.h 맨 위의 주석을
 *       보십시오. 브라우저가 그것들을 링크합니다.
 */
int glweb_make_context(const char *canvas);

#endif /* WEBGL_H */
