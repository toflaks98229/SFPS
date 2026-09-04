/**
 * @file run.h
 * @brief The state one playthrough owns, and the single call that resets it.
 *
 * ENGLISH
 * -------
 * Split out of main.c so it can be tested. Everything else in this project that
 * holds rules worth checking -- movement, the AI, the hook, the menu -- is
 * reachable from a headless tool, and the run's own state machine was the last
 * piece that was not: it lived beside WinMain, which a tool cannot link because
 * it brings its own entry point.
 *
 * The rules here are small and entirely invisible from inside the running game
 * until they are wrong in front of a player, which is the same argument
 * menu.c's split was made on.
 *
 * @note Touches no GL, no window and no level. This is bookkeeping about a
 *       playthrough, not about a world.
 *
 * 한국어
 * ------
 * 테스트할 수 있도록 main.c에서 분리했습니다. 이 프로젝트에서 검사할 가치가 있는 규칙을
 * 담은 다른 모든 것(이동, AI, 훅, 메뉴)은 헤드리스 도구에서 도달할 수 있으며, 플레이
 * 자체의 상태 머신만이 마지막까지 그렇지 않았습니다. WinMain 옆에 있었고, 도구는 자체
 * 진입점을 가지므로 그것을 링크할 수 없기 때문입니다.
 *
 * 이곳의 규칙은 작고, 플레이어 앞에서 잘못되기 전까지는 실행 중인 게임 안에서 전혀 보이지
 * 않습니다. menu.c를 분리한 것과 같은 논거입니다.
 *
 * @note GL도, 창도, 레벨도 건드리지 않습니다. 월드가 아니라 하나의 플레이에 대한
 *       기록입니다.
 */
#ifndef RUN_H
#define RUN_H

/* v3, for the one thing in here that has a position: the shrine a cleared wave
   lights. Everything else a run remembers is a count or a clock.
   v3입니다. 이 안에서 위치를 가진 유일한 것, 즉 정리된 웨이브가 켜는 제단을 위해서입니다.
   플레이가 기억하는 나머지는 전부 개수 아니면 시계입니다. */
#include "m.h"

/**
 * @struct RunState
 * @brief Everything about the CURRENT RUN that a restart has to put back.
 *
 * ENGLISH
 * -------
 * These were seven separate globals plus four function-local statics buried in
 * the frame loop, and the restart path put back six of them by naming each one.
 * That list was the only thing keeping the three ways into a restart -- the
 * menu's RESTART row, a key on the death screen, and a click on it -- agreeing
 * with each other, and it had already fallen behind: the title and world clocks
 * were never reset, and neither were the lava timers. Nothing visible went
 * wrong, which is exactly why it would have stayed that way.
 *
 * One struct makes ::run_reset the whole answer. A field added here is reset by
 * construction rather than by somebody remembering to extend a list, and the
 * question "what does a restart clear" has one place to look.
 *
 * @note This is run state, NOT world state. ::World::level, ::World::player and
 *       ::World::weapon are deliberately its siblings rather than its contents:
 *       a restart reloads the level and respawns the player through
 *       ::world_load_level, which owns rules of its own about what carries
 *       across. Folding them in would put two different lifetimes behind one
 *       reset.
 * @note Read through the ::World that owns it, never reached for directly.
 *       ::world_step is the only thing that may decide a run has ended, and
 *       every module that needs to know is told through an argument --
 *       ::scene_draw_death takes its fade time rather than reading it from a
 *       run it was handed.
 *
 * 한국어
 * ------
 * @brief 재시작이 되돌려야 하는 *현재 플레이*에 관한 모든 것입니다.
 *
 * 이들은 일곱 개의 개별 전역 변수에 더해 프레임 루프 안에 묻힌 네 개의 함수 지역
 * static이었고, 재시작 경로는 그중 여섯 개를 이름으로 하나씩 나열해 되돌렸습니다. 그
 * 목록만이 재시작으로 들어가는 세 경로(메뉴의 RESTART 행, 사망 화면에서의 키, 그곳에서의
 * 클릭)를 서로 일치시키고 있었으며, 이미 뒤처져 있었습니다. 타이틀 시계와 월드 시계는
 * 초기화된 적이 없었고 용암 타이머들도 마찬가지였습니다. 눈에 띄는 문제는 없었는데,
 * 바로 그 점이 그 상태가 계속 유지되었을 이유입니다.
 *
 * 구조체 하나로 만들면 ::run_reset이 답의 전부가 됩니다. 이곳에 추가된 필드는 누군가
 * 목록을 늘려 주기를 기다리지 않고 구조적으로 초기화되며, "재시작이 무엇을 정리하는가"를
 * 확인할 곳이 한 군데가 됩니다.
 *
 * @note 이것은 *플레이* 상태이며 월드 상태가 아닙니다. ::World::level, ::World::player,
 *       ::World::weapon은 의도적으로 이것의 내용물이 아니라 형제로 둡니다. 재시작은
 *       ::world_load_level을 통해 레벨을 다시 로드하고 플레이어를 다시 스폰하는데, 그 함수는
 *       무엇이 이어지는지에 대한 자체 규칙을 가지고 있습니다. 이들을 안에 넣으면 서로 다른 두
 *       수명이 하나의 초기화 뒤에 놓이게 됩니다.
 * @note 이것을 소유한 ::World를 통해 읽으며, 직접 손을 뻗지 않습니다. 플레이가 끝났다고 결정할
 *       수 있는 것은 ::world_step뿐이며, 알아야 하는 모든 모듈은 인자로 전달받습니다.
 *       ::scene_draw_death는 페이드 시간을 건네받은 플레이에서 읽지 않고 인자로 받습니다.
 */
