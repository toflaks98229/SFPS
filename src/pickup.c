/**
 * @file pickup.c
 * @brief Implements pickup spawning and collection. No GL -- see pickup.h.
 *
 * ENGLISH
 * -------
 * Pickups live in one fixed-size module-owned array rather than being
 * allocated: the cap is small, the lifetime is exactly one level, and a flat
 * array keeps both the spawn pass and the per-frame sweep trivial.
 *
 * 한국어
 * ------
 * 아이템은 동적으로 할당되지 않고 모듈이 소유한 고정 크기 배열에 저장됩니다.
 * 상한이 작고 수명이 정확히 한 레벨에 국한되므로, 평면 배열을 사용하면 생성
 * 과정과 매 프레임 순회가 모두 단순해지기 때문입니다.
 */

#include "pickup.h"
#include "pools.h"
#include "player.h"       /* PLAYER_EYE, to turn an eye position into feet */
#include "audio.h"
#include "fx.h"
/* loot_mote -- how long a fresh item announces itself, and how often any item
   gives off a speck. Read here rather than passed in, because the moment an
   item arrives is the moment it is decided and pickup.c is where that moment
   is -- and because the emitter is here too.
   loot_mote입니다. 갓 도착한 아이템이 얼마나 오래 자신을 알리는지, 그리고 아무 아이템이나
   얼마나 자주 알갱이를 내보내는지입니다. 인자로 받지 않고 이곳에서 읽는 이유는, 그것이
   정해지는 순간이 아이템이 도착하는 순간이고 그 순간이 있는 곳이 pickup.c이기 때문이며,
   내보내는 쪽도 이곳이기 때문입니다. */
#include "loot.h"
#include "diag.h"
#include <math.h>

/* --- Static variable definitions / 정적 변수 정의 --- */

/** @brief All pickups for the current level, active and collected alike. / 현재 레벨의 모든 아이템. 활성 상태와 획득된 것을 모두 포함합니다. */
/** @brief How many entries of ::pl->pickup.p the current level filled. / 현재 레벨이 채운 ::pl->pickup.p 항목의 개수. */

/* --- Static function prototypes / 정적 함수 프로토타입 --- */

static int pickup_kind_for(const char *k);
/* Declared rather than moved: ::pickup_spawn_level and ::pickup_toss both seed
   a mote clock, and both sit above the emitter that owns the idea. See
   ::Pickup::mote.
   옮기지 않고 선언합니다. ::pickup_spawn_level과 ::pickup_toss 둘 다 티끌 시계를 심는데,
   둘 다 그 개념을 소유한 방출부보다 위에 있습니다. ::Pickup::mote를 참조하십시오. */
static float mote_stagger(int slot);

/* --- Public function definitions / 공개 함수 정의 --- */

void pickup_reset(Pools *pl) {
    /* Clearing `active` is enough; the remaining fields are overwritten
       wholesale on the next spawn.
       `active`만 해제하면 충분합니다. 나머지 필드는 다음 생성 시 통째로
       덮어쓰기 때문입니다. */
    for (int i = 0; i < PICKUP_MAX; i++) pl->pickup.p[i].active = 0;
    pl->pickup.count = 0;
}

