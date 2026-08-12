/**
 * @file world.h
 * @brief The simulated world, and the order one frame advances it in.
 *
 * ENGLISH
 * -------
 * Everything a frame does to the game that does not need a window. The level,
 * the player, the weapon and the run are one struct so that a caller cannot
 * hold half of them, and ::world_step is the whole per-frame update in one
 * function so that its ORDER is a thing a test can reach.
 *
 * That order used to live in the body of `WinMain`, and this file exists
 * because of what that cost. main.c's own header comment declared the order
 * "load-bearing rather than incidental" -- the rope constraint before the move,
 * the audio listener before anything that plays, death noticed in exactly one
 * place -- and then nothing checked any of it, because the only way to run it
 * was to open a window and play. tools\steptest.c now does, and it can only do
 * that because this module names no GL function, no Win32 call and no menu.
 *
 * What is deliberately NOT here:
 *
 *  - **Drawing.** The renderer pulls out of this struct (see scene.h); nothing
 *    here calls into it. ::World::geometry_dirty is how the one exception --
 *    a door that moved the sectors the level mesh was built from -- gets told
 *    to the platform without this file learning what a Scene is.
 *  - **The menu.** ::Input::paused arrives as a plain flag rather than a call
 *    to menu_is_open(), so a test can freeze the world without operating a UI.
 *  - **The cursor, the window, and the keyboard.** ::Input is already in
 *    metres-and-radians terms: whoever fills it in owns the hardware.
 *
 * 한국어
 * ------
 * @brief 시뮬레이션되는 월드, 그리고 한 프레임이 그것을 진행시키는 순서입니다.
 *
 * 창이 필요하지 않은, 한 프레임이 게임에 하는 모든 일입니다. 레벨·플레이어·무기·플레이가
 * 하나의 구조체인 이유는 호출자가 그중 절반만 들고 있을 수 없게 하기 위함이며,
 * ::world_step이 프레임별 갱신 전체를 담은 하나의 함수인 이유는 그 *순서*를 테스트가
 * 도달할 수 있는 것으로 만들기 위함입니다.
 *
 * 그 순서는 이전에 `WinMain`의 본문 안에 있었고, 이 파일은 그 대가 때문에 존재합니다.
 * main.c 자신의 헤더 주석은 그 순서를 "우연이 아니라 구조적으로 중요하다"고 선언했습니다.
 * 이동 전의 로프 구속, 무엇이든 재생되기 전의 오디오 리스너, 정확히 한 곳에서만 감지되는
 * 사망. 그런데 그중 어느 것도 검사되지 않았습니다. 실행하는 유일한 방법이 창을 열고
 * 플레이하는 것이었기 때문입니다. 이제 tools\steptest.c가 검사하며, 그것이 가능한 유일한
 * 이유는 이 모듈이 GL 함수도, Win32 호출도, 메뉴도 언급하지 않는다는 것입니다.
 *
 * 의도적으로 이곳에 *없는* 것:
 *
 *  - **그리기.** 렌더러가 이 구조체에서 끌어갑니다(scene.h 참조). 이곳의 어떤 것도 렌더러를
 *    호출하지 않습니다. 유일한 예외인 "레벨 메시가 만들어진 섹터를 움직인 문"은
 *    ::World::geometry_dirty를 통해, 이 파일이 Scene이 무엇인지 배우지 않은 채로 플랫폼에
 *    전달됩니다.
 *  - **메뉴.** ::Input::paused가 menu_is_open() 호출이 아니라 단순한 플래그로 도착하므로,
 *    테스트가 UI를 조작하지 않고 월드를 정지시킬 수 있습니다.
 *  - **커서, 창, 키보드.** ::Input은 이미 미터와 라디안의 용어로 되어 있습니다. 그것을
 *    채우는 쪽이 하드웨어를 소유합니다.
 */

#ifndef WORLD_H
#define WORLD_H

#include "level.h"
#include "player.h"
#include "weapon.h"
#include "run.h"

/* ------------------------------------------------------------------ config */

/**
 * @brief Horizontal field of view for the world camera, radians. 90 degrees.
 *
 * ENGLISH
 * -------
 * Here rather than in main.c because both sides need the same number: the
 * renderer builds its projection from it, and ::world_step hands it to
 * ::wp_update, which solves the muzzle's world position by projecting the view
 * model's barrel through the same frustum. Two copies would put the tracer
 * somewhere the gun is not pointing.
 *
 * 한국어
 * ------
 * @brief 월드 카메라의 수평 시야각 (라디안). 90도입니다.
 *
 * main.c가 아니라 이곳에 있는 이유는 양쪽이 같은 값을 필요로 하기 때문입니다. 렌더러가
 * 이것으로 투영을 구성하고, ::world_step이 이것을 ::wp_update에 넘기면 그것이 뷰 모델
 * 총구를 같은 절두체로 투영해 총구의 월드 위치를 구합니다. 사본이 두 개면 예광탄이 총이
 * 가리키지 않는 곳으로 나갑니다.
 */
#define WORLD_FOV 1.5708f

/**
 * @brief Seconds after which ::RunState::world_time wraps.
 *
 * ENGLISH
 * -------
 * Twenty whole periods of the animated material shader's own pulse
 * (`20 * 2*pi / 0.45`), so the pulse is continuous across the reset. The
 * shader's DRIFT term is an unbounded scroll and cannot be made seamless this
 * way -- it jumps once every wrap. At four and a half minutes between jumps,
 * on a surface whose whole appearance is churning noise, that beats letting
 * float precision run out and the flow visibly step.
 *
 * 한국어
 * ------
 * @brief ::RunState::world_time이 순환하는 주기 (초).
 *
 * 애니메이션 재질 셰이더 자체 맥동의 정수 20주기(`20 * 2*pi / 0.45`)이므로, 맥동은
 * 초기화를 넘어 연속입니다. 셰이더의 DRIFT 항은 경계 없는 스크롤이라 이 방식으로 매끄럽게
 * 만들 수 없으며 순환마다 한 번 튑니다. 4분 30초에 한 번, 그것도 외형 전체가 끓는
 * 노이즈인 표면에서라면, float 정밀도가 바닥나며 흐름이 눈에 띄게 끊기는 것보다 낫습니다.
 */
#define WORLD_TIME_WRAP 279.253f

