/* soaktest -- does a frame get slower the longer the game runs?
 *
 * ENGLISH
 * -------
 * A report that "it lags more the further you get" has no line number in it.
 * This runs main.c's frame loop -- the same calls in the same order, minus the
 * window and the message pump -- for as long as it takes, and prints what a
 * frame COSTS every so often beside the state that could explain a change.
 *
 * The two halves are timed separately because they fail for different reasons.
 * world_step is scalar work over fixed pools; scene_frame builds vertex buffers
 * and talks to a driver. A number that climbs on one side and not the other has
 * already halved the search.
 *
 * The state columns are the things that accumulate. Every pool here is fixed
 * capacity, so none of them can grow without bound -- but a count that CLIMBS
 * AND STAYS is still a loop that got longer, and enemy.count in particular
 * never comes down: a corpse keeps its slot.
 *
 * WHAT IT FOUND, on the run it was written for. Phases 1 and 2 came back flat:
 * two minutes on one level and a hundred level loads moved neither the frame
 * time nor the working set, and no cache reported an overflow. Phase 3, which
 * kills what spawns so the waves actually advance, reproduced it on the second
 * try: the monster pool filled with corpses on wave 3 and the wave counter
 * never moved again. See the note on the slot choice in enemy.c's
 * make_monster, and the one on a refused spawn in spawners_update.
 *
 * WHAT IT STILL CANNOT SEE. No audio device is opened, the viewport is small,
 * and the frame runs as fast as it will rather than against a vsync. A cost
 * that only appears at full resolution, or in the mixer, is not in these
 * numbers.
 *
 * 한국어
 * ------
 * "진행할수록 렉이 걸린다"는 보고에는 줄 번호가 없습니다. 이 도구는 main.c의 프레임 루프를
 * (창과 메시지 펌프만 빼고 같은 호출을 같은 순서로) 오래 돌리면서, 프레임이 얼마나
 * *비싼지*를 주기적으로 출력하고 그 변화를 설명할 수 있는 상태를 나란히 적습니다.
 *
 * 두 절반을 따로 재는 이유는 서로 다른 이유로 느려지기 때문입니다. world_step은 고정 풀에
 * 대한 스칼라 연산이고, scene_frame은 정점 버퍼를 만들어 드라이버와 이야기합니다. 한쪽만
 * 오르는 숫자는 이미 탐색 범위를 절반으로 줄여 줍니다.
 *
 * 상태 열은 누적되는 것들입니다. 이곳의 모든 풀은 고정 용량이므로 무한히 자랄 수 없습니다.
 * 그러나 *올라가서 내려오지 않는* 개수는 여전히 길어진 루프이며, 특히 enemy.count는 결코
 * 내려오지 않습니다. 시체가 자기 칸을 계속 차지합니다.
 *
 * 이것이 찾아낸 것(이 파일이 작성된 계기가 된 실행에서). 1단계와 2단계는 평탄하게
 * 돌아왔습니다. 한 레벨에서 2분, 레벨 로드 100회는 프레임 시간도 워킹셋도 움직이지 않았고
 * 어떤 캐시도 초과를 보고하지 않았습니다. 생성되는 것을 죽여 웨이브가 실제로 진행되게 하는
 * 3단계가 두 번째 시도에서 재현했습니다. 몬스터 풀이 웨이브 3에서 시체로 가득 찼고 웨이브
 * 계수기는 다시는 움직이지 않았습니다. enemy.c의 make_monster에 있는 칸 선택 설명과
 * spawners_update의 거절된 생성에 대한 설명을 참조하십시오.
 *
 * 그럼에도 볼 수 없는 것. 오디오 장치를 열지 않고, 뷰포트가 작으며, 프레임이 수직 동기를
 * 기다리지 않고 낼 수 있는 만큼 빠르게 돕니다. 풀 해상도에서만 나타나는 비용이나 믹서의
 * 비용은 이 숫자들에 없습니다.
 */