void pickup_spawn_level(Pools *pl, const Level *l) {
    pickup_reset(pl);
    /* Iterates every entity even once full -- see the matching note in
       enemy_spawn_level for why the cap is checked here rather than in the
       loop condition.
       가득 찬 뒤에도 모든 엔티티를 순회합니다. 한계를 루프 조건이 아닌 이 위치에서
       검사하는 이유는 enemy_spawn_level의 동일한 주석을 참조하십시오. */
    for (int i = 0; i < l->n_ents; i++) {
        const Entity *e = &l->ents[i];
        int kind = pickup_kind_for(e->kind);
        if (kind < 0) continue;

        /* Entity coordinates are centimetres; the level is metres.
           엔티티 좌표는 센티미터 단위이고 레벨은 미터 단위입니다. */
        float x = e->x * 0.01f, z = e->z * 0.01f;
        float f, c;
        /* Search from high above with no step limit, so the pickup settles
           onto whatever floor is beneath the marker. No floor at all means
           the entity is outside the map and is skipped rather than left
           hanging in the air.
           단차 제한 없이 높은 곳에서부터 탐색하여, 표식 아래에 있는 바닥 위에
           아이템이 안착하도록 합니다. 바닥이 전혀 없다면 해당 엔티티는 맵
           바깥에 있는 것이므로, 공중에 남겨 두지 않고 건너뜁니다. */
        /* From the marker's own height rather than from a kilometre up. See
           the matching note in enemy.c: the absurd value meant "no limit" and
           a brush level answers it with the outside of its roof.
           1킬로미터 위가 아니라 표식 자신의 높이에서 찾습니다. enemy.c의 같은 설명을
           참조하십시오. 터무니없는 값이 "제한 없음"을 뜻했고, 브러시 레벨은 그것에 지붕의
           바깥면으로 답합니다. */
        if (!level_ground(l, x, z, e->y * 0.01f, 1e9f, &f, &c)) continue;

        /* A box for a weapon that takes no ammo (max_ammo 0) is not placed: nothing could ever
           pick it up, so it would sit on the floor for the whole level.
           탄약을 쓰지 않는 무기(max_ammo 0)의 상자는 놓지 않습니다. 아무도 주울 수 없으니 레벨
           내내 바닥에 남게 됩니다. */
        if (PK_AMMO_WEAPON(kind) >= 0 && wp_stats(PK_AMMO_WEAPON(kind))->max_ammo <= 0) continue;

        if (pl->pickup.count >= PICKUP_MAX) { DIAG(DIAG_PICKUP_CAP); continue; }

        Pickup *p = &pl->pickup.p[pl->pickup.count++];
        p->kind   = kind;
        p->pos    = v3f(x, f, z);
        p->anim   = (float)(pl->pickup.count) * 1.3f;   /* desync the bobbing */
        p->mote   = mote_stagger(pl->pickup.count - 1);
        p->active = 1;
    }
}

int pickup_count(const Pools *pl) { return pl->pickup.count; }

const Pickup *pickup_at(const Pools *pl, int i) {
    return (i >= 0 && i < pl->pickup.count) ? &pl->pickup.p[i] : 0;
}

int pickup_toss(Pools *pl, int kind, v3 from, v3 vel) {
    Pickup *p = 0;

    /* A collected item's hole, before growing the array. See the note on this
       function for why an arena needs this and a level never did.
       배열을 늘리기 전에 획득된 아이템의 구멍을 씁니다. 아레나가 이것을 필요로 하고 레벨은
       필요로 한 적 없는 이유는 이 함수의 설명을 참조하십시오. */
    for (int i = 0; i < pl->pickup.count; i++)
        if (!pl->pickup.p[i].active) { p = &pl->pickup.p[i]; break; }

    if (!p) {
        if (pl->pickup.count >= PICKUP_MAX) { DIAG(DIAG_PICKUP_CAP); return 0; }
        p = &pl->pickup.p[pl->pickup.count++];
    }

    Pickup zero = {0};
    *p = zero;
    p->kind   = kind;
    p->pos    = from;
    p->vel    = vel;
    /* Desynced by slot, the same way ::pickup_spawn_level does it and for one
       more reason than it had: the bob was the only thing this clock drove
       until the halo started breathing on it, and a ring of a dozen items
       thrown on the same frame from anim 0 would swell and settle in unison --
       which reads as one object with twelve parts rather than twelve items.
       ::pickup_spawn_level과 같은 방식으로 슬롯에 따라 어긋나게 하며, 그때보다 이유가 하나
       더 있습니다. 헤일로가 이 시계로 숨 쉬기 전까지 이것이 움직이던 것은 위아래 움직임뿐
       이었습니다. 같은 프레임에 anim 0에서 던져진 열몇 개의 고리는 한목소리로 부풀었다
       가라앉는데, 그것은 아이템 열두 개가 아니라 부분이 열둘인 물체 하나로 읽힙니다. */
    p->anim   = (float)(p - pl->pickup.p) * 1.3f;
    /* Every tossed item announces itself, whether it came off a corpse or out
       of a cleared wave. Not conditional on which, because the player has no
       way to tell the two apart and no reason to want to: both are "something
       arrived that was not there".
       던져진 모든 아이템이 자신을 알립니다. 시체에서 나왔든 정리된 웨이브에서 나왔든
       마찬가지입니다. 어느 쪽인지에 따라 나누지 않는 이유는, 플레이어에게 둘을 구분할
       방법도 그럴 이유도 없기 때문입니다. 둘 다 "없던 것이 도착했다"입니다. */
    p->flare  = loot_mote()->hold;
    p->mote   = mote_stagger((int)(p - pl->pickup.p));
    p->active = 1;
    return 1;
}

