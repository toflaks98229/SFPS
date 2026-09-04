/**
 * @file loot.c
 * @brief Parses assets\loot.txt. No GL, no Pools -- see loot.h.
 *
 * ENGLISH
 * -------
 * The same shape effects.txt and sounds.txt use: a section header starts a
 * definition and every keyword after it belongs to that definition, so a
 * reader who has opened one of those files can read this one without being
 * told. Unknown keywords are skipped rather than rejected, for the reason
 * fx.c's parser gives: refusing an otherwise valid file because it mentions a
 * parameter this build does not have makes the format impossible to extend
 * without breaking older builds.
 *
 * PARSED LAZILY AND ONCE. Nothing here is needed until the first monster dies
 * or the first wave clears, and a headless tool that only asks how often an
 * item gives off a speck should not pay for the drop tables. ::loot_reload clears the flag rather
 * than re-parsing immediately, so a hot reload costs nothing until something
 * actually asks a question.
 *
 * 한국어
 * ------
 * @brief assets\loot.txt를 파싱합니다. GL도 Pools도 없습니다. loot.h를 참조하십시오.
 *
 * effects.txt와 sounds.txt가 쓰는 것과 같은 형태입니다. 구역 머리글이 정의를 시작하고
 * 이후의 모든 키워드가 그 정의에 속하므로, 그 파일들 중 하나를 열어 본 사람은 설명 없이도
 * 이것을 읽을 수 있습니다. 알 수 없는 키워드는 거부하지 않고 건너뜁니다. fx.c의 파서가
 * 대는 이유와 같습니다. 이 빌드에 없는 매개변수를 언급했다는 이유로 그 외에는 멀쩡한
 * 파일을 거부하면, 오래된 빌드를 깨뜨리지 않고는 형식을 확장할 수 없게 됩니다.
 *
 * 게으르게, 한 번만 파싱합니다. 첫 몬스터가 죽거나 첫 웨이브가 정리되기 전까지는 이곳의
 * 무엇도 필요하지 않으며, 알갱이 빈도만 묻는 헤드리스 도구가 드롭 표의 비용을 낼 이유는
 * 없습니다. ::loot_reload는 즉시 다시 파싱하지 않고 플래그만 지우므로, 핫 리로드는 무언가
 * 실제로 질문할 때까지 아무 비용도 들지 않습니다.
 */

#include "loot.h"
#include "enemy.h"    /* mon_type_for -- a table is headed by the name a level places */
#include "data.h"
#include "txt.h"

/* --- Static variable definitions / 정적 변수 정의 --- */

/** @brief One table per monster kind, indexed by ::MonTypeID. / 몬스터 종류마다 하나인 표. ::MonTypeID로 인덱싱합니다. */
static LootDrop   g_drop[LOOT_TABLES];
/** @brief What a cleared wave pays. / 정리된 웨이브가 지급하는 것. */
static LootReward g_reward;

/* The boss purse. A second ::LootReward rather than a multiplier applied at the
   moment of paying, so `b` in loot.txt can say something the wave reward cannot
   -- a different `at`, a longer altar burn, an item the waves never give.
   보스 지갑입니다. 지급하는 순간에 적용하는 배수가 아니라 두 번째 ::LootReward인 이유는,
   loot.txt의 `b`가 웨이브 보상이 말할 수 없는 것을 말할 수 있어야 하기 때문입니다. 다른 `at`,
   더 긴 제단 연소, 웨이브가 결코 주지 않는 아이템 같은 것들입니다. */
static LootReward g_boss;
/** @brief How often every floor item gives off a speck. / 모든 바닥 아이템이 알갱이를 내보내는 빈도. */
static LootMote   g_mote;
/** @brief Whether ::parse has run since the last ::loot_reload. / 마지막 ::loot_reload 이후 ::parse가 돌았는지. */
static int        g_parsed;

