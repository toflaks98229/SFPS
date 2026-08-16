/**
 * @file decal.c
 * @brief The marks a shot leaves: bullet holes, blood, sparks and tracers.
 */

#include "decal.h"
#include "pools.h"
#include "render.h"
#include "door.h"     /* door_openness, door_travel: what a mark stuck to a door rides */
#include <math.h>

/* --- File-local types / 파일 지역 타입 --- */



/* --- Module state / 모듈 상태 --- */

/** @brief Marks, overwritten oldest-first once full. / 자국. 가득 차면 오래된 것부터 덮어씁니다. */
/** @brief Tracers, overwritten oldest-first once full. / 예광탄. 가득 차면 오래된 것부터 덮어씁니다. */
/** @brief Write cursors into the two rings above. / 위 두 링의 쓰기 커서. */

/** @brief Reusable GPU meshes for the billboards and the tracer lines. / 빌보드와 예광탄 선을 위한 재사용 GPU 메시. */
static Mesh    g_fx_mesh, g_line_mesh;
/** @brief CPU-side builders feeding the two meshes above, rebuilt every frame. / 위 두 메시에 데이터를 공급하는 CPU 측 빌더. 매 프레임 재구성됩니다. */
static MeshBuf g_fx_buf,  g_line_buf;
/** @brief Non-zero once ::decal_init has run. / ::decal_init이 실행되었으면 0이 아닙니다. */
static int     g_ready;

/* The blood mark must outlive the spark that appears with it and must not
 * outlive the wall mark. Neither bound is arbitrary:
 *
 *   - shorter than DECAL_SPARK_TIME and the mark would vanish before the flash
 *     that announced it, so a hit would end before it registered;
 *   - as long as DECAL_WALL_LIFE and it is the bug the constant exists to fix
 *     -- a stain left hanging where a monster used to be standing.
 *
 * A relationship that only holds because nobody has retuned one of the two is a
 * relationship worth stating.
 *
 * 혈흔은 함께 나타나는 스파크보다는 오래 남아야 하고 벽의 자국보다는 오래 남아서는 안
 * 됩니다. 두 경계 모두 임의의 값이 아닙니다. DECAL_SPARK_TIME보다 짧으면 자국이 그것을 알린
 * 섬광보다 먼저 사라져 명중이 인지되기 전에 끝나 버리고, DECAL_WALL_LIFE만큼 길면 이 상수가
 * 고치려는 바로 그 버그, 즉 몬스터가 서 있던 자리에 얼룩만 떠 있는 상황이 됩니다.
 *
 * 둘 중 하나를 아무도 재조정하지 않았기 때문에만 성립하는 관계라면 명시해 둘 가치가
 * 있습니다. */
_Static_assert(DECAL_BLOOD_LIFE > DECAL_SPARK_TIME,
               "a blood mark must outlast the spark that announced it");
_Static_assert(DECAL_BLOOD_LIFE < DECAL_WALL_LIFE,
               "a blood mark must not linger like a wall mark -- monsters move");

/* How long a mark of this kind was GIVEN. Every fade below divides by this
   rather than by DECAL_WALL_LIFE: dividing a 0.55s blood mark by 6.0 leaves it
   at 9% alpha from the moment it appears, which is a mark nobody ever sees
   rather than one that fades, and it made every blood mark 5.45s old the
   instant it spawned so its spark was skipped entirely.
   이 종류의 자국이 *부여받은* 수명입니다. 아래의 모든 페이드는 DECAL_WALL_LIFE가 아니라
   이것으로 나눕니다. 0.55초짜리 혈흔을 6.0으로 나누면 생성되는 순간부터 알파 9%가 되는데,
   이는 페이드되는 자국이 아니라 아무도 볼 수 없는 자국입니다. 또한 모든 혈흔이 생성되는 순간
   이미 5.45초 된 것이 되어 스파크가 통째로 건너뛰어졌습니다. */
static float mark_span(const Mark *m) {
    return m->blood ? DECAL_BLOOD_LIFE : DECAL_WALL_LIFE;
}

/* --- Public function definitions / 공개 함수 정의 --- */

void decal_init(void) {
    if (g_ready) return;
    mb_init(&g_fx_buf,   DECAL_MAX_MARKS   * 6 + 64);
    mb_init(&g_line_buf, DECAL_MAX_TRACERS * 2 + 32);
    g_ready = 1;
}

void decal_free(void) {
    if (!g_ready) return;
    mb_free(&g_fx_buf);
    mb_free(&g_line_buf);
    g_ready = 0;
}

void decal_reset(Pools *pl) {
    for (int i = 0; i < DECAL_MAX_MARKS;   i++) pl->decal.marks[i].life   = 0.0f;
    for (int i = 0; i < DECAL_MAX_TRACERS; i++) pl->decal.tracers[i].life = 0.0f;
    pl->decal.mark_next = pl->decal.tracer_next = 0;
}