typedef struct {
    /**
     * @brief Reached a terminal exit -- the win screen is up.
     * / 최종 출구에 도달함. 승리 화면이 표시된 상태입니다.
     */
    int   won;

    /**
     * @brief Health reached zero -- the death screen is up.
     *
     * ENGLISH
     * -------
     * A separate flag from `g_player.health == 0` rather than derived from it,
     * because the two answer different questions. Health is a number the
     * pickups and the hazards move around; this is a one-way latch saying the
     * run is over. Deriving the screen from the number would mean a medkit
     * collected on the frame the player died could un-kill them, and it would
     * make "died" and "standing on a lava floor at 0 hp" the same state.
     *
     * 한국어
     * ------
     * @brief 체력이 0에 도달함. 사망 화면이 표시된 상태입니다.
     *
     * `g_player.health == 0`에서 유도하지 않고 별도의 플래그로 둡니다. 둘이 서로 다른
     * 질문에 답하기 때문입니다. 체력은 아이템과 지형이 오르내리게 하는 숫자이고, 이것은
     * 플레이가 끝났음을 말하는 단방향 래치입니다. 화면을 숫자에서 유도하면 플레이어가
     * 죽은 프레임에 획득한 구급상자가 그를 되살릴 수 있고, "죽었다"와 "체력 0으로 용암
     * 위에 서 있다"가 같은 상태가 됩니다.
     */
    int   dead;

    /**
     * @brief The title screen is up and no run has started yet.
     *
     * ENGLISH
     * -------
     * Set at startup and cleared by the first press. The world behind it is a
     * real loaded level being rendered normally -- frozen, like the win and
     * death screens freeze it -- rather than a separate scene. That costs
     * nothing, keeps one code path for "the game is running but not
     * advancing", and means the first frame of play is not the first frame the
     * level has ever been drawn.
     *
     * @note The one field ::run_reset does NOT clear to zero. A restart puts
     *       the player straight back into the run rather than back to the
     *       title, so this is set explicitly there; see ::run_reset.
     * @note The art here is a placeholder by request: text on the frozen level.
     *       Everything the real title art will need is already in place -- it
     *       is a UI pass with the world dimmed behind it, exactly like
     *       ::scene_draw_win.
     *
     * 한국어
     * ------
     * @brief 타이틀 화면이 표시 중이며 아직 플레이가 시작되지 않았습니다.
     *
     * 시작 시 설정되고 첫 입력에서 해제됩니다. 뒤의 월드는 별도의 장면이 아니라 실제로
     * 로드되어 정상적으로 렌더링되는 레벨이며, 승리·사망 화면이 정지시키는 것과 같은
     * 방식으로 정지해 있습니다. 비용이 들지 않고, "게임이 실행 중이지만 진행하지
     * 않는다"에 대한 코드 경로가 하나로 유지되며, 플레이 첫 프레임이 그 레벨이 처음
     * 그려지는 프레임이 아니게 됩니다.
     *
     * @note ::run_reset이 0으로 지우지 *않는* 유일한 필드입니다. 재시작은 플레이어를
     *       타이틀이 아니라 플레이로 곧장 되돌리므로, 그곳에서 명시적으로 설정합니다.
     *       ::run_reset을 참조하십시오.
     * @note 이곳의 아트는 요청에 따라 임시입니다. 정지된 레벨 위의 텍스트입니다. 실제
     *       타이틀 아트에 필요한 것은 이미 전부 갖춰져 있습니다. ::scene_draw_win과
     *       정확히 같이, 월드를 어둡게 깔고 그 위에 그리는 UI 패스입니다.
     */
    int   title;

    /**
     * @brief Set by the death screen's "press any key"; consumed by the frame loop.
     *
     * ENGLISH
     * -------
     * A flag rather than restarting from inside the window procedure, because a
     * restart reloads the level and rebuilds its geometry -- work that needs
     * the Scene, which lives in WinMain. The same reason ::menu_take_action
     * reports an action instead of performing one.
     *
     * 한국어
     * ------
     * @brief 사망 화면의 "아무 키나 누르십시오"가 설정하고 프레임 루프가 소비합니다.
     *
     * 창 프로시저 안에서 재시작하지 않고 플래그를 두는 이유는, 재시작이 레벨을 다시
     * 로드하고 지오메트리를 재생성하는 일이기 때문입니다. 그 작업에는 WinMain에 있는
     * Scene이 필요합니다. ::menu_take_action이 동작을 수행하지 않고 보고하는 것과 같은
     * 이유입니다.
     */
    int   restart_wanted;

    /**
     * @brief Seconds the death screen has been up, for its fade.
     *
     * ENGLISH
     * -------
     * Also what stops the restart prompt appearing on the same frame the player
     * dies: pressing fire at the moment of death would otherwise restart the
     * run instantly, and the shot that killed you is very often still being
     * held.
     *
     * 한국어
     * ------
     * @brief 사망 화면이 표시된 시간(초). 페이드에 사용됩니다.
     *
     * 또한 플레이어가 죽은 바로 그 프레임에 재시작 안내가 뜨는 것을 막습니다. 그렇지
     * 않으면 사망 순간에 사격 버튼을 누르고 있던 것이 즉시 재시작이 되는데, 당신을 죽인
     * 그 사격은 대개 아직 눌린 상태입니다.
     */
    float death_time;

    /** @brief Seconds the title screen has been up, for its pulsing prompt. / 타이틀 화면이 표시된 시간(초). 명멸하는 안내 문구에 사용됩니다. */
    float title_time;

    /** @brief The clock animated materials run against; wrapped, and frozen with the world. / 애니메이션 재질이 사용하는 시계. 순환하며 월드와 함께 정지합니다. */
    float world_time;

    /* --- what the run has to be able to say about itself -------------------
     *
     * ENGLISH
     * -------
     * THE SCORE OF A RUN THAT IS MEANT TO END. The game is an arena the player
     * survives rather than a chain of levels they finish, and a screen that
     * says only "YOU DIED" tells them nothing they did not watch happen. Two
     * numbers is what turns a death into a result: how many it took down, and
     * how long it stayed up.
     *
     * BOTH ARE RUN-SCOPED, which is the whole reason they are here and not in
     * ::World. The monsters are rebuilt by every level load and the player is
     * respawned by it; neither could carry a total across an exit. A restart
     * clears these by construction, along with everything else in this struct.
     *
     * 한국어
     * ------
     * *끝나기로 되어 있는 플레이의 성적입니다.* 이 게임은 끝내는 레벨의 사슬이 아니라 살아남는
     * 아레나이며, "YOU DIED"만 말하는 화면은 플레이어가 직접 보지 않은 것을 아무것도 말해 주지
     * 않습니다. 죽음을 결과로 바꾸는 것은 두 숫자입니다. 몇을 쓰러뜨렸는가, 그리고 얼마나 오래
     * 서 있었는가입니다.
     *
     * 둘 다 *플레이 범위*이며, 그것이 이것들이 ::World가 아니라 이곳에 있는 이유 전부입니다.
     * 몬스터는 레벨 로드마다 다시 만들어지고 플레이어도 그때 다시 스폰되므로, 어느 쪽도 출구를
     * 건너 누계를 나를 수 없습니다. 재시작은 이 구조체의 나머지와 함께 이것들을 구조적으로
     * 지웁니다. */

    /**
     * @brief Monsters this run has put down.
     *
     * ENGLISH
     * -------
     * Added to from ::enemy_take_kills once a frame rather than counted in the
     * pool, because the pool does not survive a level load and this has to.
     * See ::EnemyPool::deaths for the other half of that split.
     *
     * @note Counts DEATHS, not hits and not drops. A monster that leaves
     *       nothing behind still counted, and the loot table cannot move this
     *       number by being retuned.
     *
     * 한국어
     * ------
     * @brief 이번 플레이가 쓰러뜨린 몬스터의 수.
     *
     * 풀에서 세지 않고 프레임마다 한 번 ::enemy_take_kills에서 더합니다. 풀은 레벨 로드를
     * 넘기지 못하지만 이것은 넘겨야 하기 때문입니다. 그 분담의 나머지 절반은
     * ::EnemyPool::deaths를 참조하십시오.
     *
     * @note 피격도 드롭도 아닌 *사망*을 셉니다. 아무것도 남기지 않은 몬스터도 세어지며,
     *       드롭 표를 조정해도 이 숫자는 움직이지 않습니다.
     */
    int   kills;

    /**
     * @brief Seconds this run has actually been played.
     *
     * ENGLISH
     * -------
     * NOT ::world_time, which wraps at ::WORLD_TIME_WRAP because it drives
     * animated materials and only its phase matters. A survival clock that
     * wrapped would tell a player who lasted longer than the wrap that they
     * lasted a few seconds, which is worse than telling them nothing.
     *
     * FROZEN SCREENS DO NOT COUNT. The pause menu, the title, the intermission
     * and the death screen itself all stop this, because none of them is time
     * the player survived -- see ::world_frozen for the list. The frame on
     * which the player dies does count: they were alive for it.
     *
     * 한국어
     * ------
     * @brief 이번 플레이가 실제로 플레이된 시간(초).
     *
     * ::world_time이 *아닙니다.* 그것은 애니메이션 재질을 구동하며 위상만이 중요하기에
     * ::WORLD_TIME_WRAP에서 순환합니다. 순환하는 생존 시계는 순환 주기보다 오래 버틴
     * 플레이어에게 몇 초 버텼다고 말하게 되며, 그것은 아무 말도 하지 않는 것보다 나쁩니다.
     *
     * *정지된 화면은 세지 않습니다.* 일시정지 메뉴, 타이틀, 인터미션, 그리고 사망 화면 자체가
     * 모두 이것을 멈춥니다. 어느 것도 플레이어가 살아남은 시간이 아니기 때문입니다. 목록은
     * ::world_frozen을 참조하십시오. 플레이어가 죽는 프레임은 셉니다. 그 프레임 동안에는 살아
     * 있었습니다.
     */
    float alive_time;

    /**
     * @brief Non-zero while the between-levels screen is up.
     *
     * ENGLISH
     * -------
     * Reaching an exit used to load the next level on the same frame. The
     * player crossed a line and the world was simply a different world, with
     * no moment in which anything was said about what they had just done --
     * which makes finishing a level indistinguishable from walking through a
     * door, and those are not the same event.
     *
     * Held in RunState rather than World because it is a property of the RUN:
     * the world during the intermission is still the finished level, fully
     * loaded and frozen. Putting it in World would have meant a World that is
     * between two of them, which is a state every query into the level would
     * then have to have an opinion about.
     *
     * 한국어
     * ------
     * 출구에 닿으면 같은 프레임에 다음 레벨을 불러왔습니다. 플레이어가 선을 넘으면 세계가
     * 그냥 다른 세계가 되었고, 방금 한 일에 대해 무언가 말해지는 순간이 없었습니다. 그러면
     * 레벨을 끝내는 것과 문을 지나는 것이 구분되지 않는데, 그 둘은 같은 사건이 아닙니다.
     *
     * World가 아니라 RunState에 두는 이유는 이것이 *플레이*의 속성이기 때문입니다.
     * 인터미션 동안의 월드는 여전히 방금 끝낸 레벨이며 온전히 로드된 채 멈춰 있습니다.
     * World에 두었다면 두 레벨 *사이*에 있는 World가 되고, 레벨에 대한 모든 질의가 그
     * 상태에 대해 의견을 가져야 했을 것입니다.
     */
    int   between;

    /** @brief Seconds the between-levels screen has been up. / 레벨 사이 화면이 떠 있던 시간(초). */
    float between_time;

    /** @brief The level just finished, and the one it leads to. / 방금 끝낸 레벨과 그것이 이어지는 레벨. */
    char  cleared[32];
    char  entering[32];

    /* --- the arena / 아레나 ------------------------------------------------
     *
     * ENGLISH
     * -------
     * A WAVE IS A STAGE THAT DOES NOT RELOAD THE LEVEL, and that is the whole
     * difference between this and ::between above. An arena is one room the
     * player stays in; the progression is how hard it is rather than where it
     * is. So there is no `cleared`/`entering` pair here -- both names would be
     * the same room -- and no ::world_load_level at the end of a wave, which is
     * also why a wave costs no geometry rebuild and no light bake.
     *
     * ZERO IS "NOT AN ARENA". A level with no spawners never starts a wave, so
     * every ordinary level runs with ::wave at 0 and none of this applies. That
     * is what lets the arena live beside the level chain rather than replacing
     * it -- and what lets every existing test carry on meaning what it meant.
     *
     * 한국어
     * ------
     * *웨이브는 레벨을 다시 로드하지 않는 스테이지이며*, 그것이 위의 ::between과 이것의 차이
     * 전부입니다. 아레나는 플레이어가 머무는 하나의 방이고, 진행은 어디인가가 아니라 얼마나
     * 험한가입니다. 그래서 이곳에는 `cleared`/`entering` 쌍이 없고(두 이름이 같은 방일
     * 것입니다) 웨이브 끝에 ::world_load_level도 없습니다. 웨이브가 지오메트리 재생성도 라이트
     * 베이크도 치르지 않는 이유이기도 합니다.
     *
     * 0은 "아레나가 아님"입니다. 스포너가 없는 레벨은 결코 웨이브를 시작하지 않으므로, 모든
     * 평범한 레벨은 ::wave가 0인 채로 돌아가고 이 중 어느 것도 적용되지 않습니다. 그것이
     * 아레나가 레벨 사슬을 대체하지 않고 그 곁에 살 수 있게 하는 것이며, 기존의 모든 검사가
     * 뜻하던 바를 계속 뜻하게 하는 것입니다. */

    /** @brief Which wave is running, 1-based. 0 means this is not an arena. / 진행 중인 웨이브. 1부터 셉니다. 0이면 아레나가 아닙니다. */
    int   wave;

    /** @brief Seconds the current wave has been fought. / 현재 웨이브를 싸운 시간 (초). */
    float wave_time;


    /** @brief Highest wave reached this run: what a death screen has to report. / 이번 플레이에서 도달한 최고 웨이브. 사망 화면이 보고해야 할 값. */
    int   wave_best;

    /* --- which game this is / 어떤 게임인가 ---------------------------------
     *
     * ENGLISH
     * -------
     * A PROPERTY OF HOW THE RUN WAS ENTERED, not of the map, and it has to be:
     * story and endless are the same single room, so the level's name cannot
     * tell them apart -- which is exactly how ::step_wave tells an arena from a
     * corridor today ("a level that is an arena becomes one by having spawners
     * rather than by being named").
     *
     * Zero is story, so ::run_reset's zeroing gives the boot default for free
     * and a build that never sets it plays the game it always played.
     *
     * WHAT READS IT: the boss banner, which is silent in endless mode; the
     * cutscenes, which do not play in it either; and the endless five-wave
     * respawn, which never fires in story mode. All three are decisions about
     * the playthrough rather than about the room, which is why this is here and
     * ::BossFight is on the monster pool.
     *
     * @note All three are gated in ONE place each -- ::boss_say, ::cut_begin
     *       and ::step_boss -- rather than at every site that could raise one.
     *       Five scattered tests is four chances for the sixth line somebody
     *       adds to be the one that speaks in endless mode.
     *
     * 한국어
     * ------
     * *맵이 아니라 플레이에 들어온 방식의 성질이며*, 그래야만 합니다. 스토리와 무한은 같은
     * 하나의 방이므로 레벨 이름으로 둘을 구별할 수 없습니다. 그런데 지금 ::step_wave가 아레나와
     * 복도를 구별하는 방식이 정확히 그것입니다("어떤 레벨이 아레나가 되는 것은 이름이 아니라
     * 스포너를 가졌기 때문").
     *
     * 0이 스토리이므로 ::run_reset의 0 초기화가 부팅 기본값을 공짜로 주며, 이것을 한 번도
     * 설정하지 않는 빌드는 언제나 해 온 게임을 그대로 합니다.
     *
     * *무엇이 이것을 읽는가:* 무한 모드에서 침묵하는 보스 배너, 무한 모드에서 역시 재생되지 않는
     * 컷신들, 그리고 스토리 모드에서 결코 발동하지 않는 무한 모드의 5웨이브 재소환입니다. 셋 다
     * 방이 아니라 플레이에 대한 결정이며, 그것이 이것은 이곳에 있고 ::BossFight는 몬스터 풀에
     * 있는 이유입니다.
     *
     * @note 셋 다 그것을 올릴 수 있는 모든 지점이 아니라 각각 *한 곳*에서 막힙니다. ::boss_say,
     *       ::cut_begin, ::step_boss입니다. 흩어진 검사 다섯 개는 누군가 추가하는 여섯 번째 대사가
     *       무한 모드에서 말하게 될 기회를 넷 만듭니다.
     */
    int   endless;

    /* --- what the boss is saying / 보스가 하는 말 ---------------------------
     *
     * ENGLISH
     * -------
     * ONE SLOT AND A CLOCK, which is door.c's pair and is copied from it on
     * purpose. door.h states the rule this needs: a message is "re-armed to the
     * full time on every touch rather than only when it has expired, so leaning
     * on a locked door holds the message steady instead of making it blink",
     * and the fade is shorter than the life "so the message holds at full
     * strength first and only fades over its last moments -- the instant it
     * appears is the instant it is most needed."
     *
     * LAST WRITER WINS, and there is no queue. Two of the five moments can fire
     * in one frame -- the killing blow is both "attacked while groggy" and
     * "dies" -- and this tree already has a standing answer for two events in
     * one frame: ::world_shake "TAKES THE LOUDER, never the sum". Here the
     * later line is the louder one, so ::step_boss orders its checks with death
     * last and lets it overwrite.
     *
     * TIMER-ENDED, NEVER KEYPRESS-ENDED. The pass that draws this takes
     * (Scene, viewport, a finished string) and has no input to read; scene.h
     * makes that a rule rather than an accident.
     *
     * 한국어
     * ------
     * *슬롯 하나와 시계 하나*이며, door.c의 그 쌍을 의도적으로 베낀 것입니다. door.h가 이것에
     * 필요한 규칙을 진술합니다. 메시지는 "만료된 뒤가 아니라 닿을 때마다 만땅으로 재장전되므로,
     * 잠긴 문에 기대고 있으면 깜빡이는 대신 안정적으로 유지"되고, 페이드는 수명보다 짧아서
     * "메시지가 먼저 온전한 세기로 버티고 마지막 순간에만 흐려집니다. 나타나는 순간이 가장
     * 필요한 순간입니다."
     *
     * *마지막에 쓴 것이 이기며 대기열은 없습니다.* 다섯 순간 중 둘은 한 프레임에 발동할 수
     * 있습니다. 죽이는 한 발은 "그로기 중 피격"이면서 동시에 "사망"입니다. 그리고 이 트리에는
     * 한 프레임의 두 사건에 대한 정해진 답이 이미 있습니다. ::world_shake는 "합이 아니라 *더 큰
     * 쪽*을 취합니다". 이곳에서는 나중 대사가 더 큰 쪽이므로, ::step_boss는 사망 판정을 마지막에
     * 두고 그것이 덮어쓰게 합니다.
     *
     * *시계로 끝나며 결코 키 입력으로 끝나지 않습니다.* 이것을 그리는 패스는 (Scene, 뷰포트,
     * 완성된 문자열)을 받으며 읽을 입력이 없습니다. scene.h는 그것을 우연이 아니라 규칙으로
     * 삼고 있습니다.
     */
    int   boss_line;    /**< A ::BossLine, or 0 for nothing. / ::BossLine 값. 없으면 0. */
    float boss_line_t;  /**< Seconds this line has left. / 이 대사에 남은 시간(초). */

    /* --- what was just picked up --------------------------------------------
     *
     * A NAME AND A LINE UNDER IT, for a few seconds, centred. An artifact is
     * thirty seconds of a rule the player cannot otherwise read: the screen
     * tint says one is running, but not which or what it does. This says both,
     * once, at the moment it is picked up -- and then gets out of the way.
     *
     * The kind rather than the strings, so scene.c owns the wording the way it
     * owns every other word on screen. 0 is nothing.
     *
     * *이름과 그 아래 한 줄*을 몇 초 동안 화면 가운데에 띄웁니다. 아티팩트는 플레이어가 달리
     * 읽을 수 없는 규칙 30초입니다. 화면 색조는 무언가 돌고 있다고 말할 뿐 어느 것인지도
     * 무엇을 하는지도 말하지 않습니다. 이것이 주운 순간에 한 번 둘 다 말하고 물러납니다.
     * 문자열이 아니라 종류를 담습니다. 화면의 다른 모든 낱말과 마찬가지로 문구는 scene.c가
     * 소유합니다. 0이면 없음입니다. */
    int   pickup_line;    /**< ::PW_* + 1 of what was taken, or 0. / 주운 ::PW_* + 1. 없으면 0. */
    float pickup_line_t;  /**< Seconds it has left. / 남은 시간(초). */

    /* --- the cutscene / 컷신 ------------------------------------------------
     *
     * ENGLISH
     * -------
     * A SCREEN, filed beside ::between rather than beside the boss banner it
     * looks like. The banner is a line drawn over a run that keeps playing; a
     * cutscene stops the run -- ::world_frozen counts it -- and that is the
     * property ::between has and ::RunState::boss_line does not.
     *
     * THREE FIELDS AND A LATCH. Which moment is playing, which of its pages is
     * up, how long that page has been up, and which moments have already been
     * spent. The last one is not tidiness: ::STORY_DEFEAT fires off
     * ::RunState::dead, which STAYS set, so without a latch the defeat
     * cutscene would restart on the frame after it ended, forever. The other
     * two fire off transitions and could have done without it; they use it
     * anyway, because "which moments have played" is one question.
     *
     * WHY THE MOMENT AND NOT A POINTER. story.txt is hot-reloaded, so the
     * ::StoryCut a cutscene started from can be freed and rebuilt underneath a
     * playing cutscene. A moment survives that; a pointer does not, and the
     * failure would be a crash on the one frame an author saved a file.
     *
     * 한국어
     * ------
     * *화면*이며, 닮아 보이는 보스 배너 곁이 아니라 ::between 곁에 편철됩니다. 배너는 계속
     * 진행되는 플레이 위에 그려지는 한 줄이고, 컷신은 플레이를 멈춥니다. ::world_frozen이 그것을
     * 셉니다. 그 성질은 ::between이 가지고 있고 ::RunState::boss_line은 가지고 있지 않습니다.
     *
     * *필드 셋과 래치 하나입니다.* 어느 순간이 재생 중인지, 그중 어느 페이지가 떠 있는지, 그
     * 페이지가 얼마나 오래 떠 있었는지, 그리고 어느 순간들이 이미 소비되었는지. 마지막 것은
     * 단정함의 문제가 아닙니다. ::STORY_DEFEAT은 ::RunState::dead에서 발화하는데 그것은 세워진
     * 채로 *남습니다*. 래치가 없으면 패배 컷신은 끝난 다음 프레임에 영원히 다시 시작합니다.
     * 나머지 둘은 전이에서 발화하므로 없어도 되었지만 그래도 함께 씁니다. "어느 순간이
     * 재생되었는가"는 하나의 질문이기 때문입니다.
     *
     * *왜 포인터가 아니라 순간인가.* story.txt는 핫 리로드되므로, 컷신이 출발한 ::StoryCut은
     * 재생 중인 컷신 아래에서 해제되고 다시 만들어질 수 있습니다. 순간은 그것을 견디고 포인터는
     * 견디지 못하며, 그 실패는 제작자가 파일을 저장한 바로 그 한 프레임의 충돌입니다. */

    /** @brief A ::StoryMoment plus one, or 0 for no cutscene. / ::StoryMoment + 1. 컷신이 없으면 0. */
    int   cut;

    /** @brief Which page of it is showing, 0-based. / 그중 몇 번째 페이지가 표시 중인지. 0부터. */
    int   cut_page;

    /** @brief Seconds the current page has been up. / 현재 페이지가 떠 있던 시간(초). */
    float cut_time;

    /** @brief Bit per ::StoryMoment already played this run. / 이번 플레이에서 이미 재생된 ::StoryMoment마다의 비트. */
    int   cut_seen;

    /* --- the shrine / 제단 -------------------------------------------------
     *
     * ENGLISH
     * -------
     * WHAT LIT THIS. `at altar` in loot.txt can throw a wave's reward
     * somewhere other than under the player's feet, and the moment it does,
     * the drop point acquires a problem the old one never had: it is across
     * the room, and nothing about the room says which part of it is the part
     * you were paid at. Six seconds of breather is not long enough to search a
     * room; it is exactly long enough to cross one you can already see.
     *
     * So the spot burns for as long as the file says, and these three fields
     * are that: where, how much longer, and when the next mote is due.
     *
     * HERE RATHER THAN IN World, for the same reason ::between is: it is a
     * property of the RUN and not of the level. The level does not change when
     * a shrine lights, ::world_load_level does not have to clear it -- a
     * restart clears the whole RunState -- and a headless ::World stepped by a
     * tool carries it without ever drawing it.
     *
     * ZERO IS "NO SHRINE", which is every level that is not an arena and every
     * arena whose loot.txt says `altar 0`. Nothing tests a flag: `time` at 0
     * IS the flag, the same bargain ::Pickup::vel makes.
     *
     * 한국어
     * ------
     * 무엇이 이것을 켰는가. loot.txt의 `at altar`는 웨이브 보상을 플레이어의 발치가 아닌
     * 다른 곳에 던질 수 있으며, 그 순간 낙하 지점은 예전에는 없던 문제를 갖게 됩니다. 방
     * 건너에 있는데, 방의 어느 것도 어느 부분이 보상받은 자리인지 말해 주지 않습니다. 6초의
     * 휴식은 방을 뒤지기에는 부족하고, 이미 보이는 방을 가로지르기에는 정확히 충분합니다.
     *
     * 그래서 그 자리가 파일이 말하는 시간만큼 타오르며, 이 세 필드가 그것입니다. 어디인지,
     * 얼마나 더인지, 다음 티끌이 언제인지입니다.
     *
     * World가 아니라 이곳인 이유는 ::between과 같습니다. 레벨이 아니라 *플레이*의
     * 속성입니다. 제단이 켜져도 레벨은 바뀌지 않고, ::world_load_level이 그것을 지울 필요도
     * 없으며(재시작이 RunState 전체를 지웁니다), 도구가 헤드리스로 진행시키는 ::World는
     * 그리지 않으면서도 그것을 지니고 다닙니다.
     *
     * 0은 "제단 없음"이며, 아레나가 아닌 모든 레벨과 loot.txt가 `altar 0`이라고 말하는 모든
     * 아레나가 그렇습니다. 플래그를 검사하는 것은 없습니다. `time`이 0인 것이 곧 플래그이며,
     * ::Pickup::vel이 맺는 것과 같은 거래입니다. */

    /** @brief Where the shrine burns. Meaningless while ::altar_time is 0. / 제단이 타오르는 자리. ::altar_time이 0이면 의미가 없습니다. */
    v3    altar_pos;

    /** @brief Seconds the shrine has left. 0 means there is none. / 제단에 남은 시간(초). 0이면 없습니다. */
    float altar_time;

    /** @brief Seconds until the next mote rises, paced the way the lava smoke is. / 다음 티끌이 떠오르기까지의 시간(초). 용암 연기와 같은 방식으로 조절됩니다. */
    float altar_mote;

    /* --- lava hazard timers ------------------------------------------------
       These were function-local statics inside the frame loop, which put them
       somewhere a restart could not see. Harmless in practice -- the accumulator
       clears itself on dry ground and the rest are sub-second timers -- but
       invisible to the one function whose job is to know what a run owns. State
       a reset cannot reach is state that has to be reasoned about instead.
       이들은 프레임 루프 안의 함수 지역 static이었고, 그래서 재시작이 볼 수 없는 곳에
       있었습니다. 실제로는 무해합니다. 누산기는 마른 땅에서 스스로 지워지고 나머지는 1초
       미만의 타이머입니다. 그러나 플레이가 무엇을 소유하는지 아는 것이 임무인 그 함수에게는
       보이지 않았습니다. 초기화가 닿을 수 없는 상태는 대신 머리로 따져야 하는 상태입니다. */

    /** @brief Fractional hazard damage carried between frames. / 프레임 사이에 이월되는 소수점 이하의 지형 피해량. */
    float hazard_accum;
    /** @brief Time until the burn sound is retriggered. / 화상 사운드를 다시 재생하기까지 남은 시간. */
    float burn_timer;
    /** @brief Time until the next batch of lava smoke. / 다음 용암 연기 묶음까지 남은 시간. */
    float smoke_timer;

    /**
     * @brief Random state for lava smoke placement.
     *
     * Seeded rather than left at zero, so a restart reproduces the same plume
     * as a fresh start instead of beginning from a degenerate state -- a
     * multiplicative congruential step from 0 is still 0's successor, and the
     * first puffs of a restarted run would land in a different pattern from the
     * first puffs of a new one.
     *
     * 0으로 두지 않고 시드를 부여하므로, 재시작이 축퇴된 상태에서 시작하지 않고 새로
     * 시작할 때와 같은 연기를 재현합니다.
     */
    unsigned smoke_rng;

    /**
     * @brief How hard the view is being shaken, 0 when it is still.
     *
     * ENGLISH
     * -------
     * A MAGNITUDE, not an offset. Where the camera actually ends up is worked
     * out when the frame is drawn, from this and ::RunState::world_time -- so
     * the shake needs no position, no velocity and no second clock, and a
     * ::World that is copied or stepped headlessly carries the whole of it in
     * one float.
     *
     * IT DOES NOT MOVE THE AIM. ::World::yaw and ::World::pitch are untouched;
     * only the drawn camera is displaced. A shake that moved where the shots
     * go would make the player fight their own weapon, and the one thing a
     * recoil kick must not do is decide where the bullet went.
     *
     * @note Raised by ::world_shake, which takes the LOUDER of the two rather
     *       than adding: a shotgun fired inside a grenade blast is one violent
     *       moment, not two summed into a camera that leaves the room.
     * @note Cleared by ::run_reset with everything else, so a restart begins
     *       still. It decays with ::RunState::world_time rather than on its own
     *       clock, which is what keeps the amplitude and the phase in step
     *       across a pause.
     *
     * 한국어
     * ------
     * @brief 시야가 얼마나 세게 흔들리고 있는가. 멈춰 있으면 0입니다.
     *
     * 변위가 아니라 *크기*입니다. 카메라가 실제로 어디에 놓이는지는 이 값과
     * ::RunState::world_time으로부터 그릴 때 계산합니다. 그래서 흔들림에는 위치도 속도도 두
     * 번째 시계도 필요 없으며, 복사되거나 헤드리스로 진행되는 ::World가 그 전부를 float 하나에
     * 담아 나릅니다.
     *
     * *조준을 움직이지 않습니다.* ::World::yaw와 ::World::pitch는 건드리지 않으며 그려지는
     * 카메라만 어긋납니다. 탄착점을 옮기는 흔들림은 플레이어가 자기 무기와 싸우게 만들며,
     * 반동이 결코 해서는 안 되는 일이 총알이 어디로 갔는지 결정하는 것입니다.
     *
     * @note ::world_shake가 올리며, 더하지 않고 둘 중 *큰* 쪽을 취합니다. 유탄 폭발 안에서 쏜
     *       샷건은 하나의 격렬한 순간이지, 카메라가 방을 떠나도록 합해질 둘이 아닙니다.
     * @note ::run_reset이 나머지와 함께 지우므로 재시작은 멈춘 상태로 시작합니다. 자기 시계가
     *       아니라 ::RunState::world_time과 함께 감쇠하며, 그것이 일시정지를 사이에 두고 진폭과
     *       위상을 어긋나지 않게 합니다.
     */
    float shake;
} RunState;

