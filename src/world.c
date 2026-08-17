/**
 * @file world.c
 * @brief One frame of the game, in the order the frame runs it.
 *
 * ENGLISH
 * -------
 * Extracted from the body of `WinMain`, where the order these steps run in was
 * documented as load-bearing and checked by nothing. See world.h for why the
 * split falls where it does; the comments below are the reasons for the order
 * itself, kept with the code they explain.
 *
 * 한국어
 * ------
 * `WinMain`의 본문에서 추출했습니다. 그곳에서 이 단계들의 실행 순서는 구조적으로 중요하다고
 * 문서화되어 있었으나 무엇도 그것을 검사하지 않았습니다. 분리 지점이 왜 이곳인지는
 * world.h를 참조하십시오. 아래 주석들은 순서 자체에 대한 이유이며, 설명 대상인 코드와 함께
 * 두었습니다.
 */

#include "world.h"
#include "hook.h"
#include "enemy.h"
#include "pickup.h"
#include "proj.h"
#include "decal.h"
#include "door.h"
#include "brush.h"
#include "fx.h"
#include "audio.h"
#include "txt.h"
#include "diag.h"

/* ------------------------------------------------------------------ config */

/** @brief Radians of look per pixel of mouse movement. / 마우스 1픽셀 이동당 시점 회전량 (라디안). */
#define MOUSE_SENS 0.0022f

/* --- lava smoke ------------------------------------------------------------
 * How often a batch of puffs is emitted, and how many are attempted per batch.
 * Together these set the density; separating them lets the rate be raised
 * without making the plume arrive in visible clumps.
 *
 * The range keeps far-off smoke out of the shared particle pool entirely. At
 * FX_MAX_PARTICLES a lava room can starve every other effect in the game, and
 * a puff behind the player is worth nothing.
 *
 * 연기 묶음을 얼마나 자주 방출하는지와 묶음당 몇 개를 시도하는지입니다. 둘이 함께 밀도를
 * 결정하며, 분리해 두면 방출이 눈에 띄는 덩어리로 도착하지 않게 하면서 비율을 올릴 수
 * 있습니다.
 *
 * 사거리는 먼 곳의 연기를 공유 파티클 풀에서 아예 배제합니다. FX_MAX_PARTICLES에서
 * 용암 방 하나가 게임의 다른 모든 효과를 고갈시킬 수 있으며, 플레이어 뒤의 연기는 아무런
 * 가치가 없습니다. */
#define LAVA_SMOKE_INTERVAL  0.16f  ///< @brief Seconds between batches. / 묶음 사이의 간격 (초).
#define LAVA_SMOKE_PER_TICK  2      ///< @brief Puffs attempted per batch. / 묶음당 시도하는 연기 수.
#define LAVA_SMOKE_RANGE     22.0f  ///< @brief Metres beyond which no smoke spawns. / 이 거리를 넘으면 연기를 생성하지 않습니다 (미터).

/**
 * @def LAVA_SMOKE_LIP
 * @brief How far off a hazard's top face the sampling happens, metres.
 *
 * Used three ways and deliberately one number: a puff starts this far ABOVE the
 * surface so it is not born inside it, the "am I really in the volume" probe
 * sits this far BELOW it for the same reason, and a floor within this much of
 * the surface counts as the surface rather than as something covering it. They
 * are the same tolerance -- how thick the skin of a lava pool is -- and giving
 * them three names would invite three different answers.
 *
 * 위험 지형 윗면에서 얼마나 떨어진 곳을 표본으로 삼는지입니다 (미터). 세 가지로 쓰이며 일부러
 * 하나의 숫자입니다. 연기는 표면 안에서 태어나지 않도록 이만큼 *위에서* 시작하고, "정말 부피
 * 안인가" 탐침은 같은 이유로 이만큼 *아래에* 놓이며, 표면에서 이 거리 안의 바닥은 그것을 덮는
 * 무언가가 아니라 표면 자체로 셉니다. 셋은 같은 허용 오차, 즉 용암 웅덩이 표피의 두께이며,
 * 이름을 셋으로 나누면 답도 셋으로 갈라지기를 청하는 일입니다.
 */
#define LAVA_SMOKE_LIP       0.05f

/* ------------------------------------------------------- look, move, weapon */

/**
 * @brief Mouse look, the grapple, movement and the weapon -- in that order.
 *
 * ENGLISH
 * -------
 * @param[in,out] w      The world.
 * @param[in]     in     This frame's intent.
 * @param[in]     aspect Viewport aspect for the muzzle solve. See ::world_step.
 * @param[in]     dt     Timestep in seconds.
 *
 * 한국어
 * ------
 * @brief 마우스 시점, 그래플, 이동, 무기를 그 순서대로 처리합니다.
 * @param[in,out] w      월드.
 * @param[in]     in     이번 프레임의 의도.
 * @param[in]     aspect 총구 계산용 뷰포트 종횡비. ::world_step을 참조하십시오.
 * @param[in]     dt     시간 간격 (초).
 */
static void step_look_move(World *w, const Input *in, float aspect, float dt) {
    /* The hook holds the aim still for its whole cycle -- see
       wp_hook_locks_aim for why each half needs it.

       Only the application to yaw/pitch is skipped. The delta was still read
       and the cursor still recentred by whoever filled in the Input: skipping
       the read instead would let the pointer wander off the window and come
       back as one enormous jump the moment the hook ended. The view model's
       sway gets the delta regardless, so the gun keeps reacting to the hand
       holding it.

       훅은 주기 전체에 걸쳐 조준을 고정합니다. 각 구간에 왜 필요한지는
       wp_hook_locks_aim을 참조하십시오.

       건너뛰는 것은 yaw/pitch에 적용하는 부분뿐입니다. 변화량은 Input을 채운 쪽이 이미
       읽었고 커서도 다시 중앙에 놓았습니다. 읽기 자체를 건너뛰면 포인터가 창 밖으로
       벗어났다가 훅이 끝나는 순간 거대한 도약으로 돌아옵니다. 뷰 모델의 스웨이는 변화량을
       그대로 받으므로, 총기는 그것을 쥔 손에 계속 반응합니다. */
    if (!wp_hook_locks_aim(&w->weapon)) {
        w->yaw   -= in->look_dx * MOUSE_SENS;
        w->pitch -= in->look_dy * MOUSE_SENS;

        const float limit = M_PI_F * 0.49f;
        w->pitch = clampf(w->pitch, -limit, limit);
    }

    float cy = cosf(w->yaw), sy = sinf(w->yaw);
    v3 fwd   = v3f(-sy, 0, -cy);
    v3 right = v3f( cy, 0, -sy);

    v3 wish = v3f(0, 0, 0);
    if (in->forward) wish = v3add(wish, fwd);
    if (in->back)    wish = v3sub(wish, fwd);
    if (in->right)   wish = v3add(wish, right);
    if (in->left)    wish = v3sub(wish, right);
    wish = v3norm(wish);

    /* --- the grapple: fire, pull, damage, launch ---------------------------
       Pressing RMB throws the claw; the rest runs itself. A Meat Hook completes
       on its own and ends in an automatic launch, so the button's only job is
       to start one -- there is nothing to hold and nothing to let go of.

       wp_hook_fire refuses while a hook is already in the air, on cooldown, or
       until the button has been released, so handing it the held state every
       frame is safe and one press stays one throw.

       Right-click means whatever the weapon in hand says it means: guns throw
       the grapple, the axe leaps. One button, because a third would be a key
       the player has to remember for one weapon. Routed on the weapon's `hook`
       column rather than on `cur == WP_AXE`, so a later weapon that also leaps
       -- or one that does neither -- is a table row and not an edit here.

       `!w->run.won` is belt and braces: a won run is frozen, so this whole
       function is unreachable then. It stays because the condition states the
       intent at the site that would otherwise have to be read against
       world_frozen to be understood.

       우클릭을 누르면 클로가 던져지고 나머지는 스스로 진행합니다. 밋 훅은 스스로 완료되고
       자동 발사로 끝나므로, 버튼의 유일한 임무는 하나를 시작하는 것입니다. 붙잡을 것도 놓을
       것도 없습니다.

       wp_hook_fire는 훅이 이미 공중에 있거나 재사용 대기 중이거나 버튼이 놓이기 전까지
       거절하므로, 매 프레임 유지 상태를 넘겨도 안전하며 한 번 누름은 한 번 던지기로
       유지됩니다.

       우클릭은 손에 든 무기가 말하는 의미를 갖습니다. 총기는 그래플을 던지고 도끼는
       도약합니다. 버튼 하나인 이유는, 세 번째 버튼은 무기 하나를 위해 플레이어가 외워야
       하는 키가 되기 때문입니다. `cur == WP_AXE`가 아니라 무기의 `hook` 열로 분배하므로,
       나중에 도약하는 무기나 둘 다 하지 않는 무기가 생겨도 이곳의 수정이 아니라 표의 행
       하나가 됩니다.

       `!w->run.won`은 이중 안전장치입니다. 승리한 플레이는 정지 상태이므로 이 함수 전체가
       그때는 도달 불가능합니다. 그래도 남겨 둔 이유는, 이 조건이 없다면 그 의도를 이해하기
       위해 world_frozen을 함께 읽어야 하기 때문입니다. */
    if (in->hook && !w->run.won) {
        if (wp_stats(w->weapon.cur)->hook)
            wp_hook_fire(&w->weapon, w->player.pos, w->yaw, w->pitch);
        else
            wp_axe_leap(&w->weapon, w->yaw, w->pitch, &w->player.vel);
    } else {
        /* Button up: rearm. Unconditional, because a throw that missed also has
           to be cleared, and rearming is what the release edge means.
           wp_hook_arm deliberately does not touch the cooldown, so tapping fast
           still cannot beat the rate limit. */
        wp_hook_arm(&w->weapon);
    }

    int hooked = (w->weapon.hook_state == HOOK_PULLING);
    float speed = PLAYER_WALK;
    /* Being reeled in, WASD stops driving position entirely. Normal walking
       SETS position every frame (see move_axis), so leaving any of it live
       would let the player simply walk out of a pull the velocity system cannot
       counteract. The claw's flight does not restrict movement at all -- only
       the pull does, and only while it lasts. */
    if (hooked) speed = 0.0f;

    /* This frame's ground speed, which drives the walk bob. Computed before the
       hook resolves, from the wish and the state the frame started in. */
    float move_speed = v3len(wish) * speed * (w->player.grounded ? 1.0f : 0.35f);

    /* The hook resolves BEFORE the player moves. Two reasons, both load
       bearing: the pull's gravity cancellation has to land on the same frame's
       gravity rather than the previous one, and the launch impulse has to be in
       `vel` before player_move integrates it -- otherwise the bounce lags a
       frame behind the arrival that caused it.

       Runs every frame regardless of the button, because flight and pull
       continue on their own once thrown. */
    wp_hook_update(&w->weapon, &w->pools, &w->level, &w->player.pos, &w->player.vel, dt);

    /* Jump is suppressed only while actually being pulled. It would not fire
       mid-pull anyway (jumping needs `grounded`), but suppressing it keeps the
       intent explicit rather than relying on that coincidence. */
    player_move(&w->player, &w->level, wish, speed, in->jump && !hooked, dt);

    /* The leap resolves after player_move, so the slam lands on where the
       player actually ended up rather than a frame behind it -- the same reason
       the hook's constraint runs before the move and this runs after.
       도약은 player_move 이후에 처리되므로, 내려찍기가 한 프레임 뒤가 아니라 플레이어가
       실제로 도달한 지점에서 터집니다. 훅의 구속이 이동 전에 실행되고 이것이 이후에
       실행되는 것과 같은 이유입니다. */
    wp_axe_land(&w->weapon, &w->pools, w->player.pos, w->player.grounded, dt);

    /* Last, so its timers see the movement that actually happened. */
    wp_update(&w->weapon, &w->pools, &w->level, dt, in->fire,
              w->player.pos, w->yaw, w->pitch, move_speed,
              in->look_dx, in->look_dy,
              WORLD_FOV, aspect, &w->player.vel, w->player.grounded);
}

