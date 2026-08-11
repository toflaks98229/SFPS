/**
 * @file door.h
 * @brief Moving sectors: doors that rise, sink or slide, and the switches that open them.
 *
 * ENGLISH
 * -------
 * A door is a sector that moves, so this module owns the MOTION and the level
 * owns the shape. See ::DoorDef in level.h for why that split is what makes a
 * moving door cost the collision routines nothing.
 *
 * @note Touches no GL. The whole state machine -- opening, waiting, closing,
 *       refusing without a key, being held open by something standing in it --
 *       is arithmetic over a struct, so tools/doortest.c drives a door through
 *       its full cycle without a window.
 * @note Mutates the Level, which almost nothing else here does. That is the
 *       point: writing the moved heights and points back into the sector is
 *       what makes level_ground, open_at and level_trace see the door without
 *       any of them knowing it exists.
 *
 * 한국어
 * ------
 * 문은 움직이는 섹터이므로, 이 모듈은 *움직임*을 소유하고 레벨은 형상을 소유합니다. 그
 * 분리가 움직이는 문의 충돌 비용을 0으로 만드는 이유는 level.h의 ::DoorDef를 참조하십시오.
 *
 * @note GL을 전혀 건드리지 않습니다. 상태 머신 전체(열림·대기·닫힘·열쇠 없는 거절·안에 선
 *       것에 의한 정지)가 구조체에 대한 산술이므로, tools/doortest.c가 창 없이 문의 전체
 *       주기를 구동합니다.
 * @note 이곳의 거의 모든 것과 달리 Level을 *수정*합니다. 그것이 요점입니다. 옮겨진 높이와
 *       점을 섹터에 되써 주는 것이, level_ground와 open_at과 level_trace가 문의 존재를
 *       모르면서도 문을 보게 만드는 방법입니다.
 */
#ifndef DOOR_H
#define DOOR_H

#include "level.h"

/* --- Tuning / 조정값 --- */

/**
 * @brief Seconds a door stays open before closing itself.
 *
 * Long enough to walk through without hurrying, short enough that a room does
 * not stay permanently connected to the one you just left. A door that never
 * closed would make the switch a one-way announcement rather than a thing you
 * can be caught on the wrong side of.
 *
 * 서두르지 않고 지나갈 만큼 길고, 방금 떠난 방과 영구히 연결된 채로 남지 않을 만큼
 * 짧습니다. 닫히지 않는 문은 스위치를 일방적인 통보로 만들며, 잘못된 쪽에 갇힐 수 있는
 * 대상이 아니게 됩니다.
 */
#define DOOR_OPEN_TIME 3.2f

/** @brief Metres from a door's outline that a touch opens it. / 접촉으로 문이 열리는 외곽선으로부터의 거리 (미터). */
#define DOOR_TOUCH_DIST 1.5f

/** @brief Metres from a switch that counts as touching it. / 스위치를 건드린 것으로 인정되는 거리 (미터). */
#define DOOR_SWITCH_DIST 1.1f

/* --- Lifecycle / 수명 주기 --- */

/**
 * @brief Binds to a level: remembers every door's closed shape and shuts them all.
 *
 * @param[in] l Level whose ::DoorDef list is read.
 *
 * @note Must run after ::level_load and before the first ::door_update. The
 *       closed shape is captured HERE rather than read from the level each
 *       frame, because ::door_update overwrites the sector -- after one frame
 *       of motion the level no longer remembers where the door started.
 *
 * @brief 레벨에 바인딩합니다. 모든 문의 닫힌 형상을 기억하고 전부 닫습니다.
 * @note ::level_load 이후, 첫 ::door_update 이전에 실행되어야 합니다. 닫힌 형상을 매
 *       프레임 레벨에서 읽지 않고 *이곳에서* 포착하는 이유는, ::door_update가 섹터를
 *       덮어쓰기 때문입니다. 한 프레임만 움직여도 레벨은 문이 어디서 출발했는지 더 이상
 *       기억하지 못합니다.
 */
void door_reset(const Level *l);

/**
 * @brief Advances every door, writing the result back into the level.
 *
 * ENGLISH
 * -------
 * @param[in,out] l          Level; the moving sectors are rewritten in place.
 * @param[in]     player_pos Player's eye, for touch triggers.
 * @param[in]     keys       Keys the player holds, a KEY_* mask.
 * @param[in]     dt         Timestep in seconds.
 * @return Non-zero when any door moved this frame, so the caller knows the
 *         level's geometry needs rebuilding.
 *
 * @note Returns "something moved" rather than rebuilding itself, because this
 *       module has no business knowing about meshes. It is the same division
 *       enemy_update makes when it reports damage instead of subtracting it.
 * @note A door will not close on the player. Being crushed by a door you were
 *       standing in is a death with no lesson in it, and the alternative --
 *       waiting -- is what every game since Doom has done.
 *
 * 한국어
 * ------
 * @brief 모든 문을 진행시키고 결과를 레벨에 되씁니다.
 * @return 이번 프레임에 문이 하나라도 움직였으면 0이 아닌 값. 호출자가 레벨 지오메트리를
 *         다시 만들어야 함을 알 수 있습니다.
 *
 * @note 직접 다시 만들지 않고 "무언가 움직였다"를 반환합니다. 이 모듈이 메시에 대해 알
 *       이유가 없기 때문입니다. enemy_update가 피해를 차감하지 않고 보고하는 것과 같은
 *       구분입니다.
 * @note 문은 플레이어 위에서 닫히지 않습니다. 서 있던 문에 눌려 죽는 것은 아무런 교훈이
 *       없는 죽음이며, 대안인 *기다리기*는 Doom 이래 모든 게임이 해 온 것입니다.
 */
int door_update(Level *l, v3 player_pos, int keys, float dt);

/* --- Read-back / 조회 --- */

/**
 * @brief How far door `i` has travelled, 0 closed to 1 open.
 * @return 0..1, or 0 for an index with no door.
 *
 * @brief 문 `i`가 얼마나 이동했는지. 0이면 닫힘, 1이면 열림입니다.
 */
float door_openness(int i);

/**
 * @brief The key a door refused for, if the player has just been turned away.
 *
 * @return A KEY_* mask, or ::KEY_NONE when nothing was refused this frame.
 *
 * @note Reported rather than announced, so the HUD decides how to say it. The
 *       message belongs to whoever owns the screen, and this module owns
 *       neither the font nor the language.
 *
 * @brief 방금 플레이어가 거절당했다면 그 문이 요구한 열쇠입니다.
 * @note 알리지 않고 *보고*하므로 HUD가 표현 방식을 정합니다. 메시지는 화면을 소유한 쪽의
 *       것이며, 이 모듈은 폰트도 언어도 소유하지 않습니다.
 */
int door_refused(void);

#endif