/** @brief Seed ::RunState::smoke_rng starts every run from. / ::RunState::smoke_rng가 매 플레이마다 시작하는 시드. */
/**
 * @brief The five things a boss fight says, in the order they can happen.
 *
 * ENGLISH
 * -------
 * @note ::BOSS_LINE_NONE is 0 so ::run_reset clears the banner by clearing the
 *       struct, which is the property every field here is chosen to have.
 * @note STORY MODE ONLY. ::step_boss gates every one of these on
 *       `!::RunState::endless`, in one place, because five separate gates is
 *       four chances to forget.
 *
 * 한국어
 * ------
 * @brief 보스전이 말하는 다섯 가지. 일어날 수 있는 순서대로입니다.
 *
 * @note ::BOSS_LINE_NONE이 0이므로 ::run_reset이 구조체를 지우는 것만으로 배너를 지웁니다.
 *       이곳의 모든 필드가 갖도록 고른 성질입니다.
 * @note *스토리 모드 전용입니다.* ::step_boss가 이 전부를 `!::RunState::endless`로, 한 곳에서
 *       막습니다. 게이트가 다섯이면 잊을 기회가 넷입니다.
 */
typedef enum {
    BOSS_LINE_NONE = 0,
    BOSS_LINE_WAKE,   /**< The maw has arrived. / 아귀가 도착했습니다. */
    BOSS_LINE_OPEN,   /**< The last ward fell; it is groggy. / 마지막 결계핵이 쓰러져 그로기입니다. */
    BOSS_LINE_HIT,    /**< It is being hurt while open. / 열린 채로 다치고 있습니다. */
    BOSS_LINE_WARD,   /**< It has raised a fresh set. / 새 무리를 세웠습니다. */
    BOSS_LINE_DIE,    /**< It is over. / 끝났습니다. */
    BOSS_LINES        /**< How many. / 개수. */
} BossLine;

