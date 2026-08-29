/**
 * @file decal.c
 * @brief Places, ages and draws the marks a shot leaves: bullet holes, blood, sparks and tracers.
 *
 * ENGLISH
 * -------
 * THE MARKS THEMSELVES ARE NOT HERE. They live in ::Pools::decal, which the
 * caller owns, and every function below takes that ::Pools by pointer. What
 * this file owns is only the drawing apparatus -- two GPU meshes and the two
 * CPU builders that feed them -- because that is the part a headless tool must
 * not be made to allocate. tools/decaltest.c drives placement and ageing with
 * no ::decal_init and therefore no GL at all.
 *
 * BOTH POOLS ARE RINGS, overwritten oldest-first. A mark is never allocated
 * and never freed: the write cursor advances, wraps, and whatever it lands on
 * is gone. That is what bounds the cost of a shotgun fired into a corner, and
 * it is why nothing here can fail for want of room.
 *
 * A MARK IS AGED BY ITS OWN SPAN, not by a shared constant. Blood and wall
 * marks are given very different lifetimes, so every fade divides by
 * ::mark_span rather than by ::DECAL_WALL_LIFE -- see that function for the
 * bug the distinction exists to fix.
 *
 * A MARK ON A DOOR RIDES IT. The door is identified once, at placement, and
 * afterwards the mark moves by the door's CHANGE in travel each frame. See
 * ::decal_hit and ::decal_update, which are the two halves of that one idea.
 *
 * @note ::decal_init and ::decal_free are the only entry points that touch GL,
 *       and ::decal_draw is the only one that issues draws. Everything else is
 *       arithmetic over the caller's ::Pools.
 * @note The module state is file-scope, so the drawing buffers are shared by
 *       every ::Pools drawn through this file. That is safe because they are
 *       rebuilt from scratch inside ::decal_draw and hold nothing between
 *       calls.
 * @warning ::decal_draw enables blending and disables depth WRITES, then puts
 *          both back as it found them. A pass added to the middle of it that
 *          returns early would leave the depth mask off for whatever draws
 *          next.
 *
 * 한국어
 * ------
 * *자국 자체는 이곳에 없습니다.* 그것은 호출자가 소유하는 ::Pools::decal에 있으며, 아래의 모든
 * 함수는 그 ::Pools를 포인터로 받습니다. 이 파일이 소유하는 것은 그리기 장치, 즉 GPU 메시 둘과
 * 그것에 데이터를 공급하는 CPU 빌더 둘뿐입니다. 헤드리스 도구에게 할당을 강요해서는 안 되는
 * 부분이 바로 그것이기 때문입니다. tools/decaltest.c는 ::decal_init 없이, 따라서 GL 없이
 * 배치와 노화를 구동합니다.
 *
 * *두 풀 모두 링이며* 오래된 것부터 덮어씁니다. 자국은 할당되지도 해제되지도 않습니다. 쓰기
 * 커서가 전진하고 순환하며, 그것이 닿는 자리에 있던 것은 사라집니다. 그래서 구석에 대고 쏜
 * 샷건의 비용에 상한이 생기며, 이곳의 어떤 것도 자리가 없어 실패할 수 없는 이유가 됩니다.
 *
 * *자국은 공유 상수가 아니라 자기 수명으로 노화합니다.* 혈흔과 벽의 자국은 매우 다른 수명을
 * 받으므로, 모든 페이드는 ::DECAL_WALL_LIFE가 아니라 ::mark_span으로 나눕니다. 이 구분이
 * 고치려는 결함은 그 함수를 참조하십시오.
 *
 * *문에 붙은 자국은 문을 타고 갑니다.* 문은 배치 시점에 한 번 식별되며, 그 뒤로 자국은 매
 * 프레임 문의 이동량 *변화*만큼 움직입니다. 하나의 개념의 두 절반인 ::decal_hit와
 * ::decal_update를 참조하십시오.
 *
 * @note GL을 건드리는 진입점은 ::decal_init과 ::decal_free뿐이며, 그리기를 발행하는 것은
 *       ::decal_draw뿐입니다. 그 외에는 모두 호출자의 ::Pools에 대한 연산입니다.
 * @note 모듈 상태가 파일 범위이므로 그리기 버퍼는 이 파일을 통해 그려지는 모든 ::Pools가
 *       공유합니다. 그것이 안전한 이유는 버퍼가 ::decal_draw 안에서 처음부터 다시 만들어지고
 *       호출 사이에 아무것도 담지 않기 때문입니다.
 * @warning ::decal_draw는 블렌딩을 켜고 깊이 *쓰기*를 끈 다음, 둘 다 찾은 상태로 되돌립니다.
 *          그 중간에 추가된 패스가 일찍 반환하면 다음에 그리는 것에게 깊이 마스크가 꺼진 채로
 *          넘어갑니다.
 */

#include "decal.h"

#include <math.h>

#include "pools.h"
#include "render.h"
#include "door.h"     /* door_openness, door_travel: what a mark stuck to a door rides */