/** @brief Capacity of ::World::cur_level, bytes. / ::World::cur_level의 크기 (바이트). */
#define WORLD_LEVEL_MAX 32

/** @brief The level a fresh ::World starts in. / 새 ::World가 시작하는 레벨. */
#define WORLD_START_LEVEL "arena"

/**
 * @brief How far ::world_progress_for_stage will walk the `next` chain.
 *
 * ENGLISH
 * -------
 * A bound rather than a trust. `next` is authored text and nothing stops one
 * stage naming an earlier one -- a hub that loops back is a reasonable thing to
 * build -- so the walk has to end whether or not the target is on the chain. A
 * number far past any episode this game will ship, because being too small is a
 * stage that cannot be selected and being too large costs a few loads that were
 * going to fail anyway.
 *
 * 한국어
 * ------
 * @brief ::world_progress_for_stage가 `next` 사슬을 따라갈 최대 횟수입니다.
 *
 * 신뢰가 아니라 상한입니다. `next`는 제작된 텍스트이고 한 스테이지가 앞선 스테이지를
 * 지목하는 것을 막는 것은 없습니다(되돌아오는 허브는 만들 만한 구조입니다). 따라서 대상이
 * 사슬 위에 있든 없든 순회는 끝나야 합니다. 이 게임이 출시할 어떤 에피소드보다도 훨씬 큰
 * 값인 이유는, 너무 작으면 고를 수 없는 스테이지가 생기고 너무 크면 어차피 실패할 로드를 몇
 * 번 더 하는 비용에 그치기 때문입니다.
 */
#define WORLD_STAGE_MAX_HOPS 64

/* ------------------------------------------------------------------- input */

/**
 * @struct Input
 * @brief One frame of intent, in the world's own terms.
 *
 * ENGLISH
 * -------
 * Deliberately not a key array. Everything here is already a decision -- "the
 * player wants to go forward", not "the W key is down" -- so the platform owns
 * the mapping and ::world_step owns what the intent means. It is also what
 * makes a test able to walk a player into a wall in three lines.
 *
 * @note Every field is a HELD state, not an edge. Weapon select, the menu, the
 *       pixelise toggle and the title/death dismissals are all edges and all
 *       handled where the message arrives -- one press must be one step, and a
 *       held key polled once a frame would walk a whole menu in a frame.
 *
 * 한국어
 * ------
 * @brief 월드 자신의 용어로 표현된 한 프레임의 의도입니다.
 *
 * 의도적으로 키 배열이 아닙니다. 이곳의 모든 것은 이미 판단입니다. "W 키가 눌려 있다"가
 * 아니라 "플레이어가 전진하려 한다"입니다. 따라서 대응 관계는 플랫폼이 소유하고, 그 의도가
 * 무엇을 뜻하는지는 ::world_step이 소유합니다. 또한 이것이 테스트가 세 줄로 플레이어를
 * 벽에 걸어 들어가게 할 수 있는 이유입니다.
 *
 * @note 모든 필드는 *유지* 상태이며 엣지가 아닙니다. 무기 선택, 메뉴, 픽셀화 전환, 타이틀·
 *       사망 화면 해제는 모두 엣지이며 모두 메시지가 도착하는 곳에서 처리됩니다. 한 번
 *       누름은 한 단계여야 하고, 프레임마다 폴링되는 유지 키는 한 프레임에 메뉴 전체를
 *       지나가게 합니다.
 */
typedef struct {
    /**
     * @brief Mouse movement since the last frame, in pixels.
     *
     * ENGLISH: Raw pixels rather than radians, because two things want it and
     * they want it differently: yaw/pitch scale it by the sensitivity, and the
     * view model's sway is a spring driven by the raw flick. It also has to
     * arrive even on frames the look is locked -- see ::Input::hook.
     *
     * 한국어: 라디안이 아닌 원시 픽셀입니다. 두 곳이 이것을 원하지만 원하는 방식이 다르기
     * 때문입니다. yaw/pitch는 감도를 곱하고, 뷰 모델의 스웨이는 원시 이동량으로 구동되는
     * 스프링입니다. 또한 시점이 고정된 프레임에도 도착해야 합니다. ::Input::hook을
     * 참조하십시오.
     */
    float look_dx, look_dy;

    int forward;  /**< Held: walk along the view direction. / 유지: 시선 방향으로 전진. */
    int back;     /**< Held: walk against it. / 유지: 시선 반대 방향으로 후진. */
    int left;     /**< Held: strafe left. / 유지: 좌측 횡이동. */
    int right;    /**< Held: strafe right. / 유지: 우측 횡이동. */
    int jump;     /**< Held: jump when grounded. / 유지: 지면에 있으면 점프. */

    /** @brief Held: the trigger. Already gated on window focus by the caller. / 유지: 방아쇠. 호출자가 이미 창 포커스로 걸러 놓습니다. */
    int fire;

    /**
     * @brief Held: right mouse -- whatever the weapon in hand says it means.
     *
     * ENGLISH: Guns throw the grapple, the axe leaps. Routed on the weapon's
     * `hook` column and not on `cur == WP_AXE`, so a later weapon that also
     * leaps is a table row rather than an edit inside ::world_step.
     *
     * 한국어: 총기는 그래플을 던지고 도끼는 도약합니다. `cur == WP_AXE`가 아니라 무기의
     * `hook` 열로 분배하므로, 나중에 도약하는 무기가 생겨도 ::world_step 내부의 수정이
     * 아니라 표의 행 하나가 됩니다.
     */
    int hook;

    /**
     * @brief Non-zero while a UI has the world stopped.
     *
     * ENGLISH: A flag rather than a call to menu_is_open(), so this module has
     * no opinion about menus and a test can freeze the world by assigning to a
     * struct. Folded into ::world_frozen with the run's own end states,
     * because all of them stop exactly the same things.
     *
     * 한국어: menu_is_open() 호출이 아니라 플래그입니다. 그래서 이 모듈은 메뉴에 대해
     * 아무 견해도 갖지 않으며, 테스트는 구조체에 대입하는 것만으로 월드를 정지시킬 수
     * 있습니다. 플레이 자체의 종료 상태들과 함께 ::world_frozen으로 접히는 이유는, 그
     * 전부가 정확히 같은 것들을 정지시키기 때문입니다.
     */
    int paused;
} Input;

/* ---------------------------------------------------------------- progress */

