/**
 * @file hook.c
 * @brief The grapple hook: a projectile claw, a winch pull, and a launch.
 *
 * ENGLISH
 * -------
 * Split out of weapon.c, which held the shotgun, the hook, the view model and
 * the decal buffers in one 1,750-line file. The hook is not a part of the gun
 * in any sense the code cares about: it has its own four-beat state machine,
 * its own physics integration, its own tuning block in weapon.h, and its own
 * headless test in tools/hooktest.c. What it shared with the shotgun was the
 * struct it hangs its fields on and nothing else -- extracting it moved no
 * logic and touched no call site, which is what says the seam was already
 * there.
 *
 * @note Touches no GL, deliberately, and that is what tools/hooktest.c is
 *       built on: the whole of the claw's flight, the pull and the launch are
 *       stepped without a window. The tether and the flying claw are DRAWN by
 *       weapon.c, because drawing them needs the gun's model-space muzzle --
 *       the one thing on the hook's side of the split that is genuinely a
 *       property of the weapon rather than of the hook.
 * @note The tuning constants stay in weapon.h beside the shotgun's. They are
 *       one feel to tune rather than two, and the MOVEMENT-FEEL banner there
 *       already points at player.h's MOMENTUM block for the other half.
 *
 * 한국어
 * ------
 * weapon.c에서 분리했습니다. 그 파일은 샷건, 훅, 뷰 모델, 데칼 버퍼를 1,750줄 하나에
 * 담고 있었습니다. 훅은 코드가 신경 쓰는 어떤 의미로도 총기의 일부가 아닙니다. 자체적인
 * 4박자 상태 머신, 자체 물리 적분, weapon.h의 자체 튜닝 블록,
 * tools/hooktest.c의 자체 헤드리스 테스트를 가집니다. 샷건과 공유하던 것은 필드를 얹는
 * 구조체뿐이었습니다. 추출하면서 로직을 옮기지도, 호출 지점을 건드리지도 않았다는 사실이
 * 그 이음매가 이미 그곳에 있었음을 말해 줍니다.
 *
 * @note 의도적으로 GL을 전혀 건드리지 않으며, tools/hooktest.c가 바로 그 위에 세워져
 *       있습니다. 클로의 비행, 견인, 도약 전체를 창 없이 진행시킵니다. 로프와 비행 중인
 *       클로를 *그리는* 것은 weapon.c입니다. 그리려면 총기의 모델 공간 총구가 필요한데,
 *       그것이 분리선의 훅 쪽에 있던 것 중 유일하게 훅이 아니라 무기의 속성인
 *       요소이기 때문입니다.
 * @note 튜닝 상수는 샷건의 것 옆 weapon.h에 그대로 둡니다. 둘이 아니라 하나의 감각을
 *       조정하는 것이며, 그곳의 MOVEMENT-FEEL 배너가 이미 나머지 절반을 위해 player.h의
 *       MOMENTUM 블록을 가리키고 있습니다.
 */
#include "weapon.h"
#include "hook.h"
#include "level.h"
#include "enemy.h"
#include "audio.h"
#include "fx.h"
/* PLAYER_GRAVITY: the pull cancels gravity while reeling, so it has to know
   what it is cancelling. weapon.c was getting this transitively; stating it
   here is the split working as intended -- the dependency did not appear, it
   became visible. Same argument as the level.h/render.h note in README.
   PLAYER_GRAVITY입니다. 견인은 감는 동안 중력을 상쇄하므로 무엇을 상쇄하는지 알아야
   합니다. weapon.c는 이것을 전이적으로 얻고 있었으며, 이곳에 명시하는 것이 분리가 의도대로
   동작한다는 뜻입니다. 의존성이 생긴 것이 아니라 *보이게* 된 것입니다. README의
   level.h/render.h 주석과 같은 논거입니다. */
#include "player.h"
#include <math.h>

/* -------------------------------------------------------------- the hook
 *
 * ENGLISH
 * -------
 * A DOOM Eternal Meat Hook: fire, pull, damage, launch. See the tuning banner
 * in weapon.h for the design; this is the machinery.
 *
 * The claw is a PROJECTILE, so this file owns a small simulation of it -- one
 * position stepped forward each frame and tested against the level and the
 * monsters. That is the only reason wp_hook_update needs the level: the throw
 * has not resolved yet when wp_hook_fire returns.
 *
 * The pull is a WINCH, which is what the previous rope-constraint version
 * deliberately was not. It accelerates the player at the target and caps the
 * result, because a Meat Hook closes distance rather than holding a length.
 * If you are looking for the swing physics, they are gone on purpose -- see
 * the note in weapon.h on why the two are different mechanics.
 *
 * These functions touch no GL and take the level explicitly, so
 * tools/hooktest.c drives the whole four-beat cycle with no context.
 *
 * 한국어
 * ------
 * DOOM Eternal의 미트 훅입니다. 발사, 견인, 피해, 도약으로 구성됩니다. 설계는
 * weapon.h의 튜닝 배너를 참조하십시오. 이곳은 그 구현입니다.
 *
 * 클로는 *발사체*이므로 이 파일이 그에 대한 작은 시뮬레이션을 소유합니다. 매 프레임
 * 전진하는 위치 하나를 레벨 및 몬스터와 충돌 검사합니다. wp_hook_update가 레벨을
 * 필요로 하는 유일한 이유가 이것입니다. wp_hook_fire가 반환되는 시점에는 투척이 아직
 * 해결되지 않았기 때문입니다.
 *
 * 견인은 *윈치*이며, 이는 이전의 로프 구속 버전이 의도적으로 피했던 방식입니다.
 * 플레이어를 대상 쪽으로 가속하고 결과에 상한을 둡니다. 미트 훅은 길이를 유지하는
 * 것이 아니라 거리를 좁히는 도구이기 때문입니다. 스윙 물리를 찾고 계신다면, 그것은
 * 의도적으로 제거되었습니다. 두 방식이 왜 다른 메커니즘인지는 weapon.h의 설명을
 * 참조하십시오.
 *
 * 이 함수들은 GL을 사용하지 않고 레벨을 명시적으로 받으므로, tools/hooktest.c가
 * 컨텍스트 없이 4단계 주기 전체를 실행합니다.
 */