/**
 * @brief Seconds a boss line stays on screen.
 *
 * ENGLISH: ::DOOR_NOTICE_TIME's number and its argument -- long enough to read
 * without being long enough that the next event has to queue behind it.
 *
 * 한국어: ::DOOR_NOTICE_TIME의 숫자이자 그 논거입니다. 읽을 수 있을 만큼 길고, 다음 사건이
 * 그 뒤에 줄을 서야 할 만큼 길지는 않습니다.
 */
#define BOSS_LINE_TIME 3.2f

/**
 * @brief Seconds an artifact's name and description stay on screen.
 *
 * Longer than ::BOSS_LINE_TIME because this one is two lines, and the second
 * is a sentence rather than a shout. Short enough that it is gone before the
 * thirty seconds it describes are half spent.
 *
 * @brief 아티팩트의 이름과 설명이 화면에 머무는 시간(초).
 * ::BOSS_LINE_TIME보다 긴 이유는 이것이 두 줄이고 둘째 줄이 외침이 아니라 문장이기
 * 때문입니다. 설명하는 30초의 절반이 지나기 전에 사라질 만큼은 짧습니다.
 */
#define PICKUP_LINE_TIME 4.0f

/**
 * @brief Seconds a groggy window must run before ::BOSS_LINE_HIT may replace
 *        ::BOSS_LINE_OPEN.
 *
 * ENGLISH: The two are adjacent by construction -- the player is already
 * shooting when the last ward falls -- so without this the "it is open" line is
 * overwritten before it can be read, by a line that says almost the same thing.
 * The same relationship ::HUD_NOTICE_FADE has to ::DOOR_NOTICE_TIME.
 *
 * 한국어: 둘은 구조적으로 인접해 있습니다. 마지막 결계핵이 쓰러질 때 플레이어는 이미 쏘고 있기
 * 때문입니다. 따라서 이것이 없으면 "열렸다"는 대사가 읽히기도 전에, 거의 같은 말을 하는 대사에
 * 덮어써집니다. ::HUD_NOTICE_FADE가 ::DOOR_NOTICE_TIME에 대해 갖는 것과 같은 관계입니다.
 */
