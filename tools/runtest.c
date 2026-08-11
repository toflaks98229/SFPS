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
#include "run.h"

static int fails;

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

    printf("\n%s\n", fails ? "FAILURES" : "all run checks passed");
    return fails ? 1 : 0;
}