/* Reeling in has to be able to reach the arrival test, or a completed pull
   would never fire its launch. Checked here rather than trusted to whoever
   next retunes the block in weapon.h. (A _Static_assert rather than #if: the
   preprocessor cannot compare floating constants.) */
_Static_assert(HOOK_ARRIVE_DIST > 0.0f,
               "HOOK_ARRIVE_DIST must be positive or the pull never arrives");
_Static_assert(HOOK_REFIRE_DELAY > HOOK_COOLDOWN,
               "a connected hook must cost more than a clean miss");

/**
 * @brief Ends the hook cycle and charges the rearm delay. The single exit path.
 *
 * ENGLISH
 * -------
 * @param[in,out] w Weapon to reset to ::HOOK_IDLE.
 * @note Every path that ends a hook goes through here -- arrival, timeout,
 *       a miss, and an explicit cancel -- so the rearm delay cannot be
 *       remembered in three places and forgotten in the fourth.
 * @note Only ever lengthens the cooldown: a hook that ended while the
 *       launcher happened to be on a longer timer should not shorten it.
 *
 * 한국어
 * ------
 * @brief 훅 주기를 종료하고 재장전 지연을 부과합니다. 유일한 종료 경로입니다.
 * @param[in,out] w ::HOOK_IDLE로 되돌릴 무기.
 * @note 훅을 끝내는 모든 경로(도달, 시간 초과, 빗나감, 명시적 취소)가 이 함수를
 *       거치므로, 재장전 지연을 세 곳에서 처리하다가 네 번째에서 빠뜨리는 일이
 *       발생할 수 없습니다.
 * @note 쿨다운은 늘리기만 합니다. 발사기가 마침 더 긴 타이머 상태일 때 훅이 끝났다고
 *       해서 그 시간을 줄여서는 안 되기 때문입니다.
 */
static void hook_end(Weapon *w) {
    w->hook_state = HOOK_IDLE;
    w->hook_enemy = -1;
    w->hook_timer = 0.0f;
    /* Cleared here rather than at the throw because this is the one funnel
       every hook passes through -- arrival, timeout and cancel all end up
       here. Leaving it set would make the next pull's first winch tick land
       at whatever phase the last one stopped at, so a hook thrown right after
       a cancel would start silent for up to a full interval.
       발사 시점이 아니라 이곳에서 해제합니다. 도달, 시간 초과, 취소가 모두 거치는 유일한
       길목이기 때문입니다. 값을 남겨 두면 다음 견인의 첫 윈치 소리가 이전 훅이 멈춘
       위상에서 시작되므로, 취소 직후에 던진 훅은 최대 한 간격만큼 무음으로 시작합니다. */
    w->hook_reel_timer = 0.0f;
    /* Zero means "not yet measured", which is what the first pull frame tests
       for. Leaving the previous hook's best distance behind would have the
       next pull comparing against an anchor that no longer exists, so a throw
       at something further away would look like it had already stalled.
       0은 "아직 측정되지 않음"을 뜻하며, 첫 견인 프레임이 이를 검사합니다. 이전 훅의
       최단 거리를 남겨 두면 다음 견인이 더 이상 존재하지 않는 앵커를 기준으로 비교하게
       되어, 더 먼 곳으로 던진 훅이 이미 정체된 것처럼 보입니다. */
    w->hook_best  = 0.0f;
    w->hook_stall = 0.0f;
    if (w->hook_cooldown < HOOK_REFIRE_DELAY) w->hook_cooldown = HOOK_REFIRE_DELAY;
}

void wp_hook_arm(Weapon *w) {
    w->hook_latched = 0;
}

int wp_hook_locks_aim(const Weapon *w) {
    return w->hook_state != HOOK_IDLE;
}

int wp_hook_in_range(const Weapon *w, const Pools *pl, const Level *l,
                     v3 eye, float yaw, float pitch) {
    /* "Would a throw connect right now" -- so the same refusals wp_hook_fire
       applies count as out of range. A crosshair that lights up while the
       launcher is on cooldown is lying about what the button will do.
       "지금 발사하면 명중하는가"이므로, wp_hook_fire가 적용하는 것과 동일한 거부
       조건들이 사거리 밖으로 간주됩니다. 발사기가 쿨다운 중인데 조준점이 켜진다면
       버튼이 무엇을 할지에 대해 거짓말을 하는 것입니다. */
    if (w->hook_state != HOOK_IDLE) return 0;
    if (w->hook_latched)            return 0;
    if (w->hook_cooldown > 0.0f)    return 0;

    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    v3 fwd = v3f(-sy * cp, sp, -cy * cp);

    /* Monsters first, then geometry -- the same priority hook_fly uses, so
       the indicator agrees with what the claw would actually hit.
       몬스터를 먼저, 그다음 지오메트리를 확인합니다. hook_fly와 동일한 우선순위이므로
       표시가 클로가 실제로 맞힐 대상과 일치합니다. */
    float t; int idx;
    if (enemy_hitscan(pl, eye, fwd, HOOK_RANGE, &t, &idx)) return 1;

    v3 n;
    return (l && level_trace(l, eye, fwd, HOOK_RANGE, &t, &n)) ? 1 : 0;
}

void wp_hook_release(Weapon *w) {
    hook_end(w);
}

