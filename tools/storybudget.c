/* storybudget -- can a story run actually be finished with what the map hands out?
 *
 * ENGLISH
 * -------
 * EVERY OTHER CHECK IN THIS FOLDER ASKS WHETHER A RULE HOLDS. This one asks
 * whether the game is winnable, which is a different question and the only one
 * a balance edit can get wrong without breaking a single rule. Health, damage,
 * spawn rates and the boss's cycles are four tables that are individually
 * correct and jointly a run nobody can finish.
 *
 * WHAT IT MEASURES, and what it deliberately does not.
 *
 *   IT MEASURES AMMUNITION AGAINST HEALTH. Every round the level hands out
 *   carries a fixed amount of damage -- ::WeaponType says how much per shot and
 *   how many rounds a box holds -- and everything a story run must destroy has
 *   a number in the bestiary. If the first total is under the second the run is
 *   unwinnable for every player who ever loads it, whatever they do, and that
 *   is a fact about two tables rather than about anyone's aim.
 *
 *   IT DOES NOT MEASURE WHETHER THE PLAYER SURVIVES. Incoming damage is a
 *   function of how well somebody dodges, and a test that asserted a health
 *   budget would be asserting a skill level. The medkits and the monsters'
 *   damage are printed for a reader and nothing here fails on them.
 *
 * A MARGIN, NOT A PASS MARK. Nobody hits every shot, so ammunition exactly
 * equal to the health in the room is a game that cannot be finished in
 * practice. ::BUDGET_MARGIN is what the check demands over the bare total.
 *
 * 한국어
 * ------
 * *이 폴더의 다른 모든 검사는 규칙이 성립하는지 묻습니다.* 이것은 게임을 이길 수 있는지
 * 묻습니다. 다른 질문이며, 밸런스 수정이 규칙을 하나도 깨뜨리지 않고 틀릴 수 있는 유일한
 * 질문입니다. 체력과 피해와 스폰 속도와 보스의 사이클은 각각 옳으면서 합쳐서는 아무도 끝낼
 * 수 없는 플레이가 될 수 있는 네 개의 표입니다.
 *
 * *무엇을 재고 무엇을 일부러 재지 않는가.*
 *   *탄약을 체력에 대고 잽니다.* 레벨이 내주는 모든 탄은 정해진 양의 피해를 나릅니다.
 *   ::WeaponType이 발당 피해와 상자 하나의 탄 수를 말합니다. 그리고 스토리 플레이가 부숴야
 *   하는 모든 것에는 도감의 수가 있습니다. 앞의 합계가 뒤의 합계보다 작으면 그 플레이는 그것을
 *   불러오는 모든 플레이어에게, 무엇을 하든 이길 수 없습니다. 그것은 누군가의 조준이 아니라
 *   두 표에 대한 사실입니다.
 *   *플레이어가 살아남는지는 재지 않습니다.* 받는 피해는 얼마나 잘 피하는가의 함수이고, 체력
 *   예산을 단언하는 검사는 실력 수준을 단언하는 것입니다. 구급상자와 몬스터의 피해는 읽는
 *   이를 위해 찍을 뿐 이곳의 무엇도 그것으로 실패하지 않습니다.
 *
 * *합격선이 아니라 여유입니다.* 아무도 모든 발을 맞히지 않으므로, 방 안의 체력과 정확히 같은
 * 탄약은 실제로는 끝낼 수 없는 게임입니다. ::BUDGET_MARGIN이 맨 합계 위에 요구하는 여유입니다.
 */

#include <stdio.h>
#include <math.h>

#include "world.h"
#include "enemy.h"
#include "weapon.h"
#include "pickup.h"
#include "level.h"
#include "loot.h"
#include "player.h"
#include "pools.h"

static Pools g_pools;

