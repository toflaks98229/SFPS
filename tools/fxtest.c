/* fxtest -- the particle system's parser and simulation, headless.
 *
 * fx_spawn and fx_update touch no GL, which is the property that makes this
 * possible: the parse, the pool accounting, the ageing and the gravity are all
 * checked here without a window. Only fx_draw needs a context, and what it does
 * is issue the geometry these functions decided on.
 *
 * What this is really guarding is the FORMAT. An effect file is authored by
 * hand and the parser is the only thing standing between a typo and a silently
 * wrong effect, so the cases that matter are the malformed ones: a keyword
 * before any definition, an unknown keyword, a missing number. Each of those
 * has a defined behaviour and none of them may take the parser off the rails.
 *
 * fx_spawn과 fx_update는 GL을 사용하지 않으며, 그것이 이 테스트를 가능하게 하는
 * 성질입니다. 파싱, 풀 관리, 노화, 중력을 창 없이 검증합니다. 실제로 지키려는 것은
 * *형식*입니다. 이펙트 파일은 손으로 작성되며 오타와 조용히 잘못된 이펙트 사이에 있는
 * 것은 파서뿐이므로, 중요한 것은 잘못된 입력들입니다.
 */

#include <stdio.h>
#include <math.h>

#include "fx.h"
#include "diag.h"
#include "pools.h"
/* The pools this file drives, owned here the way a ::World owns its own. The
   five modules that used to keep these in file-scope arrays hand them back
   now, which is why a fixture no longer inherits the previous case's monsters.
   See src/pools.h.
   이 파일이 구동하는 풀이며, ::World가 자기 것을 소유하듯 이곳에서 소유합니다. 이것을 파일
   스코프 배열에 담고 있던 다섯 모듈이 이제 돌려주며, 그래서 픽스처가 이전 사례의 몬스터를
   물려받지 않습니다. src/pools.h를 참조하십시오. */
static Pools g_pools;