/**
 * @struct PlayerProgress
 * @brief Everything the player keeps when they cross into the next level.
 *
 * ENGLISH
 * -------
 * Health, keycards, and the belt: which weapons are owned, how much each holds,
 * and which one is in hand. Not position, not facing, and not the hurt flash --
 * those are the new level's start to decide, or a frame's to forget.
 *
 * This exists because ::world_load_level used to spell the list out twice, in
 * two halves of a function with a ::player_spawn between them:
 *
 * @code
 *     int hp = w->player.health, held_keys = w->player.keys;
 *     int ammo[WP_TYPES], owned[WP_TYPES], cur = w->weapon.cur;
 *     ... 12 lines ...
 *     if (carry_state) { w->player.health = hp; ... w->player.keys = held_keys; }
 * @endcode
 *
 * That is the same shape ::RunState was extracted from, one layer up, and it
 * had the same problem: the list was a thing somebody had to keep current. Give
 * the player armour, or a powerup, or a count of secrets found, and the field
 * arrives in ::Player where it belongs and quietly does not survive a door.
 * "My armour vanished when I took the exit" is a bug that looks like a design
 * decision, which is exactly what the death check exists to avoid being.
 *
 * @note The list is now in three places rather than one -- these fields,
 *       ::world_progress_read and ::world_progress_write -- but all three are
 *       within twenty lines of each other, and a `_Static_assert` on this
 *       struct's size turns adding a field without teaching the other two into
 *       a compile error rather than a playtest surprise.
 *
 * 한국어
 * ------
 * @brief 플레이어가 다음 레벨로 넘어갈 때 가져가는 모든 것입니다.
 *
 * 체력, 키카드, 그리고 탄약대입니다. 어떤 무기를 보유했는지, 각각 얼마나 담고 있는지, 손에
 * 든 것이 무엇인지입니다. 위치도, 바라보는 방향도, 피격 섬광도 아닙니다. 그것들은 새 레벨의
 * 시작 지점이 결정하거나 한 프레임이 잊을 것들입니다.
 *
 * 이것이 존재하는 이유는 ::world_load_level이 그 목록을 두 번 적고 있었기 때문입니다. 한
 * 함수의 두 절반에, 그 사이에 ::player_spawn을 두고서 말입니다.
 *
 * 그것은 한 계층 위에서 ::RunState가 추출되어 나온 것과 같은 형태이며, 같은 문제를 갖고
 * 있었습니다. 그 목록은 누군가가 최신으로 유지해야 하는 것이었습니다. 플레이어에게 아머나
 * 파워업, 또는 발견한 비밀의 수를 준다고 해 보십시오. 그 필드는 마땅히 있어야 할 ::Player에
 * 도착하고, 조용히 문을 넘어 살아남지 못합니다. "출구를 지나니 아머가 사라졌다"는 설계
 * 판단처럼 보이는 버그이며, 사망 검사가 그렇게 되지 않기 위해 존재하는 바로 그것입니다.
 *
 * @note 이제 목록은 하나가 아니라 세 곳에 있습니다. 이 필드들, ::world_progress_read,
 *       ::world_progress_write입니다. 그러나 셋 모두 서로 20줄 안에 있으며, 이 구조체의
 *       크기에 대한 `_Static_assert`가 나머지 둘에게 알려 주지 않고 필드를 추가하는 것을
 *       플레이 중의 놀라움이 아니라 컴파일 오류로 만듭니다.
 */
typedef struct {
    int health;             /**< Hit points. / 체력. */
    int keys;               /**< KEY_* mask of the cards held. / 보유한 카드의 KEY_* 마스크. */
    int cur;                /**< Which weapon is in hand. / 손에 든 무기. */
    int ammo[WP_TYPES];     /**< Rounds in each belt. / 무기별 탄약. */
    int owned[WP_TYPES];    /**< Which weapons have been found. / 획득한 무기 여부. */
} PlayerProgress;

/* The two calls that move one are declared with the rest of the API below,
   because they take a ::World and it does not exist yet up here.
   이것을 옮기는 두 함수는 아래의 나머지 API와 함께 선언되어 있습니다. ::World를 인자로
   받는데 이 위쪽에는 그것이 아직 존재하지 않기 때문입니다. */

/* ------------------------------------------------------------------- world */

/**
 * @struct World
 * @brief The level, the player, the weapon and the run: everything a frame steps.
 *
 * ENGLISH
 * -------
 * One struct rather than four globals, for the reason ::RunState is one struct
 * rather than six fields: a caller that can only be handed all of it cannot be
 * handed a stale half of it, and there is exactly one place to look for what a
 * frame owns.
 *
 * @warning Do not copy or move a ::World after ::world_init. ::wp_init records
 *          the address of ::World::level inside the weapon module -- that is
 *          what hitscans trace against -- so a relocated World leaves the gun
 *          shooting at the geometry of a struct that no longer exists.
 *
 * 한국어
 * ------
 * @brief 레벨·플레이어·무기·플레이. 한 프레임이 진행시키는 모든 것입니다.
 *
 * 전역 변수 넷이 아니라 하나의 구조체인 이유는 ::RunState가 필드 여섯 개가 아니라 하나의
 * 구조체인 이유와 같습니다. 전부를 함께만 건네받을 수 있는 호출자는 그중 낡은 절반을
 * 건네받을 수 없으며, 한 프레임이 무엇을 소유하는지 찾아볼 곳이 정확히 한 군데입니다.
 *
 * @warning ::world_init 이후에 ::World를 복사하거나 옮기지 마십시오. ::wp_init이
 *          ::World::level의 주소를 무기 모듈 안에 기록하며 히트스캔이 그것을 대상으로
 *          탐색합니다. 따라서 위치가 바뀐 World는 더 이상 존재하지 않는 구조체의
 *          지오메트리를 향해 총을 쏘게 만듭니다.
 */
