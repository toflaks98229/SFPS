/**
 * @file sprite.h
 * @brief Procedural monster and pickup sprites, drawn at startup into RGBA atlases.
 *
 * ENGLISH
 * -------
 * Doom stored its monsters as hand-drawn sprite sheets -- megabytes of them.
 * We have code, not artists, so a monster is *drawn* at startup into an RGBA
 * atlas: each part of the creature is a signed-distance field, the parts are
 * unioned, and the union's boundary gives a clean silhouette in the alpha
 * channel plus a dark outline for free. Same trade as the textures and the
 * sounds -- keep the recipe, generate the pixels.
 *
 * @note The alpha channel here is a real silhouette mask (0 outside, 255
 *       inside), which is why sprites need their own shader mode rather than
 *       reusing the world one, where alpha means gloss.
 *
 * 한국어
 * ------
 * Doom은 몬스터를 직접 그린 스프라이트 시트로 저장했으며, 그 용량은 수 메가바이트에
 * 달했습니다. 우리에게는 아티스트가 아닌 코드가 있으므로, 몬스터는 시작 시점에 RGBA
 * 아틀라스로 *그려집니다*. 생물체의 각 부위는 부호 있는 거리장(SDF)이고, 이 부위들을
 * 합집합으로 결합하면 그 경계가 알파 채널에 깔끔한 실루엣을 만들어 내며 어두운
 * 외곽선까지 덤으로 얻습니다. 텍스처나 사운드와 동일한 방식으로, 결과물 대신 레시피를
 * 저장합니다.
 *
 * @note 여기서 알파 채널은 실제 실루엣 마스크입니다(바깥은 0, 안쪽은 255). 이것이
 *       스프라이트가 알파를 광택으로 해석하는 월드 셰이더를 재사용하지 않고 자체
 *       셰이더 모드를 필요로 하는 이유입니다.
 */
#ifndef SPRITE_H
#define SPRITE_H

#include "gl.h"

/* --- Macros and constants / 매크로 및 상수 --- */

#define SPR_CW 64           ///< @brief Monster frame cell width, pixels. / 몬스터 프레임 셀의 너비 (픽셀).
#define SPR_CH 96           ///< @brief Monster frame cell height, pixels. / 몬스터 프레임 셀의 높이 (픽셀).

#define PK_CW 48            ///< @brief Pickup cell width, pixels. / 아이템 셀의 너비 (픽셀).
#define PK_CH 48            ///< @brief Pickup cell height, pixels. / 아이템 셀의 높이 (픽셀).

/* --- Enumerations / 열거형 --- */

/**
 * @brief Animation frames shared by every monster type.
 *
 * ENGLISH
 * -------
 * @note Order matters: enemy.c maps its `EState` to these. Every monster type
 *       shares this set, so one draw path serves them all.
 *
 * 한국어
 * ------
 * 모든 몬스터 종류가 공유하는 애니메이션 프레임입니다.
 * @note 순서가 중요합니다. enemy.c가 자신의 `EState`를 이 값들에 대응시킵니다. 모든
 *       몬스터 종류가 이 집합을 공유하므로, 하나의 그리기 경로가 전부를 처리합니다.
 */
enum {
    SPR_WALK0, SPR_WALK1,   /**< Two-frame walk cycle. / 2프레임 걷기 주기. */
    SPR_ATTACK,             /**< Lunge / maw open. / 돌진 또는 입을 벌린 상태. */
    SPR_HURT,               /**< Flinch. / 피격 경직. */
    SPR_DEAD,               /**< Corpse. / 시체. */
    SPR_FRAMES              /**< Total number of frames. / 프레임의 총 개수. */
};

/* --- Public function prototypes: atlases / 공개 함수 프로토타입: 아틀라스 --- */

/**
 * @brief Returns the monster sprite atlas, building it on first call.
 *
 * ENGLISH
 * -------
 * @return The GL texture name of the atlas.
 * @note The atlas is a grid: one row per monster type (MON_* in enemy.h), one
 *       column per frame. Builds once; later calls return the same texture.
 * @warning Requires a current GL context on the first call. The texture is
 *          module-owned and must not be deleted by the caller.
 *
 * 한국어
 * ------
 * @brief 몬스터 스프라이트 아틀라스를 반환하며, 최초 호출 시 생성합니다.
 * @return 아틀라스의 GL 텍스처 이름.
 * @note 아틀라스는 격자 구조입니다. 몬스터 종류(enemy.h의 MON_*)마다 한 행,
 *       프레임마다 한 열을 차지합니다. 한 번만 생성되며 이후 호출은 동일한 텍스처를
 *       반환합니다.
 * @warning 최초 호출 시 활성 GL 컨텍스트가 필요합니다. 이 텍스처는 모듈이 소유하므로
 *          호출자가 삭제해서는 안 됩니다.
 */
GLuint sprite_atlas(void);