#define BOSS_LINE_HIT_DELAY 1.6f

#define SMOKE_RNG_SEED 0x1b3f9d21u

/**
 * @brief Buffer size ::run_summary never overruns.
 *
 * Every field it renders is bounded -- ten digits for an `int`, and a clock
 * that would need more minutes than a session has -- so this is the length of
 * the worst line the function can produce plus room to not have to think about
 * it. Callers declare `char line[RUN_SUMMARY_MAX]` and stop counting.
 *
 * ::run_summary가 넘지 않는 버퍼 크기입니다. 그것이 그리는 모든 필드가 유계이므로(`int`는 열
 * 자리, 시계는 한 세션이 가질 수 있는 것보다 많은 분이 필요할 값), 이 값은 그 함수가 만들 수
 * 있는 최악의 줄 길이에 더 이상 따져 보지 않아도 될 여유를 더한 것입니다.
 */
#define RUN_SUMMARY_MAX 80

/**
 * @brief Puts the run back to its starting state.
 *
 * ENGLISH
 * -------
 * @param[out] r     Run state to clear.
 * @param[in]  title Non-zero to come up on the title screen, zero to drop
 *                   straight into play. Startup wants the title; a restart does
 *                   not, because the player has already asked to play.
 *
 * @note Assigns a zeroed struct rather than clearing each field, so a field
 *       added to ::RunState is reset without this function being edited. That
 *       is the entire point of the struct: the previous version named six
 *       globals, and the two it did not name were the ones nobody noticed.
 *
 * 한국어
 * ------
 * @brief 플레이를 시작 상태로 되돌립니다.
 * @param[out] r     초기화할 플레이 상태.
 * @param[in]  title 0이 아니면 타이틀 화면으로 시작하고, 0이면 곧바로 플레이로
 *                   들어갑니다. 시작 시에는 타이틀을 원하지만 재시작은 그렇지 않은데,
 *                   플레이어가 이미 플레이하겠다고 요청했기 때문입니다.
 *
 * @note 필드를 하나씩 지우지 않고 0으로 초기화된 구조체를 대입하므로, ::RunState에
 *       필드를 추가해도 이 함수를 고칠 필요 없이 초기화됩니다. 그것이 구조체를 만든 이유
 *       전부입니다. 이전 버전은 여섯 개의 전역을 이름으로 나열했고, 나열하지 않은 두
 *       개가 바로 아무도 알아채지 못한 것들이었습니다.
 */