int wp_hook_fire(Weapon *w, v3 eye, float yaw, float pitch) {
    if (w->hook_state != HOOK_IDLE) return 0;   /* one claw in the air at a time */
    if (w->hook_latched) return 0;              /* this press has had its throw */
    if (w->hook_cooldown > 0.0f) return 0;

    /* The claw is away the moment the trigger goes, so the cooldown is spent
       here rather than when it lands. A throw that finds nothing still threw.
       방아쇠를 당기는 순간 클로가 떠나므로, 착지 시점이 아니라 여기서 쿨다운을
       소모합니다. 아무것도 맞히지 못한 투척도 투척한 것입니다. */
    w->hook_cooldown = HOOK_COOLDOWN;
    w->hook_latched  = 1;

    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    v3 fwd = v3f(-sy * cp, sp, -cy * cp);

    /* The claw starts at the eye and flies along the aim. Starting at the
       drawn muzzle would look better for the first metre and then diverge
       from the crosshair, which is the wrong trade -- the tether is drawn
       from the muzzle regardless (see hook_muzzle), so the visual is honest
       without the flight path having to be.
       클로는 눈에서 출발하여 조준 방향으로 날아갑니다. 화면에 그려진 총구에서
       출발하면 첫 1미터는 더 나아 보이겠지만 이후 조준점과 어긋나게 되며, 이는 잘못된
       거래입니다. 로프는 어차피 총구에서 그려지므로(hook_muzzle 참조), 비행 경로가
       그럴 필요 없이도 시각적으로 정직합니다. */
    w->hook_state  = HOOK_FLYING;
    w->hook_pos    = eye;
    w->hook_target = v3add(eye, v3scale(fwd, HOOK_RANGE));
    w->hook_enemy  = -1;
    w->hook_timer  = 0.0f;

    audio_play("hook", 75);
    return 1;
}

/**
 * @brief Steps the claw forward one frame and tests what it hits. Beat 1.
 *
 * ENGLISH
 * -------
 * @param[in,out] w  Weapon whose claw is in flight.
 * @param[in]     l  Level to collide against; may be NULL for monsters only.
 * @param[in]     dt Timestep in seconds.
 * @return 1 while still flying or on a hit, 0 when the throw missed and the
 *         hook has been ended.
 * @note Sub-steps the flight in ::HOOK_FLY_STEP increments rather than one
 *       jump per frame. At 90 m/s a single 60 Hz step is 1.5 m, which is wide
 *       enough to pass straight through a monster or a thin wall -- the
 *       classic fast-projectile tunnelling bug.
 * @note Monsters are tested before geometry over the same sub-step, so a
 *       monster standing against a wall is hooked rather than the wall behind
 *       it. That is the whole point of the mechanic, and getting the priority
 *       backwards would make it feel broken near cover.
 *
 * 한국어
 * ------
 * @brief 클로를 한 프레임 전진시키고 무엇에 맞았는지 검사합니다. 1단계입니다.
 * @param[in,out] w  클로가 비행 중인 무기.
 * @param[in]     l  충돌 판정 대상 레벨. NULL이면 몬스터만 검사합니다.
 * @param[in]     dt 시간 간격 (초).
 * @return 아직 비행 중이거나 명중하면 1, 빗나가서 훅이 종료되면 0.
 * @note 프레임당 한 번 도약하는 대신 ::HOOK_FLY_STEP 단위로 세분하여 전진합니다.
 *       초속 90m에서 60Hz 한 스텝은 1.5m이며, 이는 몬스터나 얇은 벽을 그대로
 *       통과하기에 충분한 거리입니다. 고속 발사체의 전형적인 터널링 버그입니다.
 * @note 동일한 세부 스텝 내에서 지오메트리보다 몬스터를 먼저 검사하므로, 벽에 붙어
 *       선 몬스터가 있으면 뒤쪽 벽이 아니라 몬스터가 걸립니다. 그것이 이 메커니즘의
 *       핵심이며, 우선순위를 반대로 하면 엄폐물 근처에서 고장 난 것처럼 느껴집니다.
 */
static int hook_fly(Weapon *w, Pools *pl, const Level *l, float dt) {
    v3 to_end = v3sub(w->hook_target, w->hook_pos);
    float remaining = v3len(to_end);
    if (remaining < 1e-4f) { hook_end(w); return 0; }   /* reached max range */

    v3 dir = v3scale(to_end, 1.0f / remaining);
    float travel = HOOK_FLY_SPEED * dt;
    if (travel > remaining) travel = remaining;

    /* Walk the segment in short steps so nothing thin is skipped over.
       얇은 대상을 건너뛰지 않도록 선분을 짧은 스텝으로 나누어 진행합니다. */
    float done = 0.0f;
    while (done < travel) {
        float step = travel - done;
        if (step > HOOK_FLY_STEP) step = HOOK_FLY_STEP;

        v3 from = w->hook_pos;
        v3 next = v3add(from, v3scale(dir, step));

        /* Monsters first: a demon in front of a wall is the target, not the
           wall. 몬스터 우선: 벽 앞의 악마가 대상이지 벽이 아닙니다. */
        float et; int ei;
        if (enemy_hitscan(pl, from, dir, step, &et, &ei)) {
            w->hook_pos    = v3add(from, v3scale(dir, et));
            w->hook_target = w->hook_pos;
            w->hook_enemy  = ei;
            w->hook_state  = HOOK_PULLING;
            w->hook_timer  = 0.0f;
            /* Flesh: duller sound, dark spray, no spark. The material the claw
               bit is the one thing the player cannot see from behind the
               tether, so it has to be audible.
               살점: 둔탁한 소리, 어두운 분출, 스파크 없음. 클로가 문 재질은 로프 뒤에서
               플레이어가 볼 수 없는 유일한 정보이므로 소리로 전달되어야 합니다. */
            audio_play("hbiteb", 80);
            fx_spawn(pl, "hookbiteb", w->hook_pos, v3scale(dir, -1.0f));
            return 1;
        }

        /* Then geometry. A wall is a perfectly good anchor -- it just deals
           no damage on arrival.
           다음으로 지오메트리입니다. 벽도 훌륭한 고정점이며, 다만 도달 시 피해를
           주지 않을 뿐입니다. */
        float lt; v3 ln;
        if (l && level_trace(l, from, dir, step, &lt, &ln)) {
            w->hook_pos    = v3add(from, v3scale(dir, lt));
            w->hook_target = w->hook_pos;
            w->hook_enemy  = -1;
            w->hook_state  = HOOK_PULLING;
            w->hook_timer  = 0.0f;
            /* Stone: bright chip off the surface. `ln` is the wall's own
               normal, so the sparks come off it correctly -- level_trace now
               faces its normal back along the ray.
               돌: 표면에서 튀는 밝은 파편. `ln`은 벽 자신의 법선이며, level_trace가
               법선을 광선 쪽으로 되돌리므로 스파크가 올바르게 튀어나옵니다. */
            audio_play("hbite", 75);
            fx_spawn(pl, "hookbite", w->hook_pos, ln);
            return 1;
        }

        w->hook_pos = next;
        done += step;
    }

    /* Still travelling. Running out of range is a clean miss.
       아직 비행 중입니다. 사거리를 소진하면 깨끗한 빗나감입니다. */
    if (v3len(v3sub(w->hook_target, w->hook_pos)) < 1e-3f) { hook_end(w); return 0; }
    return 1;
}


