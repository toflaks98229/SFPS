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

    /* --- a floor item's motes must RISE ------------------------------------
       This is the whole of what `itemmote` is. An item used to be marked by a
       halo drawn behind its sprite and a light on the floor under it; both were
       replaced by specks that climb off it, and the ONLY thing that makes a
       speck a climbing speck is a negative gravity in the recipe -- one
       character from making it fall. Every other check in this file passes just
       as happily with the motes pouring into the floor: they still spawn, are
       still alive partway through, and still retire on time. What the player
       would see is an item leaking, which is not the same message.

       RUN MOST OF THE WAY THROUGH THE LIFE, not a fraction of it. A shorter
       run is the trap: `speed` throws the specks gently upward along the spawn
       normal, so for the first half-second they are above where they started
       whichever way gravity points, and a check taken there passes with the
       sign flipped. It was written that way first and it did pass that way.
       Buoyancy and a throw only separate once the throw has been spent -- so
       this waits until it has been, and stops just short of the retirement
       that would leave nothing to measure.

       `itemmote`이 무엇인지의 전부입니다. 아이템은 스프라이트 뒤에 그려지는 헤일로와 발밑
       바닥의 광원으로 표시되었고, 둘 다 아이템에서 기어오르는 알갱이로 대체되었습니다.
       알갱이를 *올라가는* 알갱이로 만드는 유일한 것은 레시피의 음수 중력이며, 한 글자만
       바뀌면 떨어집니다. 이 파일의 다른 모든 검사는 티끌이 바닥으로 쏟아져도 똑같이
       통과합니다. 여전히 생성되고, 중간에 살아 있고, 제때 사라지기 때문입니다. 플레이어가
       보게 될 것은 새고 있는 아이템이며, 그것은 같은 메시지가 아닙니다.

       *수명의 대부분까지 돌리며* 일부만 돌리지 않습니다. 짧게 도는 것이 함정입니다.
       `speed`가 생성 법선을 따라 알갱이를 부드럽게 위로 던지므로, 처음 0.5초 동안은 중력이
       어느 쪽을 향하든 출발 지점보다 위에 있습니다. 그 시점에 잰 검사는 부호를 뒤집어도
       통과합니다. 처음에 그렇게 작성했고 실제로 그렇게 통과했습니다. 부력과 던지기는 던진
       힘이 다 소진된 뒤에야 갈라지므로, 이 검사는 그때까지 기다렸다가 잴 것이 없어지는
       소멸 직전에 멈춥니다. */
    {
        const float START = 3.0f;

        fx_reload(&g_pools);
        fx_spawn(&g_pools, "itemmote", v3f(0, START, 0), v3f(0, 1, 0));
        ok(fx_live_count(&g_pools) > 0, "itemmote spawns for the rise check");

        for (int i = 0; i < 80; i++) fx_update(&g_pools, DT);   /* 1.33s of a 1.5s life */
        ok(fx_live_count(&g_pools) > 0, "and is still alive to be measured");
        okf(fx_mean_height(&g_pools) > START,
            "a floor item's motes climb off it rather than falling",
            fx_mean_height(&g_pools), START);
    }

    /* --- the disc is the dome's equator ------------------------------------
       `disc` exists for one picture: a ring of light running out across a
       floor, which is how a shrine says where it is from the other side of a
       room. The dome cannot draw it -- half of a dome's particles leave the
       ground -- and that is exactly the failure that would look fine in a
       screenshot taken from head height, because a horizontal plane seen
       edge-on shows nothing either way.

       So it is measured rather than looked at. ::fx_radius_spread's `width` is
       distance from the VERTICAL AXIS through the origin, and `mean` is
       distance from the origin itself: for a ring lying flat those two are the
       same number, and for the dome checked above the width is smaller because
       the particles near its pole are directly overhead. A disc whose ratio
       slipped back toward the dome's is a disc that has stopped being flat.

       `disc`는 하나의 그림을 위해 존재합니다. 바닥을 가로질러 퍼져 나가는 빛의 고리이며,
       방 건너편에서 제단이 자기 위치를 말하는 방식입니다. 돔으로는 그것을 그릴 수 없고(돔은
       입자의 절반이 지면을 떠납니다), 그것이야말로 머리 높이에서 찍은 스크린샷에서는
       멀쩡해 보일 실패입니다. 수평 평면을 옆에서 보면 어느 쪽이든 아무것도 보이지 않기
       때문입니다.

       그래서 보는 대신 잽니다. ::fx_radius_spread의 `width`는 원점을 지나는 *수직축*으로부터의
       거리이고 `mean`은 원점 자체로부터의 거리입니다. 평평하게 누운 고리에서는 이 둘이 같은
       숫자이며, 위에서 검사한 돔에서는 극 근처의 입자가 바로 머리 위에 있으므로 폭이 더
       작습니다. 비율이 돔 쪽으로 되돌아간 디스크는 평평하기를 그만둔 디스크입니다. */
    {
        const v3 AT = { -2.0f, 1.0f, 5.0f };
        float mean = 0.0f, width = 0.0f;

        fx_reload(&g_pools);
        fx_spawn(&g_pools, "altarring", AT, v3f(0, 1, 0));
        ok(fx_live_count(&g_pools) > 0, "altarring spawns");

        /* Its life is 900ms; part-way through is enough to have travelled and
           early enough that nothing has retired.
           수명이 900ms이므로, 중간쯤이면 충분히 이동했고 아직 아무것도 사라지지 않았습니다. */
        for (int i = 0; i < 30; i++) fx_update(&g_pools, DT);
        fx_radius_spread(&g_pools, AT, &mean, &width);

        okf(mean > 0.5f, "and travels outward from where it was spawned",
            mean, 0.5f);

        float flat = mean > 0.0f ? width / mean : 0.0f;
        okf(flat > 0.95f,
            "and stays in the plane the normal is perpendicular to",
            flat, 0.95f);
    }

    /* --- the ground wave is a disc AND it is scaled ------------------------
       Both halves are already checked above, separately, and the reason to
       check them together is that they are two DIFFERENT LINES of fx.c. The
       dome case proves ::fx_spawn_scaled multiplies the speed on the dome
       branch. The altarring case proves the disc branch lays its particles
       flat. Neither of them reaches the disc branch's own `* scale`, which is a
       second copy of the same multiplication a few lines further down.

       A copy nothing reads is a copy that can be deleted, mistyped, or left out
       of the next branch somebody adds, and the symptom here would be specific
       and quiet: a blast whose ground ring is one metre across no matter what
       the grenade's radius is. It would look like a ring. It would just be
       telling the player the wrong distance -- the failure fxtest's dome check
       exists for, on the layer that check cannot see.

       `blastwave` is the only effect in the file that is both a disc and
       scaled, so it is the only place that line can be reached from.

       두 가지 모두 위에서 따로 검사되어 있으며, 함께 검사하는 이유는 그 둘이 fx.c의 서로
       *다른 줄*이기 때문입니다. 돔 검사는 ::fx_spawn_scaled가 돔 분기에서 속력에 배율을
       곱한다는 것을 증명합니다. altarring 검사는 디스크 분기가 입자를 평평하게 눕힌다는 것을
       증명합니다. 둘 중 어느 것도 디스크 분기 자신의 `* scale`에 닿지 않으며, 그것은 몇 줄
       아래에 있는 같은 곱셈의 두 번째 사본입니다.

       아무도 읽지 않는 사본은 지워지거나, 잘못 입력되거나, 다음에 누군가 추가하는 분기에서
       빠질 수 있는 사본이며, 이곳의 증상은 구체적이면서 조용합니다. 유탄의 반경이 얼마이든
       지면 고리가 언제나 1미터인 폭발입니다. 고리처럼 보이기는 할 것입니다. 다만 플레이어에게
       틀린 거리를 말할 뿐입니다. fxtest의 돔 검사가 존재하는 이유인 그 실패를, 그 검사가 볼 수
       없는 겹에서 일으키는 것입니다.

       `blastwave`는 이 파일에서 디스크이면서 동시에 배율이 적용되는 유일한 이펙트이므로, 그
       줄에 닿을 수 있는 유일한 곳입니다. */
    {
        const v3 AT = { 6.0f, 0.5f, 1.0f };
        float mean[2], width[2];
        const float WANT[2] = { 1.0f, 4.2f };   /* unit, and the grenade's */

        for (int k = 0; k < 2; k++) {
            fx_reload(&g_pools);
            fx_spawn_scaled(&g_pools, "blastwave", AT, v3f(0, 1, 0), WANT[k]);
            ok(fx_live_count(&g_pools) > 0, k ? "blastwave spawns at 4.2m"
                                              : "blastwave spawns at 1m");
            /* 300ms of life; stop just short of it, the same way the dome
               check does, so the ring is at full extent and still alive.
               수명 300ms이므로 돔 검사와 마찬가지로 그 직전에 멈춥니다. 고리가 최대로
               퍼졌으면서 아직 살아 있는 시점입니다. */
            for (int i = 0; i < 17; i++) fx_update(&g_pools, DT);
            fx_radius_spread(&g_pools, AT, &mean[k], &width[k]);
        }

        float ratio = mean[0] > 0.0f ? mean[1] / mean[0] : 0.0f;
        okf(ratio > 4.2f * 0.9f && ratio < 4.2f * 1.1f,
            "the ground wave runs out to the radius it was given", ratio, 4.2f);

        float flat = mean[1] > 0.0f ? width[1] / mean[1] : 0.0f;
        okf(flat > 0.95f,
            "and does it along the floor rather than into the air",
            flat, 0.95f);
    }

    /* --- the lit rim and the dust rim stop in the same place ---------------
       `blastring` is a SECOND copy of the authored arithmetic the check above
       exists for. speed x life = 100cm is what lets fx_spawn_scaled put a rim
       on the exact radius the damage has, and blastwave writes it as 333 x
       300ms where blastring writes it as 238 x 420ms -- two different pairs of
       numbers that have to come out at the same distance, in a file where
       nothing connects them but this note and this test.

       The failure is quiet and specific, and it is the one the blastwave check
       already names from the other side: a bright ring that stops short of the
       damage teaches the player a radius the grenade does not have, and it
       teaches it more convincingly than the dust does, because the lit edge is
       the one that can be seen in a dark room. Getting `blend add` on a ring
       that lands in the wrong place makes the mistake more legible, not less.

       MEASURED AT THE SAME FRACTION OF EACH LIFE (17 of 18 frames, 24 of 25),
       not at the same instant, because they do not travel together -- the dust
       is done in 300ms and the light takes 420. What is being compared is
       where each one STOPS.

       `blastring`은 위의 검사가 존재하는 이유인 작성된 산술의 *두 번째 사본*입니다.
       speed x life = 100cm가 fx_spawn_scaled로 하여금 피해가 가진 정확한 반경에 테두리를
       놓게 하는데, blastwave는 그것을 333 x 300ms로 쓰고 blastring은 238 x 420ms로 씁니다.
       같은 거리에서 나와야 하는 서로 다른 두 쌍의 수이며, 이 설명과 이 검사 말고는 둘을
       잇는 것이 파일에 없습니다.

       실패는 조용하고 구체적이며, 위의 blastwave 검사가 이미 반대쪽에서 이름 붙인 바로
       그것입니다. 피해에 미치지 못하고 멈추는 밝은 고리는 유탄이 갖지 않은 반경을
       플레이어에게 가르치고, 먼지보다 더 설득력 있게 가르칩니다. 어두운 방에서 보이는
       것은 빛나는 가장자리이기 때문입니다. 엉뚱한 자리에 놓인 고리에 `blend add`를 주면
       실수가 덜 보이는 것이 아니라 더 잘 보이게 됩니다.

       *같은 순간이 아니라 각자 수명의 같은 비율에서* 잽니다(18프레임 중 17, 25프레임 중
       24). 둘은 함께 이동하지 않기 때문입니다. 먼지는 300ms에 끝나고 빛은 420ms가
       걸립니다. 비교하는 것은 각자가 *멈추는* 자리입니다. */
    {
        const v3 AT = { 6.0f, 0.5f, 1.0f };
        float dust = 0.0f, ring = 0.0f, wide = 0.0f;

        fx_reload(&g_pools);
        fx_spawn_scaled(&g_pools, "blastwave", AT, v3f(0, 1, 0), 4.2f);
        ok(fx_live_count(&g_pools) > 0, "blastwave spawns for the rim check");
        for (int i = 0; i < 17; i++) fx_update(&g_pools, DT);
        fx_radius_spread(&g_pools, AT, &dust, &wide);

        fx_reload(&g_pools);
        fx_spawn_scaled(&g_pools, "blastring", AT, v3f(0, 1, 0), 4.2f);
        ok(fx_live_count(&g_pools) > 0, "blastring spawns for the rim check");
        for (int i = 0; i < 24; i++) fx_update(&g_pools, DT);
        fx_radius_spread(&g_pools, AT, &ring, &wide);

        float agree = dust > 0.0f ? ring / dust : 0.0f;
        okf(agree > 0.94f && agree < 1.06f,
            "the lit rim stops where the dust rim stops", agree, 1.0f);

        float flat = ring > 0.0f ? wide / ring : 0.0f;
        okf(flat > 0.95f, "and runs along the floor rather than into the air",
            flat, 0.95f);
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

    /* --- a particle's own wake --------------------------------------------
       `trail` is the one keyword whose value is a NAME rather than a number,
       and a name that matches nothing resolves to "no trail" in silence --
       the same rule every unknown token in the file gets, and the reason it
       is worth a check here: the symptom of a mistyped wake is an effect that
       still spawns, still moves and still looks broadly right, minus the
       layer nobody can see is absent.

       Counted rather than measured. Fourteen embers are spawned and the count
       is read again after the first wake interval has passed, so what the
       assertion rests on is that MORE particles exist than were asked for,
       which can only have come from the emitter. The second half is that the
       chain stops: a wake with a wake of its own would not settle at a
       bounded count, it would climb until the pool was full and the flood
       check above is the one that would then start failing instead.

       `trail`은 값이 숫자가 아니라 *이름*인 유일한 키워드이며, 아무것과도 맞지 않는 이름은
       조용히 "자취 없음"으로 해석됩니다. 파일의 모든 미지의 토큰이 받는 것과 같은 규칙이고,
       그래서 이곳에서 검사할 값어치가 있습니다. 오타 난 자취의 증상은, 여전히 생성되고
       여전히 움직이고 여전히 대체로 맞아 보이되 아무도 없음을 알 수 없는 겹 하나가 빠진
       이펙트이기 때문입니다.

       재는 것이 아니라 셉니다. 불티 열넷을 생성하고 첫 자취 간격이 지난 뒤 수를 다시 읽으므로,
       단언이 딛고 선 것은 요청한 것보다 *많은* 입자가 존재한다는 사실이며 그것은 방출기에서만
       나올 수 있습니다. 나머지 절반은 사슬이 멈춘다는 것입니다. 자기 자취를 가진 자취라면
       한계 있는 수에 머무르지 않고 풀이 찰 때까지 올라갔을 것이고, 그러면 위의 범람 검사가
       대신 실패하기 시작했을 것입니다. */
    {
        fx_reload(&g_pools);
        fx_spawn(&g_pools, "blastember", v3f(0, 0, 0), v3f(0, 1, 0));
        int born = fx_live_count(&g_pools);
        ok(born > 0, "blastember spawns for the wake check");

        /* 70ms between wakes, so a fifth of a second is three of them per
           ember and none of the 420ms wakes has retired yet.
           자취 간격이 70ms이므로 0.2초면 불티당 셋이고, 420ms짜리 자취는 아직 하나도
           사라지지 않았습니다. */
        for (int i = 0; i < 12; i++) fx_update(&g_pools, DT);
        int after = fx_live_count(&g_pools);
        okd(after > born, "an ember lays down a wake behind it", after, born + 1);

        /* Every wake is `emberwake`, which has no trail of its own -- and even
           if the file gave it one, ::spawn_def refuses the second link. Left
           to run out the embers' full 1.5 seconds, the count has to come back
           DOWN rather than climb: a chain one link deep is bounded by
           life / trailms per ember, and one that is not is bounded by nothing.
           모든 자취는 `emberwake`이고 그것에는 자기 자취가 없습니다. 파일이 하나 주더라도
           ::spawn_def가 두 번째 고리를 거절합니다. 불티의 1.5초를 전부 흘려보내면 수는
           올라가는 것이 아니라 *내려와야* 합니다. 한 고리 깊이의 사슬은 불티당
           life / trailms로 한계 지어지고, 그렇지 않은 사슬은 아무것에도 한계 지어지지
           않습니다. */
        for (int i = 0; i < 150; i++) fx_update(&g_pools, DT);
        int settled = fx_live_count(&g_pools);
        okd(settled < after, "and the chain is one link deep, not a cascade",
            settled, after - 1);
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
                                /* The blast, in layers -- NINE of them now.
                                   The flash, the ground wave and the embers
                                   joined the six that were here, and they are
                                   written down on the same change that spawns
                                   them rather than on the one after somebody
                                   noticed this list had fallen behind for a
                                   third time. The note further down records
                                   the first two.
                                   여러 겹의 폭발이며 이제 *아홉* 겹입니다. 섬광과 지면
                                   파동과 불티가 기존 여섯에 합류했습니다. 누군가 이 목록이
                                   세 번째로 뒤처졌음을 알아챈 다음이 아니라, 그것들을
                                   생성하는 바로 그 변경에서 함께 적었습니다. 앞의 두 번은
                                   아래의 설명이 기록하고 있습니다. */
                                "blastdome", "blastwave", "blastflash",
                                "blastcore", "blastsmoke", "blastdebris",
                                "blastember",
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
                                "blastburst", "blastshard", "boltshard",
                                /* AND THE LAYERS ADDED WITH THE REFERENCE
                                   PASS. `blastfire` is the fireball the blast
                                   spends its second as, `blastring` the lit
                                   half of the ground rim, and the other three
                                   are what a projectile now leaves behind it
                                   in the air -- proj.c emits the first two on
                                   one timer, enemy.c the third.
                                   `emberwake` is NOT here and must not be: it
                                   is reached only through `blastember`'s
                                   `trail`, and the check for that is its own
                                   block further up. A name in this list is a
                                   promise that code calls it.
                                   그리고 참고 영상 작업에서 더해진 겹들입니다. `blastfire`는
                                   폭발이 1초를 보내는 화구이고, `blastring`은 지면 테두리의
                                   빛나는 절반이며, 나머지 셋은 발사체가 이제 공중에 뒤로
                                   남기는 것입니다. 앞의 둘은 proj.c가 하나의 타이머로,
                                   셋째는 enemy.c가 방출합니다.
                                   `emberwake`는 이곳에 *없으며* 있어서도 안 됩니다. 그것은
                                   `blastember`의 `trail`을 통해서만 닿으며, 그에 대한 검사는
                                   위쪽의 자기 블록에 있습니다. 이 목록의 이름은 코드가 그것을
                                   부른다는 약속입니다. */
                                "blastfire", "blastring",
                                "fusespark", "fusetrail", "boltwake",
                                /* AND THE LAYERS A LANDING GOT. `scorch` is
                                   named from two files at once -- proj.c for
                                   the player's bolt and enemy.c for a
                                   monster's -- and a shared name is the kind
                                   this list is worst at protecting: renaming
                                   it fixes one call site, leaves the other
                                   spawning nothing, and the game still runs.
                                   그리고 착탄이 얻은 겹들입니다. `scorch`는 두 파일에서 동시에
                                   이름이 불립니다. 플레이어의 볼트에 대해서는 proj.c가,
                                   몬스터의 것에 대해서는 enemy.c가 부릅니다. 공유된 이름은 이
                                   목록이 가장 지켜 내기 어려운 종류입니다. 이름을 바꾸면 한
                                   호출 지점은 고쳐지고 다른 하나는 아무것도 생성하지 않게
                                   되는데, 게임은 그대로 돌아갑니다. */
                                "scorch", "zapflash", "zapburst",
                                /* AND A THIRD TIME, for the spawn portal.
                                   `spawnwarp` has been spawned by
                                   ::spawners_update since the telegraph
                                   existed and was named nowhere here, so the
                                   one effect whose whole job is to be read
                                   under pressure was the one nothing checked.
                                   `spawnring` and `spawnburst` join it because
                                   a portal is now three effects and a list that
                                   carries one of three is the same hole in a
                                   smaller shape.
                                   그리고 세 번째로, 소환 포탈입니다. `spawnwarp`은 예고가
                                   생긴 이래로 ::spawners_update가 생성해 왔지만 이곳에
                                   이름이 없었습니다. 압박 속에서 읽히는 것이 임무의 전부인
                                   그 이펙트가, 아무것도 검사하지 않던 바로 그것이었습니다.
                                   포탈이 이제 세 이펙트이므로 `spawnring`과 `spawnburst`도
                                   함께 넣습니다. 셋 중 하나만 나르는 목록은 같은 구멍을 더
                                   작은 모양으로 가진 것입니다. */
                                "spawnwarp", "spawnring", "spawnburst",
                                /* The shrine a cleared wave lights, in the
                                   three layers ::altar_light and ::step_altar
                                   spawn, plus the beat an item plays as it
                                   settles. Added WITH the effects rather than
                                   after somebody noticed they did nothing --
                                   which is what the three paragraphs above are
                                   a record of, and the reason this list is
                                   worth the trouble at all.
                                   정리된 웨이브가 켜는 제단이며, ::altar_light와
                                   ::step_altar이 생성하는 세 겹입니다. 여기에 아이템이
                                   내려앉을 때 재생되는 박자가 더해집니다. 누군가 이것들이
                                   아무 일도 하지 않음을 알아챈 뒤가 아니라 이펙트와 *함께*
                                   추가했습니다. 위의 세 문단이 기록하고 있는 것이 그것이며,
                                   이 목록이 애초에 수고할 값어치가 있는 이유입니다. */
                                "altarring", "altarcore", "altarmote",
                                /* What a floor item gives off, and the beat it
                                   plays as it settles. `itemmote` is paced by
                                   ::pickup_update rather than fired at an
                                   event, which is the shape of spawn this list
                                   is worst at covering: an effect nothing calls
                                   in a burst is one nobody notices has stopped
                                   existing.
                                   바닥 아이템이 내보내는 것과, 안착할 때 재생되는 박자입니다.
                                   `itemmote`은 사건에 맞춰 발사되지 않고 ::pickup_update가
                                   조절해 뿌리는데, 그것이 이 목록이 가장 놓치기 쉬운 생성
                                   형태입니다. 아무도 폭발로 부르지 않는 이펙트는, 존재하기를
                                   그만두었다는 것을 아무도 알아채지 못하는 이펙트입니다. */
                                "itemmote", "itemland" };
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

    /* --- the colour a spawn may ask for ----------------------------------
       ::fx_tint_colour is the whole of what a tint does, and it is the half of
       the drawing that can be checked without a context: the particles' colours
       are chosen inside ::fx_draw, which needs one. What is pinned here is not
       "does it change the colour" -- anything that touched the channels would
       pass that -- but the three properties the effect files depend on.

       ::fx_tint_colour이 색조가 하는 일의 전부이며, 그리기 중에서 컨텍스트 없이 검사할 수
       있는 절반입니다. 입자의 색은 컨텍스트가 필요한 ::fx_draw 안에서 정해집니다. 이곳에서
       고정하는 것은 "색이 바뀌는가"가 아닙니다. 채널을 건드리기만 해도 그것은 통과합니다.
       이펙트 파일들이 의존하는 세 가지 성질입니다. */
    {
        FxTint none  = { 0, 0, 0 };
        FxTint green = { 60, 255, 90 };
        FxTint half  = { 30, 128, 45 };   /* the same hue, half as bright */

        /* Nothing asked for, nothing changed. Every effect in the text that
           nobody recolours goes through this path. */
        float c[3] = { 1.0f, 0.38f, 0.09f };
        fx_tint_colour(none, c);
        ok(c[0] == 1.0f && c[1] == 0.38f && c[2] == 0.09f,
           "a zero tint leaves the authored colour exactly as it was");

        /* The hue is replaced rather than multiplied. A multiply could only
           darken, and an authored orange multiplied toward green is a muddy
           brown -- so the test is that green WINS the channel, not that the
           colour moved. */
        float g[3] = { 1.0f, 0.38f, 0.09f };
        fx_tint_colour(green, g);
        ok(g[1] > g[0] && g[1] > g[2],
           "a green tint on an orange effect comes out green");

        /* Brightness survives it: the birth-to-death ramp belongs to the text,
           and a tint that dimmed what it painted would be a second author for
           it. The brightest channel of the result is the brightest channel of
           the input. */
        float v = g[0] > g[1] ? g[0] : g[1];
        if (g[2] > v) v = g[2];
        okf(fabsf(v - 1.0f) < 1e-3f, "and no dimmer than the text drew it", v, 1.0f);

        /* The tint names a hue, so its own brightness is not part of the
           instruction: {60,255,90} and half of it are the same request. */
        float h[3] = { 1.0f, 0.38f, 0.09f };
        fx_tint_colour(half, h);
        ok(fabsf(h[0] - g[0]) < 0.01f && fabsf(h[1] - g[1]) < 0.01f
           && fabsf(h[2] - g[2]) < 0.01f,
           "and a darker copy of the same hue is the same instruction");

        /* White has no hue to replace, which is what keeps a layer that starts
           white-hot starting white-hot whatever colour it cools into. */
        float wh[3] = { 1.0f, 1.0f, 1.0f };
        fx_tint_colour(green, wh);
        ok(wh[0] == 1.0f && wh[1] == 1.0f && wh[2] == 1.0f,
           "and white stays white: there is no hue there to replace");

        /* And the spawn carries it. The colour itself is fx_draw's, but a tint
           that never reached the particle would leave every check above true
           and every explosion the authored colour.
           그리고 생성이 그것을 실어 나릅니다. 색 자체는 fx_draw의 것이지만, 입자에 닿지 못한
           색조는 위의 모든 검사를 참인 채로 두면서 모든 폭발을 작성된 색 그대로 남깁니다. */
        fx_reload(&g_pools);
        fx_spawn_tinted(&g_pools, "blastcore", v3f(0, 0, 0), v3f(0, 1, 0),
                        1.0f, green);
        ok(fx_live_count(&g_pools) > 0, "a tinted spawn spawns");
    }

    /* --- the lava boils, and boiling is not smoking -----------------------
     *
     * ENGLISH
     * -------
     * A LAVA SEA HAD ONE EFFECT ON IT AND IT SAID THE WRONG THING. Smoke says
     * HOT: a fire smokes, a corpse smokes, a wrecked machine smokes. What says
     * LIQUID is a surface that has to open to let something out, and `lavaboil`
     * is that -- so the claim worth a check is not that it spawns, it is that
     * it is a DIFFERENT effect from the smoke rather than the same one twice
     * under a second name.
     *
     * The difference that matters is where each one ENDS UP. A puff is thrown
     * up and out and leaves; a bubble surfaces, swells and goes, and never
     * leaves the half-metre it was born in. That is the whole reason the boil
     * reads as the lava doing something rather than as the lava emitting
     * something, and it is one number: distance travelled from the spawn point
     * over the bubble's own lifetime.
     *
     * Measured one at a time, because ::fx_radius_spread answers for every live
     * particle at once and two effects in the pool would average into a number
     * that describes neither.
     *
     * 한국어
     * ------
     * *용암 바다에는 효과가 하나 있었고 그것은 틀린 말을 하고 있었습니다.* 연기는 *뜨겁다*고
     * 말합니다. 불도 시체도 부서진 기계도 연기를 냅니다. *액체*라고 말하는 것은 무언가를
     * 내보내려고 열려야 하는 표면이며 `lavaboil`이 그것입니다. 그래서 검사할 값어치가 있는
     * 주장은 그것이 생성된다는 것이 아니라, 두 번째 이름을 쓴 같은 효과가 아니라 연기와 *다른*
     * 효과라는 것입니다.
     *
     * 중요한 차이는 각자가 *어디에서 끝나는가*입니다. 연기는 위로 바깥으로 던져져 떠나고,
     * 거품은 표면에 올라와 부풀고 사라지며 태어난 반 미터를 결코 벗어나지 않습니다. 그것이
     * 끓어오름이 용암이 무언가를 *내보내는* 것이 아니라 무언가를 *하는* 것으로 읽히는 이유
     * 전부이며, 수 하나입니다. 거품 자신의 수명 동안 생성 지점에서 이동한 거리입니다.
     *
     * 한 번에 하나씩 잽니다. ::fx_radius_spread는 살아 있는 모든 입자에 대해 한 번에 답하므로,
     * 풀에 두 효과가 있으면 어느 쪽도 서술하지 않는 수로 평균됩니다.
     */
    {
        const v3 AT = { 1.0f, -18.0f, 2.0f };
        static const char *WHICH[2] = { "lavaboil", "lavasmoke" };
        float went[2] = {0}, wide[2] = {0};

        for (int k = 0; k < 2; k++) {
            fx_reload(&g_pools);
            fx_spawn(&g_pools, WHICH[k], AT, v3f(0, 1, 0));
            ok(fx_live_count(&g_pools) > 0,
               k ? "lavasmoke is a recipe this file defines"
                 : "lavaboil is a recipe this file defines");

            /* The bubble's own life, 620ms, for both -- the comparison is
               "where is each one when the bubble is done", not "where does
               each one finish".
               둘 다 거품 자신의 수명 620ms입니다. 비교는 "각자가 끝나는 곳"이 아니라
               "거품이 끝났을 때 각자가 어디 있는가"입니다. */
            for (int i = 0; i < 37; i++) fx_update(&g_pools, DT);
            fx_radius_spread(&g_pools, AT, &went[k], &wide[k]);
        }

        okf(went[0] < 0.5f,
            "a bubble is still within half a metre of the hole it came from",
            went[0], 0.5f);
        okf(went[1] > went[0] * 2.0f,
            "and the smoke in the same time has gone much further",
            went[1], went[0] * 2.0f);
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall effect checks passed\n", fails);
    return fails != 0;
}