/* Where an item's mote clock starts, so a room's worth of them do not tick
   together. See ::Pickup::mote.

   Derived from the SLOT rather than from a generator: this runs in the same
   world a recorded demo replays, and a spawn point that consulted a random
   number would put every particle in the level one step out of phase with the
   recording for no benefit at all. Prime-ish steps of 0.11s spread sixty-four
   items across seven seconds and repeat no offset within a pool.
   아이템의 티끌 시계가 어디서 출발하는지입니다. 방 하나 분량이 함께 똑딱이지 않게 합니다.
   ::Pickup::mote를 참조하십시오.

   생성기가 아니라 *슬롯*에서 유도합니다. 이것은 기록된 데모가 재생하는 그 월드에서
   돌아가며, 난수를 참조하는 시작점은 아무 이득도 없이 레벨의 모든 입자를 기록과 한 단계
   어긋나게 만듭니다. 0.11초 단위는 아이템 64개를 7초에 걸쳐 퍼뜨리고 한 풀 안에서 같은
   오프셋을 반복하지 않습니다. */
static float mote_stagger(int slot) {
    return (float)(slot & 63) * 0.11f;
}

/* One item's turn at giving off a speck.
 *
 * ENGLISH: Paced rather than spawned once, the same arrangement world.c has
 * with the lava smoke and for the same reason: ::fx_spawn only knows how to be
 * a burst, and what a floor item needs is a trickle that is still going when
 * the player finally looks its way.
 * The timer runs whether or not anything is spawned -- see ::Pickup::mote for
 * why a paused one is worse than a wasted tick.
 *
 * 한국어: 한 번 생성하지 않고 조절해 뿌립니다. world.c가 용암 연기와 맺는 것과 같은 배치이며
 * 이유도 같습니다. ::fx_spawn은 폭발이 될 줄만 알지만, 바닥 아이템에게 필요한 것은 플레이어가
 * 마침내 그쪽을 볼 때까지도 이어지고 있는 흐름입니다.
 * 타이머는 무언가 생성되든 아니든 돕니다. 멈춘 타이머가 낭비된 틱보다 나쁜 이유는
 * ::Pickup::mote를 참조하십시오. */