/* ----------------------------------------------------------------- damage */

/**
 * @brief Monsters, hazard floors, and the one place death is noticed.
 *
 * ENGLISH
 * -------
 * @param[in,out] w  The world.
 * @param[in]     dt Timestep in seconds.
 * @note Runs after ::step_look_move, so a monster's swing is measured against
 *       where the player actually ends up this frame. The enemy module owns the
 *       AI; the player's health is owned here.
 *
 * 한국어
 * ------
 * @brief 몬스터, 위험 지형 바닥, 그리고 사망이 감지되는 단 한 곳입니다.
 * @param[in,out] w  월드.
 * @param[in]     dt 시간 간격 (초).
 * @note ::step_look_move 이후에 실행되므로, 몬스터의 공격이 플레이어가 이번 프레임에 실제로
 *       도달한 지점을 기준으로 판정됩니다. AI는 enemy 모듈이 소유하고, 플레이어의 체력은
 *       이곳이 소유합니다.
 */
static void step_damage(World *w, float dt) {
    /* Before anything can play. Everything positional is measured from here, so
       a listener set after the fact would price this frame's sounds by where
       the player stood last frame.
       무엇이든 재생되기 전에 설정합니다. 위치가 있는 모든 소리가 이 지점을 기준으로
       측정되므로, 나중에 설정하면 이번 프레임의 소리가 지난 프레임의 위치로 값이 매겨집니다. */
    audio_listener(w->player.pos);

    int dmg = enemy_update(&w->pools, &w->level, w->player.pos, dt);
    if (dmg > 0 && w->player.health > 0) {
        w->player.health -= dmg;
        if (w->player.health < 0) w->player.health = 0;
        w->player.hurt = 1.0f;
        audio_play("phurt", 90);
    }

    /* --- hazard floors: lava, acid ---------------------------------------
       Charged only while GROUNDED. A hazard is the floor, so jumping over a
       lava channel or being pulled across it by the hook has to be a way
       through -- otherwise the room is a wall rather than an obstacle, and the
       momentum systems this game is built on have nothing to do there.

       The rate is per second and multiplied by dt, so the damage is the same on
       any machine and crossing a corner costs less than standing in the middle.
       Accumulated in a float because a rate below one point per frame would
       otherwise truncate to zero and a slow hazard would be entirely harmless.

       지면에 있을 때만 적용됩니다. 위험 지형은 바닥이므로, 용암 수로를 뛰어넘거나 훅에
       끌려 가로지르는 것이 통과 수단이 되어야 합니다. 그렇지 않으면 그 방은 장애물이 아니라
       벽이 되고, 이 게임이 기반한 운동량 시스템이 그곳에서 할 일이 없어집니다.

       비율은 초당이며 dt를 곱하므로 어떤 기기에서도 피해량이 같고, 모서리를 스쳐 가는 것이
       한가운데 서 있는 것보다 덜 듭니다. float로 누적하는 이유는, 프레임당 1점 미만인
       비율이 그렇지 않으면 0으로 잘려 느린 지형이 완전히 무해해지기 때문입니다. */
    /* THE FEET, and Player::pos is the eye. A sector level ignores the height
       and would not have caught this; a brush level would have measured the
       player's head against a knee-deep pool and found the lava harmless.
       *발*입니다. Player::pos는 눈입니다. 섹터 레벨은 높이를 무시하므로 이것을 잡아내지
       못했을 것이고, 브러시 레벨은 무릎 깊이 웅덩이에 대해 플레이어의 머리를 재고서 용암이
       무해하다고 판정했을 것입니다. */
    int dps = w->player.grounded
            ? level_hazard_at(&w->level, w->player.pos.x,
                              w->player.pos.y - PLAYER_EYE, w->player.pos.z)
            : 0;
    if (dps > 0 && w->player.health > 0) {
        w->run.hazard_accum += dps * dt;
        int whole = (int)w->run.hazard_accum;
        if (whole > 0) {
            w->run.hazard_accum -= (float)whole;
            w->player.health -= whole;
            if (w->player.health < 0) w->player.health = 0;
            /* Held at a low value rather than set to 1: standing in lava should
               read as a constant burn, not as a series of separate hits, and a
               full flash every tick is a strobe.
               1로 설정하지 않고 낮은 값으로 유지합니다. 용암 위에 서 있는 것은 개별적인
               피격의 연속이 아니라 지속적인 화상으로 읽혀야 하며, 틱마다 완전한 섬광은
               스트로브가 됩니다. */
            if (w->player.hurt < 0.45f) w->player.hurt = 0.45f;
        }
        /* Retriggered on a timer for the same reason the hook's reel loop is:
           the mixer has no concept of a sustained sound.
           훅의 감기 루프와 같은 이유로 타이머로 재시작합니다. 믹서에는 지속음 개념이
           없습니다. */
        w->run.burn_timer -= dt;
        if (w->run.burn_timer <= 0.0f) {
            w->run.burn_timer = 0.28f;
            audio_play("phurt", 55);
        }
    } else {
        w->run.hazard_accum = 0.0f;
    }

    /* --- death ------------------------------------------------------------
       One place notices it, whatever emptied the bar -- a swing, a bolt or a
       lava floor. Checking at each damage site instead would mean a new damage
       source could forget to kill the player, and "the health bar sits at zero
       and nothing happens" is a bug that looks like a design decision.
       무엇이 체력을 비웠든(근접 공격, 볼트, 용암 바닥) 한 곳에서 이를 감지합니다. 피해 발생
       지점마다 검사하면 새로운 피해원이 플레이어를 죽이는 것을 잊을 수 있으며, "체력이 0인
       채로 아무 일도 일어나지 않는다"는 설계 판단처럼 보이는 버그입니다. */
    if (!w->run.dead && w->player.health <= 0) {
        w->run.dead = 1;
        w->run.death_time = 0.0f;
        /* The hook would otherwise keep reeling a corpse across the room, and
           the claw is still out there.
           그렇지 않으면 훅이 시체를 방 건너로 계속 끌어당기며, 클로도 아직 밖에 나가
           있습니다. */
        wp_hook_release(&w->weapon);
        wp_hook_arm(&w->weapon);
        audio_play("pdie", 100);
    }
}