/**
 * @brief Applies the launch impulse that ends a completed hook. Beat 4.
 *
 * ENGLISH
 * -------
 * @param[in]     travel    Unit direction the player was moving in on arrival.
 * @param[in]     speed     Arrival speed, m/s.
 * @param[in]     to_anchor Anchor relative to the player. The launch is made
 *                          perpendicular to this, so none of it points back
 *                          into the surface being bounced off.
 * @param[in,out] vel       Player momentum to overwrite with the launch.
 * @note Overwrites rather than adds. The arrival velocity points straight at
 *       whatever was just hit, so keeping it would drive the player into the
 *       target they are supposed to be bouncing off -- the launch has to
 *       replace that motion, not compete with it.
 * @note Two components: ::HOOK_LAUNCH_UP clears the target so the next hook
 *       has somewhere to go, and ::HOOK_LAUNCH_ALONG preserves a fraction of
 *       the travel direction so a chain of hooks keeps its momentum instead
 *       of stopping dead above each one.
 * @note The inward component is then removed. ::HOOK_LAUNCH_ALONG preserves
 *       the arrival direction, and on arrival that direction points at the
 *       anchor -- so against a wall the launch kept driving the player into
 *       it, where the momentum could neither travel nor decay and showed up
 *       as juddering. Only the impossible component is dropped; the upward
 *       kick and the along-the-wall travel are untouched.
 *
 * 한국어
 * ------
 * @brief 완료된 훅을 마무리하는 도약 충격량을 적용합니다. 4단계입니다.
 * @param[in]     travel    도달 시점에 플레이어가 이동하던 단위 방향.
 * @param[in]     speed     도달 속도 (m/s).
 * @param[in]     to_anchor 플레이어 기준 앵커 방향. 도약은 이 방향에 수직이 되도록
 *                          만들어지므로, 튕겨 나오는 표면 쪽을 향하는 성분이 남지
 *                          않습니다.
 * @param[in,out] vel       도약으로 덮어쓸 플레이어 운동량.
 * @note 더하지 않고 덮어씁니다. 도달 속도는 방금 부딪힌 대상을 정면으로 향하고
 *       있으므로, 그대로 두면 튕겨 나와야 할 대상 쪽으로 플레이어를 밀어 넣게 됩니다.
 *       도약은 그 운동을 대체해야지 경쟁해서는 안 됩니다.
 * @note 두 성분으로 구성됩니다. ::HOOK_LAUNCH_UP은 대상에서 벗어나게 하여 다음 훅이
 *       갈 곳을 확보하고, ::HOOK_LAUNCH_ALONG은 진행 방향의 일부를 보존하여 연속된
 *       훅이 매번 대상 위에서 멈추지 않고 운동량을 유지하게 합니다.
 * @note 이후 안쪽 성분이 제거됩니다. ::HOOK_LAUNCH_ALONG이 도달 방향을 보존하는데 도달
 *       시점의 그 방향은 앵커를 향하므로, 벽에 대해서는 도약이 플레이어를 계속 벽으로
 *       밀어 넣었습니다. 그 운동량은 나아가지도 감쇠하지도 못한 채 떨림으로
 *       나타났습니다. 불가능한 성분만 제거하며 상승 성분과 벽을 따라가는 이동은
 *       손대지 않습니다.
 */