#include <stdio.h>
#include <stdlib.h>   /* atoi: the block count is a command-line argument */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "gl.h"
#include "wgl.h"
#include "render.h"
#include "post.h"
#include "scene.h"
#include "world.h"
#include "player.h"   /* PLAYER_MAX_HP: the soak keeps the player alive on purpose */
#include "run.h"      /* run_reset: an arena entered fresh, not after a death */
#include "enemy.h"
#include "proj.h"
#include "fx.h"
#include "font.h"
#include "decal.h"
#include "menu.h"
#include "audio.h"    /* audio_init: a restart plays sounds, and the mixer is the
                        one thread this file would otherwise never start */
#include "diag.h"

#define VW 640
#define VH 360
#define DT (1.0f / 60.0f)
#define BLOCK 1800            /* 30 seconds of game time per reported row */

static double g_freq;

/* The process's working set, in KB. Resolved at runtime from kernel32 rather
   than linked, so this tool needs no library the others do not have --
   K32GetProcessMemoryInfo has been exported there since Windows 7.
   A leak is the one explanation for "slower the longer it runs" that does not
   show up as a longer loop anywhere, so it is worth a column of its own.
   프로세스의 워킹셋(KB)입니다. 링크하지 않고 실행 시점에 kernel32에서 찾으므로, 이 도구는
   다른 도구에 없는 라이브러리를 필요로 하지 않습니다. K32GetProcessMemoryInfo는 Windows 7
   이후로 그곳에 내보내져 있습니다.
   누수는 "오래 돌수록 느려진다"에 대한 설명 중 어디에서도 길어진 루프로 드러나지 않는
   유일한 것이므로, 자기 열을 가질 값어치가 있습니다. */
typedef struct { DWORD cb; DWORD n; SIZE_T a,b,c,d,e,f,g,h,i; } PMC;
typedef BOOL (WINAPI *PFN_GPMI)(HANDLE, PMC *, DWORD);
static PFN_GPMI g_gpmi;
static int g_gpmi_tried;

static long working_kb(void) {
    if (!g_gpmi_tried) {
        g_gpmi_tried = 1;
        HMODULE k = GetModuleHandleA("kernel32.dll");
        if (k) g_gpmi = (PFN_GPMI)(void *)GetProcAddress(k, "K32GetProcessMemoryInfo");
    }
    if (!g_gpmi) return -1;
    PMC m; m.cb = sizeof(m);
    if (!g_gpmi(GetCurrentProcess(), &m, sizeof(m))) return -1;
    return (long)(m.b / 1024);          /* WorkingSetSize */
}

static double now_ms(void) {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / g_freq;
}

/* An input that keeps the game busy. A soak over an idle player measures an
   idle player.
   게임을 계속 바쁘게 만드는 입력입니다. 가만히 선 플레이어에 대한 소크는 가만히 선
   플레이어를 잴 뿐입니다. */
static Input driving(int frame) {
    Input in = (Input){0};
    in.look_dx = 0.010f;
    in.forward = ((frame / 240) % 2) == 0;
    in.back    = !in.forward;
    in.left    = ((frame / 137) % 3) == 0;
    in.fire    = (frame % 20) < 3;
    in.jump    = (frame % 300) == 0;
    return in;
}

static int live_enemies(const Pools *pl) {
    int n = 0;
    for (int i = 0; i < enemy_count(pl); i++) {
        const Enemy *m = enemy_at(pl, i);
        if (m && m->active && m->state != E_DEAD) n++;
    }
    return n;
}

/* One frame, exactly as main.c orders it, with the two halves timed. */
static void one_frame(World *w, Scene *sc, int frame, double *step_ms, double *draw_ms)
{
    Input in = driving(frame);

    /* Topped up every frame. A soak is about what accumulates over an hour and
       the player does not survive an hour of this, so without it the run ends
       at the death screen -- where ::world_frozen stops the world and every
       measurement below becomes a measurement of nothing happening.
       매 프레임 채웁니다. 소크는 한 시간 동안 무엇이 누적되는가에 대한 것이고 플레이어는 이
       한 시간을 견디지 못하므로, 이것이 없으면 실행이 사망 화면에서 끝납니다. 그곳에서는
       ::world_frozen이 월드를 멈추고, 아래의 모든 측정이 아무 일도 일어나지 않는 것에 대한
       측정이 됩니다. */
    w->player.health = PLAYER_MAX_HP;

    double t0 = now_ms();
    int frozen = world_step(w, &in, (float)VW / (float)VH, DT);
    double t1 = now_ms();

    int dynamic;
    switch (world_take_geometry_scope(w, &dynamic)) {
    case WORLD_GEOM_ALL:    scene_build_level(sc, &w->level, dynamic); break;
    case WORLD_GEOM_MOVING: scene_rebuild_moving(sc, &w->level);       break;
    case WORLD_GEOM_NONE:   break;
    }

    scene_frame(w, sc, VW, VH, frozen);
    glFinish();                 /* the driver's work belongs to this frame */
    double t2 = now_ms();

    *step_ms = t1 - t0;
    *draw_ms = t2 - t1;
}