/* ------------------------------------------------------------------- smoke */

/**
 * @brief Smoke boiling off the lava, where the lava is actually exposed.
 *
 * ENGLISH
 * -------
 * @param[in,out] w  The world.
 * @param[in]     dt Timestep in seconds.
 *
 * The "actually exposed" part is the whole difficulty -- the safe dais in
 * `vault` sits in the middle of the lava moat, and smoke must not rise through
 * a floor the player is standing on. Both models answer it, by different means
 * and in their own dart function; see ::smoke_dart_sector and
 * ::smoke_dart_brush. What is shared, and all that is shared, is the shape of
 * the loop: throw a few darts, keep the first that lands somewhere visible.
 *
 * Rejection sampling rather than walking the geometry, in both models. A sector
 * is an arbitrary concave outline and a brush is an arbitrary convex solid;
 * picking a uniformly distributed point inside either one properly means
 * triangulating it. Throwing darts at a bounding box and keeping the hits costs
 * a few wasted samples on an L-shaped room and no code at all.
 *
 * @note Giving up after four darts is a budget, not a failure. A puff that
 *       cannot find open lava in four tries is one the player would barely
 *       have seen.
 *
 * 한국어
 * ------
 * @brief 용암이 실제로 *노출된* 곳에서 끓어오르는 연기입니다.
 * @param[in,out] w  월드.
 * @param[in]     dt 시간 간격 (초).
 *
 * "실제로 노출된"이 어려움의 전부입니다. `vault`의 안전한 단상이 용암 해자 한가운데 있고,
 * 연기가 플레이어가 서 있는 바닥을 뚫고 올라와서는 안 됩니다. 두 모델 모두 이에 답하되 방법이
 * 다르며 각자의 다트 함수에 있습니다. ::smoke_dart_sector와 ::smoke_dart_brush를 참조하십시오.
 * 공유되는 것은, 그리고 공유되는 것의 전부는 루프의 형태입니다. 다트를 몇 개 던지고, 보이는
 * 곳에 떨어진 첫 번째를 취합니다.
 *
 * 두 모델 모두 지오메트리를 순회하지 않고 기각 표본 추출을 씁니다. 섹터는 임의의 오목한
 * 외곽선이고 브러시는 임의의 볼록한 고체입니다. 그 어느 쪽이든 내부의 균일 분포 점을 제대로
 * 뽑으려면 삼각분할이 필요합니다. 바운딩 박스에 다트를 던져 맞은 것만 취하면 L자 방에서 표본
 * 몇 개를 낭비할 뿐 코드는 전혀 들지 않습니다.
 *
 * @note 다트 네 개 후에 포기하는 것은 실패가 아니라 예산입니다. 네 번 안에 열린 용암을 찾지
 *       못한 연기는 플레이어가 거의 보지도 못했을 연기입니다.
 */
static float smoke_rand(World *w) {
    w->run.smoke_rng = w->run.smoke_rng * 1664525u + 1013904223u;
    return (w->run.smoke_rng >> 8) * (1.0f / 16777216.0f);
}

static unsigned smoke_pick(World *w, int n) {
    w->run.smoke_rng = w->run.smoke_rng * 1664525u + 1013904223u;
    return (w->run.smoke_rng >> 16) % (unsigned)(n > 0 ? n : 1);
}

/* One dart at a sector level's lava: a point in some hazard sector's bounds,
   kept only if the hazard test still answers there. */
static int smoke_dart_sector(World *w, v3 *out) {
    int si = (int)smoke_pick(w, w->level.n_sectors);
    const Sector *sec = &w->level.sectors[si];
    if (sec->hurt <= 0 || !sec->has_bounds) return 0;

    float fx_ = smoke_rand(w), fz_ = smoke_rand(w);
    float x = (sec->min_x + (sec->max_x - sec->min_x) * fx_) * 0.01f;
    float z = (sec->min_z + (sec->max_z - sec->min_z) * fz_) * 0.01f;

    /* The one question that matters, and it is the same one the damage asks.
       A covered point answers 0.
       중요한 유일한 질문이며 피해가 묻는 것과 같습니다. 덮인 지점은 0을 답합니다. */
    if (level_hazard_at(&w->level, x, 0.0f, z) <= 0) return 0;

    /* The height comes from the sector's floor rather than from the dart, so a
       puff starts ON the lava rather than at eye level above it.
       높이는 다트가 아니라 섹터의 바닥에서 가져오므로, 연기가 용암 위쪽 눈높이가 아니라
       용암 *위에서* 시작합니다. */
    *out = v3f(x, sec->floor * 0.01f + 0.05f, z);
    return 1;
}

/* The same dart at a brush level, where the surface is a face rather than a
 * number and "covered" is a thing you can trace for.
 *
 * ENGLISH: A ::HazardDef's brushes are not solid, so a downward probe passes
 * straight through the lava and stops on whatever is genuinely underfoot: the
 * pit floor where the lava is open, and the dais where a dais covers it. That
 * comparison IS the exposure test, and it needs no second rule to keep in step
 * with the damage -- which is what the sector model's last-declared-wins was
 * doing the hard way.
 *
 * The dart is thrown at the brush's bounding box and then asked whether it is
 * really inside, for the same reason the sector path throws at a bounding box:
 * a lava surface may be sloped or clipped, and rejection costs a few samples
 * where getting it exactly right costs a face walk.
 *
 * 한국어: ::HazardDef의 브러시는 고체가 아니므로 하강 탐침이 용암을 그대로 통과해 실제로
 * 발밑에 있는 것에서 멈춥니다. 용암이 열려 있으면 구덩이 바닥이고, 단상이 덮고 있으면
 * 단상입니다. 그 비교가 곧 노출 판정이며, 피해와 동기화를 유지할 두 번째 규칙이 필요 없습니다.
 * 섹터 모델의 "마지막 선언 우선"이 어렵게 하고 있던 일이 그것입니다.
 *
 * 다트를 브러시의 바운딩 박스에 던진 뒤 실제로 안에 있는지 묻는 이유는 섹터 경로가 바운딩
 * 박스에 던지는 이유와 같습니다. 용암 표면은 기울어 있거나 잘려 있을 수 있고, 기각은 표본
 * 몇 개를 들이는 반면 정확히 맞히려면 면을 순회해야 합니다.
 */
static int smoke_dart_brush(World *w, v3 *out) {
    const Level *l = &w->level;
    if (l->n_hazards <= 0) return 0;

    const HazardDef *h = &l->hazards[smoke_pick(w, l->n_hazards)];
    if (h->n_brushes < 1) return 0;
    const Brush *b = &l->brushes->brushes[h->first_brush +
                                          (int)smoke_pick(w, h->n_brushes)];
    if (b->min.x > b->max.x) return 0;       /* a brush with no volume */

    float fx_ = smoke_rand(w), fz_ = smoke_rand(w);
    float x = b->min.x + (b->max.x - b->min.x) * fx_;
    float z = b->min.z + (b->max.z - b->min.z) * fz_;
    float top = b->max.y;

    /* Just under the top face, so a sloped or clipped surface rejects the darts
       that missed it.
       윗면 바로 아래입니다. 기울거나 잘린 표면은 빗나간 다트를 기각합니다. */
    if (level_hazard_at(l, x, top - LAVA_SMOKE_LIP, z) <= 0) return 0;

    /* What is actually underfoot here. Higher than the lava means something
       solid is standing on it, and smoke must not rise through a floor the
       player is standing on.
       이곳의 발밑에 실제로 있는 것입니다. 용암보다 높다면 고체가 그 위에 서 있는 것이고,
       연기는 플레이어가 딛고 선 바닥을 뚫고 올라와서는 안 됩니다. */
    float floor_y, ceil_y;
    if (!level_ground(l, x, z, top, 1e9f, &floor_y, &ceil_y)) return 0;
    if (floor_y > top + LAVA_SMOKE_LIP) return 0;

    *out = v3f(x, top + LAVA_SMOKE_LIP, z);
    return 1;
}

static void step_smoke(World *w, float dt) {
    w->run.smoke_timer -= dt;
    if (w->run.smoke_timer > 0.0f) return;
    w->run.smoke_timer = LAVA_SMOKE_INTERVAL;

    for (int s = 0; s < LAVA_SMOKE_PER_TICK; s++) {
        /* One dart per attempt, a few attempts per puff. Giving up is correct:
           a level with no lava should cost nothing here, and a hazard that is
           almost entirely covered should emit almost nothing.
           시도마다 다트 하나, 연기 하나당 몇 번의 시도입니다. 포기하는 것이 옳습니다.
           용암이 없는 레벨은 이곳에서 비용이 들지 않아야 하고, 거의 전부 덮인 위험 지형은
           거의 아무것도 내뿜지 않아야 합니다. */
        for (int tries = 0; tries < 4; tries++) {
            v3 at;
            if (!(w->level.brushes ? smoke_dart_brush(w, &at)
                                   : smoke_dart_sector(w, &at))) continue;

            /* Far-off smoke is invisible and still costs a slot in a shared
               pool, so it is not spawned at all.
               먼 곳의 연기는 보이지 않으면서 공유 풀의 슬롯을 차지하므로 아예 생성하지
               않습니다. */
            float dx = at.x - w->player.pos.x, dz = at.z - w->player.pos.z;
            if (dx*dx + dz*dz > LAVA_SMOKE_RANGE * LAVA_SMOKE_RANGE) continue;

            fx_spawn(&w->pools, "lavasmoke", at, v3f(0.0f, 1.0f, 0.0f));
            break;
        }
    }
}