DecalPlace decal_hit(Pools *pl, const Level *l, v3 end, v3 dir, v3 surf_n,
                     int blood) {
    Mark *m = &pl->decal.marks[pl->decal.mark_next];
    pl->decal.mark_next = (pl->decal.mark_next + 1) % DECAL_MAX_MARKS;

    /* Blood sprays back toward the shooter; a wall mark sits on the surface,
       nudged off it so the mark wins the depth test.
       피는 사수 쪽으로 튀고, 벽의 자국은 표면 위에 놓이되 깊이 테스트에서 이기도록 살짝
       띄워집니다. */
    m->p     = blood ? v3sub(end, v3scale(dir, 0.05f))
                     : v3add(end, v3scale(surf_n, 0.012f));
    m->n     = blood ? v3scale(dir, -1.0f) : surf_n;
    m->life  = blood ? DECAL_BLOOD_LIFE : DECAL_WALL_LIFE;
    m->blood = blood;

    /* --- what it is stuck to -------------------------------------------
       Asked here and never again. A mark on a wall is a world position and
       stays one; a mark on a door has to travel with the door, and the only
       moment the door can be identified from the position is this one --
       afterwards the door has moved out from under it.

       BLOOD IS NEVER ATTACHED. It marks a monster rather than a surface, it
       already does not follow what it hit, and ::DECAL_BLOOD_LIFE is half a
       second precisely so that it does not have to. Asking anyway would be a
       ::level_door_at per pellet that hits flesh, for an answer nothing reads.

       무엇에 붙어 있는지입니다. 이곳에서 묻고 다시는 묻지 않습니다. 벽의 자국은 월드
       좌표이고 계속 그렇습니다. 문의 자국은 문과 함께 이동해야 하며, 위치로부터 그 문을
       식별할 수 있는 유일한 순간이 바로 지금입니다. 그 뒤에는 문이 자국 아래에서
       빠져나간 뒤입니다.

       혈흔은 결코 붙이지 않습니다. 표면이 아니라 몬스터를 표시하고, 이미 맞은 대상을
       따라가지 않으며, ::DECAL_BLOOD_LIFE가 반 초인 이유가 정확히 그래도 되게 하기
       위함입니다. 그런데도 묻는다면 살에 맞은 산탄마다 ::level_door_at을 부르는 것이고,
       그 답은 아무도 읽지 않습니다. */
    m->door   = -1;
    m->door_t = 0.0f;
    if (!blood && l) {
        int di = level_door_at(l, end, surf_n);
        if (di >= 0) {
            m->door   = (short)di;
            m->door_t = door_openness(l, di);
        }
    }

    DecalPlace at = { m->p, m->n };
    return at;
}

void decal_tracer(Pools *pl, v3 from, v3 to) {
    Tracer *t = &pl->decal.tracers[pl->decal.tracer_next];
    pl->decal.tracer_next = (pl->decal.tracer_next + 1) % DECAL_MAX_TRACERS;
    t->a = from;
    t->b = to;
    t->life = DECAL_TRACER_LIFE;
}

int decal_live_marks(const Pools *pl) {
    int n = 0;
    for (int i = 0; i < DECAL_MAX_MARKS; i++)
        if (pl->decal.marks[i].life > 0.0f) n++;
    return n;
}

int decal_live_tracers(const Pools *pl) {
    int n = 0;
    for (int i = 0; i < DECAL_MAX_TRACERS; i++)
        if (pl->decal.tracers[i].life > 0.0f) n++;
    return n;
}

void decal_update(Pools *pl, const Level *l, float dt) {
    for (int i = 0; i < DECAL_MAX_MARKS; i++) {
        Mark *m = &pl->decal.marks[i];
        if (m->life <= 0.0f) continue;
        m->life -= dt;

        /* --- and it rides whatever it is stuck to --------------------
           The door's travel is authoritative, so this asks how far the door
           has moved SINCE the mark last looked and moves it by exactly that.
           Adding the whole travel every frame would walk the mark out of the
           level; recomputing an absolute position from a stored origin would
           work too and would need a second copy of the mark's position to
           compute it from. One number, moved by its own change.

           문의 이동량이 권위 있는 값이므로, 자국이 마지막으로 본 이후 문이 얼마나
           움직였는지를 묻고 정확히 그만큼 옮깁니다. 매 프레임 전체 이동량을 더하면 자국이
           레벨 밖으로 걸어 나갑니다. 저장된 원점에서 절대 위치를 다시 계산하는 것도
           동작하지만, 그것을 계산할 자국 위치의 사본이 하나 더 필요합니다. 숫자 하나를,
           그 자신의 변화량만큼 옮깁니다. */
        if (m->door < 0 || !l) continue;

        float now = door_openness(l, m->door);
        if (now == m->door_t) continue;

        m->p = v3add(m->p, v3sub(door_travel(l, m->door, now),
                                 door_travel(l, m->door, m->door_t)));
        m->door_t = now;
    }
    for (int i = 0; i < DECAL_MAX_TRACERS; i++)
        if (pl->decal.tracers[i].life > 0.0f) pl->decal.tracers[i].life -= dt;
}

