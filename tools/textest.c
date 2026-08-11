/* textest -- the material cache, and the ownership rule it enforces.
 *
 * Like posttest, this NEEDS a GL context: tex_mat uploads a texture on a cache
 * miss, and whether a texture handle was really deleted is a question only the
 * driver can answer. So it creates a hidden window and a real context and asks
 * the driver directly, with glIsTexture.
 *
 * What it actually catches: a material that cannot be cached leaking its
 * texture. tex_flush only ever walks the cache, so a handle that never reached
 * the cache is unreachable for the rest of the process -- and the path that
 * produces one runs on every level load and every hot reload, so it
 * accumulates. Nothing about it is visible while it happens: the material
 * still draws correctly, because the Mat returned is perfectly usable. Only
 * the caching is lost.
 *
 * That is the same shape as every other fault diag.h exists for, which is why
 * the counter is asserted here too. A counter nobody increments reads as "no
 * problem" forever.
 *
 * posttest와 마찬가지로 이 테스트는 GL 컨텍스트를 *필요로 합니다*. tex_mat은 캐시
 * 미스 시 텍스처를 업로드하며, 텍스처 핸들이 실제로 삭제되었는지는 드라이버만이 답할
 * 수 있는 질문입니다. 따라서 숨겨진 창과 실제 컨텍스트를 만들고 glIsTexture로 드라이버에
 * 직접 확인합니다.
 *
 * 실제로 잡아내는 것: 캐시할 수 없는 재질이 자신의 텍스처를 누수시키는 상황입니다.
 * tex_flush는 캐시만 순회하므로, 캐시에 도달하지 못한 핸들은 프로세스가 끝날 때까지
 * 회수할 수 없습니다. 그리고 그런 핸들을 만들어 내는 경로는 레벨 로드와 핫 리로드마다
 * 실행되므로 누적됩니다. 진행되는 동안 눈에 보이는 것은 전혀 없습니다. 반환된 Mat이
 * 완전히 사용 가능하므로 재질은 여전히 올바르게 그려지며, 오직 캐싱만 사라집니다.
 */

#include <stdio.h>

#include "gl.h"
#include "tex.h"
#include "render.h"
#include "level.h"   /* LVL_MAT -- the authoring limit the cache must cover */
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