/* -------------------------------------------------------------------- exit */

/**
 * @brief Reaching the exit: the next level, or the end of the game.
 *
 * ENGLISH
 * -------
 * @param[in,out] w The world.
 * @note Health and ammo carry over on a transition, the way a Doom episode runs
 *       -- the exit is a reward you arrive at, not a reset. A level with no
 *       `next` is terminal, so its exit ends the game.
 * @note `w->level.next` is handed straight to ::world_load_level, which copies
 *       the name before it parses. This used to need a local buffer here,
 *       because ::level_load blanks the destination's `next` field and would
 *       have erased its own search string mid-call.
 *
 * 한국어
 * ------
 * @brief 출구 도달. 다음 레벨 또는 게임의 끝입니다.
 * @param[in,out] w 월드.
 * @note 전환 시 체력과 탄약이 이어집니다. Doom 에피소드가 진행되는 방식이며, 출구는 도달하는
 *       보상이지 초기화가 아닙니다. `next`가 없는 레벨은 종착지이므로 그 출구가 게임을
 *       끝냅니다.
 * @note `w->level.next`를 ::world_load_level에 그대로 넘깁니다. 그 함수가 파싱 전에 이름을
 *       복사합니다. 이전에는 이곳에 지역 버퍼가 필요했습니다. ::level_load가 대상의 `next`
 *       필드를 비우므로 호출 도중에 자기 검색 문자열을 지웠기 때문입니다.
 */
/* The jump pad, applied where the player is standing.
 *
 * ONLY WHILE GROUNDED, and that -- not the assignment below -- is what makes
 * the height fixed. It is a correctness requirement rather than a design
 * choice.
 * The pad is found by an x/z test, and a player launched straight up stays
 * inside that radius for the whole ascent: without the ground test the pad
 * would re-set the velocity every frame, cancelling gravity, and hold them
 * rising at launch speed until they drifted sideways off it. Requiring contact
 * makes it fire once per landing, which is also what a pad you step on means.
 *
 * 플레이어가 서 있는 자리에 적용되는 점프대입니다. 높이를 고정시키는 것은 아래의 대입이
 * 아니라 *접지 조건*입니다. 설계 선택이 아니라 정확성 문제입니다. 점프대는 x/z
 * 판정으로 찾는데, 곧장 위로 발사된 플레이어는 상승 내내 그 반경 안에 머뭅니다. 접지
 * 검사가 없으면 점프대가 매 프레임 속도를 다시 설정해 중력을 상쇄하고, 옆으로 벗어날
 * 때까지 발사 속력으로 계속 올라갑니다. 접촉을 요구하면 착지마다 한 번 발동하며, 그것이
 * 밟는 점프대의 의미이기도 합니다. */
/**
 * @brief What a keypress means on the screen the player is looking at.
 *
 * ENGLISH
 * -------
 * @param[in,out] w The run being acknowledged.
 *
 * The window procedure used to answer this, which put two rules -- and the
 * delay that makes one of them bearable -- somewhere no test could reach. It
 * asked the same three questions in the same order; what changed is where.
 *
 * @note The order is the precedence. `title` and `dead` cannot both be set,
 *       but writing it as a chain says which would win if they ever were,
 *       instead of leaving that to whichever `if` was typed first.
 * @note Reports a restart rather than performing one: reloading the level
 *       rebuilds its geometry, and the caller has a menu to close and a cursor
 *       to re-decide once `dead` has changed. Same division ::door_update makes
 *       when it returns "something moved".
 *
 * 한국어
 * ------
 * @brief 플레이어가 보고 있는 화면에서 키 입력이 무엇을 뜻하는지.
 * @param[in,out] w 응답을 받는 플레이.
 *
 * 이전에는 창 프로시저가 이 답을 내렸고, 그것이 두 개의 규칙과 그중 하나를 견딜 만하게
 * 만드는 지연을 어떤 테스트도 닿을 수 없는 곳에 두었습니다. 같은 세 질문을 같은 순서로
 * 물었으며, 달라진 것은 장소입니다.
 *
 * @note 순서가 곧 우선순위입니다. `title`과 `dead`는 동시에 설정될 수 없지만, 사슬로 쓰면
 *       혹시 그렇게 되더라도 어느 쪽이 이기는지를 말해 줍니다. 어느 `if`를 먼저 썼는지에
 *       맡기지 않습니다.
 * @note 재시작을 수행하지 않고 *보고*합니다. 레벨을 다시 로드하는 것은 지오메트리를 다시
 *       만드는 일이고, 호출자에게는 닫을 메뉴와 `dead`가 바뀐 뒤에 다시 결정할 커서가
 *       있습니다. ::door_update가 "무언가 움직였다"를 반환하는 것과 같은 구분입니다.
 */
static void step_confirm(World *w) {
    if (w->run.title) {
        w->run.title = 0;
        return;
    }

    /* The grace period, and the whole reason this is not simply "any key".
       The shot that killed the player is very often still held down, and
       restarting on it reads as the game skipping the death screen entirely.
       유예 시간이며, 이것이 단순한 "아무 키"가 아닌 이유 전부입니다. 플레이어를 죽인 그
       사격은 대개 아직 눌린 상태이고, 그것으로 재시작되면 게임이 사망 화면을 통째로 건너뛴
       것처럼 보입니다. */
    if (w->run.dead && w->run.death_time > DEATH_INPUT_DELAY)
        w->run.restart_wanted = 1;
}

/**
 * @brief Puts a weapon in the player's hand, if they have it.
 *
 * ENGLISH
 * -------
 * @param[in,out] w    The run whose belt is being reached into.
 * @param[in]     want Weapon index, already converted from ::Input::want_weapon.
 *
 * @note Silently refuses a weapon that is not owned, and refuses the one
 *       already in hand. An empty hand is worse than nothing happening, and
 *       re-selecting the current weapon would restart its draw sound every
 *       press.
 *
 * 한국어
 * ------
 * @brief 플레이어가 그 무기를 가지고 있다면 손에 쥐여 줍니다.
 * @param[in,out] w    탄약대에 손을 뻗는 플레이.
 * @param[in]     want 무기 인덱스이며 ::Input::want_weapon에서 이미 변환된 값입니다.
 *
 * @note 보유하지 않은 무기와 이미 손에 든 무기를 조용히 거절합니다. 빈손은 아무 일도
 *       일어나지 않는 것보다 나쁘고, 이미 든 무기를 다시 고르면 누를 때마다 뽑기 사운드가
 *       처음부터 다시 납니다.
 */
static void step_weapon_pick(World *w, int want) {
    if (want < 0 || want >= WP_TYPES)  return;
    if (!w->weapon.owned[want])        return;
    if (w->weapon.cur == want)         return;

    w->weapon.cur = want;

    /* A switch cancels a swing in progress rather than carrying its timer
       across: the axe's dash must not be inherited by the shotgun.
       진행 중인 공격을 이월하지 않고 취소합니다. 도끼의 대쉬를 샷건이 물려받아서는
       안 됩니다. */
    w->weapon.cooldown = 0.0f;

    /* A weapon that announces itself does so on the SWITCH rather than on the
       first swing: the saw revving up is what tells the player the change took,
       and hearing it only once they attack makes the switch itself feel
       unacknowledged.
       자기를 알리는 무기는 첫 공격이 아니라 *전환 시점*에 그렇게 합니다. 톱이 돌기 시작하는
       소리가 전환이 먹혔다는 것을 알려 주며, 공격할 때에야 들린다면 전환 자체가 응답 없는
       것처럼 느껴집니다. */
    const char *dsnd = wp_stats(want)->draw_snd;
    if (dsnd) audio_play(dsnd, 85);
}

static void step_push(World *w) {
    if (!w->player.grounded) return;

    float sp = level_push_at(&w->level, w->player.pos.x, w->player.pos.z);
    if (sp <= 0.0f) return;

    w->player.vel.y   = sp;
    w->player.grounded = 0;
    audio_play_at("hland", 85, w->player.pos);
}