/**
 * @brief Returns the pickup sprite atlas, building it on first call.
 *
 * ENGLISH
 * -------
 * @return The GL texture name of the atlas.
 * @note One cell per PK_* kind (pickup.h).
 * @warning Requires a current GL context on the first call. The texture is
 *          module-owned and must not be deleted by the caller.
 *
 * 한국어
 * ------
 * @brief 아이템 스프라이트 아틀라스를 반환하며, 최초 호출 시 생성합니다.
 * @return 아틀라스의 GL 텍스처 이름.
 * @note PK_* 종류(pickup.h)마다 하나의 셀을 차지합니다.
 * @warning 최초 호출 시 활성 GL 컨텍스트가 필요합니다. 이 텍스처는 모듈이 소유하므로
 *          호출자가 삭제해서는 안 됩니다.
 */
GLuint pickup_atlas(void);

/* --- Public function prototypes: atlas lookup / 공개 함수 프로토타입: 아틀라스 조회 --- */

/**
 * @brief Returns the atlas sub-rectangle of one monster (type, frame).
 *
 * ENGLISH
 * -------
 * @param[in]  type  Monster type index (MON_* in enemy.h).
 * @param[in]  frame Frame index (SPR_* above).
 * @param[out] u0    Receives the left texture coordinate.
 * @param[out] v0    Receives the bottom texture coordinate.
 * @param[out] u1    Receives the right texture coordinate.
 * @param[out] v1    Receives the top texture coordinate.
 * @note v is flipped so a billboard's v=0 (its bottom) maps to the creature's
 *       feet.
 *
 * 한국어
 * ------
 * @brief 특정 몬스터의 (종류, 프레임)에 해당하는 아틀라스 부분 영역을 반환합니다.
 * @param[in]  type  몬스터 종류 인덱스 (enemy.h의 MON_*).
 * @param[in]  frame 프레임 인덱스 (위의 SPR_*).
 * @param[out] u0    좌측 텍스처 좌표를 받습니다.
 * @param[out] v0    하단 텍스처 좌표를 받습니다.
 * @param[out] u1    우측 텍스처 좌표를 받습니다.
 * @param[out] v1    상단 텍스처 좌표를 받습니다.
 * @note v는 반전되어 있어, 빌보드의 v=0(아래쪽)이 생물체의 발에 대응됩니다.
 */
void sprite_uv(int type, int frame, float *u0, float *v0, float *u1, float *v1);

/**
 * @brief Returns the atlas sub-rectangle of one pickup kind.
 *
 * ENGLISH
 * -------
 * @param[in]  kind Pickup kind index (PK_* in pickup.h).
 * @param[out] u0   Receives the left texture coordinate.
 * @param[out] v0   Receives the bottom texture coordinate.
 * @param[out] u1   Receives the right texture coordinate.
 * @param[out] v1   Receives the top texture coordinate.
 *
 * 한국어
 * ------
 * @brief 특정 아이템 종류에 해당하는 아틀라스 부분 영역을 반환합니다.
 * @param[in]  kind 아이템 종류 인덱스 (pickup.h의 PK_*).
 * @param[out] u0   좌측 텍스처 좌표를 받습니다.
 * @param[out] v0   하단 텍스처 좌표를 받습니다.
 * @param[out] u1   우측 텍스처 좌표를 받습니다.
 * @param[out] v1   상단 텍스처 좌표를 받습니다.
 */
void   pickup_uv(int kind, float *u0, float *v0, float *u1, float *v1);

/* --- Public function prototypes: debugging / 공개 함수 프로토타입: 디버깅 --- */

#ifdef HOT_RELOAD
/**
 * @brief Writes the whole atlas to a binary PPM for eyeballing the sprite art.
 *
 * ENGLISH
 * -------
 * @param[in] path Destination file path.
 * @return 1 on success, 0 on failure.
 * @note Composites the silhouette over a checkerboard so the alpha cutout is
 *       visible. No GL -- this is for a headless tool (tools/sprdump.c).
 * @warning Only built in dev/tool builds (HOT_RELOAD); it drags in stdio the
 *          shipped exe otherwise avoids.
 *
 * 한국어
 * ------
 * @brief 스프라이트 아트를 눈으로 확인할 수 있도록 아틀라스 전체를 이진 PPM으로 씁니다.
 * @param[in] path 저장할 파일 경로.
 * @return 성공하면 1, 실패하면 0.
 * @note 알파 컷아웃이 보이도록 실루엣을 체커보드 위에 합성합니다. GL을 사용하지
 *       않으므로 헤드리스 도구(tools/sprdump.c)용입니다.
 * @warning 개발/도구 빌드(HOT_RELOAD)에서만 컴파일됩니다. 배포용 exe가 회피하는
 *          stdio를 끌어들이기 때문입니다.
 */
int sprite_dump_ppm(const char *path);
#endif

#endif