static void header(void) {
    printf("\n  %8s %8s %8s %8s | %5s %5s %5s %5s | %8s | %s\n",
           "gametime", "frame/ms", "step/ms", "draw/ms",
           "mons", "live", "fx", "proj", "rss/KB", "diag deltas");
    printf("  %s\n",
           "--------------------------------------------------------------------------");
}

static void row(float t, double frame_ms, double step_ms, double draw_ms,
                const World *w, int d_vtx, int d_tex, int d_lc, int d_ecap)
{
    printf("  %7.0fs %8.3f %8.3f %8.3f | %5d %5d %5d %5d | %8ld |",
           t, frame_ms, step_ms, draw_ms,
           enemy_count(&w->pools), live_enemies(&w->pools),
           fx_live_count(&w->pools), proj_live(&w->pools), working_kb());
    if (d_vtx)  printf(" vtx+%d", d_vtx);
    if (d_tex)  printf(" texcache+%d", d_tex);
    if (d_lc)   printf(" lcache+%d", d_lc);
    if (d_ecap) printf(" enemycap+%d", d_ecap);
    printf("\n");
}

int main(int argc, char **argv) {
    int blocks = (argc > 1) ? atoi(argv[1]) : 40;      /* 40 x 30s = 20 minutes */
    if (blocks < 1) blocks = 1;

    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = (double)f.QuadPart;

    printf("soaktest -- %d blocks of %d frames (%.0f minutes of game time)\n",
           blocks, BLOCK, blocks * BLOCK * DT / 60.0f);

    HINSTANCE inst = GetModuleHandleA(0);
    if (!gl_bootstrap(inst)) { printf("  gl_bootstrap FAILED\n"); return 1; }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = inst;
    wc.lpszClassName = "soaktest";
    RegisterClassA(&wc);

    HWND  wnd = CreateWindowExA(0, "soaktest", "", WS_POPUP, 0, 0, VW, VH, 0, 0, inst, 0);
    HDC   dc  = GetDC(wnd);
    HGLRC rc  = gl_make_context(dc);
    if (!rc) { printf("  gl_make_context FAILED\n"); return 1; }

    /* A REAL DEVICE, because leaving it shut is how the first version of this
       file measured a game with no audio in it and called the result flat. The
       mixer is a second thread holding a lock the game thread takes on every
       sound; nothing else here exercises that. A machine without a device is
       reported rather than worked around -- the numbers below are then simply
       about less than they claim.
       실제 장치를 엽니다. 닫아 두는 것은 이 파일의 첫 판본이 오디오가 없는 게임을 재고
       그 결과를 평탄하다고 부른 방식이기 때문입니다. 믹서는 게임 스레드가 모든 소리마다
       잡는 락을 함께 쥔 두 번째 스레드이며, 이곳의 다른 무엇도 그것을 실행하지 않습니다.
       장치가 없는 기계는 우회하지 않고 보고합니다. 그러면 아래의 숫자들은 주장하는 것보다
       적은 것에 대한 숫자일 뿐입니다. */
    if (!audio_init()) printf("  (no audio device -- the mixer is not in these numbers)\n");

    rd_init();
    decal_init();
    menu_init(0);
    post_init(VW, VH);
    font_init();

    static World w;
    world_init(&w);

    Scene scene;
    scene_init(&scene, &w.weapon);
    if (!world_load_level(&w, w.cur_level, WORLD_ENTER_NEW)) {
        printf("  world_load_level FAILED\n");
        return 1;
    }
    w.run.title = 0;      /* past the title screen; the soak is about play */

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    printf("  level '%s'\n", w.cur_level);
    header();

    int frame = 0;
    double first_frame_ms = 0.0;
    double last_frame_ms  = 0.0;

    for (int b = 0; b < blocks; b++) {
        int v0 = diag_count(DIAG_VERTEX_BUF), t0d = diag_count(DIAG_TEX_CACHE);
        int l0 = diag_count(DIAG_LIGHT_CACHE), e0 = diag_count(DIAG_ENEMY_CAP);

        double step_total = 0, draw_total = 0;
        double wall0 = now_ms();
        for (int i = 0; i < BLOCK; i++, frame++) {
            double s, d;
            one_frame(&w, &scene, frame, &s, &d);
            step_total += s; draw_total += d;
        }
        double wall = now_ms() - wall0;

        double frame_ms = wall / BLOCK;
        if (b == 0) first_frame_ms = frame_ms;
        last_frame_ms = frame_ms;

        row(frame * DT, frame_ms, step_total / BLOCK, draw_total / BLOCK, &w,
            diag_count(DIAG_VERTEX_BUF) - v0, diag_count(DIAG_TEX_CACHE) - t0d,
            diag_count(DIAG_LIGHT_CACHE) - l0, diag_count(DIAG_ENEMY_CAP) - e0);
        fflush(stdout);
    }

    printf("\n  first block %.3f ms/frame, last block %.3f ms/frame  (%+.1f%%)\n",
           first_frame_ms, last_frame_ms,
           100.0 * (last_frame_ms - first_frame_ms) / first_frame_ms);

    /* --- phase 2: the same numbers across level transitions ---------------
       Phase 1 runs one level and can only see what accumulates INSIDE it. What
       survives a load is a different list, and a short one: the pools all reset
       -- enemy_spawn_level and pickup_spawn_level clear theirs first -- but the
       texture cache does not, because tex_flush is on the hot-reload path and
       nowhere else. Neither is anything the driver is holding.
       한 레벨 안에서 누적되는 것만 1단계가 볼 수 있습니다. 로드를 넘어 살아남는 것은 다른
       목록이며 짧습니다. 풀은 전부 초기화되지만(enemy_spawn_level과 pickup_spawn_level이
       먼저 비웁니다) 텍스처 캐시는 그렇지 않습니다. tex_flush가 핫 리로드 경로에만 있기
       때문입니다. 드라이버가 붙들고 있는 것도 마찬가지입니다. */
    {
        static const char *CHAIN[] = { "lqdm1", "vault", "dm03", "arena" };
        const int CHAIN_N = (int)(sizeof(CHAIN) / sizeof(CHAIN[0]));
        const int laps = 20, settle = 120;

        printf("\n  --- level transitions: %d loads, %d frames of play each ---\n",
               laps * CHAIN_N, settle);
        printf("\n  %4s %-8s %9s %9s %9s | %8s | %s\n",
               "load", "level", "load/ms", "build/ms", "frame/ms", "rss/KB",
               "diag deltas");
        printf("  %s\n",
               "------------------------------------------------------------------------");

        int loads = 0;
        double first_build = 0, last_build = 0;
        for (int lap = 0; lap < laps; lap++) {
            for (int c = 0; c < CHAIN_N; c++) {
                int t0d = diag_count(DIAG_TEX_CACHE);
                int l0  = diag_count(DIAG_LIGHT_CACHE);
                int v0  = diag_count(DIAG_VERTEX_BUF);

                double a = now_ms();
                if (!world_load_level(&w, CHAIN[c], WORLD_ENTER_CARRY)) continue;
                double b = now_ms();

                int dyn;
                world_take_geometry_scope(&w, &dyn);
                scene_build_level(&scene, &w.level, dyn);
                double d = now_ms();

                w.run.title = 0;
                double play = 0;
                for (int i = 0; i < settle; i++, frame++) {
                    double s, dd;
                    one_frame(&w, &scene, frame, &s, &dd);
                    play += s + dd;
                }

                loads++;
                double build_ms = d - b;
                if (loads == 1) first_build = build_ms;
                last_build = build_ms;

                /* Sampled rather than printed in full, but the samples include
                   the first two laps and the last: a cost that climbs in steps
                   -- a cache filling, say -- is invisible in an average and
                   obvious in a first-versus-last comparison.
                   전부가 아니라 표본을 출력하되, 표본에 처음 두 바퀴와 마지막 바퀴이
                   포함됩니다. 계단식으로 오르는 비용(예를 들어 채워지는 캐시)은 평균
                   안에서는 보이지 않고 처음과 마지막의 비교에서는 분명합니다. */
                if (lap < 2 || lap == laps - 1 || (lap % 5) == 0) {
                    printf("  %4d %-8s %9.2f %9.2f %9.3f | %8ld |",
                           loads, CHAIN[c], b - a, build_ms, play / settle,
                           working_kb());
                    int dt2 = diag_count(DIAG_TEX_CACHE) - t0d;
                    int dl2 = diag_count(DIAG_LIGHT_CACHE) - l0;
                    int dv2 = diag_count(DIAG_VERTEX_BUF) - v0;
                    if (dt2) printf(" texcache+%d", dt2);
                    if (dl2) printf(" lcache+%d", dl2);
                    if (dv2) printf(" vtx+%d", dv2);
                    printf("\n");
                    fflush(stdout);
                }
            }
        }
        printf("\n  first build %.2f ms, last build %.2f ms  (%+.1f%%)\n",
               first_build, last_build,
               100.0 * (last_build - first_build) / (first_build ? first_build : 1.0));
    }

    /* --- phase 3: the arena, with the monsters actually dying --------------
       ENGLISH: phases 1 and 2 never killed anything, and that turns out to
       matter more than either of them. A monster that dies keeps its slot --
       enemy.count only ever rises, and enemy_reset is on the level-load path
       and nowhere else -- so a wave mode spends ENEMY_MAX on corpses. What
       happens after that is what this measures.

       The player is not simulated; the monsters are killed directly through
       enemy_hurt. Aiming is steptest's and weapontest's subject, and a soak
       that had to shoot straight would be measuring the aim.

       한국어: 1단계와 2단계는 아무것도 죽이지 않았고, 그 사실이 둘 중 어느 것보다도 중요한
       것으로 드러났습니다. 죽은 몬스터는 자기 칸을 계속 차지합니다. enemy.count는 오르기만
       하고 enemy_reset은 레벨 로드 경로에만 있으므로, 웨이브 모드는 ENEMY_MAX를 시체에
       씁니다. 그 뒤에 무슨 일이 벌어지는지를 이것이 잽니다.

       플레이어를 시뮬레이션하지 않고 enemy_hurt로 몬스터를 직접 죽입니다. 조준은 steptest와
       weapontest의 주제이며, 똑바로 쏘아야 하는 소크는 조준을 재게 됩니다. */
    {
        printf("\n  --- the arena, killing what spawns ---\n");

        world_load_level(&w, WORLD_START_LEVEL, WORLD_ENTER_NEW);
        w.run.title = 0;
        /* A DEAD PLAYER FREEZES THE WORLD, and a frozen world is what the
           first version of this file measured without noticing: world_step
           gates step_wave, step_damage and the monsters on !frozen, so every
           number went flat because nothing was running. The counters looked
           like a system in a steady state and were a system that had stopped.
           run_reset puts the run back to before any of that -- wave 0, not
           dead, not between levels -- which is what entering an arena fresh
           actually looks like.
           죽은 플레이어는 월드를 정지시키며, 이 파일의 첫 판본이 알아채지 못한 채 잰 것이
           바로 정지된 월드였습니다. world_step은 step_wave와 step_damage와 몬스터를
           !frozen으로 막으므로, 아무것도 돌지 않아서 모든 숫자가 평탄했습니다. 카운터는
           정상 상태의 시스템처럼 보였지만 실은 멈춘 시스템이었습니다. run_reset은 플레이를
           그 이전으로 되돌립니다. 웨이브 0, 죽지 않음, 레벨 사이 아님이며, 그것이 아레나에
           새로 들어가는 실제 모습입니다. */
        run_reset(&w.run, 0);
        {
            int dyn; world_take_geometry_scope(&w, &dyn);
            scene_build_level(&scene, &w.level, dyn);
        }

        printf("  level '%s', %d spawners, %d level-placed monsters\n",
               w.cur_level, enemy_spawner_count(&w.pools), enemy_count(&w.pools));

        printf("\n  %8s %5s %6s %6s %6s %6s | %9s %8s | %s\n",
               "gametime", "wave", "slots", "live", "dead", "fx",
               "frame/ms", "rss/KB", "diag deltas");
        printf("  %s\n",
               "-----------------------------------------------------------------------");

        int af = 0;
        double first_ms = 0, last_ms = 0;
        for (int b = 0; b < 30; b++) {                 /* 30 x 30s = 15 minutes */
            int e0 = diag_count(DIAG_ENEMY_CAP);
            int x0 = diag_count(DIAG_FX_CAP);

            double wall0 = now_ms();
            for (int i = 0; i < BLOCK; i++, af++, frame++) {
                double s, d;
                one_frame(&w, &scene, frame, &s, &d);

                /* Kill one live monster every few frames: fast enough to clear
                   a wave, slow enough that the room is never empty on the frame
                   a spawner looks at it.
                   몇 프레임마다 살아 있는 몬스터 하나를 죽입니다. 웨이브를 정리할 만큼
                   빠르고, 스포너가 방을 보는 프레임에 방이 비어 있지 않을 만큼 느립니다. */
                if ((i % 6) == 0) {
                    for (int k = 0; k < enemy_count(&w.pools); k++) {
                        const Enemy *m = enemy_at(&w.pools, k);
                        if (m && m->active && m->state != E_DEAD) {
                            enemy_hurt(&w.pools, k, 999, v3f(0, 0, 1));
                            break;
                        }
                    }
                }
            }
            double frame_ms = (now_ms() - wall0) / BLOCK;
            if (b == 0) first_ms = frame_ms;
            last_ms = frame_ms;

            int live = live_enemies(&w.pools), slots = enemy_count(&w.pools);
            printf("  %7.0fs %5d %6d %6d %6d %6d | %9.3f %8ld |",
                   af * DT, w.run.wave, slots, live, slots - live,
                   fx_live_count(&w.pools), frame_ms, working_kb());
            int de = diag_count(DIAG_ENEMY_CAP) - e0;
            int dx = diag_count(DIAG_FX_CAP) - x0;
            if (de) printf(" enemycap+%d", de);
            if (dx) printf(" fxcap+%d", dx);
            printf("\n");
            fflush(stdout);
        }
        printf("\n  first block %.3f ms/frame, last block %.3f ms/frame  (%+.1f%%)\n",
               first_ms, last_ms, 100.0 * (last_ms - first_ms) / (first_ms ? first_ms : 1.0));
        printf("  reached wave %d with %d of %d monster slots used\n",
               w.run.wave, enemy_count(&w.pools), ENEMY_MAX);
    }

    /* --- phase 4: restarting, over and over ------------------------------
       ENGLISH: a restart is not a level load with a different name -- it is
       world_load_level into the SAME Level struct, which is the one path that
       reuses its brush slot rather than claiming a new one (see
       Level::brush_key), and the one that runs run_reset afterwards. Whatever
       a restart fails to put back is therefore invisible to phase 2, which
       only ever loaded a different level next.
       This calls it the way main.c does: restart, then take the geometry scope
       and rebuild, then play a few frames.

       한국어: 재시작은 이름이 다른 레벨 로드가 아닙니다. *같은* Level 구조체로 들어가는
       world_load_level이며, 새 슬롯을 청구하지 않고 자기 브러시 슬롯을 재사용하는 유일한
       경로이고(::Level::brush_key 참조) 그 뒤에 run_reset을 실행하는 유일한 경로입니다.
       따라서 재시작이 되돌려 놓지 못한 것은, 다음에 언제나 *다른* 레벨을 로드한 2단계에는
       보이지 않습니다.
       이곳은 main.c가 부르는 방식 그대로 부릅니다. 재시작하고, 지오메트리 범위를 가져와
       다시 만들고, 몇 프레임을 진행합니다. */
    {
        /* Long enough between restarts that the run has something to leave
           behind -- monsters spawned and killed, particles in the air, decals
           on the walls, sounds playing. Restarting from an empty room measures
           a restart with nothing to undo, which is the easy half.
           재시작 사이가 충분히 길어서 그 플레이가 남길 것이 생깁니다. 생성되고 죽은
           몬스터, 공중의 입자, 벽의 자국, 재생 중인 소리입니다. 빈 방에서 재시작하는 것은
           되돌릴 것이 없는 재시작을 재는 것이며, 그것은 쉬운 절반입니다. */
        const int restarts = 200, settle = 180;

        printf("\n  --- %d restarts, %d frames of play each ---\n", restarts, settle);
        printf("\n  %5s %10s %10s %10s | %8s | %s\n",
               "n", "restart/ms", "build/ms", "frame/ms", "rss/KB", "diag deltas");
        printf("  %s\n",
               "----------------------------------------------------------------------");

        world_load_level(&w, WORLD_START_LEVEL, WORLD_ENTER_NEW);
        w.run.title = 0;
        {
            int dyn; world_take_geometry_scope(&w, &dyn);
            scene_build_level(&scene, &w.level, dyn);
        }

        double first_r = 0, last_r = 0, first_f = 0, last_f = 0;
        for (int r = 1; r <= restarts; r++) {
            int s0 = diag_count(DIAG_LEVEL_SLOTS);
            int t0d = diag_count(DIAG_TEX_CACHE);
            int l0 = diag_count(DIAG_LIGHT_CACHE);
            int v0 = diag_count(DIAG_VERTEX_BUF);
            int m0 = diag_count(DIAG_MAT_RANGES);
            int d0 = diag_count(DIAG_DOOR_STALE);

            double a = now_ms();
            world_restart(&w);
            double b = now_ms();

            int dyn;
            switch (world_take_geometry_scope(&w, &dyn)) {
            case WORLD_GEOM_ALL:    scene_build_level(&scene, &w.level, dyn); break;
            case WORLD_GEOM_MOVING: scene_rebuild_moving(&scene, &w.level);   break;
            case WORLD_GEOM_NONE:   break;
            }
            double c2 = now_ms();

            w.run.title = 0;
            double play = 0;
            for (int i = 0; i < settle; i++, frame++) {
                double s, d;
                one_frame(&w, &scene, frame, &s, &d);
                play += s + d;

                if ((i % 6) == 0) {
                    for (int k = 0; k < enemy_count(&w.pools); k++) {
                        const Enemy *m = enemy_at(&w.pools, k);
                        if (m && m->active && m->state != E_DEAD) {
                            enemy_hurt(&w.pools, k, 999, v3f(0, 0, 1));
                            break;
                        }
                    }
                }
            }

            if (r == 1) { first_r = b - a; first_f = play / settle; }
            last_r = b - a; last_f = play / settle;

            if (r <= 3 || (r % 50) == 0 || r == restarts) {
                printf("  %5d %10.3f %10.3f %10.3f | %8ld |",
                       r, b - a, c2 - b, play / settle, working_kb());
                int ds = diag_count(DIAG_LEVEL_SLOTS) - s0;
                int dt2 = diag_count(DIAG_TEX_CACHE) - t0d;
                int dl2 = diag_count(DIAG_LIGHT_CACHE) - l0;
                int dv2 = diag_count(DIAG_VERTEX_BUF) - v0;
                int dm2 = diag_count(DIAG_MAT_RANGES) - m0;
                int dd2 = diag_count(DIAG_DOOR_STALE) - d0;
                if (ds)  printf(" lvlslot+%d", ds);
                if (dt2) printf(" texcache+%d", dt2);
                if (dl2) printf(" lcache+%d", dl2);
                if (dv2) printf(" vtx+%d", dv2);
                if (dm2) printf(" ranges+%d", dm2);
                if (dd2) printf(" doorstale+%d", dd2);
                printf(" [mons %d fx %d]", enemy_count(&w.pools),
                       fx_live_count(&w.pools));
                printf("\n");
                fflush(stdout);
            }
        }
        printf("\n  restart  %.3f -> %.3f ms  (%+.1f%%)\n", first_r, last_r,
               100.0 * (last_r - first_r) / (first_r ? first_r : 1.0));
        printf("  frame    %.3f -> %.3f ms  (%+.1f%%)\n", first_f, last_f,
               100.0 * (last_f - first_f) / (first_f ? first_f : 1.0));
    }

    scene_free(&scene);
    decal_free();
    fx_free();
    audio_shutdown();
    post_shutdown();
    return 0;
}