static void step_exit(World *w) {
    if (!level_exit_at(&w->level, w->player.pos.x, w->player.pos.z)) return;

    if (!w->level.next[0]) {
        /* Terminal level: the exit goes nowhere, so this is the end. */
        w->run.won = 1;
        audio_play("win", 100);
        return;
    }

    /* ALREADY BETWEEN LEVELS: the exit is still under the player's feet and
       level_exit_at keeps saying so, so without this the intermission would
       re-arm itself every frame and its timer would never advance.
       이미 레벨 사이입니다. 출구는 여전히 플레이어 발밑에 있고 level_exit_at은 계속 그렇게
       말하므로, 이것이 없으면 인터미션이 매 프레임 자신을 다시 세우고 타이머가 결코
       진행되지 않습니다. */
    if (w->run.between) return;

    /* The screen goes up; the level does NOT load yet. Loading here and
       showing the names afterwards would put the intermission over the level
       it is announcing, and the player would be reading "ENTERING VAULT" while
       standing in the vault.
       화면이 뜨고 레벨은 아직 로드되지 *않습니다*. 여기서 로드하고 이름을 나중에 보여
       주면 인터미션이 자신이 알리는 그 레벨 위에 뜨게 되고, 플레이어는 금고 안에 서서
       "ENTERING VAULT"를 읽게 됩니다. */
    w->run.between      = 1;
    w->run.between_time = 0.0f;
    txt_copy(w->run.cleared,  sizeof w->run.cleared,  w->level.name, -1);
    txt_copy(w->run.entering, sizeof w->run.entering, w->level.next, -1);
    audio_play("exit", 90);
}

/* How long the names are up before the level behind them loads.
 *
 * A DURATION RATHER THAN A KEYPRESS. Doom's intermission waits for one, and
 * Doom's intermission has tallies to read; ours has two names, and a prompt to
 * dismiss two names is ceremony around nothing. Long enough to read them,
 * short enough that it does not become the thing between the player and the
 * next fight.
 * 키 입력이 아니라 시간입니다. Doom의 인터미션은 입력을 기다리지만 그것에는 읽을 집계가
 * 있습니다. 우리 것에는 이름 둘뿐이고, 이름 둘을 넘기기 위한 안내는 아무것도 아닌 것을
 * 둘러싼 의식입니다. 읽을 만큼 길고, 플레이어와 다음 전투 사이를 가로막는 것이 되지 않을
 * 만큼 짧습니다. */


/* Advances the between-levels screen, and loads when it is done.
 *
 * The load is here rather than in step_exit so there is exactly one place that
 * changes which level is running -- see world_load_level's own note on that.
 * 로드가 step_exit이 아니라 이곳에 있는 이유는, 어느 레벨이 도는지를 바꾸는 곳이 정확히
 * 하나이도록 하기 위해서입니다. */
static void step_between(World *w, float dt) {
    if (!w->run.between) return;

    w->run.between_time += dt;
    if (w->run.between_time < WORLD_BETWEEN_TIME) return;

    w->run.between = 0;

    /* An unknown target changes nothing and leaves the player where they are;
       a typo does not win the game. */
    world_load_level(w, w->run.entering, WORLD_ENTER_CARRY);
}

/* --------------------------------------------------------------------- api */

/* Progress first, because it is the vocabulary the rest of this file speaks:
   world_init seeds a checkpoint with it, world_load_level chooses one, and
   world_progress_for_stage builds one from the level chain.
   진행 상태를 먼저 둡니다. 이 파일의 나머지가 사용하는 어휘이기 때문입니다. world_init이
   이것으로 체크포인트를 심고, world_load_level이 그중 하나를 고르며,
   world_progress_for_stage가 레벨 사슬로부터 하나를 만듭니다. */

/* ---------------------------------------------------------------- progress */

/* The three lists that have to agree -- the struct's fields and the two
   functions below -- are all within twenty lines of each other, and this is
   what makes disagreeing a compile error. Add `int armour;` to PlayerProgress
   and the size changes, the assert fires, and its message names the two
   functions that now need a line each.
   It checks the SIZE rather than the field count because that is the thing C
   will tell us: a struct of nothing but ints has no padding, so the arithmetic
   is exact, and a field of any other type changes it too.

   서로 일치해야 하는 세 목록(구조체의 필드와 아래 두 함수)이 모두 서로 20줄 안에 있으며,
   불일치를 컴파일 오류로 만드는 것이 이것입니다. PlayerProgress에 `int armour;`를 추가하면
   크기가 바뀌고 어서션이 발동하며, 그 메시지가 이제 한 줄씩 필요한 두 함수의 이름을
   말해 줍니다.
   필드 개수가 아니라 *크기*를 검사하는 이유는 C가 알려 줄 수 있는 것이 그것이기 때문입니다.
   int만으로 이루어진 구조체에는 패딩이 없으므로 산술이 정확하고, 다른 타입의 필드가 들어와도
   크기가 바뀝니다. */
_Static_assert(sizeof(PlayerProgress) == sizeof(int) * (3 + 2 * WP_TYPES),
               "PlayerProgress gained or lost a field. Teach world_progress_read "
               "and world_progress_write about it, then update this assert.");

void world_progress_read(const World *w, PlayerProgress *out) {
    out->health = w->player.health;
    out->keys   = w->player.keys;
    out->cur    = w->weapon.cur;
    for (int i = 0; i < WP_TYPES; i++) {
        out->ammo[i]  = w->weapon.ammo[i];
        out->owned[i] = w->weapon.owned[i];
    }
}

void world_progress_write(World *w, const PlayerProgress *p) {
    w->player.health = p->health;
    w->player.keys   = p->keys;
    w->weapon.cur    = p->cur;
    for (int i = 0; i < WP_TYPES; i++) {
        w->weapon.ammo[i]  = p->ammo[i];
        w->weapon.owned[i] = p->owned[i];
    }
}

/* What a new game starts with. Assembled by asking the weapon module rather
   than by naming a shotgun and twenty shells here: WEAPON_START_AMMO lives
   beside wp_start_belt, and a second copy of the starting belt would be a
   starting belt that drifts from the one the game boots with.
   새 게임이 시작하는 상태입니다. 이곳에서 샷건과 탄환 20발을 지명하지 않고 무기 모듈에게
   물어서 조립합니다. WEAPON_START_AMMO는 wp_start_belt 옆에 있으며, 시작 탄약대의 두 번째
   사본은 게임이 부팅하는 것과 어긋나는 시작 탄약대가 됩니다. */
static void progress_boot(PlayerProgress *out) {
    Weapon belt = {0};
    wp_start_belt(&belt);

    out->health = PLAYER_MAX_HP;
    out->keys   = KEY_NONE;
    out->cur    = belt.cur;
    for (int i = 0; i < WP_TYPES; i++) {
        out->ammo[i]  = belt.ammo[i];
        out->owned[i] = belt.owned[i];
    }
}

void world_init(World *w) {
    World zero = {0};
    *w = zero;

    /* The declared spawn, overwritten by player_spawn on the first load. Here
       so that a World is never observably uninitialised -- a caller that reads
       the player before loading a level gets a standing body at full health
       rather than one at the origin with no hit points.
       선언된 스폰 지점이며 첫 로드 시 player_spawn이 덮어씁니다. World가 관측 가능하게
       미초기화된 상태를 갖지 않도록 이곳에 둡니다. 레벨을 로드하기 전에 플레이어를 읽는
       호출자는 원점에 체력 0으로 있는 몸이 아니라 체력이 가득한 서 있는 몸을 받습니다. */
    w->player.pos    = v3f(0.0f, PLAYER_EYE, 12.0f);
    w->player.health = PLAYER_MAX_HP;
    w->player.keys   = KEY_NONE;

    txt_copy(w->cur_level, sizeof(w->cur_level), WORLD_START_LEVEL, -1);

    /* A checkpoint for a stage that has not been loaded yet. Zeroing a World
       would leave it at no health and no weapons, and a ::WORLD_ENTER_REPLAY
       against that -- a restart asked for before anything was loaded -- would
       spawn a corpse. The boot belt is the honest answer to "what would you
       have been carrying", and the first real load overwrites it anyway.
       아직 로드되지 않은 스테이지를 위한 체크포인트입니다. World를 0으로 두면 체력도 무기도
       없는 상태가 되고, 그에 대한 ::WORLD_ENTER_REPLAY(무엇도 로드되기 전에 요청된 재시작)는
       시체를 스폰합니다. "무엇을 들고 있었겠는가"에 대한 정직한 답은 부팅 구성이며, 첫 실제
       로드가 어차피 이것을 덮어씁니다. */
    progress_boot(&w->entry);

    /* title=1: a fresh World comes up on the title screen, and the same call
       seeds the smoke rng and zeroes every clock. Startup and a restart
       therefore begin from a state produced by one function rather than by a
       declaration here and an assignment there -- which is how the two drifted
       apart in the first place. See ::RunState.
       title=1입니다. 새 World는 타이틀 화면으로 시작하며, 같은 호출이 연기 난수의 시드를
       설정하고 모든 시계를 0으로 만듭니다. 따라서 시작과 재시작은 이곳의 선언과 저곳의
       대입이 아니라 하나의 함수가 만들어 낸 상태에서 출발합니다. 애초에 그 둘이 어긋난
       이유가 바로 그것이었습니다. ::RunState를 참조하십시오. */
    run_reset(&w->run, 1);
    /* The weapon, which world_init could not touch while weapon.c needed a GL
       context to load its model. It does not any more -- the drawn gun is
       ::WeaponView now -- so the World brings up the whole of itself and the
       "call wp_init after the context and before the first load" rule that
       three files used to repeat has nothing left to warn about.
       무기입니다. weapon.c가 모델을 로드하기 위해 GL 컨텍스트를 필요로 하는 동안에는
       world_init이 이것을 건드릴 수 없었습니다. 이제는 그렇지 않으므로(그려지는 총은
       ::WeaponView입니다) World가 자기 자신 전부를 준비하며, 세 개의 파일이 반복하던
       "컨텍스트 이후, 첫 로드 이전에 wp_init을 호출하라"는 규칙은 경고할 대상이
       없어졌습니다. */
    wp_init(&w->weapon);

}