static void hook_launch(v3 travel, float speed, v3 to_anchor, v3 *vel) {
    v3 up    = v3f(0.0f, HOOK_LAUNCH_UP, 0.0f);
    v3 along = v3scale(travel, speed * HOOK_LAUNCH_ALONG);

    v3 out = v3add(up, along);

    /* Strip whatever still points AT the thing just bounced off.
     *
     * HOOK_LAUNCH_ALONG preserves a share of the arrival velocity so a chain
     * of hooks keeps its momentum, and on arrival that velocity points
     * straight at the anchor -- which is a wall. Measured on a level hook:
     * the launch came out at 24.4 m/s with 13.3 of it aimed into the surface.
     * player_move refuses that move every frame, so the component neither
     * travels nor decays (airborne drag is 0.06/s), and the player grinds
     * against the wall at 20 m/s going nowhere. Frame-by-frame, position
     * froze at z=-19.57 while vel.z stayed at -20.5 for as long as it was
     * watched.
     *
     * Removing only the inward COMPONENT rather than damping the whole launch
     * keeps everything that was working: the upward kick is untouched, and the
     * part of the travel direction that runs ALONG the wall survives, which is
     * what carries a chain of hooks sideways. Only the part that was
     * physically impossible is dropped.
     *
     * 방금 튕겨 나온 대상을 *향하는* 성분을 제거합니다.
     *
     * HOOK_LAUNCH_ALONG은 연속된 훅이 운동량을 유지하도록 도달 속도의 일부를 보존하는데,
     * 도달 시점의 그 속도는 앵커를 정면으로 향하고 있으며 앵커는 곧 벽입니다. 수평 훅에서
     * 측정한 결과 도약은 24.4m/s로 나왔고 그중 13.3m/s가 표면을 향했습니다.
     * player_move가 매 프레임 그 이동을 거부하므로 이 성분은 나아가지도 감쇠하지도
     * 않으며(공중 항력 0.06/s), 플레이어는 20m/s로 벽을 갈면서 제자리에 머뭅니다.
     * 프레임 단위로 보면 위치가 z=-19.57에 고정된 채 vel.z가 -20.5로 유지되었습니다.
     *
     * 도약 전체를 감쇠시키지 않고 안쪽 *성분*만 제거하므로 잘 동작하던 것은 그대로
     * 남습니다. 상승 성분은 손대지 않고, 진행 방향 중 벽을 *따라가는* 부분은 살아남아
     * 연속된 훅을 옆으로 이끕니다. 물리적으로 불가능했던 부분만 버립니다. */
    float d = v3len(to_anchor);
    if (d > 1e-4f) {
        v3    n    = v3scale(to_anchor, 1.0f / d);
        float into = v3dot(out, n);
        if (into > 0.0f) out = v3sub(out, v3scale(n, into));
    }

    /* Cap after the strip, not before: clamping first and then removing a
       component would land under the ceiling for no stated reason.
       제거 이후에 상한을 적용합니다. 먼저 제한하고 성분을 빼면 명시되지 않은 이유로
       상한보다 낮은 값이 나옵니다. */
    float sp = v3len(out);
    if (sp > HOOK_LAUNCH_MAX) out = v3scale(out, HOOK_LAUNCH_MAX / sp);

    /* A launch that cancelled itself -- a hook taken straight into a wall with
       no sideways travel at all -- still has to get the player clear, or they
       are left resting against the surface with nothing to show for the hook.
       도약이 스스로 상쇄된 경우(측면 이동이 전혀 없이 벽에 정면으로 걸린 훅)에도
       플레이어를 떼어 놓아야 합니다. 그렇지 않으면 훅의 결과가 아무것도 없이 표면에
       붙어 있게 됩니다. */
    if (v3len(out) < HOOK_LAUNCH_UP * 0.5f)
        out = v3f(0.0f, HOOK_LAUNCH_UP, 0.0f);

    *vel = out;
}

/**
 * @brief Beats 3 and 4: decides whether the pull has ended, and ends it.
 *
 * ENGLISH
 * -------
 * @param[in,out] w    Weapon carrying the hook's live state.
 * @param[in,out] pl   Pools, for the arrival burst and the damage.
 * @param[in,out] vel  Player velocity; the launch writes it.
 * @param[in]     to   Player-to-target vector.
 * @param[in]     dist Its length.
 * @return Non-zero when the hook has arrived and released -- the caller then
 *         reports the hook as finished. Zero to carry on reeling.
 *
 * @note THREE WAYS TO ARRIVE, and the two beyond proximity are not optional:
 *       a fast pull can pass the anchor between frames, and a pull that stops
 *       closing has stalled against geometry. Proximity alone leaves the
 *       player winched to a point they already flew past.
 * @note TAKES NO LEVEL AND NO POSITION, which the split is what revealed:
 *       arriving is decided entirely from the hook's own state and the vector
 *       to its anchor. The launch that follows writes velocity and lets the
 *       player's own collision resolve it -- see ::hook_launch.
 * @note Split from ::wp_hook_update because the beats were already named in
 *       its comments and only the names were missing. What decides the hook is
 *       over is a separate question from what the winch does each frame, and
 *       tools\hooktest.c tests them as separate questions too.
 *
 * 한국어
 * ------
 * @brief 3·4번째 박자입니다. 견인이 끝났는지 판단하고 끝냅니다.
 * @param[in,out] w    훅의 실시간 상태를 지닌 무기.
 * @param[in,out] pl   도달 시의 파열과 피해를 위한 풀.
 * @param[in,out] vel  플레이어 속도. 도약이 이 값을 기록합니다.
 * @param[in]     to   플레이어에서 목표로 향하는 벡터.
 * @param[in]     dist 그 길이.
 * @return 훅이 도달해 해제되었으면 0이 아닙니다. 호출자는 훅이 끝났다고 보고합니다. 계속
 *         감아야 하면 0입니다.
 *
 * @note *도달하는 방법은 셋*이며, 근접 외의 둘은 선택 사항이 아닙니다. 빠른 견인은 프레임
 *       사이에 고정점을 지나칠 수 있고, 더 이상 가까워지지 않는 견인은 지형에 걸린
 *       것입니다. 근접만으로는 이미 지나쳐 날아간 지점으로 플레이어가 계속 감기게 됩니다.
 * @note ::wp_hook_update에서 분리했습니다. 박자들은 이미 그 함수의 주석에 이름이 붙어
 *       있었고 함수만 없었습니다. 훅이 끝났는지는 매 프레임 윈치가 무엇을 하는지와 별개의
 *       질문이며, tools\hooktest.c도 그 둘을 별개의 질문으로 검사합니다.
 */