typedef struct {
    Level    level;   /**< The level currently loaded. / 현재 로드된 레벨. */
    Player   player;  /**< Position, momentum, health, keycards. / 위치, 운동량, 체력, 열쇠. */
    Weapon   weapon;  /**< The belt, the gun in hand and the grapple. / 탄약, 손에 든 총기, 그래플. */
    RunState run;     /**< The current run. Reset through ::run_reset, never field by field. / 현재 플레이. 필드를 하나씩이 아니라 ::run_reset을 통해 초기화합니다. */

    /**
     * @brief Look angles in radians. Pitch is clamped short of vertical.
     *
     * ENGLISH: In the World rather than in ::Player because the simulation
     * needs them -- a hook is thrown along them and a hitscan is traced along
     * them -- and because ::player_spawn returns the yaw the level's start
     * faces, which has to land somewhere the next frame can read.
     *
     * 한국어: ::Player가 아니라 World에 있는 이유는 시뮬레이션이 이 값을 필요로 하기
     * 때문입니다. 훅이 이 방향으로 던져지고 히트스캔이 이 방향으로 탐색됩니다. 또한
     * ::player_spawn이 레벨 시작 지점이 바라보는 yaw를 반환하며, 그것이 다음 프레임이
     * 읽을 수 있는 곳에 놓여야 합니다.
     */
    float yaw, pitch;

    /**
     * @brief Name of the level currently loaded.
     *
     * ENGLISH: Its own buffer rather than a read of ::Level::name, and written
     * only by ::world_load_level -- which copies the requested name out before
     * it parses anything, precisely so that this field, ::Level::next and the
     * caller's argument may all alias each other safely.
     *
     * 한국어: ::Level::name을 다시 읽는 것이 아닌 자체 버퍼이며, ::world_load_level만이
     * 기록합니다. 그 함수는 파싱 전에 요청된 이름을 복사해 두는데, 바로 이 필드와
     * ::Level::next와 호출자의 인자가 서로 별칭이어도 안전하도록 하기 위함입니다.
     */
    char cur_level[WORLD_LEVEL_MAX];

    /**
     * @brief What the player was carrying when they ARRIVED in ::cur_level.
     *
     * ENGLISH
     * -------
     * The stage checkpoint, and what ::world_restart restores.
     *
     * A restart used to be handed the boot belt -- a shotgun and twenty shells
     * -- which is right for the first stage and wrong for every stage after it.
     * Reach stage two with the axe and the launcher, die, and the game took them
     * away: not a retry of the stage, a demotion out of it. The other obvious
     * answer, restoring what the player held at the moment they died, is worse
     * still -- it hands back the ammo they spent on the attempt that failed.
     *
     * What a retry means is "put me back where I started this stage", so this is
     * a snapshot taken on arrival and never touched again until the next
     * arrival. Written by ::world_load_level for whatever reason it loaded, so a
     * transition, a new game and a stage picked from a menu all leave a
     * checkpoint behind without any of them being asked to.
     *
     * 한국어
     * ------
     * @brief 플레이어가 ::cur_level에 *도착했을 때* 들고 있던 것.
     *
     * 스테이지 체크포인트이며, ::world_restart가 복원하는 값입니다.
     *
     * 재시작은 이전에 부팅 구성(샷건과 탄환 20발)을 받았습니다. 첫 스테이지에는 맞고 그
     * 이후의 모든 스테이지에는 틀립니다. 도끼와 유탄 발사기를 들고 2스테이지에 도달해서
     * 죽으면 게임이 그것들을 빼앗았습니다. 스테이지의 재시도가 아니라 스테이지에서의
     * 강등이었습니다. 또 하나의 자명한 답인 "죽은 순간에 들고 있던 것"은 더 나쁩니다. 실패한
     * 시도에서 소모한 탄약을 그대로 돌려주기 때문입니다.
     *
     * 재시도가 뜻하는 것은 "이 스테이지를 시작했던 자리로 되돌려 놓아라"이므로, 이것은
     * 도착 시점에 찍은 스냅숏이며 다음 도착까지 다시 건드리지 않습니다. ::world_load_level이
     * 무슨 이유로 로드했든 기록하므로, 전환·새 게임·메뉴에서 고른 스테이지 모두가 요청하지
     * 않아도 체크포인트를 남깁니다.
     */
    PlayerProgress entry;

    /* --- what the platform still has to do about this world -------------- */

    /**
     * @brief Set when the DRAWN level geometry no longer matches the sectors.
     *
     * ENGLISH
     * -------
     * Doors move the sectors themselves, so everything that collides sees them
     * without knowing what a door is -- but the render mesh was built once and
     * has to be rebuilt to follow. So does a level that has just been loaded.
     *
     * Those were two separate call sites, each remembering to rebuild for its
     * own reason, and a third mover of sectors would have been a third. This is
     * one flag, raised by whatever moved them and consumed in one place by
     * ::world_take_geometry -- which is also what keeps this module from having
     * to know that a Scene exists.
     *
     * 한국어
     * ------
     * @brief *그려지는* 레벨 지오메트리가 더 이상 섹터와 일치하지 않을 때 설정됩니다.
     *
     * 문은 섹터 자체를 움직이므로 충돌하는 모든 것이 문의 정체를 모른 채 그것을 봅니다.
     * 그러나 렌더 메시는 한 번 만들어졌으므로 따라가려면 다시 만들어야 합니다. 방금 로드된
     * 레벨도 마찬가지입니다.
     *
     * 이전에는 두 개의 별개 호출 지점이 각자의 이유로 재생성을 기억하고 있었고, 섹터를
     * 움직이는 세 번째 시스템이 생기면 세 번째가 되었을 것입니다. 이제는 플래그 하나이며,
     * 움직인 쪽이 세우고 ::world_take_geometry가 한 곳에서 소비합니다. 그것이 또한 이
     * 모듈이 Scene의 존재를 알지 않아도 되게 하는 장치입니다.
     */
    int geometry_dirty;

    /** @brief Non-zero once the level mesh has been uploaded at least once. / 레벨 메시가 최소 한 번 업로드되었으면 0이 아닙니다. */
    int geometry_uploaded;
} World;

/* ------------------------------------------------------------ entering one */

