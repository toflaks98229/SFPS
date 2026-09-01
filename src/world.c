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

#include <math.h>   /* cosf/sinf -- the ring a wave reward is thrown along */
#include "world.h"
#include "hook.h"
#include "enemy.h"
#include "pickup.h"
#include "proj.h"
#include "decal.h"
#include "door.h"
#include "brush.h"
#include "fx.h"
/* The rates, the purse, the drop point and the shrine -- all of it is
   assets\loot.txt, so that retuning any of them is saving a file.
   확률, 몫, 낙하 지점, 제단. 전부 assets\loot.txt이므로, 그중 무엇을 조정하든 파일을
   저장하는 일이 됩니다. */
#include "loot.h"
/* The three things said outside the fight, for loot.h's reason applied to
   words: a line is what gets rewritten most and is worth a rebuild least.
   This file only ever asks how many pages a moment has and what is on one --
   the parsing and the file are story.c's.
   전투 바깥에서 하는 세 가지 말이며, loot.h의 이유를 말에 적용한 것입니다. 대사는 가장 많이
   다시 쓰이고 재빌드를 치를 가치는 가장 적습니다. 이 파일이 묻는 것은 어떤 순간이 몇 페이지를
   가지는지와 한 페이지에 무엇이 있는지뿐입니다. 파싱과 파일은 story.c의 것입니다. */
#include "story.h"
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
/* WHAT THIS COSTS, worked out rather than hoped for, because the note above is
 * right that a lava room can starve every other effect in the game.
 *
 *   (1 / 0.11) * 4 puffs a second * 4.2s of life  ~=  150 particles resident
 *
 * against FX_MAX_PARTICLES of 1536, so a tenth of the pool with the room fully
 * smoked -- and the range gate below keeps that a tenth rather than a tenth per
 * lava pool. The other cost is draw calls: fx.c gives every particle its own,
 * so this is ~150 of them. That is why the puffs were made BIGGER and FAINTER
 * as well as more numerous; coverage per call is free where another call is
 * not, and thickness that comes from overlap reads as air where thickness that
 * comes from opacity reads as blobs.
 *
 * 이것이 치르는 비용이며, 바라는 대신 계산했습니다. 위의 주석이 옳게 지적하듯 용암 방 하나가
 * 게임의 다른 모든 효과를 고갈시킬 수 있기 때문입니다.
 *
 *   (1 / 0.11) * 초당 4개 * 수명 4.2초  ~=  상주 입자 약 150개
 *
 * FX_MAX_PARTICLES 1536에 대해 방이 완전히 연기로 찼을 때 풀의 10분의 1이며, 아래의 거리
 * 게이트가 그것을 "용암 웅덩이마다 10분의 1"이 아니라 그냥 10분의 1로 유지합니다. 다른 비용은
 * 드로우 콜입니다. fx.c는 입자마다 하나씩 발급하므로 약 150회입니다. 그래서 연기를 수만 늘리지
 * 않고 *더 크고 더 옅게* 만들었습니다. 호출당 덮는 면적은 공짜지만 호출 하나를 더하는 것은
 * 그렇지 않으며, 겹침에서 오는 두께는 공기로 읽히고 불투명도에서 오는 두께는 덩어리로
 * 읽힙니다. */
#define LAVA_SMOKE_INTERVAL  0.11f  ///< @brief Seconds between batches. / 묶음 사이의 간격 (초).
#define LAVA_SMOKE_PER_TICK  4      ///< @brief Puffs attempted per batch. / 묶음당 시도하는 연기 수.
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

    /* Read BEFORE the move, because the move is what resolves both: `grounded`
       is what player_move decides, and `vel.y` is what it spends against the
       floor. Asking afterwards gets a player who is already standing at rest,
       which says nothing about how hard they arrived.
       이동 *전에* 읽습니다. 이동이 둘 다를 결정하기 때문입니다. `grounded`는 player_move가
       정하는 것이고 `vel.y`는 그것이 바닥에 대고 소모하는 것입니다. 나중에 물으면 이미
       멈춰 서 있는 플레이어를 얻게 되는데, 그것은 얼마나 세게 도착했는지에 대해 아무것도
       말해 주지 않습니다. */
    int   was_air = !w->player.grounded;
    float fall    = -w->player.vel.y;

    /* Jump is suppressed only while actually being pulled. It would not fire
       mid-pull anyway (jumping needs `grounded`), but suppressing it keeps the
       intent explicit rather than relying on that coincidence. */
    player_move(&w->player, &w->level, wish, speed, in->jump && !hooked, dt);

    /* Landing. The floor test is ::WORLD_SHAKE_LAND_MIN's, so stepping off a
       stair is not an impact.
       착지입니다. 하한은 ::WORLD_SHAKE_LAND_MIN의 것이며, 계단을 내려서는 것은 충격이
       아닙니다. */
    if (was_air && w->player.grounded && fall > WORLD_SHAKE_LAND_MIN) {
        world_shake(w, WORLD_SHAKE_LAND * (fall / WORLD_SHAKE_LAND_MIN - 1.0f));

        /* Dust at the FEET, not at the eye. Player::pos is the eye -- the same
           distinction the hazard check makes a few lines into ::step_damage --
           and a puff born at head height reads as the player exhaling rather
           than as the floor being hit.
           눈이 아니라 *발*에서 나는 먼지입니다. Player::pos는 눈이며, ::step_damage 초입의
           지형 판정이 두는 것과 같은 구분입니다. 머리 높이에서 태어난 먼지는 바닥을 친 것이
           아니라 플레이어가 숨을 내쉰 것으로 읽힙니다. */
        v3 feet = v3f(w->player.pos.x, w->player.pos.y - PLAYER_EYE,
                      w->player.pos.z);
        fx_spawn(&w->pools, "landdust", feet, v3f(0.0f, 1.0f, 0.0f));

        audio_play_at("hland", 60, feet);
    }

    /* The leap resolves after player_move, so the slam lands on where the
       player actually ended up rather than a frame behind it -- the same reason
       the hook's constraint runs before the move and this runs after.
       도약은 player_move 이후에 처리되므로, 내려찍기가 한 프레임 뒤가 아니라 플레이어가
       실제로 도달한 지점에서 터집니다. 훅의 구속이 이동 전에 실행되고 이것이 이후에
       실행되는 것과 같은 이유입니다. */
    wp_axe_land(&w->weapon, &w->pools, w->player.pos, w->player.grounded, dt);

    /* Last, so its timers see the movement that actually happened. */
    float flash_was = w->weapon.flash;
    wp_update(&w->weapon, &w->pools, &w->level, dt, in->fire,
              w->player.pos, w->yaw, w->pitch, move_speed,
              in->look_dx, in->look_dy,
              WORLD_FOV, aspect, &w->player.vel, w->player.grounded);

    /* THE MUZZLE FLASH COMING UP IS THE SHOT, and reading it here is what saves
       plumbing a "did it fire" signal out of wp_update -- which returns nothing
       and would have had to grow a parameter to say so. The flash is set by the
       same branch that spends the ammo, so a rising edge is a shot and nothing
       else: a swing that misses does not raise it, and a weapon on cooldown
       does not either.
       *총구 섬광이 올라오는 것이 곧 사격이며*, 이곳에서 그것을 읽는 것이 "발사했는가" 신호를
       wp_update에서 끌어내는 배관을 아끼는 방법입니다. 그 함수는 아무것도 반환하지 않으며 그
       사실을 말하려면 매개변수가 늘어나야 했을 것입니다. 섬광은 탄약을 소모하는 바로 그
       분기가 설정하므로, 상승 엣지는 사격이고 그 외의 무엇도 아닙니다. 빗나간 휘두르기는
       그것을 올리지 않고, 재사용 대기 중인 무기도 마찬가지입니다. */
    if (w->weapon.flash > flash_was) world_shake(w, WORLD_SHAKE_FIRE);
}

/* ----------------------------------------------------------------- damage */

/**
 * @brief Hands over what the corpses in this level owe the floor.
 *
 * ENGLISH
 * -------
 * The roll happened at the instant of death, inside ::enemy_hurt, because that
 * is where the demo-replayable generator lives. This is the other half: which
 * ammo box ::LOOT_HELD means is a question about the player's roster, and the
 * roster is here. See ::Enemy::drop for the whole of that argument.
 *
 * ONE PASS PER FRAME OVER THE WHOLE POOL, and it is cheap for the reason the
 * pool is small: ::enemy_take_drop answers -1 for every slot that owes nothing,
 * which on almost every frame is all of them. A queue would be a second place a
 * drop can be lost.
 *
 * The toss goes straight UP rather than outward. A reward is a ring because it
 * is a purse arriving at once; a corpse's drop is one item and belongs where the
 * body fell, which is also the spot the player is already looking at.
 *
 * @param[in,out] w The world.
 *
 * 한국어
 * ------
 * @brief 이 레벨의 시체들이 바닥에 빚진 것을 건네받습니다.
 *
 * 굴림은 죽는 순간 ::enemy_hurt 안에서 일어났습니다. 데모가 재생 가능한 생성기가 그곳에 있기
 * 때문입니다. 이것은 나머지 절반입니다. ::LOOT_HELD가 어느 탄약 상자를 뜻하는지는 플레이어의
 * 보유 목록에 대한 질문이고, 그 목록이 이곳에 있습니다. 그 논거 전체는 ::Enemy::drop을
 * 참조하십시오.
 *
 * 프레임마다 풀 전체를 한 번 훑으며, 풀이 작기 때문에 저렴합니다. ::enemy_take_drop은 빚진
 * 것이 없는 모든 슬롯에 -1로 답하는데, 거의 모든 프레임에서 그것이 전부입니다. 큐는 드롭이
 * 사라질 수 있는 두 번째 장소가 됩니다.
 *
 * 던지기는 바깥이 아니라 *위로* 향합니다. 보상이 고리인 것은 몫이 한꺼번에 도착하기
 * 때문이고, 시체의 드롭은 하나이며 몸이 쓰러진 자리에 놓여야 합니다. 그 자리는 플레이어가
 * 이미 보고 있는 자리이기도 합니다.
 */
static void step_drops(World *w) {
    int gun = 0;

    for (int i = 0, n = enemy_count(&w->pools); i < n; i++) {
        v3 at;
        int kind = enemy_take_drop(&w->pools, i, &at);
        if (kind < 0) continue;

        if (kind == LOOT_HELD) {
            kind = loot_held_kind(&w->weapon, &gun);
            if (kind < 0) continue;
        }
        pickup_toss(&w->pools, kind, at, v3f(0, PICKUP_TOSS_UP * 0.55f, 0));
    }
}

/**
 * @brief Count the powerup clocks down, and set the knobs that read them.
 *
 * ENGLISH
 * -------
 * BEFORE ANYTHING THAT READS A KNOB, which is the whole reason this is its own
 * function called from its own line rather than a block inside ::step_damage.
 * It began there, beside `enemy_update` -- one of the two readers -- and the
 * comment claimed the knobs sat "right beside the calls that read them". The
 * other reader is `wp_update`, and ::step_look_move runs BEFORE ::step_damage:
 * the weapon was multiplying by a number set on the PREVIOUS frame. A player
 * who grabbed a quad and fired in the same breath fired an ordinary shot, and
 * nothing anywhere would have said so.
 *
 * ::PW_QUAD is a field on ::Weapon and ::PW_SHADOW one on ::EnemyPool, because
 * neither module knows what a ::Player is and neither should learn: weapon.c is
 * handed a number to multiply by, enemy.c a bit saying whether it can see
 * anything. Set every frame rather than once on pickup, so a clock running out
 * needs no second place to notice it -- the knob follows the timer by
 * construction.
 *
 * @note Inside the `!frozen` arm at the call site, so a powerup does not burn
 *       down behind the pause menu.
 *
 * 한국어
 * ------
 * @brief 파워업 시계를 세어 내리고, 그것을 읽는 손잡이들을 설정합니다.
 *
 * *손잡이를 읽는 그 무엇보다도 먼저입니다.* 이것이 ::step_damage 안의 블록이 아니라 자기
 * 함수이자 자기 줄에서 호출되는 이유 전부입니다. 이것은 두 독자 중 하나인 `enemy_update` 곁,
 * 그곳에서 시작했고 주석은 손잡이가 "그것을 읽는 호출 바로 곁"에 있다고 주장했습니다. 다른
 * 독자는 `wp_update`이며 ::step_look_move는 ::step_damage보다 *먼저* 돕니다. 무기는 *이전*
 * 프레임에 설정된 수를 곱하고 있었습니다. 쿼드를 집어 그 숨결에 발사한 플레이어는 평범한 탄을
 * 쏜 것이고, 어디에서도 그렇다고 말해 주지 않았을 것입니다.
 *
 * ::PW_QUAD는 ::Weapon의 필드이고 ::PW_SHADOW는 ::EnemyPool의 것입니다. 두 모듈 다 ::Player가
 * 무엇인지 모르고 알게 되어서도 안 되기 때문입니다. weapon.c는 곱할 수를, enemy.c는 무언가를
 * 볼 수 있는지에 대한 비트를 건네받습니다. 획득 시점에 한 번이 아니라 매 프레임 설정하므로,
 * 시계가 다 되는 것을 알아챌 두 번째 자리가 필요 없습니다. 손잡이는 구조적으로 타이머를
 * 따라갑니다.
 *
 * @note 호출 지점의 `!frozen` 갈래 안에 있으므로, 일시정지 메뉴 뒤에서 파워업이 타 내려가지
 *       않습니다.
 */
