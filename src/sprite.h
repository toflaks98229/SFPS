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

/* THE EDGE OF AN IMPORTED SURFACE, in pixels. 128 because that is what Doom's
   wall patches are, and resampling them would be inventing detail the artist
   did not draw. tex.c's own buffer is 256 and repeats this twice, which is a
   whole number of tiles and therefore seamless -- a size that did not divide
   256 would put a visible join down every wall in the game.
   가져온 표면 한 변의 픽셀 수입니다. 128인 이유는 Doom의 벽 패치가 그 크기이고, 다시
   샘플링하면 작가가 그리지 않은 디테일을 지어내게 되기 때문입니다. tex.c의 버퍼는
   256이며 이것을 두 번 반복하는데, 타일 수가 정수이므로 이음매가 없습니다. 256을 나누어
   떨어지지 않는 크기는 게임의 모든 벽에 눈에 보이는 이음매를 남깁니다. */
/**
 * @brief The most colours one drawing's palette may hold.
 *
 * ENGLISH
 * -------
 * WAS SIXTEEN, AND SIXTEEN WAS THE CODEC rather than a choice: the packed form
 * put three 4-bit indices into two characters, so a seventeenth colour had
 * nowhere to go. That was fine for the creatures, which are dithered and drawn
 * at 64x96, and it was quietly throwing away the wall art -- measured, the
 * eight imported walls carry 15, 15, 15, 21, 22, 40, 41 and 51 distinct
 * colours, so five of them were being crushed.
 *
 * 256 because an index then fits a byte and the `q` form spends two characters
 * on one. Measured end to end on the shipped binary, in the order the two
 * changes landed:
 *
 *     357,888   before
 *     361,472   +3,584  median cut fixed (see bake.ps1: the cut could land on
 *                       Count and split a box into itself, so fifteen slots
 *                       were only ever yielding about eight colours)
 *     374,784  +13,312  walls widened to 255 slots
 *
 * Thirteen kilobytes for every colour the artist drew, and the floppy is still
 * three quarters empty.
 *
 * @note A drawing that fits in sixteen is still emitted the old way, so
 *       wall_plain, wall_stone and wall_track keep the narrow `p` encoding.
 *       Their BYTES still moved, and so did every creature and pickup: the
 *       median-cut fix changed which colours a 15-slot palette holds. bake.ps1
 *       chooses the encoding per group; see its note on palette budgets.
 *
 * 한국어
 * ------
 * @brief 하나의 그림 팔레트가 담을 수 있는 최대 색 수입니다.
 *
 * 16이었고, 그 16은 선택이 아니라 *코덱*이었습니다. 압축 형식이 4비트 인덱스 셋을 두 문자에
 * 담았으므로 열일곱 번째 색은 갈 곳이 없었습니다. 디더링되어 64x96으로 그려지는 생물에게는
 * 문제가 없었지만, 벽 아트는 조용히 버려지고 있었습니다. 실측하면 가져온 벽 여덟 장이 각각
 * 15, 15, 15, 21, 22, 40, 41, 51개의 색을 지니므로 그중 다섯 장이 깎이고 있었습니다.
 *
 * 256인 이유는 그러면 인덱스가 한 바이트에 들어가고 `q` 형식이 하나에 두 문자를 쓰기
 * 때문입니다. 배포 바이너리에서 끝에서 끝까지, 두 변경이 들어간 순서대로 실측한 값입니다.
 *
 *     357,888   이전
 *     361,472   +3,584  중앙절단 수정 (bake.ps1 참조. cut이 Count에 놓여 상자가 자기
 *                       자신으로 쪼개질 수 있었고, 그래서 15칸이 실제로는 여덟 색쯤만
 *                       내놓고 있었습니다)
 *     374,784  +13,312  벽을 255칸으로 넓힘
 *
 * 화가가 그린 모든 색에 13킬로바이트이고, 플로피는 여전히 4분의 3이 비어 있습니다.
 *
 * @note 16에 들어가는 그림은 여전히 예전 방식으로 나가므로 wall_plain, wall_stone,
 *       wall_track은 좁은 `p` 인코딩을 유지합니다. 그러나 그것들의 *바이트*는 움직였고,
 *       생물과 아이템도 마찬가지입니다. 중앙절단 수정이 15칸 팔레트가 담는 색을
 *       바꾸었기 때문입니다. bake.ps1이 그룹마다 인코딩을 고릅니다. 그곳의 팔레트 예산
 *       설명을 참조하십시오.
 */
#define SPR_PAL_MAX 256