/* --- Compile-time invariants / 컴파일 시간 불변식 --- */

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

/* --- Module state / 모듈 상태 --- */

/* THE DRAWING APPARATUS ONLY. The mark and tracer rings and their write
   cursors are NOT here -- they are fields of ::Pools::decal, documented in
   decal.h, and reach every function below through the caller's pointer. This
   module used to hold them at file scope, which made a second ::Pools in play
   share the first one's marks.
   *그리기 장치뿐입니다.* 자국과 예광탄의 링, 그리고 그 쓰기 커서는 이곳에 *없습니다*. 그것은
   ::Pools::decal의 필드이며 decal.h에 문서화되어 있고, 호출자의 포인터를 통해 아래의 모든
   함수에 도달합니다. 이 모듈은 예전에 그것을 파일 범위에 두었고, 그래서 진행 중인 두 번째
   ::Pools가 첫 번째의 자국을 공유하게 되었습니다. */

/** @brief Reusable GPU meshes for the billboards and the tracer lines. / 빌보드와 예광탄 선을 위한 재사용 GPU 메시. */
static Mesh    g_fx_mesh, g_line_mesh;
/** @brief CPU-side builders feeding the two meshes above, rebuilt every frame. / 위 두 메시에 데이터를 공급하는 CPU 측 빌더. 매 프레임 재구성됩니다. */
static MeshBuf g_fx_buf,  g_line_buf;
/** @brief Non-zero once ::decal_init has run. / ::decal_init이 실행되었으면 0이 아닙니다. */
static int     g_ready;

/* --- static function prototypes / 정적 함수 원형 --- */

static float mark_span(const Mark *m);

/* --- Public function definitions: lifecycle / 공개 함수 정의: 수명 주기 --- */

void decal_init(void) {
    if (g_ready) return;
    /* Sized for the worst case up front, so no frame ever has to grow a
       buffer mid-draw. Six vertices per mark is two triangles per billboard;
       two per tracer is one line. The slack on each is room for the builders'
       own bookkeeping.
       최악의 경우에 맞춰 미리 크기를 잡습니다. 그래야 어떤 프레임도 그리는 도중에 버퍼를
       키울 필요가 없습니다. 자국당 정점 6개는 빌보드당 삼각형 2개이고, 예광탄당 2개는 선
       하나입니다. 각각의 여유분은 빌더 자체의 기록 처리를 위한 자리입니다. */
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
    /* Life set to zero rather than the arrays cleared: life is the only field
       that decides whether a slot is real, so zeroing it retires every mark
       without touching positions nothing will read.
       배열을 지우지 않고 수명을 0으로 둡니다. 슬롯이 실재하는지를 결정하는 필드는 수명뿐이므로,
       그것을 0으로 만들면 아무도 읽지 않을 위치를 건드리지 않고 모든 자국을 물러나게 합니다. */
    for (int i = 0; i < DECAL_MAX_MARKS;   i++) pl->decal.marks[i].life   = 0.0f;
    for (int i = 0; i < DECAL_MAX_TRACERS; i++) pl->decal.tracers[i].life = 0.0f;
    pl->decal.mark_next = pl->decal.tracer_next = 0;
}

/* --- Public function definitions: placing marks / 공개 함수 정의: 자국 배치 --- */

DecalPlace decal_hit(Pools *pl, const Level *l, v3 end, v3 dir, v3 surf_n,
                     int blood) {
    /* The ring's next slot, taken unconditionally. There is no "is there
       room" question to ask: the oldest mark is the one that gives way.
       링의 다음 슬롯을 무조건 가져옵니다. "자리가 있는가"라는 질문은 없습니다. 가장 오래된
       자국이 자리를 내어줍니다. */
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
            /* The door's openness AS OF NOW, which is the baseline every later
               move is measured against in ::decal_update.
               *지금 시점의* 문의 열림 정도이며, ::decal_update에서 이후의 모든 이동을 재는
               기준선이 됩니다. */
            m->door_t = door_openness(l, di);
        }
    }

    /* Returned so an effect spawned for this hit can sit exactly where the
       mark went, rather than recomputing the same offsets and drifting.
       이 명중을 위해 생성되는 이펙트가 같은 오프셋을 다시 계산하며 어긋나지 않고, 자국이 간
       바로 그 자리에 놓일 수 있도록 반환합니다. */
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