/**
 * @enum WorldEnter
 * @brief Where the belt comes from when a level is loaded.
 *
 * ENGLISH
 * -------
 * This was a `carry_state` flag, and a flag has two answers to a question that
 * turned out to have three. Carrying what the player holds is right for an exit
 * and wrong for a restart; the boot belt is right for a new game and wrong for a
 * restart of anything but the first stage. The third answer -- the stage's own
 * checkpoint -- had nowhere to be expressed, so a restart took the second one
 * and stripped the player of everything they had earned.
 *
 * A row rather than a flag, for the reason the weapon table is one: the next
 * reason to load a level is a case here, not a second boolean parameter that
 * has to be read together with the first.
 *
 * 한국어
 * ------
 * @brief 레벨을 로드할 때 탄약대가 어디에서 오는지를 나타냅니다.
 *
 * 이것은 `carry_state` 플래그였고, 플래그는 답이 셋인 질문에 두 개의 답만 갖습니다.
 * 플레이어가 든 것을 이어 가는 것은 출구에는 맞고 재시작에는 틀립니다. 부팅 구성은 새
 * 게임에는 맞고 첫 스테이지가 아닌 무엇의 재시작에도 틀립니다. 세 번째 답인 스테이지 자신의
 * 체크포인트는 표현될 곳이 없었으므로, 재시작이 두 번째 답을 가져가 플레이어가 얻어 낸 모든
 * 것을 벗겨 냈습니다.
 *
 * 플래그가 아니라 표의 행인 이유는 무기 표가 그런 것과 같습니다. 레벨을 로드할 다음 이유는
 * 이곳의 한 경우이며, 첫 번째와 함께 읽어야 하는 두 번째 불리언 매개변수가 아닙니다.
 */
typedef enum {
    /**
     * @brief A new game, or an authoring reload: the belt the game boots with.
     * / 새 게임 또는 제작용 리로드. 게임이 부팅하는 탄약대입니다.
     */
    WORLD_ENTER_NEW,
    /**
     * @brief An exit transition: whatever the player is holding right now.
     *
     * ENGLISH: The exit is a reward you arrive at, not a reset -- the way a Doom
     * episode runs.
     *
     * 한국어: 출구는 도달하는 보상이지 초기화가 아닙니다. Doom 에피소드가 진행되는
     * 방식입니다.
     */
    WORLD_ENTER_CARRY,
    /**
     * @brief A restart: what the player entered this stage with.
     *
     * ENGLISH: Reads ::World::entry, which is also how a caller enters a stage
     * with a progress of its own choosing -- seed `entry`, then replay it. That
     * is what ::world_start_stage does.
     *
     * 한국어: ::World::entry를 읽습니다. 호출자가 자신이 정한 진행 상태로 스테이지에
     * 진입하는 방법도 이것입니다. `entry`를 심고 그것을 재생하십시오. ::world_start_stage가
     * 하는 일이 그것입니다.
     */
    WORLD_ENTER_REPLAY,
    WORLD_ENTER_MODES   /**< How many. / 개수. */
} WorldEnter;

/* --------------------------------------------------------------------- api */

/**
 * @brief Brings a ::World up on the title screen, in ::WORLD_START_LEVEL.
 *
 * ENGLISH
 * -------
 * @param[out] w Zeroed and seeded. No level is loaded yet.
 *
 * @note Does NOT call ::wp_init, and that is deliberate twice over. ::wp_init
 *       uploads the view model's texture, so it needs a GL context the caller
 *       may not have -- which is exactly why a headless test can drive a World
 *       without one. A caller that does have a context must make the call
 *       before its first ::world_load_level, because loading releases the hook
 *       and ::wp_init is what zeroes the weapon it would be releasing.
 * @note Seeds the smoke rng and zeroes every clock, through ::run_reset, so a
 *       fresh start and a restart begin from a state one function produced.
 *
 * 한국어
 * ------
 * @brief ::WORLD_START_LEVEL을 대상으로, 타이틀 화면 상태의 ::World를 준비합니다.
 * @param[out] w 0으로 초기화되고 기본값이 설정됩니다. 레벨은 아직 로드되지 않습니다.
 *
 * @note ::wp_init을 호출하지 *않으며*, 이는 두 가지 이유로 의도적입니다. ::wp_init은 뷰
 *       모델의 텍스처를 업로드하므로 호출자가 갖고 있지 않을 수도 있는 GL 컨텍스트를
 *       필요로 합니다. 그것이 바로 헤드리스 테스트가 컨텍스트 없이 World를 구동할 수 있는
 *       이유입니다. 컨텍스트를 가진 호출자는 첫 ::world_load_level 이전에 그 호출을 해야
 *       합니다. 로드가 훅을 해제하는데, 해제 대상인 무기를 0으로 초기화하는 것이
 *       ::wp_init이기 때문입니다.
 * @note ::run_reset을 통해 연기 난수의 시드를 설정하고 모든 시계를 0으로 만들므로, 새 시작과
 *       재시작이 하나의 함수가 만들어 낸 상태에서 출발합니다.
 */
void world_init(World *w);

/**
 * @brief Loads a level and puts everything that belongs to one into place.
 *
 * ENGLISH
 * -------
 * @param[in,out] w    The world to load into.
 * @param[in]     name Level name. May alias ::World::cur_level or
 *                     ::Level::next; both are safe.
 * @param[in]     how  Where the belt comes from. See ::WorldEnter.
 * @return 1 on success. 0 if no level of that name exists, in which case
 *         NOTHING has changed and the caller stays where it is -- a typo must
 *         not drop the player into a void, and it must not win the game either.
 *
 * @note Whatever `how` decides, the result is recorded in ::World::entry: the
 *       progress the player is standing there with IS the checkpoint for this
 *       stage. Read back out of the world rather than copied from the branch
 *       that set it, so the checkpoint cannot disagree with what the player
 *       actually has.
 *
 * @note The steps below have to happen together and in this order. They used to
 *       be written out at three call sites -- startup, an exit transition and a
 *       hot reload -- and had already drifted: the reload did not respawn the
 *       player, and whether that was deliberate could not be answered from the
 *       code.
 * @note `name` is copied into a local before ::level_load runs. ::level_load
 *       blanks the destination's `next` field before parsing, so a caller
 *       handing over `w->level.next` directly would have its search string
 *       erased mid-call. Copying first makes that impossible rather than
 *       leaving it as a rule the caller has to know.
 * @note ::door_reset must run after the sectors exist and before the first
 *       ::door_update: it copies each door's CLOSED shape out of the level, and
 *       can only do that while the level still holds it.
 *
 * 한국어
 * ------
 * @brief 레벨을 로드하고, 레벨에 속한 모든 것을 제자리에 놓습니다.
 * @param[in,out] w    로드 대상 월드.
 * @param[in]     name 레벨 이름. ::World::cur_level이나 ::Level::next와 별칭이어도
 *                     안전합니다.
 * @param[in]     how  탄약대가 어디에서 오는지. ::WorldEnter를 참조하십시오.
 * @return 성공하면 1. 해당 이름의 레벨이 없으면 0이며, 이 경우 *아무것도* 바뀌지 않고
 *         호출자는 있던 자리에 머무릅니다. 오타가 플레이어를 빈 공간에 떨어뜨려서도, 게임을
 *         이기게 해서도 안 됩니다.
 *
 * @note `how`가 무엇을 결정했든 그 결과는 ::World::entry에 기록됩니다. 플레이어가 그곳에 서
 *       있는 상태 그 자체가 이 스테이지의 체크포인트입니다. 그것을 설정한 분기에서 복사하지
 *       않고 월드에서 다시 읽어 오므로, 체크포인트가 플레이어가 실제로 가진 것과 어긋날 수
 *       없습니다.
 *
 * @note 아래 단계들은 반드시 함께, 이 순서로 수행되어야 합니다. 이전에는 시작·출구 전환·핫
 *       리로드 세 곳에 각각 작성되어 있었고 이미 어긋나 있었습니다. 리로드는 플레이어를
 *       다시 스폰하지 않았는데, 그것이 의도인지 코드만 보고는 답할 수 없었습니다.
 * @note `name`은 ::level_load 실행 전에 지역 버퍼로 복사됩니다. ::level_load는 파싱 전에
 *       대상의 `next` 필드를 비우므로, `w->level.next`를 그대로 넘기는 호출자는 호출 도중에
 *       검색 문자열이 지워집니다. 먼저 복사하면 그것이 호출자가 알아야 하는 규칙이 아니라
 *       불가능한 일이 됩니다.
 * @note ::door_reset은 섹터가 존재한 뒤, 첫 ::door_update 이전에 실행되어야 합니다. 각 문의
 *       *닫힌* 형상을 레벨에서 복사하며, 레벨이 아직 그것을 담고 있는 동안에만 가능합니다.
 */