static void step_powers(World *w, float dt) {
    for (int i = 0; i < PW_KINDS; i++) {
        w->player.power[i] -= dt;
        if (w->player.power[i] < 0.0f) w->player.power[i] = 0.0f;
    }
    w->weapon.damage_mul   = w->player.power[PW_QUAD]   > 0.0f ? PLAYER_QUAD_MUL : 1;
    w->pools.enemy.blinded = w->player.power[PW_SHADOW] > 0.0f;
}

/**
 * @brief What percentage of incoming damage survives ::PW_AEGIS. 100 when none.
 *
 * ENGLISH
 * -------
 * ONE PLACE THAT KNOWS THE RULE, two call sites that apply it in the unit they
 * happen to hold -- because the first version applied it at one of them only.
 * The aegis cut the integer hit from `enemy_update` and never touched the
 * hazard rate a few lines below it, so an artifact whose whole point is
 * surviving the arena's lava sea did nothing at all in the lava.
 *
 * THE TWO SITES ROUND DIFFERENTLY AND SHOULD. An integer hit needs a floor of
 * one: a one-point blow scaled to 30% is zero, and damage that stops counting
 * while a clock runs is a pentagram, not a coat of armour. A rate does NOT need
 * one -- `hazard_accum` carries the fraction across frames, so 40 a second
 * scaled to 12 a second still arrives, just slower. Clamping the rate would
 * make a weak hazard hurt MORE under the aegis than without it.
 *
 * 한국어
 * ------
 * @brief 들어오는 피해 중 ::PW_AEGIS를 지나 남는 비율입니다. 없으면 100입니다.
 *
 * *규칙을 아는 한 자리*와, 각자 쥐고 있는 단위로 그것을 적용하는 두 호출 지점입니다. 첫 판이
 * 둘 중 하나에만 적용했기 때문입니다. 아이기스는 `enemy_update`가 준 정수 피해를 깎았고 그
 * 몇 줄 아래의 유해 지형 비율은 건드리지 않았으므로, 요점 전부가 이 투기장의 용암 바다에서
 * 살아남는 것인 아티팩트가 용암 속에서 아무 일도 하지 않았습니다.
 *
 * *두 지점은 반올림이 다르며 그래야 합니다.* 정수 피해에는 1의 하한이 필요합니다. 1점짜리
 * 일격을 30%로 줄이면 0이고, 시계가 도는 동안 세기를 멈추는 피해는 갑옷이 아니라 펜타그램입니다.
 * 비율에는 하한이 *필요 없습니다*. `hazard_accum`이 프레임을 가로질러 소수를 나르므로 초당
 * 40이 초당 12로 줄어도 여전히 도착하며, 다만 느릴 뿐입니다. 비율에 하한을 두면 약한 유해
 * 지형이 아이기스가 있을 때 *없을 때보다 더* 아프게 됩니다.
 */