/* --- Public function definitions: ageing and drawing / 공개 함수 정의: 노화와 그리기 --- */

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
        /* An exact compare, not an epsilon: the two values come from the same
           field and a door that has not moved returns bit-for-bit what was
           stored. Anything else is real motion, however small.
           엡실론이 아니라 정확한 비교입니다. 두 값은 같은 필드에서 오며 움직이지 않은 문은
           저장된 값을 비트 단위로 그대로 돌려줍니다. 그 밖의 것은 아무리 작아도 실제
           움직임입니다. */
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
       버퍼는 살아 있는 것만 담기 때문입니다.

       Sized by ::DECAL_MAX_MARKS and reused by all three passes below,
       including the tracer pass. That is sound only because
       ::DECAL_MAX_TRACERS is the smaller of the two; a tracer ring grown past
       the mark ring would overrun this array.
       ::DECAL_MAX_MARKS 크기이며 아래 세 패스가 모두 재사용합니다. 예광탄 패스도
       포함합니다. 그것이 타당한 이유는 오직 ::DECAL_MAX_TRACERS가 둘 중 작은 쪽이기
       때문입니다. 예광탄 링을 자국 링보다 크게 키우면 이 배열을 넘어서게 됩니다. */
    int order[DECAL_MAX_MARKS];

    /* --- the marks themselves --- */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mb_reset(&g_fx_buf);

    int n_live = 0;
    for (int i = 0; i < DECAL_MAX_MARKS; i++) {
        if (pl->decal.marks[i].life <= 0.0f) continue;
        v3 n = pl->decal.marks[i].n;
        /* Any vector not parallel to n gives us a tangent basis. The y-axis is
           the usual pick and the x-axis stands in for a near-vertical normal,
           where the cross product would collapse toward zero length.
           n과 평행하지 않은 벡터라면 무엇이든 접선 기저를 줍니다. 보통은 y축을 쓰고, 법선이
           거의 수직일 때는 x축이 대신합니다. 그 경우 외적의 길이가 0에 가까워지기
           때문입니다. */
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
            /* Divided by the mark's OWN span. Clamped because ::decal_update
               subtracts dt after the mark was given its full life, so a mark
               placed this frame can read fractionally over 1.
               자국 *자신의* 수명으로 나눕니다. ::decal_update가 자국에 수명을 온전히 준 뒤
               dt를 빼므로, 이번 프레임에 놓인 자국은 1을 아주 조금 넘을 수 있어 제한합니다. */
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
        /* Age measured against the mark's own span again, so a blood mark gets
           the same spark window as a wall mark rather than skipping it for
           having a shorter life.
           나이를 다시 자국 자신의 수명 기준으로 잽니다. 그래야 혈흔이 수명이 짧다는 이유로
           스파크를 건너뛰지 않고 벽의 자국과 같은 스파크 구간을 얻습니다. */
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
            /* Fades across ::DECAL_SPARK_TIME rather than the mark's life: the
               flare is an event at the moment of the hit, not the mark ageing.
               자국의 수명이 아니라 ::DECAL_SPARK_TIME에 걸쳐 사라집니다. 섬광은 자국의 노화가
               아니라 명중 순간의 사건입니다. */
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

/* --- Public function definitions: queries / 공개 함수 정의: 조회 --- */

int decal_live_marks(const Pools *pl) {
    /* Counted rather than tracked. The rings retire a slot by letting its life
       run out, so no counter could be kept correct without a second pass over
       the same array this one walks.
       추적하지 않고 셉니다. 링은 수명이 다하게 두어 슬롯을 물러나게 하므로, 이 함수가 도는
       바로 그 배열을 한 번 더 훑지 않고서는 어떤 계수기도 올바르게 유지할 수 없습니다. */
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

/* --- static helper definitions / 정적 보조 함수 정의 --- */

/**
 * @brief How long a mark of this kind was GIVEN, for ageing it against.
 *
 * ENGLISH
 * -------
 * @param[in] m The mark to ask about.
 * @return Its full lifetime in seconds: ::DECAL_BLOOD_LIFE or ::DECAL_WALL_LIFE.
 *
 * @note Every fade in ::decal_draw divides by this rather than by
 *       ::DECAL_WALL_LIFE. Dividing a 0.55s blood mark by 6.0 leaves it at 9%
 *       alpha from the moment it appears, which is a mark nobody ever sees
 *       rather than one that fades -- and it made every blood mark 5.45s old
 *       the instant it spawned, so its spark was skipped entirely.
 *
 * 한국어
 * ------
 * @brief 이 종류의 자국이 *부여받은* 수명입니다. 노화를 재는 기준입니다.
 * @param[in] m 물어볼 자국.
 * @return 초 단위의 전체 수명. ::DECAL_BLOOD_LIFE 또는 ::DECAL_WALL_LIFE입니다.
 *
 * @note ::decal_draw의 모든 페이드는 ::DECAL_WALL_LIFE가 아니라 이것으로 나눕니다. 0.55초짜리
 *       혈흔을 6.0으로 나누면 생성되는 순간부터 알파 9%가 되는데, 이는 페이드되는 자국이
 *       아니라 아무도 볼 수 없는 자국입니다. 또한 모든 혈흔이 생성되는 순간 이미 5.45초 된
 *       것이 되어 스파크가 통째로 건너뛰어졌습니다.
 */
static float mark_span(const Mark *m) {
    return m->blood ? DECAL_BLOOD_LIFE : DECAL_WALL_LIFE;
}
