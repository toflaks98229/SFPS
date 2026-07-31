/* diagtest -- the capacity-overflow counters, and the truncation they expose.
 *
 * Two things are being checked, and the second matters more than the first.
 *
 * 1. The counters themselves: report, read back, name, summarise, and refuse
 *    to walk off the end of their own tables on a bad index.
 *
 * 2. That the report sites are actually WIRED UP. A counter nobody increments
 *    is worse than no counter at all -- it reads as "no problem" forever. So
 *    this drives a real MeshBuf past its capacity and asserts the count moves.
 *    That is the case this whole module exists for: mb_vtx silently drops
 *    vertices when full, which shows up as a hole in the world and nothing
 *    else.
 *
 * Built only in dev/tool builds. In release DIAG() compiles to ((void)0) and
 * there is nothing here to test -- which is itself verified, by checking the
 * linker map for diag.o's contribution rather than from inside a test.
 */

#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "render.h"

static int fails;
static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void okd(int cond, const char *what, int got, int want) {
    printf("  %-58s %6d / %6d  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

int main(void) {
    printf("diagtest\n\n");

    /* --- counting ---------------------------------------------------------
       Counters accumulate and never reset, because an overflow during level
       load is long over by the time anyone reads the HUD. */
    {
        int before = diag_count(DIAG_TEX_OPS);
        diag_report(DIAG_TEX_OPS);
        diag_report(DIAG_TEX_OPS);
        okd(diag_count(DIAG_TEX_OPS) == before + 2,
            "reports accumulate rather than overwrite",
            diag_count(DIAG_TEX_OPS), before + 2);

        ok(diag_count(DIAG_MESH_POINTS) == 0,
           "and are independent -- one counter does not move another");
    }

    /* --- bad indices must not read or write out of bounds ------------------
       DIAG_COUNT itself is the plausible mistake: it looks like a valid
       member of the enum and is exactly one past the end. */
    {
        diag_report(DIAG_COUNT);              /* must be ignored, not a write */
        diag_report((DiagKind)-1);
        diag_report((DiagKind)9999);
        ok(diag_count(DIAG_COUNT) == 0, "an out-of-range report is ignored");
        ok(diag_count((DiagKind)-1) == 0, "and a negative one too");

        /* Never NULL: a HUD path must not crash on a bad index. */
        ok(diag_name(DIAG_COUNT) != 0 && diag_name((DiagKind)-1) != 0,
           "diag_name never returns NULL, however bad the index");
    }

    /* --- every counter has a name -----------------------------------------
       The name table is indexed directly by DiagKind, so a short table would
       read past its end. A _Static_assert catches a wrong LENGTH at compile
       time; this catches an empty or duplicated ENTRY, which it cannot. */
    {
        int all_named = 1, all_distinct = 1;
        for (int i = 0; i < DIAG_COUNT; i++) {
            const char *a = diag_name((DiagKind)i);
            if (!a || !*a) all_named = 0;
            for (int j = i + 1; j < DIAG_COUNT; j++)
                if (strcmp(a, diag_name((DiagKind)j)) == 0) all_distinct = 0;
        }
        ok(all_named, "every counter has a non-empty name");
        ok(all_distinct, "and no two counters share one, so the HUD is readable");
    }

    /* --- the summary string ------------------------------------------------ */
    {
        char buf[128];

        /* Something is already counted from the tests above, so this reports. */
        int any = diag_summary(buf, sizeof(buf));
        ok(any == 1, "diag_summary reports when a counter is non-zero");
        ok(strstr(buf, "texop") != 0, "and names the counter that fired");

        /* Truncation must still terminate. One byte can hold only the
           terminator itself. */
        char tiny[2] = { 'X', 'X' };
        diag_summary(tiny, 1);
        ok(tiny[0] == 0, "a 1-byte buffer receives just the terminator");

        char small[8];
        memset(small, 'X', sizeof(small));
        diag_summary(small, sizeof(small));
        ok(small[sizeof(small) - 1] == 0 || strlen(small) < sizeof(small),
           "a too-small buffer truncates instead of overflowing");

        /* Defensive: must not write through a null destination. */
        ok(diag_summary(0, 64) == 0, "a null buffer is refused, not dereferenced");
        ok(diag_summary(buf, 0) == 0, "and a zero capacity too");
    }

    /* --- THE POINT: the report site in mb_vtx is really wired up ------------
       Everything above tests the counter in isolation, which would pass just
       as well if no caller ever invoked it. This drives a real MeshBuf past
       capacity through the real mb_vtx and asserts the count moves.

       mb_init/mb_vtx touch no GL, so this runs with no context -- the same
       property that lets hooktest check ribbon geometry headlessly. */
    {
        MeshBuf b;
        mb_init(&b, 4);                       /* room for exactly 4 vertices */

        int before = diag_count(DIAG_VERTEX_BUF);
        v3 p = v3f(0, 0, 0), n = v3f(0, 1, 0);

        for (int i = 0; i < 4; i++) mb_vtx(&b, p, n, 0.0f, 0.0f);
        okd(b.count == 4, "a MeshBuf accepts vertices up to its capacity",
            b.count, 4);
        okd(diag_count(DIAG_VERTEX_BUF) == before,
            "and reports nothing while it still has room",
            diag_count(DIAG_VERTEX_BUF) - before, 0);

        /* Now overflow it. */
        for (int i = 0; i < 10; i++) mb_vtx(&b, p, n, 0.0f, 0.0f);
        okd(b.count == 4, "a full buffer drops vertices rather than growing",
            b.count, 4);
        okd(diag_count(DIAG_VERTEX_BUF) == before + 10,
            "and every dropped vertex is counted -- the silent hole is now visible",
            diag_count(DIAG_VERTEX_BUF) - before, 10);

        /* And the summary picks it up, which is what reaches the HUD. */
        char buf[128];
        diag_summary(buf, sizeof(buf));
        ok(strstr(buf, "vtx") != 0, "and it appears in the HUD summary");

        mb_free(&b);
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall diagnostics checks passed\n", fails);
    return fails != 0;
}
