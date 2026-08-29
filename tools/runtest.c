/* runtest -- check that a restart puts back everything a run owns.
 *
 * This is the test the RunState refactor was for. The rules are trivial to
 * state and were impossible to check: the state lived beside WinMain, which a
 * tool cannot link, so the one state machine in the project with three
 * different ways into it was the one nobody could assert anything about.
 *
 * What actually went wrong before, and what these assertions pin down: the
 * restart path assigned three fields BY NAME, and the run owned more than
 * three. The title and world clocks and the lava timers were never put back.
 * Nothing visible broke -- which is exactly why it survived, and exactly the
 * kind of thing that stops being harmless the moment a field is added that is
 * not a sub-second timer.
 *
 * 재시작이 플레이가 소유한 모든 것을 되돌리는지 검사합니다. RunState 리팩토링이 가능하게
 * 만든 테스트입니다. 규칙 자체는 간단하지만 검사할 수 없었습니다. 상태가 WinMain 옆에
 * 있었고 도구는 그것을 링크할 수 없으므로, 진입로가 셋이나 되는 이 프로젝트의 유일한
 * 상태 머신이 정작 아무것도 단언할 수 없는 대상이었습니다.
 */

#include <stdio.h>
#include <string.h>   /* strcmp -- the summary is compared as a whole line */
#include "run.h"

static int fails;

/* The summary for a run with these three numbers on it, so a case reads as the
   line it expects rather than as four assignments and a call.
   이 세 숫자를 가진 플레이의 요약입니다. 그래야 한 사례가 대입 네 번과 호출 하나가 아니라
   기대하는 줄 그 자체로 읽힙니다. */
static void summary_of(char *out, int cap, int kills, float alive, int wave_best) {
    RunState r;
    run_reset(&r, 0);
    r.kills      = kills;
    r.alive_time = alive;
    r.wave_best  = wave_best;
    run_summary(&r, out, cap);
}

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Fill every field with something a fresh run must not have, so a field the
   reset forgets shows up as itself rather than as a zero that happened to be
   right. Deliberately NOT a memset: the point is that each field is named here
   once, so adding one to RunState without adding it here leaves the new field
   untouched and the "nothing survives" check below still passes -- which is why
   that check is written against a byte scan instead.
   모든 필드를 새 플레이가 가져서는 안 될 값으로 채웁니다. 그래야 초기화가 빠뜨린 필드가,
   우연히 맞은 0이 아니라 그 값 자체로 드러납니다. 일부러 memset을 쓰지 않습니다. */
static void dirty(RunState *r) {
    r->won            = 1;
    r->dead           = 1;
    r->title          = 1;
    r->restart_wanted = 1;
    r->death_time     = 12.5f;
    r->title_time     = 34.5f;
    r->world_time     = 200.0f;
    r->hazard_accum   = 0.75f;
    r->burn_timer     = 0.19f;
    r->smoke_timer    = 0.09f;
    r->smoke_rng      = 0xdeadbeefu;
    r->kills          = 41;
    r->alive_time     = 903.5f;
    r->wave_best      = 7;
}

/* One whole summary line, compared against the line it should be. Printed
   either way, because a wrong clock is far easier to understand from the two
   strings side by side than from "FAIL".
   요약 한 줄 전체를, 마땅히 그러해야 할 줄과 비교합니다. 어느 쪽이든 출력하는 이유는, 틀린
   시계는 "FAIL"보다 두 문자열을 나란히 놓고 볼 때 훨씬 이해하기 쉽기 때문입니다. */
static void oks(const char *got, const char *want, const char *what) {
    int good = strcmp(got, want) == 0;
    printf("  %-42s %-24s %s\n", what, got, good ? "ok" : "FAIL");
    if (!good) { printf("      wanted: %s\n", want); fails++; }
}