#define SPR_WALL 128

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
 * THE CELL IS A WINDOW ON DOOM'S SCREEN, not a box the art is centred in.
 *
 * That distinction is the whole of where a viewmodel sits. Doom does not centre
 * its weapons: it stores a per-frame offset and draws the sprite AT it, so the
 * shotgun rests left of centre, the chaingun sits right of where the shotgun
 * does, and the chainsaw's cutting frames run off the right edge of the screen
 * on purpose. Those offsets are the artist placing the weapon. Centring each
 * drawing in a tight cell throws all of it away and puts every weapon in the
 * same place -- which is what this used to do, and why the imported shotgun sat
 * dead centre looking like it belonged to a different game.
 *
 * So the cell spans Doom's full 320-unit screen width and 144 rows of its 3D
 * view, and a frame's position in the cell IS its position there. Nothing
 * in the format changed to allow it: `o <x> <y>` already places a drawing in
 * its cell, and the importer now computes that from the offsets instead of
 * from a centring rule.
 *
 * Costing nothing is what makes it practical. bake.ps1 crops each drawing to
 * its ink, so a cell four times the area of the old one stores the same
 * pixels; only the atlas texture grows, and that is RAM rather than floppy.
 *
 * 192x104 keeps the cell's pixel aspect equal to its screen aspect, which is
 * what makes the 1.2 correction for Doom's non-square pixels fall out of the
 * geometry rather than being applied by hand: 104/144 over 192/320 is 1.204.
 *
 * 한국어
 * ------
 * Doom 방식의 무기 스프라이트입니다. 외곽선을 압출하지 않고 아트로 그립니다. 존재하면 3D
 * 뷰 모델을 *대체*하며, 3D 쪽은 폴백으로 남습니다. 이는 몬스터가 따르는 것보다 의도적으로
 * 강한 규칙입니다. 그림은 SDF 생물체 *위에* 합성되는데, 절반만 그려진 몬스터 도감도
 * 생물체를 보여 주어야 하기 때문입니다. 반면 3D 총기 위에 그린 총기는 총이 두 자루입니다.
 *
 * 셀은 그림을 가운데 넣는 상자가 아니라 *Doom 화면을 들여다보는 창*입니다. 이 구분이 뷰
 * 모델의 위치 전부입니다. Doom은 무기를 가운데 두지 않고 프레임마다 오프셋을 저장해 그
 * 자리에 그립니다. 그래서 샷건은 중앙 왼쪽에, 체인건은 그보다 오른쪽에 놓이고, 전기톱의
 * 절단 프레임은 의도적으로 화면 오른쪽 밖으로 나갑니다. 그 오프셋이 곧 아티스트의 배치
 * 입니다. 좁은 셀에 가운데 맞추면 그것을 전부 버리고 모든 무기를 같은 자리에 놓게 되며,
 * 이전이 그러했고 그래서 이식한 샷건이 정중앙에 어색하게 놓였습니다.
 *
 * 포맷은 이를 위해 바뀐 것이 없습니다. `o <x> <y>`가 이미 셀 안 배치를 담당하며, 임포터가
 * 그 값을 가운데 맞춤 규칙이 아니라 오프셋에서 계산할 뿐입니다. 비용이 들지 않는다는 점이
 * 이를 실용적으로 만듭니다. bake.ps1이 잉크에 맞춰 자르므로 넓이가 네 배인 셀도 같은
 * 픽셀을 저장하며, 커지는 것은 아틀라스 텍스처뿐이고 그것은 플로피가 아니라 RAM입니다.
 */
/* WHICH DOOM SCREEN, and it is not the obvious one.
 *
 * Doom's screen is 320x200, but its 3D VIEW is 168 rows: the status bar takes
 * the bottom 32, and that is the framing the game shipped with. The weapon is
 * still drawn against the full 200 -- BASEYCENTER is a fixed 100 whatever the
 * view height is -- so the bar does not merely hide the bottom of the gun, it
 * changes where the gun sits AND how big it is against what you can see.
 *
 * Matching the 200-row fullscreen view instead put the shotgun at 35.8%..99.8%
 * of the screen: its bottom balanced exactly on the edge with nothing cut off,
 * which reads as a gun perched too high and too small. Doom as shipped puts it
 * at 33.0%..109.2% -- a fifth larger, and planted past the bottom edge.
 *
 * 어느 Doom 화면인가의 문제이며, 뻔한 쪽이 아닙니다. Doom의 화면은 320x200이지만 3D
 * *뷰*는 168행입니다. 상태 표시줄이 아래 32행을 가져가며, 그것이 게임이 배포된 구도
 * 입니다. 무기는 여전히 200행 전체를 기준으로 그려지므로(BASEYCENTER는 뷰 높이와 무관한
 * 고정된 100입니다) 표시줄은 총의 아래쪽을 가리기만 하는 것이 아니라, 총이 놓이는
 * 위치와 보이는 영역 대비 크기까지 바꿉니다.
 *
 * 200행 전체 화면에 맞췄을 때 샷건은 화면의 35.8%..99.8%에 놓였습니다. 아래가 잘리지
 * 않고 가장자리에 정확히 걸터앉아, 너무 높고 작게 놓인 총으로 읽혔습니다. 배포판 Doom은
 * 33.0%..109.2%에 놓습니다. 5분의 1만큼 크고, 아래 가장자리 너머로 박혀 있습니다. */