static void motes(Pools *pl, Pickup *p, v3 player_eye, float dt) {
    const LootMote *m = loot_mote();

    p->mote -= dt;
    if (p->mote > 0.0f) return;

    /* Reset off the flare, so an item that has just arrived is emitting four
       times as fast as one that has been lying there -- which is the whole of
       how "something appeared" is told apart from "something is here".
       섬광을 기준으로 되돌립니다. 갓 도착한 아이템은 오래 놓여 있던 것보다 네 배 빠르게
       내보내며, 그것이 "무언가 나타났다"와 "무언가 여기 있다"를 구분 짓는 전부입니다. */
    p->mote = p->flare > 0.0f ? m->hurry : m->rate;

    float dx = p->pos.x - player_eye.x, dz = p->pos.z - player_eye.z;
    if (dx * dx + dz * dz > m->range * m->range) return;

    /* From the LOWER PART of the billboard, not from the floor and not from its
       middle. Specks that start at floor level spend the first third of their
       life climbing to the item, and specks that start at its centre appear
       out of nothing halfway up it -- from down here they rise THROUGH the
       drawing, which is what makes them read as coming off it.
       A literal, because the billboard's own lift lives in scene.c and a
       drawing constant is not something the simulation should be reaching for:
       what this number has to be right about is "inside the sprite, low", and
       that stays true whatever the drawer does with the rest.
       바닥도 한가운데도 아닌 빌보드의 *아래쪽*에서 나옵니다. 바닥 높이에서 출발한 알갱이는
       수명의 첫 3분의 1을 아이템까지 오르는 데 쓰고, 한가운데에서 출발한 알갱이는 아이템의
       중간에서 난데없이 나타납니다. 이 높이에서는 그림을 *뚫고* 올라가며, 그것이 알갱이가
       아이템에서 나오는 것으로 읽히게 만듭니다.
       리터럴인 이유는 빌보드의 들림 높이가 scene.c에 살고 있고, 시뮬레이션이 그리기 상수에
       손을 뻗을 일은 아니기 때문입니다. 이 숫자가 맞혀야 하는 것은 "스프라이트 안쪽,
       아래쪽"이며, 그리는 쪽이 나머지를 어떻게 하든 그것은 참으로 남습니다. */
    fx_spawn(pl, "itemmote", v3f(p->pos.x, p->pos.y + 0.55f, p->pos.z),
             v3f(0, 1, 0));
}

/* The frame a tossed item settles. One burst, at the item rather than under
   it, and NOT the same effect a collection plays -- an arrival and a departure
   that look alike is a player who cannot tell whether they just gained
   something or lost it.
   던져진 아이템이 안착하는 프레임입니다. 아래가 아니라 아이템 자리에 한 번, 그리고 획득이
   재생하는 것과는 *다른* 이펙트입니다. 도착과 떠남이 똑같이 보인다는 것은, 플레이어가
   방금 무언가를 얻었는지 잃었는지 구분할 수 없다는 뜻입니다. */
static void land(Pools *pl, const Pickup *p) {
    fx_spawn(pl, "itemland", v3f(p->pos.x, p->pos.y + 0.35f, p->pos.z),
             v3f(0, 1, 0));
}

/* One item's flight. Returns non-zero while it is still in the air, which is
   also the answer to "may it be collected yet".
   아이템 하나의 비행입니다. 아직 공중에 있으면 0이 아닌 값을 반환하며, 그것은 "아직 획득할 수
   있는가"에 대한 답이기도 합니다. */
static int fly(Pools *pl, Pickup *p, const Level *l, float dt) {
    if (p->vel.x == 0.0f && p->vel.y == 0.0f && p->vel.z == 0.0f) return 0;

    p->vel.y -= PICKUP_GRAVITY * dt;
    p->pos = v3add(p->pos, v3scale(p->vel, dt));

    /* Only ever caught on the way DOWN. Tested against the rising half too and
       an item thrown upward from the floor lands on the frame it left, because
       it is still at floor height and already "below" it.
       내려오는 중에만 잡습니다. 올라가는 절반에서도 검사하면, 바닥에서 위로 던진 아이템이
       떠난 프레임에 착지합니다. 아직 바닥 높이에 있고 이미 그 "아래"이기 때문입다. */
    if (p->vel.y > 0.0f) return 1;

    float f, c;
    if (!level_ground(l, p->pos.x, p->pos.z, p->pos.y + 1.0f, 1e9f, &f, &c)) {
        /* Nowhere to land: no floor was found under where it got to. Stopped
           where it is rather than falling for ever, so a reward thrown at a
           hole is reachable instead of gone.
           내려앉을 곳이 없습니다. 도달한 자리 아래에서 바닥을 찾지 못했습니다. 영원히
           떨어지는 대신 그 자리에 멈추므로, 구멍을 향해 던져진 보상은 사라지지 않고 닿을 수
           있는 곳에 남습니다. */
        p->vel = v3f(0, 0, 0);
        land(pl, p);
        return 0;
    }

    if (p->pos.y <= f) {
        p->pos.y = f;
        p->vel   = v3f(0, 0, 0);
        land(pl, p);
        return 0;
    }
    return 1;
}