int world_load_level(World *w, const char *name, WorldEnter how);

/**
 * @brief What a player arriving at `name` for the first time would be carrying.
 *
 * ENGLISH
 * -------
 * @param[in]  name Stage to arrive at. Need not be reachable; see the return.
 * @param[out] out  The belt to start there with. Untouched on failure.
 * @return 1 if `name` was found on the chain from ::WORLD_START_LEVEL. 0 if the
 *         chain ends, breaks or loops before reaching it.
 *
 * For "start from a stage you have cleared". Every stage before the target is
 * walked -- ALL of them, not merely the one immediately before it -- and every
 * weapon any of them hands out is granted, at half that weapon's belt capacity.
 *
 * Accumulating across the whole run is the point. Weapons are found once and
 * kept for the rest of the episode, so a player who reaches stage four honestly
 * is holding what stages one, two and three gave them. Reading only the
 * previous stage would hand them whatever stage three happened to contain and
 * silently take back the axe they picked up in stage one.
 *
 * The chain is the level text's own: each stage names its successor in `next`,
 * and that is the only ordering this game has. No table of stages is kept
 * beside it -- a second list of what follows what is a second thing to get
 * wrong, and this one is already the thing the exit uses.
 *
 * Half of maximum rather than a full belt or the pickup amount: a full belt
 * makes the granted start easier than the earned one, and reproducing what the
 * player "would have had" exactly is not a thing the level text knows. Half is
 * the honest approximation -- enough to fight with, not enough to skip the
 * stage's own supply.
 *
 * @note Keycards are deliberately NOT granted. They are per-stage: ::door_reset
 *       re-locks every door on load, and the cards for those doors are in the
 *       stage the player is about to play.
 * @note Loads each stage on the chain into a scratch ::Level to read its
 *       entities. That is ~19KB of stack for the duration of the call, and the
 *       call happens once when a stage is picked, never in a frame.
 *
 * 한국어
 * ------
 * @brief `name`에 처음 도착하는 플레이어가 들고 있을 것.
 * @param[in]  name 도착할 스테이지. 도달 가능하지 않아도 됩니다. 반환값을 참조하십시오.
 * @param[out] out  그곳에서 시작할 탄약대. 실패 시에는 건드리지 않습니다.
 * @return `name`이 ::WORLD_START_LEVEL에서 이어지는 사슬 위에서 발견되면 1. 사슬이 그
 *         전에 끝나거나 끊기거나 순환하면 0.
 *
 * "클리어한 스테이지부터 시작하기"를 위한 것입니다. 대상 이전의 *모든* 스테이지를
 * 순회하며(직전 하나가 아니라 전부입니다), 그중 어느 것이든 내주는 무기를 그 무기의 최대
 * 탄약의 절반과 함께 부여합니다.
 *
 * 플레이 전체에 걸쳐 누적하는 것이 핵심입니다. 무기는 한 번 획득하면 에피소드의 나머지 동안
 * 유지되므로, 4스테이지에 정직하게 도달한 플레이어는 1·2·3스테이지가 준 것을 들고 있습니다.
 * 직전 스테이지만 읽으면 3스테이지가 우연히 담고 있던 것만 주게 되고, 1스테이지에서 주운
 * 도끼를 조용히 회수하게 됩니다.
 *
 * 사슬은 레벨 텍스트 자신의 것입니다. 각 스테이지가 `next`에 자신의 다음을 적으며, 이
 * 게임이 가진 순서는 그것뿐입니다. 그 옆에 스테이지 표를 따로 두지 않습니다. 무엇 다음에
 * 무엇이 오는지에 대한 두 번째 목록은 틀릴 수 있는 두 번째 대상이며, 이 사슬은 이미 출구가
 * 사용하는 바로 그것입니다.
 *
 * 가득 채우거나 아이템 획득량이 아니라 최대치의 절반인 이유는, 가득 채우면 부여된 시작이
 * 직접 얻어 낸 시작보다 쉬워지고, 플레이어가 "가지고 있었을" 양을 정확히 재현하는 것은 레벨
 * 텍스트가 아는 일이 아니기 때문입니다. 절반은 정직한 근사입니다. 싸우기에는 충분하고
 * 그 스테이지 자신의 보급을 건너뛰기에는 부족합니다.
 *
 * @note 키카드는 의도적으로 부여하지 *않습니다*. 키카드는 스테이지별입니다. ::door_reset이
 *       로드 시 모든 문을 다시 잠그며, 그 문들의 카드는 플레이어가 이제부터 플레이할 스테이지
 *       안에 있습니다.
 * @note 사슬 위의 각 스테이지를 임시 ::Level로 로드하여 엔티티를 읽습니다. 호출이 지속되는
 *       동안 약 19KB의 스택을 사용하며, 이 호출은 스테이지를 고를 때 한 번 일어나고 프레임
 *       안에서는 결코 일어나지 않습니다.
 */