/* ------------------------------------------------------------ level loading */

int world_load_level(World *w, const char *name, WorldEnter how) {
    /* Copied before anything is parsed. `name` may be w->cur_level, and it may
       be w->level.next -- which level_load blanks before it parses, so a name
       read out of it mid-call would be an empty string. One copy here removes
       the hazard instead of leaving it as a rule every caller has to know.
       파싱 전에 복사합니다. `name`은 w->cur_level일 수도 있고 w->level.next일 수도
       있습니다. 후자는 level_load가 파싱 전에 비우므로, 호출 도중에 그곳에서 읽은 이름은 빈
       문자열이 됩니다. 이곳의 복사 한 번이, 모든 호출자가 알아야 하는 규칙 대신 위험 자체를
       없앱니다. */
    char want[WORLD_LEVEL_MAX];
    txt_copy(want, sizeof(want), name, -1);

    /* Parsed first, and nothing else is touched until it succeeds. An unknown
       target -- a typo, or a half-authored map -- must leave the player where
       they are rather than dropping them into a void.
       먼저 파싱하며, 성공하기 전까지는 아무것도 건드리지 않습니다. 알 수 없는 대상(오타
       또는 절반만 제작된 맵)은 플레이어를 빈 공간에 떨어뜨리는 대신 있던 자리에 두어야
       합니다. */
    if (!level_load(want, &w->level)) return 0;

    /* The sectors just changed, so the mesh built from them is stale. Raised
       here rather than rebuilt here: this module does not know what a Scene is.
       ALL of it, and this is the site that has to say so: a new level shares
       nothing with the one before it, so the static half is stale too and a
       partial rebuild would leave the previous level's walls on screen.
       See ::World::geometry_dirty. */
    w->geometry_dirty = WORLD_GEOM_ALL;

    /* What the player keeps, taken before the spawn and put back after it. Read
       unconditionally, because reading is free and a branch here would be a
       second place ::WorldEnter is interpreted.
       플레이어가 가져가는 것입니다. 스폰 전에 가져오고 스폰 후에 되돌려 놓습니다. 조건 없이
       읽는 이유는 읽기가 공짜이고, 이곳의 분기는 `carry_state`가 의미를 갖는 두 번째 장소가
       되기 때문입니다. */
    PlayerProgress carried;
    world_progress_read(w, &carried);

    w->yaw   = player_spawn(&w->player, &w->level);   /* resets health */
    w->pitch = 0.0f;

    /* --- where the belt comes from ---------------------------------------
       player_spawn has just reset health, and health is the ONLY thing it
       resets: the belt and the keycards have nobody but this switch. Every arm
       writes the whole progress, so none of them can leave half of it behind
       from whoever was playing a moment ago.
       player_spawn이 방금 체력을 초기화했으며, 그것이 초기화하는 것은 체력뿐입니다. 탄약대와
       키카드에는 이 switch 외에 아무도 없습니다. 모든 갈래가 진행 상태 전체를 쓰므로, 어느
       것도 조금 전에 플레이하던 사람의 절반을 남겨 둘 수 없습니다. */
    PlayerProgress start;
    switch (how) {
    case WORLD_ENTER_CARRY:
        start = carried;      /* the exit is a reward you arrive at */
        break;
    case WORLD_ENTER_REPLAY:
        start = w->entry;     /* put me back where I started this stage */
        break;
    case WORLD_ENTER_NEW:
    default:
        progress_boot(&start);
        break;
    }
    world_progress_write(w, &start);

    /* Whatever the arm above decided, the player is now standing in this stage
       holding it -- and THAT is the checkpoint a restart of this stage
       restores. Read back out of the world rather than copied from `start`,
       because the world is what the player actually has: if anything between
       here and the write ever adjusted the belt, the checkpoint would follow it
       instead of describing a state that no longer exists.
       위의 갈래가 무엇을 결정했든, 플레이어는 이제 그것을 들고 이 스테이지에 서 있습니다.
       그리고 *그것이* 이 스테이지의 재시작이 복원하는 체크포인트입니다. `start`에서 복사하지
       않고 월드에서 다시 읽는 이유는, 플레이어가 실제로 가진 것은 월드이기 때문입니다.
       이곳과 쓰기 사이에서 무엇이든 탄약대를 조정한다면, 체크포인트는 더 이상 존재하지 않는
       상태를 서술하는 대신 그것을 따라가야 합니다. */
    world_progress_read(w, &w->entry);

    /* Monsters and pickups are placed from the level's own entities. */
    enemy_spawn_level(&w->pools, &w->level);
    pickup_spawn_level(&w->pools, &w->level);

    /* After the sectors exist and before the first door_update: door.c copies
       each door's CLOSED shape here, and it can only do that while the level
       still holds it.
       섹터가 존재한 뒤, 첫 door_update 이전입니다. door.c가 각 문의 *닫힌* 형상을 이곳에서
       복사하며, 레벨이 아직 그것을 담고 있는 동안에만 가능합니다. */
    door_reset(&w->level);
    proj_reset(&w->pools);

    /* A bullet hole belongs to the wall it was shot into, and that wall is
       gone. These outlived a level change before -- six seconds of the last
       map's firefight hanging in the air of the next one, which is brief enough
       that nobody filed it and wrong enough that nobody would have defended it.
       탄흔은 그것이 박힌 벽에 속하며, 그 벽은 사라졌습니다. 이전에는 레벨 전환을 넘어
       살아남았습니다. 지난 맵 교전의 6초가 다음 맵의 허공에 떠 있었던 것인데, 아무도 신고하지
       않을 만큼 짧고 아무도 옹호하지 않을 만큼 잘못된 상태였습니다. */
    decal_reset(&w->pools);

    /* A hook mid-flight belongs to the level that is being left. Carrying it
       across would reel the player toward an anchor in geometry that no longer
       exists, and toward a monster index that now means somebody else.
       비행 중인 훅은 떠나는 레벨에 속합니다. 이를 이어 가면 더 이상 존재하지 않는
       지오메트리의 앵커로, 그리고 이제 다른 대상을 가리키는 몬스터 인덱스로 플레이어를
       끌어당기게 됩니다. */
    wp_hook_release(&w->weapon);
    wp_hook_arm(&w->weapon);

    /* Last, and from the copy: this is the level the world is now in, and it is
       what a restart and a hot reload reload. */
    txt_copy(w->cur_level, sizeof(w->cur_level), want, -1);
    return 1;
}

/* --------------------------------------------------- starting part way in */

/**
 * @brief Are two NUL-terminated level names the same?
 *
 * ENGLISH: txt_is compares a counted token to a literal, which is what the
 * parsers need; both sides here are already whole strings.
 *
 * 한국어: txt_is는 길이가 주어진 토큰을 리터럴과 비교하며, 파서가 필요로 하는 형태가
 * 그것입니다. 이곳의 양쪽은 이미 완전한 문자열입니다.
 */