/**
 * @brief The empty table an unknown monster kind gets.
 *
 * ENGLISH: A `chance` of zero and no entries, so ::loot_pick answers "nothing"
 * without a special case. Static rather than built on demand because
 * ::loot_drop returns a borrowed pointer and a caller may hold it across the
 * frame.
 *
 * 한국어: `chance`가 0이고 항목이 없으므로 ::loot_pick이 특수한 경우 없이 "없음"으로
 * 답합니다. 필요할 때 만들지 않고 정적인 이유는, ::loot_drop이 참조 포인터를 돌려주고
 * 호출자가 그것을 프레임 너머로 들고 있을 수 있기 때문입니다.
 */
static const LootDrop g_none;

/* --- Static function prototypes / 정적 함수 프로토타입 --- */

static void parse(void);
static int  kind_for(const char *name, int len);
static int  add_item(LootItem *list, int *n, int kind, int count);

/* --- Static function definitions / 정적 함수 정의 --- */

/**
 * @brief Resolves an item name, including the one no level can place.
 *
 * ENGLISH: `held` is checked FIRST, before ::pickup_kind_for_n, so that adding
 * a real pickup called "held" would be caught as a conflict here rather than
 * silently shadowing the pseudo-kind at some later point in a table.
 *
 * 한국어: `held`를 ::pickup_kind_for_n보다 *먼저* 검사합니다. 그래야 "held"라는 실제
 * 아이템이 추가되었을 때, 표의 어느 시점에선가 의사 종류를 조용히 가리는 대신 이곳에서
 * 충돌로 드러납니다.
 */
static int kind_for(const char *name, int len) {
    if (txt_is(name, len, "held")) return LOOT_HELD;
    return pickup_kind_for_n(name, len);
}

/**
 * @brief Appends one entry, or folds it into the one already there.
 *
 * ENGLISH: A name listed twice ADDS rather than replacing, because a file that
 * says `give health 1` twice means two medkits by every reading of it. The
 * fold also means a table can list more lines than ::LOOT_ENTRIES as long as
 * it names no more items than that.
 *
 * 한국어: 같은 이름이 두 번 나오면 대체가 아니라 *더합니다*. `give health 1`을 두 번 적은
 * 파일은 어떻게 읽어도 구급상자 두 개를 뜻하기 때문입니다. 합치기 덕분에, 표가 명명하는
 * 아이템의 종류만 ::LOOT_ENTRIES 이하라면 줄 수는 그보다 많아도 됩니다.
 */
static int add_item(LootItem *list, int *n, int kind, int count) {
    if (kind == -1 || count <= 0) return 0;

    for (int i = 0; i < *n; i++)
        if (list[i].kind == kind) { list[i].n += count; return count; }

    if (*n >= LOOT_ENTRIES) return 0;
    list[*n].kind = kind;
    list[*n].n    = count;
    (*n)++;
    return count;
}