int world_progress_for_stage(const char *name, PlayerProgress *out);

/**
 * @brief Begins a run at `name`, as though the player had played their way there.
 *
 * ENGLISH
 * -------
 * @param[in,out] w    The world.
 * @param[in]     name Stage to start in.
 * @return 1 on success. 0 if the stage is unreachable or does not load, in
 *         which case nothing has changed.
 *
 * @note Seeds ::World::entry and then enters as a ::WORLD_ENTER_REPLAY of it,
 *       which is what makes a restart of a stage started this way put the player
 *       back with the same belt rather than the boot one.
 *
 * 한국어
 * ------
 * @brief 플레이어가 직접 플레이해 도달한 것처럼 `name`에서 플레이를 시작합니다.
 * @param[in,out] w    월드.
 * @param[in]     name 시작할 스테이지.
 * @return 성공하면 1. 스테이지에 도달할 수 없거나 로드되지 않으면 0이며, 이 경우 아무것도
 *         바뀌지 않습니다.
 *
 * @note ::World::entry를 심은 뒤 그것의 ::WORLD_ENTER_REPLAY로 진입합니다. 이 방식으로
 *       시작한 스테이지를 재시작하면 부팅 구성이 아니라 같은 탄약대로 되돌아가는 이유가
 *       그것입니다.
 */
int world_start_stage(World *w, const char *name);

/**
 * @brief Reads out what the player would keep across a level boundary.
 *
 * ENGLISH
 * -------
 * @param[in]  w   The world.
 * @param[out] out Filled in completely; every field is written.
 *
 * @note One caller today -- ::world_load_level, which reads before the spawn and
 *       writes after it. Exported anyway because the type is the answer to "what
 *       does a player keep", and a save file is exactly this struct.
 *
 * 한국어
 * ------
 * @brief 플레이어가 레벨 경계를 넘어 가져갈 것을 읽어 냅니다.
 * @param[in]  w   월드.
 * @param[out] out 모든 필드가 완전히 채워집니다.
 *
 * @note 오늘의 호출자는 하나입니다. ::world_load_level이 스폰 전에 읽고 스폰 후에 씁니다.
 *       그럼에도 공개하는 이유는, 이 타입이 "플레이어는 무엇을 가져가는가"에 대한 답이며
 *       세이브 파일이 정확히 이 구조체이기 때문입니다.
 */
void world_progress_read(const World *w, PlayerProgress *out);

/**
 * @brief Puts a ::PlayerProgress back, over whatever a spawn just set.
 *
 * ENGLISH
 * -------
 * @param[in,out] w The world.
 * @param[in]     p What to restore.
 * @note ::player_spawn resets health, so this has to run AFTER it rather than
 *       before -- which is the whole reason the read and the write are two calls
 *       and not one.
 *
 * 한국어
 * ------
 * @brief ::PlayerProgress를 스폰이 방금 설정한 값 위에 되돌려 놓습니다.
 * @param[in,out] w 월드.
 * @param[in]     p 복원할 내용.
 * @note ::player_spawn이 체력을 초기화하므로, 이 호출은 그 앞이 아니라 *뒤에* 와야 합니다.
 *       읽기와 쓰기가 하나가 아니라 두 개의 호출인 이유가 바로 그것입니다.
 */
void world_progress_write(World *w, const PlayerProgress *p);

/**
 * @brief Restarts the current level as a fresh run.
 *
 * ENGLISH
 * -------
 * @param[in,out] w The world to restart.
 *
 * @note One implementation, three ways in: the menu's RESTART row, a key on the
 *       death screen and a click on it. All three set
 *       ::RunState::restart_wanted and nothing else, so they cannot clear
 *       different halves of the same state -- a death screen that restarted
 *       without clearing `won`, or a menu restart that left the player dead,
 *       would each be a separate half-working path.
 * @note Clears ::RunState::restart_wanted along with everything else, through
 *       ::run_reset, which is why no caller does so by hand.
 * @note Does not touch the cursor or close the menu. Those belong to whoever
 *       owns the window, and they have to follow this call rather than precede
 *       it: `dead` has just changed what the cursor should be.
 *
 * 한국어
 * ------
 * @brief 현재 레벨을 새 플레이로 재시작합니다.
 * @param[in,out] w 재시작할 월드.
 *
 * @note 구현은 하나이고 진입로는 셋입니다. 메뉴의 RESTART 행, 사망 화면에서의 키, 그곳에서의
 *       클릭입니다. 셋 모두 ::RunState::restart_wanted만을 설정하므로 같은 상태의 서로 다른
 *       절반을 지울 수 없습니다. `won`을 지우지 않고 재시작하는 사망 화면이나 플레이어를
 *       죽은 채로 두는 메뉴 재시작은 각각 절반만 동작하는 별개의 경로가 됩니다.
 * @note ::run_reset을 통해 나머지 전부와 함께 ::RunState::restart_wanted도 지우므로, 손으로
 *       지우는 호출자가 없습니다.
 * @note 커서를 건드리거나 메뉴를 닫지 않습니다. 그것들은 창을 소유한 쪽에 속하며, 이 호출보다
 *       앞이 아니라 뒤에 와야 합니다. `dead`가 방금 커서의 올바른 상태를 바꾸었기 때문입니다.
 */
void world_restart(World *w);