void pickup_update(Pools *pl, const Level *l, v3 player_eye,
                   int *health, int health_max,
                   Weapon *w, int *keys, float *power, float dt, int *took) {
    /* The player's feet, so a pickup at floor level is compared like with
       like rather than against the eye 1.7 m up. */
    float feet_y = player_eye.y - PLAYER_EYE;

    for (int i = 0; i < pl->pickup.count; i++) {
        Pickup *p = &pl->pickup.p[i];
        if (!p->active) continue;
        /* The animation clock advances before the range test, so a pickup
           the player never reaches still bobs.
           애니메이션 시계는 거리 판정보다 먼저 진행되므로, 플레이어가 닿지 않는
           아이템도 계속 움직입니다. */
        p->anim += dt;

        /* BEFORE the flight test, so an item still in the air leaves a trail of
           specks behind it. A reward thrown across a room and a reward that
           appeared on the floor are the same item by the time it lands; the
           trail is what makes the throw itself worth watching.
           비행 판정보다 *앞*이므로, 아직 공중에 있는 아이템도 뒤에 알갱이 자국을 남깁니다.
           방을 가로질러 던져진 보상과 바닥에 나타난 보상은 착지할 무렵이면 같은
           아이템입니다. 던지는 것 자체를 볼 만하게 만드는 것이 그 자국입니다. */
        motes(pl, p, player_eye, dt);

        /* IN THE AIR IS NOT COLLECTABLE, and that is the point rather than a
           limitation: the arc is what makes the reward noticed, and an item
           collected on the frame it was thrown never drew one. It also stops a
           reward tossed from the player's own feet being absorbed instantly by
           the player standing there.
           공중에 있는 것은 획득할 수 없으며, 그것은 제약이 아니라 요점입니다. 포물선이 보상을
           알아채이게 만드는 것인데, 던져진 프레임에 획득된 아이템은 포물선을 그린 적이
           없습니다. 또한 플레이어 자신의 발치에서 던져진 보상이 그 자리에 서 있는 플레이어에게
           즉시 흡수되는 것을 막습니다. */
        if (fly(pl, p, l, dt)) continue;

        /* Counted down only now, past the flight test, which is what holds the
           flare at full while the item is still in the air. See ::Pickup::flare.
           비행 판정을 지난 지금에야 감소시킵니다. 그것이 아이템이 아직 공중에 있는 동안
           섬광을 최대로 붙들어 두는 방법입니다. ::Pickup::flare를 참조하십시오. */
        if (p->flare > 0.0f) {
            p->flare -= dt;
            if (p->flare < 0.0f) p->flare = 0.0f;
        }

        /* Squared distance, avoiding a square root in the common
           "not close enough" case.
           제곱 거리를 사용하여, 대부분을 차지하는 "충분히 가깝지 않음" 경우에
           제곱근 연산을 피합니다. */
        float dx = player_eye.x - p->pos.x, dz = player_eye.z - p->pos.z;
        if (dx*dx + dz*dz > PICKUP_RADIUS * PICKUP_RADIUS) continue;
        if (fabsf(feet_y - p->pos.y) > 1.6f) continue;     /* different floor */

        /* Only take it if it helps -- otherwise leave it to come back for. */
        if (power && p->kind >= PK_POWER0 && p->kind <= PK_POWER_LAST) {
            /* SET, NOT ADDED. Picking a second quad up while the first is
               still running restarts the clock; it does not bank sixty
               seconds. An author who placed two of an artifact laid out a
               route that can be kept topped up, not a pair that stacks --
               and stacking is how a room with three of them becomes a room
               with a minute and a half of one.
               Taken unconditionally, unlike a medkit. A quad at full health
               is still worth having, so there is no "only if it helps" test
               to write -- and one that refused a pickup already running
               would strand the player next to an artifact they could see and
               not collect.
               *더하지 않고 설정합니다.* 첫 번째가 도는 동안 두 번째 쿼드를 주우면
               시계가 다시 시작되지 60초가 적립되지 않습니다. 아티팩트를 둘 놓은
               제작자는 계속 채워 갈 수 있는 경로를 깐 것이지 쌓이는 한 쌍을 놓은
               것이 아니며, 쌓기는 셋 있는 방을 1분 30초짜리 하나가 있는 방으로
               만드는 방식입니다.
               구급상자와 달리 조건 없이 가져갑니다. 체력이 가득해도 쿼드는 여전히
               가질 값어치가 있으므로 "도움이 될 때만" 검사를 쓸 것이 없고, 이미
               도는 것을 거절하는 검사는 플레이어를 보이지만 주울 수 없는 아티팩트
               곁에 붙들어 둡니다. */
            power[p->kind - PK_POWER0] = PLAYER_POWER_TIME;
            /* Told to the caller so the HUD can name it. `+ 1` because 0 is
               "nothing was picked up" and ::PW_QUAD is 0.
               호출자에게 알려 HUD가 이름을 부를 수 있게 합니다. 0이 "주운 것 없음"이고
               ::PW_QUAD가 0이므로 `+ 1`입니다. */
            if (took) *took = p->kind - PK_POWER0 + 1;
            audio_play("part", 88);
        } else if (p->kind == PK_HEALTH) {
            if (*health >= health_max) continue;
            *health += PICKUP_HEALTH;
            /* Clamp after adding: a partial top-up still consumes the whole
               medkit rather than banking the remainder.
               더한 뒤에 상한을 적용합니다. 일부만 회복되더라도 남은 양을
               저장해 두지 않고 구급상자 전체를 소비합니다. */
            if (*health > health_max) *health = health_max;
            audio_play("pmed", 80);
        } else if (PK_KEY_MASK(p->kind) != KEY_NONE) {
            /* Taken even when already held: a second red key is not useful,
               but leaving one lying there reads as a key you have not found
               yet, and a player who backtracks for it has been lied to.
               이미 가지고 있어도 획득합니다. 두 번째 붉은 열쇠가 유용하지는 않지만, 그대로
               놓아두면 아직 찾지 못한 열쇠처럼 보이고, 그것을 위해 되돌아온 플레이어는
               속은 것입니다. */
            *keys |= PK_KEY_MASK(p->kind);
            audio_play("key", 95);
        } else if (PK_WEAPON_WEAPON(p->kind) >= 0) {
            /* --- a weapon lying on the floor ------------------------------
               Taken even when already owned, because it carries ammunition and
               a player who walks over a second axe expects to be given
               something. Owning it already just means the belt is what gets
               topped up.

               Switching to it on the FIRST pickup only. Finding a weapon
               should put it in your hands -- that is the moment it exists --
               but a later one yanking the shotgun away mid-fight would be the
               game overriding a choice the player already made.

               이미 보유 중이어도 획득합니다. 탄약을 가지고 있으며, 두 번째 도끼를 밟은
               플레이어는 무언가 얻기를 기대하기 때문입니다. 이미 보유 중이라면 채워지는
               것이 탄약일 뿐입니다.

               전환은 *처음* 획득할 때만 합니다. 무기를 발견하면 손에 쥐여야 합니다.
               그 순간이 그 무기가 존재하게 되는 순간입니다. 그러나 나중에 주운 것이
               교전 도중 샷건을 빼앗는다면, 그것은 플레이어가 이미 내린 선택을 게임이
               뒤엎는 일입니다. */
            int gw = PK_WEAPON_WEAPON(p->kind);
            const WeaponType *S = wp_stats(gw);
            int had = w->owned[gw];

            if (had && w->ammo[gw] >= S->max_ammo) continue;

            w->owned[gw] = 1;
            w->ammo[gw] += S->start_ammo;
            if (w->ammo[gw] > S->max_ammo) w->ammo[gw] = S->max_ammo;
            if (!had) wp_swap_to(w, gw);
            audio_play("pammo", 90);
        } else {
            /* An ammo box, for whichever belt its kind names. */
            int aw = PK_AMMO_WEAPON(p->kind);
            if (aw < 0) continue;
            const WeaponType *S = wp_stats(aw);

            /* A box for a weapon you do not have is left alone rather than
               banked. Walking over it later, once the weapon is found, is what
               makes finding the weapon feel like it opened something up.
               보유하지 않은 무기의 상자는 쌓아 두지 않고 그대로 둡니다. 무기를 찾은 뒤
               다시 지나가며 줍는 것이, 무기를 찾은 일이 무언가를 열어 주었다는 느낌을
               만듭니다. */
            if (!w->owned[aw]) continue;
            if (w->ammo[aw] >= S->max_ammo) continue;

            w->ammo[aw] += S->pickup_ammo;
            if (w->ammo[aw] > S->max_ammo) w->ammo[aw] = S->max_ammo;
            audio_play("pammo", 80);
        }
        /* Deactivated rather than removed, so indices stay stable for the
           renderer iterating the same array.
           제거하지 않고 비활성화합니다. 같은 배열을 순회하는 렌더러를 위해
           인덱스를 안정적으로 유지하기 위함입니다. */
        p->active = 0;

        /* Thrown upward: the item goes INTO the player, which is a different
           motion from something breaking apart where it stood.
           위쪽으로 던집니다. 아이템이 플레이어에게 *들어가는* 것이며, 이는 제자리에서
           부서지는 것과는 다른 움직임입니다. */
        fx_spawn(pl, "pickup", v3f(p->pos.x, p->pos.y + 0.4f, p->pos.z), v3f(0, 1, 0));
    }
}

