/**
 * @file glsl.h
 * @brief The one line every shader in this project begins with, and why it is
 *        not written out at the two places that need it.
 *
 * ENGLISH
 * -------
 * There are exactly two shader programs here -- the world's, in render.c, and
 * the resolve pass's, in post.c -- and until now each opened with its own
 * literal `#version 330 core`. That is four copies of a decision that has to
 * be the same in all four places, and it is the decision that changes first
 * when a second host appears: WebGL 2 speaks GLSL ES 3.00, not GLSL 3.30, and
 * a `#version` line is not something a driver will meet you halfway on.
 *
 * WHY THE PRECISION LINES ARE PART OF IT. A GLSL ES fragment shader has NO
 * default precision for float. Not "a low one" -- none, and a shader that does
 * not declare one fails to compile with a message about a type it never
 * mentioned. A vertex shader does have a default, and giving it the same
 * declaration anyway is legal, so one prologue serves both stages and there is
 * no second macro to pick between. On desktop GL the two lines would be inert
 * and are simply not emitted.
 *
 * WHAT IS NOT HERE: anything about what a shader DOES. This header is the
 * dialect, not the content. If something in the body of a shader ever needs to
 * differ between hosts, that is a sign the shader is using a feature one host
 * does not have, and the answer is to stop using it -- see the affine varying
 * in render.c, which had exactly that problem and was rewritten to arithmetic
 * that both hosts run rather than split in two.
 *
 * 한국어
 * ------
 * @brief 이 프로젝트의 모든 셰이더가 시작하는 한 줄, 그리고 그것이 필요한 두 곳에 직접 적혀
 *        있지 않은 이유.
 *
 * 셰이더 프로그램은 정확히 둘입니다. render.c의 월드용과 post.c의 해상 패스용이며, 지금까지는
 * 각자 `#version 330 core`를 직접 적고 있었습니다. 네 곳에서 같아야만 하는 판단의 사본이 네
 * 개라는 뜻이고, 두 번째 호스트가 나타날 때 가장 먼저 바뀌는 판단이 바로 그것입니다. WebGL 2가
 * 쓰는 것은 GLSL 3.30이 아니라 GLSL ES 3.00이며, `#version` 줄은 드라이버가 적당히 봐주는
 * 종류의 것이 아닙니다.
 *
 * *정밀도 줄이 왜 여기 같이 있는가.* GLSL ES 프래그먼트 셰이더에는 float의 기본 정밀도가
 * *없습니다*. "낮은 것이 있다"가 아니라 없으며, 선언하지 않은 셰이더는 언급한 적도 없는 타입에
 * 대한 메시지와 함께 컴파일에 실패합니다. 정점 셰이더에는 기본값이 있지만 같은 선언을 주어도
 * 합법이므로, 프롤로그 하나가 두 단계를 모두 맡고 골라야 할 두 번째 매크로가 없습니다.
 * 데스크톱 GL에서는 그 두 줄이 무해하며 애초에 방출되지 않습니다.
 *
 * 여기에 *없는* 것: 셰이더가 무엇을 *하는가*에 관한 것. 이 헤더는 방언이지 내용이 아닙니다.
 * 셰이더 본문의 무언가가 호스트마다 달라져야 한다면, 그것은 한쪽 호스트에 없는 기능을 쓰고
 * 있다는 신호이며 답은 그 기능을 그만 쓰는 것입니다. render.c의 어파인 varying이 정확히 그
 * 문제를 가지고 있었고, 둘로 쪼개는 대신 두 호스트가 모두 실행하는 산술로 다시 썼습니다.
 */
#ifndef GLSL_H
#define GLSL_H

/**
 * @brief The `#version` line, and on ES the precision declarations with it.
 *
 * ENGLISH: Spliced as the FIRST source string of every shader. It must be
 * first -- `#version` has to be the first line of a GLSL unit, ahead of even a
 * comment -- which is why this is a separate part handed to glShaderSource
 * rather than something concatenated onto a body that starts with a comment.
 *
 * 한국어: 모든 셰이더의 *첫* 소스 문자열로 이어 붙입니다. 첫 번째여야 합니다. `#version`은
 * GLSL 단위의 첫 줄이어야 하며 주석보다도 앞서야 합니다. 그래서 이것은 주석으로 시작하는
 * 본문에 이어 붙이는 것이 아니라 glShaderSource에 넘기는 별도의 조각입니다.
 */
#ifdef __EMSCRIPTEN__
#define GLSL_PROLOGUE \
    "#version 300 es\n" \
    "precision highp float;\n" \
    "precision highp int;\n"
#else
#define GLSL_PROLOGUE "#version 330 core\n"
#endif

#endif /* GLSL_H */
