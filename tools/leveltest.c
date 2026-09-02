/* leveltest -- check the sector level parses, builds and answers queries.
 *
 * Geometry bugs and collision bugs look identical from inside the game (you
 * fall through something, or you cannot walk somewhere) so they are worth
 * separating before any of it is rendered.
 */

#include <stdio.h>
#include <math.h>
#include <time.h>
#include "level.h"
#include "brush.h"   /* brush_point_in -- a teleport destination must not sit in its own volume */
#include "txt.h"    /* txt_is, and txt_to_int, which every number below goes through */
#include <limits.h> /* INT_MAX / INT_MIN, the two values the saturation lands on */
#include <string.h>   /* strlen -- an entity kind is a plain C string */
#include "data.h"    /* data_baked / data_text: the blob and the file, told apart */
#include "player.h"
#include "pickup.h"  /* pickup_kind_for_n -- is the artifact IN the level */
/* Builds real geometry to check winding and spans, so it needs the renderer's
   CPU-side half and MdlRange by value. level.h forward-declares both rather
   than including them, keeping the GL stack out of the simulation headers.
   감기 순서와 구간을 검사하기 위해 실제 지오메트리를 생성하므로, 렌더러의 CPU 측
   절반과 값으로 사용되는 MdlRange가 필요합니다. level.h는 GL 스택을 시뮬레이션
   헤더에서 배제하기 위해 이 둘을 포함하지 않고 전방 선언합니다. */
#include "render.h"
#include "model.h"
#include "diag.h"   /* diag_count -- the overflow report is the thing under test */
/* mon_stats / mon_type_for: the far end of the entity-kind pipeline. A kind is
   written in a .map, stored by level.c and resolved by enemy.c, and no single
   module can check that the three agree.
   mon_stats / mon_type_for: 엔티티 종류 파이프라인의 반대쪽 끝입니다. 종류는 .map에
   적히고 level.c가 저장하며 enemy.c가 해석하는데, 그 셋이 일치하는지는 어느 한
   모듈도 검사할 수 없습니다. */