static int hook_try_arrive(Weapon *w, Pools *pl, v3 *vel, v3 to, float dist, float dt) {
    /* --- beats 3 and 4: arrival, damage, launch ---------------------------
       Two ways to arrive, and the second is not optional. A proximity test
       alone misses whenever one frame's travel exceeds the arrival radius:
       at HOOK_PULL_MAX the player covers 0.63 m per 60Hz frame, and against a
       target the pull can overshoot -- especially a moving one, or when the
       player carries sideways momentum into the hook -- the closest approach
       can land outside HOOK_ARRIVE_DIST entirely. The player then sails past
       and oscillates around the target forever, because the pull keeps
       reversing to chase it. That is exactly what happened here: a straight
       20 m hook overshot to 29 m and never got nearer than 2.07 m against a
       1.6 m threshold.

       So arrival is ALSO "the target is now behind me". Once the direction to
       it has flipped relative to travel, the hook has done its job whether or
       not the radius was ever satisfied.

       도달 판정이 두 가지이며 두 번째는 선택 사항이 아닙니다. 근접 판정만으로는 한
       프레임의 이동 거리가 도달 반경을 넘는 순간 놓치게 됩니다. HOOK_PULL_MAX에서
       플레이어는 60Hz 프레임당 0.63m를 이동하며, 견인이 대상을 지나칠 수 있는 상황
       (특히 대상이 움직이거나 플레이어가 측면 운동량을 가지고 훅에 들어온 경우)에는
       최근접 거리가 HOOK_ARRIVE_DIST 바깥에 머무를 수 있습니다. 그러면 플레이어는
       대상을 지나쳐 날아가고, 견인이 계속 방향을 뒤집어 추격하므로 영원히 진동하게
       됩니다. 실제로 그런 일이 발생했습니다. 20m 직선 훅이 29m까지 지나쳐 갔고 1.6m
       임계값에 대해 2.07m보다 가까워진 적이 없었습니다.

       따라서 "대상이 이제 내 뒤에 있다"도 도달로 간주합니다. 대상을 향한 방향이 진행
       방향 대비 뒤집혔다면, 반경 조건을 만족했든 아니든 훅은 제 역할을 다한 것입니다.

       The overshoot test is deliberately narrow. It asks whether the velocity
       ALONG THE HOOK has reversed -- not whether the total velocity happens to
       point away, which is a different and much looser question. On the first
       pull frame the player is still nearly stationary while gravity
       cancellation has nudged vel.y upward, so a whole-vector test reads that
       tiny upward drift as "the target is behind me" and ends the hook
       instantly, having moved nobody. Requiring real inbound speed first, and
       then testing only the component that matters, is what separates a
       genuine fly-past from noise.

       지나침 판정은 의도적으로 좁게 설계되었습니다. *훅 방향* 속도 성분이 뒤집혔는지를
       묻지, 전체 속도가 우연히 반대쪽을 향하는지를 묻지 않습니다. 후자는 훨씬 느슨한
       다른 질문입니다. 견인 첫 프레임에는 플레이어가 거의 정지해 있는 반면 중력 상쇄가
       vel.y를 위로 살짝 밀어 올리므로, 벡터 전체로 판정하면 그 미세한 상승을 "대상이
       뒤에 있다"로 읽어 아무도 움직이지 않은 채 훅이 즉시 종료됩니다. 먼저 실제로
       접근 중일 것을 요구하고 그다음 관련된 성분만 검사하는 것이, 진짜 지나침과
       잡음을 구분하는 방법입니다. */
    float closing = v3dot(*vel, v3scale(to, 1.0f / (dist > 1e-4f ? dist : 1.0f)));
    int   passed  = (w->hook_timer > 0.15f && closing < -1.0f);

    /* Third way to arrive: the pull has stopped closing the distance.
     *
     * The radius test alone cannot finish a hook whose anchor the player is
     * physically unable to reach, and the floor is the everyday case -- the
     * pull measures from the eye, the player stands on the floor, so the
     * distance bottoms out at PLAYER_EYE (1.70m) against a 1.60m radius and
     * simply stops changing. Measured: it reaches 1.70m in 0.75s and holds
     * there for the remaining 2.25s until the timeout gives up, which is the
     * "player waits for the loop to end" behaviour this fixes.
     *
     * Tracked against the BEST distance so far rather than the previous
     * frame's, so the winch's small oscillation against gravity does not read
     * as progress and keep resetting the timer. See the note on hook_best.
     *
     * 도달하는 세 번째 경로: 견인이 거리를 더 이상 좁히지 못하는 경우입니다.
     *
     * 반경 판정만으로는 플레이어가 물리적으로 도달할 수 없는 앵커의 훅을 끝낼 수
     * 없으며, 바닥이 바로 그 일상적인 경우입니다. 견인은 눈에서 거리를 재고 플레이어는
     * 바닥에 서므로, 거리는 1.60m 반경에 대해 PLAYER_EYE(1.70m)에서 바닥을 치고 변화를
     * 멈춥니다. 측정 결과 0.75초 만에 1.70m에 도달한 뒤 시간 초과가 포기할 때까지 남은
     * 2.25초 동안 그대로 유지되었으며, 이것이 "플레이어가 루프가 끝나기를 기다리는"
     * 바로 그 동작입니다.
     *
     * 직전 프레임이 아니라 지금까지의 *최단* 거리를 기준으로 추적하므로, 중력에 맞서는
     * 윈치의 미세한 진동이 진전으로 읽혀 타이머를 계속 초기화하는 일이 없습니다. */
    if (w->hook_best <= 0.0f) {
        /* First pull frame: the claw has just bitten, so this is the first
           honest measurement of how far the anchor is. */
        w->hook_best  = dist;
        w->hook_stall = 0.0f;
    } else if (dist < w->hook_best - HOOK_STALL_DIST) {
        w->hook_best  = dist;
        w->hook_stall = 0.0f;
    } else {
        w->hook_stall += dt;
    }

    /* Require a moment of pulling first. Without it a hook whose anchor is
       already inside the arrival radius -- point blank at a wall -- would
       stall-complete on frame two before the winch has moved anyone, and the
       launch would fire from a standstill.
       먼저 일정 시간 견인되었을 것을 요구합니다. 그렇지 않으면 앵커가 이미 도달 반경
       안에 있는 훅(벽에 밀착해 쏜 경우)이 윈치가 아무도 움직이기 전인 두 번째 프레임에
       정체로 완료되어, 도약이 정지 상태에서 발사됩니다. */
    int stalled = (w->hook_timer > 0.15f && w->hook_stall > HOOK_STALL_TIME);

    if (dist < HOOK_ARRIVE_DIST || passed || stalled) {
        /* The direction of travel at the moment of arrival, which the launch
           partly preserves. Falls back to the hook direction when the player
           is somehow stationary, so a launch always has a sane axis.
           도달 순간의 이동 방향이며, 도약이 이를 부분적으로 보존합니다. 플레이어가
           어떤 이유로든 정지해 있으면 훅 방향으로 대체하여 도약이 항상 온전한 축을
           갖도록 합니다. */
        float speed = v3len(*vel);
        v3 travel = (speed > 0.1f) ? v3scale(*vel, 1.0f / speed)
                                   : v3norm(to);

        /* Beat 3: hooking a monster is an attack. Dealt once, here, rather
           than per frame of contact.
           3단계: 몬스터를 거는 것은 공격입니다. 접촉하는 매 프레임이 아니라 이곳에서
           한 번만 적용됩니다. */
        if (w->hook_enemy >= 0) {
            enemy_hurt(pl, w->hook_enemy, HOOK_IMPACT_DAMAGE, travel);
            fx_spawn(pl, "blood", w->hook_target, v3scale(travel, -1.0f));
        }

        /* Beat 4: bounce off automatically. No button, no timing.
         *
         * The arrival sound and burst are shared by both targets, and
         * deliberately: what the player is being told here is "you have
         * arrived and are being thrown", which is the same event whether they
         * hooked a wall or a demon. The material was already announced when
         * the claw bit -- saying it twice would make the louder, later sound
         * the one that carries the information, which is backwards.
         *
         * 도달 사운드와 버스트는 양쪽 대상이 공유하며, 이는 의도된 것입니다. 여기서
         * 플레이어에게 전달되는 것은 "도달했고 이제 튕겨 나간다"이며, 이는 벽을 걸었든
         * 악마를 걸었든 동일한 사건입니다. 재질은 클로가 물었을 때 이미 알렸습니다.
         * 두 번 말하면 더 크고 나중에 나는 소리가 정보를 전달하게 되는데, 이는 순서가
         * 뒤바뀐 것입니다. */
        audio_play("hland", 85);
        fx_spawn(pl, "hookland", w->hook_target, v3scale(travel, -1.0f));

        /* `to` is the anchor relative to the player, computed above -- exactly
           the direction the launch must not point along.
           `to`는 위에서 계산한 플레이어 기준 앵커 방향이며, 정확히 도약이 향해서는 안
           되는 방향입니다. */
        hook_launch(travel, speed, to, vel);

        hook_end(w);
        return 1;
    }

    return 0;
}

