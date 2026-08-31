/* loottest -- the drop tables, the purse, and the two rolls a kill spends.
 *
 * ENGLISH
 * -------
 * A DROP RATE IS THE ONE NUMBER IN THIS GAME THAT CANNOT BE CHECKED BY LOOKING.
 * "The brute pays about twice as often as the water spirit" is a claim about a thousand
 * kills, and a player who sees twenty of them has seen noise. So it is measured
 * here, over enough rolls that the answer is the rate rather than the sample --
 * and the rolls are handed in rather than generated, so this file measures the
 * table and not a random number generator.
 *
 * The other half is the plumbing that cannot be seen either way: `held`
 * resolving against a roster that does not include the gun, a `chance` of zero
 * meaning zero rather than "the first entry", an entry that names nothing being
 * dropped instead of silently becoming ammo. Each of those is invisible in the
 * running game -- the symptom is a drop that looks slightly wrong, once.
 *
 * WHAT THIS DOES NOT DO is assert the shipped numbers. `chance 26` for a water spirit is
 * a design decision somebody will move next week, and a test that names it goes
 * red on that edit while proving nothing. What is asserted is that the file is
 * READ -- that whatever it says arrives intact at the other end.
 *
 * 한국어
 * ------
 * 드롭 확률은 이 게임에서 *보는 것으로는 검사할 수 없는* 유일한 숫자입니다. "브루트는 물의 정령보다
 * 두 배쯤 자주 지급한다"는 천 번의 처치에 대한 주장이고, 그중 스무 번을 본 플레이어는 잡음을
 * 본 것입니다. 그래서 이곳에서, 답이 표본이 아니라 확률이 될 만큼 충분한 굴림에 걸쳐
 * 측정합니다. 그리고 굴림은 생성하지 않고 건네받으므로, 이 파일이 재는 것은 표이지 난수
 * 생성기가 아닙니다.
 *
 * 나머지 절반은 어느 쪽으로도 볼 수 없는 배관입니다. 그 총이 없는 보유 목록에 비추어 해석되는
 * `held`, "첫 항목"이 아니라 0을 뜻하는 `chance 0`, 조용히 탄약이 되는 대신 버려지는 무의미한
 * 이름의 항목입니다. 각각은 실행 중인 게임에서 보이지 않으며, 증상은 한 번, 조금 이상해 보이는
 * 드롭입니다.
 *
 * *이 파일이 하지 않는 일은* 배포된 숫자를 단언하는 것입니다. 물의 정령의 `chance 26`은 누군가
 * 다음 주에 옮길 설계 결정이고, 그것을 적은 검사는 그 편집에 빨개지면서 아무것도 증명하지
 * 않습니다. 단언하는 것은 파일이 *읽힌다는 것*, 즉 그것이 말하는 무엇이든 반대편에 온전히
 * 도착한다는 것입니다.
 */

#include <stdio.h>

