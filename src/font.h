/**
 * @file font.h
 * @brief 화면에 텍스트를 렌더링하는 기능을 제공합니다.
 *
 * 맵 에디터는 재질 이름, 좌표, 섹터 목록, 도구 상태 등을 표시해야 하며,
 * 이 모든 것은 타이틀 바에 담을 수 없습니다. 게임 또한 탄약 카운터 등을 위해
 * 이 기능이 필요합니다.
 *
 * 5x7 크기의 96개 글리프가 있으며, 각 열은 1바이트로 표현됩니다. 전체 글꼴은 480바이트입니다.
 * 시작 시 텍스처 아틀라스로 구워지며 각 문자는 하나의 쿼드로 렌더링됩니다.
 * 이는 재질이나 사운드와 마찬가지로, 결과물 대신 레시피를 저장하는 방식입니다.
 */
#ifndef FONT_H
#define FONT_H

#include "render.h"

/* --- 매크로 및 상수 --- */

#define FONT_CW 6      /**< @brief 문자 당 전진 폭 (글리프 픽셀 단위). */
#define FONT_CH 8      /**< @brief 줄 높이 (글리프 픽셀 단위). */

/* --- 함수 --- */

/**
 * @brief 폰트 글리프에서 텍스처 아틀라스를 빌드합니다.
 *
 * 여러 번 호출해도 안전하며, 이후의 호출은 아무 작업도 수행하지 않습니다.
 */
void font_init(void);

/**
 * @brief 생성된 폰트 텍스처 아틀라스의 GLuint 핸들을 반환합니다.
 * @return 폰트 텍스처 핸들.
 */
GLuint font_texture(void);

/**
 * @brief 주어진 문자열 `s`에 대한 쿼드들을 `b`에 추가합니다.
 *
 * 쿼드는 호출자의 MVP(Model-View-Projection) 행렬이 정의하는 공간에 그려집니다.
 * (x, y)는 첫 글리프의 좌상단 위치이며, `size`는 글리프 픽셀 하나의 크기입니다.
 * 한 줄의 높이는 FONT_CH * size 입니다.
 *
 * 텍스트는 y좌표 아래 방향으로 그려지며, 이는 화면 좌표계가 y-down 프로젝션일 때
 * 올바르게 표시되도록 합니다.
 * @param b 정점 데이터를 추가할 MeshBuf 포인터.
 * @param x 텍스트 시작 x 좌표.
 * @param y 텍스트 시작 y 좌표 (상단).
 * @param size 글리프 픽셀당 크기.
 * @param s 렌더링할 문자열.
 * @return 렌더링된 문자열이 차지한 너비를 반환하여, 호출자가 추가적인 레이아웃 작업을 할 수 있도록 합니다.
 */
float font_text(MeshBuf *b, float x, float y, float size, const char *s);

/**
 * @brief 실제로 지오메트리를 생성하지 않고 문자열이 차지할 너비를 계산합니다.
 * @param size 글리프 픽셀당 크기.
 * @param s 너비를 계산할 문자열.
 * @return 계산된 너비.
 */
float font_width(float size, const char *s);

#endif