/**
 * @brief Beat 2: one frame of the winch.
 *
 * ENGLISH
 * -------
 * @param[in,out] w    Weapon carrying the hook's live state.
 * @param[in,out] pos  Player position, read for the pull direction.
 * @param[in,out] vel  Player velocity, which the pull adds to.
 * @param[in]     to   Player-to-target vector.
 * @param[in]     dist Its length.
 * @param[in]     dt   Frame time.
 *
 * @note Reached only when ::hook_try_arrive said the hook is still running, so
 *       every early exit that used to guard this is now the caller's business.
 *
 * 한국어
 * ------
 * @brief 2번째 박자입니다. 윈치의 한 프레임입니다.
 * @param[in,out] w    훅의 실시간 상태를 지닌 무기.
 * @param[in,out] pos  플레이어 위치. 견인 방향을 위해 읽습니다.
 * @param[in,out] vel  플레이어 속도. 견인이 여기에 더합니다.
 * @param[in]     to   플레이어에서 목표로 향하는 벡터.
 * @param[in]     dist 그 길이.
 * @param[in]     dt   프레임 시간.
 *
 * @note ::hook_try_arrive가 훅이 아직 진행 중이라고 답했을 때만 도달합니다. 따라서 이곳을
 *       지키던 조기 탈출은 전부 호출자의 소관이 되었습니다.
 */