void run_reset(RunState *r, int title);

/**
 * @brief Writes what this run amounted to, as one line of text.
 *
 * ENGLISH
 * -------
 * @param[in]  r   The run to describe.
 * @param[out] out Buffer to write into; always left null-terminated.
 * @param[in]  cap Capacity of `out` in bytes, including the terminator. Use
 *                 ::RUN_SUMMARY_MAX and the line always fits.
 * @return The length written, never more than `cap - 1`.
 *
 * Produces `kills 12   time 3:41`, with `   wave 5` appended when the run was
 * fought in an arena (::RunState::wave_best above zero) and omitted when it was
 * not -- an ordinary level has no wave, and printing `wave 0` there would be
 * the screen inventing a number.
 *
 * HERE RATHER THAN IN scene.c, which is the only thing that draws it. What the
 * end screens say about a run is a rule about runs: which facts are reported,
 * whether the minutes carry, and whether a value that does not apply is shown
 * anyway. Written in the pass that draws it, none of that is reachable without
 * a window -- and "3:7" instead of "3:07" is exactly the sort of wrongness that
 * survives every playtest because nobody dies at seven seconds past the minute
 * while watching for it.
 *
 * @note The clock does NOT roll over into hours. Minutes simply keep counting,
 *       so a long run reads `74:20` rather than `1:14:20`. One less field to
 *       misread, and a survival time is compared against other survival times
 *       rather than read off a wall.
 * @note Truncates rather than overruns if handed a short buffer, because
 *       ::txt_append_str and ::txt_append_int both do.
 *
 * 한국어
 * ------
 * @brief 이번 플레이가 무엇이었는지를 한 줄의 텍스트로 씁니다.
 *
 * @param[in]  r   서술할 플레이.
 * @param[out] out 쓸 버퍼. 언제나 널로 종료됩니다.
 * @param[in]  cap `out`의 용량(바이트). 종료 문자를 포함합니다. ::RUN_SUMMARY_MAX를 쓰면 줄은
 *                 언제나 들어갑니다.
 * @return 쓴 길이. 절대 `cap - 1`을 넘지 않습니다.
 *
 * `kills 12   time 3:41`을 만들며, 아레나에서 치른 플레이라면(::RunState::wave_best가 0보다
 * 크면) `   wave 5`를 덧붙이고 아니면 생략합니다. 평범한 레벨에는 웨이브가 없으며, 그곳에
 * `wave 0`을 찍는 것은 화면이 숫자를 지어내는 일입니다.
 *
 * 이것을 그리는 유일한 곳인 scene.c가 아니라 이곳에 둡니다. 종료 화면이 플레이에 대해 무엇을
 * 말하는가는 *플레이에 대한 규칙*입니다. 어떤 사실을 보고할지, 분이 넘어가는지, 해당하지 않는
 * 값을 그래도 보여 줄지가 그렇습니다. 그리는 패스 안에 쓰면 그중 어느 것도 창 없이는 닿을 수
 * 없으며, "3:07" 대신 "3:7"은 모든 플레이테스트를 견디고 살아남는 종류의 오류입니다. 그것을
 * 지켜보는 동안 정각 7초에 죽는 사람은 없기 때문입니다.
 *
 * @note 시계는 시간 단위로 넘어가지 *않습니다*. 분이 계속 늘어나므로 긴 플레이는
 *       `1:14:20`이 아니라 `74:20`으로 읽힙니다. 잘못 읽을 자리가 하나 줄고, 생존 시간은
 *       벽시계가 아니라 다른 생존 시간과 비교되는 값입니다.
 * @note 짧은 버퍼를 받으면 넘치지 않고 잘립니다. ::txt_append_str과 ::txt_append_int가
 *       둘 다 그렇게 하기 때문입니다.
 */
int run_summary(const RunState *r, char *out, int cap);

#endif