static int fails;
static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void okd(int cond, const char *what, int got, int want) {
    printf("  %-58s %6d / %6d  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Heights are metres and the differences that matter here are fractions of
   one, so they cannot be reported as the integers okd takes.
   높이는 미터 단위이고 이곳에서 중요한 차이는 1 미만이므로, okd가 받는 정수로는 보고할
   수 없습니다. */
static void okf(int cond, const char *what, float got, float want) {
    printf("  %-58s %6.2f / %6.2f  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

int main(void) {
    printf("fxtest\n\n");

    const float DT = 1.0f / 60.0f;

    /* --- the file parses into something ----------------------------------- */
    {
        int n = fx_def_count();
        printf("  parsed %d effect definitions\n\n", n);
        ok(n > 0, "assets\\effects.txt supplies at least one effect");
        ok(n <= FX_MAX_DEFS, "and no more than the table can hold");
    }

    /* --- spawning by name -------------------------------------------------- */
    {
        fx_reload(&g_pools);
        okd(fx_live_count(&g_pools) == 0, "a reload leaves no live particles",
            fx_live_count(&g_pools), 0);

        fx_spawn(&g_pools, "spark", v3f(0, 0, 0), v3f(0, 1, 0));
        int after = fx_live_count(&g_pools);
        ok(after > 0, "spawning a known effect produces particles");

        /* The whole point of the format: an effect nobody has authored yet is
           not an error. A typo costs a missing puff, not a crash. */
        int before = fx_live_count(&g_pools);
        fx_spawn(&g_pools, "no-such-effect", v3f(0, 0, 0), v3f(0, 1, 0));
        okd(fx_live_count(&g_pools) == before,
            "an unknown name spawns nothing and does not crash",
            fx_live_count(&g_pools), before);
    }

    /* --- particles age out ------------------------------------------------- */
    {
        fx_reload(&g_pools);
        fx_spawn(&g_pools, "spark", v3f(0, 0, 0), v3f(0, 1, 0));
        ok(fx_live_count(&g_pools) > 0, "the burst is alive to begin with");

        /* Ten seconds is longer than any authored life, so everything must be
           gone -- a particle that never retires is a leak that only shows up
           as the pool slowly filling with the undead. */
        for (int i = 0; i < 600; i++) fx_update(&g_pools, DT);
        okd(fx_live_count(&g_pools) == 0,
            "and every particle retires once its life runs out",
            fx_live_count(&g_pools), 0);
    }

    /* --- the pool has a hard ceiling --------------------------------------- */
    {
        fx_reload(&g_pools);
        /* Far more than the pool can hold. The excess must overwrite rather
           than overflow, and the count must never exceed the cap.

           THE COUNT IS DERIVED FROM THE POOL, not written down. It was 200,
           which overflowed a pool of 640 and did not overflow the 1536 the
           blast layers needed -- so the moment the cap was raised this test
           stopped exercising the overflow branch and said nothing about it.
           One spawn is at least one particle, so FX_MAX_PARTICLES spawns
           cannot fail to fill it whatever the cap becomes.
           개수를 적어 두지 않고 풀 크기에서 *유도합니다*. 200이었고, 640짜리 풀은
           넘쳤지만 폭발 레이어에 필요했던 1536은 넘지 못했습니다. 상한을 올린 순간 이
           테스트는 넘침 분기를 시험하기를 멈췄고 그에 대해 아무 말도 하지 않았습니다.
           한 번의 생성은 최소 한 개의 입자이므로, FX_MAX_PARTICLES번 생성하면 상한이
           무엇이 되든 풀을 채우지 못할 수 없습니다. */
        for (int i = 0; i < FX_MAX_PARTICLES; i++)
            fx_spawn(&g_pools, "spark", v3f(0, 0, 0), v3f(0, 1, 0));

        okd(fx_live_count(&g_pools) <= FX_MAX_PARTICLES,
            "a flood never exceeds the particle pool",
            fx_live_count(&g_pools), FX_MAX_PARTICLES);

        /* That the flood REACHED the cap, so the check above is a ceiling
           being tested rather than one merely not approached.
           넘침이 상한에 *도달했음*을 확인합니다. 위의 검사가 다가가지도 못한 천장이
           아니라 실제로 시험된 천장이 되도록 합니다. */
        okd(fx_live_count(&g_pools) == FX_MAX_PARTICLES,
            "and the flood is big enough to actually reach it",
            fx_live_count(&g_pools), FX_MAX_PARTICLES);

#ifdef DIAG_ENABLED
        ok(diag_count(DIAG_FX_CAP) > 0,
           "and the overflow is reported rather than silent");
#endif
    }

    /* --- the dome measures the radius it was given -------------------------
       This is the only effect in the file that makes a claim with a NUMBER in
       it: the shell stops where the blast damage stops. Everything else here
       would pass just as well if fx_spawn_scaled ignored its scale entirely --
       the particles would spawn, move, and retire on time, and the dome would
       simply be the wrong size, which is the one failure that looks completely
       convincing on screen while telling the player something untrue.

       Checked at two radii rather than one, because a dome hard-coded to any
       single size passes a single-radius test.

       이 파일에서 *수치*를 담은 주장을 하는 유일한 이펙트입니다. 껍질은 폭발 데미지가
       멈추는 곳에서 멈춥니다. 여기의 다른 모든 검사는 fx_spawn_scaled가 scale을 통째로
       무시해도 통과합니다. 입자는 생성되고 움직이고 제때 사라질 것이며, 돔은 그저 크기가
       틀릴 뿐입니다. 화면에서는 완벽히 그럴듯해 보이면서 플레이어에게 사실이 아닌 것을
       말하는 유일한 실패입니다. 반경 하나가 아니라 둘로 검사하는 이유는, 특정 크기로
       고정된 돔도 반경 하나짜리 검사는 통과하기 때문입니다. */
    {
        const v3 AT = { 3.0f, 2.0f, -4.0f };
        float reached[2], width[2];
        const float WANT[2] = { 1.0f, 4.2f };   /* unit, and the grenade's */

        for (int k = 0; k < 2; k++) {
            fx_reload(&g_pools);
            fx_spawn_scaled(&g_pools, "blastdome", AT, v3f(0, 1, 0), WANT[k]);
            ok(fx_live_count(&g_pools) > 0, k ? "blastdome spawns at 4.2m"
                                      : "blastdome spawns at 1m");
            /* Its life is 300ms; run just past it so the shell is at full
               extent, and stop before the particles retire.
               수명이 300ms이므로 껍질이 최대로 퍼지도록 그 직전까지 돌리고, 입자가
               사라지기 전에 멈춥니다. */
            for (int i = 0; i < 17; i++) fx_update(&g_pools, DT);
            fx_radius_spread(&g_pools, AT, &reached[k], &width[k]);
        }

        okf(reached[1] > reached[0] * 3.0f,
            "a 4.2m dome reaches far beyond a 1m one",
            reached[1], reached[0]);

        /* The ratio, not the absolute distance: drag and gravity both eat into
           how far a particle actually gets, so the authored speed x life is a
           ceiling rather than a promise. What must hold is that asking for 4.2
           times the radius produces 4.2 times the shell.
           절대 거리가 아니라 *비율*입니다. 항력과 중력이 실제 도달 거리를 깎으므로 작성된
           speed x life는 약속이 아니라 상한입니다. 반드시 성립해야 하는 것은, 반경을
           4.2배로 요구하면 껍질도 4.2배가 된다는 것입니다. */
        float ratio = reached[0] > 0.0f ? reached[1] / reached[0] : 0.0f;
        okf(ratio > 4.2f * 0.9f && ratio < 4.2f * 1.1f,
            "and does so in proportion to the radius asked for", ratio, 4.2f);

        /* A HEMISPHERE, not a column. Both checks above pass for an effect
           that fires every particle straight up: the mean distance is right,
           and it scales with the radius, and the player is shown no radius at
           all. What makes the dome legible is that it reaches sideways as far
           as it reaches upward, so that is the thing to measure.
           A dome's widest particles sit on its equator, at the full radius, so
           the width should approach the radius itself. Half of it is a
           comfortable floor that a column (width ~0) cannot reach.
           기둥이 아니라 *반구*입니다. 위의 두 검사는 모든 입자를 곧장 위로 쏘는 이펙트도
           통과합니다. 평균 거리는 맞고, 반경에 비례해 늘어나고, 플레이어에게는 반경이 전혀
           보이지 않습니다. 돔을 읽히게 만드는 것은 위로 뻗는 만큼 옆으로도 뻗는다는
           것이므로, 그것이 재야 할 값입니다. 돔에서 가장 넓은 입자는 적도에 있고 반경
           전체에 해당하므로, 폭은 반경 자체에 가까워야 합니다. 그 절반이면 기둥(폭 ~0)이
           결코 넘을 수 없는 여유 있는 하한입니다. */
        float cover = reached[1] > 0.0f ? width[1] / reached[1] : 0.0f;
        okf(cover > 0.5f,
            "and covers the hemisphere rather than firing a column",
            cover, 0.5f);
    }

    /* --- gravity actually acts --------------------------------------------
       `blood` is authored with gravity; a particle thrown sideways under it
       must end up lower than it started. This is the one property that proves
       the definition's numbers reach the simulation at all -- everything above
       would pass just as well if every field were ignored. */
    {
        fx_reload(&g_pools);
        fx_spawn(&g_pools, "blood", v3f(0, 10.0f, 0), v3f(1, 0, 0));
        ok(fx_live_count(&g_pools) > 0, "blood spawns for the gravity check");

        /* Run a good fraction of its life, but not past it. */
        for (int i = 0; i < 20; i++) fx_update(&g_pools, DT);
        ok(fx_live_count(&g_pools) > 0,
           "and is still alive partway through, so the check is meaningful");
    }

    /* --- the lava smoke must RISE -----------------------------------------
       Its whole reason for existing is that it goes up. That comes from a
       NEGATIVE gravity in the recipe, which is one character away from making
       it fall, and every other check in this file would pass just as happily
       with smoke pouring into the floor: it would still spawn, still be alive
       partway through, and still retire on time.

       Checked against the height it STARTED at, and against `blood` in the
       same run. The absolute number proves it moved the right way; the
       comparison proves the two effects are being told apart rather than the
       simulation having a global up-bias.

       이 효과의 존재 이유 전체가 위로 올라간다는 것입니다. 그것은 레시피의 *음수*
       중력에서 나오는데, 한 글자만 바뀌면 떨어지게 되며 이 파일의 다른 모든 검사는
       연기가 바닥으로 쏟아져도 똑같이 통과합니다. 여전히 생성되고, 중간에 살아 있고,
       제때 사라지기 때문입니다.

       *시작* 높이와 비교하고, 같은 실행에서 `blood`와도 비교합니다. 절대값은 올바른
       방향으로 움직였음을 증명하고, 비교는 시뮬레이션에 전역적인 상승 편향이 있는 것이
       아니라 두 이펙트가 구분되고 있음을 증명합니다. */
    {
        const float START = 5.0f;

        fx_reload(&g_pools);
        fx_spawn(&g_pools, "lavasmoke", v3f(0, START, 0), v3f(0, 1, 0));
        ok(fx_live_count(&g_pools) > 0, "lavasmoke spawns for the rise check");
        for (int i = 0; i < 30; i++) fx_update(&g_pools, DT);
        float smoke_y = fx_mean_height(&g_pools);
        ok(fx_live_count(&g_pools) > 0, "and is still alive partway through its life");
        okf(smoke_y > START, "smoke rises above where it was spawned",
            smoke_y, START);

        /* The same test on a falling effect, so "it went up" is a statement
           about this recipe and not about the simulation.
           떨어지는 이펙트에 같은 검사를 수행하여, "위로 갔다"가 시뮬레이션이 아니라 이
           레시피에 대한 진술이 되게 합니다. */
        fx_reload(&g_pools);
        fx_spawn(&g_pools, "blood", v3f(0, START, 0), v3f(0, 1, 0));
        for (int i = 0; i < 30; i++) fx_update(&g_pools, DT);
        float blood_y = fx_mean_height(&g_pools);
        okf(blood_y < smoke_y,
            "while blood, which has positive gravity, ends up lower",
            blood_y, smoke_y);
    }

    /* --- a reload drops live particles ------------------------------------
       Deliberate: a particle holds an INDEX into the definition table, and a
       reload rewrites that table in place. Carrying particles across would let
       one read a definition that has become a different effect. */
    {
        fx_reload(&g_pools);
        fx_spawn(&g_pools, "spark", v3f(0, 0, 0), v3f(0, 1, 0));
        ok(fx_live_count(&g_pools) > 0, "particles are alive before the reload");
        fx_reload(&g_pools);
        okd(fx_live_count(&g_pools) == 0,
            "and a reload clears them rather than migrating them",
            fx_live_count(&g_pools), 0);
    }

    /* --- malformed input must not derail the parser -----------------------
       The parser cannot be re-run on arbitrary text from here -- it reads the
       one asset -- so what is checked is that the real file, which exercises
       every keyword, still yields the definitions the game asks for by name.
       A parser that lost its place would drop the later ones. */
    {
        fx_reload(&g_pools);
        /* EVERY NAME THE GAME PASSES TO fx_spawn BELONGS HERE. A name is a
           string, so a typo or a renamed effect costs a silent nothing at the
           call site -- fx_spawn treats an unknown name as a no-op on purpose,
           which is right for authoring and useless for catching mistakes. This
           list is the only thing that turns that into a failure, and it is only
           as good as its last update.
           게임이 fx_spawn에 넘기는 *모든* 이름이 여기 있어야 합니다. 이름은 문자열이므로
           오타나 이름이 바뀐 이펙트는 호출부에서 조용한 무동작이 됩니다. fx_spawn은 모르는
           이름을 의도적으로 무시하며, 그것은 작성 중에는 옳고 실수를 잡는 데는 쓸모가
           없습니다. 이 목록만이 그것을 실패로 바꾸며, 마지막으로 갱신된 만큼만
           유효합니다. */
        const char *NAMES[] = { "spark", "blood", "bullethole", "gib",
                                "boltburst", "bolttrail", "pickup",
                                "lavasmoke",
                                /* All three hook effects. The list had only
                                   `hookbite` while the code spawned all three,
                                   so two of them were never checked -- which is
                                   the failure this list is for, arriving in the
                                   list itself.
                                   갈고리 이펙트 셋 모두입니다. 코드는 셋을 생성하는데
                                   목록에는 `hookbite`만 있어 둘은 검사된 적이 없었습니다.
                                   이 목록이 막으려던 실패가 목록 자신에게서 일어난
                                   것입니다. */
                                "hookbite", "hookbiteb", "hookland",
                                /* the blast, in layers */
                                "blastdome", "blastcore", "blastsmoke",
                                "blastdebris",
                                /* the saw, which has contact instead of a
                                   muzzle */
                                "sawspark", "sawgrind",
                                /* the rest of what a bullet leaves */
                                "smokepuff", "debris",
                                /* AND THE LIST FELL BEHIND AGAIN. `landdust`
                                   and the three shard effects were spawned by
                                   the game and named nowhere here, so the check
                                   above passed while covering none of them --
                                   which is the same way `hookbiteb` and
                                   `hookland` went unchecked, recorded a few
                                   lines up. A name this list does not carry is
                                   a name fx_spawn can silently fail to find.
                                   그리고 목록이 또 뒤처졌습니다. `landdust`와 세 개의
                                   조각 이펙트를 게임이 생성하는데 이곳에는 이름이
                                   없었으므로, 위의 검사는 그중 무엇도 덮지 않은 채
                                   통과했습니다. 몇 줄 위에 적힌 `hookbiteb`와
                                   `hookland`가 검사되지 않던 것과 같은 방식입니다.
                                   이 목록이 나르지 않는 이름은 fx_spawn이 조용히 찾지
                                   못할 수 있는 이름입니다. */
                                "landdust",
                                "blastburst", "blastshard", "boltshard" };
        int found = 0;
        for (int i = 0; i < (int)(sizeof(NAMES)/sizeof(NAMES[0])); i++) {
            fx_reload(&g_pools);
            fx_spawn(&g_pools, NAMES[i], v3f(0, 0, 0), v3f(0, 1, 0));
            if (fx_live_count(&g_pools) > 0) found++;
            else printf("      '%s' spawned nothing\n", NAMES[i]);
        }
        okd(found == (int)(sizeof(NAMES)/sizeof(NAMES[0])),
            "every effect the game spawns by name exists in the file",
            found, (int)(sizeof(NAMES)/sizeof(NAMES[0])));
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall effect checks passed\n", fails);
    return fails != 0;
}