static void hook_pull(Weapon *w, v3 *pos, v3 *vel, v3 to, float dist, float dt) {
    /* --- beat 2: the pull ------------------------------------------------- */

    /* The winch, restarted on a timer rather than played once. A pull lasts
       whatever it lasts -- a 40m hook runs most of a second -- so the sound has
       to be sustained, and the mixer has no loop: a long sound is a short one
       retriggered. See HOOK_REEL_INTERVAL for why the interval is shorter than
       the recipe.
       윈치입니다. 한 번 재생하지 않고 타이머로 재시작합니다. 견인은 길이가 정해져 있지
       않으며(40m 훅은 거의 1초가 걸립니다) 따라서 소리도 지속되어야 하는데, 믹서에는
       루프가 없습니다. 긴 소리는 짧은 소리를 재시작한 것입니다. 간격이 레시피보다 짧은
       이유는 HOOK_REEL_INTERVAL을 참조하십시오. */
    w->hook_reel_timer -= dt;
    if (w->hook_reel_timer <= 0.0f) {
        w->hook_reel_timer = HOOK_REEL_INTERVAL;
        audio_play("hreel", 45);
    }

    if (w->hook_timer > HOOK_PULL_TIMEOUT) {
        /* Could not get there -- the target is running, or is somewhere the
           player cannot follow. A failure, so no launch.
           도달할 수 없었습니다. 대상이 도망치고 있거나 플레이어가 따라갈 수 없는 곳에
           있습니다. 실패이므로 도약도 없습니다. */
        hook_end(w);
        return;
    }
    if (dist < 1e-4f) return;                  /* degenerate; nothing sane to do */

    v3 dir = v3scale(to, 1.0f / dist);           /* player -> target, unit */

    /* Cancel gravity so a long horizontal hook does not sag into the floor
       before it arrives. This runs before player_move applies gravity for the
       frame, so adding it back here is what neutralises it.
       긴 수평 훅이 도달 전에 바닥으로 처지지 않도록 중력을 상쇄합니다. 이 코드는
       player_move가 해당 프레임의 중력을 적용하기 전에 실행되므로, 여기서 중력을 다시
       더하는 것이 상쇄 효과를 냅니다. */
    vel->y += PLAYER_GRAVITY * HOOK_PULL_ANTIGRAV * dt;

    /* The winch. Straight at the target, capped -- see the weapon.h banner on
       why this is a pull rather than a rope constraint.
       윈치입니다. 대상을 정면으로 향하며 상한이 있습니다. 이것이 로프 구속이 아닌
       견인인 이유는 weapon.h의 배너를 참조하십시오. */
    *vel = v3add(*vel, v3scale(dir, HOOK_PULL_ACCEL * dt));

    /* Bleed off the component ACROSS the hook, so the pull converges on a
       straight line instead of flying the curve the entry momentum implies.

       Decomposed rather than clamped as a whole: `along` is the winch's own
       doing and is capped below, while `across` is whatever the player brought
       with them. Only the second is decayed, and exponentially, so a fast
       entry still curves for the first moments -- it reads as being yanked --
       and is straight by the time arrival matters. See the HOOK_PULL_STRAIGHTEN
       banner in weapon.h for the measurements that motivated this.

       훅을 *가로지르는* 성분을 감쇠시켜, 진입 운동량이 만드는 곡선을 그리는 대신 견인이
       직선으로 수렴하도록 합니다.

       전체를 클램프하지 않고 성분을 분해합니다. `along`은 윈치 자신이 만든 것이며 아래에서
       상한이 적용되고, `across`는 플레이어가 가지고 들어온 것입니다. 후자만을, 그것도
       지수적으로 감쇠시키므로 빠른 진입은 초반에 여전히 곡선을 그려 "끌려가는" 느낌을
       주면서도 도달이 중요해지는 시점에는 직선이 됩니다. 이 변경을 뒷받침한 측정값은
       weapon.h의 HOOK_PULL_STRAIGHTEN 배너를 참조하십시오. */
    float along  = v3dot(*vel, dir);
    v3    across = v3sub(*vel, v3scale(dir, along));

    if (HOOK_PULL_STRAIGHTEN > 0.0f) {
        /* expf, not (1 - k*dt): the linear form goes negative and FLIPS the
           component the moment k*dt exceeds 1, which at k=8 is any frame
           longer than 125ms. main.c clamps dt to 0.1s so that cannot happen
           today, but a decay that reverses direction under a slow frame is
           not a property to leave lying around.
           (1 - k*dt)가 아니라 expf입니다. 선형 식은 k*dt가 1을 넘는 순간 음수가 되어
           성분을 *뒤집습니다*. k=8에서는 125ms보다 긴 프레임이 그에 해당합니다. main.c가
           dt를 0.1초로 제한하므로 현재는 발생할 수 없지만, 프레임이 느려지면 방향이
           뒤집히는 감쇠를 그대로 두는 것은 좋지 않습니다. */
        across = v3scale(across, expf(-HOOK_PULL_STRAIGHTEN * dt));
    }

    /* Cap the winch's own contribution, not the total: the cap is a reel-in
       speed limit and has nothing to say about momentum across the line.
       전체가 아니라 윈치 자신의 기여분에만 상한을 적용합니다. 이 상한은 감아 들이는 속도
       제한이며, 선을 가로지르는 운동량과는 무관합니다. */
    if (along > HOOK_PULL_MAX) along = HOOK_PULL_MAX;

    *vel = v3add(across, v3scale(dir, along));

    float sp = v3len(*vel);
    if (sp > HOOK_MAX_SPEED) *vel = v3scale(*vel, HOOK_MAX_SPEED / sp);

    (void)pos;   /* read through `to` above; never written -- the pull works
                    through velocity so it collides like any other movement */
}

int wp_hook_update(Weapon *w, Pools *pl, const Level *l, v3 *pos, v3 *vel, float dt) {
    if (w->hook_state == HOOK_IDLE) return 0;

    w->hook_timer += dt;

    /* --- beat 1: the claw is still travelling ---------------------------- */
    if (w->hook_state == HOOK_FLYING)
        return hook_fly(w, pl, l, dt);

    /* --- the target may have moved --------------------------------------- */
    if (w->hook_enemy >= 0) {
        const Enemy *e = enemy_at(pl, w->hook_enemy);
        /* A dead or despawned target ends the hook without a launch: there is
           nothing left to bounce off. Tracking by index rather than position
           is what makes this detectable at all.
           죽었거나 사라진 대상은 도약 없이 훅을 종료시킵니다. 튕겨 나올 대상이 남아
           있지 않기 때문입니다. 위치가 아닌 인덱스로 추적하기에 이를 감지할 수
           있습니다. */
        if (!e || !e->active || e->state == E_DEAD) { hook_end(w); return 0; }
        /* Aim at the centre of mass rather than the feet, or the pull drags
           the player into the floor. Height comes from the type table, since
           Enemy stores only what varies per instance.
           발이 아닌 몸통 중심을 겨냥합니다. 그렇지 않으면 견인이 플레이어를 바닥으로
           끌고 들어갑니다. Enemy는 개체마다 달라지는 값만 보관하므로 신장은 종류
           테이블에서 가져옵니다. */
        const MonType *S = mon_stats(e->type);
        float mid = S ? S->height * 0.5f : 0.8f;
        w->hook_target = v3f(e->pos.x, e->pos.y + mid, e->pos.z);
    }

    v3 to = v3sub(w->hook_target, *pos);
    float dist = v3len(to);

    /* Beats 3 and 4. Returns non-zero once the hook has arrived and let
       go, which is also the end of this function.
       3·4번째 박자입니다. 훅이 도달해 놓아 버리면 0이 아닌 값을 돌려주며, 그것이
       이 함수의 끝이기도 합니다. */
    if (hook_try_arrive(w, pl, vel, to, dist, dt)) return 0;

    hook_pull(w, pos, vel, to, dist, dt);
    return 1;
}
