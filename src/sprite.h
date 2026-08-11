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

/* --- The hand-drawn viewmodel / 손으로 그린 뷰 모델 --------------------------
 *
 * ENGLISH
 * -------
 * A Doom-style weapon sprite, drawn as art rather than extruded from an
 * outline. It REPLACES the 3D view model when it exists, and the 3D one stays
 * as the fallback: `assets/sprites/gun0.png` and up is all it takes to switch,
 * and deleting them switches back. That is deliberately a stronger rule than
 * the monsters follow -- a drawing composites OVER an SDF creature, because a
 * half-drawn bestiary should still show creatures, whereas a gun drawn over a
 * 3D gun would be two guns.
 *
 * The cell is 128x96, which is the size a viewmodel actually occupies and, at
 * this project's art resolution, roughly what Doom's own weapon sprites were.
 *
 * 한국어
 * ------
 * Doom 방식의 무기 스프라이트입니다. 외곽선을 압출하지 않고 아트로 그립니다. 존재하면 3D
 * 뷰 모델을 *대체*하며, 3D 쪽은 폴백으로 남습니다. `assets/sprites/gun0.png` 이후를 넣는
 * 것만으로 전환되고, 지우면 되돌아갑니다. 이는 몬스터가 따르는 것보다 의도적으로 강한
 * 규칙입니다. 그림은 SDF 생물체 *위에* 합성되는데, 절반만 그려진 몬스터 도감도 생물체를
 * 보여 주어야 하기 때문입니다. 반면 3D 총기 위에 그린 총기는 총이 두 자루가 됩니다.
 *
 * 셀은 128x96이며, 뷰 모델이 실제로 차지하는 크기이자 이 프로젝트의 아트 해상도에서 Doom
 * 자신의 무기 스프라이트와 대략 같은 크기입니다.
 */
#define WPN_CW 128          ///< @brief Weapon frame cell width, pixels. / 무기 프레임 셀의 너비 (픽셀).
#define WPN_CH  96          ///< @brief Weapon frame cell height, pixels. / 무기 프레임 셀의 높이 (픽셀).

/**
 * @brief Weapon animation frames, in the order `gun<N>.png` names them.
 *
 * ENGLISH
 * -------
 * Driven by the weapon's own timers rather than a separate animation clock, so
 * the drawing cannot fall out of step with what the gun is actually doing --
 * the same reason the monsters' frames are chosen from ::EState.
 *
 * 한국어
 * ------
 * @brief `gun<N>.png`가 이름 붙이는 순서대로의 무기 애니메이션 프레임입니다.
 *
 * 별도의 애니메이션 시계가 아니라 무기 자신의 타이머가 구동하므로, 그림이 총기가 실제로
 * 하는 일과 어긋날 수 없습니다. 몬스터의 프레임을 ::EState에서 고르는 것과 같은
 * 이유입니다.
 */
enum {
    WPN_IDLE,      /**< At rest. / 대기 상태. */
    WPN_FIRE,      /**< The shot itself, while the muzzle flash lives. / 발사 순간. 총구 화염이 살아 있는 동안. */
    WPN_PUMP0,     /**< Pump drawn back. / 펌프를 당긴 상태. */
    WPN_PUMP1,     /**< Pump returning. / 펌프가 돌아오는 상태. */
    WPN_FRAMES     /**< How many. / 프레임 수. */
};

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

/**
 * @brief Decodes a sprite text into a caller-owned buffer. Tests only.
 *
 * ENGLISH
 * -------
 * @param[in]     text   Sprite text in bake.ps1's format.
 * @param[in,out] rgba   Destination, W*H*4 bytes. NOT cleared -- decoding
 *                       composites, so the caller decides what is underneath.
 * @param[in]     W,H    Destination size in pixels.
 * @param[in]     weapon Non-zero to place sprites in weapon cells, zero for
 *                       monster cells.
 *
 * @note Exists because the shipped decoder can only ever see the baked blob:
 *       there is no file behind DATA_SPRITES, so without this the only way to
 *       exercise the codec is to rebuild with different art and look at the
 *       screen. tools/sprtest.c uses it to check the format against pixels it
 *       computed by hand.
 * @warning Dev builds only, like ::sprite_dump_ppm.
 *
 * 한국어
 * ------
 * @brief 스프라이트 텍스트를 호출자 소유 버퍼로 디코딩합니다. 테스트 전용입니다.
 * @param[in,out] rgba 대상 버퍼(W*H*4 바이트). 비우지 *않습니다*. 디코딩은 합성이므로
 *                     아래에 무엇이 있을지는 호출자가 정합니다.
 * @param[in]     weapon 0이 아니면 무기 셀에, 0이면 몬스터 셀에 배치합니다.
 *
 * @note 배포되는 디코더는 구워진 텍스트만 볼 수 있기 때문에 존재합니다. DATA_SPRITES
 *       뒤에는 파일이 없으므로, 이것이 없으면 코덱을 실행해 볼 유일한 방법이 다른 아트로
 *       다시 빌드해서 화면을 보는 것뿐입니다.
 * @warning ::sprite_dump_ppm과 같이 개발 빌드 전용입니다.
 */
void sprite_decode_text(const char *text, unsigned char *rgba, int W, int H,
                        int weapon);