static void parse(void) {
    /* Cleared wholesale rather than field by field: a reload must not leave a
       table from the previous file standing beside the new one, and "every
       monster drops nothing" is the right answer for a file that says nothing.
       필드마다가 아니라 통째로 비웁니다. 리로드가 이전 파일의 표를 새 표 옆에 남겨 두어서는
       안 되며, 아무 말도 하지 않는 파일에 대한 옳은 답은 "모든 몬스터가 아무것도 떨어뜨리지
       않는다"입니다. */
    for (int i = 0; i < LOOT_TABLES; i++) g_drop[i] = g_none;

    LootReward zr = {0};
    g_reward = zr;
    g_boss   = zr;

    /* --- defaults ---------------------------------------------------------
     * What the game did before this file existed, so a missing or emptied
     * loot.txt plays rather than paying nothing and marking nothing. The drop
     * tables have no default -- "monsters drop nothing" IS what the game did --
     * but the reward and the motes do, because a wave that pays nothing and an
     * item nobody can see are both indistinguishable from a bug.
     * 이 파일이 있기 전에 게임이 하던 것입니다. loot.txt가 없거나 비었을 때 아무것도
     * 지급하지 않고 아무 표식도 남기지 않는 대신 플레이가 되도록 하기 위함입니다. 드롭 표에는
     * 기본값이 없습니다("몬스터는 아무것도 떨어뜨리지 않는다"가 곧 게임이 하던 것입니다).
     * 그러나 보상과 알갱이에는 있습니다. 아무것도 지급하지 않는 웨이브와 아무도 볼 수 없는
     * 아이템은 둘 다 결함과 구별되지 않기 때문입니다. */
    g_reward.at    = LOOT_AT_PLAYER;
    g_reward.out   = 3.4f;
    g_reward.up    = 4.2f;
    g_reward.altar = 0.0f;
    add_item(g_reward.item, &g_reward.n_items, PK_HEALTH, 2);
    add_item(g_reward.item, &g_reward.n_items, LOOT_HELD, 3);

    /* The boss purse defaults to a MULTIPLE of the wave's rather than to a list
       of its own, so a file that retunes `r` and forgets `b` still pays a boss
       more than a wave. A default written out as its own numbers would drift
       from the wave's the first time somebody halved one of them, and the
       failure -- a boss that pays less than the wave before it -- reads as a
       bug in the fight rather than as two lists that stopped agreeing.
       보스 지갑의 기본값은 자기만의 목록이 아니라 웨이브 지갑의 *배수*입니다. 그래야 `r`을
       다시 조율하고 `b`를 잊은 파일도 여전히 보스에 웨이브보다 많이 지급합니다. 자기 숫자로
       적어 둔 기본값은 누군가 그중 하나를 반으로 줄이는 첫 순간 웨이브 쪽과 어긋나고, 그
       실패(직전 웨이브보다 적게 지급하는 보스)는 서로 맞지 않게 된 두 목록이 아니라 전투의
       버그로 읽힙니다. */
    g_boss = g_reward;
    for (int i = 0; i < g_boss.n_items; i++)
        g_boss.item[i].n *= LOOT_BOSS_MULTIPLE;
    g_boss.altar = LOOT_BOSS_ALTAR;
    g_boss.out   = g_reward.out * 1.3f;
    g_boss.up    = g_reward.up  * 1.2f;

    g_mote.rate  = 0.34f;
    g_mote.hurry = 0.08f;
    g_mote.hold  = 1.20f;
    g_mote.range = 22.0f;

    const char *p = data_text(DATA_LOOT);

    /* Which section the keywords now belong to. Three pointers rather than a
       tag, because that is what the keywords actually need: `chance` writes
       through `drop`, `at` through the reward, and a keyword whose pointer is
       null simply has nothing to apply to -- the same rule fx.c states as "a
       keyword before any `e`".
       키워드가 지금 어느 구역에 속하는지입니다. 태그가 아니라 포인터 셋인 이유는 키워드가
       실제로 필요로 하는 것이 그것이기 때문입니다. `chance`는 `drop`을 통해, `at`은 보상을
       통해 씁니다. 포인터가 널인 키워드는 그저 적용할 대상이 없으며, fx.c가 "어떤 `e`보다
       앞선 키워드"라고 적은 것과 같은 규칙입니다. */
    LootDrop   *drop   = 0;
    LootReward *reward = 0;
    LootMote   *mote   = 0;

    /* Every `give held` in one purse walks the roster, so the file's own
       ordering decides which weapon is fed first and a reload does not shuffle
       it. Not a member of LootReward: it is parser state, and a cursor stored
       beside the answer is a cursor somebody advances twice.
       한 몫 안의 모든 `give held`가 보유 목록을 훑으므로, 어느 무기가 먼저 채워지는지는
       파일 자신의 순서가 정하며 리로드가 그것을 뒤섞지 않습니다. LootReward의 멤버가 아닌
       이유는 이것이 파서의 상태이기 때문이며, 답 곁에 보관된 커서는 누군가 두 번
       전진시키는 커서입니다. */

    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;

        /* --- section headers --- */
        if (txt_is(t, len, "d")) {
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;

            /* A name resolved through mon_type_for rather than through a list
               of our own, so a table is headed by the SAME name the level
               places the monster under. A second vocabulary would agree with
               the first until somebody added a monster.
               우리만의 목록이 아니라 mon_type_for로 이름을 해석하므로, 표의 머리글은
               레벨이 그 몬스터를 배치할 때 쓰는 것과 *같은* 이름입니다. 두 번째 어휘는
               누군가 몬스터를 추가하기 전까지만 첫 번째와 일치합니다. */
            char name[24];
            txt_copy(name, sizeof name, nm, len);
            int type = mon_type_for(name);

            /* An unknown monster is parsed and DISCARDED rather than ending
               the file: a loot.txt written for a build with a sixth monster
               should still feed the five this build has.
               알 수 없는 몬스터는 파일을 끝내지 않고 파싱된 뒤 *버려집니다*. 여섯 번째
               몬스터가 있는 빌드용으로 쓰인 loot.txt도 이 빌드가 가진 다섯에게는 그대로
               먹여야 합니다. */
            drop   = (type >= 0 && type < LOOT_TABLES) ? &g_drop[type] : 0;
            reward = 0;
            mote   = 0;
            continue;
        }
        if (txt_is(t, len, "r")) {
            /* The reward is cleared HERE and not above, so a file with no `r`
               keeps the defaults while a file that opens one starts from
               empty. Otherwise `give health 1` in the file would mean three
               medkits: its one plus the default two.
               보상은 위가 아니라 *이곳에서* 비웁니다. `r`이 없는 파일은 기본값을 유지하고,
               `r`을 연 파일은 빈 상태에서 시작하게 하기 위함입니다. 그러지 않으면 파일의
               `give health 1`이 구급상자 셋을 뜻하게 됩니다. 그 하나에 기본값 둘이
               더해지기 때문입니다. */
            LootReward z = {0};
            float out = g_reward.out, up = g_reward.up;
            g_reward = z;
            g_reward.at  = LOOT_AT_PLAYER;
            g_reward.out = out;
            g_reward.up  = up;

            reward = &g_reward;
            drop   = 0;
            mote   = 0;
            continue;
        }
        if (txt_is(t, len, "b")) {
            /* The boss purse. Same grammar as `r`, same clearing rule, and the
               same pointer does the writing -- which is the whole reason `r`
               was written through one in the first place.
               ITS DEFAULT IS THE WAVE REWARD, not empty. A loot.txt that never
               opens a `b` should pay SOMETHING for a boss rather than nothing,
               because a boss that pays nothing is indistinguishable from a boss
               whose reward failed -- and that is the argument the wave reward's
               own default is built on.
               보스 지갑입니다. `r`과 같은 문법, 같은 비우기 규칙이며, 같은 포인터가 씁니다.
               애초에 `r`을 포인터를 통해 쓰게 만든 이유가 그것입니다.
               *기본값은 비어 있음이 아니라 웨이브 보상입니다.* `b`를 한 번도 열지 않는
               loot.txt는 보스에 대해 아무것도가 아니라 *무언가*를 지급해야 합니다. 아무것도
               지급하지 않는 보스는 보상이 실패한 보스와 구별되지 않으며, 그것이 웨이브 보상
               자신의 기본값이 기반하는 논거입니다. */
            LootReward z = {0};
            float out = g_boss.out, up = g_boss.up;
            g_boss = z;
            g_boss.at  = LOOT_AT_PLAYER;
            g_boss.out = out;
            g_boss.up  = up;

            reward = &g_boss;
            drop   = 0;
            mote   = 0;
            continue;
        }
        if (txt_is(t, len, "m")) {
            /* NOT cleared, unlike the reward: every field has a working default
               and an `m` block that sets only `rate` should keep the rest
               rather than zeroing `range` and emitting nowhere.
               보상과 달리 비우지 *않습니다*. 모든 필드에 동작하는 기본값이 있으며, `rate`만
               설정한 `m` 블록은 `range`를 0으로 만들어 아무 데서도 내보내지 않게 되는 대신
               나머지를 유지해야 합니다. */
            mote   = &g_mote;
            drop   = 0;
            reward = 0;
            continue;
        }

        /* --- keywords --- */
        int ok = 1, v = 0;

        if (drop) {
            if (txt_is(t, len, "chance")) {
                p = txt_read_int(p, &v, &ok);
                if (ok) drop->chance = v < 0 ? 0 : (v > 100 ? 100 : v);
            } else if (txt_is(t, len, "item")) {
                const char *nm = txt_token(p, &len);
                if (!nm) break;
                p = nm + len;
                int kind = kind_for(nm, len);
                p = txt_read_int(p, &v, &ok);
                if (ok) drop->weight += add_item(drop->item, &drop->n_items, kind, v);
            }
            continue;
        }

        if (reward) {
            if (txt_is(t, len, "give")) {
                const char *nm = txt_token(p, &len);
                if (!nm) break;
                p = nm + len;
                int kind = kind_for(nm, len);
                p = txt_read_int(p, &v, &ok);
                if (ok) add_item(reward->item, &reward->n_items, kind, v);
            } else if (txt_is(t, len, "at")) {
                const char *nm = txt_token(p, &len);
                if (nm) {
                    p = nm + len;
                    reward->at = txt_is(nm, len, "altar")  ? LOOT_AT_ALTAR
                               : txt_is(nm, len, "centre") ? LOOT_AT_CENTRE
                               : txt_is(nm, len, "center") ? LOOT_AT_CENTRE
                                                           : LOOT_AT_PLAYER;
                }
            }
            /* Centimetres per second and milliseconds in the file, metres per
               second and seconds here. Converted at the one place the text
               becomes a number, so nothing downstream carries a unit it has to
               remember to divide by.
               파일에서는 cm/s와 밀리초, 이곳에서는 m/s와 초입니다. 텍스트가 숫자가 되는 그
               한 곳에서 변환하므로, 그 아래의 무엇도 나눠야 한다고 기억해야 할 단위를 들고
               다니지 않습니다. */
            else if (txt_is(t, len, "out"))
                { p = txt_read_int(p, &v, &ok); if (ok) reward->out = v * 0.01f; }
            else if (txt_is(t, len, "up"))
                { p = txt_read_int(p, &v, &ok); if (ok) reward->up = v * 0.01f; }
            else if (txt_is(t, len, "altar"))
                { p = txt_read_int(p, &v, &ok); if (ok) reward->altar = v * 0.001f; }
            continue;
        }

        if (mote) {
            if (txt_is(t, len, "rate"))
                { p = txt_read_int(p, &v, &ok); if (ok) mote->rate = v * 0.001f; }
            else if (txt_is(t, len, "hurry"))
                { p = txt_read_int(p, &v, &ok); if (ok) mote->hurry = v * 0.001f; }
            else if (txt_is(t, len, "hold"))
                { p = txt_read_int(p, &v, &ok); if (ok) mote->hold = v * 0.001f; }
            else if (txt_is(t, len, "range"))
                { p = txt_read_int(p, &v, &ok); if (ok) mote->range = v * 0.01f; }
            continue;
        }
        /* a keyword before any section header: nothing to apply to */
    }

    /* NEITHER INTERVAL MAY BE ZERO, and this is not tidiness. Both are what a
       countdown is reset TO, so a zero means the timer expires on the frame it
       is set and every item in range emits once per frame -- 1536 particles in
       a third of a second, the whole shared pool, spent on specks by a file
       with a typo in it. `rate 0` is the one edit here that can take every
       other effect in the game down with it.
       A file may still turn the specks off entirely: `range 0` puts every item
       out of range, which costs nothing and says so plainly.
       *어느 간격도 0일 수 없으며*, 이것은 단정함의 문제가 아닙니다. 둘 다 카운트다운이
       *되돌아가는* 값이므로 0은 설정된 프레임에 곧바로 만료된다는 뜻이고, 사거리 안의 모든
       아이템이 프레임마다 하나씩 내보냅니다. 3분의 1초에 입자 1536개, 공유 풀 전체를 오타
       하나가 든 파일이 알갱이에 써 버립니다. 이곳에서 게임의 다른 모든 이펙트를 함께
       끌어내릴 수 있는 편집은 `rate 0` 하나뿐입니다.
       파일은 여전히 알갱이를 완전히 끌 수 있습니다. `range 0`이면 모든 아이템이 사거리
       밖이며, 비용이 들지 않고 그렇다고 분명히 말합니다. */
    if (g_mote.rate  < 0.02f) g_mote.rate  = 0.02f;
    if (g_mote.hurry < 0.02f) g_mote.hurry = 0.02f;
    if (g_mote.hold  < 0.0f)  g_mote.hold  = 0.0f;
    if (g_mote.range < 0.0f)  g_mote.range = 0.0f;

    g_parsed = 1;
}