#define WPN_DOOM_W    320   ///< @brief Doom's screen width, in its own units. / Doom 화면의 너비 (자체 단위).
#define WPN_DOOM_FULL 200   ///< @brief Doom's whole screen height; the psprite reference. / Doom 화면 전체 높이. 뷰 모델의 기준.
#define WPN_DOOM_VIEW 168   ///< @brief The 3D view: the screen less its status bar. / 3D 뷰. 화면에서 상태 표시줄을 뺀 높이.
#define WPN_DOOM_TOP   24   ///< @brief View row the cell's top edge sits on. / 셀 상단이 놓이는 뷰의 행.

#define WPN_CW 192          ///< @brief Weapon cell width, pixels: Doom's whole screen width. / 무기 셀의 너비 (픽셀). Doom 화면 전체 너비.
#define WPN_CH 104          ///< @brief Weapon cell height, pixels: view rows WPN_DOOM_TOP..WPN_DOOM_VIEW. / 무기 셀의 높이 (픽셀).

/**
 * @brief How many drawings one weapon's row can hold.
 *
 * ENGLISH
 * -------
 * A CAP, not a list of moments. It used to be a list -- IDLE, FIRE, PUMP0,
 * PUMP1 -- which works only while every moment needs a drawing of its own and
 * every weapon needs the same moments. Neither is true. A pump passes through
 * the same pose going out and coming back, so naming slots after moments meant
 * storing one pose twice; and Doom gives the chaingun two drawings, the
 * launcher two, the shotgun three and the chainsaw four, so a shared list of
 * moments would pad the short weapons with duplicates to fill their slots.
 *
 * So a slot is just a drawing, WHICH drawing means something different per
 * weapon, and what each one is and when it shows lives beside that weapon's
 * animation cycle in weapon.c. The atlas only has to know how wide a row is.
 *
 * 한국어
 * ------
 * @brief 한 무기의 행이 담을 수 있는 그림의 수, 즉 *상한*입니다.
 *
 * 순간의 목록이 아닙니다. 예전에는 목록이었고(IDLE, FIRE, PUMP0, PUMP1) 그것은 모든
 * 순간이 저마다의 그림을 필요로 하고 모든 무기가 같은 순간을 필요로 할 때에만 통합니다.
 * 둘 다 사실이 아닙니다. 펌프는 나갈 때와 돌아올 때 같은 자세를 지나므로 슬롯을 순간으로
 * 이름 붙이면 한 자세를 두 번 저장하게 되고, Doom은 체인건에 그림 둘, 발사기에 둘,
 * 샷건에 셋, 전기톱에 넷을 주므로 공유된 순간 목록은 짧은 무기의 빈 슬롯을 복제로 채우게
 * 됩니다. 그래서 슬롯은 그저 그림이며, *어느* 그림인지는 무기마다 다르고, 각각이 무엇이며
 * 언제 보이는지는 weapon.c의 해당 무기 애니메이션 주기 옆에 있습니다.
 */
#define WPN_FRAMES 4

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
/**
 * @brief Fills `rgba` with one imported surface, by name.
 *
 * ENGLISH
 * -------
 * @param[in]  name The baked drawing's name, e.g. "wall_brick".
 * @param[out] rgba ::SPR_WALL x ::SPR_WALL x 4 bytes, written in full.
 * @return 1 if a drawing of that name was found, 0 if none was.
 * @note Fetched ON DEMAND by the material that wants it rather than built into
 *       an atlas. There is no atlas of every surface to fill, and making one
 *       would carry every texture in the game for a level that uses three.
 * @note Zeroes the buffer first, so a name that matches nothing leaves a known
 *       result. A missing texture should look like a missing texture rather
 *       than like whatever the caller's allocation happened to hold.
 *
 * 한국어
 * ------
 * @brief 이름으로 가져온 표면 하나를 `rgba`에 채웁니다.
 * @return 해당 이름의 그림을 찾으면 1, 없으면 0입니다.
 * @note 아틀라스에 미리 넣지 않고, 그것을 원하는 재질이 *필요할 때* 가져옵니다. 채워야 할
 *       전체 표면 아틀라스가 없으며, 만든다면 셋만 쓰는 레벨을 위해 게임의 모든 텍스처를
 *       싣게 됩니다.
 * @note 먼저 버퍼를 0으로 채우므로 일치하는 것이 없는 이름도 알려진 결과를 남깁니다.
 */
int sprite_wall(const char *name, unsigned char *rgba);

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

int sprite_weapon_muzzle_px(int type, int frame, int *x, int *y);
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
void weapon_uv(int type, int frame, float *u0, float *v0, float *u1, float *v1);

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
int weapon_muzzle(int type, int frame, float *u, float *v);

#endif
