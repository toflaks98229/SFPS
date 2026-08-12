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
#include "enemy.h"
#include "pickup.h"
#include "proj.h"
#include "door.h"
#include "fx.h"
#include "audio.h"
#include "txt.h"

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
    wp_hook_update(&w->weapon, &w->level, &w->player.pos, &w->player.vel, dt);

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
    wp_axe_land(&w->weapon, w->player.pos, w->player.grounded, dt);

    /* Last, so its timers see the movement that actually happened. */
    wp_update(&w->weapon, dt, in->fire,
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

    int dmg = enemy_update(&w->level, w->player.pos, dt);
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
    int dps = w->player.grounded
            ? level_hazard_at(&w->level, w->player.pos.x, w->player.pos.z)
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
 * The "actually exposed" part is the whole difficulty. A hazard sector may be
 * covered by a later sector -- the safe dais in `vault` sits in the middle of
 * the lava moat -- and smoke must not rise through a floor the player is
 * standing on. The test for that already exists and is the same one the damage
 * uses: ::level_hazard_at resolves the last-declared-wins rule, so a point
 * under the dais reports 0 even though the lava sector's own polygon contains
 * it. Sampling a random point inside the sector's bounds and asking that
 * question is therefore correct by construction -- there is no second rule to
 * keep in step, and a platform added later is automatically smoke-free.
 *
 * Rejection sampling rather than walking the polygon: a sector is an arbitrary
 * concave outline, and picking a uniformly distributed point inside one
 * properly means triangulating it. Throwing darts at its bounding box and
 * keeping the hits costs a few wasted samples on an L-shaped room and no code
 * at all.
 *
 * The height comes from the sector's floor rather than from the hit point, so a
 * puff starts ON the lava rather than at eye level above it.
 *
 * 한국어
 * ------
 * @brief 용암이 실제로 *노출된* 곳에서 끓어오르는 연기입니다.
 * @param[in,out] w  월드.
 * @param[in]     dt 시간 간격 (초).
 *
 * "실제로 노출된"이 어려움의 전부입니다. 위험 지형 섹터는 나중에 선언된 섹터에 덮일 수
 * 있고(`vault`의 안전한 단상이 용암 해자 한가운데 있습니다), 연기가 플레이어가 서 있는
 * 바닥을 뚫고 올라와서는 안 됩니다. 그 판정은 이미 존재하며 피해가 쓰는 것과 동일합니다.
 * ::level_hazard_at이 마지막 선언 우선 규칙을 해석하므로, 단상 아래의 지점은 용암 섹터의
 * 다각형이 그것을 포함하더라도 0을 보고합니다. 따라서 섹터 경계 안의 임의의 점을 뽑아 그
 * 질문을 던지는 것은 구조적으로 올바릅니다. 동기화를 유지할 두 번째 규칙이 없으며, 나중에
 * 추가되는 발판은 자동으로 연기가 나지 않습니다.
 *
 * 다각형을 순회하지 않고 기각 표본 추출을 씁니다. 섹터는 임의의 오목한 외곽선이며, 그
 * 내부의 균일 분포 점을 제대로 뽑으려면 삼각분할이 필요합니다. 바운딩 박스에 다트를 던져
 * 맞은 것만 취하면 L자 방에서 표본 몇 개를 낭비할 뿐 코드는 전혀 들지 않습니다.
 *
 * 높이는 충돌 지점이 아니라 섹터의 바닥에서 가져오므로, 연기가 용암 위쪽 눈높이가 아니라
 * 용암 *위에서* 시작합니다.
 */
static void step_smoke(World *w, float dt) {
    w->run.smoke_timer -= dt;
    if (w->run.smoke_timer > 0.0f) return;
    w->run.smoke_timer = LAVA_SMOKE_INTERVAL;

    for (int s = 0; s < LAVA_SMOKE_PER_TICK; s++) {
        /* One dart per attempt, a few attempts per puff. Giving up is correct:
           a level with no lava should cost nothing here, and a sector that is
           almost entirely covered should emit almost nothing.
           시도마다 다트 하나, 연기 하나당 몇 번의 시도입니다. 포기하는 것이 옳습니다.
           용암이 없는 레벨은 이곳에서 비용이 들지 않아야 하고, 거의 전부 덮인 섹터는 거의
           아무것도 내뿜지 않아야 합니다. */
        for (int tries = 0; tries < 4; tries++) {
            w->run.smoke_rng = w->run.smoke_rng * 1664525u + 1013904223u;
            int si = (int)((w->run.smoke_rng >> 16) % (unsigned)
                           (w->level.n_sectors > 0 ? w->level.n_sectors : 1));
            const Sector *sec = &w->level.sectors[si];
            if (sec->hurt <= 0 || !sec->has_bounds) continue;

            w->run.smoke_rng = w->run.smoke_rng * 1664525u + 1013904223u;
            float fx_ = (w->run.smoke_rng >> 8) * (1.0f / 16777216.0f);
            w->run.smoke_rng = w->run.smoke_rng * 1664525u + 1013904223u;
            float fz_ = (w->run.smoke_rng >> 8) * (1.0f / 16777216.0f);

            float x = (sec->min_x + (sec->max_x - sec->min_x) * fx_) * 0.01f;
            float z = (sec->min_z + (sec->max_z - sec->min_z) * fz_) * 0.01f;

            /* The one question that matters, and it is the same one the damage
               asks. A covered point answers 0.
               중요한 유일한 질문이며 피해가 묻는 것과 같습니다. 덮인 지점은 0을
               답합니다. */
            if (level_hazard_at(&w->level, x, z) <= 0) continue;

            /* Far-off smoke is invisible and still costs a slot in a shared
               pool, so it is not spawned at all.
               먼 곳의 연기는 보이지 않으면서 공유 풀의 슬롯을 차지하므로 아예 생성하지
               않습니다. */
            float dx = x - w->player.pos.x, dz = z - w->player.pos.z;
            if (dx*dx + dz*dz > LAVA_SMOKE_RANGE * LAVA_SMOKE_RANGE) continue;

            v3 at = v3f(x, sec->floor * 0.01f + 0.05f, z);
            fx_spawn("lavasmoke", at, v3f(0.0f, 1.0f, 0.0f));
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
static void step_exit(World *w) {
    if (!level_exit_at(&w->level, w->player.pos.x, w->player.pos.z)) return;

    if (!w->level.next[0]) {
        /* Terminal level: the exit goes nowhere, so this is the end. */
        w->run.won = 1;
        audio_play("win", 100);
        return;
    }

    /* An unknown target changes nothing and leaves the player where they are;
       a typo does not win the game. */
    if (world_load_level(w, w->level.next, 1))
        audio_play("exit", 90);
}

/* --------------------------------------------------------------------- api */

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
}

int world_load_level(World *w, const char *name, int carry_state) {
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
       See ::World::geometry_dirty. */
    w->geometry_dirty = 1;

    int hp = w->player.health, held_keys = w->player.keys;
    int ammo[WP_TYPES], owned[WP_TYPES], cur = w->weapon.cur;
    for (int i = 0; i < WP_TYPES; i++) {
        ammo[i]  = w->weapon.ammo[i];
        owned[i] = w->weapon.owned[i];
    }

    w->yaw   = player_spawn(&w->player, &w->level);   /* resets health */
    w->pitch = 0.0f;
    if (carry_state) {
        w->player.health = hp;                        /* ...so restore it */
        for (int i = 0; i < WP_TYPES; i++) {
            w->weapon.ammo[i]  = ammo[i];
            w->weapon.owned[i] = owned[i];
        }
        w->weapon.cur  = cur;
        w->player.keys = held_keys;
    }

    /* Monsters and pickups are placed from the level's own entities. */
    enemy_spawn_level(&w->level);
    pickup_spawn_level(&w->level);

    /* After the sectors exist and before the first door_update: door.c copies
       each door's CLOSED shape here, and it can only do that while the level
       still holds it.
       섹터가 존재한 뒤, 첫 door_update 이전입니다. door.c가 각 문의 *닫힌* 형상을 이곳에서
       복사하며, 레벨이 아직 그것을 담고 있는 동안에만 가능합니다. */
    door_reset(&w->level);
    proj_reset();

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

void world_restart(World *w) {
    /* carry_state=0: a restart is a fresh run, so health and ammo reset. */
    world_load_level(w, w->cur_level, 0);

    /* title=0: the player asked to play, so a restart goes straight back into
       the run rather than showing the title again. */
    run_reset(&w->run, 0);
}

int world_frozen(const World *w, int paused) {
    return w->run.won || w->run.dead || w->run.title || paused;
}

int world_step(World *w, const Input *in, float aspect, float dt) {
    /* Derived once and returned, so the renderer draws the frame the step
       actually ran. See ::world_frozen. */
    int frozen = world_frozen(w, in->paused);

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
    if (!frozen && door_update(&w->level, w->player.pos, w->player.keys, dt))
        w->geometry_dirty = 1;

    /* Grenades and bolts advance with the world. Frozen with it too: a grenade
       hanging in mid-air behind a pause menu says the game is still running
       when it is not. */
    if (!frozen) proj_update(&w->level, dt);

    /* Particles advance even on the win screen: freezing the world mid-air
       would strand whatever was in flight when the exit was reached.
       승리 화면에서도 입자는 진행합니다. 월드를 공중에서 정지시키면 출구에 도달한 순간
       날아가던 것들이 그대로 멈춰 버립니다. */
    fx_update(dt);

    /* Pickups top up health, ammo and the roster when walked over. The weapon
       is passed whole rather than one ammo pointer, because a box says which
       belt it fills and a weapon pickup fills none of them. */
    if (!frozen)
        pickup_update(w->player.pos, &w->player.health, PLAYER_MAX_HP,
                      &w->weapon, &w->player.keys, dt);

    if (!frozen) step_exit(w);

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
    if (!w->geometry_dirty) return 0;
    w->geometry_dirty = 0;

    /* The first rebuild creates the mesh; every one after it replaces an
       existing allocation. The caller no longer has to know which it is. */
    if (dynamic) *dynamic = w->geometry_uploaded;
    w->geometry_uploaded = 1;
    return 1;
}