/**
 * @brief The muzzle a weapon frame recorded, in CELL pixels. Tests only.
 *
 * @param[in]  frame WPN_* frame.
 * @param[out] x,y   Cell coordinates, after centring and bottom-seating.
 * @return Non-zero when that frame recorded one.
 *
 * @note ::weapon_muzzle normalises and flips v for the quad it feeds; this
 *       reports the raw placement so a test can check the seating arithmetic
 *       without also depending on the flip.
 *
 * @brief 무기 프레임이 기록한 총구를 *셀* 픽셀 단위로 반환합니다. 테스트 전용입니다.
 * @note ::weapon_muzzle은 쿼드에 넣기 위해 정규화하고 v를 뒤집습니다. 이 함수는 배치
 *       계산만 검사할 수 있도록 원시 좌표를 보고합니다.
 */
/**
 * @brief One character of the sprite alphabet back to its six bits. Tests only.
 *
 * @param[in] c A character.
 * @return 0..63, or -1 when it is not in the alphabet.
 *
 * @note Exposed so tools/sprtest.c can check this against the string bake.ps1
 *       encodes with. The two must describe the same alphabet in the same
 *       order, one is PowerShell and the other is C, and a mismatch decodes
 *       every drawing in the game to the wrong palette indices -- which looks
 *       like the art was drawn wrong.
 *
 * @brief 스프라이트 알파벳의 한 문자를 6비트 값으로 되돌립니다. 테스트 전용입니다.
 * @note bake.ps1이 인코딩에 쓰는 문자열과 대조할 수 있도록 노출합니다. 둘은 같은 알파벳을
 *       같은 순서로 기술해야 하는데 하나는 PowerShell이고 다른 하나는 C이며, 어긋나면
 *       게임의 모든 그림이 잘못된 팔레트 인덱스로 디코딩됩니다.
 */
int sprite_b64val(char c);

int sprite_weapon_muzzle_px(int frame, int *x, int *y);
#endif

/* --- The hand-drawn viewmodel / 손으로 그린 뷰 모델 --- */

/**
 * @brief Whether any weapon art exists, and therefore whether to draw it.
 *
 * ENGLISH
 * -------
 * @return Non-zero when `assets/sprites/gun<N>.png` supplied at least one frame.
 *
 * @note This is the switch. ::wp_draw_view asks it once per frame and draws the
 *       sprite or the extruded model accordingly, so adding art is dropping in
 *       a file and removing it is deleting one. Nothing else changes and no
 *       flag has to be kept in agreement with the files on disk.
 * @note Cheap after the first call: the answer is settled while the atlas is
 *       built and remembered.
 *
 * 한국어
 * ------
 * @brief 무기 아트가 존재하는지, 따라서 그것을 그릴지 여부입니다.
 * @return `assets/sprites/gun<N>.png`가 최소 한 프레임을 제공했으면 0이 아닙니다.
 *
 * @note 이것이 전환 스위치입니다. ::wp_draw_view가 프레임마다 한 번 묻고 그에 따라
 *       스프라이트 또는 압출 모델을 그립니다. 따라서 아트를 추가하는 것은 파일을 넣는
 *       것이고 제거하는 것은 파일을 지우는 것입니다. 그 외에는 아무것도 바뀌지 않으며,
 *       디스크의 파일과 일치시켜야 하는 플래그도 없습니다.
 * @note 첫 호출 이후로는 저렴합니다. 아틀라스를 생성하는 동안 답이 정해지고 기억됩니다.
 */
int weapon_has_art(void);

/**
 * @brief The weapon atlas: one row, ::WPN_FRAMES columns.
 *
 * ENGLISH
 * -------
 * @return The texture name, or 0 when there is no art.
 * @warning Requires a current GL context. Built on first use.
 *
 * 한국어
 * ------
 * @brief 무기 아틀라스입니다. 한 행에 ::WPN_FRAMES개의 열입니다.
 * @return 텍스처 이름. 아트가 없으면 0입니다.
 * @warning 활성 GL 컨텍스트가 필요합니다. 최초 사용 시 생성됩니다.
 */
GLuint weapon_atlas(void);

/**
 * @brief UV rectangle of one weapon frame within ::weapon_atlas.
 *
 * ENGLISH
 * -------
 * @param[in]  frame          One of the WPN_* frames; clamped.
 * @param[out] u0,v0,u1,v1    The sub-rect.
 *
 * 한국어
 * ------
 * @brief ::weapon_atlas 내에서 무기 프레임 하나의 UV 사각 영역입니다.
 * @param[in]  frame       WPN_* 프레임 중 하나. 범위를 벗어나면 제한됩니다.
 * @param[out] u0,v0,u1,v1 부분 영역.
 */
void weapon_uv(int frame, float *u0, float *v0, float *u1, float *v1);

/**
 * @brief Where a weapon frame's muzzle sits, as a fraction of its cell.
 *
 * @param[in]  frame One of the WPN_* frames; clamped.
 * @param[out] u     0..1 across the cell, left to right.
 * @param[out] v     0..1 up the cell, bottom to top.
 * @return Non-zero when the drawing marked a muzzle with a magenta pixel.
 *
 * @note The marker is part of the DRAWING, so redrawing the gun moves the
 *       flash and the tracers with it. That is the same reason modeledit puts
 *       a draggable muzzle on the 3D model rather than a constant in weapon.c.
 *
 * @brief 무기 프레임의 총구 위치를 셀에 대한 비율로 반환합니다.
 * @return 그림이 마젠타 픽셀로 총구를 표시했으면 0이 아닌 값.
 * @note 표식은 *그림의 일부*이므로 총을 다시 그리면 화염과 예광탄이 함께 움직입니다.
 *       modeledit이 weapon.c의 상수가 아니라 3D 모델에 끌 수 있는 총구를 두는 것과 같은
 *       이유입니다.
 */
int weapon_muzzle(int frame, float *u, float *v);

#endif