static int aegis_pct(const World *w) {
    return w->player.power[PW_AEGIS] > 0.0f ? PLAYER_AEGIS_PCT : 100;
}

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

    /* Drained on the same frame the monsters were stepped, so a kill this frame
       leaves its item this frame. Here rather than in ::world_step because this
       is the one function that has just moved the monsters, and a drop noticed
       anywhere else is a drop noticed a frame late.
       몬스터가 진행된 바로 그 프레임에 비웁니다. 그래야 이번 프레임의 처치가 이번 프레임에
       아이템을 남깁니다. ::world_step이 아니라 이곳인 이유는 몬스터를 방금 움직인 함수가
       이것 하나이기 때문이며, 다른 어디에서 알아챈 드롭은 한 프레임 늦게 알아챈 드롭입니다. */
    step_drops(w);

    /* AEGIS FIRST, because everything below this reads `dmg` -- the health,
       the flash and the shake all describe the hit that actually landed. Cut
       it here and the whole frame agrees; cut it at the subtraction alone and
       a protected player is thrown around by a blow they barely felt.
       Integer percent, and the max() is what stops a cut from becoming an
       immunity: a one-point hit scaled to 30% is zero, and a hazard that
       stopped counting while a clock ran would be a pentagram rather than a
       coat of armour.
       *아이기스가 먼저입니다.* 이 아래의 모든 것이 `dmg`를 읽기 때문입니다. 체력도
       섬광도 흔들림도 *실제로 들어온* 타격을 서술합니다. 이곳에서 깎으면 프레임
       전체가 일치하고, 뺄셈에서만 깎으면 보호받는 플레이어가 거의 느끼지 못한
       일격에 내던져집니다.
       정수 퍼센트이며, max()가 경감이 면역이 되는 것을 막습니다. 1점짜리 타격을
       30%로 줄이면 0이고, 시계가 도는 동안 아예 세지 않는 위험은 갑옷이 아니라
       펜타그램입니다. */
    if (dmg > 0) {
        dmg = dmg * aegis_pct(w) / 100;
        if (dmg < 1) dmg = 1;
    }

    if (dmg > 0 && w->player.health > 0) {
        w->player.health -= dmg;
        if (w->player.health < 0) w->player.health = 0;
        w->player.hurt = 1.0f;
        audio_play("phurt", 90);

        /* Scaled by what the hit was worth against a full bar, so a scratch
           registers and a mauling is unmistakable. The health figure the player
           is already watching is what drives it, which is why this needs no
           tuning table of its own.
           그 피격이 가득 찬 체력 바에 대해 얼마짜리였는지로 조정하므로, 스치는 상처도
           등록되고 크게 물어뜯긴 것은 착각할 수 없습니다. 플레이어가 이미 지켜보고 있는
           체력 수치가 이것을 구동하며, 그래서 자체 조율 표가 필요 없습니다. */
        world_shake(w, WORLD_SHAKE_HURT * (float)dmg / (float)PLAYER_MAX_HP);
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
        /* THE CUT IS APPLIED HERE, into the float, rather than to `dps`
           above -- `dps` is an int, and 30% of a 3-a-second hazard rounds
           to nothing. The accumulator carries the fraction, so a weak
           hazard under the aegis stays slow instead of becoming harmless.
           *깎기는 위의 `dps`가 아니라 이곳의 실수에 적용됩니다.* `dps`는 정수이고, 초당 3인
           유해 지형의 30%는 0으로 반올림됩니다. 누산기가 소수를 나르므로, 아이기스 아래의
           약한 유해 지형은 무해해지는 대신 느려질 뿐입니다. */
        w->run.hazard_accum += dps * dt * (float)aegis_pct(w) / 100.0f;
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

/**
 * @brief Seconds between one mote of a burning shrine and the next.
 *
 * ENGLISH: Chosen against `altarmote`'s own 1600ms life: at this interval about
 * eight are in the air at once, which is enough to read as a column from across
 * the room and few enough that none of them has to be bright. Denser and the
 * shrine glares; sparser and it flickers, and a flicker reads as something
 * breaking rather than as something waiting for you.
 *
 * 한국어: `altarmote` 자신의 1600ms 수명에 맞춰 정했습니다. 이 간격이면 여덟쯤이 동시에
 * 공중에 있는데, 방 건너에서 기둥으로 읽히기에 충분하고 그중 어느 것도 밝을 필요가 없을 만큼
 * 적습니다. 더 촘촘하면 제단이 눈부시고, 더 성기면 깜박이며, 깜박임은 당신을 기다리는
 * 무언가가 아니라 고장 나는 무언가로 읽힙니다.
 */
#define ALTAR_MOTE_INTERVAL 0.20f

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

/**
 * @brief Shakes the camera for every detonation whose light is still up.
 *
 * ENGLISH
 * -------
 * @param[in,out] w The world whose view is shaken, and whose player's position
 *                  the falloff is measured from.
 *
 * @note READS THE SAME RECORD ::scene_lights DOES. A blast is one event and the
 *       two things it does to the player -- brightening the room and moving the
 *       camera -- have to end together, so both come off the ::Flash pool and
 *       both go through ::proj_flash_fade. A separate timer here would be a
 *       second opinion about when the explosion was over, and the way that
 *       fails is a camera still rolling in a room that has gone dark again.
 * @note FED EVERY FRAME, NOT ONCE AT THE MOMENT IT WENT OFF, and that is what
 *       lets this function have no notion of "new". ::world_shake takes the
 *       LOUDER of what it holds and what it is offered, so handing it the same
 *       decaying curve on every frame of the flash's life is idempotent -- and
 *       ::WORLD_SHAKE_DECAY outruns the flash's own curve within two frames
 *       anyway, so the result is the peak, set once, without a flag anybody
 *       could get wrong.
 * @note Measured to the player's FEET. They are what is standing on the floor
 *       the blast travelled through, and the half metre up to the eye is
 *       nothing beside a reach of ten.
 *
 * 한국어
 * ------
 * @brief 빛이 아직 남아 있는 모든 폭발에 대해 카메라를 흔듭니다.
 * @param[in,out] w 시야가 흔들릴 월드이며, 감쇠를 재는 기준이 되는 플레이어의 위치를 가진
 *                  쪽이기도 합니다.
 *
 * @note *::scene_lights와 같은 기록을 읽습니다.* 폭발은 하나의 사건이고, 그것이 플레이어에게
 *       하는 두 가지(방을 밝히는 것과 카메라를 움직이는 것)는 함께 끝나야 하므로, 둘 다
 *       ::Flash 풀에서 나오고 둘 다 ::proj_flash_fade를 거칩니다. 이곳에 별도의 타이머를 두면
 *       폭발이 언제 끝났는지에 대한 두 번째 견해가 되며, 그것이 실패하는 방식은 이미 다시
 *       어두워진 방에서 카메라만 계속 흔들리는 것입니다.
 * @note *터진 순간에 한 번이 아니라 매 프레임 먹입니다.* 그 덕분에 이 함수는 "새것"이라는
 *       개념을 가질 필요가 없습니다. ::world_shake는 자신이 들고 있는 것과 제안받은 것 중 *더
 *       큰* 쪽을 취하므로, 섬광이 사는 매 프레임에 같은 감쇠 곡선을 건네는 것은 멱등입니다.
 *       게다가 ::WORLD_SHAKE_DECAY가 두 프레임 안에 섬광 자신의 곡선을 앞지르므로, 결과는
 *       누구든 틀릴 수 있는 플래그 없이 최댓값을 한 번 설정한 것과 같습니다.
 * @note 플레이어의 *발*까지 잽니다. 폭발이 지나온 바닥에 서 있는 것이 발이며, 눈까지의 0.5m는
 *       10미터의 도달 거리 곁에서는 아무것도 아닙니다.
 */
static void step_blast(World *w) {
    for (int i = 0, n = proj_flash_count(&w->pools); i < n; i++) {
        const Flash *f = proj_flash_at(&w->pools, i);
        if (!f || f->life <= 0.0f) continue;

        float reach = f->radius * WORLD_SHAKE_BLAST_REACH;
        if (reach <= 0.0f) continue;

        float dist = v3len(v3sub(f->pos, w->player.pos));
        if (dist >= reach) continue;

        /* Linear, and named `t` for the same falloff ::proj_blast writes: one
           shape for how a blast weakens, whether what it is weakening is
           damage or the camera.
           선형이며, ::proj_blast가 쓰는 것과 같은 감쇠이므로 이름도 `t`입니다. 폭발이 약해지는
           방식은 하나의 형태이며, 약해지는 대상이 피해든 카메라든 마찬가지입니다. */
        float t = 1.0f - dist / reach;
        world_shake(w, WORLD_SHAKE_BLAST * f->power * t * proj_flash_fade(f));
    }
}

/* ------------------------------ screens, the belt, pads, the exit, the arena */

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
/* ------------------------------------------------------------- the cutscene */

/* Puts a moment on screen, once per run, if the file authored one.
 *
 * THREE REFUSALS AND THEY ARE ALL THE SAME REFUSAL: already seen, already
 * playing, or nothing written. Each leaves the caller's own state exactly as it
 * was, which is what lets every trigger below be an unconditional call at the
 * point the moment happens rather than a condition the trigger has to get
 * right. ::music_play's bargain.
 *
 * MARKED SEEN WHETHER OR NOT IT PLAYS. A moment with no pages has HAPPENED --
 * the maw died, the player died -- and a story.txt saved mid-run that adds the
 * cut must not make it start halfway through the screen it would have preceded.
 *
 * 어떤 순간을 화면에 올립니다. 플레이당 한 번, 파일이 그것을 제작했다면.
 *
 * *거절이 셋이고 셋 다 같은 거절입니다.* 이미 보았거나, 이미 재생 중이거나, 쓰인 것이 없거나.
 * 각각은 호출자 자신의 상태를 그대로 남기며, 그것이 아래의 모든 발화점을 조건이 아니라 순간이
 * 일어나는 지점에서의 무조건 호출로 만듭니다. ::music_play의 거래입니다.
 *
 * *재생되든 아니든 보았다고 표시합니다.* 페이지가 없는 순간도 *일어났습니다*. 아귀가 죽었고
 * 플레이어가 죽었습니다. 그리고 플레이 도중에 저장되어 그 컷을 추가한 story.txt가, 그것이
 * 선행했어야 할 화면의 도중에 컷신을 시작하게 만들어서는 안 됩니다. */
static void cut_begin(World *w, int moment) {
    /* ONE GATE, NOT THREE, and it is ::boss_say's gate for ::boss_say's
       reason: every cutscene is story-only, and three separate `if (!endless)`
       tests at three trigger sites is two chances for the fourth moment
       somebody adds to be the one that plays in endless mode.
       *셋이 아니라 하나의 게이트*이며, ::boss_say의 게이트이자 그 이유입니다. 모든 컷신은
       스토리 전용이고, 세 발화 지점에 흩어진 `if (!endless)` 셋은 누군가 나중에 추가하는 네 번째
       순간이 무한 모드에서 재생될 기회를 둘 만듭니다. */
    if (w->run.endless) return;
    if (moment < 0 || moment >= STORY_MOMENTS) return;

    int bit = 1 << moment;
    if (w->run.cut_seen & bit) return;
    w->run.cut_seen |= bit;

    if (w->run.cut) return;
    if (!story_for(moment)) return;

    w->run.cut      = moment + 1;
    w->run.cut_page = 0;
    w->run.cut_time = 0.0f;
}

/* Ends the cutscene and lets what it was covering happen.
 *
 * THE WIN IS RAISED HERE, and only for ::STORY_VICTORY. ::step_boss already
 * refuses to raise it on the frame the maw dies so that the maw's last line can
 * be read, and the banner clock used to be what raised it afterwards; the
 * cutscene simply took that place in the queue. Guarded on `dead` for the
 * reason the banner clock was: the killing blow and the player's own death can
 * land in one frame, and a run that ended both ways has no screen.
 *
 * 컷신을 끝내고, 그것이 덮고 있던 것이 일어나게 합니다.
 *
 * *승리는 이곳에서 세워지며* ::STORY_VICTORY에 대해서만입니다. ::step_boss는 아귀의 마지막
 * 대사가 읽힐 수 있도록 아귀가 죽는 프레임에 그것을 세우기를 이미 거절하고 있고, 그 뒤에
 * 그것을 세우던 것은 배너 시계였습니다. 컷신은 그 대기열의 자리를 이어받았을 뿐입니다.
 * `dead`를 함께 검사하는 이유는 배너 시계와 같습니다. 죽이는 한 발과 플레이어 자신의 죽음이 한
 * 프레임에 떨어질 수 있고, 양쪽으로 끝난 플레이에는 화면이 없습니다. */
static void cut_end(World *w) {
    int moment = w->run.cut - 1;

    w->run.cut      = 0;
    w->run.cut_page = 0;
    w->run.cut_time = 0.0f;

    if (moment == STORY_VICTORY && !w->run.dead) w->run.won = 1;
}

/* One page forward, or out. What a press does and what the clock does, so the
   two cannot drift -- a skip that advanced the page without the timeout's
   end-of-cut test would run off the end of the array.
   한 페이지 앞으로, 또는 밖으로. 누름이 하는 일이자 시계가 하는 일이며, 그래서 둘이 어긋날 수
   없습니다. 타임아웃의 컷 종료 판정 없이 페이지만 넘기는 건너뛰기는 배열 끝을 넘어갑니다. */
static void cut_advance(World *w) {
    const StoryCut *c = story_for(w->run.cut - 1);

    /* The file was reloaded out from under a playing cutscene and the moment is
       gone. Ending it is the only answer that leaves the run playable; holding
       a screen whose text no longer exists would be a freeze with nothing on
       it.
       재생 중인 컷신 아래에서 파일이 다시 읽혔고 그 순간이 사라졌습니다. 플레이를 계속 가능하게
       두는 답은 끝내는 것뿐입니다. 더 이상 존재하지 않는 텍스트의 화면을 붙들고 있는 것은 아무
       것도 없는 정지입니다. */
    if (!c) { cut_end(w); return; }

    w->run.cut_page++;
    w->run.cut_time = 0.0f;
    if (w->run.cut_page >= c->n_pages) cut_end(w);
}

/* The page clock.
 *
 * OUTSIDE `!frozen`, and it has to be: a cutscene is one of the things
 * ::world_frozen counts, so a clock gated on `!frozen` would be a screen that
 * stops the world and then waits for the world to advance it. ::step_between
 * carries the same warning for the same reason.
 *
 * 페이지 시계입니다.
 *
 * `!frozen` *바깥*이며 그래야만 합니다. 컷신은 ::world_frozen이 세는 것 중 하나이므로,
 * `!frozen`으로 막힌 시계는 월드를 멈춰 놓고 월드가 자신을 진행시켜 주기를 기다리는 화면이
 * 됩니다. ::step_between이 같은 이유로 같은 경고를 달고 있습니다. */
static void step_cut(World *w, float dt) {
    if (!w->run.cut) return;

    const StoryCut *c = story_for(w->run.cut - 1);
    if (!c) { cut_end(w); return; }

    /* A page index past the end of a cut that was rewritten under us. Not
       reachable while the file holds still; reachable on the frame an author
       saves a shorter story.txt, which is exactly when a crash would be
       blamed on the edit rather than on the missing bound.
       우리 아래에서 다시 쓰인 컷의 끝을 넘어선 페이지 인덱스입니다. 파일이 가만히 있는 동안에는
       도달하지 않습니다. 제작자가 더 짧은 story.txt를 저장하는 프레임에 도달하며, 그때가 바로
       충돌의 책임이 빠진 경계가 아니라 그 편집에 돌아갈 시점입니다. */
    if (w->run.cut_page >= c->n_pages) { cut_end(w); return; }

    w->run.cut_time += dt;
    if (w->run.cut_time >= c->page[w->run.cut_page].hold) cut_advance(w);
}

static void step_confirm(World *w) {
    /* FIRST, because a cutscene is drawn over whatever it is covering and a
       press belongs to the thing on top. Without this, a press during the
       defeat cutscene would reach the death screen underneath and restart the
       run from behind a screen the player was still reading.
       *가장 먼저*입니다. 컷신은 그것이 덮고 있는 무엇 위에 그려지고, 누름은 맨 위의 것에
       속합니다. 이것이 없으면 패배 컷신 도중의 누름이 아래의 사망 화면에 도달하여, 플레이어가
       아직 읽고 있는 화면 뒤에서 플레이를 재시작합니다. */
    if (w->run.cut) {
        cut_advance(w);
        return;
    }

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
static void step_push(World *w) {
    if (!w->player.grounded) return;

    float sp = level_push_at(&w->level, w->player.pos.x, w->player.pos.z);
    if (sp <= 0.0f) return;

    w->player.vel.y   = sp;
    w->player.grounded = 0;
    audio_play_at("hland", 85, w->player.pos);
}

/**
 * @brief Standing in a teleporter: out the other side, facing where it says.
 *
 * ENGLISH
 * -------
 * @param[in,out] w The world. Position, look angle and velocity all change.
 *
 * AT MOST ONE PER FRAME, and the `return` is the whole guard. Two teleporters
 * pointed at each other are a legal thing for an author to draw and an infinite
 * loop for anything that keeps testing: arriving inside the second volume would
 * send the player back into the first on the same frame, forever, with no frame
 * ever drawn in between. Leaving after the first hop means the player is *seen*
 * at the destination before the next one can act, so a loop is a fast round
 * trip rather than a hang.
 *
 * THE VIEW IS YANKED, ON PURPOSE. The destination's `angle` is the author
 * saying which way you should be looking when you come out, and a teleporter
 * that dropped you facing whatever you happened to face on the way in would put
 * you in a corner of a room you have never seen. Quake does the same thing for
 * the same reason. It is the one place in this game that moves the camera
 * without the mouse, which is why it is worth saying out loud here.
 *
 * MOMENTUM IS ROTATED, NOT KEPT AND NOT DROPPED. Keeping the vector sends a
 * player who ran in northwards flying north out of a door that faces south --
 * into a wall, at speed, having done nothing wrong. Dropping it stops a running
 * player dead, which reads as the teleporter grabbing them. Rotating the
 * horizontal part by the same delta the view turned through preserves what the
 * player actually had, which is speed in the direction they were going: run in,
 * run out. Vertical velocity is left alone -- falling into a teleporter and
 * arriving still falling is the truthful answer, and the destination is a point
 * in the air as easily as on a floor.
 *
 * @note Tested against ::Player::pos, which is the eye. The volume an author
 *       draws is a doorway a body walks through, so any point on that body
 *       entering it is the event; the eye is the one this engine already uses
 *       for ::TriggerDef and ::HazardDef, and using a different one here would
 *       make a teleporter and a trigger drawn at the same size behave
 *       differently.
 *
 * 한국어
 * ------
 * @brief 텔레포터 안에 서 있는 것: 반대편으로, 그것이 말하는 방향을 보면서.
 *
 * *한 프레임에 많아야 하나이며*, `return`이 그 보호 장치의 전부입니다. 서로를 겨눈 텔레포터
 * 둘은 제작자가 그려도 되는 것이고 계속 검사하는 쪽에게는 무한 루프입니다. 두 번째 부피 안에
 * 도착하면 같은 프레임에 다시 첫 번째로 보내지고, 그 사이에 한 프레임도 그려지지 않은 채로
 * 영원히 그렇게 됩니다. 첫 도약 뒤에 나가면 플레이어는 다음 것이 작동하기 전에 목적지에서
 * *보이므로*, 루프는 멈춤이 아니라 빠른 왕복이 됩니다.
 *
 * *시야를 의도적으로 낚아챕니다.* 목적지의 `angle`은 나올 때 어디를 보아야 하는지에 대한
 * 제작자의 말이며, 들어갈 때 우연히 향하던 쪽으로 내려놓는 텔레포터는 본 적 없는 방의
 * 구석을 보게 만듭니다. Quake도 같은 이유로 같은 일을 합니다. 이 게임에서 마우스 없이
 * 카메라를 움직이는 유일한 곳이며, 그래서 이곳에 소리 내어 적어 둘 값어치가 있습니다.
 *
 * *운동량은 회전시키며, 유지하지도 버리지도 않습니다.* 벡터를 그대로 두면 북쪽으로 달려 들어간
 * 플레이어가 남쪽을 향한 문에서 북쪽으로 날아 나갑니다. 아무 잘못도 하지 않았는데 벽으로,
 * 전속력으로입니다. 버리면 달리던 플레이어가 그 자리에 멈추는데, 그것은 텔레포터가 그를
 * 붙잡은 것처럼 읽힙니다. 수평 성분을 시야가 돈 것과 같은 각도만큼 회전시키면 플레이어가
 * 실제로 가지고 있던 것, 곧 *가던 방향으로의 속도*가 보존됩니다. 달려 들어가면 달려
 * 나옵니다. 수직 속도는 그대로 둡니다. 텔레포터로 떨어져 들어가 여전히 떨어지며 도착하는 것이
 * 정직한 답이고, 목적지는 바닥만큼이나 쉽게 공중의 한 점일 수 있습니다.
 *
 * @note ::Player::pos에 대해 검사하며, 그것은 눈입니다. 제작자가 그리는 부피는 몸이 걸어
 *       지나가는 문간이므로 그 몸의 어느 점이든 들어서는 것이 사건입니다. 눈은 이 엔진이
 *       ::TriggerDef와 ::HazardDef에 대해 이미 쓰는 것이며, 이곳에서만 다른 것을 쓰면 같은
 *       크기로 그려진 텔레포터와 트리거가 다르게 행동하게 됩니다.
 */
static void step_teleport(World *w) {
    const Level *l = &w->level;
    if (!l->brushes) return;

    for (int i = 0; i < l->n_teleports; i++) {
        const TeleportDef *t = &l->teleports[i];
        if (!brush_point_in(l->brushes, t->first_brush, t->n_brushes,
                            w->player.pos)) continue;

        float turn = t->yaw - w->yaw;
        float c = cosf(turn), s = sinf(turn);
        float vx = w->player.vel.x, vz = w->player.vel.z;

        /* THE MARKER IS THE FEET AND ::Player::pos IS THE EYE, which is the
           convention ::player_spawn already uses: a `start` at floor level
           becomes `pos.y = PLAYER_EYE`. Putting the eye at the destination
           instead sinks the body a whole ::PLAYER_EYE into the floor, and what
           that looks like is not a teleporter that fails -- it is one that
           works and then shoves the player 1.7m upward on the next frame, which
           reads as the destination being wrong rather than the offset.
           *표식은 발이고 ::Player::pos는 눈이며*, 그것이 ::player_spawn이 이미 쓰는
           약속입니다. 바닥 높이의 `start`는 `pos.y = PLAYER_EYE`가 됩니다. 대신 눈을
           목적지에 두면 몸이 ::PLAYER_EYE만큼 통째로 바닥에 잠기며, 그 모습은 실패한
           텔레포터가 아닙니다. 작동한 다음 다음 프레임에 플레이어를 1.7m 위로 밀어 올리는
           텔레포터이고, 그것은 오프셋이 아니라 목적지가 틀린 것처럼 읽힙니다. */
        w->player.pos   = v3f(t->dest.x, t->dest.y + PLAYER_EYE, t->dest.z);
        w->yaw          = t->yaw;
        w->player.vel.x = vx * c - vz * s;
        w->player.vel.z = vx * s + vz * c;

        /* The landing thump, because there is no teleport sound in the recipe
           table and this is the closest thing in it to arriving somewhere. A
           silent teleport reads as a rendering glitch: the room changes and
           nothing says the game did it on purpose.
           착지음입니다. 레시피 표에 텔레포트 소리가 없고, 그 안에서 *어딘가에 도착하는 것*에
           가장 가까운 것이 이것이기 때문입니다. 소리 없는 텔레포트는 렌더링 결함으로
           읽힙니다. 방이 바뀌는데 게임이 일부러 그랬다고 말하는 것이 아무것도 없습니다. */
        audio_play_at("hland", 90, w->player.pos);
        return;
    }
}

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

/**
 * @brief Where a cleared wave's reward lands, as loot.txt asks for it.
 *
 * ENGLISH
 * -------
 * THE FALLBACK IS THE OLD BEHAVIOUR, and it is what makes `at altar` safe to
 * leave switched on for the whole episode: a map that placed no altar pays at
 * the player's feet exactly as it always did, rather than dropping the purse at
 * the origin or refusing to pay. An author adds the marker when they want the
 * shrine, and every level without one is unaffected.
 *
 * The nearest altar rather than the first, because a map may have several and
 * the one that means anything is the one in the room the fight happened in.
 * "First in the entity list" is a property of how the map was saved.
 *
 * @param[in] w The world, read for the player, the level and the spawners.
 * @return A point on the floor, feet height.
 *
 * 한국어
 * ------
 * @brief 정리된 웨이브의 보상이 놓이는 자리이며, loot.txt가 요청하는 대로입니다.
 *
 * *되돌아가는 곳이 예전 동작이며*, 그것이 `at altar`를 에피소드 전체에 켜 둔 채로 두어도
 * 안전한 이유입니다. 제단을 배치하지 않은 맵은 몫을 원점에 떨어뜨리거나 지급을 거부하는
 * 대신, 늘 그랬듯 정확히 플레이어의 발치에 지급합니다. 제작자는 제단을 원할 때 표식을
 * 추가하며, 그것이 없는 모든 레벨은 영향을 받지 않습니다.
 *
 * 첫 번째가 아니라 *가장 가까운* 제단인 이유는, 맵에 여럿이 있을 수 있고 그중 의미가 있는
 * 것은 전투가 벌어진 방에 있는 것이기 때문입니다. "엔티티 목록의 첫 번째"는 맵이 어떻게
 * 저장되었는지의 속성일 뿐입니다.
 */
static v3 reward_point(const World *w) {
    v3 feet = v3f(w->player.pos.x, w->player.pos.y - PLAYER_EYE, w->player.pos.z);
    int at = loot_reward()->at;

    if (at == LOOT_AT_ALTAR) {
        float best = 0.0f;
        v3    found = feet;
        int   any = 0;

        for (int i = 0; i < w->level.n_ents; i++) {
            const Entity *e = &w->level.ents[i];
            if (!txt_eq(e->kind, "altar")) continue;

            /* Entity coordinates are centimetres; the level is metres. Settled
               onto whatever floor is under the marker, the same way
               ::pickup_spawn_level settles an item -- a shrine floating above
               its own room is a reward nobody can walk onto.
               엔티티 좌표는 센티미터이고 레벨은 미터입니다. ::pickup_spawn_level이 아이템을
               안착시키는 것과 같은 방식으로 표식 아래의 바닥에 내려놓습니다. 자기 방 위에 떠
               있는 제단은 아무도 걸어 올라갈 수 없는 보상입니다. */
            float x = e->x * 0.01f, z = e->z * 0.01f;
            float f, c;
            if (!level_ground(&w->level, x, z, e->y * 0.01f, 1e9f, &f, &c)) continue;

            float dx = x - feet.x, dz = z - feet.z;
            float d2 = dx * dx + dz * dz;
            if (!any || d2 < best) { best = d2; found = v3f(x, f, z); any = 1; }
        }
        return found;                    /* feet, when the map placed none */
    }

    if (at == LOOT_AT_CENTRE) {
        /* The average of the spawners, which is the middle of the fight rather
           than the middle of the map -- the two are the same only in a room
           somebody drew symmetrically.
           스포너들의 평균이며, 맵의 한가운데가 아니라 *전투*의 한가운데입니다. 둘이 같은
           것은 누군가 대칭으로 그린 방에서뿐입니다. */
        float sx = 0.0f, sz = 0.0f;
        int   n  = 0;
        for (int i = 0, sn = enemy_spawner_count(&w->pools); i < sn; i++) {
            const Spawner *sp = enemy_spawner_at(&w->pools, i);
            if (!sp) continue;
            sx += sp->pos.x; sz += sp->pos.z; n++;
        }
        if (n) {
            float x = sx / (float)n, z = sz / (float)n, f, c;
            if (level_ground(&w->level, x, z, w->player.pos.y, 1e9f, &f, &c))
                return v3f(x, f, z);
        }
        return feet;
    }

    return feet;
}

/**
 * @brief Lights the drop point as a shrine, and says so in three layers.
 *
 * ENGLISH
 * -------
 * The ring and the core fire once, here; the motes are paced by ::step_altar
 * for as long as ::RunState::altar_time lasts. The split is the same one the
 * lava smoke makes and for the same reason: an effect that must still be
 * visible in five seconds cannot be a burst, and a burst is the only thing
 * ::fx_spawn knows how to be.
 *
 * @param[in,out] w  The world whose run remembers the shrine.
 * @param[in]     at Where it burns, feet height.
 *
 * 한국어
 * ------
 * @brief 낙하 지점을 제단으로 켜고, 세 겹으로 그것을 말합니다.
 *
 * 고리와 코어는 이곳에서 한 번 발사되고, 티끌은 ::RunState::altar_time이 지속되는 동안
 * ::step_altar가 조절해 뿌립니다. 이 분리는 용암 연기가 하는 것과 같으며 이유도 같습니다.
 * 5초 뒤에도 보여야 하는 이펙트는 폭발일 수 없고, ::fx_spawn이 될 줄 아는 것은 폭발뿐입니다.
 */
/* TAKES THE BURN RATHER THAN READING IT. It used to call `loot_reward()->altar`
   itself, which was correct while a wave was the only thing that paid. A boss
   purse has its own, longer burn -- and a function that reaches for the wave's
   table would light a boss's shrine for a wave's duration while its own number
   sat in the block that was actually paid out.
   Handed the number for the reason scene.h hands its passes finished facts:
   deciding which table applies is the caller's question, not this one's.
   *연소 시간을 읽지 않고 건네받습니다.* 이전에는 스스로 `loot_reward()->altar`를 불렀고, 지급하는
   것이 웨이브뿐인 동안에는 그것이 옳았습니다. 보스 지갑은 자기만의 더 긴 연소 시간을 가지며,
   웨이브의 표로 손을 뻗는 함수는 실제로 지급된 블록에 자기 숫자가 있는데도 보스의 제단을 웨이브의
   길이만큼 태우게 됩니다.
   숫자를 건네받는 이유는 scene.h가 자기 패스들에 완성된 사실을 건네는 것과 같습니다. 어느 표가
   적용되는지는 호출자의 질문이지 이 함수의 질문이 아닙니다. */
static void altar_light(World *w, v3 at, float burn) {
    if (burn <= 0.0f) return;

    w->run.altar_pos  = at;
    w->run.altar_time = burn;
    w->run.altar_mote = 0.0f;          /* the first mote is due immediately */

    fx_spawn(&w->pools, "altarring", at, v3f(0, 1, 0));
    fx_spawn(&w->pools, "altarcore", v3f(at.x, at.y + 0.3f, at.z), v3f(0, 1, 0));
}

/**
 * @brief Throws a cleared wave's reward down at the point loot.txt asks for.
 *
 * ENGLISH
 * -------
 * SCATTERED BY INDEX RATHER THAN AT RANDOM. Item k of n leaves along the k-th
 * of n evenly spaced directions, so the drop is a ring and every run produces
 * the same one. Random angles would look no better -- a dozen items from one
 * point read as a burst either way -- and would make this the one thing in a
 * recorded demo that replays differently, for nothing.
 *
 * WHAT is thrown, and WHERE, both come from assets\loot.txt now. The purse used
 * to be two constants in world.h and the drop point was always the player's
 * feet; the reason it moved is that neither number could be tried without a
 * rebuild, and a reward economy that cannot be tried is one nobody tunes.
 * ::pickup_toss still refuses to be collected in flight, so standing on the drop
 * point does not absorb the reward before it is seen.
 *
 * @param[in,out] w World whose player is being paid.
 * @note `held` entries walk the roster -- see ::loot_held_kind -- so a box for
 *       a gun the player has not found is never thrown.
 *
 * 한국어
 * ------
 * @brief 정리된 웨이브의 보상을 loot.txt가 요청하는 자리에 던집니다.
 *
 * 무작위가 아니라 *인덱스로* 흩뿌립니다. n개 중 k번째 아이템은 균등하게 나뉜 n개 방향 중
 * k번째로 떠나므로, 드롭은 고리 모양이고 모든 플레이가 같은 것을 만들어 냅니다. 무작위 각도가
 * 더 나아 보이지도 않으며(한 지점에서 나온 열몇 개는 어느 쪽이든 폭발로 읽힙니다), 기록된
 * 데모에서 다르게 재생되는 유일한 것을 아무 대가 없이 만들게 됩니다.
 *
 * *무엇을* 던지는지와 *어디에* 던지는지가 이제 둘 다 assets\loot.txt에서 옵니다. 몫은
 * world.h의 상수 둘이었고 낙하 지점은 언제나 플레이어의 발치였습니다. 그것이 옮겨 간 이유는
 * 어느 숫자도 재빌드 없이는 시험해 볼 수 없었기 때문이며, 시험해 볼 수 없는 보상 경제는
 * 아무도 조율하지 않는 경제입니다. ::pickup_toss는 여전히 비행 중 획득을 거부하므로, 낙하
 * 지점에 서 있어도 보상이 보이기 전에 흡수되지 않습니다.
 *
 * @param[in,out] w 플레이어가 보상받는 월드.
 */
/* Throws one purse. Was ::wave_reward's whole body until a boss needed the same
   twenty lines with a different table -- and this file's own history says what
   happens when a monster can be created two ways, or a level rebuilt in three
   places: the fix goes into one of them.
   지갑 하나를 던집니다. 보스가 다른 표로 같은 스무 줄을 필요로 하기 전까지는
   ::wave_reward의 본문 전부였습니다. 그리고 이 파일 자신의 이력이, 몬스터를 만드는 방법이
   둘이거나 레벨을 세 곳에서 다시 만들 때 무슨 일이 일어나는지 말해 줍니다. 수정은 그중 하나에만
   들어갑니다. */
static void reward_items(World *w, const LootReward *r) {

    /* What is actually going to be thrown, gathered first: the ring is spaced
       by how many there are, and a `held` entry may produce nothing at all.
       Spacing by the intended count instead would leave a gap in the ring
       wherever a weapon was not owned.
       실제로 던져질 것을 먼저 모읍니다. 고리의 간격은 개수로 정해지는데, `held` 항목은
       아무것도 만들어 내지 않을 수 있습니다. 의도한 개수로 간격을 잡으면 보유하지 않은
       무기가 있는 자리마다 고리에 틈이 생깁니다. */
    int kinds[LOOT_REWARD_MAX];
    int n = 0, gun = 0;

    /* The cap is in the OUTER condition too, so a purse that overflows raises
       ::DIAG_PICKUP_CAP once rather than once per remaining entry -- a counter
       that reports how many lines the file has left is not reporting the fault.
       상한을 바깥 조건에도 둡니다. 그래야 넘치는 몫이 남은 항목마다가 아니라 *한 번*
       ::DIAG_PICKUP_CAP을 올립니다. 파일에 몇 줄이 남았는지를 보고하는 계수기는 결함을
       보고하는 것이 아닙니다. */
    for (int i = 0; i < r->n_items && n < LOOT_REWARD_MAX; i++) {
        for (int k = 0; k < r->item[i].n; k++) {
            if (n >= LOOT_REWARD_MAX) { DIAG(DIAG_PICKUP_CAP); break; }

            int kind = r->item[i].kind;
            if (kind == LOOT_HELD) {
                kind = loot_held_kind(&w->weapon, &gun);
                if (kind < 0) break;         /* holding nothing that takes ammo */
            }
            kinds[n++] = kind;
        }
    }

    if (!n) return;

    v3 at = reward_point(w);

    for (int i = 0; i < n; i++) {
        float a = 6.2831853f * (float)i / (float)n;
        v3 vel = v3f(cosf(a) * r->out, r->up, sinf(a) * r->out);
        pickup_toss(&w->pools, kinds[i], at, vel);
    }

    altar_light(w, at, r->altar);
}

static void wave_reward(World *w) {
    reward_items(w, loot_reward());
}

/**
 * @brief Keeps a lit shrine burning, and lets it go out.
 *
 * ENGLISH
 * -------
 * Paced rather than spawned once, for the reason ::altar_light gives: a burst is
 * over in half a second and the breather is ::WORLD_WAVE_BREAK long. The
 * interval is a constant here rather than in loot.txt because it is a property
 * of the EFFECT -- how dense a column of motes reads -- and effects.txt is where
 * a person tunes how that column looks. What loot.txt owns is how long it lasts,
 * which is a property of the reward.
 *
 * 한국어
 * ------
 * @brief 켜진 제단을 계속 타오르게 하고, 꺼지게 둡니다.
 *
 * 한 번 생성하지 않고 조절해 뿌리는 이유는 ::altar_light가 대는 것과 같습니다. 폭발은 0.5초면
 * 끝나고 휴식은 ::WORLD_WAVE_BREAK만큼입니다. 간격이 loot.txt가 아니라 이곳의 상수인 이유는
 * 그것이 *이펙트*의 속성(티끌 기둥이 얼마나 촘촘하게 읽히는가)이기 때문이며, 그 기둥이 어떻게
 * 보이는지를 사람이 조정하는 곳은 effects.txt입니다. loot.txt가 소유하는 것은 그것이 얼마나
 * 오래 지속되는지이며, 그것은 보상의 속성입니다.
 */
static void step_altar(World *w, float dt) {
    if (w->run.altar_time <= 0.0f) return;

    w->run.altar_time -= dt;
    if (w->run.altar_time <= 0.0f) { w->run.altar_time = 0.0f; return; }

    w->run.altar_mote -= dt;
    if (w->run.altar_mote > 0.0f) return;
    w->run.altar_mote = ALTAR_MOTE_INTERVAL;

    fx_spawn(&w->pools, "altarmote",
             v3f(w->run.altar_pos.x, w->run.altar_pos.y + 0.2f, w->run.altar_pos.z),
             v3f(0, 1, 0));
}

/**
 * @brief Runs the arena: starts waves, notices they are cleared, times the rest.
 *
 * ENGLISH
 * -------
 * Does nothing at all on a level with no spawners, which is every level that is
 * not an arena. That test is first and is the reason this can be called
 * unconditionally from ::world_step without every ordinary level growing a wave
 * counter it never uses.
 *
 * WHY THE CLEAR TEST IS ::enemy_wave_done AND NOT "no monsters alive". A wave
 * is cleared when nothing is left to send AND nothing is still walking; asking
 * only the second question ends the wave in the gap between two groups, which
 * is every few seconds. See that function.
 *
 * 한국어
 * ------
 * @brief 아레나를 돌립니다. 웨이브를 시작하고, 정리된 것을 알아채고, 휴식을 잽니다.
 *
 * 스포너가 없는 레벨에서는 아무것도 하지 않으며, 그것이 아레나가 아닌 모든 레벨입니다. 그
 * 검사가 맨 앞에 있는 것이, 평범한 레벨마다 쓰지도 않을 웨이브 계수기를 갖지 않고도 이 함수를
 * ::world_step에서 조건 없이 부를 수 있는 이유입니다.
 *
 * 정리 판정이 "살아 있는 몬스터 없음"이 아니라 ::enemy_wave_done인 이유. 웨이브는 보낼 것이
 * 남지 않았고 *동시에* 걸어다니는 것도 없을 때 정리됩니다. 두 번째만 물으면 두 무리 사이의
 * 빈틈에서 웨이브가 끝나며, 그것은 몇 초마다입니다.
 */
/* Puts a line on the boss banner, if this is a game that has one.
 *
 * ONE GATE, NOT FIVE. Every ::BossLine is story-only, and five separate
 * `if (!endless)` tests at five call sites is four chances for the sixth line
 * somebody adds to be the one that speaks in endless mode.
 *
 * Re-armed to the FULL time on every call rather than only when the previous
 * line has expired -- door.h's rule, and for its reason: a message that only
 * re-arms when empty blinks instead of holding.
 *
 * 이 게임이 배너를 가진 게임이라면 배너에 대사를 올립니다.
 *
 * *다섯이 아니라 하나의 게이트입니다.* 모든 ::BossLine은 스토리 전용이고, 다섯 호출 지점에
 * 흩어진 `if (!endless)` 다섯 개는 누군가 나중에 추가하는 여섯 번째 대사가 무한 모드에서
 * 말하게 될 기회를 넷 만듭니다.
 *
 * 이전 대사가 만료되었을 때만이 아니라 호출할 때마다 *만땅으로* 재장전합니다. door.h의
 * 규칙이며 그 이유도 같습니다. 비었을 때만 재장전하는 메시지는 유지되는 대신 깜빡입니다. */
static void boss_say(World *w, int line) {
    if (w->run.endless) return;
    w->run.boss_line   = line;
    w->run.boss_line_t = BOSS_LINE_TIME;
}

/* What a beaten maw pays. ::wave_reward's shape, reading the other block.
   쓰러진 아귀가 지급하는 것. 다른 블록을 읽는 ::wave_reward의 형태입니다. */
static void boss_reward(World *w) {
    reward_items(w, loot_boss_reward());
}

/* --- one frame of the boss fight ---------------------------------------
 *
 * ENGLISH
 * -------
 * BEFORE ::step_wave, and for ::step_wave's own reason for being before
 * ::step_exit: what ends this room has to resolve before what merely counts it.
 * A maw that dies on the same frame a wave clears must be the thing that
 * happened.
 *
 * IT DOES NOT DECIDE DEATH. ::world_step's ordering warning requires that death
 * be noticed in exactly one place after every source of damage, and the maw's
 * death is noticed where every other monster's is -- in ::enemy_hurt, through
 * the health boundary that ::BOSS_CYCLES makes zero on the last cycle. What
 * this function does is notice that the boss it was watching is GONE, which is
 * a different question and one only it is asking.
 *
 * 한국어
 * ------
 * ::step_wave보다 *앞*이며, ::step_wave가 ::step_exit보다 앞인 것과 같은 이유입니다. 이 방을
 * 끝내는 것은 그것을 세기만 하는 것보다 먼저 결판나야 합니다. 웨이브가 정리되는 것과 같은
 * 프레임에 죽는 아귀는, 일어난 일이 그것이어야 합니다.
 *
 * *이 함수는 사망을 결정하지 않습니다.* ::world_step의 순서 경고는 사망이 모든 피해원 이후
 * 정확히 한 곳에서 감지될 것을 요구하며, 아귀의 사망도 다른 모든 몬스터와 같은 곳에서
 * 감지됩니다. ::enemy_hurt에서, ::BOSS_CYCLES가 마지막 사이클에 0으로 만드는 체력 경계를
 * 통해서입니다. 이 함수가 하는 일은 자신이 지켜보던 보스가 *사라졌다*는 것을 알아채는 것이고,
 * 그것은 다른 질문이며 오직 이 함수만이 묻고 있는 질문입니다. */
static void step_boss(World *w, float dt) {
    BossFight *b = &w->pools.enemy.boss;
    int idx = enemy_boss_index(&w->pools);

    /* --- it was here last frame and is not now --------------------------
       The one place the fight ends. Checked before everything else so a maw
       that died this frame cannot also be found groggy, re-warded, or counted
       into a cycle it did not live to see.
       전투가 끝나는 유일한 곳입니다. 다른 무엇보다 먼저 검사하므로, 이번 프레임에 죽은 아귀가
       동시에 그로기로 발견되거나, 결계가 다시 세워지거나, 살아서 보지 못한 사이클에
       계산되는 일이 없습니다. */
    if (b->active && idx < 0) {
        b->active     = 0;
        b->cycle      = 0;
        b->groggy     = 0.0f;
        b->was_groggy = 0;

        /* SUPPRESSION LIFTED HERE AND ONLY HERE, so the two modes cannot
           disagree about it: story mode is over anyway and endless mode gets
           its arena back at full rate until the next maw is due.
           억제는 이곳에서만 해제되므로 두 모드가 그에 대해 어긋날 수 없습니다. 스토리 모드는
           어차피 끝났고, 무한 모드는 다음 아귀가 예정될 때까지 아레나를 온전한 속도로
           돌려받습니다. */
        w->pools.enemy.spawn_slow = 0.0f;

        boss_reward(w);
        boss_say(w, BOSS_LINE_DIE);

        /* STORY MODE DOES NOT WIN HERE, and the reason is the line just
           posted. ::world_frozen counts `won`, and ::scene_frame suppresses the
           banner once a run is over -- so raising `won` on this frame would
           post the maw's last sentence and cover it with the win screen in the
           same frame. Nobody would ever see it, and the bug would look like a
           line that was never written.
           So the win waits for the line to expire; the banner clock below is
           what raises it. Everything is already dead, so the seconds in between
           cost nothing -- and they are where a victory cutscene slots in when
           there is one.
           스토리 모드는 이곳에서 이기지 않으며, 그 이유는 방금 게시한 대사입니다.
           ::world_frozen이 `won`을 세고 ::scene_frame은 플레이가 끝나면 배너를 억제하므로,
           이 프레임에 `won`을 세우면 아귀의 마지막 문장을 게시하고 같은 프레임에 승리 화면으로
           덮게 됩니다. 아무도 그것을 보지 못하고, 그 버그는 애초에 쓰이지 않은 대사처럼
           보입니다.
           그래서 승리는 대사가 만료되기를 기다립니다. 그것을 세우는 것은 아래의 배너
           시계입니다. 이미 전부 죽어 있으므로 그 사이의 몇 초는 아무 비용도 들지 않으며,
           승리 컷신이 생기는 날 그것이 끼어들 자리이기도 합니다. */
        if (w->run.endless)
            b->next_wave = (short)(w->run.wave + WORLD_BOSS_EVERY);
        return;
    }

    /* --- endless mode's FIRST appointment --------------------------------
       Without this the respawn below never fires: it wants `next_wave > 0` and
       nothing had ever set it, so an endless run met no maw at all and the
       whole boss half of that mode was unreachable. The story branch could not
       cover for it either -- it is gated on `!endless` by construction.
       tools/bosstest.c found this by asserting that the endless maw could be
       killed and getting "yes" from a pool that never had one. Three checks
       were passing vacuously.
       Booked on the same interval as every later one, so "every five waves" is
       true from the first appearance rather than from the second.
       이것이 없으면 아래의 재소환이 결코 발동하지 않습니다. 그것은 `next_wave > 0`을 원하는데
       그것을 세운 적이 아무도 없었으므로, 무한 플레이는 아귀를 아예 만나지 못했고 그 모드의
       보스 절반이 통째로 도달 불가였습니다. 스토리 분기가 대신해 줄 수도 없습니다. 그것은
       구조적으로 `!endless`로 막혀 있습니다.
       tools/bosstest.c가 무한 모드의 아귀를 죽일 수 있다고 단언했다가, 애초에 아귀를 가진 적
       없는 풀에서 "그렇다"를 받아 이것을 찾았습니다. 세 검사가 공허하게 통과하고 있었습니다.
       이후의 모든 약속과 같은 간격으로 예약하므로, "5웨이브마다"가 두 번째 등장이 아니라
       첫 등장부터 참입니다. */
    if (w->run.endless && !b->have_started && b->next_wave == 0) {
        b->have_started = 1;
        b->next_wave    = WORLD_BOSS_EVERY;
    }

    /* --- when the next one is due ---------------------------------------
       ENDLESS ONLY, and refused while one already stands. The wave clock and
       the boss's own life are two clocks that can disagree -- a fight lasting
       longer than ::WORLD_BOSS_EVERY waves would otherwise summon a second maw
       into the first one's fight, and the health bar has room for one subject.
       Scheduled from the wave the last one DIED on rather than from a multiple,
       so a long fight pushes the next appointment out instead of arriving the
       instant it ends.
       무한 모드 전용이며, 이미 하나가 서 있으면 거절합니다. 웨이브 시계와 보스 자신의 수명은
       서로 어긋날 수 있는 두 시계입니다. ::WORLD_BOSS_EVERY 웨이브보다 오래 끄는 전투는 그러지
       않으면 첫 아귀의 전투에 두 번째 아귀를 소환하게 되고, 체력바에는 대상 하나의 자리밖에
       없습니다. 배수가 아니라 직전 아귀가 *죽은* 웨이브에서부터 예약하므로, 긴 전투는 다음
       약속을 밀어낼 뿐 전투가 끝나는 순간 도착하지 않습니다. */
    if (!b->active && idx < 0 && w->run.endless &&
        b->next_wave > 0 && w->run.wave >= b->next_wave) {
        if (enemy_boss_summon(&w->pools, &w->level)) {
            b->next_wave = 0;
            idx = enemy_boss_index(&w->pools);
        }
    }

    /* --- story mode starts its fight after one wave, not on arrival ------
       ONE WAVE, and this used to be zero. The comment here argued that "the
       story arena IS the boss fight, and a player who has to survive to wave
       five to meet it is playing endless mode with a cutscene" -- exact about
       `glasstower`, which was seven brushes and nothing but ward slots, and
       false about the 807-brush deathmatch map that replaced it. Measured on
       that map, the maw stood up ONE FRAME after the intro cutscene ended, at
       wave 1, four wards already placed and five monsters already walking: the
       player met the boss before reaching any of the twenty-six weapons and
       pickups the map lays out, in a room they had not seen.
       The sentence rejecting FIVE waves is still right. It was not about one.
       See ::WORLD_BOSS_STORY_WAVE for the whole of the reasoning.
       *한 웨이브이며*, 이것은 0이었습니다. 이곳의 주석은 "스토리 아레나가 곧 보스전이며,
       그것을 만나기 위해 5웨이브를 버텨야 하는 플레이어는 컷신이 붙은 무한 모드를 하고 있는
       것"이라고 주장했습니다. 브러시 일곱 개에 결계핵 자리뿐이던 `glasstower`에 대해서는
       정확했고, 그것을 대신한 브러시 807개짜리 데스매치 맵에 대해서는 틀렸습니다. 그 맵에서
       재어 보니 아귀는 인트로 컷신이 끝난 *한 프레임 뒤*에 일어섰습니다. 다섯을 거부한 문장은
       여전히 옳습니다. 그것은 하나에 대한 말이 아니었습니다. */
    if (!b->active && idx < 0 && !w->run.endless && b->next_wave == 0 &&
        !b->have_started && w->run.wave >= WORLD_BOSS_STORY_WAVE) {
        if (enemy_boss_summon(&w->pools, &w->level)) {
            b->have_started = 1;
            idx = enemy_boss_index(&w->pools);
        }
    }

    if (idx < 0) return;

    /* --- it has just arrived --------------------------------------------- */
    if (!b->active) {
        b->active     = 1;
        b->cycle      = 0;
        b->groggy     = 0.0f;
        b->was_groggy = 0;
        w->pools.enemy.spawn_slow = WORLD_BOSS_SPAWN_SLOW;
        enemy_ward_place(&w->pools, &w->level);
        world_shake(w, WORLD_SHAKE_HURT);
        boss_say(w, BOSS_LINE_WAKE);
        return;
    }

    if (enemy_guards_alive(&w->pools) > 0) {
        /* Warded. The clock is not running and must be put back, or a window
           interrupted by a re-ward would resume already half spent.
           수호 중입니다. 시계는 돌고 있지 않으며 되돌려야 합니다. 그러지 않으면 재점화로
           중단된 창이 이미 절반 소모된 채로 재개됩니다. */
        b->groggy     = 0.0f;
        b->was_groggy = 0;
        return;
    }

    /* --- groggy ---------------------------------------------------------- */
    const Enemy *e = enemy_at(&w->pools, idx);
    const MonType *S = mon_stats(e->type);

    if (!b->was_groggy) {
        b->was_groggy = 1;
        world_shake(w, WORLD_SHAKE_HURT);
        /* AT THE BOSS, not at the player, so the sound points. The player is by
           construction looking away from the maw when the last ward falls --
           that is the fight's design -- and a non-positional cue would tell
           them something happened without telling them where.
           플레이어가 아니라 *보스 위치*에서 재생하므로 소리가 방향을 가리킵니다. 마지막
           결계핵이 쓰러질 때 플레이어는 구조적으로 아귀를 등지고 있으며(그것이 이 전투의
           설계입니다), 위치 없는 신호는 무언가 일어났다고만 말하고 어디인지는 말하지
           않습니다. */
        audio_play_at("bossopen", 100, enemy_at(&w->pools, idx)->pos);
        boss_say(w, BOSS_LINE_OPEN);
    }

    b->groggy += dt;

    /* Hurt while open. Held off for ::BOSS_LINE_HIT_DELAY because the player is
       already shooting when the last ward falls, so without it this line
       overwrites ::BOSS_LINE_OPEN a few frames after it appeared -- with a line
       that says almost the same thing.
       열린 채로 다치고 있습니다. ::BOSS_LINE_HIT_DELAY만큼 미루는 이유는, 마지막 결계핵이
       쓰러질 때 플레이어가 이미 쏘고 있기 때문입니다. 그러지 않으면 이 대사가
       ::BOSS_LINE_OPEN을 나타난 지 몇 프레임 만에 덮어쓰는데, 거의 같은 말을 하는 대사로
       덮어씁니다. */
    if (b->groggy > BOSS_LINE_HIT_DELAY && e->flash > 0.0f &&
        w->run.boss_line != BOSS_LINE_HIT)
        boss_say(w, BOSS_LINE_HIT);

    int ceiling = S->hp * (BOSS_CYCLES - b->cycle)     / BOSS_CYCLES;
    int floor_hp = S->hp * (BOSS_CYCLES - b->cycle - 1) / BOSS_CYCLES;

    /* --- the window expires ---------------------------------------------
       THE DEFENCE AGAINST A FIGHT THAT CANNOT END. The window has no timer by
       design -- it ends when the health crosses the boundary, which is what
       makes the cycle count exact -- and that is a promise a player can decline
       by running out of ammunition, by losing the maw behind them, or simply by
       stopping. So it is taken back: the wards return and the health goes up to
       the ceiling of the segment it started at. The cycle does NOT advance.
       The bar visibly refilling is the only honest way to say the window is
       gone; a bar that stayed put would report progress the player no longer
       has. See ::BOSS_GROGGY_MAX.
       끝날 수 없는 전투에 대한 방어입니다. 이 창은 설계상 타이머가 없습니다. 체력이 경계를
       넘을 때 끝나고, 그것이 사이클 수를 정확하게 만듭니다. 그런데 그것은 플레이어가 탄약이
       떨어져서, 아귀를 등 뒤로 놓쳐서, 또는 그냥 멈춤으로써 거절할 수 있는 약속입니다. 그래서
       회수합니다. 결계핵이 돌아오고 체력은 시작했던 구간의 천장까지 올라갑니다. 사이클은
       오르지 *않습니다.*
       바가 눈에 띄게 되차오르는 것이 창이 날아갔다고 말하는 유일하게 정직한 방법입니다.
       그대로 머무는 바는 플레이어가 더 이상 갖고 있지 않은 진척을 보고합니다.
       ::BOSS_GROGGY_MAX를 참조하십시오. */
    if (b->groggy >= BOSS_GROGGY_MAX) {
        enemy_boss_heal(&w->pools, ceiling);
        b->groggy     = 0.0f;
        b->was_groggy = 0;
        enemy_ward_place(&w->pools, &w->level);
        world_shake(w, WORLD_SHAKE_HURT);
        audio_play_at("bossward", 100, e->pos);
        boss_say(w, BOSS_LINE_WARD);
        return;
    }

    /* --- the boundary was crossed ---------------------------------------
       The last boundary is zero (::types_check enforces the divisibility), so
       reaching it is a death and ::enemy_hurt has already performed it. This
       branch is only ever the first two.
       마지막 경계는 0이므로(나누어떨어짐은 ::types_check가 강제합니다) 그곳에 닿는 것은 곧
       사망이고 ::enemy_hurt가 이미 처리했습니다. 이 분기는 언제나 앞의 둘뿐입니다. */
    if (e->health <= floor_hp && floor_hp > 0) {
        b->cycle++;
        b->groggy     = 0.0f;
        b->was_groggy = 0;
        enemy_ward_place(&w->pools, &w->level);
        audio_play_at("bossward", 100, e->pos);
        boss_say(w, BOSS_LINE_WARD);
    }
}

static void step_wave(World *w, float dt) {
    if (!enemy_spawner_count(&w->pools)) return;

    /* --- the first wave -------------------------------------------------
       Started here rather than by ::world_load_level, so a level that is an
       arena becomes one by having spawners rather than by being named -- and so
       an arena entered from a menu, a restart or a transition all begin the
       same way without any of them being asked to.
       ::world_load_level이 아니라 이곳에서 시작합니다. 그래서 어떤 레벨이 아레나가 되는 것은
       이름이 아니라 스포너를 가졌기 때문이며, 메뉴·재시작·전환 중 무엇으로 들어온 아레나든
       요청받지 않고도 같은 방식으로 시작합니다. */
    if (w->run.wave < 1) {
        w->run.wave      = 1;
        w->run.wave_best = 1;
        w->run.wave_time = 0.0f;
        enemy_wave_arm(&w->pools, 1);
        return;
    }

    /* --- the breather ----------------------------------------------------
       Counted down BEFORE the clear test, so the frame the break ends is the
       frame the next wave arms rather than one that could also notice the new
       wave is already clear -- it is, for the instant before its spawners run.
       정리 판정보다 *먼저* 감소시킵니다. 휴식이 끝나는 프레임이 다음 웨이브를 장전하는
       프레임이 되게 하기 위함이며, 그러지 않으면 그 프레임이 새 웨이브가 이미 정리되었다고
       알아챌 수도 있습니다. 스포너가 돌기 직전 한순간 실제로 그러합니다. */
    if (w->run.wave_break > 0.0f) {
        w->run.wave_break -= dt;
        if (w->run.wave_break > 0.0f) return;

        w->run.wave_break = 0.0f;
        w->run.wave++;
        if (w->run.wave > w->run.wave_best) w->run.wave_best = w->run.wave;
        w->run.wave_time = 0.0f;
        enemy_wave_arm(&w->pools, w->run.wave);
        return;
    }

    w->run.wave_time += dt;

    /* Paid on the frame the wave is noticed to be over, not when the breather
       ends: the reward has to be in the air while the room is going quiet,
       which is the moment it reads as payment for what just happened.
       휴식이 끝날 때가 아니라 웨이브가 끝났음을 알아챈 프레임에 지급합니다. 보상은 방이
       조용해지는 동안 공중에 있어야 하며, 그때가 방금 일어난 일에 대한 대가로 읽히는
       순간입니다. */
    if (enemy_wave_done(&w->pools)) {
        w->run.wave_break = WORLD_WAVE_BREAK;
        wave_reward(w);
    }
}

/* Advances the between-levels screen, and loads when it is done.
 *
 * The load is here rather than in step_exit so there is exactly one place that
 * changes which level is running -- see world_load_level's own note on that.
 * How long the screen stays up is ::WORLD_BETWEEN_TIME, and the reasoning for
 * that duration lives beside the constant in world.h rather than being restated
 * here -- a second copy of it sat at this spot until the constant moved, and
 * two copies of a rationale is how one of them comes to describe a number that
 * is no longer the number.
 * 로드가 step_exit이 아니라 이곳에 있는 이유는, 어느 레벨이 도는지를 바꾸는 곳이 정확히
 * 하나이도록 하기 위해서입니다. 화면이 떠 있는 시간은 ::WORLD_BETWEEN_TIME이며, 그 시간에
 * 대한 근거는 이곳에 다시 적지 않고 world.h의 상수 곁에 둡니다. 상수가 옮겨 간 뒤에도 그
 * 사본이 이 자리에 남아 있었으며, 근거의 사본이 둘이라는 것은 그중 하나가 더 이상 그 숫자가
 * 아닌 숫자를 설명하게 되는 방식입니다. */
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

void world_init(World *w) { world_init_in(w, 0); }

void world_init_in(World *w, BrushStore *bs) {
    World zero = {0};
    *w = zero;

    /* AFTER the clear, or it would be cleared. NULL is the default store and is
       what the zeroing already produced, so the ordinary path assigns nothing
       meaningful -- this line exists for the caller that named one.
       초기화 *뒤*여야 합니다. 그러지 않으면 함께 지워집니다. NULL이 기본 저장소이고 0으로
       비우는 것이 이미 그 값을 만들었으므로 평범한 경로에서는 의미 있는 대입이 아닙니다. 이
       줄은 저장소를 지목한 호출자를 위해 있습니다. */
    w->store = bs;

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
    if (!level_load_in(w->store, want, &w->level)) return 0;

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
        while (n < LVL_KIND && k[n]) n++;

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
static int stage_walk(const char *root, const char *name,
                      PlayerProgress *out, Level *scan) {
    PlayerProgress p;
    progress_boot(&p);

    char at[WORLD_LEVEL_MAX];
    txt_copy(at, sizeof(at), root, -1);

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

        /* THE DEFAULT STORE, deliberately, even for a world that named its own.
           The scratch is transient -- taken here and given back by the caller
           before the frame ends -- and it never becomes a world's level, so it
           has no reason to occupy a slot in that world's storage. Loading it
           into the default store also means a world with its own store contends
           with nothing at all here, where before the scan and the running level
           shared one pool of two.
           호출자가 자기 저장소를 지목한 월드에 대해서도 *의도적으로* 기본 저장소를 씁니다.
           임시 레벨은 이곳에서 잡혔다가 프레임이 끝나기 전에 호출자가 돌려주며, 결코 어떤
           월드의 레벨이 되지 않으므로 그 월드의 저장 공간에서 슬롯을 차지할 이유가 없습니다.
           기본 저장소에 로드하면 자기 저장소를 가진 월드는 이곳에서 아무것과도 경쟁하지
           않습니다. 이전에는 스캔과 실행 중인 레벨이 슬롯 둘짜리 풀 하나를 공유했습니다. */
        if (!level_load(at, scan)) return 0;    /* a broken link in the chain */

        grant_weapons_of(scan, &p);

        if (!scan->next[0]) return 0;           /* the chain ended first */
        txt_copy(at, sizeof(at), scan->next, -1);
    }

    return 0;   /* longer than any episode, or a loop */
}

int world_progress_for_stage(const char *root, const char *name, PlayerProgress *out) {
    /* One scratch level, reused for every hop. Zeroed once: level_load resets
       the counts and names it fills, so nothing stale is ever read, but a
       19KB local that begins as whatever was on the stack is a bad habit to
       leave lying around next to a parser.
       모든 구간에 재사용하는 임시 레벨 하나입니다. 한 번만 0으로 만듭니다. level_load가
       자신이 채우는 개수와 이름을 초기화하므로 낡은 값을 읽을 일은 없지만, 스택에 있던
       무엇으로 시작하는 19KB 지역 변수를 파서 옆에 남겨 두는 것은 나쁜 습관입니다. */
    Level scan = {0};

    int found = stage_walk(root, name, out, &scan);

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

int world_start_stage(World *w, const char *root, const char *name) {
    PlayerProgress want;
    if (!world_progress_for_stage(root, name, &want)) return 0;

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
    /* THE MODE SURVIVES, and it is the one thing here that has to be named.
       ::run_reset's whole charter is that a field added to ::RunState is
       cleared by construction -- "the previous version named six globals, and
       the two it did not name were the ones nobody noticed" -- so carrying a
       field across it by hand is exactly the shape that argument warns about.
       It is right anyway, because ::RunState::endless is not a fact about this
       run: it is a fact about WHICH GAME is being played, and a restart is a
       retry of that game rather than a return to the menu that chose it.
       Cleared instead, an endless run restarted from the pause menu would come
       back as a story run -- same room, so nothing on screen would say so, and
       the first sign would be a boss banner in a mode that has none.
       ONE FIELD, and the day there is a second one the pair belongs in a struct
       of its own beside ::RunState rather than in a longer list here. That is
       the line this file has already been over once.
       *모드는 살아남으며*, 이곳에서 이름으로 불러야 하는 유일한 것입니다. ::run_reset의 헌장
       전체가 ::RunState에 추가된 필드는 구조적으로 지워진다는 것입니다. "이전 판은 여섯 개의
       전역을 이름으로 나열했고, 나열하지 않은 두 개가 바로 아무도 알아채지 못한 것들이었습니다."
       그러므로 그것을 손으로 건너 나르는 것은 그 논거가 경계하는 바로 그 형태입니다.
       그럼에도 옳습니다. ::RunState::endless는 *이번 플레이*에 대한 사실이 아니라 *어떤 게임을
       하고 있는가*에 대한 사실이고, 재시작은 그 게임의 재시도이지 그것을 고른 메뉴로 돌아가는
       일이 아니기 때문입니다.
       지웠다면, 일시정지 메뉴에서 재시작한 무한 플레이가 스토리 플레이로 돌아옵니다. 같은
       방이므로 화면의 어느 것도 그렇다고 말해 주지 않고, 첫 신호는 그것을 갖지 않은 모드에 뜬
       보스 배너입니다.
       *필드 하나이며*, 두 번째가 생기는 날 그 쌍은 이곳의 더 긴 목록이 아니라 ::RunState 곁의
       자기 구조체에 속합니다. 이 파일이 이미 한 번 지나온 선입니다. */
    int endless = w->run.endless;

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
    w->run.endless = endless;

    /* THE INTRO DOES NOT REPLAY, and nothing here has to say so: ::cut_begin is
       called by ::world_begin and by nothing else, so a restart simply never
       asks. A retry is not a new story, and a player who has died four times
       has read it four times too many.
       *인트로는 다시 재생되지 않으며*, 이곳의 무엇도 그렇게 말할 필요가 없습니다. ::cut_begin을
       부르는 것은 ::world_begin뿐이므로 재시작은 그냥 묻지 않습니다. 재시도는 새 이야기가
       아니며, 네 번 죽은 플레이어는 그것을 네 번 더 읽은 것입니다. */
}

int world_begin(World *w, int endless) {
    /* The load is FIRST, and nothing about the run is touched until it
       succeeds. ::world_load_level promises that a level which will not parse
       changes nothing; clearing the run before asking would break that promise
       from the outside -- the level would be untouched and the title screen
       would be gone, which is a player standing in the backdrop with no menu.
       로드가 *먼저*이며, 성공하기 전까지 플레이에 대해서는 아무것도 건드리지 않습니다.
       ::world_load_level은 파싱되지 않는 레벨이 아무것도 바꾸지 않는다고 약속합니다. 묻기 전에
       플레이를 지우면 그 약속을 바깥에서 깨뜨립니다. 레벨은 그대로인데 타이틀 화면이 사라지고,
       그것은 메뉴도 없이 배경 속에 서 있는 플레이어입니다. */
    if (!world_load_level(w, WORLD_BOSS_ARENA, WORLD_ENTER_NEW)) return 0;

    /* AFTER the load, because ::world_load_level does not clear the run and
       this does: the clocks, the wave counter, the banner and the cutscene all
       belong to a playthrough rather than to a level, and a mode change is the
       start of a new one. title=0 for ::world_restart's reason -- the player
       has already asked to play, and asking again is the title screen they just
       left.
       로드 *뒤*입니다. ::world_load_level은 플레이를 지우지 않고 이것은 지웁니다. 시계, 웨이브
       계수기, 배너, 컷신은 전부 레벨이 아니라 한 번의 플레이에 속하며, 모드 변경은 새 플레이의
       시작입니다. title=0인 것은 ::world_restart의 이유와 같습니다. 플레이어는 이미 플레이하겠다고
       요청했고, 다시 묻는 것은 방금 떠나온 그 타이틀 화면입니다. */
    run_reset(&w->run, 0);

    /* The whole of what the argument decides, in one assignment after the
       zeroing that would otherwise undo it. See ::RunState::endless.
       인자가 결정하는 것의 전부이며, 그러지 않으면 그것을 되돌릴 0 초기화 뒤의 대입 하나입니다.
       ::RunState::endless를 참조하십시오. */
    w->run.endless = endless ? 1 : 0;

    /* Unconditional: ::cut_begin holds the mode gate, so this line says "the
       intro happens here" and nothing about which game it is.
       무조건입니다. 모드 게이트는 ::cut_begin이 쥐고 있으므로, 이 줄은 "인트로는 이곳에서
       일어난다"고 말할 뿐 어떤 게임인지에 대해서는 말하지 않습니다. */
    cut_begin(w, STORY_INTRO);
    return 1;
}

int world_boss_present(const World *w) {
    /* THE FLAG, AT LAST. This function used to answer by scanning for a live
       brute, and world.h stated both the reason and the condition under which
       the answer would move: "If a later bestiary needs a real boss the answer
       moves into ::MonType and this function keeps its signature." The bestiary
       now has one, so it has moved, and the signature is kept.
       The brute deliberately gets nothing in exchange. It was standing in for a
       boss because there was none; leaving it able to switch the soundtrack now
       would mean the boss theme says nothing on the frame the maw arrives.
       마침내 플래그입니다. 이 함수는 살아 있는 브루트를 훑어 답했고, world.h는 그 이유와 답이
       옮겨 가는 조건을 함께 밝혔습니다. "이후의 도감이 진짜 보스를 필요로 하면 답은
       ::MonType으로 옮겨 가고 이 함수는 시그니처를 유지합니다." 이제 도감이 하나를 가졌으므로
       답은 옮겨 갔고 시그니처는 유지됩니다.
       브루트는 그 대가로 아무것도 받지 않으며, 의도적입니다. 보스가 없어서 보스를 대신하고
       있었을 뿐입니다. 지금도 사운드트랙을 바꿀 수 있게 두면, 정작 아귀가 도착하는 프레임에
       보스 테마가 아무 말도 하지 않게 됩니다. */
    return enemy_boss_index(&w->pools) >= 0;
}

void world_shake(World *w, float amount) {
    if (amount <= 0.0f) return;
    if (amount > WORLD_SHAKE_MAX) amount = WORLD_SHAKE_MAX;
    if (amount > w->run.shake) w->run.shake = amount;
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
    /* THE CUTSCENE IS ON THIS LIST AND THAT IS ALL IT TOOK. A cutscene is a
       moment held over a room that is still there, which is the same kind of
       state the intermission is -- and adding it here is what stops the player
       walking, shooting and being shot at behind the words. The clock that ends
       it runs outside the frozen gate; see ::step_cut.
       *컷신이 이 목록에 있고 그것으로 끝이었습니다.* 컷신은 여전히 그 자리에 있는 방 위에
       붙잡아 둔 순간이며, 인터미션과 같은 종류의 상태입니다. 이곳에 더하는 것이 플레이어가 말
       뒤에서 걷고 쏘고 맞는 것을 멈춥니다. 그것을 끝내는 시계는 정지 게이트 바깥에서 돕니다.
       ::step_cut을 참조하십시오. */
    return w->run.won || w->run.dead || w->run.title || w->run.between ||
           w->run.cut || paused;
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
        step_powers(w, dt);
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

    /* --- the defeat cutscene, once the fall has finished ------------------
       NOT on the frame the player dies. ::scene_frame drops the camera over
       ::DEATH_ANIM_TIME and its own note says why: "the fall is the part that
       says what happened". A screen of text over it would cover the one thing
       the player wants to see, so the words wait for the body to land.
       The mode gate is ::cut_begin's, and the once-per-run latch is too --
       which is what makes this safe to ask on every frame `dead` is set, and
       `dead` is set on all of them.
       *플레이어가 죽는 프레임이 아닙니다.* ::scene_frame은 ::DEATH_ANIM_TIME에 걸쳐 카메라를
       떨어뜨리고 자신의 주석이 이유를 말합니다. *"무슨 일이 있었는지 말해 주는 것이 바로 그
       넘어짐입니다."* 그 위에 덮인 텍스트 화면은 플레이어가 보고 싶어 하는 단 하나를 가리므로,
       말은 몸이 착지하기를 기다립니다.
       모드 게이트는 ::cut_begin의 것이고 플레이당 한 번이라는 래치도 그렇습니다. 그것이 `dead`가
       세워진 모든 프레임에서 이것을 물어도 안전하게 만들며, `dead`는 그 모든 프레임에서
       세워져 있습니다. */
    if (w->run.dead && w->run.death_time >= DEATH_ANIM_TIME)
        cut_begin(w, STORY_DEFEAT);
    if (w->player.hurt > 0.0f) w->player.hurt -= dt * 2.0f;

    /* Decays with the world rather than on a clock of its own, so a shake that
       was going when the menu opened is still going when it closes -- and its
       phase, which ::scene_frame reads out of ::RunState::world_time, has not
       run on without it. Gated for the same reason that clock is.
       자기 시계가 아니라 월드와 함께 감쇠하므로, 메뉴가 열릴 때 진행 중이던 흔들림은 메뉴가
       닫힐 때에도 진행 중입니다. 그리고 ::scene_frame이 ::RunState::world_time에서 읽는
       위상도 그것 없이 앞서 나가지 않았습니다. 그 시계와 같은 이유로 게이트를 겁니다. */
    if (!frozen && w->run.shake > 0.0f) {
        w->run.shake -= dt * WORLD_SHAKE_DECAY;
        if (w->run.shake < 0.0f) w->run.shake = 0.0f;
    }

    if (!frozen) step_smoke(w, dt);

    /* The shrine, which outlives the burst that lit it. Frozen with the world
       for the reason everything else here is: a pause menu should not spend the
       breather the player is holding.
       그것을 켠 폭발보다 오래가는 제단입니다. 이곳의 다른 모든 것과 같은 이유로 월드와 함께
       멈춥니다. 일시정지 메뉴가 플레이어가 붙들고 있는 휴식을 소비해서는 안 됩니다. */
    if (!frozen) step_altar(w, dt);


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

    /* AFTER the grenades, because a detonation happens inside ::proj_update and
       the frame a blast goes off is the frame the camera has to move. Frozen
       with the world even though the flash it reads is not -- a menu is a thing
       to read, and reading it through a shaking camera is the shake being
       spent on the one moment it has nothing to confirm.
       유탄 *다음*입니다. 폭발은 ::proj_update 안에서 일어나고, 폭발이 터지는 프레임이 곧
       카메라가 움직여야 하는 프레임이기 때문입니다. 이것이 읽는 섬광은 멈추지 않는데도
       이쪽은 월드와 함께 멈춥니다. 메뉴는 읽는 것이고, 흔들리는 카메라로 읽는 것은 흔들림이
       확인해 줄 것이 아무것도 없는 유일한 순간에 그것을 쓰는 일입니다. */
    if (!frozen) step_blast(w);

    /* Particles advance even on the win screen: freezing the world mid-air
       would strand whatever was in flight when the exit was reached.
       승리 화면에서도 입자는 진행합니다. 월드를 공중에서 정지시키면 출구에 도달한 순간
       날아가던 것들이 그대로 멈춰 버립니다. */
    fx_update(&w->pools, dt);

    /* And the light those explosions left, on the particles' terms rather than
       the projectile's: it belongs to the same event as the burst it is
       lighting, and freezing one while the other keeps expanding leaves a
       white room under a smoke cloud that is still growing. See
       ::proj_flash_update.
       그리고 그 폭발들이 남긴 빛이며, 발사체가 아니라 *입자*의 기준을 따릅니다. 그것은
       자신이 밝히고 있는 폭발과 같은 사건에 속하며, 한쪽만 멈추면 아직 커지고 있는 연기
       구름 아래에 하얀 방이 남습니다. ::proj_flash_update를 참조하십시오. */
    proj_flash_update(&w->pools, dt);

    /* Pickups top up health, ammo and the roster when walked over. The weapon
       is passed whole rather than one ammo pointer, because a box says which
       belt it fills and a weapon pickup fills none of them. */
    if (!frozen)
        pickup_update(&w->pools, &w->level, w->player.pos,
                      &w->player.health, PLAYER_MAX_HP,
                      &w->weapon, &w->player.keys, w->player.power, dt);

    /* Before the exit, because a pad and an exit on the same tile is a level
       bug either way and this order makes it an obvious one: the player is
       thrown into the air rather than quietly finishing the level.
       출구보다 먼저입니다. 같은 칸에 점프대와 출구가 있는 것은 어느 쪽이든 레벨의 결함이며,
       이 순서는 그것을 눈에 띄게 만듭니다. 조용히 레벨이 끝나는 대신 플레이어가 공중으로
       던져집니다. */
    if (!frozen) step_push(w);
    if (!frozen) step_teleport(w);

    /* AFTER the monsters were stepped and BEFORE the exit.
       After, because "is the wave clear" is a question about the state this
       frame produced -- asking before means answering about the previous frame
       and ending a wave one frame after the last monster fell, which is a frame
       in which it is already visibly over.
       Before ::step_exit, because an arena's exit is not what ends it: a wave
       clearing must not race a player standing on an exit pad, and this order
       makes the wave the thing that resolves first.
       몬스터가 진행된 *뒤*이고 출구보다 *앞*입니다.
       뒤인 것은 "웨이브가 정리되었는가"가 이번 프레임이 만들어 낸 상태에 대한 질문이기
       때문입니다. 앞에서 물으면 이전 프레임에 대해 답하게 되고, 마지막 몬스터가 쓰러진 지 한
       프레임 뒤에 웨이브가 끝납니다. 그 한 프레임은 이미 눈에 띄게 끝나 있는 프레임입니다.
       ::step_exit보다 앞인 것은 아레나를 끝내는 것이 출구가 아니기 때문입니다. 웨이브 정리가
       출구 발판에 선 플레이어와 경쟁해서는 안 되며, 이 순서가 웨이브를 먼저 결판나게 합니다. */
    /* BEFORE the wave, and for the wave's own reason for being before the exit:
       what ENDS this room resolves before what merely counts it. A maw that
       dies on the frame a wave clears must be the thing that happened, and this
       order is what says so.
       Also before, because ::step_boss is what raises and clears
       ::EnemyPool::spawn_slow, and ::step_wave arms spawners through it.
       웨이브보다 *앞*이며, 웨이브가 출구보다 앞인 것과 같은 이유입니다. 이 방을 *끝내는* 것이
       그것을 세기만 하는 것보다 먼저 결판납니다. 웨이브가 정리되는 프레임에 죽는 아귀는 일어난
       일이 그것이어야 하고, 이 순서가 그렇게 말합니다.
       또한 ::step_boss가 ::EnemyPool::spawn_slow를 세우고 지우는 곳이며, ::step_wave가 그것을
       통해 스포너를 장전하기 때문에도 앞입니다. */
    if (!frozen) step_boss(w, dt);

    if (!frozen) step_wave(w, dt);

    if (!frozen) step_exit(w);

    /* The banner's clock. OUTSIDE `!frozen` deliberately: the boss's last line
       is raised on the frame the maw dies, and in story mode that is the frame
       `won` goes up and freezes everything -- a line gated on `!frozen` would
       be posted and then held on screen for the rest of the process. It expires
       the way ::door_notice_left does, and the drawing fades it over the tail.
       배너의 시계입니다. `!frozen` *바깥*이며 의도적입니다. 보스의 마지막 대사는 아귀가 죽는
       프레임에 올라가는데, 스토리 모드에서 그 프레임은 `won`이 서서 모든 것을 정지시키는
       프레임입니다. `!frozen`으로 막힌 대사는 게시된 뒤 프로세스가 끝날 때까지 화면에 남습니다.
       ::door_notice_left가 그러하듯 만료되며, 그리기가 꼬리에서 흐리게 만듭니다. */
    if (w->run.boss_line_t > 0.0f) {
        w->run.boss_line_t -= dt;
        if (w->run.boss_line_t <= 0.0f) {
            int was = w->run.boss_line;
            w->run.boss_line_t = 0.0f;
            w->run.boss_line   = BOSS_LINE_NONE;

            /* THE VICTORY CUTSCENE, in the gap ::step_boss opened. That gap was
               made so the maw's last line could be read before the win screen
               covered it, and its note says outright that it is "where a
               victory cutscene slots in when there is one". There is one now,
               so this is where it starts -- and ::cut_end raises `won` when it
               finishes, which is what the line below used to do here.
               A moment with no pages authored raises `won` on this very frame,
               because ::cut_begin refuses and ::step_cut has nothing to hold:
               deleting the `victory` block from story.txt gives back exactly
               the behaviour that was here before, with no branch saying so.
               승리 컷신이며, ::step_boss가 열어 둔 틈입니다. 그 틈은 승리 화면이 덮기 전에
               아귀의 마지막 대사를 읽을 수 있게 하려고 만들어졌고, 그곳의 주석은 그것이 "승리
               컷신이 생기는 날 그것이 끼어들 자리"라고 명시하고 있습니다. 이제 하나가 생겼으므로
               이곳이 그 시작점입니다. 그리고 그것이 끝날 때 `won`을 세우는 것은 ::cut_end이며,
               그것이 예전에 이 자리의 한 줄이 하던 일입니다.
               제작된 페이지가 없는 순간은 바로 이 프레임에 `won`을 세웁니다. ::cut_begin이
               거절하고 ::step_cut이 붙들 것이 없기 때문입니다. story.txt에서 `victory` 블록을
               지우면 이전에 이곳에 있던 동작을 그대로 돌려받으며, 그렇게 말하는 분기는 어디에도
               없습니다. */
            if (was == BOSS_LINE_DIE && !w->run.dead) {
                cut_begin(w, STORY_VICTORY);
                if (!w->run.cut) w->run.won = 1;
            }
        }
    }

    /* OUTSIDE the frozen test for the intermission's own reason, and it is a
       stronger case here: a cutscene freezes the world, so a page clock gated
       on `!frozen` is a screen that stops everything and then waits for
       everything to advance it. That is a hang with text on it, and the only
       way out would be the key that skips a page -- which would make the
       timeout look like it worked for anybody who never sat still.
       AFTER ::step_confirm and everything the frame does, so a page that is
       skipped by a press this frame does not also age by this frame's dt.
       인터미션 자신의 이유로 `frozen` 검사 *바깥*이며, 이곳에서는 더 강한 경우입니다. 컷신은
       월드를 정지시키므로 `!frozen`으로 막힌 페이지 시계는 모든 것을 멈춰 놓고 모든 것이 자신을
       진행시켜 주기를 기다리는 화면입니다. 그것은 텍스트가 얹힌 멈춤이고, 빠져나갈 유일한 길은
       페이지를 넘기는 키인데, 그러면 가만히 있어 본 적 없는 사람에게는 타임아웃이 동작하는
       것처럼 보입니다.
       ::step_confirm과 이번 프레임이 하는 모든 것 *뒤*이므로, 이번 프레임의 누름으로 넘어간
       페이지가 이번 프레임의 dt만큼 나이를 먹지도 않습니다. */
    step_cut(w, dt);

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

    /* --- what the end screens will have to report ------------------------
       THE SCORE OF A RUN, kept here because ::world_step is the only thing
       that may decide a run has ended and therefore the only thing that knows
       when to stop counting.

       The kills are DRAINED rather than counted, and drained here rather than
       beside ::step_drops, because monsters die in three places across a
       frame: the player's own shot in ::step_look_move, a monster's own death
       inside ::step_damage, and a grenade in ::proj_update -- which runs after
       both. One drain here, last, collects all three. Draining beside the
       drops instead would post a grenade's kills a frame late, which is
       harmless until the frame it is late by is the last frame of the run.

       Unconditional, not gated on `!frozen`. Nothing can raise the tally while
       the world is frozen, so the gate would buy nothing and cost the one
       thing it is meant to protect: the frame the player dies is frozen from
       the next frame onward, and a kill landed on it is still a kill.

       한국어
       ------
       한 플레이의 성적이며, ::world_step만이 플레이가 끝났다고 결정할 수 있고 따라서 언제
       세기를 멈출지 아는 것도 그것뿐이므로 이곳에 둡니다.

       처치는 세는 것이 아니라 *비워 가져오며*, ::step_drops 곁이 아니라 이곳입니다. 한
       프레임 안에서 몬스터는 세 곳에서 죽기 때문입니다. ::step_look_move의 플레이어 사격,
       ::step_damage 안의 죽음, 그리고 그 둘보다 뒤에 도는 ::proj_update의 유탄입니다. 맨
       마지막인 이곳의 배수 한 번이 셋을 모두 거두어들입니다. 드롭 곁에서 비우면 유탄의 처치가
       한 프레임 늦게 기록되며, 그 늦은 한 프레임이 플레이의 마지막 프레임이 되기 전까지는
       무해합니다.

       `!frozen`으로 막지 않고 조건 없이 수행합니다. 월드가 정지한 동안에는 집계를 올릴 수
       있는 것이 없으므로 게이트는 얻는 것이 없고, 정작 지켜야 할 하나를 잃습니다. 플레이어가
       죽는 프레임은 그다음 프레임부터 정지되며, 그 프레임에 성립한 처치도 처치입니다. */
    w->run.kills += enemy_take_kills(&w->pools);

    /* SEPARATE FROM world_time above, which wraps. This one must not: a player
       who outlasted the wrap would be told they lasted four seconds. See
       ::RunState::alive_time.
       위의 world_time과 별개이며, 그것은 순환합니다. 이것은 순환해서는 안 됩니다. 순환 주기보다
       오래 버틴 플레이어에게 4초 버텼다고 말하게 됩니다. ::RunState::alive_time을 참조하십시오. */
    if (!frozen) w->run.alive_time += dt;

    return frozen;
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