/* --- Static helper function definitions / 정적 헬퍼 함수 정의 --- */

/**
 * @brief Turns an entity kind string into a pickup kind.
 *
 * ENGLISH
 * -------
 * @brief Turns an entity kind string into a pickup kind.
 * @param[in] k Null-terminated entity kind name from the level data.
 * @return ::PK_AMMO, ::PK_HEALTH, or -1 when the entity is not a pickup.
 * @note Compared character by character rather than with `strcmp` to avoid
 *       pulling the C string functions into a size-bound build. The explicit
 *       terminator check on each branch is what keeps "ammobox" from matching
 *       "ammo".
 *
 * 한국어
 * ------
 * @brief 엔티티 종류 문자열을 아이템 종류로 변환합니다.
 * @param[in] k 레벨 데이터에서 가져온 널로 끝나는 엔티티 종류 이름.
 * @return ::PK_AMMO, ::PK_HEALTH 중 하나. 아이템이 아니면 -1.
 * @note 크기가 제한된 빌드에 C 문자열 함수를 포함시키지 않기 위해 `strcmp` 대신
 *       문자 단위로 비교합니다. 각 분기의 명시적인 종료 문자 검사가 "ammobox"가
 *       "ammo"와 일치하지 않도록 막아 줍니다.
 */
