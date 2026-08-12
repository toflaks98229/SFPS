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
#include "player.h"       /* PLAYER_EYE, to turn an eye position into feet */
#include "audio.h"
#include "fx.h"
#include "diag.h"
#include <math.h>

/* --- Static variable definitions / 정적 변수 정의 --- */

/** @brief All pickups for the current level, active and collected alike. / 현재 레벨의 모든 아이템. 활성 상태와 획득된 것을 모두 포함합니다. */
static Pickup g_pickups[PICKUP_MAX];
/** @brief How many entries of ::g_pickups the current level filled. / 현재 레벨이 채운 ::g_pickups 항목의 개수. */
static int    g_count;

/* --- Static function prototypes / 정적 함수 프로토타입 --- */

static int pickup_kind_for(const char *k);

/* --- Public function definitions / 공개 함수 정의 --- */

void pickup_reset(void) {
    /* Clearing `active` is enough; the remaining fields are overwritten
       wholesale on the next spawn.
       `active`만 해제하면 충분합니다. 나머지 필드는 다음 생성 시 통째로
       덮어쓰기 때문입니다. */
    for (int i = 0; i < PICKUP_MAX; i++) g_pickups[i].active = 0;
    g_count = 0;
}

void pickup_spawn_level(const Level *l) {
    pickup_reset();
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
        if (!level_ground(l, x, z, 1000.0f, 0.0f, &f, &c)) continue;

        if (g_count >= PICKUP_MAX) { DIAG(DIAG_PICKUP_CAP); continue; }

        Pickup *p = &g_pickups[g_count++];
        p->kind   = kind;
        p->pos    = v3f(x, f, z);
        p->anim   = (float)(g_count) * 1.3f;   /* desync the bobbing */
        p->active = 1;
    }
}

int pickup_count(void) { return g_count; }

const Pickup *pickup_at(int i) {
    return (i >= 0 && i < g_count) ? &g_pickups[i] : 0;
}

void pickup_update(v3 player_eye, int *health, int health_max,
                   Weapon *w, int *keys, float dt) {
    /* The player's feet, so a pickup at floor level is compared like with
       like rather than against the eye 1.7 m up. */
    float feet_y = player_eye.y - PLAYER_EYE;

    for (int i = 0; i < g_count; i++) {
        Pickup *p = &g_pickups[i];
        if (!p->active) continue;
        /* The animation clock advances before the range test, so a pickup
           the player never reaches still bobs.
           애니메이션 시계는 거리 판정보다 먼저 진행되므로, 플레이어가 닿지 않는
           아이템도 계속 움직입니다. */
        p->anim += dt;

        /* Squared distance, avoiding a square root in the common
           "not close enough" case.
           제곱 거리를 사용하여, 대부분을 차지하는 "충분히 가깝지 않음" 경우에
           제곱근 연산을 피합니다. */
        float dx = player_eye.x - p->pos.x, dz = player_eye.z - p->pos.z;
        if (dx*dx + dz*dz > PICKUP_RADIUS * PICKUP_RADIUS) continue;
        if (fabsf(feet_y - p->pos.y) > 1.6f) continue;     /* different floor */

        /* Only take it if it helps -- otherwise leave it to come back for. */
        if (p->kind == PK_HEALTH) {
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
            if (!had) w->cur = gw;
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
        fx_spawn("pickup", v3f(p->pos.x, p->pos.y + 0.4f, p->pos.z), v3f(0, 1, 0));
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
 *   ammo                the shotgun's box, under the name every level already
 *                       uses -- renaming it would empty the authored maps
 *   <weapon>ammo        that weapon's box, e.g. "rapidammo"
 *   <weapon>            the weapon itself, e.g. "axe"
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

    for (int w = 0; w < WP_TYPES; w++) {
        const char *n = wp_stats(w)->name;

        /* "<name>ammo", compared without building a string. */
        int i = 0;
        while (i < len && n[i] && k[i] == n[i]) i++;
        if (!n[i] && name_eq_n(k + i, len - i, "ammo")) return PK_AMMO_FOR(w);
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