#include "loot.h"
#include "enemy.h"
#include "pickup.h"
#include "weapon.h"

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void oki(int cond, const char *what, int got, int want) {
    printf("  %-58s %5d / %-5d %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Rolls spread evenly over [0,1) rather than taken from a generator.
 *
 * ENGLISH: A generator would make every count below depend on its seed, and a
 * failure would then be indistinguishable from an unlucky run -- which is the
 * one failure mode a test of a probability must not have. An even sweep asks the
 * table the only question that has an exact answer: of N evenly spaced rolls,
 * how many fall inside `chance`.
 *
 * 한국어: 생성기를 쓰면 아래의 모든 개수가 그 씨앗에 의존하게 되고, 실패가 운 나쁜 실행과
 * 구별되지 않게 됩니다. 확률에 대한 검사가 결코 가져서는 안 되는 단 하나의 실패 양상이
 * 그것입니다. 균등한 훑기는 정확한 답이 있는 유일한 질문을 표에 던집니다. 균등하게 나뉜 N번의
 * 굴림 중 몇 번이 `chance` 안에 떨어지는가입니다. */
#define ROLLS 1000
static float sweep(int i) { return (float)i / (float)ROLLS; }

int main(void) {
    printf("loot\n");

    /* --- the file is read at all ----------------------------------------
       Everything below is a claim about a table, and a table nobody filled
       would pass most of them by answering "nothing" consistently.
       아래의 모든 것은 표에 대한 주장이며, 아무도 채우지 않은 표는 "없음"으로 일관되게
       답함으로써 그중 대부분을 통과합니다. */
    printf("\nassets\\loot.txt reaches the tables\n");
    {
        int with_a_table = 0;
        for (int t = 0; t < MON_TYPES; t++)
            if (loot_drop(t)->n_items > 0) with_a_table++;

        ok(with_a_table > 0, "at least one monster kind has a drop table");

        const LootReward *r = loot_reward();
        ok(r->n_items > 0, "the wave reward lists something to pay");
        ok(r->out > 0.0f && r->up > 0.0f, "and a throw that leaves the ground");

        const LootMote *m = loot_mote();
        ok(m->rate > 0.0f, "a settled item still gives off specks");
        ok(m->range > 0.0f, "and there is a distance it does so within");

        /* NEITHER INTERVAL IS ZERO, checked against the FILE rather than
           against the clamp. A countdown reset to zero expires on the frame it
           is set, so every item in range would emit once per frame and fill the
           shared particle pool in a third of a second -- with the specks then
           evicting every impact, trail and burst in the level. The clamp in
           loot.c is what stops it; this is what notices that it had to.
           *어느 간격도 0이 아니며*, 보정값이 아니라 *파일*에 대고 검사합니다. 0으로 되돌려진
           카운트다운은 설정된 프레임에 만료되므로, 사거리 안의 모든 아이템이 프레임마다 하나씩
           내보내고 3분의 1초 만에 공유 입자 풀을 채웁니다. 그러면 그 알갱이들이 레벨의 모든
           피격·궤적·폭발을 밀어냅니다. 그것을 막는 것이 loot.c의 보정이고, 그것이 필요했음을
           알아채는 것이 이 검사입니다. */
        ok(m->hurry >= 0.02f && m->rate >= 0.02f,
           "and no interval a timer resets to is an instant");

        /* Announcing has to be FASTER than resting, or the arrival is told in
           the same voice as the standing still and the player is shown nothing
           at the one moment there is something to show.
           알리는 쪽이 쉬는 쪽보다 *빨라야* 합니다. 그러지 않으면 도착이 가만히 있는 것과 같은
           목소리로 말해지고, 보여 줄 것이 있는 유일한 순간에 플레이어는 아무것도 보지 못합니다. */
        ok(m->hurry < m->rate, "and it announces itself faster than it rests");
    }

    /* --- every kind a table names is a kind ------------------------------
       A name the file got wrong -- `helth`, `rapidamo` -- must produce NO entry
       rather than entry zero, which is PK_AMMO and would have every monster in
       the level quietly dropping shells.
       파일이 틀리게 적은 이름(`helth`, `rapidamo`)은 0번 항목이 아니라 *아무* 항목도 만들지
       않아야 합니다. 0번은 PK_AMMO이며, 그러면 레벨의 모든 몬스터가 조용히 산탄을
       떨어뜨리게 됩니다. */
    printf("\nno entry is a kind by accident\n");
    {
        int bad = 0, weights = 0;
        for (int t = 0; t < MON_TYPES; t++) {
            const LootDrop *d = loot_drop(t);
            int sum = 0;
            for (int i = 0; i < d->n_items; i++) {
                int k = d->item[i].kind;
                if (k != LOOT_HELD && (k < 0 || k >= PK_KINDS)) bad++;
                if (d->item[i].n <= 0) bad++;
                sum += d->item[i].n;
            }
            if (sum != d->weight) weights++;
        }
        oki(bad == 0, "every entry names a real kind and carries weight", bad, 0);
        oki(weights == 0, "and the cached weight is the sum of the entries",
            weights, 0);
    }

    /* --- the two rolls do what the header says ---------------------------
       ::loot_pick takes them rather than owning them, so this is where "the
       chance decides IF and the weights decide WHAT" is actually checked. Both
       halves fail silently in the game: a chance that also selects makes the
       first entry the only one that ever drops, and weights that also gate make
       a monster pay far more often than its rate.
       ::loot_pick은 굴림을 소유하지 않고 *받으므로*, "확률이 여부를 정하고 가중치가 무엇을
       정한다"가 실제로 검사되는 곳이 이곳입니다. 두 절반 모두 게임 안에서는 조용히
       실패합니다. 선택까지 하는 확률은 첫 항목만 떨어지게 만들고, 여부까지 정하는 가중치는
       몬스터가 자기 확률보다 훨씬 자주 지급하게 만듭니다. */
    printf("\nthe chance decides IF, the weights decide WHAT\n");
    {
        /* Built here rather than read, so the numbers under test are this
           file's and not a design decision somebody is about to move.
           읽지 않고 이곳에서 만듭니다. 검사 대상 숫자가 누군가 곧 옮길 설계 결정이 아니라 이
           파일의 것이 되도록 하기 위함입니다. */
        LootDrop d = {0};
        d.chance = 40;
        d.item[0].kind = PK_HEALTH; d.item[0].n = 3;
        d.item[1].kind = PK_AMMO;   d.item[1].n = 1;
        d.n_items = 2;
        d.weight  = 4;

        int any = 0, health = 0, ammo = 0;
        for (int i = 0; i < ROLLS; i++) {
            int k = loot_pick(&d, sweep(i), sweep((i * 7) % ROLLS));
            if (k < 0) continue;
            any++;
            if (k == PK_HEALTH) health++;
            else if (k == PK_AMMO) ammo++;
        }

        oki(any == ROLLS * 40 / 100, "40% of evenly spread rolls drop something",
            any, ROLLS * 40 / 100);
        ok(health > ammo * 2, "and weight 3 against 1 lands mostly on the 3");
        oki(health + ammo == any, "with nothing falling between the entries",
            health + ammo, any);

        d.chance = 0;
        int none = 0;
        for (int i = 0; i < ROLLS; i++)
            if (loot_pick(&d, sweep(i), sweep(i)) < 0) none++;
        oki(none == ROLLS, "a chance of 0 drops nothing at all", none, ROLLS);

        d.chance = 100;
        int all = 0;
        for (int i = 0; i < ROLLS; i++)
            if (loot_pick(&d, sweep(i), sweep(i)) >= 0) all++;
        oki(all == ROLLS, "and a chance of 100 always drops", all, ROLLS);

        /* THE EDGE OF THE DIE. `which` at exactly 1.0 walks past the last
           weight, and answering -1 there would read as "nothing dropped" on a
           roll the chance already said drops -- a rate that is quietly wrong.
           *주사위의 모서리입니다.* `which`가 정확히 1.0이면 마지막 가중치를 지나쳐 걷는데,
           그곳에서 -1로 답하면 확률이 이미 떨어진다고 말한 굴림이 "아무것도 떨어지지
           않았다"로 읽힙니다. 조용히 틀린 확률입니다. */
        ok(loot_pick(&d, 0.0f, 1.0f) >= 0,
           "a roll landing on the very edge still drops something");
    }

    /* --- an empty table is an answer ------------------------------------- */
    printf("\nnothing is an answer\n");
    {
        LootDrop empty = {0};
        ok(loot_pick(&empty, 0.0f, 0.0f) < 0, "a table with no entries drops nothing");
        ok(loot_pick(0, 0.0f, 0.0f) < 0, "and neither does no table at all");

        ok(loot_drop(-1) != 0 && loot_drop(9999) != 0,
           "an out-of-range monster gets a table rather than a null");
        oki(loot_drop(9999)->chance == 0, "and that table is empty",
            loot_drop(9999)->chance, 0);
    }

    /* --- `held` against a real roster -------------------------------------
       The pseudo-kind exists so that a box for a gun the player has not found is
       never thrown. That is checked by not owning one.
       의사 종류가 존재하는 이유는 찾지 못한 총의 상자가 결코 던져지지 않게 하기 위해서입니다.
       보유하지 않음으로써 검사합니다. */
    printf("\n`held` is answered against what is actually carried\n");
    {
        Weapon w = {0};
        int gun = 0;
        oki(loot_held_kind(&w, &gun) == -1,
            "a player holding nothing is owed no ammo box",
            loot_held_kind(&w, &gun), -1);

        w.owned[WP_SHOTGUN] = 1;
        gun = 0;
        int a = loot_held_kind(&w, &gun);
        int b = loot_held_kind(&w, &gun);
        ok(a == PK_AMMO_FOR(WP_SHOTGUN) && b == a,
           "one gun owned gets every box, rather than one and two nothings");

        /* Two guns, and the cursor is what spreads the purse across them. A
           player fed three boxes for the gun they already filled has been paid
           in something they cannot use.
           총 둘이며, 몫을 그 둘에 퍼뜨리는 것이 커서입니다. 이미 채운 총의 상자 셋을 받은
           플레이어는 쓸 수 없는 것으로 지급받은 것입니다. */
        w.owned[WP_RAPID] = 1;
        gun = 0;
        int first  = loot_held_kind(&w, &gun);
        int second = loot_held_kind(&w, &gun);
        ok(first != second, "two guns owned are fed in turn");

        /* And the cursor is the caller's: a fresh one starts over, which is what
           makes one corpse's drop independent of the last one's.
           그리고 커서는 호출자의 것입니다. 새 커서는 처음부터 시작하며, 그것이 시체 하나의
           드롭을 이전 것과 독립적으로 만듭니다. */
        int fresh = 0;
        ok(loot_held_kind(&w, &fresh) == first,
           "and a fresh cursor starts from the first gun again");

        ok(loot_held_kind(0, &gun) == -1, "no roster at all is owed nothing");
    }

    /* --- a reload re-reads and does not accumulate ------------------------
       ::loot_reload clears a flag rather than re-parsing, so the parse happens
       on the next question. The failure worth catching is a table that GROWS
       across reloads -- an `item` line folded into an entry that was never
       cleared -- because that reads as a monster that pays more every time the
       author saves the file.
       ::loot_reload은 다시 파싱하지 않고 플래그만 지우므로, 파싱은 다음 질문에서 일어납니다.
       잡을 가치가 있는 실패는 리로드를 거치며 *자라나는* 표입니다. 결코 비워지지 않은 항목에
       접힌 `item` 줄이며, 그것은 제작자가 파일을 저장할 때마다 더 많이 지급하는 몬스터로
       읽힙니다. */
    printf("\na reload re-reads rather than accumulating\n");
    {
        int before[MON_TYPES], after[MON_TYPES], moved = 0;
        for (int t = 0; t < MON_TYPES; t++) before[t] = loot_drop(t)->weight;

        loot_reload();
        for (int t = 0; t < MON_TYPES; t++) after[t] = loot_drop(t)->weight;

        for (int t = 0; t < MON_TYPES; t++) if (before[t] != after[t]) moved++;
        oki(moved == 0, "the same file parsed twice gives the same weights",
            moved, 0);

        int n_before = loot_reward()->n_items;
        loot_reload();
        oki(loot_reward()->n_items == n_before,
            "and the purse does not grow when it is re-read",
            loot_reward()->n_items, n_before);
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall loot checks passed\n", fails);
    return fails != 0;
}