/* Whether two names match. Avoids <string.h>, and is the same loop enemy.c and
   fx.c use on their own name lookups.
   두 이름이 일치하는지 여부입니다. <string.h>를 끌어오지 않으며, enemy.c와 fx.c가 자체
   이름 조회에 쓰는 것과 같은 루프입니다. */
/* Compares a name against a slice that is NOT NUL-terminated. The sprite
   decoder holds names as pointer+length inside one big text blob, and copying
   each into a buffer to compare it would be a second place that decides how
   long a pickup name may be. The NUL-terminated version this replaced became
   dead once every caller went through here.
   NUL로 끝나지 않는 조각과 이름을 비교합니다. 스프라이트 디코더는 하나의 큰 텍스트
   덩어리 안에서 이름을 포인터와 길이로 들고 있으며, 비교를 위해 각각을 버퍼로 복사하면
   아이템 이름의 최대 길이를 정하는 두 번째 장소가 생깁니다. */
static int name_eq_n(const char *a, int n, const char *b) {
    int i = 0;
    while (i < n && b[i] && a[i] == b[i]) i++;
    return i == n && !b[i];
}

/**
 * @brief The pickup kind a level entity's name asks for, or -1.
 *
 * ENGLISH
 * -------
 * Three families, checked in the order a name could be ambiguous in:
 *
 *   health              the medkit
 *   mana                the shotgun's mana; `ammo` is the same kind under the
 *                       name the authored maps carry
 *   <weapon>mana        that weapon's mana, e.g. "rapidmana"; `<weapon>ammo`
 *                       is accepted as the older spelling
 *   <weapon>            the weapon's magic circle, e.g. "axe"
 *
 * The weapon families are derived from ::WEAPONS by walking it, so a weapon
 * added to that table can be placed in a level immediately -- there is no
 * second list of entity names to extend, and therefore no way for one to name
 * a weapon the other does not.
 *
 * @note "<weapon>ammo" is tested before "<weapon>" because the latter is a
 *       prefix of the former. Reversed, "rapidammo" would match the weapon
 *       "rapid" and a level would spawn a free gun where it asked for a box.
 *
 * 한국어
 * ------
 * @brief 레벨 엔티티 이름이 요구하는 아이템 종류. 없으면 -1입니다.
 *
 * 이름이 모호할 수 있는 순서대로 세 계열을 검사합니다. 무기 계열은 ::WEAPONS를 순회하여
 * 유도하므로, 그 표에 추가된 무기는 즉시 레벨에 배치할 수 있습니다. 확장해야 할 두 번째
 * 엔티티 이름 목록이 없으며, 따라서 한쪽만 아는 무기가 생길 수 없습니다.
 *
 * @note "<무기>ammo"를 "<무기>"보다 먼저 검사합니다. 후자가 전자의 접두사이기 때문입니다.
 *       순서가 반대라면 "rapidammo"가 무기 "rapid"와 일치하여, 상자를 요청한 레벨이 공짜
 *       무기를 생성하게 됩니다.
 */