void decal_draw(const Pools *pl, mat4 view_proj, v3 cam_pos, v3 cam_right, v3 cam_up) {
    if (!g_ready) return;

    rd_mvp(view_proj);
    rd_mode(RD_FLAT);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);

    /* One upload, then a draw per mark so each can fade independently without
       a per-vertex colour attribute. `order` maps a draw index back to the pool
       slot it came from, because the buffer holds only the live ones.
       한 번 업로드한 뒤 자국마다 한 번씩 그립니다. 그래야 정점별 색 속성 없이도 각각 따로
       페이드할 수 있습니다. `order`는 그리기 인덱스를 그것이 온 풀 슬롯으로 되돌립니다.
       버퍼는 살아 있는 것만 담기 때문입니다. */
    int order[DECAL_MAX_MARKS];

    /* --- the marks themselves --- */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mb_reset(&g_fx_buf);

    int n_live = 0;
    for (int i = 0; i < DECAL_MAX_MARKS; i++) {
        if (pl->decal.marks[i].life <= 0.0f) continue;
        v3 n = pl->decal.marks[i].n;
        /* Any vector not parallel to n gives us a tangent basis. */
        v3 hint = (n.y > 0.9f || n.y < -0.9f) ? v3f(1, 0, 0) : v3f(0, 1, 0);
        v3 t = v3norm(v3cross(hint, n));
        v3 b = v3cross(n, t);
        mb_billboard(&g_fx_buf, pl->decal.marks[i].p, t, b, 0.085f, 0.085f);
        order[n_live++] = i;
    }
    if (n_live) {
        mesh_upload(&g_fx_mesh, &g_fx_buf, 1);
        glBindVertexArray(g_fx_mesh.vao);
        for (int k = 0; k < n_live; k++) {
            const Mark *m = &pl->decal.marks[order[k]];
            float a = m->life / mark_span(m);
            if (a > 1.0f) a = 1.0f;
            /* Blood stains dark red where a wall hole is near-black. */
            if (m->blood) rd_color(0.30f, 0.02f, 0.02f, a * 0.85f);
            else          rd_color(0.04f, 0.03f, 0.03f, a * 0.85f);
            glDrawArrays(GL_TRIANGLES, k * 6, 6);
        }
    }

    /* --- the spark on a fresh one: a brief additive flare so a hit reads
           instantly. A dark bullet hole 30m down a fogged corridor is invisible
           on its own. --- */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    mb_reset(&g_fx_buf);

    int sn = 0;
    for (int i = 0; i < DECAL_MAX_MARKS; i++) {
        if (pl->decal.marks[i].life <= 0.0f) continue;
        if (mark_span(&pl->decal.marks[i]) - pl->decal.marks[i].life > DECAL_SPARK_TIME) continue;
        /* Billboarded to the camera, and scaled with distance so a far hit
           stays legible instead of shrinking to a single pixel. */
        float dist = v3len(v3sub(pl->decal.marks[i].p, cam_pos));
        float size = 0.12f + dist * 0.012f;
        mb_billboard(&g_fx_buf, pl->decal.marks[i].p, cam_right, cam_up, size, size);
        order[sn++] = i;
    }
    if (sn) {
        mesh_upload(&g_fx_mesh, &g_fx_buf, 1);
        glBindVertexArray(g_fx_mesh.vao);
        for (int k = 0; k < sn; k++) {
            const Mark *m = &pl->decal.marks[order[k]];
            float age = mark_span(m) - m->life;
            float a   = 1.0f - age / DECAL_SPARK_TIME;
            /* A red puff on flesh, a warm spark on stone. */
            if (m->blood) rd_color(0.90f, 0.12f, 0.10f, a * 0.9f);
            else          rd_color(1.0f, 0.85f, 0.50f, a * 0.9f);
            glDrawArrays(GL_TRIANGLES, k * 6, 6);
        }
    }

    /* --- tracers: additive, very short lived --- */
    mb_reset(&g_line_buf);

    int tn = 0;
    for (int i = 0; i < DECAL_MAX_TRACERS; i++) {
        if (pl->decal.tracers[i].life <= 0.0f) continue;
        mb_line(&g_line_buf, pl->decal.tracers[i].a, pl->decal.tracers[i].b);
        order[tn++] = i;
    }
    if (tn) {
        mesh_upload(&g_line_mesh, &g_line_buf, 1);
        glBindVertexArray(g_line_mesh.vao);
        glLineWidth(2.0f);
        for (int k = 0; k < tn; k++) {
            float a = pl->decal.tracers[order[k]].life / DECAL_TRACER_LIFE;
            rd_color(1.0f, 0.82f, 0.42f, a * 0.9f);
            glDrawArrays(GL_LINES, k * 2, 2);
        }
    }

    /* Left as they were found, so the pass that draws next is not handed a
       depth mask somebody else switched off.
       발견한 상태 그대로 되돌려 놓습니다. 그래야 다음에 그리는 패스가 다른 누군가 꺼 둔 깊이
       마스크를 넘겨받지 않습니다. */
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