int main(void) {
    printf("runtest\n\n");

    /* --- a restart clears the run ---------------------------------------
       title=0, because a restart is the player asking to play again rather
       than asking to see the title. */
    {
        RunState r;
        dirty(&r);
        run_reset(&r, 0);

        ok(r.won == 0,            "a restart clears the win latch");
        ok(r.dead == 0,           "and the death latch");
        ok(r.title == 0,          "and goes straight into the run, not the title");
        ok(r.restart_wanted == 0, "and consumes the restart request itself");
        ok(r.death_time == 0.0f,  "the death clock restarts");
        ok(r.title_time == 0.0f,  "the title clock restarts");
        ok(r.world_time == 0.0f,  "and so does the clock the lava flows against");
        ok(r.hazard_accum == 0.0f,"no hazard damage is carried into the new run");
        ok(r.burn_timer == 0.0f,  "the burn sound timer restarts");
        ok(r.smoke_timer == 0.0f, "and the smoke timer");

        /* The score, which is the whole reason a death screen has anything to
           say. A restart that kept either of these would credit the new run
           with the previous one's kills -- and the first thing anybody does
           after a bad run is restart.
           성적이며, 사망 화면이 말할 거리를 갖는 이유 전부입니다. 어느 하나라도 남기는
           재시작은 새 플레이에 이전 플레이의 처치를 얹습니다. 그리고 나쁜 플레이 뒤에 누구나
           가장 먼저 하는 일이 재시작입니다. */
        ok(r.kills == 0,          "the kill count starts the new run at zero");
        ok(r.alive_time == 0.0f,  "and so does the survival clock");
    }

    /* --- startup shows the title ----------------------------------------
       The one field that is not simply zeroed, and the only difference
       between the two entry points. */
    {
        RunState r;
        dirty(&r);
        run_reset(&r, 1);
        ok(r.title == 1, "startup comes up on the title screen");
        ok(r.won == 0 && r.dead == 0 && r.death_time == 0.0f,
           "and is otherwise identical to a restart");
    }

    /* --- the smoke rng is seeded, not zeroed -----------------------------
       A multiplicative congruential generator started at zero is not stuck,
       but it is a different sequence from the one a fresh start uses, so a
       restarted run would produce a different plume from a new one. Asserted
       against the seed rather than against "non-zero" so that changing the
       constant without meaning to fails here.
       0에서 시작한 곱셈 합동 생성기는 멈추지는 않지만 새로 시작할 때와 다른 수열이므로,
       재시작한 플레이가 새 플레이와 다른 연기를 만들게 됩니다. */
    {
        RunState a, b;
        dirty(&a);
        run_reset(&a, 1);      /* a fresh start */
        dirty(&b);
        run_reset(&b, 0);      /* a restart */

        ok(a.smoke_rng == SMOKE_RNG_SEED, "a fresh start seeds the smoke rng");
        ok(b.smoke_rng == SMOKE_RNG_SEED, "and a restart seeds it identically");
        ok(a.smoke_rng != 0u,             "so neither begins from a zero state");
    }

    /* --- nothing at all survives a reset ---------------------------------
       The assertion that actually holds the refactor to its promise. The
       field-by-field checks above only cover the fields somebody remembered to
       list, and forgetting to list one is precisely the failure this struct
       exists to prevent -- so this compares raw bytes instead and needs no
       maintenance when a field is added.

       Two dirty patterns rather than one: a single pattern cannot tell "the
       reset cleared this" from "the reset ignored it and the byte happened to
       match". Padding is written by the same struct assignment in run_reset, so
       it is deterministic and safe to compare.

       리팩토링의 약속을 실제로 책임지는 단언입니다. 위의 필드별 검사는 누군가 나열한
       필드만 다루는데, 나열을 빠뜨리는 것이야말로 이 구조체가 막으려는 실패입니다. 그래서
       이 검사는 원시 바이트를 비교하며, 필드가 추가되어도 손댈 필요가 없습니다. */
    {
        RunState a, b;
        unsigned char *pa = (unsigned char *)&a, *pb = (unsigned char *)&b;
        int differ = 0;

        dirty(&a);
        for (unsigned i = 0; i < sizeof(RunState); i++) pb[i] = 0x5a;

        run_reset(&a, 0);
        run_reset(&b, 0);

        for (unsigned i = 0; i < sizeof(RunState); i++)
            if (pa[i] != pb[i]) differ++;

        ok(differ == 0,
           "two differently-dirtied runs reset to the identical state");
    }

    /* --- what a run says it was ------------------------------------------
       The wording is a rule about runs rather than about the screen that draws
       it, which is why it is reachable from here at all. What these pin down is
       the pad: 187 seconds is 3:07, and a version that writes 3:7 is wrong for
       a sixth of every minute and looks corrupt rather than merely incorrect.
       문구는 그것을 그리는 화면이 아니라 플레이에 대한 규칙이며, 그래서 이곳에서 닿을 수
       있습니다. 이 검사들이 고정하는 것은 0 채우기입니다. 187초는 3:07이고, 3:7로 쓰는 판은
       매 분의 6분의 1 동안 틀리며 부정확해 보이는 대신 깨져 보입니다. */
    printf("\n  --- what a run says it was ---\n");
    {
        char line[RUN_SUMMARY_MAX];

        summary_of(line, sizeof(line), 12, 221.0f, 0);
        oks(line, "kills 12   time 3:41", "kills and a clock, on a plain level");

        summary_of(line, sizeof(line), 0, 0.0f, 0);
        oks(line, "kills 0   time 0:00", "a run that ended before it began");

        summary_of(line, sizeof(line), 3, 187.0f, 0);
        oks(line, "kills 3   time 3:07", "seconds under ten are padded, not bare");

        summary_of(line, sizeof(line), 1, 59.999f, 0);
        oks(line, "kills 1   time 0:59", "the last instant of a minute is that minute");

        summary_of(line, sizeof(line), 1, 60.0f, 0);
        oks(line, "kills 1   time 1:00", "and the next one rolls the minute over");

        /* Minutes keep counting rather than becoming hours. A survival time is
           compared against other survival times, and 74:20 sorts against 3:41
           by eye while 1:14:20 does not.
           분은 시간이 되지 않고 계속 늘어납니다. 생존 시간은 다른 생존 시간과 비교되며,
           74:20은 3:41과 눈으로 비교되지만 1:14:20은 그렇지 않습니다. */
        summary_of(line, sizeof(line), 88, 4460.0f, 0);
        oks(line, "kills 88   time 74:20", "an hour-long run grows no hours field");

        /* The wave is reported only when there was an arena to reach one in.
           웨이브는 그것에 도달할 아레나가 있었을 때만 보고됩니다. */
        summary_of(line, sizeof(line), 30, 125.0f, 5);
        oks(line, "kills 30   time 2:05   wave 5", "an arena run reports its best wave");

        summary_of(line, sizeof(line), 30, 125.0f, 0);
        ok(!strstr(line, "wave"),
           "a level with no waves does not report wave 0");

        /* A short buffer truncates and still terminates rather than writing
           past the end of it. Every append in txt.h promises this; run_summary
           chains six of them and the promise has to survive the chain.
           짧은 버퍼는 끝을 넘어 쓰지 않고 잘리면서도 종료 문자를 남깁니다. txt.h의 모든
           덧붙이기가 이것을 약속하며, run_summary는 그것을 여섯 번 잇습니다. 약속은 그 연쇄를
           견뎌야 합니다. */
        {
            char small[8];
            for (int i = 0; i < 8; i++) small[i] = 1;
            RunState r;
            run_reset(&r, 0);
            r.kills = 1234567;
            int n = run_summary(&r, small, (int)sizeof(small));
            ok(n <= 7 && small[7] == 0,
               "a buffer too short truncates and stays a string");
        }
    }

    printf("\n%s\n", fails ? "FAILURES" : "all run checks passed");
    return fails ? 1 : 0;
}