int main(void) {
    printf("textest\n\n");

    HINSTANCE inst = GetModuleHandleA(0);
    if (!gl_bootstrap(inst)) { printf("  gl_bootstrap FAILED\n"); return 1; }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = inst;
    wc.lpszClassName = "textest";
    RegisterClassA(&wc);

    HWND  w  = CreateWindowExA(0, "textest", "", 0, 0, 0, 320, 180, 0, 0, inst, 0);
    HDC   dc = GetDC(w);
    HGLRC rc = gl_make_context(dc);
    if (!rc) { printf("  gl_make_context FAILED\n"); return 1; }

    rd_init();   /* tex_use sets uniforms on the renderer's program */

    /* --- the cache must cover what a level can author ----------------------
       A name longer than the cache's field would be truncated on store but
       compared in full on lookup, so it could never match its own entry. The
       static assert in weapon.c makes that unreachable; this states the same
       bound where a reader of the cache will see it. */
    ok(TEX_NAME_MAX >= LVL_MAT,
       "the cache holds any material name a level can author");

    /* --- a normal material is cached, and the cache is what owns it -------- */
    {
        Mat a = tex_mat("brick");
        ok(a.tex != 0 || a.proc != 0, "a known material resolves to something usable");

        /* Same name twice must be the SAME texture, not a second upload.
           This is the whole point of the cache, and a miss here would mean
           every draw call rebuilt its material from the recipe. */
        Mat b = tex_mat("brick");
        okd(a.tex == b.tex && a.proc == b.proc,
            "and asking again returns the cached one rather than rebuilding",
            (int)b.tex, (int)a.tex);
    }

    /* --- an unknown name is a fallback, not a crash and not a leak --------- */
    {
        int before = diag_count(DIAG_TEX_CACHE);
        Mat u = tex_mat("no-such-material-here");
        ok(u.tex == 0, "an unknown name yields no texture rather than a stray one");
        okd(diag_count(DIAG_TEX_CACHE) == before,
            "and is not counted as a cache overflow -- there was nothing to cache",
            diag_count(DIAG_TEX_CACHE) - before, 0);
    }

    /* --- THE POINT: filling the cache must not leak -------------------------
       Everything above would pass just as well with the leak still present.
       This fills the cache past capacity with distinct real materials and then
       asks the DRIVER whether the last texture handed back is still alive
       after a flush.

       Before the fix, tex_mat generated a texture, failed to store it because
       the cache was full, and returned the live handle anyway. tex_flush walks
       only the cache, so that handle survived the flush -- which is exactly
       what glIsTexture reports below.

       The recipe names are real ones from assets/textures.txt: a made-up name
       resolves to no texture at all and so could never demonstrate the leak. */
    {
        static const char *NAMES[] = {
            "brick", "metal", "blued", "gunmetal", "steel", "walnut", "grip",
            "pbrick", "ptile", "ppanel", "pwood", "phex", "pmarble", "prust",
            "pgrid", "rope"
        };
        const int n = (int)(sizeof(NAMES) / sizeof(NAMES[0]));

        int before = diag_count(DIAG_TEX_CACHE);

        /* Ask every distinct material for a texture, watching each call for
           the one that overflows. The handle must come from THAT call: a
           cached material is legitimately still alive, so sampling the last
           texture seen would assert against a texture the cache rightly owns
           and fail for the wrong reason. */
        int    overflowed = 0;
        GLuint from_overflow = 0;
        for (int i = 0; i < n; i++) {
            int prev = diag_count(DIAG_TEX_CACHE);
            Mat m = tex_mat(NAMES[i]);
            if (diag_count(DIAG_TEX_CACHE) > prev) {
                overflowed = 1;
                from_overflow = m.tex;   /* must be 0: the handle was reclaimed */
            }
        }

        /* Which outcome is correct depends on how MAX_CACHED compares to n,
           and BOTH are checked -- build.ps1 runs this file twice, once at the
           shipped capacity and once with it forced small. Without the second
           run the reclaim path never executes here, and a branch that only
           runs when something has gone wrong is the last one that may go
           untested. */
        if (overflowed) {
            /* The material must still be usable, but must NOT carry a live
               handle: tex_flush only walks the cache, so a handle returned
               from here would be unreachable forever. This is the regression
               the whole fix is about. */
            ok(from_overflow == 0 || glIsTexture(from_overflow) == GL_FALSE,
               "an uncacheable material hands back no live texture to leak");
        } else {
            okd(diag_count(DIAG_TEX_CACHE) - before == 0,
                "every distinct material fits -- the cache cannot overflow",
                n, n);
        }

        /* Whatever happened above, a flush must leave nothing behind that the
           cache was supposed to own. */
        Mat keep = tex_mat("brick");
        GLuint owned = keep.tex;
        tex_flush();
        ok(owned == 0 || glIsTexture(owned) == GL_FALSE,
           "tex_flush deletes every texture the cache owned");
    }

    /* --- the cache survives a flush and refills ---------------------------- */
    {
        Mat again = tex_mat("brick");
        ok(again.tex != 0 || again.proc != 0,
           "and the next lookup rebuilds rather than returning a dead handle");
    }

    /* No GL error may be left for whatever draws next. */
    {
        GLenum e = glGetError();
        ok(e == GL_NO_ERROR, "the whole exercise raises no GL error");
        if (e != GL_NO_ERROR) printf("      glGetError = 0x%04X\n", e);
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall texture cache checks passed\n", fails);
    return fails != 0;
}