int pickup_kind_for_n(const char *k, int len) {
    if (name_eq_n(k, len, "health")) return PK_HEALTH;
    if (name_eq_n(k, len, "ammo"))   return PK_AMMO;
    if (name_eq_n(k, len, "mana"))   return PK_AMMO;
    if (name_eq_n(k, len, "quad"))   return PK_POWER0 + PW_QUAD;
    if (name_eq_n(k, len, "shadow")) return PK_POWER0 + PW_SHADOW;
    if (name_eq_n(k, len, "aegis"))  return PK_POWER0 + PW_AEGIS;

    for (int w = 0; w < WP_TYPES; w++) {
        const char *n = wp_stats(w)->name;
        /* "<name>mana" and "<name>ammo", compared without building a string.
           `mana` is the current name; `ammo` is what the authored maps carry.
           "<이름>mana"와 "<이름>ammo"를 문자열을 만들지 않고 비교합니다. `mana`가 현재
           이름이고 `ammo`는 저작된 맵이 담고 있는 이름입니다. */
        int i = 0;
        while (i < len && n[i] && k[i] == n[i]) i++;
        if (!n[i] && (name_eq_n(k + i, len - i, "mana") ||
                      name_eq_n(k + i, len - i, "ammo"))) return PK_AMMO_FOR(w);
    }
    for (int w = 0; w < WP_TYPES; w++)
        if (name_eq_n(k, len, wp_stats(w)->name)) return PK_WEAPON_FOR(w);

    /* Keycards, named by colour: `redkey`, `bluekey`, `yellowkey`. The colour
       leads because that is how a player refers to them and how the door that
       wants one is written -- `key red`.
       색이 앞에 옵니다. 플레이어가 그렇게 부르고, 그것을 요구하는 문도 `key red`로
       기록되기 때문입니다. */
    if (name_eq_n(k, len, "redkey"))    return PK_KEY0 + 0;
    if (name_eq_n(k, len, "bluekey"))   return PK_KEY0 + 1;
    if (name_eq_n(k, len, "yellowkey")) return PK_KEY0 + 2;

    return -1;
}

static int pickup_kind_for(const char *k) {
    int n = 0;
    while (k[n]) n++;
    return pickup_kind_for_n(k, n);
}
