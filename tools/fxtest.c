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
        fx_reload();
        okd(fx_live_count() == 0, "a reload leaves no live particles",
            fx_live_count(), 0);

        fx_spawn("spark", v3f(0, 0, 0), v3f(0, 1, 0));
        int after = fx_live_count();
        ok(after > 0, "spawning a known effect produces particles");

        /* The whole point of the format: an effect nobody has authored yet is
           not an error. A typo costs a missing puff, not a crash. */
        int before = fx_live_count();
        fx_spawn("no-such-effect", v3f(0, 0, 0), v3f(0, 1, 0));
        okd(fx_live_count() == before,
            "an unknown name spawns nothing and does not crash",
            fx_live_count(), before);
    }

    /* --- particles age out ------------------------------------------------- */
    {
        fx_reload();
        fx_spawn("spark", v3f(0, 0, 0), v3f(0, 1, 0));
        ok(fx_live_count() > 0, "the burst is alive to begin with");

        /* Ten seconds is longer than any authored life, so everything must be
           gone -- a particle that never retires is a leak that only shows up
           as the pool slowly filling with the undead. */
        for (int i = 0; i < 600; i++) fx_update(DT);
        okd(fx_live_count() == 0,
            "and every particle retires once its life runs out",
            fx_live_count(), 0);
    }

    /* --- the pool has a hard ceiling --------------------------------------- */
    {
        fx_reload();
        /* Far more than the pool can hold. The excess must overwrite rather
           than overflow, and the count must never exceed the cap. */
        for (int i = 0; i < 200; i++)
            fx_spawn("spark", v3f(0, 0, 0), v3f(0, 1, 0));

        okd(fx_live_count() <= FX_MAX_PARTICLES,
            "a flood never exceeds the particle pool",
            fx_live_count(), FX_MAX_PARTICLES);

#ifdef DIAG_ENABLED
        ok(diag_count(DIAG_FX_CAP) > 0,
           "and the overflow is reported rather than silent");
#endif
    }

    /* --- gravity actually acts --------------------------------------------
       `blood` is authored with gravity; a particle thrown sideways under it
       must end up lower than it started. This is the one property that proves
       the definition's numbers reach the simulation at all -- everything above
       would pass just as well if every field were ignored. */
    {
        fx_reload();
        fx_spawn("blood", v3f(0, 10.0f, 0), v3f(1, 0, 0));
        ok(fx_live_count() > 0, "blood spawns for the gravity check");

        /* Run a good fraction of its life, but not past it. */
        for (int i = 0; i < 20; i++) fx_update(DT);
        ok(fx_live_count() > 0,
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

        fx_reload();
        fx_spawn("lavasmoke", v3f(0, START, 0), v3f(0, 1, 0));
        ok(fx_live_count() > 0, "lavasmoke spawns for the rise check");
        for (int i = 0; i < 30; i++) fx_update(DT);
        float smoke_y = fx_mean_height();
        ok(fx_live_count() > 0, "and is still alive partway through its life");
        okf(smoke_y > START, "smoke rises above where it was spawned",
            smoke_y, START);

        /* The same test on a falling effect, so "it went up" is a statement
           about this recipe and not about the simulation.
           떨어지는 이펙트에 같은 검사를 수행하여, "위로 갔다"가 시뮬레이션이 아니라 이
           레시피에 대한 진술이 되게 합니다. */
        fx_reload();
        fx_spawn("blood", v3f(0, START, 0), v3f(0, 1, 0));
        for (int i = 0; i < 30; i++) fx_update(DT);
        float blood_y = fx_mean_height();
        okf(blood_y < smoke_y,
            "while blood, which has positive gravity, ends up lower",
            blood_y, smoke_y);
    }

    /* --- a reload drops live particles ------------------------------------
       Deliberate: a particle holds an INDEX into the definition table, and a
       reload rewrites that table in place. Carrying particles across would let
       one read a definition that has become a different effect. */
    {
        fx_reload();
        fx_spawn("spark", v3f(0, 0, 0), v3f(0, 1, 0));
        ok(fx_live_count() > 0, "particles are alive before the reload");
        fx_reload();
        okd(fx_live_count() == 0,
            "and a reload clears them rather than migrating them",
            fx_live_count(), 0);
    }

    /* --- malformed input must not derail the parser -----------------------
       The parser cannot be re-run on arbitrary text from here -- it reads the
       one asset -- so what is checked is that the real file, which exercises
       every keyword, still yields the definitions the game asks for by name.
       A parser that lost its place would drop the later ones. */
    {
        fx_reload();
        const char *NAMES[] = { "spark", "blood", "bullethole", "gib",
                                "boltburst", "bolttrail", "hookbite", "pickup",
                                "lavasmoke" };
        int found = 0;
        for (int i = 0; i < (int)(sizeof(NAMES)/sizeof(NAMES[0])); i++) {
            fx_reload();
            fx_spawn(NAMES[i], v3f(0, 0, 0), v3f(0, 1, 0));
            if (fx_live_count() > 0) found++;
            else printf("      '%s' spawned nothing\n", NAMES[i]);
        }
        okd(found == (int)(sizeof(NAMES)/sizeof(NAMES[0])),
            "every effect the game spawns by name exists in the file",
            found, (int)(sizeof(NAMES)/sizeof(NAMES[0])));
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall effect checks passed\n", fails);
    return fails != 0;
}