/* --- Public function definitions / 공개 함수 정의 --- */

const LootDrop *loot_drop(int mon_type) {
    if (!g_parsed) parse();
    if (mon_type < 0 || mon_type >= LOOT_TABLES) return &g_none;
    return &g_drop[mon_type];
}

const LootReward *loot_reward(void) {
    if (!g_parsed) parse();
    return &g_reward;
}

const LootReward *loot_boss_reward(void) {
    if (!g_parsed) parse();
    return &g_boss;
}

const LootMote *loot_mote(void) {
    if (!g_parsed) parse();
    return &g_mote;
}

int loot_pick(const LootDrop *d, float chance, float which) {
    if (!d || d->weight <= 0) return -1;
    if (chance * 100.0f >= (float)d->chance) return -1;

    /* Walking the weights rather than dividing into them, so an entry of
       weight 0 -- which `item health 0` is, and which add_item already refuses
       -- could never be selected by a rounding error.
       가중치로 나누지 않고 훑습니다. 그래야 가중치 0인 항목(`item health 0`이 그것이며
       add_item이 이미 거부합니다)이 반올림 오차로 선택되는 일이 결코 없습니다. */
    float roll = which * (float)d->weight;
    float acc  = 0.0f;
    for (int i = 0; i < d->n_items; i++) {
        acc += (float)d->item[i].n;
        if (roll < acc) return d->item[i].kind;
    }

    /* Only reachable when `which` rounds to exactly 1.0. The last entry is the
       answer, not -1: "the die landed on the edge" must not read as "nothing
       dropped", which is a rate that is quietly wrong rather than visibly.
       `which`가 정확히 1.0으로 반올림될 때만 도달합니다. 답은 -1이 아니라 마지막
       항목입니다. "주사위가 모서리에 섰다"가 "아무것도 떨어지지 않았다"로 읽혀서는 안 되며,
       그것은 눈에 띄게 틀린 확률이 아니라 조용히 틀린 확률입니다. */
    return d->item[d->n_items - 1].kind;
}

int loot_held_kind(const Weapon *w, int *gun) {
    if (!w) return -1;
    int start = gun ? *gun : 0;

    for (int k = 0; k < WP_TYPES; k++) {
        int t = (start + k) % WP_TYPES;
        if (!w->owned[t]) continue;
        /* A weapon with no belt (max_ammo 0) is owed nothing, however owned it is.
           탄띠가 없는 무기(max_ammo 0)는 아무리 보유해도 빚진 것이 없습니다. */
        if (wp_stats(t)->max_ammo <= 0) continue;
        if (gun) *gun = t + 1;
        return PK_AMMO_FOR(t);
    }
    return -1;                     /* holding nothing that takes ammo */
}

void loot_reload(void) { g_parsed = 0; }
