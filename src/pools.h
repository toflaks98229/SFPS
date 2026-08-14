/**
 * @file pools.h
 * @brief The fixed-capacity pools a run spawns into, in one struct a caller owns.
 *
 * ENGLISH
 * -------
 * Five modules kept their contents in file-scope arrays: the monsters, the
 * items, the projectiles, the particles and the marks a shot leaves. Each was
 * reached by calling into the module and each was reset by calling into it
 * again, so a ::World could be built twice and there would still be one set of
 * monsters. world.c itself holds no such state -- it is the six modules it
 * calls that do -- which made the whole file look independent while it was not.
 *
 * What that cost is not hypothetical. tools\steptest.c has to call
 * ::proj_reset and its neighbours by hand before every fixture, because the
 * previous case's contents are still there. An editor that wanted to preview a
 * level beside the running game would be previewing INTO the running game. And
 * ::World, whose whole reason for existing is that a caller cannot hold half a
 * game, could only ever hold half of one.
 *
 * WHY ONE STRUCT AND NOT FIVE PARAMETERS. The pools are not independent of
 * each other and pretending otherwise makes the plumbing worse than the
 * problem: ::proj_blast damages monsters, a projectile's detonation spawns
 * particles, a shot leaves a mark and a sound. A function that advances one of
 * these reaches two or three of the others, so a parameter per pool would have
 * ::wp_update taking four and every one of them would have to be added again
 * the next time something reached one more. One parameter is added once.
 *
 * @note These are all POOLS in the same sense: a fixed array, a cap chosen so
 *       a frame never allocates, and oldest-first eviction when it fills. That
 *       shared property is what makes them one struct rather than a bag.
 * @note Not everything global moved here. audio, post, menu and tex keep their
 *       own state and should: they are one output device, one framebuffer, one
 *       open menu and one texture cache per process, and giving a second World
 *       a second sound card would be a worse lie than the one this fixes.
 *
 * 한국어
 * ------
 * @brief 한 플레이가 생성해 넣는 고정 용량 풀들을, 호출자가 소유하는 하나의 구조체에 모읍니다.
 *
 * 다섯 개 모듈이 자기 내용물을 파일 스코프 배열에 담고 있었습니다. 몬스터, 아이템, 발사체,
 * 입자, 그리고 사격이 남긴 자국입니다. 각각은 모듈을 호출해야 닿을 수 있었고 다시 호출해야
 * 초기화되었으므로, ::World를 두 개 만들어도 몬스터는 여전히 한 벌뿐이었습니다. world.c
 * 자신은 그런 상태를 갖지 않습니다. 그것을 가진 것은 world.c가 호출하는 여섯 모듈이며, 그
 * 사실이 이 파일 전체를 독립적으로 *보이게* 만들면서 실제로는 아니게 했습니다.
 *
 * 그 대가는 가정이 아닙니다. tools\steptest.c는 픽스처마다 ::proj_reset과 그 이웃들을 손으로
 * 불러야 합니다. 이전 사례의 내용물이 그대로 남아 있기 때문입니다. 실행 중인 게임 옆에서
 * 레벨을 미리 보려는 에디터는 실행 중인 게임 *안으로* 미리 보게 됩니다. 그리고 호출자가
 * 게임의 절반만 들고 있을 수 없게 하는 것이 존재 이유인 ::World가, 정작 언제나 절반만 들고
 * 있을 수 있었습니다.
 *
 * 왜 다섯 개의 인자가 아니라 하나의 구조체인가. 풀들은 서로 독립적이지 않으며, 독립적인
 * 척하면 배관이 문제보다 나빠집니다. ::proj_blast는 몬스터에 피해를 주고, 발사체의 폭발은
 * 입자를 생성하며, 사격은 자국과 소리를 남깁니다. 이 중 하나를 진행시키는 함수는 다른 둘
 * 셋에 닿으므로, 풀마다 인자를 두면 ::wp_update가 넷을 받게 되고 무언가가 하나 더에 닿을
 * 때마다 그 전부를 다시 추가해야 합니다. 인자 하나는 한 번만 추가됩니다.
 *
 * @note 이들은 모두 같은 의미의 *풀*입니다. 고정 배열, 프레임이 결코 할당하지 않도록 고른
 *       상한, 그리고 가득 찼을 때 가장 오래된 것부터 버리는 축출입니다. 그 공유된 성질이
 *       이것들을 잡동사니가 아니라 하나의 구조체로 만듭니다.
 * @note 전역이던 것이 전부 이곳으로 오지는 않았습니다. audio, post, menu, tex는 자기 상태를
 *       유지하며 그래야 합니다. 그것들은 프로세스당 출력 장치 하나, 프레임버퍼 하나, 열린
 *       메뉴 하나, 텍스처 캐시 하나이며, 두 번째 World에 두 번째 사운드 카드를 주는 것은
 *       이 작업이 고치는 거짓말보다 더 나쁜 거짓말입니다.
 */
#ifndef POOLS_H
#define POOLS_H

#include "proj.h"
#include "pickup.h"
#include "decal.h"
#include "fx.h"
#include "enemy.h"

/**
 * @struct Pools
 * @brief Everything a run spawns and discards, owned by whoever owns the run.
 *
 * @note Zero-initialised is a valid empty state for every member, so a
 *       `Pools p = {0}` needs no further reset. ::World gets one for free from
 *       ::world_init clearing itself.
 *
 * 한국어: 한 플레이가 생성하고 버리는 모든 것이며, 그 플레이를 소유한 쪽이 소유합니다.
 * @note 모든 멤버에 대해 0 초기화가 유효한 빈 상태이므로 `Pools p = {0}`에는 추가 초기화가
 *       필요 없습니다. ::World는 ::world_init이 자신을 비우면서 이것을 공짜로 얻습니다.
 */
struct Pools {
    ProjPool   proj;     /**< Grenades and bolts in flight. / 비행 중인 유탄과 볼트. */
    PickupPool pickup;   /**< Items the level laid out, and which are left. / 레벨이 배치한 아이템과 남아 있는 것들. */
    DecalPool  decal;    /**< The marks and tracers a shot leaves. / 사격이 남긴 자국과 예광탄. */
    FxPool     fx;       /**< Particles in flight. / 날아다니는 입자. */
    EnemyPool  enemy;    /**< Monsters, and the shots they have out. / 몬스터와 그들이 쏘아 둔 것. */
};

#endif