static int fails;
static void ok(int cond, const char *what) {
    printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void okf(int cond, const char *what, float got, float want) {
    printf("  %-56s %9.0f / %-9.0f %s\n", what, (double)got, (double)want,
           cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/**
 * @brief The damage one round of this weapon delivers, everything landing.
 *
 * ENGLISH: A hitscan's round is all of its pellets, a projectile's is its blast
 * once; melee takes no rounds and answers zero because it spends none. Read off
 * ::WeaponType rather than written down, so retuning a gun retunes the budget.
 * 한국어: 히트스캔의 한 발은 펠릿 전부이고 발사체의 한 발은 그 폭발 한 번입니다. 근접은 탄을
 * 쓰지 않으므로 0으로 답합니다. 적어 두지 않고 ::WeaponType에서 읽으므로, 총을 조율하면 예산도
 * 함께 조율됩니다.
 */
static int damage_per_round(int w) {
    const WeaponType *S = wp_stats(w);
    if (S->max_ammo <= 0) return 0;              /* the axe spends nothing */
    int pellets = S->pellets > 0 ? S->pellets : 1;
    return S->damage * pellets;
}

int main(void) {
    printf("storybudget\n\n");

    static World w;
    world_init(&w);
    if (!world_load_level(&w, w.cur_level, WORLD_ENTER_NEW)) {
        printf("  world_load_level FAILED\n");
        return 1;
    }
    (void)g_pools;

    printf("the story arena is %s, and the maw is due on wave %d\n",
           w.cur_level, WORLD_BOSS_STORY_WAVE);

    /* --- what a story run must destroy -------------------------------------
     *
     * THE FIGHT IS THE FLOOR, NOT THE WHOLE BILL. A story run ends when the maw
     * dies, so the maw and the wards that shield it are what it is REQUIRED to
     * kill. Everything else in the room may be walked away from -- and the
     * summons below are counted anyway, because a player who ignores forty
     * monsters in a room this size is a player who is not going to be shooting
     * the boss either.
     *
     * *전투가 바닥이지 청구서 전체가 아닙니다.* 스토리 플레이는 아귀가 죽으면 끝나므로, 아귀와
     * 그것을 가리는 결계핵이 반드시 죽여야 하는 것입니다. 방 안의 나머지는 지나쳐도 됩니다.
     * 그럼에도 아래의 소환은 세어 넣습니다. 이만한 방에서 몬스터 마흔을 무시하는 플레이어는
     * 보스도 쏘고 있지 않을 플레이어이기 때문입니다. */
    int maw_hp  = mon_stats(MON_MAW)->hp;
    int ward_hp = mon_stats(MON_WARD)->hp;
    int rounds  = BOSS_CYCLES - 1;               /* the last boundary is death */
    int ward_total = rounds * BOSS_WARDS * ward_hp;

    /* Every WARD_SUMMON_DMG of damage that is SURVIVED books WARD_SUMMON_COUNT
       monsters, from the maw and from each ward. The killing blow is not
       survived, so a ward pays for hp/WARD_SUMMON_DMG of its own life.
       살아남은 피해 WARD_SUMMON_DMG마다 WARD_SUMMON_COUNT마리를 예약합니다. 아귀도 결계핵도
       그렇습니다. 마지막 일격은 살아남은 것이 아니므로, 결계핵은 자기 생애의
       hp/WARD_SUMMON_DMG만큼을 지급합니다. */
    int summons = (maw_hp / WARD_SUMMON_DMG + rounds * BOSS_WARDS * (ward_hp / WARD_SUMMON_DMG))
                  * WARD_SUMMON_COUNT;

    /* The summon tables are water spirits with either brutes or casters, so the
       average summon is the mean of the two pairs' means.
       소환 표는 물의 정령에 브루트 아니면 캐스터이므로, 평균 소환은 두 쌍의 평균의 평균입니다. */
    int summon_hp = (mon_stats(MON_WATER_SPIRIT)->hp * 2
                     + mon_stats(MON_BRUTE)->hp + mon_stats(MON_CASTER)->hp) / 4;

    int need = maw_hp + ward_total + summons * summon_hp;

    printf("\nwhat a run has to destroy\n");
    printf("      the maw            %5d\n", maw_hp);
    printf("      %d ward round(s)    %5d   (%d x %d hp)\n",
           rounds, ward_total, rounds * BOSS_WARDS, ward_hp);
    printf("      %3d summon(s)      %5d   (%d hp each, averaged over the tables)\n",
           summons, summons * summon_hp, summon_hp);
    printf("      ------------------------\n");
    printf("      total              %5d\n", need);

    /* --- what the level hands out ------------------------------------------
     *
     * THE BELT IT STARTS WITH, then every weapon and every box the map places.
     * A weapon on the floor carries ::WeaponType::start_ammo and a box carries
     * ::WeaponType::pickup_ammo, which is the same arithmetic ::pickup_collect
     * does -- read through the same table so a retuned box retunes this.
     * 시작 탄띠, 그다음 맵이 놓는 모든 무기와 모든 상자입니다. 바닥의 무기는
     * ::WeaponType::start_ammo를, 상자는 ::WeaponType::pickup_ammo를 나릅니다.
     * ::pickup_collect가 하는 것과 같은 산술이며, 같은 표를 통해 읽으므로 상자를 조율하면
     * 이것도 함께 조율됩니다. */
    int rounds_of[WP_TYPES];
    for (int i = 0; i < WP_TYPES; i++) rounds_of[i] = 0;
    rounds_of[WP_SHOTGUN] = wp_stats(WP_SHOTGUN)->start_ammo;

    int boxes = 0, guns = 0, medkits = 0;
    for (int i = 0; i < w.level.n_ents; i++) {
        const char *k = w.level.ents[i].kind;
        int klen = 0;
        while (k[klen]) klen++;
        int kind = pickup_kind_for_n(k, klen);
        if (kind < 0) continue;
        int aw = PK_AMMO_WEAPON(kind), gw = PK_WEAPON_WEAPON(kind);
        if (aw >= 0 && wp_stats(aw)->max_ammo > 0) {
            rounds_of[aw] += wp_stats(aw)->pickup_ammo;
            boxes++;
        } else if (gw >= 0) {
            rounds_of[gw] += wp_stats(gw)->start_ammo;
            guns++;
        } else if (kind == PK_HEALTH) {
            medkits++;
        }
    }

    printf("\nwhat the level hands out\n");
    int have = 0;
    for (int i = 0; i < WP_TYPES; i++) {
        int dpr = damage_per_round(i);
        printf("      %-8s %4d round(s) x %3d = %6d\n",
               wp_stats(i)->name, rounds_of[i], dpr, rounds_of[i] * dpr);
        have += rounds_of[i] * dpr;
    }
    printf("      ------------------------\n");
    printf("      total              %5d   (from %d gun(s) and %d box(es))\n",
           have, guns, boxes);

    /* --- THE BELT IS THE CATCH, and the figure above does not know about it.
       ::WeaponType::max_ammo is what a player may HOLD, and a box walked over
       with a full belt tops up to the cap and throws the rest away. So the
       total is an upper bound that assumes rounds are spent as fast as they are
       found -- true of a fight and false of a sweep of the room beforehand.
       Printed per gun rather than folded in, because WHICH gun overflows is the
       useful half: the rapid's rounds against its belt are several beltfuls
       lying on the floor, and they are only ammunition if the fight lasts long
       enough to go back for them.
       *탄띠가 함정이며*, 위의 수치는 그것을 모릅니다. ::WeaponType::max_ammo는 플레이어가
       *들 수 있는* 양이고, 탄띠가 가득 찬 채로 상자를 밟으면 상한까지만 채우고 나머지는
       버려집니다. 그러므로 저 합계는 탄을 찾는 속도만큼 빨리 쓴다고 가정한 상한입니다. 전투
       중에는 참이고 미리 방을 훑는 동안에는 거짓입니다.
       합쳐 넣지 않고 총마다 찍는 이유는 *어느 총이 넘치는가*가 쓸모 있는 절반이기 때문입니다.
       연사의 탄은 자기 탄띠에 대해 여러 탄띠분이 바닥에 놓여 있다는 뜻이고, 전투가 그것을
       가지러 돌아갈 만큼 길어야만 탄약입니다. */
    printf("\n      and what a belt can hold at once\n");
    for (int i = 0; i < WP_TYPES; i++) {
        int cap = wp_stats(i)->max_ammo;
        if (cap <= 0 || rounds_of[i] == 0) continue;
        printf("      %-8s %4d round(s) against a %d belt: %.1f beltful(s)\n",
               wp_stats(i)->name, rounds_of[i], cap,
               (double)rounds_of[i] / (double)cap);
    }

    /* --- and the verdict ---------------------------------------------------
     *
     * 1.35, AND IT IS A GUESS THE CHECK MAKES HONEST. Nobody lands every
     * pellet of every shell, and the number is what this file asserts rather
     * than what it measured -- so it is stated here, once, as the one opinion
     * in the file. A third again as much ammunition as there is health is a run
     * that tolerates missing a quarter of it.
     * *1.35이며, 검사가 그것을 정직하게 만드는 추측입니다.* 아무도 모든 탄피의 모든 펠릿을
     * 맞히지 않으며, 이 수는 이 파일이 *잰* 것이 아니라 *단언하는* 것입니다. 그래서 파일에서
     * 유일한 의견으로 이곳에 한 번 적습니다. 방 안의 체력보다 3분의 1 더 많은 탄약은 그중
     * 4분의 1을 빗맞혀도 견디는 플레이입니다. */
    const float BUDGET_MARGIN = 1.35f;

    printf("\nthe verdict\n");
    printf("      %d of damage available against %d of health, %.2fx\n",
           have, need, need ? (double)have / (double)need : 0.0);
    ok(need > 0, "a story run has something to destroy");
    okf((float)have >= (float)need * BUDGET_MARGIN,
        "and enough ammunition to destroy it, with room to miss",
        (float)have, (float)need * BUDGET_MARGIN);

    /* --- the half that is printed and not asserted ------------------------- */
    printf("\nwhat it has to survive, for a reader\n");
    printf("      %d medkit(s) x %d = %d, on top of %d starting health\n",
           medkits, PICKUP_HEALTH, medkits * PICKUP_HEALTH, PLAYER_MAX_HP);
    printf("      the wave purse adds %d health every %.0fs, at the altar\n",
           2 * PICKUP_HEALTH, (double)WORLD_WAVE_TIME);
    {
        const MonAttack *A = mon_attack(MON_MAW, 0);
        if (A) printf("      the maw's opening pattern is %d x %d = %d a volley\n",
                      A->damage, A->burst, A->damage * A->burst);
    }

    printf("\n%s\n", fails ? "SOME BUDGET CHECKS FAILED" : "the story is finishable on paper");
    return fails ? 1 : 0;
}