#include "enemy.h"

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-52s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void okd(int cond, const char *what, int got, int want) {
    printf("  %-52s %6d / %6d  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void okf(int cond, const char *what, float got, float want) {
    printf("  %-52s %8.3f / %8.3f  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* ------------------------------ vertex fingerprints, shared by the checks */

/* One number for the whole contents of a vertex buffer, so that two builds can
 * be compared without comparing them field by field at every call site.
 *
 * `n_floats` chooses how much of a vertex counts, and the two settings are not
 * interchangeable. VTX_ALL includes the baked light, which is what a check for
 * "the same build twice" wants. VTX_GEOM stops before it, which is what a
 * check across a door's motion wants: the SHAPE has to match exactly, while
 * the LIGHT deliberately does not follow a door, so folding both into one
 * number would make the accepted trade look like a failure.
 *
 * The per-vertex hashes are SUMMED rather than chained, so the result does not
 * depend on the order the vertices arrive in -- two builds that produce the
 * same surfaces in a different order still agree.
 *
 * 정점 버퍼의 내용 전체를 하나의 숫자로 만들어, 두 생성 결과를 호출 지점마다 필드별로
 * 비교하지 않고도 비교할 수 있게 합니다.
 *
 * `n_floats`는 정점의 어디까지를 셀지 고르며, 두 설정은 서로 바꿔 쓸 수 없습니다. VTX_ALL은
 * 구워진 빛을 포함하며 "같은 생성을 두 번" 검사할 때 원하는 것입니다. VTX_GEOM은 그 앞에서
 * 멈추며 문의 움직임을 가로지르는 검사가 원하는 것입니다. *형태*는 정확히 일치해야 하지만
 * *빛*은 의도적으로 문을 따라가지 않으므로, 둘을 한 숫자에 접어 넣으면 받아들이기로 한
 * 대가가 실패처럼 보이게 됩니다.
 *
 * 정점별 해시를 연결하지 않고 *더하므로* 결과는 정점이 도착하는 순서에 의존하지 않습니다.
 * 같은 표면을 다른 순서로 만들어 낸 두 생성도 여전히 일치합니다. */
#define VTX_GEOM 8                       /* px..v -- everything but the light */
#define VTX_ALL  (int)(sizeof(Vtx) / sizeof(float))

static unsigned vtx_fingerprint_n(const MeshBuf *b, int n_floats) {
    unsigned acc = 0;
    for (int i = 0; i < b->count; i++) {
        const float *f = &b->v[i].px;
        unsigned h = 2166136261u;                 /* FNV-1a over the vertex */
        for (int k = 0; k < n_floats; k++) {
            unsigned bits;
            /* Through a union rather than a cast: -O2 is allowed to assume a
               float and an unsigned never alias, and reading one through a
               pointer to the other is where that assumption bites.
               캐스트가 아니라 공용체를 거칩니다. -O2는 float와 unsigned가 서로 앨리어싱
               하지 않는다고 가정해도 되며, 한쪽을 다른 쪽의 포인터로 읽는 것이 바로 그
               가정이 무는 지점입니다. */
            union { float f; unsigned u; } cv;
            cv.f = f[k];
            bits = cv.u;
            /* -0.0 and +0.0 compare equal and hash differently. A cap built
               alone and the same cap built with the rest must fingerprint the
               same, so the sign of zero is normalised away.
               -0.0과 +0.0은 같다고 비교되지만 다르게 해시됩니다. 따로 만든 바닥과 나머지와
               함께 만든 같은 바닥은 같은 지문이어야 하므로 0의 부호를 지웁니다. */
            if (bits == 0x80000000u) bits = 0;
            h = (h ^ bits) * 16777619u;
        }
        acc += h;
    }
    return acc;
}

static unsigned vtx_fingerprint(const MeshBuf *b) {
    return vtx_fingerprint_n(b, VTX_ALL);
}

/* -------------------------------------------------- the baked-light cache */

/* Moves every door in `l` to `t` of its travel, exactly as door.c's apply()
   does. The point is not to simulate a door but to reach the geometry the
   game is actually rebuilding on the frames that cost something.
   door.c의 apply()가 하는 것과 똑같이 `l`의 모든 문을 이동 구간의 `t` 지점으로 옮깁니다.
   목적은 문을 흉내 내는 것이 아니라, 비용이 드는 프레임에 게임이 실제로 다시 만들고 있는
   지오메트리에 도달하는 것입니다. */
static void move_every_door(Level *l, float t) {
    int n = l->n_doors > LVL_MAX_DOORS ? LVL_MAX_DOORS : l->n_doors;
    for (int i = 0; i < n; i++) {
        const DoorDef *d = &l->doors[i];
        if (d->sector < 0 || d->sector >= l->n_sectors) continue;
        Sector *s = &l->sectors[d->sector];

        switch (d->axis) {
        case DOOR_UP:   s->ceil  = (short)(s->ceil  + d->amount * t); break;
        case DOOR_DOWN: s->floor = (short)(s->floor - d->amount * t); break;
        case DOOR_X:
        case DOOR_Z: {
            int off = (d->axis == DOOR_X) ? 0 : 1;
            for (int k = 0; k < s->n; k++)
                s->pts[k*2 + off] = (short)(s->pts[k*2 + off] + d->amount * t);
            level_bounds(s);
            break;
        }
        default: break;
        }
    }
    /* A slid door occupies different grid cells, and sector_at consults the
       grid first -- level_edge_spans asks it who the neighbour is. */
    level_grid_build(l);
}

/* Gives a level a sun if it has none, because the bake is the sun now.
 *
 * ::bake_light stopped summing point lamps -- they are per-fragment lights in
 * the shader beside the grenades, see scene.c's LIGHT_LAMP_POWER -- and every
 * hand-authored level in this project declares lamps and no sun. So all three
 * shipped names below would bake NOTHING, the cache would hold nothing, and
 * every check in this section would compare two identical unlit builds and
 * pass without touching the code it is about.
 *
 * Overhead and bright, which is the least interesting sun there is on purpose:
 * the subject here is the cache, not the lighting, and a sun that reaches most
 * vertices is a sun that gives the cache something to hold.
 *
 * 레벨에 태양이 없으면 하나 줍니다. 이제 베이크가 곧 태양이기 때문입니다.
 *
 * ::bake_light는 점광원을 합하는 일을 그만두었고(셰이더 안에서 유탄 곁의 프래그먼트 광원이
 * 되었습니다. scene.c의 LIGHT_LAMP_POWER를 참조하십시오), 이 프로젝트의 손으로 만든 레벨은
 * 전부 등만 선언하고 태양은 선언하지 않습니다. 그래서 아래의 출하 레벨 셋은 아무것도 굽지
 * 않고, 캐시는 아무것도 담지 않으며, 이 절의 모든 검사가 조명 없는 동일한 두 생성을 비교하며
 * 대상 코드를 건드리지도 않은 채 통과했을 것입니다.
 *
 * 머리 위이고 밝은, 일부러 가장 심심한 태양입니다. 이곳의 주제는 조명이 아니라 캐시이고,
 * 대부분의 정점에 닿는 태양이 곧 캐시에 담을 것을 주는 태양입니다. */
static void ensure_sun(Level *l) {
    if (l->sun_power > 0 || l->sky_power > 0) return;
    l->sun[0] = 0.0f; l->sun[1] = 1.0f; l->sun[2] = 0.0f;
    l->sun_power = 200;
}

/* Does this buffer hold a vertex at exactly this place, facing this way? */
static int has_vertex_like(const MeshBuf *b, const Vtx *v) {
    for (int i = 0; i < b->count; i++)
        if (b->v[i].px == v->px && b->v[i].py == v->py && b->v[i].pz == v->pz
         && b->v[i].nx == v->nx && b->v[i].ny == v->ny && b->v[i].nz == v->nz)
            return 1;
    return 0;
}

/* The two properties the cache has to have, and the number that says whether
 * it was worth having.
 *
 *   1. IT MUST NOT CHANGE THE PICTURE ON THE FRAME A LEVEL LOADS. An empty
 *      cache is every vertex traced, which is what the bake did before there
 *      was a cache, so a level's first build has to come out identical to one
 *      built with the cache disabled. This is the check that would catch a
 *      hash collision being treated as a match.
 *   2. IT MUST FOLLOW A VERTEX THAT MOVES. A vertex at a new position has no
 *      cached reading and has to be traced, or a door would drag stale
 *      lighting around with it.
 *
 * And the number: how many of a rebuild's vertices the cache can answer. That
 * is the whole return on this, and it belongs in the test rather than in a
 * commit message, because it is the thing that stops being true first.
 *
 * 캐시가 가져야 할 두 성질, 그리고 그것이 가질 가치가 있었는지 말해 주는 수치입니다.
 *
 *   1. 레벨이 로드되는 프레임의 화면을 바꾸어서는 안 됩니다. 빈 캐시는 모든 정점을 판정하는
 *      것이고 그것이 캐시가 없던 시절 베이크가 하던 일이므로, 레벨의 첫 생성은 캐시를 끈
 *      생성과 동일하게 나와야 합니다. 해시 충돌을 일치로 취급하는 사태를 잡을 검사입니다.
 *   2. 움직인 정점은 따라가야 합니다. 새 위치의 정점에는 캐시된 값이 없으므로 판정되어야
 *      하며, 그러지 않으면 문이 낡은 조명을 끌고 다니게 됩니다.
 *
 * 그리고 수치: 재생성의 정점 중 몇 개를 캐시가 답할 수 있는가. 이것이 이 작업의 수익 전부이며,
 * 가장 먼저 사실이 아니게 되는 것이므로 커밋 메시지가 아니라 테스트에 있어야 합니다. */
static void light_cache_one(const char *name) {
    Level a, b;
    if (!level_load(name, &a)) { printf("    (no level '%s')\n", name); return; }
    ensure_sun(&a);

    MeshBuf first, again;
    mb_init(&first, 32768);
    mb_init(&again, 32768);

    char what[96];

    /* 1. level_load has just emptied the cache, so this build traces
          everything -- the pre-cache behaviour. Building a second time from
          the same level must then reproduce it exactly, this time entirely out
          of the cache. Same picture, no traces. */
    level_geometry(&first, &a, 0, 0);
    int traced = level_light_cache_count();
    int unique = traced;

    level_geometry(&again, &a, 0, 0);

    snprintf(what, sizeof(what), "%s: a cached rebuild is the build it cached", name);
    ok(first.count == again.count
       && vtx_fingerprint(&first) == vtx_fingerprint(&again), what);

    /* 1b. THE CLAIM THE CACHE RESTS ON, run rather than asserted in a comment.
           level.c says an empty cache reproduces the old behaviour vertex for
           vertex -- but a filling cache and a switched-off one are two
           different code paths through bake_light, and the only thing that
           makes them agree is the key being everything the bake reads. If a
           position and a normal were ever NOT enough to decide a vertex's
           light, this is where it would show, as two builds of one level that
           disagree about a colour.

           The comparison is over VTX_ALL, light included: comparing the shape
           would pass no matter what the cache handed back.

           캐시가 딛고 선 주장을, 주석 속 단언이 아니라 실행해서 확인합니다. level.c는 빈
           캐시가 이전 동작을 정점 하나까지 재현한다고 말하지만, 채워지는 캐시와 꺼진 캐시는
           bake_light를 지나는 서로 다른 두 경로이며, 그 둘을 일치시키는 것은 키가 베이크가
           읽는 전부라는 사실뿐입니다. 위치와 법선이 한 정점의 빛을 결정하기에 충분하지 않은
           경우가 있다면, 한 레벨의 두 생성이 색에 대해 어긋나는 형태로 이곳에서 드러납니다.

           비교는 빛을 포함한 VTX_ALL로 합니다. 형태만 비교하면 캐시가 무엇을 돌려주든
           통과하기 때문입니다. */
    MeshBuf nocache;
    mb_init(&nocache, 32768);

    level_light_cache_enable(0);
    level_geometry(&nocache, &a, 0, 0);
    level_light_cache_enable(1);

    snprintf(what, sizeof(what), "%s: the cache changes nothing it did not trace", name);
    ok(nocache.count == first.count
       && vtx_fingerprint(&nocache) == vtx_fingerprint(&first), what);

    mb_free(&nocache);

    printf("    %-8s %5d verts, %d unique keys, sun %d, %d doors\n",
           name, first.count, unique, a.sun_power, a.n_doors);

    if (a.n_doors < 1) { mb_free(&first); mb_free(&again); return; }

    /* 2. The same level with its doors half open, which is the state a frame
          that pays for a rebuild is in. Every vertex that MOVED has to be
          traced afresh -- checked by building the moved level from a cold
          cache and requiring the same answer. If the cache were handing back
          readings for vertices that are no longer where they were, the two
          would differ. */
    if (!level_load(name, &b)) { mb_free(&first); mb_free(&again); return; }
    ensure_sun(&b);
    move_every_door(&b, 0.5f);

    MeshBuf warm, cold;
    mb_init(&warm, 32768);
    mb_init(&cold, 32768);

    /* Warm: the cache still holds the closed-door build, as it would in play. */
    level_geometry(&warm, &b, 0, 0);

    /* Cold: the same geometry with nothing remembered. */
    level_light_cache_reset();
    level_geometry(&cold, &b, 0, 0);

    snprintf(what, sizeof(what), "%s: the same shape either way, warm or cold", name);
    ok(warm.count == cold.count
       && vtx_fingerprint_n(&warm, VTX_GEOM) == vtx_fingerprint_n(&cold, VTX_GEOM),
       what);

    /* What the cache answered, and what it therefore did not trace. Counted
       against the cold build rather than guessed at: a vertex the warm build
       could serve is one that already existed at that position and normal.
       캐시가 답한 것, 따라서 판정하지 않은 것입니다. 짐작이 아니라 차가운 생성에 대해
       셉니다. 따뜻한 생성이 답할 수 있는 정점이란 그 위치와 법선에 이미 존재했던
       정점입니다. */
    int reused = 0;
    for (int i = 0; i < cold.count; i++)
        if (has_vertex_like(&first, &cold.v[i])) reused++;

    printf("      doors at half travel: %d of %d verts answered from cache"
           " (%.1f%%)\n", reused, cold.count,
           cold.count ? 100.0 * reused / cold.count : 0.0);

    /* The light on a vertex that did not move is the light it had, which is
       the trade this makes and the reason a door no longer relights the room
       behind it. Reported for the same reason the hit rate is.
       움직이지 않은 정점의 빛은 그것이 가지고 있던 빛입니다. 이것이 이 작업이 하는 거래이며,
       문이 더 이상 뒤쪽 방을 다시 밝히지 않는 이유입니다. 적중률과 같은 이유로 보고합니다. */
    if (warm.count == cold.count) {
        int frozen = 0;
        for (int i = 0; i < warm.count; i++)
            if (warm.v[i].lr != cold.v[i].lr || warm.v[i].lg != cold.v[i].lg
             || warm.v[i].lb != cold.v[i].lb) frozen++;
        printf("      light that no longer follows the door: %d of %d verts\n",
               frozen, warm.count);
    }

    mb_free(&first); mb_free(&again); mb_free(&warm); mb_free(&cold);
}

/* What happens when the table is too small for the level.
 *
 * The overflow path -- trace anyway, store nothing, count it -- is the kind of
 * code that is written once and never runs again, because the shipped table is
 * comfortably larger than the shipped levels. So build.ps1 builds a second
 * binary with LIGHT_CACHE_SLOTS forced small, and this is what that binary
 * exists to run. In the normal build the table is big enough and the checks
 * below assert the opposite: that nothing overflowed.
 *
 * Either way what is required is the same, and it is the important part: an
 * overflowing cache must still draw the RIGHT PICTURE. It is allowed to be
 * slow. A level that renders differently because its light cache filled up
 * would be a fault that appears only on large maps and only sometimes.
 *
 * 테이블이 레벨에 비해 너무 작을 때 무슨 일이 일어나는가입니다.
 *
 * 초과 경로(그래도 판정하고, 저장하지 않고, 센다)는 한 번 작성된 뒤 다시 실행되지 않는
 * 종류의 코드입니다. 출하 테이블이 출하 레벨보다 넉넉히 크기 때문입니다. 그래서 build.ps1이
 * LIGHT_CACHE_SLOTS를 작게 강제한 두 번째 바이너리를 만들며, 이 함수가 그 바이너리가
 * 실행하려고 존재하는 것입니다. 일반 빌드에서는 테이블이 충분히 크므로 아래 검사가 그 반대,
 * 즉 아무것도 넘치지 않았음을 단언합니다.
 *
 * 어느 쪽이든 요구되는 것은 같고 그것이 중요한 부분입니다. 넘친 캐시도 여전히 *옳은 그림*을
 * 그려야 합니다. 느려도 됩니다. 라이트 캐시가 가득 찼다는 이유로 다르게 렌더링되는 레벨은
 * 큰 맵에서만, 그것도 가끔만 나타나는 결함입니다. */
static void overflow_checks(void) {
    /* arena with a sun on it, and the two halves of that are separate facts.
       ARENA rather than the bigger dm03 because the two are within a few
       thousand vertices of each other and arena is the one with doors, so it
       is the level whose rebuilds the cache exists for.
       WITH A SUN because the cache holds what the bake touched, and the bake
       is directional light now: a level that declares no sun returns from
       bake_light immediately and could not overflow a table of any size. The
       level that fills a cache is the lit one, not the large one -- which is
       worth knowing before sizing the table against a vertex count.
       태양을 얹은 arena이며, 그 둘은 서로 다른 사실입니다.
       *arena*인 이유는 더 큰 dm03과 정점 수가 몇 천 개밖에 차이 나지 않는 데다 문이 있는
       쪽이 arena이기 때문입니다. 캐시가 존재하는 이유인 재생성을 겪는 레벨이 그것입니다.
       *태양을 얹는* 이유는 캐시가 담는 것이 베이크가 건드린 정점이고 이제 베이크는 방향성
       조명이기 때문입니다. 태양을 선언하지 않는 레벨은 bake_light에서 즉시 반환하며 어떤
       크기의 테이블도 넘치게 할 수 없습니다. 캐시를 채우는 레벨은 큰 레벨이 아니라 *밝은*
       레벨이며, 정점 수를 기준으로 테이블 크기를 정하기 전에 알아 둘 가치가 있습니다. */
    Level l;
    if (!level_load("arena", &l)) { printf("    (no level 'arena')\n"); return; }
    ensure_sun(&l);

    int before = diag_count(DIAG_LIGHT_CACHE);

    MeshBuf cached, plain;
    mb_init(&cached, 32768);
    mb_init(&plain,  32768);

    level_geometry(&cached, &l, 0, 0);
    int fired = diag_count(DIAG_LIGHT_CACHE) - before;
    int held  = level_light_cache_count();

    level_light_cache_enable(0);
    level_geometry(&plain, &l, 0, 0);
    level_light_cache_enable(1);

    int slots = level_light_cache_slots();
    printf("    %d slots (%d bytes of .bss), level filled %d, DIAG fired %d\n",
           slots, level_light_cache_bytes(), held, fired);

    /* The two builds must agree whether or not the table ran out, which is the
       property that lets a small table be a performance decision rather than a
       rendering one. */
    ok(cached.count == plain.count
       && vtx_fingerprint(&cached) == vtx_fingerprint(&plain),
       "an overflowing cache still draws the same picture");

    if (held >= slots) {
        /* The whole reason the small binary is built: reach the cap, and prove
           the report fires. A cap that is never reached is a cap that has
           never been tested. */
        ok(fired > 0, "a table too small for the level reports the overflow");
        ok(held == slots, "and it filled every slot it had before giving up");
    } else {
        ok(fired == 0, "the shipped table is big enough for the largest level");
        ok(held < slots, "with room left over, so the probe stays short");
    }

    mb_free(&cached);
    mb_free(&plain);
}

/* A level with more lamps than the shader has slots for.
 *
 * That used to be impossible by construction: LVL_MAX_LIGHTS was RD_MAX_LIGHTS
 * and a static assert held them equal, because evaluating a lamp in the
 * shader's loop was the only way a lamp was applied. The bake separated them,
 * and then the lamps went BACK to the shader's loop and they stayed separate,
 * because what reaches the loop is the nearest RD_MAX_LIGHTS of them chosen
 * per frame. The two caps still answer different questions -- one is load
 * time, the other is per-fragment.
 *
 * WHAT THIS FILE CAN STILL SAY ABOUT THAT, and what it cannot. Which lamps a
 * frame carries is ::scene_lights' answer and changes as the player walks, so
 * it is watched in tools/scenetest.c where there is a frame to watch. What is
 * a level-layer fact is that all sixteen survive the load, and that NONE of
 * them is baked -- because a lamp applied in both places is applied twice, and
 * a room lit twice does not look broken, it looks bright. That is the failure
 * this fixture is placed to catch.
 *
 * Built here rather than authored into a level file, for the reason the rest of
 * this file builds its fixtures: a shipped level is a map somebody edits, and a
 * check that depends on it having sixteen lamps goes red the day somebody
 * removes one.
 *
 * 셰이더가 가진 슬롯보다 등이 많은 레벨입니다.
 *
 * 이전에는 구조적으로 불가능했습니다. LVL_MAX_LIGHTS가 RD_MAX_LIGHTS였고 정적 검사가 둘을
 * 같게 붙들고 있었는데, 셰이더 반복문에서 평가하는 것이 등이 적용되는 유일한 방법이었기
 * 때문입니다. 베이크가 둘을 갈라놓았고, 이후 등이 셰이더 반복문으로 *돌아왔지만* 둘은 여전히
 * 분리되어 있습니다. 반복문에 도달하는 것은 프레임마다 골라진 가장 가까운 RD_MAX_LIGHTS개이기
 * 때문입니다. 두 상한은 여전히 서로 다른 질문에 답합니다. 하나는 로드 시간, 다른 하나는
 * 프래그먼트별 비용입니다.
 *
 * *이 파일이 그것에 대해 여전히 말할 수 있는 것과 말할 수 없는 것.* 한 프레임이 어느 등을
 * 나르는지는 ::scene_lights의 답이며 플레이어가 걸으면 달라지므로, 볼 프레임이 있는
 * tools/scenetest.c에서 지켜봅니다. 레벨 층위의 사실은 열여섯이 모두 로드를 견딘다는 것과,
 * 그중 *어느 것도 구워지지 않는다*는 것입니다. 두 곳에서 적용된 등은 두 번 적용된 것이고,
 * 두 번 밝혀진 방은 고장 나 보이지 않고 밝아 보이기 때문입니다. 이 픽스처가 잡으려고 놓인
 * 실패가 그것입니다.
 *
 * 레벨 파일에 작성하지 않고 이곳에서 만드는 이유는 이 파일의 나머지가 픽스처를 만드는 이유와
 * 같습니다. 출하 레벨은 누군가 편집하는 맵이고, 그것이 등 열여섯 개를 가졌다는 데 의존하는
 * 검사는 누군가 하나를 지우는 날 빨개집니다. */
/* Twice what the shader can hold, so the eight that would not have fitted are
   the whole point of the fixture.

   The static assert is not defensive padding -- it is the check. The realistic
   way this separation gets undone is not somebody hardcoding an 8 in the
   parser; it is somebody tying the two caps together again, and that lands
   here as a compile error naming the reason rather than as a test that
   silently writes past the end of Level::lights.
   셰이더가 담을 수 있는 것의 두 배이므로, 들어가지 못했을 여덟 개가 이 픽스처의 요점
   전부입니다.

   정적 검사는 방어적 여백이 아니라 *그 자체가 검사*입니다. 이 분리가 되돌려지는 현실적인
   경로는 누군가 파서에 8을 하드코딩하는 것이 아니라 두 상한을 다시 묶는 것이며, 그것은
   Level::lights의 끝을 조용히 넘어 쓰는 테스트가 아니라 이유를 말하는 컴파일 오류로
   이곳에 도착합니다. */
#define WANT_LAMPS 16
_Static_assert(WANT_LAMPS > RD_MAX_LIGHTS,
               "the fixture has to exceed the shader's slots to test anything");
_Static_assert(WANT_LAMPS <= LVL_MAX_LIGHTS,
               "a level may no longer declare more lamps than the shader holds"
               " -- the two caps have been tied together again");
/* A ROOM PER LAMP, so this fixture spends a sector on each one too -- which is
   a claim on a second cap, and it was not being made. A build with
   LVL_MAX_SECTORS below WANT_LAMPS wrote sixteen sectors into an array that
   held eight and took the segfault, with nothing to say the fixture had
   outgrown the format. It is the same argument as the assert above it: the
   realistic way this breaks is somebody lowering a cap, and that should land
   here naming the reason.
   등마다 방 하나이므로 이 픽스처는 등마다 섹터도 하나씩 씁니다. 이는 두 번째 상한에 대한
   주장이며 그동안 제기되지 않고 있었습니다. LVL_MAX_SECTORS가 WANT_LAMPS보다 낮은 빌드는
   여덟 개를 담는 배열에 섹터 열여섯 개를 기록하고 segfault를 냈으며, 픽스처가 형식보다
   커졌다고 말해 주는 것은 아무것도 없었습니다. 위 검사와 같은 논거입니다. 이것이 깨지는
   현실적인 경로는 누군가 상한을 낮추는 것이며, 그것은 이유를 말하며 이곳에 도착해야
   합니다. */
_Static_assert(WANT_LAMPS <= LVL_MAX_SECTORS,
               "one room per lamp: the fixture needs a sector for each");

static void many_lamp_checks(void) {
    Level l;
    Level zero = {0};
    l = zero;

    /* SIXTEEN SMALL ROOMS IN A ROW, one lamp each, rather than one big room
       with sixteen lamps in it. The first attempt was the big room and it
       failed for a reason worth keeping: light is baked at VERTICES, a flat
       quad floor has vertices only at its corners, and a lamp in the middle of
       a 60m room reaches none of them. Nothing was lit and the check reported a
       cap that was working fine.

       A room per lamp puts four floor corners inside every lamp's radius, so
       "was this lamp applied" becomes a question the geometry can answer.

       큰 방 하나에 등 열여섯 개가 아니라, 등이 하나씩 있는 *작은 방 열여섯 개를 줄지어*
       놓습니다. 첫 시도가 큰 방이었고 붙잡아 둘 만한 이유로 실패했습니다. 빛은 *정점*에
       구워지는데, 평평한 사각 바닥은 모서리에만 정점을 가지며, 60m 방 한가운데의 등은 그중
       어디에도 닿지 않습니다. 아무것도 밝혀지지 않았고 검사는 멀쩡히 동작하는 상한을
       문제라고 보고했습니다.

       등마다 방 하나를 두면 모든 등의 반경 안에 바닥 모서리 넷이 들어오므로, "이 등이
       적용되었는가"가 지오메트리가 답할 수 있는 질문이 됩니다. */
    for (int i = 0; i < WANT_LAMPS; i++) {
        short x0 = (short)(-4000 + i * 500);
        short x1 = (short)(x0 + 400);

        Sector *s = &l.sectors[l.n_sectors++];
        short pts[8] = { x0,-200,  x1,-200,  x1,200,  x0,200 };
        for (int k = 0; k < 8; k++) s->pts[k] = pts[k];
        s->n = 4;
        s->floor = 0;
        s->ceil  = 400;
        level_bounds(s);

        Light *L = &l.lights[l.n_lights++];
        L->x = (short)((x0 + x1) / 2);
        L->y = 200;
        L->z = 0;
        L->radius = 600;          /* reaches its own room's corners, not the next */
        L->r = L->g = L->b = 255;
        L->power = 100;
    }
    level_grid_build(&l);

    okd(l.n_lights == WANT_LAMPS,
        "a level may declare more lamps than the shader holds",
        l.n_lights, WANT_LAMPS);
    ok(LVL_MAX_LIGHTS > RD_MAX_LIGHTS,
       "and the two caps are no longer the same number");

    int before = diag_count(DIAG_LIGHT_CAP);

    MeshBuf b;
    mb_init(&b, 32768);
    level_light_cache_reset();
    level_geometry(&b, &l, 0, 0);

    okd(diag_count(DIAG_LIGHT_CAP) == before,
        "none of them was dropped on the way in",
        diag_count(DIAG_LIGHT_CAP) - before, 0);

    /* NOT ONE VERTEX CARRIES LIGHT, and every lamp is inside a room whose
       floor corners are well within its radius -- the fixture is built that
       way, see the note on the rooms above. So this is not "the lamps missed";
       it is the bake declining to sum them at all, which is what has to be
       true for the shader's copy to be the only copy.

       The level declares no sun, so there is no other term that could write a
       vertex here either: a non-zero reading can only have come from a lamp.

       *정점 하나도 빛을 지니지 않으며*, 모든 등은 바닥 모서리가 자기 반경 안에 넉넉히 들어오는
       방 안에 있습니다. 픽스처가 그렇게 지어져 있습니다. 위의 방 설명을 참조하십시오. 따라서
       이것은 "등이 빗나갔다"가 아니라 베이크가 아예 합하기를 거절한다는 뜻이며, 셰이더의
       사본이 유일한 사본이려면 그것이 참이어야 합니다.

       이 레벨은 태양을 선언하지 않으므로 이곳의 정점에 값을 쓸 수 있는 다른 항도 없습니다.
       0이 아닌 값은 등에서만 올 수 있습니다. */
    int baked = 0;
    for (int i = 0; i < b.count; i++)
        if (b.v[i].lr > 0.0f || b.v[i].lg > 0.0f || b.v[i].lb > 0.0f) baked++;

    printf("      %d of %d vertices carry baked light (want 0)\n",
           baked, b.count);
    ok(b.count > 0 && baked == 0,
       "and not one of them is baked into a vertex as well");

    mb_free(&b);
    level_light_cache_reset();
}

static void light_cache_checks(void) {
    /* Every shipped level: the cache is about door placement and lamp count,
       and only the levels know either. */
    static const char *NAMES[] = { "arena", "vault", "dm03" };
    for (int i = 0; i < (int)(sizeof(NAMES)/sizeof(NAMES[0])); i++)
        light_cache_one(NAMES[i]);

    printf("\n  --- when the table is too small ---\n");
    overflow_checks();

    printf("\n  --- more lamps than the shader could ever hold ---\n");
    many_lamp_checks();

    /* Left empty for whatever runs next, so one test cannot lend another its
       readings. */
    level_light_cache_reset();
}

/* The two refusals in the text loader that had no counter behind them.
 *
 * ENGLISH
 * -------
 * A sector past ::LVL_MAX_SECTORS and a point past ::LVL_MAX_PTS were both
 * dropped in silence while every cap beside them -- doors, entities, lights,
 * triggers, hazards -- reported. ::DIAG_SECTOR_CAP and ::DIAG_POINT_CAP close
 * that, and this is what proves they close it: a counter whose branch no
 * binary reaches is a counter nobody has seen work.
 *
 * The shipped caps cannot be reached by the shipped levels -- that is what
 * makes them adequate caps -- so build.ps1 compiles leveltest_tinycaps with
 * both forced down, the same bargain textest_tinycache and leveltest_tinylcache
 * already make. This function asserts the OPPOSITE thing in each binary, and
 * decides which from the constants rather than from an #ifdef: they are visible
 * here because this file and level.c are compiled with the same -D.
 *
 * What is required of both is the same and is the important part: a loader that
 * ran out of room must still produce a SELF-CONSISTENT level. Dropping the
 * surplus is allowed. Ending up with more sectors than the array holds, or an
 * outline longer than its own storage, is not -- and that is the failure the
 * silence used to hide.
 *
 * 한국어
 * ------
 * 텍스트 로더에서 뒤에 카운터가 없던 두 거절입니다.
 *
 * ::LVL_MAX_SECTORS를 넘은 섹터와 ::LVL_MAX_PTS를 넘은 점은 둘 다 조용히 버려졌습니다.
 * 그 곁의 모든 상한(문, 엔티티, 광원, 트리거, 위험 지형)이 보고하는 동안에 말입니다.
 * ::DIAG_SECTOR_CAP과 ::DIAG_POINT_CAP이 그것을 막으며, 이 함수가 그것을 증명합니다.
 * 어떤 바이너리도 도달하지 않는 분기를 가진 카운터는 아무도 동작을 본 적 없는 카운터입니다.
 *
 * 출하 상한은 출하 레벨이 도달할 수 없습니다. 그것이 그 상한을 충분한 상한으로 만드는
 * 것입니다. 그래서 build.ps1이 둘 다 낮춘 leveltest_tinycaps를 컴파일하며, 이는
 * textest_tinycache와 leveltest_tinylcache가 이미 맺고 있는 것과 같은 거래입니다. 이
 * 함수는 각 바이너리에서 *서로 반대되는 것*을 단언하며, 어느 쪽인지를 #ifdef가 아니라
 * 상수로부터 결정합니다. 이 파일과 level.c가 같은 -D로 컴파일되므로 상수가 이곳에서
 * 보입니다.
 *
 * 양쪽에 요구되는 것은 같고 그것이 중요한 부분입니다. 자리가 모자랐던 로더도 여전히
 * *자기 자신과 일관된* 레벨을 내놓아야 합니다. 초과분을 버리는 것은 허용됩니다. 배열이
 * 담는 것보다 많은 섹터로 끝나거나 자기 저장 공간보다 긴 외곽선으로 끝나는 것은 허용되지
 * 않으며, 침묵이 감추고 있던 실패가 바로 그것입니다.
 */
/* Every number in a level file comes through ::txt_to_int.
 *
 * ENGLISH
 * -------
 * It accumulated into a signed int with no bound on how many digits it would
 * accept -- ::txt_is_number takes a run of any length -- so `9999999999` in a
 * hand-edited levels.txt was signed overflow, which is undefined rather than
 * merely wrong. The fix saturates, and these state where.
 *
 * The interesting cases are the two ends. INT_MIN has no positive counterpart,
 * so a reader that negates its magnitude has the same undefined behaviour one
 * step later, and the value that reaches it -- `-2147483648` -- is one an
 * author can plausibly type after a copy-paste.
 *
 * 한국어
 * ------
 * 레벨 파일의 모든 숫자가 ::txt_to_int를 지나갑니다.
 *
 * 이 함수는 몇 자리까지 받을지에 대한 제한 없이 부호 있는 int에 누적했습니다.
 * ::txt_is_number가 임의 길이의 숫자 나열을 받기 때문입니다. 그래서 손으로 고친
 * levels.txt의 `9999999999`는 부호 있는 오버플로였고, 이는 단지 틀린 것이 아니라 정의되지
 * 않은 동작입니다. 수정은 포화시키며, 아래가 그 지점을 진술합니다.
 *
 * 흥미로운 것은 양 끝입니다. INT_MIN에는 대응하는 양수가 없으므로, 절댓값을 부정하는
 * 판독기는 한 걸음 뒤에 같은 정의되지 않은 동작을 갖게 됩니다. 그리고 그곳에 도달하는 값인
 * `-2147483648`은 제작자가 복사·붙여넣기 뒤에 충분히 칠 만한 값입니다.
 */
static void number_checks(void) {
    static const struct { const char *text; int want; const char *what; } CASES[] = {
        { "0",                     0,       "zero" },
        { "1300",                  1300,    "an ordinary push speed" },
        { "-450",                  -450,    "and an ordinary negative height" },
        { "2147483647",            INT_MAX, "INT_MAX exactly" },
        { "-2147483648",           INT_MIN, "INT_MIN exactly, which has no positive twin" },
        { "2147483648",            INT_MAX, "one past INT_MAX saturates" },
        { "-2147483649",           INT_MIN, "one past INT_MIN saturates" },
        { "99999999999999999999",  INT_MAX, "twenty digits saturate" },
        { "-99999999999999999999", INT_MIN, "and twenty negative ones" },
    };

    for (int i = 0; i < (int)(sizeof(CASES)/sizeof(CASES[0])); i++) {
        int len = 0;
        while (CASES[i].text[len]) len++;
        okd(txt_to_int(CASES[i].text, len) == CASES[i].want, CASES[i].what,
            txt_to_int(CASES[i].text, len), CASES[i].want);
    }

    /* The gate in front of it still calls all of those numbers, and only
       numbers. Saturating was chosen over rejecting precisely so this did not
       have to change -- a length limit here would stop a parse at a digit.
       그 앞의 관문은 여전히 그 전부를 숫자라고 부르며, 숫자만 그렇게 부릅니다. 거부가 아니라
       포화를 고른 것이 바로 이것을 바꾸지 않아도 되게 하기 위함입니다. 이곳의 길이 제한은
       파싱을 숫자 앞에서 멈추게 합니다. */
    ok(txt_is_number("99999999999999999999", 20),
       "a number too big to hold is still a number to the parser");
    ok(!txt_is_number("-", 1), "a lone minus is not");
    ok(!txt_is_number("12a", 3), "and neither is a digit run with a letter in it");
}

static void cap_checks(void) {
    static const char *NAMES[] = { "arena", "vault", "dm03" };

    /* Shipped caps, spelled out rather than inferred, so this reads as the
       claim it is: nothing the project ships comes near either one.
       추론하지 않고 명시한 출하 상한이며, 그래야 이것이 곧 주장으로 읽힙니다. 이
       프로젝트가 출하하는 무엇도 둘 중 어느 것에도 근접하지 않습니다. */
    int shipped = (LVL_MAX_SECTORS >= 64 && LVL_MAX_PTS >= 48);

    int sec_fired = 0, pt_fired = 0;

    for (int i = 0; i < (int)(sizeof(NAMES)/sizeof(NAMES[0])); i++) {
        int before_s = diag_count(DIAG_SECTOR_CAP);
        int before_p = diag_count(DIAG_POINT_CAP);

        Level l;
        if (!level_load(NAMES[i], &l)) {
            printf("    (no level '%s')\n", NAMES[i]);
            continue;
        }

        int ds = diag_count(DIAG_SECTOR_CAP) - before_s;
        int dp = diag_count(DIAG_POINT_CAP)  - before_p;
        sec_fired += ds;
        pt_fired  += dp;

        /* Required of every binary: what came out fits what it came out of.
           모든 바이너리에 요구되는 것입니다. 나온 것은 그것이 나온 곳에 들어맞습니다. */
        okd(l.n_sectors <= LVL_MAX_SECTORS, "sectors fit the array", l.n_sectors,
            LVL_MAX_SECTORS);

        int longest = 0, at_cap = 0;
        for (int s = 0; s < l.n_sectors; s++) {
            if (l.sectors[s].n > longest) longest = l.sectors[s].n;
            if (l.sectors[s].n >= LVL_MAX_PTS) at_cap = 1;
        }
        okd(longest <= LVL_MAX_PTS, "every outline fits its storage", longest,
            LVL_MAX_PTS);

        /* The counter and the state have to agree. A report with no truncated
           sector behind it is a counter firing on the wrong branch, which is
           worse than no counter -- it sends the reader to a constant that was
           never the problem.
           카운터와 상태가 일치해야 합니다. 잘려 나간 섹터 없이 나온 보고는 엉뚱한 분기에서
           발생한 카운터이며, 카운터가 없는 것보다 나쁩니다. 문제였던 적 없는 상수로 읽는
           사람을 보내기 때문입니다. */
        if (ds > 0)
            okd(l.n_sectors == LVL_MAX_SECTORS,
                "a reported sector overflow left the array full", l.n_sectors,
                LVL_MAX_SECTORS);
        if (dp > 0)
            ok(at_cap, "a reported point overflow left an outline at the cap");

        printf("    %-6s %2d sectors (cap %d), longest outline %2d (cap %d),"
               " DIAG %d/%d\n",
               NAMES[i], l.n_sectors, LVL_MAX_SECTORS, longest, LVL_MAX_PTS,
               ds, dp);

        level_release(&l);
    }

    if (shipped) {
        okd(sec_fired == 0, "no shipped level runs out of sectors", sec_fired, 0);
        okd(pt_fired  == 0, "and none runs out of outline points",  pt_fired,  0);
    } else {
        /* The whole reason this binary exists. If either of these goes red, the
           report is not reachable and the shipped assertions above are asserting
           that an unreachable thing did not happen.
           이 바이너리가 존재하는 이유 전부입니다. 둘 중 하나라도 빨간불이면 그 보고는 도달
           불가능하며, 위의 출하 단언은 도달할 수 없는 일이 일어나지 않았다고 단언하고 있는
           셈입니다. */
        ok(sec_fired > 0, "the forced sector cap is reported, not swallowed");
        ok(pt_fired  > 0, "and so is the forced point cap");
    }
}

int main(void) {
    Level l;
    printf("leveltest\n\n");

    if (!level_load("arena", &l)) { printf("  level 'arena' not found\n"); return 1; }
    printf("  loaded '%s': %d sectors, %d entities, start %d %d %d\n\n",
           l.name, l.n_sectors, l.n_ents, l.start[0], l.start[1], l.start[2]);

    /* Shape, not contents: `arena` is a map somebody edits, and a test that
       says "six sectors" fails the moment they add a seventh, which teaches
       you to ignore the suite rather than to read it. */
    ok(l.n_sectors >= 2, "the level parsed more than one sector");
    ok(l.n_ents   >= 1,  "and at least one entity");
    ok(l.sectors[0].n >= 3, "the first sector is a real polygon");

    /* --- the lamps this level declares are lit, and do not crowd -----------

       A TEXT `light` LINE IS LIT WHERE A BRUSH ONE IS NOT, and only a test
       says so: both go through the same ::Light and differ by one field that
       nothing in the geometry or the bake reads. The rule is about who writes
       the line -- the importer emits `light` by the dozen, a person types this
       one -- so it cannot be inferred from anything in the file, and if the
       flag were dropped the four lamps below would load, count, and light
       nothing, exactly as they did for the two revisions they were deleted.

       AND THE COUNT IS THE OTHER HALF. ::LVL_LAMP_MAX offers three; the sort
       that picks them can only churn when there is a fourth to pick between,
       so the claim worth holding is that no place the map puts a player has
       more lamps in reach than the cap can hold. scenetest makes the same
       claim about the brush arena. Same rule, both level formats, because the
       failure it guards against does not care which parser built the room.

       *텍스트 `light` 줄은 켜져 있고 브러시의 것은 아니며*, 그것을 말하는 것은 검사뿐입니다.
       둘은 같은 ::Light를 지나며 기하와 베이크의 무엇도 읽지 않는 필드 하나로 갈립니다.
       규칙은 그 줄을 누가 쓰는가에 대한 것이므로 파일 안의 무엇으로도 추론할 수 없고,
       플래그가 빠지면 아래 네 등은 불러들여지고 세어지고 아무것도 밝히지 않습니다. 삭제되어
       있던 두 판 동안 정확히 그랬던 대로입니다.
       *그리고 개수가 나머지 절반입니다.* ::LVL_LAMP_MAX는 셋을 내놓으며, 그것을 고르는
       정렬은 고를 넷째가 있을 때만 요동칩니다. 그러므로 지킬 값어치가 있는 주장은 맵이
       플레이어를 두는 어느 자리에도 상한이 담을 수 있는 것보다 많은 등이 닿지 않는다는
       것입니다. scenetest가 브러시 투기장에 대해 같은 주장을 합니다. 같은 규칙, 두 형식.
       이것이 막는 실패는 어느 파서가 방을 지었는지 신경 쓰지 않기 때문입니다. */
    {
        int unlit = 0;
        for (int i = 0; i < l.n_lights; i++) if (!l.lights[i].lit) unlit++;

        int worst = 0;
        for (int i = 0; i < l.n_ents; i++) {
            int n = 0;
            for (int j = 0; j < l.n_lights; j++) {
                const Light *L = &l.lights[j];
                float dx = (float)(l.ents[i].x - L->x),
                      dy = (float)(l.ents[i].y - L->y),
                      dz = (float)(l.ents[i].z - L->z);
                if (dx*dx + dy*dy + dz*dz < (float)L->radius * (float)L->radius) n++;
            }
            if (n > worst) worst = n;
        }
        printf("      %d lamp(s), %d of them unlit; the most reaching one marker is %d\n",
               l.n_lights, unlit, worst);
        ok(l.n_lights > 0, "the level declares a lamp");
        ok(unlit == 0, "and a text `light` line is lit, unlike a brush one");
        ok(worst <= LVL_LAMP_MAX,
           "and no marker has more of them in reach than the cap can hold");
    }

    /* --- geometry --- */
    MeshBuf b;
    mb_init(&b, 32768);
    MdlRange ranges[LVL_MAX_RANGES];
    int nr = level_geometry(&b, &l, ranges, LVL_MAX_RANGES);

    printf("\n  geometry: %d verts, %d material ranges\n", b.count, nr);
    for (int i = 0; i < nr; i++)
        printf("    %-8s first %5d  count %5d\n",
               ranges[i].mat, ranges[i].first, ranges[i].count);

    ok(b.count > 0,       "geometry produced vertices");
    ok(nr > 0 && nr <= LVL_MAX_RANGES, "material ranges within bounds");

    int covered = 0;
    for (int i = 0; i < nr; i++) covered += ranges[i].count;
    ok(covered == b.count, "ranges cover every vertex exactly once");

    ok(b.count % 3 == 0, "vertex count is a whole number of triangles");

    /* A triangle whose winding disagrees with its normal is culled from
       exactly the side it is lit on, so it reads as a missing face rather
       than as a shading bug. Every wall in the level was once wrong this way. */
    {
        int flipped = 0, up = 0, down = 0, side = 0;
        for (int i = 0; i + 2 < b.count; i += 3) {
            v3 p0 = v3f(b.v[i].px,   b.v[i].py,   b.v[i].pz);
            v3 p1 = v3f(b.v[i+1].px, b.v[i+1].py, b.v[i+1].pz);
            v3 p2 = v3f(b.v[i+2].px, b.v[i+2].py, b.v[i+2].pz);
            v3 geo = v3norm(v3cross(v3sub(p1, p0), v3sub(p2, p0)));
            v3 nrm = v3f(b.v[i].nx, b.v[i].ny, b.v[i].nz);
            if (nrm.y > 0.9f) up++; else if (nrm.y < -0.9f) down++; else side++;
            if (v3dot(geo, nrm) < 0.5f) flipped++;
        }
        printf("    %d up, %d down, %d side\n", up, down, side);
        ok(flipped == 0, "every triangle's winding agrees with its normal");
        ok(up > 0 && down > 0 && side > 0, "floors, ceilings and walls all present");
    }

    /* A pit is invisible if the sector above it is still drawn across the
       hole: the pit, its walls and its collision are all there and none of it
       can be seen. So no floor triangle at the room's height may contain a
       point that a lower sector governs.

       The pit is found rather than named, so this keeps working as the map
       is edited. */
    {
        int pit = -1;
        for (int i = 1; i < l.n_sectors; i++)
            if (l.sectors[i].floor < l.sectors[0].floor) { pit = i; break; }
        float px = 0.0f, pz = 0.0f;
        if (pit >= 0) {
            const Sector *ps = &l.sectors[pit];
            for (int k = 0; k < ps->n; k++) {
                px += ps->pts[k*2] * 0.01f;
                pz += ps->pts[k*2+1] * 0.01f;
            }
            px /= ps->n; pz /= ps->n;
        }
        float room_y = l.sectors[0].floor * 0.01f;
        int over = 0;
        for (int i = 0; pit >= 0 && i + 2 < b.count; i += 3) {
            if (b.v[i].ny < 0.9f) continue;                  /* floors only */
            if (fabsf(b.v[i].py - room_y) > 0.001f) continue;
            float ax = b.v[i].px,   az = b.v[i].pz;
            float bx = b.v[i+1].px, bz = b.v[i+1].pz;
            float cx = b.v[i+2].px, cz = b.v[i+2].pz;
            /* Area first: a degenerate triangle satisfies every side test at
               once and would read as covering the whole map. */
            float a2 = (bx-ax)*(cz-az) - (cx-ax)*(bz-az);
            if (a2 > -1e-6f && a2 < 1e-6f) continue;
            float d0 = (bx-ax)*(pz-az) - (bz-az)*(px-ax);
            float d1 = (cx-bx)*(pz-bz) - (cz-bz)*(px-bx);
            float d2 = (ax-cx)*(pz-cz) - (az-cz)*(px-cx);
            if ((d0 >= 0 && d1 >= 0 && d2 >= 0) ||
                (d0 <= 0 && d1 <= 0 && d2 <= 0)) over++;
        }
        ok(pit >= 0, "the level has a sunken sector to test");
        okf(over == 0, "no floor is drawn over it", (float)over, 0.0f);
    }

    /* --- ground queries -----------------------------------------------------
       Derived from the level rather than written out, because `arena` is a map
       somebody edits. Pinning these to its numbers meant every map edit broke
       the test suite, which teaches you to ignore it. What matters is the
       rules, not this week's floor heights. */
    float f, c;
    printf("\n");

    const Sector *room = &l.sectors[0];
    float rf = room->floor * 0.01f, rc = room->ceil * 0.01f;

    /* Centroid of the first sector, which is the room every level starts as. */
    float rx = 0.0f, rz = 0.0f;
    for (int i = 0; i < room->n; i++) {
        rx += room->pts[i*2] * 0.01f;
        rz += room->pts[i*2+1] * 0.01f;
    }
    rx /= room->n; rz /= room->n;

    ok(level_ground(&l, rx, rz, rf, 10.0f, &f, &c),
       "the room's centroid is inside a sector");
    okf(fabsf(f - rf) < 0.001f, "and reports the room's own floor", f, rf);
    okf(fabsf(c - rc) < 0.001f, "and the room's own ceiling", c, rc);

    ok(!level_ground(&l, 1000.0f, 1000.0f, 0.0f, 10.0f, &f, &c),
       "far outside the map is in no sector");

    /* Overlapping sectors: whichever was declared last governs the point,
       whether it is above the room (a platform) or below it (a pit). */
    {
        int checked = 0, wrong = 0;
        for (int i = 1; i < l.n_sectors; i++) {
            const Sector *sec = &l.sectors[i];
            float cx = 0.0f, cz = 0.0f;
            for (int k = 0; k < sec->n; k++) {
                cx += sec->pts[k*2] * 0.01f;
                cz += sec->pts[k*2+1] * 0.01f;
            }
            cx /= sec->n; cz /= sec->n;
            /* Only sectors nothing later overlaps can be predicted this way. */
            if (level_sector_at(&l, cx, cz) != i) continue;
            checked++;
            float want = sec->floor * 0.01f;
            if (!level_ground(&l, cx, cz, want, 10.0f, &f, &c)) { wrong++; continue; }
            if (fabsf(f - want) > 0.001f) wrong++;
        }
        ok(checked > 0, "the level has overlapping sectors to test");
        okf(wrong == 0, "each one governs its own centre", (float)wrong, 0.0f);
    }

    /* A floor out of stepping reach must be refused rather than silently
       dropping through to whatever is underneath. */
    {
        int high = 0;
        for (int i = 1; i < l.n_sectors; i++)
            if (l.sectors[i].floor > l.sectors[high].floor) high = i;
        const Sector *sec = &l.sectors[high];
        float cx = 0.0f, cz = 0.0f;
        for (int k = 0; k < sec->n; k++) {
            cx += sec->pts[k*2] * 0.01f;
            cz += sec->pts[k*2+1] * 0.01f;
        }
        cx /= sec->n; cz /= sec->n;
        float top = sec->floor * 0.01f;
        ok(top - rf > PLAYER_STEP, "the level has a ledge too high to step onto");
        ok(!level_ground(&l, cx, cz, rf, PLAYER_STEP, &f, &c),
           "which cannot be stood on from the floor below");
        ok(level_ground(&l, cx, cz, top, PLAYER_STEP, &f, &c) &&
           fabsf(f - top) < 0.001f, "but can be once you are up there");
    }

    /* --- traces --- */
    printf("\n");
    float t; v3 n;

    v3 eye = v3f(rx, rf + 1.7f, rz);
    ok(level_trace(&l, eye, v3f(0, -1, 0), 120.0f, &t, &n),
       "straight down hits the floor");
    okf(fabsf(t - 1.7f) < 0.02f, "the floor is 1.7 below the eye", t, 1.7f);
    okf(n.y > 0.9f, "floor normal points up", n.y, 1.0f);

    ok(level_trace(&l, eye, v3f(0, 1, 0), 120.0f, &t, &n),
       "straight up hits the ceiling");
    okf(fabsf(t - (rc - rf - 1.7f)) < 0.02f, "at the room's ceiling height",
        t, rc - rf - 1.7f);
    okf(n.y < -0.9f, "ceiling normal points down", n.y, -1.0f);

    ok(level_trace(&l, eye, v3f(-1, 0, 0), 200.0f, &t, &n),
       "sideways hits a wall");
    okf(fabsf(n.y) < 0.2f, "wall normal is horizontal", n.y, 0.0f);

    /* --- a hit normal must FACE the ray that found it ----------------------
       Checking only that the wall normal is horizontal was not enough, and the
       gap shipped: nearest_edge_normal returns the sector's OUTWARD normal,
       which for a player standing inside a room points into the wall. All four
       walls came back pointing away from the shooter while the floor and
       ceiling were correct -- those two derive their normal from the ray's own
       direction rather than from the polygon.

       Nothing crashed and nothing looked wrong in the geometry, so the only
       symptom was impact particles being thrown into the wall and never seen.
       Every direction is checked here, not just one, because the previous
       version passed on the single case it happened to try.
       충돌 법선은 자신을 찾아낸 광선을 *향해야* 합니다. 벽 법선이 수평인지만 검사한
       것으로는 부족했고 그 틈이 출시되었습니다. 아무것도 중단되지 않고 지오메트리도
       멀쩡해 보였으므로, 유일한 증상은 피격 파티클이 벽 속으로 던져져 보이지 않는
       것이었습니다. 이전 버전이 우연히 시도한 한 가지 경우에서 통과했기 때문에, 여기서는
       한 방향이 아니라 모든 방향을 검사합니다. */
    {
        v3 DIRS[6] = { v3f( 1,0,0), v3f(-1,0,0), v3f(0,0, 1),
                       v3f(0,0,-1), v3f(0, 1,0), v3f(0,-1,0) };
        int away = 0, tested = 0;
        for (int i = 0; i < 6; i++) {
            float tt; v3 nn;
            if (!level_trace(&l, eye, DIRS[i], 200.0f, &tt, &nn)) continue;
            tested++;
            if (v3dot(nn, DIRS[i]) > 0.0f) away++;
        }
        ok(tested >= 5, "the fixture presents surfaces in every direction");
        okf(away == 0,
            "every hit normal faces the ray, so particles spawn outward",
            (float)away, 0.0f);
    }

    /* --- level_blocked must agree with level_trace -------------------------
       level_blocked is the visibility half of level_trace, split out so a
       line-of-sight test stops paying for the bisection that locates the hit
       and the edge scan that derives a normal there. It is the call the monster
       AI now makes once per monster instead of a full trace.

       The two share ::march precisely so they cannot disagree, and this is what
       holds that claim to account. A disagreement has one visible symptom and
       it is a bad one: level_blocked reporting clear where level_trace reports
       a hit is a monster that sees, and therefore shoots, through a wall --
       exactly what the caster's second line-of-sight check exists to prevent.

       Rays are cast in every direction from a point inside the map rather than
       along the axes only, because a disagreement would come from the marcher's
       step landing differently on a grazing angle, and an axis-aligned ray is
       the case least likely to expose that.

       level_blocked는 level_trace의 가시성 부분을 분리한 것으로, 시야 판정이 충돌 지점을
       찾는 이분 탐색과 그곳의 법선을 유도하는 모서리 순회 비용을 더 이상 치르지 않게
       합니다. 몬스터 AI가 이제 온전한 판정 대신 몬스터마다 한 번씩 호출하는 함수입니다.

       두 함수는 서로 어긋날 수 없도록 ::march를 공유하며, 이 검사가 그 주장을 실제로
       책임집니다. 어긋남의 증상은 하나뿐이고 그것은 나쁜 증상입니다. level_trace가 충돌을
       보고하는 곳에서 level_blocked가 뚫려 있다고 답하면, 그것은 벽을 통해 보고 따라서 벽을
       통해 쏘는 몬스터입니다. 캐스터의 두 번째 시야 검사가 막으려는 것이 정확히 그것입니다.

       축 방향만이 아니라 모든 방향으로 광선을 쏩니다. 어긋남이 생긴다면 스치는 각도에서
       마처의 간격이 다르게 놓이는 데서 올 텐데, 축에 정렬된 광선은 그것을 드러낼 가능성이
       가장 낮은 경우이기 때문입니다. */
    {
        int disagree = 0, cast = 0, blocked_n = 0;
        unsigned rng = 0x13579bdfu;
        for (int i = 0; i < 400; i++) {
            rng = rng * 1664525u + 1013904223u;
            float a = ((rng >> 8) & 0xffff) / 65536.0f * 6.2831853f;
            rng = rng * 1664525u + 1013904223u;
            float e = (((rng >> 8) & 0xffff) / 65536.0f - 0.5f) * 1.6f;

            v3 d = v3norm(v3f(cosf(a), e, sinf(a)));
            float tt; v3 nn;
            int hit = level_trace(&l, eye, d, 60.0f, &tt, &nn);
            int blk = level_blocked(&l, eye, d, 60.0f);
            cast++;
            if (blk) blocked_n++;
            if (hit != blk) disagree++;
        }
        ok(cast > 0 && blocked_n > 0,
           "the fixture has rays that actually hit something");
        okf(disagree == 0,
            "level_blocked agrees with level_trace on every ray",
            (float)disagree, 0.0f);
    }

    /* --- partial overlap ---------------------------------------------------
       Two sectors overlapping only part of an edge -- the case the whole
       last-wins authoring model invites, and the one that was broken.

           room   -400..400 x -400..400
           slab    100..900 x -100..100     <- a corridor pushed out east

       The room's east edge is x=400, running z=-400..400. The slab meets it
       only over z=-100..100. There the two sectors join and nothing should be
       solid; north and south of that the same edge faces the void and must be
       a full-height wall.

       The old rule asked "what is beyond this edge?" once, at the edge's
       midpoint, and applied the answer to the whole edge. The midpoint here
       lands inside the slab, so the entire east wall vanished along its full
       length even though only a quarter of it was actually open -- and with
       differing heights what survived was just the step, which is why the
       opening looked as tall as the overlap. */
    printf("\n");
    {
        Level o;
        Level zero = {0};
        o = zero;
        o.n_sectors = 2;

        Sector *room = &o.sectors[0];
        room->n = 4;
        short rp[8] = { -400,-400,  400,-400,  400,400,  -400,400 };
        for (int i = 0; i < 8; i++) room->pts[i] = rp[i];
        room->floor = 0; room->ceil = 600;

        Sector *slab = &o.sectors[1];
        slab->n = 4;
        short qp[8] = { 100,-100,  900,-100,  900,100,  100,100 };
        for (int i = 0; i < 8; i++) slab->pts[i] = qp[i];
        slab->floor = 0; slab->ceil = 600;

        EdgeSpan sp[LVL_MAX_SPANS];
        int n = level_edge_spans(&o, 0, 1, sp, LVL_MAX_SPANS);

        /* Edge 1 runs z=-400..400, so t is linear in z. */
        float ez[3]        = { -300.0f, 0.0f, 300.0f };
        float ewant[3]     = { 6.0f,    0.0f, 6.0f };
        const char *ename[3] = {
            "east edge south of the opening is a full wall",
            "east edge across the opening is not solid",
            "east edge north of the opening is a full wall",
        };
        for (int k = 0; k < 3; k++) {
            float t = (ez[k] + 400.0f) / 800.0f, solid = 0.0f;
            for (int i = 0; i < n; i++)
                if (t >= sp[i].t0 && t <= sp[i].t1) solid += sp[i].y1 - sp[i].y0;
            okf(fabsf(solid - ewant[k]) < 0.01f, ename[k], solid, ewant[k]);
        }

        /* And through the geometry, which is what the player walks into. */
        MeshBuf ob;
        mb_init(&ob, 8192);
        level_geometry(&ob, &o, 0, 0);

        /* The plane x=4 is also touched by the room's floor corners and by
           the slab's own north and south walls, so select on the normal too:
           only the east wall faces along x. */
        int on_east = 0, in_gap = 0;
        for (int i = 0; i < ob.count; i++) {
            if (fabsf(ob.v[i].px - 4.0f) > 0.001f) continue;
            if (fabsf(ob.v[i].nx) < 0.9f) continue;
            on_east++;
            if (fabsf(ob.v[i].pz) < 0.99f) in_gap++;
        }
        /* Two quads, six vertices each: the wall either side of the opening. */
        okf(on_east == 12, "east wall is two pieces, not none and not one",
            (float)on_east, 12.0f);
        okf(in_gap == 0, "and no wall is built across the opening",
            (float)in_gap, 0.0f);
        mb_free(&ob);
    }

    /* --- a box dropped into a room ------------------------------------------
       A sector laid inside another is a platform. If it keeps a lower ceiling
       than the room it sits in, the volume above it is solid and gets built
       as walls -- a pillar standing on the box, which is what a reported bug
       looked like. The editor now inherits the ceiling from the sector under
       the cursor; this is the rule that makes that the right thing to do. */
    printf("\n");
    {
        for (int variant = 0; variant < 2; variant++) {
            Level o;
            Level zero = {0};
            o = zero;
            o.n_sectors = 2;

            Sector *rm = &o.sectors[0];
            short rp[8] = { -800,-800,  800,-800,  800,800,  -800,800 };
            for (int i = 0; i < 8; i++) rm->pts[i] = rp[i];
            rm->n = 4; rm->floor = 0; rm->ceil = 600;

            Sector *bx = &o.sectors[1];
            short bp[8] = { -200,-200,  200,-200,  200,200,  -200,200 };
            for (int i = 0; i < 8; i++) bx->pts[i] = bp[i];
            bx->n = 4; bx->floor = 50;
            bx->ceil = variant ? 300 : 600;   /* 300 is the old hardcoded value */

            MeshBuf ob;
            mb_init(&ob, 16384);
            level_geometry(&ob, &o, 0, 0);

            int above = 0;
            for (int i = 0; i < ob.count; i++) {
                if (fabsf(ob.v[i].ny) > 0.5f) continue;          /* walls only */
                if (fabsf(ob.v[i].px) > 2.01f) continue;
                if (fabsf(ob.v[i].pz) > 2.01f) continue;
                if (ob.v[i].py > 0.55f) above++;                 /* above its top */
            }
            if (variant == 0)
                okf(above == 0, "a box sharing the room's ceiling has nothing on top",
                    (float)above, 0.0f);
            else
                ok(above > 0, "and a lower ceiling really does build that pillar");
            mb_free(&ob);
        }
    }

    /* --- worst-case build cost ---------------------------------------------
       Cutting edges made level_geometry quadratic in sector count: every edge
       is now tested against every other sector's outline. The editor rebuilds
       on every frame of a drag, so this needs to stay well inside a frame at
       the format's limit, not just on a six-sector test map. */
    printf("\n");
    {
        Level big;
        Level zero = {0};
        big = zero;
        /* A full grid of overlapping squares -- every sector touching its
           neighbours, which is the expensive arrangement.

           The side is DERIVED from the cap rather than written as 8. It was 8
           because the cap is 64, which is the same coupling as writing the cap
           out twice: a build with a smaller LVL_MAX_SECTORS ran this loop past
           the end of the array, and gcc said so the first time one existed
           (-Waggressive-loop-optimizations, "iteration 1 invokes undefined
           behavior"). The comment above already claims this fixture sits at
           the format's limit; deriving it is what makes the claim true in
           every build rather than in the one it was written in.

           변의 길이를 8이라고 쓰지 않고 상한에서 *유도*합니다. 8이었던 것은 상한이
           64이기 때문이며, 이는 상한을 두 번 적어 두는 것과 같은 결합입니다. 더 작은
           LVL_MAX_SECTORS로 빌드하면 이 루프가 배열 끝을 넘어갔고, 그런 빌드가 처음
           생겼을 때 gcc가 그렇게 말했습니다(-Waggressive-loop-optimizations, "반복 1이
           정의되지 않은 동작을 유발합니다"). 위 주석은 이미 이 픽스처가 형식의 한계에
           있다고 주장합니다. 유도하는 것이 그 주장을, 그것이 작성된 빌드에서만이 아니라
           모든 빌드에서 참이게 만듭니다. */
        int side = 1;
        while ((side + 1) * (side + 1) <= LVL_MAX_SECTORS) side++;

        for (int gz = 0; gz < side; gz++) {
            for (int gx = 0; gx < side; gx++) {
                Sector *s = &big.sectors[big.n_sectors++];
                short x0 = (short)(gx * 300 - 1200), z0 = (short)(gz * 300 - 1200);
                short x1 = (short)(x0 + 400),        z1 = (short)(z0 + 400);
                short p[8] = { x0,z0,  x1,z0,  x1,z1,  x0,z1 };
                for (int i = 0; i < 8; i++) s->pts[i] = p[i];
                s->n = 4;   /* four, and LVL_MAX_PTS is never below that */
                s->floor = (short)((gx + gz) * 20);
                s->ceil = 600;
            }
        }

        MeshBuf bb;
        mb_init(&bb, 65536);
        clock_t t0 = clock();
        int reps = 20;
        for (int i = 0; i < reps; i++) { mb_reset(&bb); level_geometry(&bb, &big, 0, 0); }
        float ms = (float)(clock() - t0) * 1000.0f / CLOCKS_PER_SEC / reps;
        printf("  %d sectors, %d verts: %.2f ms per rebuild\n",
               big.n_sectors, bb.count, ms);
        ok(ms < 16.0f, "a full level rebuilds inside one frame");
        mb_free(&bb);
    }

    /* --- the bounding-box fast path must never change an ANSWER ------------
       point_in_sector rejects against a cached box before running the
       crossing test. That is an optimisation, and an optimisation that
       disagrees with the thing it optimises is just a bug with better timing.

       Two properties are checked, and the second is the one that actually bit:

         1. With bounds computed, every query agrees with the same query on a
            sector whose bounds were never computed.
         2. A Level assembled FIELD BY FIELD -- which every headless fixture in
            tools/ does, and which no one thinks of as an initialisation step --
            is correct without anyone calling level_bounds.

       Property 2 failed once already. `Level l = {0}` leaves a box of
       (0,0)-(0,0), which is a perfectly valid box that contains only the
       origin, so every sector rejected every point and the entire level became
       solid. Five test suites went red at once. The fix was a separate
       has_bounds flag, whose zero value means "not computed" -- so the safe
       answer is the one zeroed memory already gives. */
    {
        Level hand = {0};
        Sector *s = &hand.sectors[hand.n_sectors++];
        short p[8] = { -1200,-1200,  1200,-1200,  1200,1200,  -1200,1200 };
        for (int i = 0; i < 8; i++) s->pts[i] = p[i];
        s->n = 4; s->floor = 0; s->ceil = 600;

        ok(!s->has_bounds,
           "a hand-built sector starts with no bounds -- zeroed means unknown");

        /* Sample a grid over and around the square, recording the answer
           before and after the bounds exist. */
        int disagreed = 0, inside_seen = 0, outside_seen = 0;
        float fa, ca, fb, cb;
        float probe[9] = { -30.0f, -12.0f, -11.9f, -6.0f, 0.0f,
                            6.0f, 11.9f, 12.0f, 30.0f };

        int before[9][9];
        for (int i = 0; i < 9; i++)
            for (int j = 0; j < 9; j++) {
                before[i][j] = level_ground(&hand, probe[i], probe[j],
                                            -1e9f, 1e9f, &fa, &ca);
                if (before[i][j]) inside_seen++; else outside_seen++;
            }

        level_bounds(s);
        ok(s->has_bounds, "and level_bounds marks them computed");

        for (int i = 0; i < 9; i++)
            for (int j = 0; j < 9; j++) {
                int after = level_ground(&hand, probe[i], probe[j],
                                         -1e9f, 1e9f, &fb, &cb);
                if (after != before[i][j]) disagreed++;
            }

        /* Both outcomes must appear, or the comparison proved nothing: a grid
           entirely outside the square would agree trivially. */
        ok(inside_seen > 0 && outside_seen > 0,
           "the probe grid straddles the sector's edge");
        okf(disagreed == 0,
            "the box rejects only points the crossing test rejects anyway",
            (float)disagreed, 0.0f);
    }

    /* --- the levels are a fixture and not a shipment ----------------------
     *
     * THE GAME SHIPS ONE MAP. `arena`, `vault` and `dm03` are the old campaign
     * in the sector format that came before brush maps, and world.h says what
     * became of them: "the old campaign levels still load by name; nothing
     * reaches them by walking forward". No shipped source names one. So
     * bake.ps1 keeps the file and drops the bytes, which is what $mapsNotBaked
     * already does for atrium.
     *
     * NOTHING WOULD HAVE NOTICED, which is why this is here. Every tool is a
     * HOT_RELOAD build and reads `assets\levels.txt` from disk, so the whole
     * suite goes on passing whether the blob holds the campaign or nothing at
     * all -- the exclusion could be reverted, or silently stop working, and the
     * only evidence would be a binary two kilobytes larger than anybody meant.
     * ::data_baked reads the blob in either build, which is exactly the hole it
     * exists to reach through; ::data_text reads the file. The pair is the
     * claim: the fixture survives and the shipment does not carry it.
     *
     * *게임은 맵 하나를 출하합니다.* `arena`, `vault`, `dm03`은 브러시 맵 이전의 섹터 형식으로
     * 된 옛 캠페인이고, world.h가 그 결말을 적었습니다. "옛 캠페인 레벨은 여전히 이름으로
     * 불러들여지지만, 앞으로 걸어서 그곳에 닿는 것은 없다." 출하 소스는 그중 하나도 이름 대지
     * 않습니다. 그래서 bake.ps1은 파일을 남기고 바이트를 버립니다. $mapsNotBaked가 atrium에
     * 대해 이미 하는 일입니다.
     * *무엇도 알아채지 못했을 것이고*, 그것이 이 검사가 있는 이유입니다. 모든 도구는 HOT_RELOAD
     * 빌드이고 `assets\levels.txt`를 디스크에서 읽으므로, 블롭이 캠페인을 담든 아무것도 담지
     * 않든 스위트 전체가 계속 통과합니다. 제외가 되돌려지거나 조용히 작동을 멈춰도, 증거는
     * 아무도 의도하지 않은 2킬로바이트 큰 바이너리뿐입니다. ::data_baked는 어느 빌드에서나
     * 블롭을 읽으며, 그것이 이 함수가 닿으라고 존재하는 구멍입니다. ::data_text는 파일을
     * 읽습니다. 그 쌍이 곧 주장입니다. 픽스처는 살아남고 출하물은 그것을 지고 가지 않습니다. */
    {
        const char *blob = data_baked(DATA_LEVELS);
        const char *file = data_text(DATA_LEVELS);
        int blob_len = blob ? (int)strlen(blob) : 0;
        int file_len = file ? (int)strlen(file) : 0;

        printf("      levels: %d byte(s) baked, %d read from disk\n",
               blob_len, file_len);
        okd(blob_len == 0, "the shipped blob carries no sector level",
            blob_len, 0);
        ok(file_len > 1000, "and the authoring build still reads them from disk");
    }

    /* --- the eight integers of a `light` line ------------------------------
       THIS CHECK READ ARENA'S FOUR, THEN LOST THEM, AND HAS THEM BACK. What it
       proves is a parser property: a `light` line is eight integers, the
       reader has to consume exactly those eight, and a miscount leaves it
       mid-line so every declaration after it is read as garbage -- silently,
       because the level still loads and is merely lit wrongly.

       While the lamps were deleted the check had no input, and what stood in
       its place was the weaker fact that every shipped level declared zero.
       Its own note said what to do if they ever came back: "a fixture for that
       line is the first thing to write back". This is that.

       THE LAST LAMP IS THE WITNESS. Reading seven integers instead of eight, or
       nine, does not go wrong where it happens -- it goes wrong afterwards, so
       the first lamp can be perfect while the fourth is nonsense. All four are
       compared field for field and the fourth is the one that would fail.

       AND THE ENTITIES AROUND THEM. A parse that went wrong in the lamp block
       would eat the declarations either side of it, which is why the count of
       entities is still asserted here rather than left to the checks above.

       *이 검사는 arena의 넷을 읽었고, 그것을 잃었고, 되찾았습니다.* 증명하는 것은 파서의
       성질입니다. `light` 줄은 정수 여덟 개이고 읽기는 정확히 그 여덟 개를 소비해야 하며,
       개수를 잘못 세면 줄 중간에 남아 이후의 모든 선언이 쓰레기 값으로 읽힙니다. 조용히
       그렇습니다. 레벨은 여전히 로드되고 다만 잘못 조명될 뿐이기 때문입니다.
       등이 삭제되어 있는 동안 이 검사에는 입력이 없었고, 그 자리에는 모든 출하 레벨이 0을
       선언한다는 더 약한 사실이 있었습니다. 그 주석 자신이 등이 돌아오면 무엇을 할지 적어
       두었습니다. "그 줄을 위한 픽스처가 가장 먼저 쓰여야 할 것입니다." 이것이 그것입니다.
       *마지막 등이 증인입니다.* 여덟 대신 일곱이나 아홉을 읽는 것은 그 자리에서 어긋나지
       않고 *그 뒤에서* 어긋나므로, 첫 등이 완벽한 채로 넷째가 헛소리일 수 있습니다. 넷 모두를
       필드 단위로 비교하며, 실패할 것은 넷째입니다. */
    {
        static const char *NAMES[] = { "arena", "vault", "dm03" };
        int declared = 0, loaded = 0, ents = 0;

        for (int i = 0; i < (int)(sizeof(NAMES)/sizeof(NAMES[0])); i++) {
            Level lit;
            if (!level_load(NAMES[i], &lit)) continue;
            loaded++;
            declared += lit.n_lights;
            ents     += lit.n_ents;
        }

        int want = (int)(sizeof(NAMES)/sizeof(NAMES[0]));
        okd(loaded == want, "every shipped text level loads for the light check",
            loaded, want);
        okd(declared > 0, "and at least one of them declares a point light",
            declared, 1);

        /* assets/levels.txt, `l arena`, field for field. */
        static const short WANT[4][8] = {
            {     0,  380, -1100,  900, 255, 190, 110, 115 },
            { -1500,  300,   200,  800, 120, 170, 255,  90 },
            {  1400,  300,   900,  750, 120, 170, 255,  85 },
            {     0,  300,  -400,  600, 255, 140,  70,  70 },
        };
        Level a;
        int matched = 0;
        if (level_load("arena", &a)) {
            for (int i = 0; i < 4 && i < a.n_lights; i++) {
                const Light *L = &a.lights[i];
                const short *w = WANT[i];
                if (L->x == w[0] && L->y == w[1] && L->z == w[2] &&
                    L->radius == w[3] && L->r == w[4] && L->g == w[5] &&
                    L->b == w[6] && L->power == w[7]) matched++;
                else
                    printf("      lamp %d read %d %d %d %d  %d %d %d  %d\n", i,
                           L->x, L->y, L->z, L->radius, L->r, L->g, L->b, L->power);
            }
        }
        okd(matched == 4, "and every field of every lamp survived the read",
            matched, 4);

        /* The reader still has to keep its place through the lamp block.
           Entities are declared around it, so a parse that went wrong there
           would eat them.
           읽기는 등이 앉은 자리를 지나면서도 위치를 지켜야 합니다. 그 둘레에 엔티티가
           선언되어 있으므로, 그곳에서 잘못된 파싱은 그것들을 먹어 치웁니다. */
        ok(ents > 0,
           "and the entities around them still parsed");
    }

    /* --- hazard floors -----------------------------------------------------
       `hurt <dps>` makes a floor damage whatever stands on it. Two properties
       matter and neither is visible from inside the game until a player is
       standing in lava wondering why nothing is happening:

       A sector with no `hurt` must read as exactly zero, or every floor in
       every existing level becomes lethal the moment the field is added.

       And the LAST-WINS rule has to apply here as it does to floor height,
       because that is what allows a safe platform in the middle of a lava
       pit -- which is the only thing that makes a lava room playable rather
       than merely a wall.

       Built by hand rather than read from the shipped level, for the reason
       the fixture note at the top of this file gives: a test that asserts this
       week's map goes red the moment somebody edits it.

       `hurt <dps>`는 바닥이 그 위에 선 대상에게 피해를 주게 합니다. 두 가지 성질이
       중요하며, 둘 다 플레이어가 용암 위에 서서 왜 아무 일도 없는지 의아해하기
       전까지는 게임 안에서 보이지 않습니다.

       `hurt`가 없는 섹터는 정확히 0으로 읽혀야 합니다. 그렇지 않으면 이 필드가 추가되는
       순간 기존 모든 레벨의 모든 바닥이 치명적이 됩니다.

       그리고 바닥 높이와 마찬가지로 마지막 선언 우선 규칙이 적용되어야 합니다. 그것이
       용암 구덩이 한가운데의 안전한 발판을 가능하게 하며, 그것만이 용암 방을 단순한 벽이
       아니라 플레이 가능한 곳으로 만듭니다.

       이 파일 상단의 픽스처 참고 사항이 밝히는 이유로, 배포되는 레벨을 읽지 않고 손으로
       만듭니다. 이번 주의 맵을 단언하는 테스트는 누군가 그것을 편집하는 순간
       빨간불이 됩니다. */
    {
        Level h;
        Level zero = {0};
        h = zero;

        /* A big safe room. */
        Sector *room = &h.sectors[h.n_sectors++];
        short rp[8] = { -1000,-1000, 1000,-1000, 1000,1000, -1000,1000 };
        for (int i = 0; i < 8; i++) room->pts[i] = rp[i];
        room->n = 4; room->floor = 0; room->ceil = 600; room->hurt = 0;

        /* A lava pool inside it. */
        Sector *lava = &h.sectors[h.n_sectors++];
        short lp[8] = { -500,-500, 500,-500, 500,500, -500,500 };
        for (int i = 0; i < 8; i++) lava->pts[i] = lp[i];
        lava->n = 4; lava->floor = -40; lava->ceil = 600; lava->hurt = 20;

        /* And a safe platform in the middle of the lava, declared last. */
        Sector *isle = &h.sectors[h.n_sectors++];
        short ip[8] = { -150,-150, 150,-150, 150,150, -150,150 };
        for (int i = 0; i < 8; i++) isle->pts[i] = ip[i];
        isle->n = 4; isle->floor = 20; isle->ceil = 600; isle->hurt = 0;

        for (int i = 0; i < h.n_sectors; i++) level_bounds(&h.sectors[i]);
        level_grid_build(&h);

        okf(level_hazard_at(&h, 0.0f, 0.0f, 0.0f) == 0,
            "the safe island in the lava hurts nothing",
            (float)level_hazard_at(&h, 0.0f, 0.0f, 0.0f), 0.0f);

        okf(level_hazard_at(&h, 3.5f, 0.0f, 0.0f) == 20,
            "the lava around it does",
            (float)level_hazard_at(&h, 3.5f, 0.0f, 0.0f), 20.0f);

        okf(level_hazard_at(&h, 8.0f, 0.0f, 8.0f) == 0,
            "and the room outside the lava does not",
            (float)level_hazard_at(&h, 8.0f, 0.0f, 8.0f), 0.0f);

        okf(level_hazard_at(&h, 500.0f, 0.0f, 500.0f) == 0,
            "a point outside the map is not a hazard either",
            (float)level_hazard_at(&h, 500.0f, 0.0f, 500.0f), 0.0f);

        /* A level parsed with no `hurt` anywhere must be entirely safe. This
           is the check that would catch the field being left uninitialised by
           the sector parser, which would make every floor hazardous by
           whatever happened to be on the stack.
           `hurt`가 전혀 없는 레벨은 완전히 안전해야 합니다. 섹터 파서가 이 필드를
           초기화하지 않고 두는 경우를 잡아내는 검사이며, 그렇게 되면 스택에 남아 있던
           값에 따라 모든 바닥이 위험해집니다. */
        Level plain;
        if (level_load("arena", &plain)) {
            int any = 0;
            for (int i = 0; i < plain.n_sectors; i++)
                if (plain.sectors[i].hurt != 0) any = 1;
            ok(!any, "a level that authors no hazard has none");
        }
    }

    mb_free(&b);

    /* --- a locked door wears its key -------------------------------------
     *
     * The level text says `wall_door` and the KEY decides the colour, so the
     * author states the requirement once. What this guards is the version
     * where they state it twice and the two disagree: a red door that opens
     * with the blue card is a level lying to the player about its own rules,
     * and lying convincingly, because a picture is the kind of thing a player
     * trusts without checking.
     *
     * Checked through the SECTOR the door names rather than by re-deriving the
     * material here, because re-deriving it would be a second copy of the rule
     * and the two copies could agree while both being wrong.
     *
     * 레벨 텍스트는 `wall_door`라고 쓰고 *열쇠*가 색을 정하므로, 작성자는 요구 사항을 한
     * 번만 말합니다. 이 검사가 막는 것은 두 번 말했다가 둘이 어긋나는 경우입니다. 파란
     * 카드로 열리는 빨간 문은 레벨이 자기 규칙에 대해 설득력 있게 거짓말하는 것입니다.
     * 여기서 재질을 다시 유도하지 않고 문이 지목한 *섹터*를 통해 검사하는 이유는, 다시
     * 유도하면 규칙의 사본이 둘이 되고 둘 다 틀리면서 서로 일치할 수 있기 때문입니다.
     */
    {
        Level kd;
        if (level_load("arena", &kd)) {
            int keyed = 0, coloured = 0, generic_left = 0;
            for (int i = 0; i < kd.n_doors; i++) {
                const DoorDef *d = &kd.doors[i];
                if (d->key == KEY_NONE) continue;
                if (d->sector < 0 || d->sector >= kd.n_sectors) continue;
                keyed++;

                const char *m = kd.sectors[d->sector].mat_wall;
                int ml = 0;
                while (ml < LVL_MAT && m[ml]) ml++;

                if (txt_is(m, ml, "wall_door")) generic_left++;

                const char *want = (d->key & KEY_RED)    ? "door_red"
                                 : (d->key & KEY_BLUE)   ? "door_blue"
                                 : (d->key & KEY_YELLOW) ? "door_yellow" : "";
                if (txt_is(m, ml, want)) coloured++;
            }

            ok(keyed > 0, "the arena has a locked door to check");
            okd(generic_left == 0,
                "no locked door is left wearing the generic face",
                generic_left, 0);
            okd(coloured == keyed, "and every one wears its own key's colour",
                coloured, keyed);
        }
    }


    /* --- an entity's optional numbers do not eat the next statement -------
     *
     * Entities may now carry numbers after their position, and the parser
     * finds out how many by TRYING to read one and stopping when the next
     * token is not a number. That is only safe because txt_read_int leaves the
     * stream exactly where it found it on failure -- and because no statement
     * in this format begins with a number.
     *
     * The second half of that is a property of the LANGUAGE, not of the
     * parser, and nothing else states it. A statement that began with a number
     * would be silently swallowed as the previous entity's parameter, and the
     * symptom would be a missing sector or a lost light somewhere further down
     * the file, nowhere near the entity that ate it.
     *
     * WHICH IS WHY THE CHECK IS "EVERY PARAMETER IS ZERO". No shipped level
     * writes one yet, so a non-zero parameter today can only have come from the
     * parser consuming something that was not offered to it. When the first
     * real parameter is authored this check moves to naming it -- but until
     * then this is the strongest statement available, and it costs nothing.
     *
     * 엔티티는 이제 위치 뒤에 수치를 담을 수 있고, 파서는 하나를 *읽어 보고* 다음 토큰이
     * 숫자가 아니면 멈추는 방식으로 개수를 알아냅니다. 이것이 안전한 이유는 txt_read_int가
     * 실패 시 스트림을 발견한 그대로 남기기 때문이며, 또한 이 형식의 어떤 문장도 숫자로
     * 시작하지 않기 때문입니다.
     *
     * 후자는 파서가 아니라 *언어*의 성질이고 다른 어디에도 적혀 있지 않습니다. 숫자로
     * 시작하는 문장이 생기면 앞 엔티티의 파라미터로 조용히 삼켜지며, 증상은 파일 한참
     * 아래의 사라진 섹터나 잃어버린 광원으로 나타납니다. 그것을 먹은 엔티티 근처가
     * 아닙니다.
     *
     * 그래서 검사가 "모든 파라미터가 0"입니다. 아직 어떤 배포 레벨도 파라미터를 쓰지
     * 않으므로, 오늘 0이 아닌 파라미터는 파서가 주어지지 않은 것을 소비했다는 뜻뿐입니다.
     * 첫 실제 파라미터가 작성되면 이 검사는 그것을 지목하는 쪽으로 옮겨 갑니다.
     */
    {
        static const char *NAMES[] = { "arena", "vault" };
        int checked = 0, dirty = 0;

        for (int n = 0; n < (int)(sizeof(NAMES)/sizeof(NAMES[0])); n++) {
            Level el;
            if (!level_load(NAMES[n], &el)) continue;
            checked++;
            for (int i = 0; i < el.n_ents; i++)
                for (int k = 0; k < LVL_ENT_PARAMS; k++)
                    if (el.ents[i].p[k] != 0) {
                        if (!dirty)
                            printf("      '%s' entity %d ('%s') has p[%d] = %d\n",
                                   NAMES[n], i, el.ents[i].kind, k,
                                   el.ents[i].p[k]);
                        dirty++;
                    }
        }

        ok(checked > 0, "the shipped levels load for the parameter check");
        okd(dirty == 0,
            "no entity picked up a number the level never offered it",
            dirty, 0);
    }

    /* --- every monster is placeable as a spawner --------------------------
     *
     * ENGLISH
     * -------
     * THE PIPELINE THIS CHECKS HAS THREE OWNERS AND NO MEETING POINT. The FGD
     * offers `monster_spawner_<name>`; level.c strips `monster_` and stores the
     * rest in ::LVL_KIND bytes; enemy.c strips `spawner_` and asks
     * ::mon_type_for for the remainder, which compares whole names. Each step
     * is correct on its own and none of them can see the budget the other two
     * are spending, so the failure is a name that is simply too long and a
     * marker that resolves to nothing.
     *
     * It fails SILENTLY, which is why it is worth a check rather than a
     * playthrough: the spawner is skipped, the room stays empty, and the map
     * still loads without complaint. `monster_spawner_water_spirit` is twenty
     * characters after the first prefix comes off, was stored as
     * `spawner_water_s` when the kind shared LVL_MAT at 16, and asked for a monster called
     * `water_s`.
     *
     * Driven from ::MON_TYPES rather than from a list written here, so a
     * monster added tomorrow is checked without this file being edited -- and a
     * monster given a long name fails HERE, at the table that named it, rather
     * than in a room that quietly never fills.
     *
     * 한국어
     * ------
     * 이 검사가 보는 파이프라인에는 주인이 셋 있고 그들이 만나는 자리가 없습니다. FGD는
     * `monster_spawner_<name>`을 제공하고, level.c는 `monster_`를 떼어 나머지를 ::LVL_KIND
     * 바이트에 저장하며, enemy.c는 `spawner_`를 떼어 남은 것을 ::mon_type_for에 묻고 그것은
     * 이름 전체를 비교합니다. 각 단계는 저마다 옳고, 어느 것도 나머지 둘이 쓰고 있는 예산을
     * 볼 수 없습니다. 그래서 그 실패는 그저 너무 긴 이름이고 아무것으로도 해석되지 않는
     * 표식입니다.
     *
     * 이것은 *조용히* 실패하며, 그래서 플레이가 아니라 검사가 필요합니다. 스포너는
     * 건너뛰어지고, 방은 비어 있고, 맵은 여전히 불평 없이 로드됩니다.
     * `monster_spawner_water_spirit`는 첫 접두사를 뗀 뒤 스무 글자여서, 종류가 LVL_MAT를 함께 쓰며 16이던
     * 시절 `spawner_water_s`로 저장되었고 `water_s`라는 몬스터를 물었습니다.
     *
     * 이곳에 적은 목록이 아니라 ::MON_TYPES에서 몰아가므로, 내일 추가되는 몬스터도 이 파일을
     * 고치지 않고 검사됩니다. 그리고 긴 이름을 받은 몬스터는 조용히 채워지지 않는 방이
     * 아니라 그것을 이름 지은 표에서, *이곳에서* 실패합니다.
     */
    {
        printf("\n  --- spawner classnames survive the import ---\n");

        /* The two prefixes, each measured from its own literal: a hand-written
           8 here would be a third place for the same fact to live.
           접두사 둘이며 각각 자기 리터럴에서 잽니다. 이곳에 손으로 쓴 8은 같은 사실이 사는
           세 번째 자리가 됩니다. */
        static const char MON_PRE[]  = "monster_";
        static const char SPWN_PRE[] = "spawner_";
        const int mp = (int)sizeof(MON_PRE)  - 1;
        const int sp = (int)sizeof(SPWN_PRE) - 1;

        int lost = 0, worst = 0;
        const char *worst_name = "(none)";

        for (int t = 0; t < MON_TYPES; t++) {
            const char *name = mon_stats(t)->name;

            /* What the mapper places, spelled the way the FGD offers it. */
            char cn[128];
            int pos = txt_append_str(cn, (int)sizeof(cn), 0,   MON_PRE);
            pos     = txt_append_str(cn, (int)sizeof(cn), pos, SPWN_PRE);
            pos     = txt_append_str(cn, (int)sizeof(cn), pos, name);

            /* level.c: strip the family prefix, keep the rest in LVL_KIND.
               This is copy_name's operation, run on copy_name's buffer. */
            char kind[LVL_KIND];
            txt_copy(kind, LVL_KIND, cn + mp, -1);

            /* enemy.c: strip `spawner_` and resolve what is left. It wants
               the prefix matched AND something after it. */
            int named = txt_is(kind, sp, SPWN_PRE) && kind[sp];
            int got   = named ? mon_type_for(kind + sp) : -1;

            int need = pos - mp;      /* what had to fit, terminator apart */
            if (need > worst) { worst = need; worst_name = name; }

            if (got != t) {
                printf("      %-30s -> kind '%s' -> %d, wanted %d\n",
                       cn, kind, got, t);
                lost++;
            }
        }

        printf("      longest spawner kind: %d of %d bytes ('%s')\n",
               worst, LVL_KIND - 1, worst_name);

        okd(lost == 0,
            "every monster is reachable as monster_spawner_<name>", lost, 0);

        /* And the same question asked of the shipped levels, through the real
           import rather than a reproduction of it. The check above proves the
           BUDGET is wide enough for every monster; this one proves no level
           actually authored a classname that overran it -- an item or a kind
           this test does not know to build would be caught here and nowhere
           else. Released after each load: there are only ::LVL_BRUSH_SLOTS
           brush slots and holding four would evict rather than report.
           그리고 같은 질문을 출하 레벨들에게, 재현이 아니라 실제 임포트를 통해 묻습니다. 위의
           검사는 *예산*이 모든 몬스터에 대해 충분함을 증명하고, 이 검사는 어떤 레벨도 그것을
           넘는 classname을 실제로 작성하지 않았음을 증명합니다. 이 테스트가 만들 줄 모르는
           아이템이나 종류는 다른 어디도 아닌 이곳에서 잡힙니다. 로드마다 해제하는 이유는
           브러시 슬롯이 ::LVL_BRUSH_SLOTS개뿐이어서, 넷을 쥐고 있으면 보고가 아니라 축출이
           일어나기 때문입니다. */
        {
            static const char *const LEVELS[] = { "arena", "vault",
                                                  "atrium", "lqdm4" };
            const int n_levels = (int)(sizeof(LEVELS)/sizeof(LEVELS[0]));
            int before = diag_count(DIAG_ENT_KIND), seen = 0;

            for (int i = 0; i < n_levels; i++) {
                Level el;
                if (!level_load(LEVELS[i], &el)) continue;
                seen++;
                level_release(&el);
            }

            /* ALL of them, not one of them. `spire` was in this list and was
               deleted, and `seen > 0` would have gone on passing while the
               sweep quietly covered three levels instead of four -- the same
               silence this whole change is about, one level up.
               하나가 아니라 *전부*입니다. 이 목록에 있던 `spire`는 삭제되었고, `seen > 0`은
               훑기가 넷이 아니라 셋을 덮는 동안에도 계속 통과했을 것입니다. 이 변경 전체가
               다루는 바로 그 침묵이며, 한 단계 위에서 일어납니다. */
            okd(seen == n_levels,
                "the shipped levels load for the kind check", seen, n_levels);
            okd(diag_count(DIAG_ENT_KIND) == before,
                "and none of them authored a kind too long to store",
                diag_count(DIAG_ENT_KIND) - before, 0);
        }
    }

    /* --- teleporters -----------------------------------------------------
       The shipped arena has two, and the pair is the point: a trigger_teleport
       is half a mechanism and level.c refuses to store one whose
       info_teleport_destination did not resolve. So a count of two is not just
       "two volumes parsed" -- it is two volumes that FOUND the point they name.

       AND THE DESTINATION MUST NOT BE INSIDE ITS OWN VOLUME, which is the
       assertion worth having here. ::step_teleport leaves after one hop, so a
       teleporter that lands you back in itself does not hang the game -- it
       does something worse and quieter: it fires again on the very next frame,
       every frame, and the player is stuck in place hearing the sound over and
       over with no way to walk out. An author can draw that by accident and it
       is invisible in the editor.

       텔레포터입니다. 출하 아레나에 둘이 있고, *짝*이 요점입니다. trigger_teleport는 절반짜리
       기구이며 level.c는 info_teleport_destination이 해소되지 않은 것을 저장하지 않습니다.
       그러므로 둘이라는 개수는 "부피 둘이 파싱되었다"가 아니라 "부피 둘이 자기가 지목한 점을
       *찾았다*"입니다.

       *그리고 목적지는 자기 부피 안에 있어서는 안 되며*, 그것이 이곳에서 가질 값어치가 있는
       단언입니다. ::step_teleport는 한 번 도약한 뒤 나가므로, 자기 자신 안에 내려놓는
       텔레포터가 게임을 멈추지는 않습니다. 대신 더 나쁘고 더 조용한 일을 합니다. 바로 다음
       프레임에 다시 발동하고 매 프레임 그러하며, 플레이어는 그 자리에 붙들린 채 같은 소리를
       계속 들으면서 걸어 나갈 수 없습니다. 제작자가 실수로 그릴 수 있고 에디터에서는 보이지
       않습니다. */
    {
        Level tl;
        if (!level_load("lqdm4", &tl)) {
            ok(0, "the shipped arena loads for the teleport check");
        } else {
            printf("\n  teleporters: %d\n", tl.n_teleports);
            okd(tl.n_teleports == 2,
                "the arena's two teleporters both found their destination",
                tl.n_teleports, 2);

            int self = 0, at_origin = 0;
            for (int i = 0; i < tl.n_teleports; i++) {
                const TeleportDef *t = &tl.teleports[i];
                /* The point the PLAYER occupies, not the one the .map wrote:
                   the marker is the feet and Player::pos is the eye, so a
                   destination whose feet sit outside a volume can still have
                   its eye inside one. That is the case that loops.
                   .map이 적은 점이 아니라 *플레이어가 차지하는* 점입니다. 표식은
                   발이고 Player::pos는 눈이므로, 발이 부피 바깥인 목적지도 눈은
                   안에 있을 수 있습니다. 그것이 루프가 되는 경우입니다. */
                v3 eye = v3f(t->dest.x, t->dest.y + PLAYER_EYE, t->dest.z);
                if (tl.brushes &&
                    brush_point_in(tl.brushes, t->first_brush, t->n_brushes,
                                   eye)) self++;
                if (t->dest.x == 0.0f && t->dest.y == 0.0f && t->dest.z == 0.0f)
                    at_origin++;
            }
            okd(self == 0,
                "and none of them lands you back inside itself", self, 0);
            okd(at_origin == 0,
                "and none of them resolved to the world origin", at_origin, 0);

            /* --- the artifacts are IN the level ------------------------
             *
             * ENGLISH
             * -------
             * THE THREE POWERUPS SHIPPED WITHOUT SPAWNING. Their sprites drew,
             * pickuptest walked each one twice, steptest proved all three
             * effects end to end, and the map carried an entity for each --
             * and none of them ever reached a level, because the importer
             * wrote `"classname" "quad"` and level.c takes a classname apart
             * by ALIAS or by a `monster_`/`item_` PREFIX and ignores anything
             * that is neither. A bare `quad` parsed to no kind and was
             * dropped, in a loop whose own comment says it "cannot check that
             * this one is real".
             *
             * Every test that existed was of a part. This is the one that
             * asks the whole question -- is the thing in the game -- and it
             * is asked of the SHIPPED map rather than a fixture, because the
             * bug lived in the file the fixtures do not use.
             *
             * 한국어
             * ------
             * *파워업 셋이 스폰되지 않은 채로 출하되었습니다.* 스프라이트는 그려졌고,
             * pickuptest는 각각을 두 번 밟았고, steptest는 세 효과를 끝에서 끝까지
             * 증명했으며, 맵은 각각에 대한 엔티티를 실었습니다. 그런데 어느 것도 레벨에
             * 닿은 적이 없습니다. 임포터가 `"classname" "quad"`를 썼고, level.c는
             * classname을 ALIAS나 `monster_`/`item_` *접두사*로 해체하며 둘 다 아닌
             * 것은 무시하기 때문입니다. 맨이름 `quad`는 아무 kind로도 파싱되지 않고
             * 버려졌습니다. 그 루프 자신의 주석이 "이것이 진짜인지 검사할 수 없다"고
             * 말하는 바로 그곳에서입니다.
             *
             * 존재하던 모든 테스트는 *부분*에 대한 것이었습니다. 이것은 전체 질문을 하는
             * 하나입니다. 그것이 게임 안에 있는가. 그리고 픽스처가 아니라 *출하되는* 맵에
             * 대해 묻습니다. 버그가 픽스처는 쓰지 않는 파일에 살았기 때문입니다. */
            int found[PW_KINDS] = {0};
            for (int i = 0; i < tl.n_ents; i++) {
                int k = pickup_kind_for_n(tl.ents[i].kind,
                                          (int)strlen(tl.ents[i].kind));
                if (k >= PK_POWER0 && k <= PK_POWER_LAST) found[k - PK_POWER0]++;
            }
            okd(found[PW_QUAD] == 1 && found[PW_SHADOW] == 1 &&
                found[PW_AEGIS] == 1,
                "the arena's three artifacts all reached it as pickups",
                found[PW_QUAD] + found[PW_SHADOW] + found[PW_AEGIS], 3);

            /* --- and no entity in it is a kind nobody claims ------------
             *
             * ENGLISH
             * -------
             * THE GENERAL FORM OF THE BUG ABOVE. level.c turns `item_x` into
             * kind `x` for ANY x -- its own comment says it "cannot check that
             * this one is real" -- so a classname can survive the parse, take a
             * slot in Level::ents, and mean nothing to any module. That is how
             * `item_artifact_invulnerability` sat in the shipped map looking
             * like an item.
             *
             * The check level.c cannot make, made here instead: every kind the
             * arena carries must be claimed by pickup.c, by enemy.c, or be one
             * of the handful world.c and level.c read by name. A kind nobody
             * claims is an author's intent that silently does nothing.
             *
             * 한국어
             * ------
             * *위 버그의 일반형입니다.* level.c는 어떤 x에 대해서든 `item_x`를 kind `x`로
             * 만들며, 그 주석 스스로 "이것이 진짜인지 검사할 수 없다"고 말합니다. 그래서
             * classname이 파싱을 살아남아 Level::ents의 자리를 차지하고도 어느 모듈에게도
             * 아무 의미가 없을 수 있습니다. `item_artifact_invulnerability`가 출하 맵에서
             * 아이템처럼 보이며 앉아 있던 방식이 그것입니다.
             *
             * level.c가 할 수 없는 검사를 대신 이곳에서 합니다. 투기장이 지닌 모든 kind는
             * pickup.c나 enemy.c가 주장하거나, world.c와 level.c가 이름으로 읽는 몇 안 되는
             * 것 중 하나여야 합니다. 아무도 주장하지 않는 kind는 조용히 아무 일도 하지 않는
             * 제작자의 의도입니다. */
            static const char *const INFO_KINDS[] = {
                "exit", "push", "altar", "wardair", "wardground",
                "spawner_brute", "spawner_caster", "spawner_water_spirit",
            };
            int unclaimed = 0;
            for (int i = 0; i < tl.n_ents; i++) {
                const char *k = tl.ents[i].kind;
                int len = (int)strlen(k);
                if (pickup_kind_for_n(k, len) >= 0) continue;
                if (mon_type_for(k) >= 0) continue;
                int info = 0;
                for (int j = 0; j < (int)(sizeof(INFO_KINDS)/sizeof(INFO_KINDS[0])); j++)
                    if (txt_eq(k, INFO_KINDS[j])) { info = 1; break; }
                if (info) continue;
                if (!unclaimed) printf("\n  kinds nothing claims:\n");
                printf("    %s\n", k);
                unclaimed++;
            }
            okd(unclaimed == 0,
                "and every kind in it is claimed by some module",
                unclaimed, 0);
            level_release(&tl);
        }
    }

    /* --- the baked-light cache ------------------------------------------
       The cache answers most of a rebuild without tracing, which is only
       correct while the key -- a vertex's position and normal -- really is
       everything the bake reads. If that ever stops being true, the level
       still builds, still draws, and is lit slightly wrong: no crash, no
       missing geometry, nothing a code review or a playthrough would catch.
       So it is checked here, against every shipped level.

       캐시는 재생성의 대부분을 판정 없이 답하며, 이는 키(정점의 위치와 법선)가 정말로
       베이크가 읽는 전부일 때에만 옳습니다. 언젠가 그것이 사실이 아니게 되면 레벨은 여전히
       만들어지고 여전히 그려지며 조명만 살짝 틀립니다. 죽지도 않고, 빠진 지오메트리도 없고,
       코드 검토도 플레이도 잡아내지 못합니다. 그래서 이곳에서 모든 출하 레벨에 대해
       검사합니다. */
    {
        printf("\n  --- baked-light cache ---\n");
        light_cache_checks();
    }

    /* --- the caps that used to fail in silence ---------------------------
       Last, because it loads every level again and a counter reading is only
       as clean as what ran before it -- these compare deltas, but a level left
       loaded by an earlier section would still be paying for its own bake.
       마지막입니다. 모든 레벨을 다시 로드하며, 카운터 읽기는 그 앞에 실행된 것만큼만
       깨끗합니다. 이 검사들이 증분을 비교하기는 하지만, 앞 절이 로드된 채 남긴 레벨은
       여전히 자기 베이크 비용을 치르고 있게 됩니다. */
    {
        printf("\n  --- the number reader every level goes through ---\n");
        number_checks();
    }

    {
        printf("\n  --- capacity reports from the text loader ---\n");
        cap_checks();
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall level checks passed\n", fails);
    return fails != 0;
}
