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

    /**
     * @brief Seconds left in the breather between waves; 0 while fighting.
     *
     * ENGLISH: DOES NOT FREEZE THE WORLD, unlike every other pause in this
     * struct. The breather is when the player runs around picking up what the
     * wave dropped, so it has to be played rather than watched -- see
     * ::world_frozen for the list of things that do stop, and note that this is
     * deliberately not on it.
     *
     * 한국어: 이 구조체의 다른 모든 멈춤과 달리 *월드를 정지시키지 않습니다*. 휴식은
     * 플레이어가 웨이브가 떨어뜨린 것을 주우러 돌아다니는 시간이므로, 보는 것이 아니라
     * 플레이하는 것이어야 합니다. 무엇이 실제로 멈추는지는 ::world_frozen의 목록을
     * 참조하십시오. 이것이 의도적으로 그 목록에 없다는 점에 유의하십시오.
     */
    float wave_break;

    /** @brief Highest wave reached this run: what a death screen has to report. / 이번 플레이에서 도달한 최고 웨이브. 사망 화면이 보고해야 할 값. */
    int   wave_best;

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
#define SMOKE_RNG_SEED 0x1b3f9d21u

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

#endif