static int name_same(const char *a, const char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

/**
 * @brief Grants every weapon `l` hands out, into an in-progress belt.
 *
 * ENGLISH
 * -------
 * @param[in]     l   A stage to read the pickups of.
 * @param[in,out] out Weapons found are marked owned. Nothing is taken away.
 *
 * @note Asked of the stage's ENTITIES rather than of a table of "what stage two
 *       gives you". The entities are what the stage actually contains; a table
 *       is a second answer that goes stale the first time somebody moves a
 *       weapon between maps in the editor.
 * @note Accumulates -- never clears -- because that is what carrying a weapon
 *       through an episode means. See ::world_progress_for_stage.
 *
 * 한국어
 * ------
 * @brief `l`이 내주는 모든 무기를 작성 중인 탄약대에 부여합니다.
 * @param[in]     l   아이템을 읽어 낼 스테이지.
 * @param[in,out] out 발견된 무기를 보유 상태로 표시합니다. 무엇도 회수하지 않습니다.
 *
 * @note "2스테이지가 무엇을 주는가"라는 표가 아니라 스테이지의 *엔티티*에게 묻습니다.
 *       엔티티는 그 스테이지가 실제로 담고 있는 것이고, 표는 누군가 에디터에서 무기를 다른
 *       맵으로 옮기는 순간 낡아 버리는 두 번째 답입니다.
 * @note 지우지 않고 *누적*합니다. 에피소드 전체에 걸쳐 무기를 지니고 다닌다는 것이 뜻하는
 *       바가 그것입니다. ::world_progress_for_stage를 참조하십시오.
 */
static void grant_weapons_of(const Level *l, PlayerProgress *out) {
    for (int i = 0; i < l->n_ents; i++) {
        const char *k = l->ents[i].kind;
        int n = 0;
        while (n < LVL_MAT && k[n]) n++;

        int wp = PK_WEAPON_WEAPON(pickup_kind_for_n(k, n));
        if (wp >= 0 && wp < WP_TYPES) out->owned[wp] = 1;
    }
}

/**
 * @brief The walk itself, so its four exits all get the release below.
 *
 * ENGLISH
 * -------
 * @param[in]     name Stage to walk to.
 * @param[out]    out  The belt on arrival; untouched unless 1 is returned.
 * @param[in,out] scan Scratch level, reused for every hop.
 *
 * @note Split out for one reason: this function leaves by four different
 *       returns and the scratch level has to be released on every one of them.
 *       A release before each return is four chances to add a fifth return and
 *       forget; a wrapper is one place that cannot be forgotten.
 *
 * 한국어
 * ------
 * @brief 순회 자체이며, 네 개의 출구 모두가 아래의 반납을 거치도록 분리했습니다.
 * @param[in]     name 걸어갈 스테이지.
 * @param[out]    out  도착 시점의 탄약대. 1을 반환할 때만 기록됩니다.
 * @param[in,out] scan 모든 구간에 재사용하는 임시 레벨.
 *
 * @note 분리한 이유는 하나입니다. 이 함수는 서로 다른 네 개의 return으로 빠져나가고, 임시
 *       레벨은 그 전부에서 반납되어야 합니다. return마다 반납을 두는 것은 다섯 번째 return을
 *       추가하며 잊을 기회가 네 번 있다는 뜻이고, 감싸는 함수는 잊을 수 없는 한 곳입니다.
 */
static int stage_walk(const char *name, PlayerProgress *out, Level *scan) {
    PlayerProgress p;
    progress_boot(&p);

    char at[WORLD_LEVEL_MAX];
    txt_copy(at, sizeof(at), WORLD_START_LEVEL, -1);

    /* Bounded because `next` is authored text and may point backwards. A cycle
       would otherwise walk for ever, and a stage-select menu is not a place to
       hang.
       `next`가 제작된 텍스트이고 뒤를 가리킬 수도 있으므로 상한을 둡니다. 그렇지 않으면
       순환이 영원히 돌게 되며, 스테이지 선택 메뉴는 멈춰 있을 자리가 아닙니다. */
    for (int hop = 0; hop < WORLD_STAGE_MAX_HOPS; hop++) {
        /* Reached it. Everything before it has already been folded in, and the
           target's own weapons are deliberately NOT -- they are what the player
           is about to go and find.
           도달했습니다. 그 이전의 모든 것은 이미 접혀 들어갔고, 대상 자신의 무기는
           의도적으로 포함하지 *않습니다*. 그것은 플레이어가 이제 가서 찾을 것입니다. */
        if (name_same(at, name)) {
            /* Half of what each belt holds, for every weapon the walk granted.
               Applied at the end rather than per stage, so a weapon that
               appears in two stages is not counted twice.
               순회가 부여한 모든 무기에 대해 각 탄약대 용량의 절반입니다. 스테이지마다가
               아니라 마지막에 적용하므로, 두 스테이지에 등장하는 무기가 두 번 세어지지
               않습니다. */
            for (int i = 0; i < WP_TYPES; i++)
                p.ammo[i] = p.owned[i] ? wp_stats(i)->max_ammo / 2 : 0;

            *out = p;
            return 1;
        }

        if (!level_load(at, scan)) return 0;    /* a broken link in the chain */

        grant_weapons_of(scan, &p);

        if (!scan->next[0]) return 0;           /* the chain ended first */
        txt_copy(at, sizeof(at), scan->next, -1);
    }

    return 0;   /* longer than any episode, or a loop */
}

int world_progress_for_stage(const char *name, PlayerProgress *out) {
    /* One scratch level, reused for every hop. Zeroed once: level_load resets
       the counts and names it fills, so nothing stale is ever read, but a
       19KB local that begins as whatever was on the stack is a bad habit to
       leave lying around next to a parser.
       모든 구간에 재사용하는 임시 레벨 하나입니다. 한 번만 0으로 만듭니다. level_load가
       자신이 채우는 개수와 이름을 초기화하므로 낡은 값을 읽을 일은 없지만, 스택에 있던
       무엇으로 시작하는 19KB 지역 변수를 파서 옆에 남겨 두는 것은 나쁜 습관입니다. */
    Level scan = {0};

    int found = stage_walk(name, out, &scan);

    /* GIVEN BACK BEFORE THIS FRAME ENDS, and this is the call the whole brush
       slot change was made for. `scan` is a stack local that is about to stop
       existing; there are ::LVL_BRUSH_SLOTS of them and the running level holds
       one, so a scan that walked away with the other left the pool full for the
       rest of the process. The old pool keyed slots by a `Level *` and would
       have handed this address out again on the next call, which is how a dead
       Level went on owning storage -- see ::Level::brush_key.
       이번 프레임이 끝나기 전에 돌려주며, 브러시 슬롯 변경 전체가 이 호출을 위해 이루어졌습니다.
       `scan`은 곧 존재하기를 그만둘 스택 지역 변수입니다. 슬롯은 ::LVL_BRUSH_SLOTS개뿐이고
       실행 중인 레벨이 하나를 쥐고 있으므로, 나머지 하나를 들고 떠난 스캔은 프로세스가 끝날
       때까지 풀을 가득 찬 채로 남겼습니다. 이전 풀은 슬롯을 `Level *`로 키잉했고 다음 호출에서
       이 주소를 다시 내주었을 것이며, 그것이 죽은 Level이 저장 공간을 계속 소유하던
       방식입니다. ::Level::brush_key를 참조하십시오. */
    level_release(&scan);
    return found;
}

int world_start_stage(World *w, const char *name) {
    PlayerProgress want;
    if (!world_progress_for_stage(name, &want)) return 0;

    /* Seed the checkpoint and enter as a replay of it. That is one mechanism
       doing two jobs: the player starts the stage with this belt, and a restart
       of the stage puts them back with it rather than with the boot one.
       The old checkpoint is kept until the load has succeeded, because a stage
       that does not load must change nothing -- the same contract
       world_load_level keeps about the level itself.
       체크포인트를 심고 그것의 재생으로 진입합니다. 하나의 기구가 두 가지 일을 합니다.
       플레이어는 이 탄약대로 스테이지를 시작하고, 그 스테이지를 재시작하면 부팅 구성이
       아니라 같은 것으로 되돌아갑니다.
       로드가 성공하기 전까지 이전 체크포인트를 보관하는 이유는, 로드되지 않는 스테이지는
       아무것도 바꾸지 말아야 하기 때문입니다. world_load_level이 레벨 자체에 대해 지키는
       것과 같은 계약입니다. */
    PlayerProgress prev = w->entry;
    w->entry = want;

    if (world_load_level(w, name, WORLD_ENTER_REPLAY)) return 1;

    w->entry = prev;
    return 0;
}

void world_restart(World *w) {
    /* REPLAY, not NEW: a retry means "put me back where I started this stage",
       which for the first stage IS the boot belt and for every stage after it
       is what the player walked in with. NEW here is what stripped a player who
       died in stage two of the axe they earned in stage one.
       NEW가 아니라 REPLAY입니다. 재시도는 "이 스테이지를 시작했던 자리로 되돌려 놓아라"를
       뜻하며, 첫 스테이지에서 그것은 곧 부팅 구성이고 그 이후의 모든 스테이지에서는
       플레이어가 걸어 들어온 상태입니다. 이곳의 NEW가, 2스테이지에서 죽은 플레이어에게서
       1스테이지에서 얻은 도끼를 빼앗은 것입니다. */
    world_load_level(w, w->cur_level, WORLD_ENTER_REPLAY);

    /* title=0: the player asked to play, so a restart goes straight back into
       the run rather than showing the title again. */
    run_reset(&w->run, 0);
}

int world_frozen(const World *w, int paused) {
    /* THE INTERMISSION FREEZES TOO. It is the same kind of state as the win
       screen: a moment held over a level that is finished with. Without it the
       player keeps walking and shooting behind the names, and the monsters
       keep coming -- a player who dies during the screen announcing that they
       cleared the level has been told two contradictory things.
       인터미션도 정지시킵니다. 승리 화면과 같은 종류의 상태이며, 이미 끝난 레벨 위에
       붙잡아 둔 순간입니다. 그러지 않으면 플레이어는 이름 뒤에서 계속 걷고 쏘고 몬스터도
       계속 다가옵니다. 레벨을 클리어했다고 알리는 화면 도중에 죽는 플레이어는 서로
       모순되는 두 가지를 들은 것입니다. */
    return w->run.won || w->run.dead || w->run.title || w->run.between || paused;
}

int world_step(World *w, const Input *in, float aspect, float dt) {
    /* The frame clock, first and unconditionally -- BEFORE the frozen test,
       because a paused world still draws, still rebuilds geometry, and can
       still overflow something. A clock that stopped while the pause menu was
       up would stamp those reports with the frame the pause began on and make
       a fault that happens only while paused look like one that happened just
       before it.
       This is the only site that advances it, and it is here rather than in
       the render loop so that a headless tool gets real frame numbers without
       knowing diag exists.

       프레임 시계이며, 가장 먼저 무조건 진행합니다. frozen 검사보다 *앞*인 것은, 정지된
       월드도 여전히 그려지고 지오메트리를 다시 만들며 무언가를 초과할 수 있기 때문입니다.
       일시정지 메뉴가 떠 있는 동안 멈추는 시계는 그런 보고들에 일시정지가 시작된 프레임을
       찍어, 정지 중에만 발생하는 결함을 그 직전에 발생한 것처럼 보이게 만듭니다.
       시계를 진행시키는 유일한 지점이며, 렌더 루프가 아니라 이곳에 둔 덕분에 헤드리스
       툴은 diag의 존재를 몰라도 실제 프레임 번호를 얻습니다. */
    DIAG_TICK();

    /* Derived once and returned, so the renderer draws the frame the step
       actually ran. See ::world_frozen. */
    int frozen = world_frozen(w, in->paused);

    /* --- the edges, before anything that could be answering them -----------
       OUTSIDE the frozen test, all of them, and they have to be: the screens
       these act on are exactly the screens that freeze the world. A `confirm`
       gated on `!frozen` could never dismiss a title screen, because the title
       screen is what made the world frozen.

       `let_go` is first. It is the only one that undoes something rather than
       starting something, and a hook released this frame must not be reeling
       the player somewhere while the frame decides what else to do.

       엣지들이며, 그것들이 답하고 있을 수 있는 무엇보다도 먼저입니다.
       전부 frozen 검사 *바깥*이며 그래야만 합니다. 이들이 작용하는 화면이 바로 월드를
       정지시키는 화면이기 때문입니다. `!frozen`으로 막힌 `confirm`은 타이틀 화면을 결코
       해제할 수 없습니다. 타이틀 화면이 곧 월드를 정지시킨 장본인이기 때문입니다.

       `let_go`가 먼저입니다. 무언가를 시작하는 대신 되돌리는 유일한 엣지이며, 이번 프레임에
       놓인 훅이 프레임이 다른 무엇을 할지 결정하는 동안 플레이어를 끌어당기고 있어서는 안
       됩니다. */
    if (in->let_go) {
        wp_hook_release(&w->weapon);
        wp_hook_arm(&w->weapon);
    }
    if (in->confirm) step_confirm(w);

    /* The one edge that IS gated, because it is the one that acts on the world
       rather than on the screen in front of it. Switching weapons behind a
       pause menu, on the death screen, or before the run has started are all
       the same answer: there is no hand to put anything in yet.
       유일하게 게이트가 걸린 엣지입니다. 눈앞의 화면이 아니라 월드에 작용하는 유일한
       엣지이기 때문입니다. 일시정지 메뉴 뒤에서, 사망 화면에서, 또는 플레이가 시작되기 전에
       무기를 바꾸는 것은 모두 같은 답입니다. 아직 무언가를 쥐여 줄 손이 없습니다. */
    if (!frozen && in->want_weapon) step_weapon_pick(w, in->want_weapon - 1);

    /* Look, move, fire and the AI all stop. The last frame keeps being drawn
       under the overlay, which is what the caller does with the return value. */
    if (!frozen) {
        step_look_move(w, in, aspect, dt);
        step_damage(w, dt);
    }

    /* --- clocks that run whether or not the world does -------------------
       The death and title screens animate while frozen -- they are what the
       freeze is FOR -- and the hurt flash has to fade out even if the player
       opened the menu on the frame they were hit.
       사망 화면과 타이틀 화면은 정지 중에도 애니메이션합니다. 정지가 존재하는 이유가 바로
       그것입니다. 그리고 피격 섬광은 플레이어가 피격된 프레임에 메뉴를 열었더라도 사라져야
       합니다. */
    if (w->run.dead)  w->run.death_time += dt;
    if (w->run.title) w->run.title_time += dt;
    if (w->player.hurt > 0.0f) w->player.hurt -= dt * 2.0f;

    if (!frozen) step_smoke(w, dt);

    /* Doors move the sectors themselves, so anything that collides sees them
       without knowing what a door is -- but the DRAWN geometry has to be
       rebuilt to follow. Only while something is actually moving: a level with
       no doors, or with all of them at rest, pays nothing.
       문은 섹터 자체를 움직이므로 충돌하는 모든 것이 문의 정체를 모른 채 그것을 봅니다.
       그러나 *그려지는* 지오메트리는 따라가려면 다시 만들어야 합니다. 실제로 움직이는
       동안에만 수행하므로, 문이 없거나 전부 멈춰 있는 레벨은 비용을 치르지 않습니다. */
    if (!frozen && door_update(&w->level, w->player.pos, w->player.keys, dt)
        && w->geometry_dirty < WORLD_GEOM_MOVING)
        /* Raised to MOVING, never lowered TO it. A door that moves on the frame
           a level loaded must not turn that load's whole rebuild into a partial
           one -- the static half would still be the previous level's.
           MOVING으로 올릴 뿐, MOVING으로 *내리지는* 않습니다. 레벨이 로드된 프레임에 움직인
           문이 그 로드의 전체 재생성을 부분 재생성으로 바꿔서는 안 됩니다. 그러면 정적인
           절반이 여전히 이전 레벨의 것으로 남습니다. */
        w->geometry_dirty = WORLD_GEOM_MOVING;

    /* Grenades and bolts advance with the world. Frozen with it too: a grenade
       hanging in mid-air behind a pause menu says the game is still running
       when it is not. */
    if (!frozen) proj_update(&w->pools, &w->level, dt);

    /* Particles advance even on the win screen: freezing the world mid-air
       would strand whatever was in flight when the exit was reached.
       승리 화면에서도 입자는 진행합니다. 월드를 공중에서 정지시키면 출구에 도달한 순간
       날아가던 것들이 그대로 멈춰 버립니다. */
    fx_update(&w->pools, dt);

    /* Pickups top up health, ammo and the roster when walked over. The weapon
       is passed whole rather than one ammo pointer, because a box says which
       belt it fills and a weapon pickup fills none of them. */
    if (!frozen)
        pickup_update(&w->pools, w->player.pos, &w->player.health, PLAYER_MAX_HP,
                      &w->weapon, &w->player.keys, dt);

    /* Before the exit, because a pad and an exit on the same tile is a level
       bug either way and this order makes it an obvious one: the player is
       thrown into the air rather than quietly finishing the level.
       출구보다 먼저입니다. 같은 칸에 점프대와 출구가 있는 것은 어느 쪽이든 레벨의 결함이며,
       이 순서는 그것을 눈에 띄게 만듭니다. 조용히 레벨이 끝나는 대신 플레이어가 공중으로
       던져집니다. */
    if (!frozen) step_push(w);

    if (!frozen) step_exit(w);

    /* OUTSIDE the frozen test, because the intermission is itself what froze
       the world -- gating its own clock on `!frozen` would stop the timer that
       is supposed to end it, and the screen would stay up forever.
       `frozen` 검사 *바깥*입니다. 인터미션 자신이 월드를 정지시킨 장본인이므로, 그 시계를
       `!frozen`으로 막으면 그것을 끝내야 할 타이머가 멈추고 화면이 영원히 남습니다. */
    step_between(w, dt);

    /* --- the clock animated materials run against ------------------------
       Advances with dt rather than being read from a system timer, so it stops
       when the world does: a lava floor that keeps churning behind a pause menu
       says the game is still running when it is not.

       The wrap is unconditional. Nothing can push the clock past the bound
       while frozen, so this is a no-op then -- but the bound belongs to the
       value rather than to the frames that advance it.

       시스템 타이머가 아니라 dt로 진행하므로 월드가 멈추면 함께 멈춥니다. 일시정지 메뉴
       뒤에서 계속 끓는 용암 바닥은 게임이 멈춰 있는데도 돌아가고 있다고 말하는 셈입니다.

       순환은 조건 없이 수행합니다. 정지 중에는 시계를 경계 밖으로 밀어낼 수 있는 것이 없어
       아무 동작도 하지 않지만, 그 경계는 값을 진행시키는 프레임이 아니라 값 자체에
       속합니다. */
    if (!frozen) w->run.world_time += dt;
    if (w->run.world_time > WORLD_TIME_WRAP) w->run.world_time -= WORLD_TIME_WRAP;

    return frozen;
}

int world_take_geometry(World *w, int *dynamic) {
    return world_take_geometry_scope(w, dynamic) != WORLD_GEOM_NONE;
}

WorldGeom world_take_geometry_scope(World *w, int *dynamic) {
    if (!w->geometry_dirty) return WORLD_GEOM_NONE;
    WorldGeom scope = (WorldGeom)w->geometry_dirty;
    w->geometry_dirty = WORLD_GEOM_NONE;

    /* The first rebuild creates the mesh; every one after it replaces an
       existing allocation. The caller no longer has to know which it is. */
    if (dynamic) *dynamic = w->geometry_uploaded;
    w->geometry_uploaded = 1;
    return scope;
}