/**
 * @brief Whether the world is stopped this frame.
 *
 * ENGLISH
 * -------
 * @param[in] w      The world.
 * @param[in] paused ::Input::paused -- non-zero while a UI has it stopped.
 * @return Non-zero when nothing in the world may advance.
 *
 * @note Pure, and one definition for the several places that ask. The title
 *       screen, the win screen, the death screen and an open menu freeze
 *       exactly the same things, and the reason this is a function rather than
 *       four conditions is that four conditions repeated across call sites is
 *       how one of them ends up missing a site.
 * @note ::world_step returns the value it used, so the renderer does not derive
 *       it a second time from state the step has since changed. Asking again
 *       after the step would hide the crosshair on the very frame the player
 *       died, one frame before the death screen it belongs to appears.
 *
 * 한국어
 * ------
 * @brief 이번 프레임에 월드가 정지 상태인지 여부입니다.
 * @param[in] w      월드.
 * @param[in] paused ::Input::paused. UI가 월드를 정지시켰으면 0이 아닙니다.
 * @return 월드의 어떤 것도 진행할 수 없으면 0이 아닙니다.
 *
 * @note 부수 효과가 없으며, 이를 묻는 여러 곳을 위한 하나의 정의입니다. 타이틀 화면, 승리
 *       화면, 사망 화면, 열린 메뉴는 정확히 같은 것들을 정지시킵니다. 네 개의 조건이 아니라
 *       함수인 이유는, 여러 호출 지점에 반복되는 네 개의 조건은 그중 하나가 어느 한 곳을
 *       빠뜨리게 되는 지름길이기 때문입니다.
 * @note ::world_step이 자신이 사용한 값을 반환하므로, 렌더러가 그 사이 갱신이 바꿔 놓은
 *       상태로부터 이 값을 두 번째로 유도하지 않습니다. 갱신 이후에 다시 물으면 플레이어가
 *       죽은 바로 그 프레임에, 그에 해당하는 사망 화면이 나타나기 한 프레임 전에 조준점이
 *       사라집니다.
 */
int world_frozen(const World *w, int paused);

/**
 * @brief Advances the world by one frame.
 *
 * ENGLISH
 * -------
 * @param[in,out] w      The world.
 * @param[in]     in     This frame's intent.
 * @param[in]     aspect Viewport aspect the view model's muzzle is solved
 *                       against. See the note below.
 * @param[in]     dt     Timestep in seconds.
 * @return The ::world_frozen value this step used, for the renderer to reuse.
 *
 * @warning The order inside is load-bearing, and tools\steptest.c is what says
 *          so in a form that fails. In brief: the rope constraint resolves
 *          before the move so this frame's correction reaches this frame's
 *          position; the axe's slam resolves after it so it lands where the
 *          player actually ended up; the audio listener is set before anything
 *          can play, or every positional sound this frame is priced by where
 *          the player stood last frame; and death is noticed in exactly one
 *          place, after every source of damage, so a new source cannot forget
 *          to kill the player.
 * @note `aspect` is the WINDOW's, not the offscreen buffer's. The renderer uses
 *       the buffer's for its projection, and the two differ slightly once the
 *       buffer's width has been rounded to whole pixels. That is the behaviour
 *       this call inherited and it is preserved deliberately; whether the
 *       muzzle should be solved against the buffer instead is a real question
 *       and a separate change.
 *
 * 한국어
 * ------
 * @brief 월드를 한 프레임 진행시킵니다.
 * @param[in,out] w      월드.
 * @param[in]     in     이번 프레임의 의도.
 * @param[in]     aspect 뷰 모델 총구를 계산할 기준 뷰포트 종횡비. 아래 참고 사항을
 *                       확인하십시오.
 * @param[in]     dt     시간 간격 (초).
 * @return 이 갱신이 사용한 ::world_frozen 값. 렌더러가 재사용합니다.
 *
 * @warning 내부의 순서는 구조적으로 중요하며, tools\steptest.c가 그것을 *실패할 수 있는*
 *          형태로 진술합니다. 요약하면, 로프 구속은 이동보다 먼저 처리되어야 이번 프레임의
 *          보정이 이번 프레임의 위치에 반영됩니다. 도끼의 내려찍기는 이동 이후에 처리되어야
 *          플레이어가 실제로 도달한 지점에서 터집니다. 오디오 리스너는 무엇이든 재생되기
 *          전에 설정되어야 하며, 그렇지 않으면 이번 프레임의 모든 위치 소리가 지난 프레임의
 *          위치로 값이 매겨집니다. 그리고 사망은 모든 피해원 이후에 정확히 한 곳에서
 *          감지되어야 하며, 그래야 새로운 피해원이 플레이어를 죽이는 것을 잊을 수 없습니다.
 * @note `aspect`는 *창*의 값이며 오프스크린 버퍼의 값이 아닙니다. 렌더러는 투영에 버퍼의
 *       값을 쓰고, 버퍼의 너비가 정수 픽셀로 반올림되고 나면 둘은 미세하게 다릅니다. 이
 *       호출이 물려받은 동작이며 의도적으로 보존했습니다. 총구를 버퍼 기준으로 계산해야
 *       하는지는 실재하는 질문이고, 별개의 변경입니다.
 */
int world_step(World *w, const Input *in, float aspect, float dt);

/**
 * @brief Claims the pending level-geometry rebuild, if there is one.
 *
 * ENGLISH
 * -------
 * @param[in,out] w       The world.
 * @param[out]    dynamic Receives what to pass ::scene_build_level as its
 *                        `dynamic` argument: 0 the first time, so the mesh is
 *                        created, and 1 afterwards, so an existing allocation
 *                        is replaced. May be NULL.
 * @return Non-zero if the drawn geometry must be rebuilt now.
 *
 * @note "Take", not "test": the flag is cleared and the upload is recorded as
 *       having happened, so a caller that asks must then rebuild. That is the
 *       trade for the caller never having to work out `dynamic` itself, which
 *       was previously a literal 0 at one call site and a literal 1 at two
 *       others.
 *
 * 한국어
 * ------
 * @brief 대기 중인 레벨 지오메트리 재생성 요청이 있으면 가져옵니다.
 * @param[in,out] w       월드.
 * @param[out]    dynamic ::scene_build_level의 `dynamic` 인자로 넘길 값을 받습니다. 첫
 *                        번째는 0이어서 메시가 생성되고, 이후는 1이어서 기존 할당이
 *                        교체됩니다. NULL이어도 됩니다.
 * @return 지금 그려지는 지오메트리를 다시 만들어야 하면 0이 아닙니다.
 *
 * @note "검사"가 아니라 "가져오기"입니다. 플래그가 지워지고 업로드가 일어난 것으로
 *       기록되므로, 물어본 호출자는 반드시 재생성해야 합니다. 그것이 호출자가 `dynamic`을
 *       스스로 따지지 않아도 되는 것의 대가입니다. 그 값은 이전에 한 호출 지점에서는 리터럴
 *       0이었고 다른 두 곳에서는 리터럴 1이었습니다.
 */
int world_take_geometry(World *w, int *dynamic);

#endif /* WORLD_H */
