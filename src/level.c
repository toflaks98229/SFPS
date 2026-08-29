/**
 * @file level.c
 * @brief Parses sector levels, builds their geometry, and answers collision queries.
 *
 * ENGLISH
 * -------
 * Overlapping sectors are the authoring model here rather than an error, and
 * almost every subtlety in this file follows from that one decision. A point
 * may lie inside several sectors, and the LAST one declared governs it; an
 * edge may be covered by another sector along only part of its length, so
 * walls are cut into spans rather than treated as uniform. See ::EdgeSpan.
 *
 * Collision follows Doom's P_TryMove: ask whether a position is standable
 * rather than intersecting the player against wall segments.
 *
 * 한국어
 * ------
 * 이곳에서는 섹터가 겹치는 것이 오류가 아니라 제작 방식 그 자체이며, 이 파일의 거의
 * 모든 미묘한 부분이 그 하나의 결정에서 파생됩니다. 한 지점이 여러 섹터에 속할 수
 * 있고, 그중 *마지막에* 선언된 섹터가 그 지점을 지배합니다. 또한 모서리는 길이의
 * 일부만 다른 섹터에 덮일 수 있으므로, 벽은 균일한 것으로 취급되지 않고 구간으로
 * 잘립니다. ::EdgeSpan을 참조하십시오.
 *
 * 충돌 처리는 Doom의 P_TryMove를 따릅니다. 플레이어를 벽 선분과 교차 판정하는 대신,
 * 해당 위치에 설 수 있는지를 묻습니다.
 */

#include "level.h"
/* The geometry builders, which level.h deliberately does NOT include -- it
   forward-declares MeshBuf/MdlRange so the simulation headers stay free of
   the GL stack. The .c file is where the real definitions belong.
   지오메트리 빌더입니다. level.h는 이를 의도적으로 포함하지 않고 MeshBuf/MdlRange를
   전방 선언하여 시뮬레이션 헤더가 GL 스택으로부터 자유롭게 유지되도록 합니다. 실제
   정의가 필요한 곳은 .c 파일입니다. */
#include "render.h"
#include "model.h"
#include "data.h"
#include "brush.h"
#include "brushstore.h"   /* BrushStore, by value: the slots a brush level lands in */
#include "txt.h"
#include "diag.h"

#include <math.h>

#define U       0.01f    /* file units (cm) -> world units (m) */
#define LEVEL_UV 0.5f    /* texels per world unit, matching the old box level */

/* How far apart the ray marcher samples, in world units.
 *
 * ENGLISH
 * -------
 * Named rather than left as a literal because ::level_trace and ::level_blocked
 * both march with it and MUST agree: the visibility test deciding a wall is
 * clear at a spacing the shot test would have caught is a monster that shoots
 * through geometry. One constant, shared by ::march, is what makes that
 * impossible rather than merely unlikely.
 *
 * 5cm is the thinnest geometry the marcher can be trusted to notice. Raising it
 * is the obvious way to make a trace cheaper and it is the wrong dial: the cost
 * is dominated by how OFTEN a trace runs, not by how many samples one takes,
 * and a coarser step buys speed by silently losing thin walls.
 *
 * 한국어
 * ------
 * 광선 마처가 샘플링하는 간격(월드 단위)입니다.
 *
 * 리터럴로 두지 않고 이름을 붙인 이유는 ::level_trace와 ::level_blocked가 모두 이 값으로
 * 전진하며 반드시 일치해야 하기 때문입니다. 사격 판정이라면 잡아냈을 간격에서 가시성
 * 판정이 벽을 뚫려 있다고 결론 내리면, 그것은 지오메트리를 관통해 쏘는 몬스터입니다.
 * ::march가 공유하는 하나의 상수가, 그 일을 단지 일어나기 어렵게 만드는 대신 불가능하게
 * 만듭니다.
 *
 * 5cm는 마처가 놓치지 않는다고 신뢰할 수 있는 가장 얇은 지오메트리입니다. 이 값을 올리는
 * 것은 판정을 싸게 만드는 뻔한 방법이지만 잘못된 조정입니다. 비용을 지배하는 것은 한 번의
 * 판정이 몇 개의 샘플을 취하는지가 아니라 판정이 *얼마나 자주* 실행되는지이며, 성긴
 * 간격은 얇은 벽을 조용히 잃는 대가로 속도를 삽니다.
 */
#define TRACE_STEP 0.05f

/* A level may declare more lights than the shader can evaluate only if someone
   raises one cap without the other. The excess would be parsed, stored, and
   then silently ignored at draw time -- a room darker than its author wrote,
   with nothing to say why. This file is the one that sees both headers, so the
   check lives here.
   셰이더가 계산할 수 있는 것보다 레벨이 더 많은 광원을 선언할 수 있으려면, 누군가 한쪽
   상한만 올려야 합니다. 초과분은 파싱되고 저장된 뒤 그리기 시점에 조용히 무시됩니다.
   제작자가 작성한 것보다 어두운 방이 되며 그 이유를 알려 주는 것은 없습니다. 이 파일이
   두 헤더를 모두 참조하므로 검사를 이곳에 둡니다. */
/* The two caps used to be tied together, and this file used to assert it: the
   shader had to be able to evaluate every light a level could declare, because
   evaluating them was the only way they were applied.

   That is no longer what happens. A level's lights are baked into the vertices
   when it loads, so the shader never sees them, and the number of them a level
   may declare has nothing to do with how many uniform slots exist. The two are
   now independent on purpose -- ::LVL_MAX_LIGHTS is a .bss and load-time cost,
   ::RD_MAX_LIGHTS is a per-fragment one -- and an assert tying them would be
   the thing standing between a level and its sixty-fourth lamp.

   두 상한은 한때 서로 묶여 있었고 이 파일이 그것을 단언했습니다. 셰이더가 레벨이 선언할 수
   있는 모든 광원을 평가할 수 있어야 했는데, 평가가 그것을 적용하는 유일한 방법이었기
   때문입니다.

   이제는 그렇지 않습니다. 레벨의 광원은 로드될 때 정점에 구워지므로 셰이더는 그것을 결코
   보지 않으며, 레벨이 선언할 수 있는 개수는 유니폼 슬롯이 몇 개인지와 아무 관계가 없습니다.
   이제 둘은 의도적으로 독립입니다. ::LVL_MAX_LIGHTS는 .bss와 로드 시점의 비용이고
   ::RD_MAX_LIGHTS는 프래그먼트마다의 비용입니다. 둘을 묶는 단언은 레벨과 그 64번째 등
   사이를 가로막는 것이 됩니다. */

/* ----------------------------------------------------------------- parser */

/* txt_copy with this file's argument order. Kept as a name rather than
   replaced at each of its call sites, because "copy this token into that
   field" reads better here than the general helper's four arguments do.
   이 파일의 인자 순서를 따르는 txt_copy입니다. 호출 지점마다 교체하지 않고 이름을
   유지하는 이유는, 이곳에서는 "이 토큰을 저 필드에 복사한다"가 범용 헬퍼의 인자 네 개
   보다 잘 읽히기 때문입니다. */
static void copy_name(char *dst, int cap, const char *src, int len) {
    txt_copy(dst, cap, src, len);
}

void level_bounds(Sector *s) {
    if (s->n < 1) {
        /* No outline: leave the box unmarked. point_in_sector then falls
           through to the crossing test, which returns 0 for a sector with no
           points anyway -- the same answer, reached by the path that cannot be
           wrong.
           외곽선이 없는 경우: 박스를 표시하지 않은 채로 둡니다. 그러면 point_in_sector가
           교차 판정으로 넘어가며, 점이 없는 섹터에 대해 어차피 0을 반환합니다. 결과는
           같으면서도 틀릴 수 없는 경로를 거치게 됩니다. */
        s->has_bounds = 0;
        return;
    }

    short lo_x = s->pts[0], hi_x = s->pts[0];
    short lo_z = s->pts[1], hi_z = s->pts[1];
    for (int i = 1; i < s->n; i++) {
        short px = s->pts[i*2], pz = s->pts[i*2+1];
        if (px < lo_x) lo_x = px;
        if (px > hi_x) hi_x = px;
        if (pz < lo_z) lo_z = pz;
        if (pz > hi_z) hi_z = pz;
    }
    s->min_x = lo_x; s->max_x = hi_x;
    s->min_z = lo_z; s->max_z = hi_z;
    s->has_bounds = 1;
}

/* ------------------------------------------------------- sector lookup grid */

void level_grid_build(Level *l) {
    SectorGrid *g = &l->grid;

    /* Cleared first and marked unbuilt, so an early return at any point below
       leaves a grid every query safely falls back from rather than a
       half-populated one that would omit sectors.
       먼저 비우고 미생성으로 표시합니다. 아래 어느 지점에서 조기 반환하더라도, 섹터를
       누락시킬 절반만 채워진 격자가 아니라 모든 질의가 안전하게 되돌아갈 수 있는 격자가
       남도록 하기 위함입니다. */
    g->built = 0;
    for (int i = 0; i < LVL_GRID_DIM * LVL_GRID_DIM; i++) {
        g->count[i] = 0;
        g->overflow[i] = 0;
    }

    /* Below the threshold the grid costs more than the scan it replaces, so
       none is built and every query takes the scan path -- see
       LVL_GRID_MIN_SECTORS for the measurements. Leaving it unbuilt is exactly
       the same state a hand-assembled Level is in, so this needs no separate
       handling anywhere: the fallback already exists and is already correct.
       임계값 미만에서는 격자가 대체하려는 순회보다 비용이 크므로 생성하지 않고, 모든
       질의가 순회 경로를 택합니다. 측정값은 LVL_GRID_MIN_SECTORS를 참조하십시오.
       생성하지 않은 상태는 손으로 조립한 Level과 정확히 같은 상태이므로 별도의 처리가
       필요 없습니다. 폴백이 이미 존재하며 이미 올바릅니다. */
    if (l->n_sectors < LVL_GRID_MIN_SECTORS) return;

    /* The grid spans every sector's bounds. Built from the cached boxes rather
       than by rescanning the points: level_load has just computed them, and the
       editor's contract is to refresh them before calling here.
       격자는 모든 섹터의 경계를 포괄합니다. 점을 다시 순회하지 않고 캐시된 박스로부터
       생성합니다. level_load가 방금 그것을 계산했고, 에디터의 계약은 이 함수를 호출하기
       전에 그것을 갱신하는 것입니다. */
    int lo_x = 0, lo_z = 0, hi_x = 0, hi_z = 0, any = 0;
    for (int i = 0; i < l->n_sectors; i++) {
        const Sector *s = &l->sectors[i];
        if (!s->has_bounds) continue;
        if (!any) {
            lo_x = s->min_x; hi_x = s->max_x;
            lo_z = s->min_z; hi_z = s->max_z;
            any = 1;
        } else {
            if (s->min_x < lo_x) lo_x = s->min_x;
            if (s->max_x > hi_x) hi_x = s->max_x;
            if (s->min_z < lo_z) lo_z = s->min_z;
            if (s->max_z > hi_z) hi_z = s->max_z;
        }
    }
    if (!any) return;              /* no usable bounds: stay unbuilt */

    /* Round the cell size up, so DIM cells always cover the whole extent. The
       +1 guarantees a non-zero size even for a zero-extent level, which would
       otherwise divide by zero in the cell lookup.
       셀 크기를 올림합니다. 그래야 DIM개의 셀이 항상 전체 범위를 덮습니다. +1은 범위가
       0인 레벨에서도 크기가 0이 되지 않도록 보장하며, 그렇지 않으면 셀 조회에서 0으로
       나누게 됩니다. */
    int span_x = hi_x - lo_x, span_z = hi_z - lo_z;
    int cw = span_x / LVL_GRID_DIM + 1;
    int ch = span_z / LVL_GRID_DIM + 1;

    g->min_x  = (short)lo_x;  g->min_z  = (short)lo_z;
    g->cell_w = (short)cw;    g->cell_h = (short)ch;

    /* A sector goes into every cell its bounding box touches, not just the one
       its centre falls in: a sector larger than a cell must be found from
       anywhere inside it.
       섹터는 중심이 속한 셀이 아니라 바운딩 박스가 닿는 모든 셀에 들어갑니다. 셀보다 큰
       섹터도 그 내부 어디에서나 발견되어야 하기 때문입니다. */
    for (int i = 0; i < l->n_sectors; i++) {
        const Sector *s = &l->sectors[i];

        /* Without bounds there is no box to place, and guessing would risk
           omitting the sector from a cell that needs it. Mark every cell
           unusable instead: correctness before speed.
           경계값이 없으면 배치할 박스가 없으며, 추측하면 그 섹터가 필요한 셀에서
           누락될 위험이 있습니다. 대신 모든 셀을 사용 불가로 표시합니다. 속도보다
           정확성입니다. */
        if (!s->has_bounds) {
            for (int k = 0; k < LVL_GRID_DIM * LVL_GRID_DIM; k++) g->overflow[k] = 1;
            continue;
        }

        int cx0 = (s->min_x - lo_x) / cw, cx1 = (s->max_x - lo_x) / cw;
        int cz0 = (s->min_z - lo_z) / ch, cz1 = (s->max_z - lo_z) / ch;
        if (cx0 < 0) cx0 = 0;
        if (cz0 < 0) cz0 = 0;
        if (cx1 >= LVL_GRID_DIM) cx1 = LVL_GRID_DIM - 1;
        if (cz1 >= LVL_GRID_DIM) cz1 = LVL_GRID_DIM - 1;

        for (int cz = cz0; cz <= cz1; cz++) {
            for (int cx = cx0; cx <= cx1; cx++) {
                int c = cz * LVL_GRID_DIM + cx;
                if (g->count[c] >= LVL_GRID_MAX_PER_CELL) {
                    /* Full: mark the cell unusable rather than dropping the
                       sector. A dropped sector is a floor that vanishes; a
                       fallback is merely the scan this grid exists to avoid.
                       가득 참: 섹터를 버리는 대신 셀을 사용 불가로 표시합니다. 버려진
                       섹터는 사라진 바닥이지만, 되돌아가기는 이 격자가 피하려던 순회일
                       뿐입니다. */
                    g->overflow[c] = 1;
                    continue;
                }
                g->sect[c][g->count[c]++] = (unsigned char)i;
            }
        }
    }

    g->built = 1;
}

/* ---------------------------------------- level_load, in its four stages */

/**
 * @brief Empties a ::Level so a parse starts from a known state.
 *
 * ENGLISH: Split out of ::level_load because a reset that is missing a field
 * is invisible: the new level simply inherits the old one's lights, or its
 * exit, and looks like a level authored that way. The struct is cleared field
 * by field rather than wholesale because ::Level carries the sector arrays,
 * and zeroing sixty-four sectors to then overwrite the used ones is work no
 * frame needs.
 *
 * 한국어: ::level_load에서 분리했습니다. 필드가 빠진 초기화는 눈에 보이지 않기 때문입니다.
 * 새 레벨이 옛 레벨의 광원이나 출구를 그대로 물려받고, 원래 그렇게 작성된 레벨처럼
 * 보입니다. 구조체를 통째로 0으로 만들지 않고 필드별로 비우는 이유는 ::Level이 섹터 배열을
 * 담고 있으며, 예순네 개의 섹터를 0으로 채운 뒤 쓰이는 것만 덮어쓰는 일은 어떤 프레임도
 * 필요로 하지 않기 때문입니다.
 */
/* EVERY COUNT, and the omission is the failure this function exists to
   prevent. n_triggers was added and not cleared here, so a Level loaded twice
   kept the first load's triggers and appended the second's -- and because a
   Level is normally a long-lived static reloaded per level, that is the common
   path rather than an edge case. The symptom was a trigger count that grew by
   one every time the level was opened.
   모든 개수이며, 빠뜨리는 것이 바로 이 함수가 막으려고 존재하는 실패입니다. n_triggers가
   추가되고 이곳에서 지워지지 않아, 두 번 로드된 Level이 첫 로드의 트리거를 유지한 채 두 번째
   것을 덧붙였습니다. Level은 보통 레벨마다 다시 읽히는 오래 사는 정적 변수이므로, 그것은
   예외가 아니라 일상적인 경로입니다. 증상은 레벨을 열 때마다 하나씩 늘어나는 트리거 수였습니다. */
static void level_clear(Level *out) {
    out->n_sectors  = 0;
    out->n_ents     = 0;
    out->n_lights   = 0;
    out->n_doors    = 0;
    out->n_triggers = 0;
    out->n_hazards  = 0;
    out->name[0]    = 0;
    out->next[0]    = 0;
    out->start[0]   = out->start[1] = out->start[2] = 0;
    out->start_h    = 0;

    /* THE DOORS STOP BEING THE DOORS THIS DESCRIBED. Cleared here rather than
       left for ::door_reset, because the two answer different questions and
       only one of them is guaranteed to be asked: door_reset is a thing a
       caller does, and a caller that forgets it used to leave the previous
       level's travel and closed outlines sitting behind this level's
       definitions. `count` of 0 is what makes that a level whose doors do not
       move, instead of one moving somebody else's geometry.

       ::DIAG_DOOR_STALE still fires for it -- door_update compares this count
       against `n_doors` -- so a forgotten reset is reported rather than merely
       survived.

       Not the brush claim, which ::level_load deliberately keeps: see
       ::Level::brush_key.

       문이 이것이 서술하던 문이기를 그만둡니다. ::door_reset에 맡기지 않고 이곳에서 비우는
       이유는, 둘이 서로 다른 질문에 답하는데 반드시 던져지는 것은 한쪽뿐이기 때문입니다.
       door_reset은 호출자가 하는 일이며, 그것을 잊은 호출자는 이전 레벨의 이동량과 닫힌
       외곽선을 이번 레벨의 정의 뒤에 그대로 남겨 두었습니다. `count`가 0이라는 것이, 남의
       지오메트리를 움직이는 레벨이 아니라 문이 움직이지 않는 레벨로 만듭니다.

       그래도 ::DIAG_DOOR_STALE은 발생합니다. door_update가 이 개수를 `n_doors`와 비교하기
       때문이며, 따라서 잊힌 reset은 그냥 넘어가지 않고 보고됩니다.

       브러시 주장은 지우지 않습니다. ::level_load가 의도적으로 유지합니다.
       ::Level::brush_key를 참조하십시오. */
    DoorSet none = {0};
    out->door_run = none;
}

/**
 * @brief Drops sectors too small to triangulate, compacting the rest.
 *
 * ENGLISH: Must run before ::level_cache_bounds and ::level_grid_build, both
 * of which walk `n_sectors` -- doing it after would compute a box and a grid
 * cell for geometry that is about to be discarded, and leave the grid holding
 * indices past the compacted end.
 *
 * 한국어: ::level_cache_bounds와 ::level_grid_build보다 먼저 실행되어야 합니다. 둘 다
 * `n_sectors`를 순회하므로, 나중에 하면 곧 버려질 지오메트리에 대해 박스와 격자 셀을
 * 계산하게 되고 격자에는 압축된 끝을 넘어선 인덱스가 남습니다.
 */
static void level_drop_degenerate(Level *out) {
    /* A sector with fewer than three points cannot be triangulated; drop it
       rather than letting it produce degenerate geometry later. */
    int w = 0;
    for (int i = 0; i < out->n_sectors; i++)
        if (out->sectors[i].n >= 3) out->sectors[w++] = out->sectors[i];
    out->n_sectors = w;
}

/**
 * @brief Caches every surviving sector's bounding box.
 * / 살아남은 모든 섹터의 바운딩 박스를 캐시합니다.
 */
static void level_cache_bounds(Level *out) {
    /* Cache each surviving sector's bounding box. After the compaction above,
       so the dropped sectors are not walked, and before anything can query the
       level -- point_in_sector rejects against these, so a level whose bounds
       were never computed would collide as if every sector were empty.
       살아남은 각 섹터의 바운딩 박스를 캐시합니다. 위의 압축 이후에 수행하여 버려진
       섹터를 순회하지 않으며, 레벨에 대한 질의가 가능해지기 전에 수행합니다.
       point_in_sector가 이 값으로 기각하므로, 경계값이 계산되지 않은 레벨은 모든 섹터가
       비어 있는 것처럼 충돌 판정됩니다. */
    for (int i = 0; i < out->n_sectors; i++)
        level_bounds(&out->sectors[i]);
}

/**
 * @brief Gives a locked door the material of the key it wants.
 * / 잠긴 문에 그것이 요구하는 열쇠의 재질을 부여합니다.
 */
static void level_apply_door_materials(Level *out) {
    /* --- a locked door wears its key -------------------------------------
     *
     * ENGLISH
     * -------
     * DERIVED, NOT AUTHORED. The key a door needs is already written in the
     * level text; making the author also write the matching material is asking
     * them to state the same fact twice, and the failure mode is a red door
     * that opens with the blue card -- a level that lies to the player about
     * its own rules, and lies convincingly, because the picture is exactly the
     * kind of thing a player trusts without checking.
     *
     * Only the generic `wall_door` is replaced. A door the author gave some
     * other surface keeps it, so this is a default rather than an override:
     * the moment it takes a decision away from whoever wrote the level, it
     * stops being a convenience and becomes an obstacle.
     *
     * This pairs with the HUD keycard row. The row says which cards you hold;
     * this says which card the door in front of you wants. Neither is much use
     * without the other -- knowing you have the blue card does not help at a
     * door whose colour you cannot see.
     *
     * 한국어
     * ------
     * 작성된 것이 아니라 *파생된* 것입니다. 문이 요구하는 열쇠는 이미 레벨 텍스트에 적혀
     * 있습니다. 작성자에게 대응하는 재질까지 쓰게 하는 것은 같은 사실을 두 번 말하라는
     * 것이고, 실패 형태는 파란 카드로 열리는 빨간 문입니다. 레벨이 자기 규칙에 대해
     * 플레이어에게 거짓말을 하는 것이며, 설득력 있게 합니다. 그림은 플레이어가 확인 없이
     * 믿는 바로 그런 것이기 때문입니다.
     *
     * 일반 `wall_door`만 교체합니다. 작성자가 다른 표면을 준 문은 그대로 유지되므로,
     * 이것은 덮어쓰기가 아니라 기본값입니다. 레벨을 쓴 사람에게서 결정을 빼앗는 순간
     * 편의가 아니라 방해가 됩니다.
     *
     * HUD의 키카드 행과 짝을 이룹니다. 그 행은 어떤 카드를 가졌는지 말하고, 이것은 앞에
     * 있는 문이 어떤 카드를 원하는지 말합니다. 한쪽만으로는 쓸모가 적습니다. 파란 카드를
     * 가졌다는 것을 알아도 색을 볼 수 없는 문 앞에서는 도움이 되지 않습니다. */
    for (int i = 0; i < out->n_doors; i++) {
        const DoorDef *d = &out->doors[i];
        if (d->key == KEY_NONE) continue;
        if (d->sector < 0 || d->sector >= out->n_sectors) continue;

        Sector *s = &out->sectors[d->sector];
        int wl = 0;
        while (wl < LVL_MAT && s->mat_wall[wl]) wl++;
        if (!txt_is(s->mat_wall, wl, "wall_door")) continue;

        /* Lowest set bit wins, the same rule door_key_name uses, so a door
           needing two cards shows the first of them rather than nothing.
           door_key_name과 같이 가장 낮은 비트가 이깁니다. 두 카드를 요구하는 문이
           아무것도 아닌 것이 아니라 그중 첫 번째를 보여 줍니다. */
        const char *m = (d->key & KEY_RED)    ? "door_red"
                      : (d->key & KEY_BLUE)   ? "door_blue"
                      : (d->key & KEY_YELLOW) ? "door_yellow" : 0;
        if (m) {
            int ml = 0;
            while (m[ml]) ml++;
            copy_name(s->mat_wall, LVL_MAT, m, ml);
        }
    }

}

/**
 * @brief Reads one `door` opcode: axis, distance, speed, tag and key.
 *
 * ENGLISH
 * -------
 * @param[in]     p   Cursor just past the opcode word.
 * @param[in,out] out Level receiving the door.
 * @param[in]     cur Sector the door moves, or null when this opcode is
 *                    outside the level being loaded -- which is also how a
 *                    door in some OTHER level is skipped, since `cur` is only
 *                    set while the wanted level is being read.
 * @return The cursor advanced past everything this opcode consumed.
 *
 * @note Sixty-five lines, and the second-largest thing the old ::level_load
 *       did. Out here it can be read on its own; inside, it was one of eleven
 *       branches and the only one with a nested tokenizer loop of its own.
 *
 * 한국어
 * ------
 * @brief `door` opcode 하나를 읽습니다. 축, 거리, 속도, 태그, 열쇠입니다.
 * @param[in]     p   opcode 단어 바로 뒤의 커서.
 * @param[in,out] out 문을 받을 레벨.
 * @param[in]     cur 문이 움직이는 섹터이며, 이 opcode가 로드 중인 레벨 바깥이면 널입니다.
 *                    `cur`는 원하는 레벨을 읽는 동안에만 설정되므로, *다른* 레벨의 문이
 *                    건너뛰어지는 방식이기도 합니다.
 * @return 이 opcode가 소비한 만큼 진행된 커서.
 *
 * @note 예순다섯 줄이며, 기존 ::level_load가 하던 일 중 두 번째로 큰 것이었습니다. 밖으로
 *       나오면 그 자체로 읽을 수 있습니다. 안에서는 열한 개 분기 중 하나였고, 자체 중첩
 *       토크나이저 루프를 가진 유일한 분기였습니다.
 */
static const char *parse_door(const char *p, Level *out, const Sector *cur) {
    int len;
    const char *ax = txt_token(p, &len);
    if (!ax) return p;
    p = ax + len;

    int axis = -1;
    if      (txt_is(ax, len, "up"))   axis = DOOR_UP;
    else if (txt_is(ax, len, "down")) axis = DOOR_DOWN;
    else if (txt_is(ax, len, "x"))    axis = DOOR_X;
    else if (txt_is(ax, len, "z"))    axis = DOOR_Z;
    if (axis < 0) return p;

    int amount, ok = 1;
    p = txt_read_int(p, &amount, &ok);
    if (!ok) return p;

    /* Defaults chosen so `door up 300` alone is a complete, sensible
       door: it opens on touch, needs no key, and travels at a speed
       that reads as a door rather than as a lift.
       `door up 300`만으로도 완결된 문이 되도록 기본값을 정했습니다. 접촉 시
       열리고, 열쇠가 필요 없으며, 승강기가 아니라 문으로 읽히는 속도로
       움직입니다. */
    int speed = 300, tag = 0, key = KEY_NONE;

    /* The door's own sub-opcodes, in any order and all optional. This inner
       loop keeps its OWN continue/break: `continue` takes the next sub-opcode,
       `break` hands the token back to the caller's loop. They are not the
       function's exits and must not become returns -- doing exactly that is
       how the first cut of this extraction silently parsed every door as
       keyless, which leveltest caught by finding no locked door in the arena.
       문 자신의 하위 opcode이며, 순서는 자유롭고 전부 선택적입니다. 이 내부 루프는 자기만의
       continue/break를 유지합니다. `continue`는 다음 하위 opcode로 넘어가고 `break`는 토큰을
       호출자의 루프에 돌려줍니다. 이것들은 함수의 탈출구가 *아니며* return이 되어서는 안
       됩니다. 이 추출의 첫 시도가 바로 그렇게 해서 모든 문을 조용히 열쇠 없는 문으로
       파싱했고, leveltest가 아레나에 잠긴 문이 없다는 것으로 그것을 잡아냈습니다. */
    for (;;) {
        const char *o = txt_token(p, &len);
        if (!o) break;

        if (txt_is(o, len, "speed")) {
            p = o + len;
            int v; p = txt_read_int(p, &v, &ok);
            if (ok && v > 0) speed = v;
            continue;
        }
        if (txt_is(o, len, "tag")) {
            p = o + len;
            int v; p = txt_read_int(p, &v, &ok);
            if (ok) tag = v;
            continue;
        }
        if (txt_is(o, len, "key")) {
            p = o + len;
            const char *c = txt_token(p, &len);
            if (!c) break;
            p = c + len;
            if      (txt_is(c, len, "red"))    key = KEY_RED;
            else if (txt_is(c, len, "blue"))   key = KEY_BLUE;
            else if (txt_is(c, len, "yellow")) key = KEY_YELLOW;
            continue;
        }
        break;      /* not ours: leave it for the outer loop */
    }

    if (!cur) return p;
    if (out->n_doors >= LVL_MAX_DOORS) { DIAG(DIAG_DOOR_CAP); return p; }

    DoorDef *d = &out->doors[out->n_doors++];
    d->sector = (short)(cur - out->sectors);
    d->axis   = (short)axis;
    d->amount = (short)amount;
    d->speed  = (short)speed;
    d->tag    = (short)tag;
    d->key    = (short)key;
    return p;
    return p;
}

/**
 * @brief Reads one `e` opcode: an entity's kind, position and parameters.
 *
 * ENGLISH
 * -------
 * @param[in]     p     Cursor just past the opcode word.
 * @param[in,out] out   Level receiving the entity.
 * @param[in]     found Non-zero while the wanted level is being read; entities
 *                      outside it are parsed and discarded, because the cursor
 *                      still has to travel over them.
 * @return The advanced cursor, or null when the text ran out mid-opcode --
 *         which stops the parse, the way the `break` in the old loop did.
 *
 * 한국어
 * ------
 * @brief `e` opcode 하나를 읽습니다. 엔티티의 종류, 위치, 매개변수입니다.
 * @param[in]     p     opcode 단어 바로 뒤의 커서.
 * @param[in,out] out   엔티티를 받을 레벨.
 * @param[in]     found 원하는 레벨을 읽는 동안 0이 아닙니다. 그 바깥의 엔티티는 파싱한 뒤
 *                      버립니다. 커서는 어차피 그 위를 지나가야 하기 때문입니다.
 * @return 진행된 커서. opcode 도중에 텍스트가 끝나면 널이며, 기존 루프의 `break`와 같이
 *         파싱을 중단시킵니다.
 */
static const char *parse_entity(const char *p, Level *out, int found) {
    int len;
    const char *kind = txt_token(p, &len);
    if (!kind) return 0;
    int klen = len;
    p = kind + len;

    int x, z, ok;
    p = txt_read_int(p, &x, &ok);
    if (!ok) return p;
    p = txt_read_int(p, &z, &ok);
    if (!ok) return p;

    /* OPTIONAL TRAILING NUMBERS, as many as the line supplies.
       Safe to attempt because txt_read_int leaves the stream exactly
       where it found it when the next token is not a number -- so
       reading past the end of one entity's line stops on the `e` or
       `s` that starts the next statement without consuming it. An
       entity that writes none keeps the zeros, which is what every
       level authored before this did.
       줄이 제공하는 만큼 선택적으로 뒤따르는 수치를 읽습니다. txt_read_int가 다음
       토큰이 숫자가 아닐 때 스트림을 발견한 그대로 남기므로 시도해도 안전합니다.
       한 엔티티의 줄 끝을 지나 읽어도 다음 문장을 시작하는 `e`나 `s`에서 멈추고
       그것을 소비하지 않습니다. 아무것도 쓰지 않은 엔티티는 0을 유지하며, 이 필드
       이전에 작성된 모든 레벨이 그렇습니다. */
    int par[LVL_ENT_PARAMS] = {0};
    for (int i = 0; i < LVL_ENT_PARAMS; i++) {
        int got, more;
        p = txt_read_int(p, &got, &more);
        if (!more) break;
        par[i] = got;
    }

    /* Parsed either way, stored only for the level being loaded: the cursor
       has to travel over another level's entities regardless, and returning
       early would leave it mid-line.
       어느 쪽이든 파싱하되 로드 중인 레벨에 대해서만 저장합니다. 커서는 다른 레벨의
       엔티티 위도 어차피 지나가야 하며, 일찍 반환하면 줄 중간에 남게 됩니다. */
    if (found && out->n_ents < LVL_MAX_ENTS) {
        Entity *e = &out->ents[out->n_ents++];
        copy_name(e->kind, LVL_MAT, kind, klen);
        e->x = (short)x;
        e->z = (short)z;
        for (int i = 0; i < LVL_ENT_PARAMS; i++) e->p[i] = (short)par[i];
    }
    return p;
}

/**
 * @brief Reads the level text and fills `out` with the named level.
 *
 * ENGLISH: The tokenizer loop and its eleven opcodes, which is the whole of
 * what the file format is. Separated from ::level_load so that function is a
 * statement of the four stages a load runs through rather than a place where
 * one of them happens to be three hundred lines long.
 * @return Non-zero when the named level was found.
 *
 * 한국어: 토크나이저 루프와 열한 개의 opcode이며, 파일 형식의 전부입니다. ::level_load에서
 * 분리하여 그 함수가 로드가 거치는 네 단계에 대한 서술이 되도록 했습니다. 그러지 않으면 그중
 * 하나가 우연히 삼백 줄인 자리가 됩니다.
 * @return 지정한 이름의 레벨을 찾으면 0이 아닙니다.
 */
static int level_parse_text(const char *name, Level *out) {
    const char *p = data_text(DATA_LEVELS);
    int found = 0, len;
    Sector *cur = 0;

    for (;;) {
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;

        if (txt_is(t, len, "l")) {
            if (found) break;               /* next level: this one is done */
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;
            found = txt_is(nm, len, name);
            if (found) copy_name(out->name, sizeof(out->name), nm, len);
            cur = 0;
            continue;
        }

        if (txt_is(t, len, "start")) {
            for (int i = 0; i < 3; i++) {
                int v, ok;
                p = txt_read_int(p, &v, &ok);
                if (!ok) break;
                if (found) out->start[i] = (short)v;
            }
            continue;
        }

        if (txt_is(t, len, "next")) {
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;
            if (found) copy_name(out->next, sizeof(out->next), nm, len);
            continue;
        }

        if (txt_is(t, len, "s")) {
            cur = 0;
            /* Reported BEFORE the room is refused, in the shape the light
               cap below already uses: ask whether the cap was hit, then ask
               whether there is room. Two statements rather than an else,
               because `found` gates both and an else would have to repeat it.
               See ::DIAG_SECTOR_CAP.
               방이 거절되기 *전에* 보고하며, 아래의 광원 상한이 이미 쓰는 형태를 따릅니다.
               상한에 닿았는지 먼저 묻고, 그다음 자리가 있는지 묻습니다. else가 아니라 문장
               둘인 이유는 `found`가 양쪽을 막고 있어 else가 그것을 다시 써야 하기
               때문입니다. ::DIAG_SECTOR_CAP을 참조하십시오. */
            if (found && out->n_sectors >= LVL_MAX_SECTORS) DIAG(DIAG_SECTOR_CAP);
            if (found && out->n_sectors < LVL_MAX_SECTORS) {
                cur = &out->sectors[out->n_sectors++];

                /* ZEROED WHOLE, then given the defaults that are not zero. The
                   fields used to be cleared one at a time, which meant a field
                   added later inherited whatever the previous level left in
                   this slot -- ::level_clear resets the COUNT, not the array.
                   A zeroed struct is reset by construction rather than by
                   somebody remembering to extend a list, which is the argument
                   ::run_reset is built on.
                   통째로 0으로 만든 뒤 0이 아닌 기본값만 부여합니다. 이전에는 필드를 하나씩
                   지웠고, 그 말은 나중에 추가된 필드가 이 슬롯에 이전 레벨이 남긴 값을
                   물려받는다는 뜻입니다. ::level_clear가 초기화하는 것은 배열이 아니라
                   *개수*입니다. 0으로 초기화된 구조체는 누군가 목록을 늘려 주기를 기다리지
                   않고 구조적으로 초기화되며, ::run_reset이 딛고 선 논거가 그것입니다. */
                Sector fresh = {0};
                *cur = fresh;
                cur->ceil = 300;    /* safe unless the file says otherwise */
                copy_name(cur->mat_floor, LVL_MAT, "brick", 5);
                copy_name(cur->mat_wall,  LVL_MAT, "brick", 5);
                copy_name(cur->mat_ceil,  LVL_MAT, "brick", 5);
            }
            continue;
        }

        if (txt_is(t, len, "floor") || txt_is(t, len, "ceil")) {
            int is_floor = txt_is(t, len, "floor");
            int v, ok;
            p = txt_read_int(p, &v, &ok);
            if (ok && cur) { if (is_floor) cur->floor = (short)v;
                             else          cur->ceil  = (short)v; }
            continue;
        }

        /* `hurt <dps>` -- lava, acid, a burning grate. Damage per SECOND, so
           crossing a corner costs less than standing in the middle and the
           rate is the same on any machine.
           `hurt <dps>`이며 용암, 산성 웅덩이, 불타는 격자 등입니다. *초당* 피해량이므로
           모서리를 스쳐 지나가는 것이 한가운데 서 있는 것보다 덜 들고, 어떤 기기에서도
           비율이 같습니다. */
        if (txt_is(t, len, "hurt")) {
            int v, ok;
            p = txt_read_int(p, &v, &ok);
            if (ok && cur) cur->hurt = (short)v;
            continue;
        }

        /* `door <up|down|x|z> <amount> [speed <n>] [tag <n>] [key <colour>]`
         *
         * ENGLISH
         * -------
         * Attaches to the sector being described, so a door is written inside
         * the `s` block it moves rather than in a list somewhere else that has
         * to name it. That is the same reason `hurt` sits here: a property of a
         * sector belongs with the sector, and a second table keyed by index is
         * a thing to keep in step.
         *
         * The optional words are read in a loop rather than positionally, so
         * `door up 300 key red` and `door up 300 tag 2 speed 400` are both
         * valid and neither needs a placeholder for what it does not say.
         *
         * 한국어
         * ------
         * 설명 중인 섹터에 붙습니다. 따라서 문은 그것을 지목해야 하는 다른 곳의 목록이
         * 아니라, 그것이 움직이는 `s` 블록 *안에* 기록됩니다. `hurt`가 이곳에 있는 것과 같은
         * 이유입니다. 섹터의 속성은 섹터와 함께 있어야 하며, 인덱스로 참조하는 두 번째 표는
         * 동기화를 유지해야 할 대상이 됩니다.
         *
         * 선택 단어들은 위치가 아니라 루프로 읽으므로, `door up 300 key red`와
         * `door up 300 tag 2 speed 400` 모두 유효하며 말하지 않은 것에 대한 자리
         * 표시자가 필요 없습니다.
         */
        if (txt_is(t, len, "door")) {
            p = parse_door(p, out, cur);
            continue;
        }

        /* `mat floor brick wall steel ceil brick` -- surface/name pairs, as
           many as are given. Reading only the first pair silently dropped the
           rest, so a file could say one thing and the engine do another. */
        if (txt_is(t, len, "mat")) {
            for (;;) {
                const char *save = p;
                const char *which = txt_token(p, &len);
                if (!which) break;
                int wlen = len;

                int is_floor = txt_is(which, wlen, "floor");
                int is_wall  = txt_is(which, wlen, "wall");
                int is_ceil  = txt_is(which, wlen, "ceil");
                if (!is_floor && !is_wall && !is_ceil) { p = save; break; }
                p = which + wlen;

                const char *nm = txt_token(p, &len);
                if (!nm) { p = save; break; }
                p = nm + len;

                if (cur) {
                    if      (is_floor) copy_name(cur->mat_floor, LVL_MAT, nm, len);
                    else if (is_ceil)  copy_name(cur->mat_ceil,  LVL_MAT, nm, len);
                    else               copy_name(cur->mat_wall,  LVL_MAT, nm, len);
                }
            }
            continue;
        }

        if (txt_is(t, len, "p")) {
            for (;;) {
                int x, z, ok;
                const char *save = p;
                p = txt_read_int(p, &x, &ok);
                if (!ok) { p = save; break; }
                p = txt_read_int(p, &z, &ok);
                if (!ok) { p = save; break; }
                /* `cur` rather than `found`: a sector the `s` handler above
                   refused leaves `cur` null, and those points belong to a room
                   already counted as missing. Reporting them again would charge
                   one fault twice and send the reader to the wrong constant.
                   See ::DIAG_POINT_CAP.
                   `found`이 아니라 `cur`입니다. 위의 `s` 처리기가 거절한 섹터는 `cur`을 널로
                   남기며, 그 점들은 이미 사라진 것으로 계수된 방의 것입니다. 다시 보고하면
                   하나의 결함에 두 번 값을 매기고 읽는 사람을 엉뚱한 상수로 보내게
                   됩니다. ::DIAG_POINT_CAP을 참조하십시오. */
                if (cur && cur->n >= LVL_MAX_PTS) DIAG(DIAG_POINT_CAP);
                if (cur && cur->n < LVL_MAX_PTS) {
                    cur->pts[cur->n * 2 + 0] = (short)x;
                    cur->pts[cur->n * 2 + 1] = (short)z;
                    cur->n++;
                }
            }
            continue;
        }

        if (txt_is(t, len, "e")) {
            p = parse_entity(p, out, found);
            if (!p) break;
            continue;
        }

        /* A point light: position, reach, colour and brightness.
         *
         *   light <x> <y> <z> <radius> <r> <g> <b> <power>
         *
         * Eight integers on one line rather than a block, because a light has
         * no optional parts -- every field is needed for it to appear at all,
         * so there is nothing for a multi-line form to make optional.
         *
         * Past LVL_MAX_LIGHTS the light is parsed and dropped, which is
         * reported: a room that is darker than the author intended gives no
         * hint that a cap was the cause.
         *
         * 점광원입니다. 위치, 도달 거리, 색상, 밝기로 구성됩니다.
         *
         * 블록이 아니라 한 줄에 정수 여덟 개인 이유는, 광원에 선택적인 부분이 없기
         * 때문입니다. 모든 필드가 있어야 광원이 나타나므로 여러 줄 형식이 선택적으로
         * 만들 것이 없습니다.
         *
         * LVL_MAX_LIGHTS를 넘으면 파싱되되 버려지며 이는 보고됩니다. 제작자의 의도보다
         * 어두운 방은 용량 한계가 원인이라는 단서를 주지 않기 때문입니다. */
        if (txt_is(t, len, "light")) {
            int v[8], ok = 1;
            for (int i = 0; i < 8 && ok; i++) p = txt_read_int(p, &v[i], &ok);
            if (!ok) continue;

            if (found && out->n_lights >= LVL_MAX_LIGHTS) DIAG(DIAG_LIGHT_CAP);
            if (found && out->n_lights < LVL_MAX_LIGHTS) {
                Light *L = &out->lights[out->n_lights++];
                L->x = (short)v[0]; L->y = (short)v[1]; L->z = (short)v[2];
                L->radius = (short)v[3];
                L->r = (short)v[4]; L->g = (short)v[5]; L->b = (short)v[6];
                L->power = (short)v[7];
            }
            continue;
        }
    }

    return found;
}

/* ======================================================= brush-backed levels
 *
 * ENGLISH
 * -------
 * A level made of brushes answers the same questions a level made of sectors
 * does, through the same functions, so that using a .map without a converter
 * did not also mean rewriting the eight modules that hold a `const Level *`.
 * ::Level::brushes is the whole of the switch; everything below is the four
 * answers it selects.
 *
 * 한국어
 * ------
 * 브러시로 만든 레벨은 섹터로 만든 레벨과 같은 질문에 같은 함수를 통해 답합니다. 변환기 없이
 * .map을 쓰는 일이 `const Level *`를 들고 있는 여덟 모듈을 다시 쓰는 일까지 뜻하지 않도록
 * 하기 위함입니다. ::Level::brushes가 전환의 전부이고, 아래는 그것이 고르는 네 가지 답입니다.
 */

/* The far edge of what a .map may describe, in world units. Traces that stand
   in for an unbounded query are clamped to it: ::player_spawn asks for ground
   with a step of 1e9, and a sweep to a billion metres has no float precision
   left by the time it arrives.
   .map이 기술할 수 있는 세계의 끝이며 월드 단위입니다. 무한한 질의를 대신하는 트레이스는
   여기에 맞춰 제한됩니다. ::player_spawn은 단차 1e9로 지면을 묻는데, 10억 미터까지의 스윕은
   도착할 무렵 남은 float 정밀도가 없습니다. */
#define BRUSH_EDGE (BRUSH_MAX_COORD * BRUSH_UNIT)

/* Far enough above a floor that the upward probe does not re-hit it through
   the skin the downward one left.
   하강 탐침이 남긴 스킨을 통과해 상승 탐침이 같은 바닥에 다시 닿지 않을 만큼의 간격입니다. */
#define CEIL_PROBE (BRUSH_SKIN * 4.0f)

/* level_ground and level_trace are POINT queries -- player.c samples five of
   them around its own radius, and a shot is a ray. So the box is a point, and
   the swept-box trace collapses to exactly the sweep they want.
   level_ground와 level_trace는 *점* 질의입니다. player.c는 자기 반경 둘레로 다섯 개를
   표본하고, 사격은 광선입니다. 따라서 상자는 점이며, 스윕 상자 트레이스는 그들이 원하는 바로
   그 스윕으로 축약됩니다. */
static const v3 POINT_BOX = { 0.0f, 0.0f, 0.0f };

/**
 * @brief The store a caller gets when it does not name one.
 *
 * ENGLISH
 * -------
 * THE STORAGE ITSELF IS ::BrushStore'S NOW, and this is one instance of it --
 * the one ::level_load and ::level_release use. What that changes is not where
 * the bytes are but WHOSE they are: ::LVL_BRUSH_SLOTS was a ceiling on how many
 * brush levels could be live in the PROCESS, and it is now a ceiling per store.
 * A second consumer that wants levels of its own takes a second store and
 * neither can starve the other. See brushstore.h.
 *
 * WHY THERE IS STILL A DEFAULT. The overwhelming case is a program that runs
 * one set of levels, and there are 31 calls to ::level_load in this project that
 * are all that program. Making every one of them name a store would be 31 copies
 * of a fact with one answer -- which is the argument pools.h makes for leaving
 * audio, post and menu where they are, applied to a caller instead of a module.
 * The difference from before is that this is now *a* store rather than *the*
 * store, and nothing is refused because something else already loaded a level.
 *
 * 한국어
 * ------
 * @brief 호출자가 저장소를 지목하지 않을 때 받는 저장소입니다.
 *
 * *저장 공간 자체는 이제 ::BrushStore의 것이며*, 이것은 그 인스턴스 하나입니다.
 * ::level_load와 ::level_release가 쓰는 것입니다. 그것이 바꾸는 것은 바이트가 어디 있는지가
 * 아니라 그것이 *누구의 것인지*입니다. ::LVL_BRUSH_SLOTS는 *프로세스* 안에서 몇 개의 브러시
 * 레벨이 살아 있을 수 있는지에 대한 천장이었고, 이제 저장소당 천장입니다. 자기 레벨을 원하는
 * 두 번째 소비자는 두 번째 저장소를 가지며 어느 쪽도 상대를 굶길 수 없습니다. brushstore.h를
 * 참조하십시오.
 *
 * *그럼에도 기본값이 있는 이유.* 압도적인 경우는 한 벌의 레벨을 돌리는 프로그램이며, 이
 * 프로젝트에는 그 프로그램에 해당하는 ::level_load 호출이 31개 있습니다. 그 전부가 저장소를
 * 지목하게 만드는 것은 답이 하나인 사실의 사본 31개를 만드는 일이며, pools.h가 audio·post·menu를
 * 있던 자리에 두는 근거로 펴는 논지를 모듈이 아니라 호출자에 적용한 것입니다. 이전과 다른 점은
 * 이것이 이제 *그* 저장소가 아니라 *하나의* 저장소라는 것이며, 다른 무언가가 이미 레벨을
 * 로드했다는 이유로 거절되는 일이 없다는 것입니다.
 */
static BrushStore g_default_store;

/** @brief NULL means the default. One place decides, so no caller has to. / NULL이면 기본값입니다. 한 곳이 결정하므로 어떤 호출자도 결정하지 않아도 됩니다. */
static BrushStore *store_or_default(BrushStore *bs) {
    return bs ? bs : &g_default_store;
}

/* WHAT IDENTIFIES THE HOLDER OF A SLOT, and the whole of this change is that it
   is no longer a `Level *`.

   It was. `g_brush_owner[i] == out` matched a load against the ADDRESS of the
   Level that last took the slot, and an address says nothing about whether the
   object at it is still the same object -- or still an object. world.c's
   level-chain scan loads into a stack local and returns; that address then sat
   in this table, and the next Level to land on it inherited a brush map it had
   never loaded. The eviction path was worse: it wrote through the stored
   pointer, so telling a dead Level it had been evicted was a write to a stack
   frame that no longer existed.

   A serial is issued once and never reused, so a stale key matches nothing and
   is simply not a claim. Nothing here records where a Level lives, which is
   what makes "is that Level still alive?" a question this file never has to
   answer.

   슬롯을 쥔 쪽을 무엇으로 식별하는가이며, 이번 변경의 전부는 그것이 더 이상 `Level *`가
   아니라는 점입니다.

   이전에는 그러했습니다. `g_brush_owner[i] == out`은 로드를 마지막으로 슬롯을 가져간 Level의
   *주소*와 대응시켰는데, 주소는 그 자리의 객체가 여전히 같은 객체인지, 애초에 객체이기는
   한지에 대해 아무 말도 하지 않습니다. world.c의 레벨 사슬 스캔은 스택 지역 변수에 로드한 뒤
   반환합니다. 그 주소가 이 표에 남았고, 그 자리에 놓인 다음 Level은 자신이 로드한 적 없는
   브러시 맵을 물려받았습니다. 축출 경로는 더 나빴습니다. 저장된 포인터를 통해 기록했으므로,
   죽은 Level에게 축출되었다고 알리는 일이 더 이상 존재하지 않는 스택 프레임에 대한 쓰기가
   되었습니다.

   일련번호는 한 번 발급되고 재사용되지 않으므로, 낡은 키는 어느 것과도 맞지 않고 그저 주장이
   아닐 뿐입니다. 이곳의 무엇도 Level이 어디 사는지 기록하지 않으며, 그것이 "그 Level이 아직
   살아 있는가"를 이 파일이 결코 답하지 않아도 되게 만듭니다.

   The key table and the serial counter are fields of ::BrushStore now rather
   than file-scope arrays here; the reasoning above is why they are serials, and
   brushstore.h is why they moved.
   키 표와 일련번호 계수기는 이제 이곳의 파일 스코프 배열이 아니라 ::BrushStore의 필드입니다.
   위의 논거는 그것들이 왜 일련번호인지에 대한 것이고, 왜 옮겨 갔는지는 brushstore.h입니다. */

static BrushMap *brush_slot_for(BrushStore *bs, Level *out) {
    /* The slot this Level already holds, so reloading the running level -- a
       restart, a hot reload -- reuses its own storage rather than taking the
       scan's. Asked first, and asked of the key rather than of the address.
       이 Level이 이미 쥐고 있는 슬롯입니다. 실행 중인 레벨을 다시 로드하는 경우(재시작, 핫
       리로드) 스캔의 것을 빼앗지 않고 자기 저장 공간을 재사용합니다. 주소가 아니라 키에게,
       그리고 가장 먼저 묻습니다. */
    if (out->brush_key)
        for (int i = 0; i < LVL_BRUSH_SLOTS; i++)
            if (bs->key[i] == out->brush_key) return &bs->map[i];

    for (int i = 0; i < LVL_BRUSH_SLOTS; i++) {
        if (bs->key[i]) continue;

        /* Stepped past 0 here rather than by an init call, because 0 is what a
           zeroed store starts at and 0 must never be issued -- it is what
           `Level l = {0}` holds and has to mean "no claim". Doing it at the
           point of issue is what keeps `static BrushStore s;` a valid empty
           store with nothing to call on it.
           초기화 호출이 아니라 이곳에서 0을 넘깁니다. 0은 0으로 초기화된 저장소가 시작하는
           값이며 결코 발급되어서는 안 됩니다. `Level l = {0}`이 가진 값이자 "주장 없음"을
           뜻해야 하기 때문입니다. 발급 시점에 처리하는 것이 `static BrushStore s;`를 아무것도
           호출할 필요 없는 유효한 빈 저장소로 유지하는 방법입니다. */
        if (!bs->next_key) bs->next_key = 1;

        bs->key[i]     = bs->next_key++;
        out->brush_key = bs->key[i];
        return &bs->map[i];
    }

    /* Full, and THE NEWCOMER IS REFUSED rather than an incumbent evicted.

       Eviction was the old answer and it could not be made safe: the evicted
       Level has to be told, or it goes on reading storage that now holds
       somebody else's map, and telling it means holding its address -- which is
       the defect above. Refusing needs no such reach. The caller gets 0 from
       ::load_brush_level, falls through to the text loader, and a level that
       cannot be loaded leaves the player where they are, which is the contract
       ::level_load already keeps for a name that does not resolve.

       Refusing also fails toward the incumbent rather than away from it. Two
       slots are enough for the two Levels this project runs at once; a third
       asking is a new situation, and quietly breaking one of the two that were
       already working is the worst of the available answers.

       가득 찼으며, 기존 것을 축출하는 대신 *새로 온 쪽을 거절*합니다.

       축출이 이전의 답이었고 안전하게 만들 수 없었습니다. 축출된 Level에게 알려야 하며,
       그러지 않으면 이제 남의 맵이 든 저장 공간을 계속 읽습니다. 그리고 알리려면 그 주소를
       쥐고 있어야 하는데, 그것이 위에서 말한 결함입니다. 거절은 그렇게 손을 뻗을 필요가
       없습니다. 호출자는 ::load_brush_level에서 0을 받고 텍스트 로더로 내려가며, 로드할 수
       없는 레벨은 플레이어를 있던 자리에 둡니다. 해석되지 않는 이름에 대해 ::level_load가
       이미 지키는 계약입니다.

       또한 거절은 기존 쪽을 향해서가 아니라 기존 쪽을 *지키는* 방향으로 실패합니다. 슬롯 둘은
       이 프로젝트가 동시에 돌리는 Level 둘에 충분하며, 셋째가 요청하는 것은 새로운 상황입니다.
       이미 동작하던 둘 중 하나를 조용히 망가뜨리는 것은 가능한 답 중 최악입니다. */
    DIAG(DIAG_LEVEL_SLOTS);
    return 0;
}

void level_release(Level *l) { level_release_in(0, l); }

void level_release_in(BrushStore *bs, Level *l) {
    if (!l) return;
    BrushStore *st = store_or_default(bs);

    if (l->brush_key)
        for (int i = 0; i < LVL_BRUSH_SLOTS; i++)
            if (st->key[i] == l->brush_key) { st->key[i] = 0; break; }

    /* Both, and in this order does not matter -- what matters is that neither
       is left behind. A Level that gave back its slot but kept the pointer is
       the exact state eviction used to produce.
       둘 다이며 순서는 상관없습니다. 중요한 것은 어느 쪽도 남지 않는 것입니다. 슬롯은
       돌려주고 포인터는 쥐고 있는 Level이 바로 축출이 만들어 내던 그 상태입니다. */
    l->brush_key = 0;
    l->brushes   = 0;
}

/* The player start, in the units ::Level already speaks: centimetres and
   millidegrees. The 90 is Quake's angle convention meeting this engine's yaw --
   tools/mapview.c derives it and says why it is a conversion and not a copy. */
static void brush_start_of(Level *out, const BrushMap *bm) {
    for (int i = 0; i < bm->n_ents; i++) {
        const BrushEnt *e = &bm->ents[i];
        const char *cn = brush_ent_value(e, "classname");
        if (!cn || !txt_eq(cn, "info_player_start")) continue;

        v3 o;
        if (!brush_ent_point(e, "origin", &o)) continue;
        out->start[0] = (short)(o.x * 100.0f);
        out->start[1] = (short)(o.z * 100.0f);
        out->start_h  = (short)(o.y * 100.0f);

        float ang = brush_ent_num(e, "angle", 90.0f);
        if (ang < 0.0f) ang = 90.0f;         /* -1 and -2 are up and down */
        out->start[2] = (short)((ang - 90.0f) * 1000.0f);
        return;
    }
}

/**
 * Quake's `light` entities, turned into the lamps ::bake_light already knows.
 *
 * ENGLISH
 * -------
 * WHAT QUAKE WRITES, because that is what an author placing a light in
 * TrenchBroom gets:
 *
 *   light    brightness, defaulting to 300. Read as REACH here, because this
 *            engine separates reach from brightness and Quake does not -- its
 *            single number is a radius under a linear falloff, which is the
 *            same shape ::Light::radius has. A brighter Quake light is a light
 *            that carries further, and keeping that is what makes a value
 *            copied from a Quake tutorial land where the author expected.
 *   _color   the colour. Accepted in both spellings the tools use: 0..1 floats
 *            and 0..255 bytes, told apart by whether anything exceeds 1. That
 *            is ericw-tools' own rule, and guessing wrong turns a white lamp
 *            into one at 1/255 brightness, which reads as no lamp at all.
 *
 * ::Light::power stays at the reference 100. Quake has no second brightness
 * number to take it from, and inventing a key the editor does not offer would
 * be a control nobody can reach.
 *
 * 한국어
 * ------
 * Quake의 `light` 엔티티를 ::bake_light가 이미 아는 등으로 바꿉니다.
 *
 * Quake가 쓰는 것을 읽습니다. TrenchBroom에서 광원을 놓는 제작자가 얻는 것이 그것이기
 * 때문입니다.
 *
 *   light    밝기이며 기본값 300입니다. 이곳에서는 *도달 거리*로 읽습니다. 이 엔진은 도달
 *            거리와 밝기를 분리하지만 Quake는 그러지 않으며, 그 하나의 숫자는 선형 감쇠
 *            아래의 반경으로 ::Light::radius와 같은 형태입니다. 더 밝은 Quake 광원은 더 멀리
 *            닿는 광원이고, 그것을 지키는 것이 Quake 강좌에서 복사한 값이 제작자가 기대한
 *            자리에 떨어지게 합니다.
 *   _color   색상입니다. 도구들이 쓰는 두 표기를 모두 받습니다. 0..1 실수와 0..255 바이트이며,
 *            1을 넘는 값이 있는지로 구별합니다. ericw-tools 자신의 규칙이고, 잘못 추측하면
 *            흰 등이 1/255 밝기가 되어 등이 없는 것처럼 보입니다.
 *
 * ::Light::power는 기준값 100으로 둡니다. Quake에는 그것을 가져올 두 번째 밝기 숫자가 없고,
 * 에디터가 제공하지 않는 키를 만들어 내는 것은 아무도 닿을 수 없는 조절 장치입니다.
 */
/* The sun and the sky the worldspawn declares, if it declares any.
 *
 * ENGLISH
 * -------
 * ericw-tools lights an outdoor Quake level with `_sunlight` (a directional
 * sun), `_sun_mangle` (which way it shines) and `_sunlight2` (a dome of sky
 * light). None of the three is a point light, so ::brush_lights_of never saw
 * them and an imported outdoor map arrived with only its accent lamps -- which
 * on `lqdm1` is 0.5% of the light the author placed.
 *
 * MANGLE IS YAW, PITCH, ROLL, IN DEGREES, and it names the direction the light
 * TRAVELS: ericw's default is "0 -90 0", straight down. ::Level::sun wants the
 * direction the sun is IN, so this negates once here rather than at every
 * vertex of every bake.
 *
 * Roll is read and discarded. A directional light has no roll -- it is in the
 * key because mangle is a general orientation triple -- and silently ignoring
 * the third number is better than pretending the parse used it.
 *
 * 한국어
 * ------
 * ericw-tools는 야외 Quake 레벨을 `_sunlight`(방향성 태양), `_sun_mangle`(비추는 방향),
 * `_sunlight2`(하늘 돔 조명)로 조명합니다. 셋 다 점광원이 아니므로 ::brush_lights_of는 그것들을
 * 본 적이 없고, 가져온 야외 맵은 장식용 램프만 지닌 채 도착했습니다. `lqdm1`에서 그것은 제작자가
 * 배치한 빛의 0.5%입니다.
 *
 * *mangle은 도 단위의 yaw, pitch, roll이며* 빛이 *진행하는* 방향을 가리킵니다. ericw의 기본값은
 * "0 -90 0", 곧 수직 아래입니다. ::Level::sun은 태양이 *있는* 방향을 원하므로, 모든 베이크의
 * 모든 정점에서가 아니라 이곳에서 한 번 부호를 뒤집습니다.
 *
 * roll은 읽고 버립니다. 방향성 광원에는 roll이 없습니다. mangle이 일반적인 방향 삼중항이라
 * 키에 들어 있을 뿐이며, 세 번째 수를 조용히 무시하는 것이 그것을 쓴 척하는 것보다 낫습니다. */
static void brush_sun_of(Level *out, const BrushMap *bm) {
    out->sun[0] = out->sun[1] = out->sun[2] = 0.0f;
    out->sun_power = 0;
    out->sky_power = 0;

    for (int i = 0; i < bm->n_ents; i++) {
        const BrushEnt *e = &bm->ents[i];
        const char *cn = brush_ent_value(e, "classname");
        if (!cn || !txt_eq(cn, "worldspawn")) continue;

        float sun = brush_ent_num(e, "_sunlight",  0.0f);
        float sky = brush_ent_num(e, "_sunlight2", 0.0f);
        if (sun <= 0.0f && sky <= 0.0f) return;      /* an indoor level */

        out->sun_power = (short)clampf(sun, 0.0f, 32000.0f);
        out->sky_power = (short)clampf(sky, 0.0f, 32000.0f);

        /* Straight down when the map declares a sun and no angle for it,
           which is ericw's own default and the sensible one: a level that
           says "there is a sun" and nothing else means overhead.
           맵이 태양은 선언하고 각도는 선언하지 않으면 수직 아래입니다. ericw 자신의
           기본값이며 합리적인 값입니다. "태양이 있다"고만 말하는 레벨은 머리 위를
           뜻합니다. */
        float m[3] = { 0.0f, -90.0f, 0.0f };
        brush_ent_triple(e, "_sun_mangle", m);

        float yaw = m[0] * 0.01745329f, pitch = m[1] * 0.01745329f;
        float cp = cosf(pitch);
        /* Quake's axes, then this engine's: ::map_dir is (x, z, -y) and the
           same rotation has to happen to a direction as to a point.
           Quake의 축에서 이 엔진의 축으로 옮깁니다. 방향에도 점과 같은 회전이 필요합니다. */
        float qx = cosf(yaw) * cp, qy = sinf(yaw) * cp, qz = sinf(pitch);
        float ex = qx, ey = qz, ez = -qy;

        /* Negated: the file says where the light goes, the bake asks where it
           comes from.
           부호를 뒤집습니다. 파일은 빛이 가는 곳을 말하고, 베이크는 오는 곳을 묻습니다. */
        float len = sqrtf(ex * ex + ey * ey + ez * ez);
        if (len < 1e-6f) { out->sun_power = 0; return; }
        out->sun[0] = -ex / len;
        out->sun[1] = -ey / len;
        out->sun[2] = -ez / len;
        return;
    }
}

static void brush_lights_of(Level *out, const BrushMap *bm) {
    for (int i = 0; i < bm->n_ents; i++) {
        const BrushEnt *e = &bm->ents[i];
        const char *cn = brush_ent_value(e, "classname");
        /* ::txt_eq, not ::txt_is with a length. The length form stops comparing
           where the literal ends, so this read `light` and accepted
           `light_fluoro`, `light_torch_small` and every other member of Quake's
           lamp family as a point light with this one's fields -- which the
           imported maps have not carried yet and would the first time one did.
           Nine sites had that shape; see the note on ::txt_eq.
           길이를 넘기는 ::txt_is가 아니라 ::txt_eq입니다. 길이 형태는 리터럴이 끝나는
           곳에서 비교를 멈추므로, 이 줄은 `light`를 읽으면서 `light_fluoro`,
           `light_torch_small`을 비롯한 Quake 등 계열 전부를 이곳의 필드를 가진 점광원으로
           받아들였습니다. 가져온 맵들이 아직 그것을 나르지 않았을 뿐이며, 하나라도 나르는
           순간 그렇게 됩니다. 아홉 곳이 그 형태였습니다. ::txt_eq의 설명을 참조하십시오. */
        if (!cn || !txt_eq(cn, "light")) continue;

        v3 o;
        if (!brush_ent_point(e, "origin", &o)) continue;

        if (out->n_lights >= LVL_MAX_LIGHTS) { DIAG(DIAG_LIGHT_CAP); continue; }
        Light *L = &out->lights[out->n_lights++];

        L->x = (short)(o.x * 100.0f);
        L->y = (short)(o.y * 100.0f);
        L->z = (short)(o.z * 100.0f);

        /* Map units to centimetres: the reach is authored in the file's own
           units and stored in the level's. */
        float reach = brush_ent_num(e, "light", 300.0f) * BRUSH_UNIT * 100.0f;
        if (reach < 1.0f)     reach = 1.0f;
        if (reach > 32000.0f) reach = 32000.0f;   /* Light::radius is a short */
        L->radius = (short)reach;

        float c[3] = { 1.0f, 1.0f, 1.0f };
        if (!brush_ent_triple(e, "_color", c)) c[0] = c[1] = c[2] = 1.0f;
        /* Told apart by magnitude, which is what the tools do. A colour of
           "1 1 1" is white either way; "255 255 255" can only be bytes. */
        float s = (c[0] > 1.0f || c[1] > 1.0f || c[2] > 1.0f) ? 1.0f : 255.0f;
        L->r = (short)clampf(c[0] * s, 0.0f, 255.0f);
        L->g = (short)clampf(c[1] * s, 0.0f, 255.0f);
        L->b = (short)clampf(c[2] * s, 0.0f, 255.0f);

        L->power = 100;
    }
}

/**
 * Quake's `func_door` entities, turned into the doors door.c already drives.
 *
 * ENGLISH
 * -------
 * HOW FAR IT TRAVELS IS NOT AUTHORED, and that is Quake's rule rather than a
 * shortcut: a door slides its own size along the direction it opens, less a
 * `lip` that stays behind so the leaf never vanishes completely. An author who
 * resizes a door gets the right travel without editing a second number, and
 * the second number is exactly the kind that goes stale.
 *
 *   angle    -1 up, -2 down, otherwise a heading in degrees. The headings are
 *            Quake's -- 0 east, 90 north -- and become this engine's axes the
 *            same way ::brush_start_of converts a facing.
 *   speed    units per second, default 100, converted to the centimetres per
 *            second ::DoorDef speaks.
 *   lip      units left behind, default 8, which is Quake's.
 *
 * 한국어
 * ------
 * Quake의 `func_door` 엔티티를 door.c가 이미 구동하는 문으로 바꿉니다.
 *
 * 얼마나 움직이는지는 제작된 값이 아니며, 이는 편법이 아니라 Quake의 규칙입니다. 문은 열리는
 * 방향으로 자기 크기만큼 미끄러지되, 문짝이 완전히 사라지지 않도록 `lip`만큼은 남깁니다.
 * 문의 크기를 바꾼 제작자는 두 번째 숫자를 고치지 않고도 옳은 이동 거리를 얻으며, 그 두 번째
 * 숫자야말로 낡아 버리는 종류의 숫자입니다.
 */
/**
 * Names, interned to the small numbers the door state machine compares.
 *
 * ENGLISH
 * -------
 * A .map links by name: a trigger names its `target`, a door its `targetname`,
 * and the two match when the strings do. ::DoorDef::tag is a number because
 * that is what door.c has always compared and there is no reason to make it
 * carry strings -- so the names are turned into numbers here, in the order they
 * are first met.
 *
 * ORDER OF FIRST APPEARANCE, deliberately, rather than a hash. Two names that
 * hashed alike would silently wire a trigger to the wrong door, and a hash
 * needs a width chosen against a collision rate nobody can measure from inside
 * one level. Sixteen names is the cap, which is ::LVL_MAX_DOORS, because a name
 * that no door answers to has nothing to fire.
 *
 * 한국어
 * ------
 * 이름을 문 상태 기계가 비교하는 작은 숫자로 사상합니다.
 *
 * .map은 이름으로 연결합니다. 트리거가 자신의 `target`을, 문이 자신의 `targetname`을
 * 지목하며, 문자열이 일치하면 둘이 대응합니다. ::DoorDef::tag가 숫자인 이유는 door.c가 늘
 * 비교해 온 것이 숫자이고 그것에 문자열을 지우게 할 이유가 없기 때문입니다. 그래서 이름은
 * 이곳에서 숫자가 되며, 처음 마주친 순서를 따릅니다.
 *
 * 해시가 아니라 처음 등장한 순서인 것은 의도적입니다. 같은 값으로 해시되는 두 이름은 트리거를
 * 조용히 엉뚱한 문에 연결하며, 해시는 레벨 하나 안에서는 아무도 잴 수 없는 충돌률에 맞춰 폭을
 * 골라야 합니다. 상한은 이름 열여섯 개이고 이는 ::LVL_MAX_DOORS입니다. 어떤 문도 응답하지 않는
 * 이름에는 발동시킬 것이 없기 때문입니다.
 */
typedef struct {
    char names[LVL_MAX_DOORS][BR_VAL];
    int  n;
} TagPool;

static int tag_for(TagPool *tp, const char *name) {
    if (!name || !name[0]) return 0;          /* unnamed: opens on touch */

    for (int i = 0; i < tp->n; i++) {
        int k = 0;
        while (tp->names[i][k] && tp->names[i][k] == name[k]) k++;
        if (!tp->names[i][k] && !name[k]) return i + 1;
    }
    if (tp->n >= LVL_MAX_DOORS) { DIAG(DIAG_DOOR_CAP); return 0; }

    txt_copy(tp->names[tp->n], BR_VAL, name, -1);
    return ++tp->n;                            /* tags are 1-based; 0 is "none" */
}

/* `key red`, or the mask an editor's dropdown writes. Both spellings, because
   the .map is read by a person as often as by TrenchBroom and `1` says
   nothing at all.
   `key red`이거나 에디터의 드롭다운이 쓰는 마스크입니다. 두 표기를 모두 받는 이유는 .map을
   TrenchBroom만큼이나 사람이 자주 읽고, `1`은 아무것도 말해 주지 않기 때문입니다. */
static int key_for(const BrushEnt *e) {
    const char *v = brush_ent_value(e, "key");
    if (!v || !v[0]) return KEY_NONE;

    if (v[0] >= '0' && v[0] <= '9') {
        int m = 0;
        for (int i = 0; v[i] >= '0' && v[i] <= '9'; i++) m = m * 10 + (v[i] - '0');
        return m & (KEY_RED | KEY_BLUE | KEY_YELLOW);
    }
    if (txt_eq(v, "red"))    return KEY_RED;
    if (txt_eq(v, "blue"))   return KEY_BLUE;
    if (txt_eq(v, "yellow")) return KEY_YELLOW;
    return KEY_NONE;
}

/**
 * `trigger_*` entities: their brushes stop being solid and become a volume.
 *
 * ENGLISH: The classname prefix is all that is read, so `trigger_multiple` and
 * `trigger_once` both arrive here -- this engine has no notion of a trigger
 * that fires only once, and a door that reopens is closer to what the name
 * `trigger_once` promises than refusing to load it would be.
 *
 * 한국어: classname 접두사만 읽으므로 `trigger_multiple`과 `trigger_once`가 모두 이곳에
 * 도착합니다. 이 엔진에는 한 번만 발동하는 트리거라는 개념이 없으며, 다시 열리는 문이
 * `trigger_once`라는 이름이 약속하는 것에 더 가깝습니다. 로드를 거부하는 것보다는 그렇습니다.
 *
 * EXCEPT `trigger_hurt`, which shares the prefix and nothing else: it fires no
 * target and it never stops firing. ::brush_hazards_of takes it. Reading the
 * prefix alone would have made every lava pit a nameless door switch.
 * 다만 `trigger_hurt`는 예외입니다. 접두사만 공유할 뿐 나머지는 다릅니다. 그것은 target을
 * 발동시키지 않고 발동을 멈추지도 않습니다. ::brush_hazards_of가 가져갑니다. 접두사만
 * 읽었다면 모든 용암 구덩이가 이름 없는 문 스위치가 되었을 것입니다.
 */
static void brush_triggers_of(Level *out, BrushMap *bm, TagPool *tp) {
    for (int i = 0; i < bm->n_ents; i++) {
        const BrushEnt *e = &bm->ents[i];
        const char *cn = brush_ent_value(e, "classname");
        if (!cn) continue;

        static const char PRE[] = "trigger_";
        int n = 0;
        while (PRE[n] && cn[n] == PRE[n]) n++;
        if (PRE[n]) continue;
        if (txt_eq(cn, "trigger_hurt")) continue;
        if (e->n_brushes < 1) continue;

        /* Walked into, not bumped into. Cleared before anything can trace
           against them, which is why this runs at load and not on first touch.
           부딪히는 것이 아니라 걸어 들어가는 것입니다. 무엇도 그것에 대해 판정하기 전에
           지웁니다. 이것이 첫 접촉이 아니라 로드 시점에 실행되는 이유입니다. */
        for (int k = 0; k < e->n_brushes; k++)
            bm->brushes[e->first_brush + k].solid = 0;

        if (out->n_triggers >= LVL_MAX_TRIGGERS) { DIAG(DIAG_ENT_CAP); continue; }
        TriggerDef *t = &out->triggers[out->n_triggers++];
        t->first_brush = (short)e->first_brush;
        t->n_brushes   = (short)e->n_brushes;
        t->tag         = (short)tag_for(tp, brush_ent_value(e, "target"));
    }
}

/**
 * `trigger_hurt`: the lava, as a volume rather than as a floor.
 *
 * ENGLISH: Same two moves as a trigger -- the brushes stop being solid and
 * become a region -- and then the difference. A trigger asks "who do I tell";
 * this asks "how much, per second". Quake spells that `dmg` and so does this,
 * because an author who has read any Quake tutorial has already typed it.
 *
 * The rate is stored as a short and clamped to one: `dmg 0` on a volume the
 * author bothered to draw is a mistake in the file rather than a request for a
 * harmless pool, and a hazard that does nothing is the kind of thing nobody
 * notices until they wonder why the lava is safe. A pool that should not hurt
 * is a pool with no trigger_hurt in it.
 *
 * 한국어: 트리거와 같은 두 동작입니다. 브러시가 고체이기를 멈추고 영역이 됩니다. 그다음이
 * 차이입니다. 트리거는 "누구에게 알리는가"를 묻고, 이것은 "초당 얼마인가"를 묻습니다.
 * Quake가 그것을 `dmg`라 쓰고 이곳도 그렇게 씁니다. Quake 강좌를 하나라도 읽은 제작자는
 * 이미 그것을 입력해 보았기 때문입니다.
 *
 * 비율은 short로 저장하고 1로 하한을 둡니다. 제작자가 굳이 그려 놓은 부피의 `dmg 0`은
 * 무해한 웅덩이를 요청한 것이 아니라 파일의 실수이며, 아무 일도 하지 않는 위험 지형은 용암이
 * 왜 안전한지 의아해지기 전까지 아무도 알아채지 못하는 종류의 것입니다. 아프지 않아야 할
 * 웅덩이는 trigger_hurt가 들어 있지 않은 웅덩이입니다.
 */
static void brush_hazards_of(Level *out, BrushMap *bm) {
    for (int i = 0; i < bm->n_ents; i++) {
        const BrushEnt *e = &bm->ents[i];
        const char *cn = brush_ent_value(e, "classname");
        if (!cn || !txt_eq(cn, "trigger_hurt")) continue;
        if (e->n_brushes < 1) continue;

        for (int k = 0; k < e->n_brushes; k++)
            bm->brushes[e->first_brush + k].solid = 0;

        if (out->n_hazards >= LVL_MAX_HAZARDS) { DIAG(DIAG_ENT_CAP); continue; }
        HazardDef *h = &out->hazards[out->n_hazards++];
        h->first_brush = (short)e->first_brush;
        h->n_brushes   = (short)e->n_brushes;

        float dps = brush_ent_num(e, "dmg", (float)LVL_HURT_DEFAULT);
        if (dps < 1.0f) dps = 1.0f;
        h->dps = (short)dps;
    }
}

static void brush_doors_of(Level *out, const BrushMap *bm, TagPool *tp) {
    for (int i = 0; i < bm->n_ents; i++) {
        const BrushEnt *e = &bm->ents[i];
        const char *cn = brush_ent_value(e, "classname");
        if (!cn || !txt_eq(cn, "func_door")) continue;
        if (e->n_brushes < 1) continue;          /* a door with no leaf */

        if (out->n_doors >= LVL_MAX_DOORS) { DIAG(DIAG_DOOR_CAP); continue; }

        /* The group's own extent, which is what the travel is measured from. */
        v3 lo = v3f(1e30f, 1e30f, 1e30f), hi = v3f(-1e30f, -1e30f, -1e30f);
        int any = 0;
        for (int k = 0; k < e->n_brushes; k++) {
            const Brush *b = &bm->brushes[e->first_brush + k];
            if (b->min.x > b->max.x) continue;
            if (b->min.x < lo.x) lo.x = b->min.x;
            if (b->min.y < lo.y) lo.y = b->min.y;
            if (b->min.z < lo.z) lo.z = b->min.z;
            if (b->max.x > hi.x) hi.x = b->max.x;
            if (b->max.y > hi.y) hi.y = b->max.y;
            if (b->max.z > hi.z) hi.z = b->max.z;
            any = 1;
        }
        if (!any) continue;                      /* nothing bounded, nothing to move */

        DoorDef *d = &out->doors[out->n_doors++];
        d->sector      = -1;
        d->first_brush = (short)e->first_brush;
        d->n_brushes   = (short)e->n_brushes;
        /* Named, so a trigger can reach it. An unnamed door opens on touch,
           which is what door.c already does when the tag is zero.
           이름이 있으면 트리거가 닿을 수 있습니다. 이름 없는 문은 접촉으로 열리며, 태그가
           0일 때 door.c가 이미 하는 일입니다. */
        d->tag         = (short)tag_for(tp, brush_ent_value(e, "targetname"));
        d->key         = (short)key_for(e);

        float ang  = brush_ent_num(e, "angle", -1.0f);
        float span;
        if (ang <= -1.5f)      { d->axis = DOOR_DOWN; span = hi.y - lo.y; }
        else if (ang < 0.0f)   { d->axis = DOOR_UP;   span = hi.y - lo.y; }
        else {
            /* Quake's headings against this engine's axes: east is +x, north
               is -z. A door opening south or west travels the other way, which
               ::DoorDef::amount carries as a sign. */
            int quarter = ((int)(ang + 0.5f) / 90) & 3;
            if (quarter == 0 || quarter == 2) { d->axis = DOOR_X; span = hi.x - lo.x; }
            else                              { d->axis = DOOR_Z; span = hi.z - lo.z; }
            (void)quarter;
        }

        float lip   = brush_ent_num(e, "lip", 8.0f) * BRUSH_UNIT;
        float travel = span - lip;
        if (travel < 0.0f) travel = 0.0f;

        /* Signed for the horizontal axes, because DOOR_X and DOOR_Z name a
           line and not a direction along it. */
        if (d->axis == DOOR_X && (int)(ang + 0.5f) >= 180) travel = -travel;
        if (d->axis == DOOR_Z && (int)(ang + 0.5f) <  180) travel = -travel;

        d->amount = (short)clampf(travel * 100.0f, -32000.0f, 32000.0f);

        float sp = brush_ent_num(e, "speed", 100.0f) * BRUSH_UNIT * 100.0f;
        d->speed = (short)clampf(sp, 1.0f, 32000.0f);
    }
}

/**
 * Point entities, turned into the markers pickup.c and enemy.c already claim.
 *
 * ENGLISH
 * -------
 * A PREFIX IS STRIPPED AND NOTHING ELSE IS TRANSLATED. `monster_imp` becomes
 * the kind `imp`, `item_health` becomes `health`, and level.c never learns what
 * either of them is -- ::Entity's own note says the kind is interpreted by
 * whichever module owns it, and that stays true. Adding a monster is a row in
 * enemy.c's table and a line in the FGD; this function does not change.
 *
 * The prefixes exist because TrenchBroom's entity browser sorts by classname
 * and an FGD groups by it. `monster_*` and `item_*` are Quake's, so a mapper
 * finds the monsters together and the items together, which is most of what
 * makes a long entity list usable.
 *
 * @note Classnames this file handles itself -- info_player_start, light,
 *       func_door -- are skipped here. They became ::Level::start, ::Light and
 *       ::DoorDef in the passes above, and turning them into markers as well
 *       would place a pickup where the player spawns.
 *
 * 한국어
 * ------
 * 지점 엔티티를 pickup.c와 enemy.c가 이미 차지한 표식으로 바꿉니다.
 *
 * 접두사를 떼어 낼 뿐 그 밖의 무엇도 번역하지 않습니다. `monster_imp`는 종류 `imp`가 되고
 * `item_health`는 `health`가 되며, level.c는 둘 중 무엇이 무엇인지 끝내 배우지 않습니다.
 * ::Entity 자신의 설명이 종류는 그것을 소유한 모듈이 해석한다고 말하며, 그것은 계속
 * 참입니다. 몬스터를 추가하는 일은 enemy.c 표의 행 하나와 FGD의 줄 하나이고, 이 함수는 바뀌지
 * 않습니다.
 *
 * 접두사가 있는 이유는 TrenchBroom의 엔티티 목록이 classname으로 정렬하고 FGD가 그것으로
 * 묶기 때문입니다. `monster_*`와 `item_*`는 Quake의 것이므로 제작자는 몬스터를 한데서, 아이템을
 * 한데서 찾습니다. 긴 엔티티 목록을 쓸 만하게 만드는 것의 대부분이 그것입니다.
 *
 * @note 이 파일이 스스로 처리하는 classname(info_player_start, light, func_door)은 이곳에서
 *       건너뜁니다. 위의 단계들에서 ::Level::start, ::Light, ::DoorDef가 되었으며, 그것을 표식
 *       으로도 만들면 플레이어가 스폰하는 자리에 아이템을 놓게 됩니다.
 */
static void brush_ents_of(Level *out, const BrushMap *bm) {
    /* PREFIXES FOR THE FAMILIES THAT GROW, an alias for the ones that do not.
       `monster_` and `item_` are open-ended: the whole point of stripping a
       prefix is that adding a monster never brings anybody back to this
       function. The exit and the jump pad are not families -- there is one of
       each idea and there always will be -- so they are two rows rather than a
       third prefix, which would also have to explain why `info_player_start`
       is not an entity marker when `info_exit` is.
       자라나는 계열에는 접두사를, 그렇지 않은 것에는 별칭을 씁니다. `monster_`와 `item_`은
       열려 있습니다. 접두사를 떼어 내는 것의 요점 자체가, 몬스터를 추가하는 일이 결코 이
       함수로 돌아오게 하지 않는다는 것입니다. 출구와 점프대는 계열이 아닙니다. 각 개념이
       하나씩 있고 앞으로도 그럴 것이므로, 세 번째 접두사가 아니라 두 개의 행입니다. 세 번째
       접두사였다면 `info_exit`은 엔티티 표식인데 왜 `info_player_start`는 아닌지도 설명해야
       했을 것입니다. */
    static const char *const PREFIX[] = { "monster_", "item_" };
    static const struct { const char *cn; const char *kind; } ALIAS[] = {
        { "info_exit", "exit" },
        { "info_push", "push" },
        /* Where a cleared wave pays, when loot.txt says `at altar`. An alias
           and not a third prefix, for the reason above: there is one idea of
           "the shrine this room pays at" and there always will be. Nothing
           spawns from it -- pickup.c's kind lookup does not know the name and
           will not -- so a map that places one and a loot.txt that never asks
           for it cost each other nothing.
           loot.txt가 `at altar`라고 말할 때 정리된 웨이브가 지급되는 자리입니다. 위의
           이유대로 세 번째 접두사가 아니라 별칭입니다. "이 방이 지급하는 제단"이라는
           개념은 하나이고 앞으로도 그럴 것입니다. 이것에서 생성되는 것은 없습니다.
           pickup.c의 종류 조회는 이 이름을 모르고 앞으로도 모를 것이므로, 이것을 배치한
           맵과 그것을 결코 요청하지 않는 loot.txt는 서로에게 아무 비용도 지우지 않습니다. */
        { "info_altar", "altar" },

        /* Where a ward MAY stand, which is a kind of statement no entity in
           this game had made before: every other marker says a thing IS here.
           A boss fight raises a random few of these each cycle and none of them
           at any other time, so the level places more than it will ever use.
           ALIASES AND NOT A `monster_` PREFIX, deliberately. The prefix
           resolves through ::mon_type_for into ::enemy_spawn_level, which
           creates the monster at load -- and a ward standing in a room with no
           boss makes every boss that later arrives invulnerable from its first
           frame. The name is the enforcement: there is no classname a map can
           write that puts a live ward in a level.
           Two rows rather than one with a key, because the two are what the
           author is choosing BETWEEN when they place one -- an air marker sends
           the things that fight at range, a ground marker the things that
           close. A single class with a spawnflag would put that choice one
           dialog deeper than the decision deserves.
           결계핵이 설 수 *있는* 자리이며, 이 게임의 어떤 엔티티도 지금까지 해 본 적 없는 종류의
           진술입니다. 다른 모든 표식은 무언가가 *여기 있다*고 말합니다. 보스전은 사이클마다 이
           중 무작위로 몇을 세우고 그 외의 어느 때에도 세우지 않으므로, 레벨은 앞으로 쓸 것보다
           많이 배치합니다.
           *`monster_` 접두사가 아니라 별칭이며, 의도적입니다.* 접두사는 ::mon_type_for를 거쳐
           ::enemy_spawn_level로 해석되고 그것이 로드 시점에 몬스터를 만듭니다. 그런데 보스가
           없는 방에 선 결계핵은, 나중에 도착하는 모든 보스를 첫 프레임부터 무적으로 만듭니다.
           이름이 곧 강제입니다. 맵이 적어서 살아 있는 결계핵을 레벨에 넣을 수 있는 classname은
           존재하지 않습니다.
           키 하나를 가진 한 행이 아니라 두 행인 이유는, 제작자가 하나를 놓을 때 고르고 있는
           것이 바로 그 둘 *사이*이기 때문입니다. 공중 표식은 거리를 두고 싸우는 것들을, 지상
           표식은 붙는 것들을 보냅니다. 스폰플래그를 가진 단일 클래스는 그 선택을, 그 결정이
           받아야 할 것보다 대화상자 하나만큼 더 깊은 곳에 두게 됩니다. */
        { "info_ward_air",    "wardair"    },
        { "info_ward_ground", "wardground" },
    };

    for (int i = 0; i < bm->n_ents; i++) {
        const BrushEnt *e = &bm->ents[i];
        const char *cn = brush_ent_value(e, "classname");
        if (!cn) continue;

        const char *kind = 0;
        for (int a = 0; a < (int)(sizeof(ALIAS) / sizeof(ALIAS[0])); a++) {
            int n = 0;
            while (ALIAS[a].cn[n] && cn[n] == ALIAS[a].cn[n]) n++;
            if (!ALIAS[a].cn[n] && !cn[n]) { kind = ALIAS[a].kind; break; }
        }
        for (int p = 0; !kind && p < (int)(sizeof(PREFIX) / sizeof(PREFIX[0])); p++) {
            int n = 0;
            while (PREFIX[p][n] && cn[n] == PREFIX[p][n]) n++;
            if (!PREFIX[p][n] && cn[n]) { kind = cn + n; break; }
        }
        if (!kind) continue;

        v3 o;
        if (!brush_ent_point(e, "origin", &o)) continue;

        if (out->n_ents >= LVL_MAX_ENTS) { DIAG(DIAG_ENT_CAP); continue; }
        Entity *en = &out->ents[out->n_ents++];

        copy_name(en->kind, LVL_MAT, kind, -1);
        en->x = (short)(o.x * 100.0f);
        en->z = (short)(o.z * 100.0f);
        en->y = (short)(o.y * 100.0f);

        /* The numbers whichever module owns this kind interprets. A monster
           reads none of them; a spawner reads all three. Named after the keys
           Quake uses for the same jobs so the FGD does not invent vocabulary.
           이 종류를 소유한 모듈이 해석하는 수치입니다. 몬스터는 하나도 읽지 않고 스포너는
           셋 다 읽습니다. FGD가 어휘를 새로 만들지 않도록, Quake가 같은 일에 쓰는 키 이름을
           따랐습니다. */
        en->p[0] = (short)clampf(brush_ent_num(e, "wait",     0.0f) * 10.0f, 0.0f, 32000.0f);
        en->p[1] = (short)clampf(brush_ent_num(e, "count",    0.0f),         0.0f, 32000.0f);
        en->p[2] = (short)clampf(brush_ent_num(e, "maxalive", 0.0f),         0.0f, 32000.0f);

        /* THE PAD'S FIRST NUMBER IS A SPEED, not a wait. ::Entity::p is
           deliberately generic -- level.h argues that naming its fields would
           hand this file an opinion about entities it exists not to know about
           -- but the KEYS above are named, and one entity wants a different
           key in the same slot. Written here rather than by adding a `speed`
           row to every entity, because a spawner has no speed and a pad has no
           wait, and a slot that means one thing for one kind is exactly what
           `p` was for.
           Map units per second to the centimetres per second ::level_push_at
           reads, so `speed 416` is the 13 m/s LVL_PUSH_DEFAULT describes.
           점프대의 첫 숫자는 대기 시간이 아니라 *속력*입니다. ::Entity::p는 의도적으로
           범용이며(level.h는 그 필드에 이름을 붙이면 알지 않기 위해 존재하는 이 파일에
           엔티티에 대한 의견을 쥐여 준다고 논합니다) 위의 *키*는 이름이 있는데, 한 엔티티가
           같은 자리에 다른 키를 원합니다. 모든 엔티티에 `speed` 행을 더하는 대신 이곳에
           적습니다. 스포너에는 속력이 없고 점프대에는 대기 시간이 없으며, 어떤 종류에게 한
           가지를 뜻하는 자리가 바로 `p`의 용도이기 때문입니다.
           초당 맵 단위를 ::level_push_at이 읽는 초당 센티미터로 바꾸므로, `speed 416`이
           LVL_PUSH_DEFAULT가 말하는 13 m/s입니다. */
        if (en->kind[0]=='p' && en->kind[1]=='u' && en->kind[2]=='s' &&
            en->kind[3]=='h' && en->kind[4]==0)
            en->p[0] = (short)clampf(brush_ent_num(e, "speed", 0.0f)
                                     * BRUSH_UNIT * 100.0f, 0.0f, 32000.0f);
    }
}

static int load_brush_level(BrushStore *bs, const char *name, Level *out) {
    int len = 0;
    const char *text = data_map(name, &len);
    if (!text) return 0;                     /* no .map of that name */

    /* No slot left, so this is not a brush level today. Returning 0 sends
       ::level_load down to the text loader, and a name with no text either
       fails the load outright -- which leaves the player where they are.
       남은 슬롯이 없으므로 이것은 오늘은 브러시 레벨이 아닙니다. 0을 반환하면 ::level_load가
       텍스트 로더로 내려가고, 텍스트도 없는 이름은 로드 자체가 실패하며 그것은 플레이어를
       있던 자리에 둡니다. */
    BrushMap *bm = brush_slot_for(bs, out);
    if (!bm) return 0;

    if (!brush_parse(text, len, bm)) return 0;

    out->brushes = bm;
    copy_name(out->name, sizeof(out->name), name, -1);

    /* WHERE THE EXIT LEADS, off worldspawn. On the level and not on the exit
       marker because ::Level::next is one string: a level has one place it goes
       on, and putting the name on each exit would let two of them disagree
       about where that is. Quake hangs it on a trigger_changelevel and can,
       because its levels carry a destination per trigger; this one does not,
       and inventing the storage to match would be inventing a feature.
       출구가 어디로 이어지는지이며 worldspawn에서 읽습니다. 출구 표식이 아니라 레벨에 두는
       이유는 ::Level::next가 문자열 하나이기 때문입니다. 레벨이 이어지는 곳은 하나이고, 그
       이름을 출구마다 두면 둘이 서로 다른 말을 할 수 있게 됩니다. Quake는 그것을
       trigger_changelevel에 겁니다. 그럴 수 있는 이유는 Quake의 레벨이 트리거마다 목적지를
       나르기 때문입니다. 이곳은 그렇지 않으며, 맞추려고 저장 공간을 만드는 것은 기능을 만들어
       내는 일입니다. */
    for (int i = 0; i < bm->n_ents; i++) {
        const char *cn = brush_ent_value(&bm->ents[i], "classname");
        if (!cn || !txt_eq(cn, "worldspawn")) continue;
        const char *nx = brush_ent_value(&bm->ents[i], "next");
        if (nx && nx[0]) copy_name(out->next, sizeof(out->next), nx, -1);
        break;
    }

    brush_start_of(out, bm);
    brush_lights_of(out, bm);
    brush_sun_of(out, bm);

    /* Triggers before doors, so a trigger's `target` is interned first and a
       door's `targetname` finds the number already assigned. Either order
       works -- the pool is order-of-first-appearance and both sides go through
       it -- but reading the thing that POINTS before the thing pointed at is
       the order somebody tracing the link would take.
       문보다 트리거를 먼저 봅니다. 트리거의 `target`이 먼저 사상되고 문의 `targetname`이
       이미 배정된 숫자를 찾도록 하기 위함입니다. 어느 순서든 동작합니다. 풀은 처음 등장한
       순서를 따르고 양쪽 모두 그것을 거칩니다. 다만 *가리키는* 것을 가리켜지는 것보다 먼저
       읽는 것이 그 연결을 따라가는 사람이 택할 순서입니다. */
    TagPool tags = {0};
    brush_triggers_of(out, bm, &tags);
    brush_hazards_of(out, bm);
    brush_doors_of(out, bm, &tags);
    brush_ents_of(out, bm);
    return 1;
}

/* The floor and ceiling at a point, as a pair of traces rather than a lookup.
   This is where the slope arrives: a 2D query has one answer per x,z and a
   downward sweep has the answer for wherever the surface actually is. */
static int brush_ground(const Level *l, float x, float z, float feet,
                        float step, float *out_floor, float *out_ceil) {
    const BrushMap *bm = l->brushes;

    /* HOW FAR ABOVE THE FEET THE SEARCH STARTS, which is not the same number as
       the step limit and this is where the two models genuinely differ.
       ::sector_at ignores height entirely -- a plan point has one floor -- so
       ::level_ground could take `step` as 1e9 to mean "no limit" and lose
       nothing. Starting a downward sweep 1e9 above the room finds the OUTSIDE
       of its roof, because with brushes there is something up there.
       So the sentinel is split back into the two things it was standing for:
       this bounds where to look, and the caller's `step` still decides what
       counts, applied to the answer exactly as the sector path applies it.
       Two metres because it must clear PLAYER_STEP -- a step you can climb has
       to be found -- while staying under the headroom of the tightest space
       anyone builds, so the probe does not begin in the storey above.
       발보다 얼마나 위에서 탐색을 시작하는지이며, 단차 상한과는 다른 숫자입니다. 두 모델이
       진짜로 갈라지는 지점입니다. ::sector_at은 높이를 완전히 무시합니다. 평면상의 한 점에
       바닥이 하나이므로 ::level_ground는 `step`을 1e9로 받아 "제한 없음"을 뜻해도 잃는 것이
       없었습니다. 방보다 1e9 위에서 하강 스윕을 시작하면 그 지붕의 *바깥면*을 찾게 됩니다.
       브러시에서는 그 위에 무언가가 있기 때문입니다. 그래서 그 특별값이 대신하던 두 가지를
       다시 나눕니다. 이 값은 어디를 볼지를 한정하고, 호출자의 `step`은 여전히 무엇이
       인정되는지를 결정하며 섹터 경로가 적용하는 것과 똑같이 답에 적용됩니다. 2미터인 이유는
       PLAYER_STEP을 넘어야 하고(오를 수 있는 단차는 찾아야 합니다) 동시에 누구든 만드는 가장
       비좁은 공간의 머리 위 여유보다는 낮아야 하기 때문입니다. 탐침이 위층에서 시작하지
       않도록 말입니다. */
    #define GROUND_LOOK_UP 2.0f

    float look = (step < GROUND_LOOK_UP) ? step : GROUND_LOOK_UP;
    if (look < 0.0f) look = 0.0f;

    BrushTrace down;
    brush_trace(bm, 0, bm->n_brushes, v3f(x, feet + look, z),
                v3f(x, -BRUSH_EDGE, z), POINT_BOX, POINT_BOX, &down);

    /* The probe began inside something -- a ceiling lower than the look-ahead.
       Retried from the feet, which are in open space by definition if the
       caller is standing anywhere at all.
       탐침이 무언가의 안에서 시작했습니다. 미리보기 높이보다 낮은 천장입니다. 발 위치에서
       다시 시도합니다. 호출자가 어디엔가 서 있기라도 하다면 발은 정의상 빈 공간에 있습니다. */
    if (down.start_solid)
        brush_trace(bm, 0, bm->n_brushes, v3f(x, feet, z),
                    v3f(x, -BRUSH_EDGE, z), POINT_BOX, POINT_BOX, &down);

    /* Starting inside a solid is the brush answer to "outside the map": there
       is no floor to stand on here. ::level_ground's callers already treat 0
       that way.
       고체 안에서 시작하는 것이 "맵 바깥"에 대한 브러시 쪽 답입니다. 이곳에는 딛고 설 바닥이
       없습니다. ::level_ground의 호출자들은 이미 0을 그렇게 취급합니다. */
    if (down.start_solid || !down.hit) return 0;

    /* The step limit, applied to the answer -- the same test, in the same
       place, that the sector path makes. */
    if (down.end.y > feet + step) return 0;

    *out_floor = down.end.y;

    BrushTrace up;
    brush_trace(bm, 0, bm->n_brushes,
                v3f(x, down.end.y + CEIL_PROBE, z), v3f(x, BRUSH_EDGE, z),
                POINT_BOX, POINT_BOX, &up);
    *out_ceil = up.hit ? up.end.y : BRUSH_EDGE;
    return 1;
}

/* Shared by the trace and the visibility test, so the two cannot disagree about
   whether a wall is solid -- level.h's note on ::level_blocked makes that a
   requirement, and one function is how it is kept. */
static int brush_ray(const Level *l, v3 origin, v3 dir, float max_dist,
                     BrushTrace *out) {
    const BrushMap *bm = l->brushes;
    v3 end = v3add(origin, v3scale(dir, max_dist));
    brush_trace(bm, 0, bm->n_brushes, origin, end, POINT_BOX, POINT_BOX, out);
    return out->start_solid || out->hit;
}

int level_load(const char *name, Level *out) { return level_load_in(0, name, out); }

int level_load_in(BrushStore *bs, const char *name, Level *out) {
    /* The baked light belongs to the level it was traced against, and a new
       level puts different walls between the same coordinates and a lamp.
       Dropped here rather than by the caller because this is the one place a
       level can become a different one.
       구워진 빛은 그것이 판정된 레벨에 속하며, 새 레벨은 같은 좌표와 등 사이에 다른 벽을
       놓습니다. 호출자가 아니라 이곳에서 비우는 이유는 레벨이 다른 레벨이 될 수 있는 곳이
       이곳뿐이기 때문입니다. */
    level_light_cache_reset();
    level_clear(out);

    /* A .map first, and the name is the filename. Preferred rather than merely
       supported: the whole direction of this work is that the editor's output
       IS the level, so a level that exists in both forms is one somebody is
       midway through moving and the .map is the one they are editing.
       Falling through to levels.txt is what keeps arena, vault and the imported
       Doom maps working while that move happens.
       .map을 먼저 보며 이름은 곧 파일명입니다. 단지 지원하는 것이 아니라 *우선*합니다. 이
       작업의 방향 전체가 에디터의 출력이 곧 레벨이라는 것이므로, 두 형태로 모두 존재하는
       레벨은 누군가 옮기는 중인 레벨이고 .map이 그 사람이 편집하고 있는 쪽입니다. levels.txt로
       내려가는 경로는 그 이동이 진행되는 동안 arena와 vault, 그리고 가져온 Doom 맵들이 계속
       동작하게 합니다. */
    out->brushes = 0;
    if (load_brush_level(store_or_default(bs), name, out)) return 1;

    int found = level_parse_text(name, out);

    /* --- what the text does not say, derived once, in the only order that
           works ------------------------------------------------------------
       Each of these reads what the one before it wrote: the boxes are cached
       for the sectors that survived the drop, the grid is built FROM those
       boxes, and all three must precede any query into the level.
       ::point_in_sector rejects against the cached boxes, so a level whose
       bounds were never computed collides as if every sector were empty.

       Named steps rather than eighty lines of comments and loops, because the
       ORDER is the rule here and a sequence of four calls states it where a
       run of loops only implies it.

       텍스트가 말하지 않는 것을 한 번만 유도하며, 유일하게 성립하는 순서로 수행합니다.
       각각은 바로 앞의 것이 기록한 결과를 읽습니다. 박스는 버려짐을 견딘 섹터에 대해
       캐시되고, 격자는 *그 박스들로부터* 생성되며, 셋 모두 레벨에 대한 어떤 질의보다도
       앞서야 합니다. ::point_in_sector가 캐시된 박스로 기각하므로, 경계값이 계산되지 않은
       레벨은 모든 섹터가 비어 있는 것처럼 충돌 판정됩니다.

       주석과 루프 여든 줄이 아니라 이름 붙은 단계인 이유는, 이곳의 규칙이 곧 *순서*이기
       때문입니다. 네 번의 호출은 그것을 명시하지만, 이어진 루프들은 암시할 뿐입니다. */
    level_drop_degenerate(out);
    level_cache_bounds(out);
    level_grid_build(out);
    level_apply_door_materials(out);

    return found;
}

/* -------------------------------------------------------------- 2D helpers */

/* Reject a point against the sector's bounding box before walking its edges.
 *
 * ENGLISH
 * -------
 * sector_at asks this of EVERY sector, and level_trace asks sector_at once per
 * 0.05m of ray, so this is the innermost loop in the whole simulation: a 40m
 * hook trace is ~800 steps, each walking every edge of every sector. Measured
 * with tools/levelbench.c, that is 13us per trace on the arena and 27us on a
 * level at LVL_MAX_SECTORS -- up to 18% of a frame at full monster load.
 *
 * The box test is four compares against integers the sector already stores.
 * Most sectors are nowhere near any given point, so most calls stop here
 * instead of doing the crossing test on every edge.
 *
 * Deliberately computed rather than cached: the bounds live in the Sector's
 * own points, so nothing has to be kept in sync when the editor moves a vertex
 * and nothing is added to the Level struct that would have to be parsed,
 * stored or embedded. The scan is the same one point_in_sector was already
 * about to do, and it stops early on a miss.
 *
 * Comparison is in FILE units (the raw shorts), not world units: it saves the
 * multiply per point, and the answer is identical because U is positive.
 *
 * 한국어
 * ------
 * 모서리를 순회하기 전에 섹터의 바운딩 박스로 점을 먼저 기각합니다.
 *
 * sector_at은 *모든* 섹터에 이 질문을 하고, level_trace는 광선 0.05m마다 sector_at을
 * 호출하므로, 이곳이 시뮬레이션 전체에서 가장 안쪽 루프입니다. 40m 훅 판정은 약 800
 * 스텝이며 각 스텝이 모든 섹터의 모든 모서리를 순회합니다. tools/levelbench.c로
 * 측정한 결과 아레나에서 판정당 13us, LVL_MAX_SECTORS 규모의 레벨에서 27us이며, 몬스터가
 * 가득 찬 상태에서 프레임의 최대 18%에 해당합니다.
 *
 * 박스 검사는 섹터가 이미 저장하고 있는 정수에 대한 비교 4번입니다. 대부분의 섹터는
 * 주어진 점에서 멀리 떨어져 있으므로, 대부분의 호출이 모든 모서리에 교차 판정을 수행하지
 * 않고 이곳에서 멈춥니다.
 *
 * 캐시하지 않고 매번 계산하는 것은 의도적입니다. 경계값은 섹터 자신의 점에 들어 있으므로
 * 에디터가 정점을 옮겨도 동기화할 것이 없고, 파싱·저장·내장해야 할 필드가 Level 구조체에
 * 추가되지도 않습니다. 이 순회는 point_in_sector가 어차피 수행하려던 것과 동일하며,
 * 벗어나는 경우 일찍 중단됩니다.
 *
 * 비교는 월드 단위가 아니라 *파일 단위*(원본 short)로 수행합니다. 점마다 곱셈을 아낄 수
 * 있고, U가 양수이므로 결과는 동일합니다.
 */
static int point_in_sector(const Sector *s, float x, float z) {
    if (s->n < 3) return 0;

    /* Bounding-box reject. Comparison is in FILE units (the raw shorts): it
       saves a multiply per bound and the answer is identical because U is
       positive.

       An INVALID box (min > max) means the bounds were never computed, and the
       test is skipped rather than trusted. That case is not hypothetical: a
       Level assembled field by field -- which every headless fixture in tools/
       does -- has zeroed bounds, and honouring a zero box would reject every
       point and make the whole level solid. Skipping costs one compare on a
       path that is already walking the edges, and it is what keeps this an
       optimisation rather than a second source of truth about where a sector
       is.

       기각용 바운딩 박스입니다. 비교는 *파일 단위*(원본 short)로 수행합니다. 경계마다
       곱셈을 아낄 수 있고, U가 양수이므로 결과는 동일합니다.

       *유효하지 않은* 박스(min > max)는 경계값이 계산된 적이 없다는 뜻이며, 그 경우 이
       검사를 신뢰하지 않고 건너뜁니다. 이는 가상의 상황이 아닙니다. 필드를 하나씩
       채워서 만든 Level은(tools/의 모든 헤드리스 픽스처가 그렇게 합니다) 경계값이 0이며,
       0인 박스를 그대로 따르면 모든 점이 기각되어 레벨 전체가 막힌 것이 됩니다. 건너뛰는
       비용은 어차피 모서리를 순회하려던 경로에서 비교 한 번이며, 이것이 이 코드를 섹터
       위치에 대한 두 번째 진실 공급원이 아니라 최적화로 유지하는 방법입니다. */
    if (s->has_bounds) {
        const float fx = x / U, fz = z / U;
        if (fx < s->min_x || fx > s->max_x || fz < s->min_z || fz > s->max_z)
            return 0;
    }

    int inside = 0;
    for (int i = 0, j = s->n - 1; i < s->n; j = i++) {
        float xi = s->pts[i*2] * U, zi = s->pts[i*2+1] * U;
        float xj = s->pts[j*2] * U, zj = s->pts[j*2+1] * U;
        if ((zi > z) == (zj > z)) continue;
        if (x < (xj - xi) * (z - zi) / (zj - zi) + xi) inside = !inside;
    }
    return inside;
}

/* Outward normal of edge i, in the xz plane. The winding of an authored
   polygon cannot be trusted, so the candidate is tested against the interior
   and flipped if it points the wrong way. */
static v3 edge_normal(const Sector *s, int i) {
    int j = (i + 1) % s->n;
    float ax = s->pts[i*2] * U, az = s->pts[i*2+1] * U;
    float bx = s->pts[j*2] * U, bz = s->pts[j*2+1] * U;
    float dx = bx - ax, dz = bz - az;

    v3 n = v3norm(v3f(dz, 0.0f, -dx));
    float mx = (ax + bx) * 0.5f, mz = (az + bz) * 0.5f;
    if (point_in_sector(s, mx + n.x * 0.01f, mz + n.z * 0.01f))
        n = v3scale(n, -1.0f);
    return n;
}

/* The sector governing a point: the last one declared that contains it.
   One place decides this, so geometry, collision and tracing cannot disagree
   about where a floor is.
 *
 * ENGLISH
 * -------
 * Two paths that must agree exactly. The grid path visits only the sectors
 * whose bounding boxes touch the point's cell; the scan path visits all of
 * them. Both keep the LAST match in index order, because declaration order is
 * what decides a platform from a pit -- the grid stores its indices ascending
 * (level_grid_build appends sector 0 first), so walking a cell in storage
 * order is walking it in declaration order, and "last wins" survives.
 *
 * The scan is not dead code kept for reference. It runs whenever the grid is
 * unbuilt -- which is what a hand-assembled Level gives, and every headless
 * fixture in tools/ builds one -- and whenever a cell overflowed. Both cases
 * are correct and merely slower, which is the property that lets the grid be
 * an optimisation rather than a second source of truth.
 *
 * 한국어
 * ------
 * 정확히 일치해야 하는 두 경로입니다. 격자 경로는 바운딩 박스가 해당 점의 셀에 닿는
 * 섹터만 방문하고, 순회 경로는 전부 방문합니다. 양쪽 모두 인덱스 순서상 *마지막*
 * 일치를 유지하는데, 단상과 구덩이를 가르는 것이 선언 순서이기 때문입니다. 격자는
 * 인덱스를 오름차순으로 저장하므로(level_grid_build가 0번 섹터를 먼저 추가합니다) 셀을
 * 저장 순서로 순회하는 것이 곧 선언 순서로 순회하는 것이며, "마지막이 우선"이
 * 유지됩니다.
 *
 * 순회 경로는 참고용으로 남겨 둔 죽은 코드가 아닙니다. 격자가 생성되지 않은 모든
 * 경우(손으로 조립한 Level이 그러하며, tools/의 모든 헤드리스 픽스처가 그런 것을
 * 만듭니다)와 셀이 초과한 모든 경우에 실행됩니다. 두 경우 모두 올바르되 다만 느릴
 * 뿐이며, 이 성질이 격자를 두 번째 진실 공급원이 아닌 최적화로 만듭니다.
 */
static const Sector *sector_at(const Level *l, float x, float z) {
    const SectorGrid *g = &l->grid;

    if (g->built) {
        /* File units, matching how the grid was built and how point_in_sector
           compares -- no multiply per lookup.
           격자가 생성된 방식 및 point_in_sector가 비교하는 방식과 동일하게 파일
           단위입니다. 조회마다 곱셈이 필요 없습니다. */
        int fx = (int)(x / U), fz = (int)(z / U);
        int cx = (fx - g->min_x) / g->cell_w;
        int cz = (fz - g->min_z) / g->cell_h;

        /* Outside the grid's extent is outside every sector's bounding box, so
           no sector can contain the point and the answer is "none" without
           looking at any of them.
           격자 범위 바깥은 모든 섹터의 바운딩 박스 바깥이므로, 어떤 섹터도 그 점을
           포함할 수 없으며 섹터를 하나도 보지 않고 "없음"이 답이 됩니다. */
        if (fx < g->min_x || fz < g->min_z ||
            cx < 0 || cx >= LVL_GRID_DIM || cz < 0 || cz >= LVL_GRID_DIM)
            return 0;

        int c = cz * LVL_GRID_DIM + cx;
        if (!g->overflow[c]) {
            const Sector *found = 0;
            for (int k = 0; k < g->count[c]; k++) {
                const Sector *s = &l->sectors[g->sect[c][k]];
                if (point_in_sector(s, x, z)) found = s;
            }
            return found;
        }
        /* Overflowed cell: fall through to the scan. */
    }

    const Sector *found = 0;
    for (int i = 0; i < l->n_sectors; i++)
        if (point_in_sector(&l->sectors[i], x, z)) found = &l->sectors[i];
    return found;
}

/* ---------------------------------------------------------------- geometry */

/* ------------------------------------------------------------ cap clipping
 *
 * A sector's floor and ceiling must not be drawn where a later-declared sector
 * covers them. sector_at() already says the later one wins, so if the render
 * disagrees you get a pit you cannot see -- the room's floor is drawn flat
 * across the hole -- and, where two sectors share a ceiling height, two
 * coplanar surfaces that z-fight.
 *
 * Doom sidesteps this by making sectors tile the plane. Overlap is exactly
 * what makes this format easy to author, so the cost is paid here instead:
 * triangulate the cap, then subtract every later sector from it.
 *
 * Subtracting triangles rather than whole polygons keeps every clip convex,
 * which is what makes the half-plane split below correct without a general
 * polygon boolean. Concave sectors come out right because they are cut into
 * triangles first, by the same ear clipper that made the cap.
 */

typedef struct { float x, z; } P2;

#define CAP_MAX_V      12    /* a convex piece gains a vertex per cut */
#define CAP_MAX_PIECES 32

typedef struct { P2 v[CAP_MAX_V]; int n; } Piece;

/* Splitting exactly through a vertex yields that point twice -- once as the
   vertex, once as the crossing -- and a repeated point makes a zero-area
   triangle later. Dropping it here is cheaper than filtering slivers out of
   the mesh afterwards. */
static void piece_add(Piece *p, P2 q) {
    if (p->n >= CAP_MAX_V) return;
    if (p->n > 0) {
        P2 last = p->v[p->n - 1];
        if (fabsf(last.x - q.x) < 1e-6f && fabsf(last.z - q.z) < 1e-6f) return;
    }
    p->v[p->n++] = q;
}

/* Splits a convex piece by the directed line a->b: the part to the left goes
   to `in`, the part to the right to `out`. Vertex order is preserved, so a
   piece keeps the winding of the triangle it came from. */
static void split_piece(const Piece *p, P2 a, P2 b, Piece *in, Piece *out) {
    const float EPS = 1e-6f;
    float ex = b.x - a.x, ez = b.z - a.z;
    in->n = out->n = 0;

    for (int i = 0; i < p->n; i++) {
        P2 c = p->v[i], d = p->v[(i + 1) % p->n];
        float sc = ex * (c.z - a.z) - ez * (c.x - a.x);
        float sd = ex * (d.z - a.z) - ez * (d.x - a.x);

        /* A vertex exactly on the line belongs to BOTH halves. Giving it to
           one only left the other with two vertices, and the piece was thrown
           away as degenerate -- which deleted the whole floor of any sector
           that shared an edge with a later one. */
        if (sc >= -EPS) piece_add(in,  c);
        if (sc <=  EPS) piece_add(out, c);

        if ((sc > EPS && sd < -EPS) || (sc < -EPS && sd > EPS)) {
            float t = sc / (sc - sd);
            P2 m = { c.x + (d.x - c.x) * t, c.z + (d.z - c.z) * t };
            piece_add(in, m);
            piece_add(out, m);
        }
    }

    /* Fewer than three vertices is a line, not an area. Zeroing it here means
       the caller's `n >= 3` tests are the only place that has to know. */
    if (in->n  < 3) in->n  = 0;
    if (out->n < 3) out->n = 0;
}

/* Replaces the piece list with the parts of it outside `t`. Each of the
   triangle's edges peels off what lies outside it; whatever survives all
   three is inside the triangle, and is dropped. */
static int subtract_tri(Piece *pieces, int n, const P2 *t,
                        float tx0, float tx1, float tz0, float tz1) {
    Piece out[CAP_MAX_PIECES];
    int n_out = 0;

    for (int i = 0; i < n; i++) {
        Piece cur = pieces[i];

        /* Bounding boxes per PIECE, not per piece list. Testing the list as a
           whole meant every piece was split by every clip triangle even when
           it was nowhere near one, so the count tripled each time and pieces
           had to be left uncut -- which put the floor back over the hole. */
        float px0 = cur.v[0].x, px1 = px0, pz0 = cur.v[0].z, pz1 = pz0;
        for (int k = 1; k < cur.n; k++) {
            if (cur.v[k].x < px0) px0 = cur.v[k].x;
            if (cur.v[k].x > px1) px1 = cur.v[k].x;
            if (cur.v[k].z < pz0) pz0 = cur.v[k].z;
            if (cur.v[k].z > pz1) pz1 = cur.v[k].z;
        }
        int apart = tx1 <= px0 || tx0 >= px1 || tz1 <= pz0 || tz0 >= pz1;

        /* No room to split this one into three: leave it whole. A floor drawn
           over a hole merely looks wrong -- it is the old bug -- whereas a
           dropped piece is a hole in the floor, which is worse. */
        if (apart || n_out + 3 > CAP_MAX_PIECES) {
            if (n_out < CAP_MAX_PIECES) out[n_out++] = cur;
            continue;
        }

        for (int e = 0; e < 3 && cur.n >= 3; e++) {
            Piece in, side;
            split_piece(&cur, t[e], t[(e + 1) % 3], &in, &side);
            if (side.n >= 3) out[n_out++] = side;
            cur = in;
        }
        /* Whatever survived all three half-planes is inside the triangle. */
    }

    for (int i = 0; i < n_out; i++) pieces[i] = out[i];
    return n_out;
}

/* Does this sector's bounding box overlap the box [x0,x1] x [z0,z1]?
 *
 * File units, and inclusive on both ends: two sectors that share an edge have
 * boxes that touch exactly, and rejecting a neighbour for touching rather than
 * overlapping would drop the cut where every real cut is.
 *
 * A sector with no bounds answers YES rather than NO. It has no outline to
 * intersect, so it costs one wasted loop; answering NO would make the reject
 * decide something it does not know.
 *
 * 파일 단위이며 양 끝을 포함합니다. 모서리를 공유하는 두 섹터의 박스는 정확히 맞닿으며,
 * 겹침이 아니라 맞닿음을 이유로 이웃을 기각하면 실제 절단점이 있는 바로 그 자리의 절단점을
 * 버리게 됩니다.
 *
 * 경계값이 없는 섹터는 아니오가 아니라 *예*로 답합니다. 교차시킬 외곽선이 없으므로 헛도는
 * 루프 한 번의 비용이 들 뿐이며, 아니오로 답하면 기각이 자신이 알지 못하는 것을 결정하게
 * 됩니다. */
static int sector_box_meets(const Sector *o, int x0, int z0, int x1, int z1) {
    if (!o->has_bounds) return 1;
    if (o->max_x < x0 || o->min_x > x1) return 0;
    if (o->max_z < z0 || o->min_z > z1) return 0;
    return 1;
}

/* Triangulates a sector's outline at height y. Ear clipping lives in render.c
   for the extrusion caps, so mb_polygon is borrowed rather than copied. */
static int cap_triangles(MeshBuf *tmp, const Sector *s, float y, int up) {
    short pts[LVL_MAX_PTS * 2];
    for (int i = 0; i < s->n * 2; i++) pts[i] = s->pts[i];
    int first = tmp->count;
    mb_polygon(tmp, pts, s->n, y, up, LEVEL_UV);
    return (tmp->count - first) / 3;
}

static void add_cap(MeshBuf *b, MeshBuf *tmp, const Level *l, int si,
                    float y, int up) {
    const Sector *s = &l->sectors[si];

    mb_reset(tmp);
    int mine = cap_triangles(tmp, s, y, up);
    int clip_first = tmp->count;

    /* Only the later sectors that could actually overlap this one. A sector
       whose box does not meet this sector's box cannot remove any of its
       floor, so triangulating it here produces clip triangles that
       subtract_tri will reject one at a time, for every triangle of this cap.
       Rejecting the whole sector once instead is the same answer and is what
       keeps a large map's build from being quadratic in sectors.

       Exact, not conservative: a piece of floor can only be cut away by an
       outline that covers it, and an outline that covers it has a box that
       meets this one.

       실제로 겹칠 수 있는 뒤쪽 섹터만 처리합니다. 박스가 이 섹터의 박스와 만나지 않는
       섹터는 이 바닥에서 아무것도 제거할 수 없으므로, 이곳에서 삼각형화하면 subtract_tri가
       이 바닥의 삼각형마다 하나씩 기각하게 될 클리핑 삼각형만 만들어 냅니다. 섹터 전체를
       한 번에 기각하는 것은 같은 답이면서, 큰 맵의 생성이 섹터 수에 2차가 되지 않게
       합니다.

       보수적인 것이 아니라 정확합니다. 바닥 조각은 그것을 덮는 외곽선에 의해서만 잘려
       나가며, 그것을 덮는 외곽선의 박스는 이 박스와 만납니다. */
    for (int j = si + 1; j < l->n_sectors; j++) {
        if (s->has_bounds
            && !sector_box_meets(&l->sectors[j],
                                 s->min_x, s->min_z, s->max_x, s->max_z))
            continue;
        cap_triangles(tmp, &l->sectors[j], y, 1);
    }
    int clips = (tmp->count - clip_first) / 3;

    v3 nrm = v3f(0.0f, up ? 1.0f : -1.0f, 0.0f);

    for (int k = 0; k < mine; k++) {
        Piece pieces[CAP_MAX_PIECES];
        int n = 1;
        pieces[0].n = 3;
        for (int i = 0; i < 3; i++) {
            const Vtx *v = &tmp->v[k * 3 + i];
            pieces[0].v[i].x = v->px;
            pieces[0].v[i].z = v->pz;
        }

        for (int c = 0; c < clips && n > 0; c++) {
            const Vtx *cv = &tmp->v[clip_first + c * 3];
            P2 t[3];
            for (int i = 0; i < 3; i++) { t[i].x = cv[i].px; t[i].z = cv[i].pz; }

            /* The clip triangle's bounds, measured once and reused for the
               per-piece rejection inside subtract_tri. */
            float tx0 = t[0].x, tx1 = t[0].x, tz0 = t[0].z, tz1 = t[0].z;
            for (int i = 1; i < 3; i++) {
                if (t[i].x < tx0) tx0 = t[i].x;
                if (t[i].x > tx1) tx1 = t[i].x;
                if (t[i].z < tz0) tz0 = t[i].z;
                if (t[i].z > tz1) tz1 = t[i].z;
            }

            /* "Inside" must mean left of every edge, so wind it that way. */
            float area = (t[1].x - t[0].x) * (t[2].z - t[0].z)
                       - (t[2].x - t[0].x) * (t[1].z - t[0].z);
            if (area < 0.0f) { P2 sw = t[1]; t[1] = t[2]; t[2] = sw; }

            n = subtract_tri(pieces, n, t, tx0, tx1, tz0, tz1);
        }

        for (int p = 0; p < n; p++) {
            const Piece *pc = &pieces[p];
            for (int i = 1; i + 1 < pc->n; i++) {
                P2 q0 = pc->v[0], q1 = pc->v[i], q2 = pc->v[i + 1];
                /* The wrap-around vertex can still coincide with the first,
                   and a cut along an edge leaves slivers. Neither draws
                   anything; both cost vertices and confuse any check that
                   asks what a triangle covers. */
                float a2 = (q1.x - q0.x) * (q2.z - q0.z)
                         - (q2.x - q0.x) * (q1.z - q0.z);
                if (a2 > -1e-7f && a2 < 1e-7f) continue;
                mb_vtx(b, v3f(q0.x, y, q0.z), nrm, q0.x*LEVEL_UV, q0.z*LEVEL_UV);
                mb_vtx(b, v3f(q1.x, y, q1.z), nrm, q1.x*LEVEL_UV, q1.z*LEVEL_UV);
                mb_vtx(b, v3f(q2.x, y, q2.z), nrm, q2.x*LEVEL_UV, q2.z*LEVEL_UV);
            }
        }
    }
}

/* `face_out` picks which side of the edge the surface is seen from: outward
   for the side of a platform, inward for the wall of a room or the side of a
   pit. Getting it wrong does not merely mislight the wall -- it culls it from
   exactly the side you are standing on, so the face looks missing. */
static void add_wall(MeshBuf *b, const Sector *s, int i, const EdgeSpan *sp) {
    float y0 = sp->y0, y1 = sp->y1;
    if (y1 - y0 < 0.0005f) return;

    int j = (i + 1) % s->n;
    float ox = s->pts[i*2] * U, oz = s->pts[i*2+1] * U;
    float dx = s->pts[j*2] * U - ox, dz = s->pts[j*2+1] * U - oz;

    /* Only the piece of the edge this span covers. A wall next to a platform
       that reaches partway along it is two quads, not one. */
    float ax = ox + dx * sp->t0, az = oz + dz * sp->t0;
    float bx = ox + dx * sp->t1, bz = oz + dz * sp->t1;

    v3 n = edge_normal(s, i);
    if (!sp->face_out) n = v3scale(n, -1.0f);

    /* u runs from the edge's own start, not the piece's, so the texture is
       continuous across a cut instead of restarting at every seam. */
    float full = sqrtf(dx*dx + dz*dz);
    float u0 = full * sp->t0 * LEVEL_UV, u1 = full * sp->t1 * LEVEL_UV;
    /* v grows downward so the texture is not mirrored between the two sides
       of a step. */
    float v0 = -y1 * LEVEL_UV, v1 = -y0 * LEVEL_UV;

    /* --- and it travels with a surface that has moved ---------------------
       Anchoring v to world height is right for a wall that stays put and wrong
       for a door: the quad's bottom edge rises with the ceiling while the
       texture stays pinned in space, so the leaf reads as being erased from
       below instead of rising. The point of the leaf now at world `y` was at
       `y - uv_y` before it moved, and that is the height its texture belongs to.

       ONLY THE WALL THE MOVED SURFACE BOUNDS. A rising ceiling lifts the leaf
       above it and leaves a jamb standing on the unmoved floor beside it;
       offsetting that one would slide its texture off the floor it is still
       resting on. Positive means the ceiling rose, so the wall is the one whose
       BOTTOM is that ceiling; negative means the floor sank, so it is the one
       whose TOP is that floor. Both comparisons are against the same
       `short * U` the span was built from, so they are exact rather than
       nearly equal.

       움직인 면과 함께 이동합니다. v를 월드 높이에 고정하는 것은 제자리에 있는 벽에는 맞고
       문에는 틀립니다. 사각형의 아래 모서리는 천장과 함께 올라가는데 텍스처는 공간에 박혀
       있으므로, 문짝이 올라가는 것이 아니라 아래에서 지워지는 것으로 읽힙니다. 지금 월드 `y`에
       있는 문짝의 그 지점은 움직이기 전에 `y - uv_y`에 있었고, 그것이 그 지점의 텍스처가 속한
       높이입니다.

       움직인 면이 경계를 이루는 벽에만 적용합니다. 올라가는 천장은 그 위의 문짝을 들어 올리고,
       그 옆에는 움직이지 않은 바닥 위에 선 문설주를 남깁니다. 그것까지 오프셋하면 텍스처가 아직
       딛고 있는 바닥에서 미끄러집니다. 양수는 천장이 올라갔다는 뜻이므로 그 벽은 *아래*가 그
       천장인 벽이고, 음수는 바닥이 내려갔다는 뜻이므로 *위*가 그 바닥인 벽입니다. 두 비교 모두
       그 구간을 만든 것과 같은 `short * U`에 대한 것이므로 근사가 아니라 정확합니다. */
    if ((s->uv_y > 0 && y0 == s->ceil  * U) ||
        (s->uv_y < 0 && y1 == s->floor * U)) {
        float d = s->uv_y * U * LEVEL_UV;
        v0 += d;
        v1 += d;
    }

    v3 p00 = v3f(ax, y0, az), p10 = v3f(bx, y0, bz);
    v3 p11 = v3f(bx, y1, bz), p01 = v3f(ax, y1, az);

    /* Emit whichever winding actually agrees with n, rather than deriving it
       on paper. Reasoning about handedness in the xz plane is where this went
       wrong the first time; measuring it cannot. */
    v3 geo = v3norm(v3cross(v3sub(p10, p00), v3sub(p11, p00)));
    if (v3dot(geo, n) < 0.0f) {
        mb_vtx(b, p00, n, u0, v1); mb_vtx(b, p01, n, u0, v0); mb_vtx(b, p11, n, u1, v0);
        mb_vtx(b, p00, n, u0, v1); mb_vtx(b, p11, n, u1, v0); mb_vtx(b, p10, n, u1, v1);
    } else {
        mb_vtx(b, p00, n, u0, v1); mb_vtx(b, p10, n, u1, v1); mb_vtx(b, p11, n, u1, v0);
        mb_vtx(b, p00, n, u0, v1); mb_vtx(b, p11, n, u1, v0); mb_vtx(b, p01, n, u0, v0);
    }
}

/* Appends a range, merging with the previous one when the material matches. */
static void push_range(MdlRange *r, int *n, int max, const char *mat,
                       int first, int count) {
    if (count <= 0) return;
    if (*n > 0) {
        const char *a = r[*n - 1].mat, *b = mat;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) { r[*n - 1].count += count; return; }
    }
    /* Out of room: fold the run into the previous range rather than dropping
       it. The material comes out wrong, which is visible; dropping the range
       leaves the triangles undrawn, which reads as a hole in the level and
       took a headless check to notice. */
    if (*n >= max) { DIAG(DIAG_MAT_RANGES); if (*n > 0) r[*n - 1].count += count; return; }
    MdlRange *e = &r[(*n)++];
    int i = 0;
    for (; mat[i] && i < (int)sizeof(e->mat) - 1; i++) e->mat[i] = mat[i];
    e->mat[i] = 0;
    e->first = first;
    e->count = count;
}

int level_sector_at(const Level *l, float x, float z) {
    const Sector *s = sector_at(l, x, z);
    return s ? (int)(s - l->sectors) : -1;
}

/* How far inside a surface to step before asking what is there. Small enough
   that a wall a few centimetres thick is not stepped through, large enough to
   clear the float slop on a trace that reports a hit exactly on a plane.
   무엇이 있는지 묻기 전에 표면 안쪽으로 얼마나 들어갈지입니다. 몇 센티미터 두께의 벽을 통과해
   버리지 않을 만큼 작고, 평면 위에서 정확히 충돌을 보고한 판정의 부동소수점 오차를 벗어날 만큼
   큽니다. */
#define DOOR_PROBE 0.02f

int level_door_at(const Level *l, v3 p, v3 n) {
    if (!l || l->door_run.count <= 0) return -1;

    /* Into the solid, away from the ray that found it. */
    v3 in = v3sub(p, v3scale(n, DOOR_PROBE));

    if (l->brushes) {
        for (int i = 0; i < l->door_run.count; i++) {
            const DoorDef *d = &l->doors[i];
            if (d->n_brushes <= 0) continue;
            if (brush_point_in(l->brushes, d->first_brush, d->n_brushes, in))
                return i;
        }
        return -1;
    }

    /* A sector door's leaf stands on that sector's own footprint, so the point
       just inside it lands there -- and last-wins means the door, declared
       after the room it sits in, is what sector_at returns.
       섹터 문의 문짝은 그 섹터 자신의 발자국 위에 서 있으므로, 그 안쪽의 점은 그곳에
       떨어집니다. 그리고 마지막 선언 우선 규칙에 따라, 자신이 놓인 방보다 뒤에 선언된 문이
       sector_at이 반환하는 것입니다. */
    int si = level_sector_at(l, in.x, in.z);
    if (si < 0) return -1;

    for (int i = 0; i < l->door_run.count; i++)
        if (l->doors[i].sector == si) return i;

    return -1;
}

int level_exit_at(const Level *l, float x, float z) {
    for (int i = 0; i < l->n_ents; i++) {
        const char *k = l->ents[i].kind;
        if (!(k[0]=='e'&&k[1]=='x'&&k[2]=='i'&&k[3]=='t'&&k[4]==0)) continue;
        float dx = x - l->ents[i].x * U, dz = z - l->ents[i].z * U;
        if (dx*dx + dz*dz <= LVL_EXIT_RADIUS * LVL_EXIT_RADIUS) return 1;
    }
    return 0;
}

float level_push_at(const Level *l, float x, float z) {
    for (int i = 0; i < l->n_ents; i++) {
        const char *k = l->ents[i].kind;
        if (!(k[0]=='p'&&k[1]=='u'&&k[2]=='s'&&k[3]=='h'&&k[4]==0)) continue;

        float dx = x - l->ents[i].x * U, dz = z - l->ents[i].z * U;
        if (dx*dx + dz*dz > LVL_PUSH_RADIUS * LVL_PUSH_RADIUS) continue;

        /* An unwritten speed is the default, not nothing. A pad the author
           dropped in without a number should still throw them somewhere --
           the alternative is a pad that reads as broken, and the author's next
           move is to doubt the feature rather than to type a number.
           기록되지 않은 속력은 0이 아니라 기본값입니다. 작성자가 수치 없이 놓은
           점프대도 어딘가로 던져야 합니다. 그러지 않으면 고장 난 것처럼 읽히고, 작성자의
           다음 행동은 숫자를 입력하는 것이 아니라 기능을 의심하는 것이 됩니다. */
        int sp = l->ents[i].p[0];
        if (sp <= 0) sp = LVL_PUSH_DEFAULT;
        return sp * U;
    }
    return 0.0f;
}

v3 level_edge_normal(const Level *l, int sector, int edge) {
    return edge_normal(&l->sectors[sector], edge);
}


/* Where every other sector's outline crosses this edge, as parameters along
 * it. These are the only places the answer to "what is beyond?" can change,
 * so they are exactly where the edge has to be cut.
 *
 * REJECTED BY BOUNDING BOX FIRST, and that is what makes this affordable at
 * the format's limit. Without it the loop below is every edge against every
 * other sector's every edge -- quadratic in sectors and quadratic again in
 * points -- which levelbench measured at 3.8ms for the converted Freedoom map,
 * 23% of a 60fps frame, on a rebuild that also happens whenever a door moves.
 *
 * The test is exact rather than conservative: an intersection point lies on
 * both segments, so it lies inside both boxes, so two boxes that do not meet
 * cannot produce one. Nothing is traded for the speed here.
 *
 * 먼저 바운딩 박스로 기각하며, 그것이 형식의 상한에서 이 함수를 감당할 만하게 만드는
 * 것입니다. 그것이 없으면 아래 루프는 모든 모서리를 다른 모든 섹터의 모든 모서리와
 * 비교합니다. 섹터 수에 2차이고 점 수에 다시 2차이며, levelbench가 변환된 Freedoom 맵에서
 * 3.8ms, 60fps 프레임의 23%로 측정한 값입니다. 게다가 그 재생성은 문이 움직일 때마다
 * 일어납니다.
 *
 * 이 판정은 보수적인 것이 아니라 *정확합니다*. 교차점은 두 선분 위에 있으므로 두 박스
 * 안에도 있으며, 따라서 만나지 않는 두 박스는 교차점을 만들 수 없습니다. 이곳의 속도는
 * 무엇과도 교환하지 않았습니다. */
static int edge_cuts(const Level *l, int si, int e, float *t, int max) {
    const Sector *s = &l->sectors[si];
    int j = (e + 1) % s->n;
    float ax = s->pts[e*2] * U, az = s->pts[e*2+1] * U;
    float dx = s->pts[j*2] * U - ax, dz = s->pts[j*2+1] * U - az;

    /* The edge's own box, in the file units the sector bounds are kept in. */
    int ex0 = s->pts[e*2],   ex1 = s->pts[j*2];
    int ez0 = s->pts[e*2+1], ez1 = s->pts[j*2+1];
    if (ex0 > ex1) { int tmp = ex0; ex0 = ex1; ex1 = tmp; }
    if (ez0 > ez1) { int tmp = ez0; ez0 = ez1; ez1 = tmp; }

    int n = 0;
    for (int k = 0; k < l->n_sectors && n < max; k++) {
        if (k == si) continue;
        if (!sector_box_meets(&l->sectors[k], ex0, ez0, ex1, ez1)) continue;
        const Sector *o = &l->sectors[k];
        for (int i = 0; i < o->n && n < max; i++) {
            int m = (i + 1) % o->n;
            float cx = o->pts[i*2] * U, cz = o->pts[i*2+1] * U;
            float ex = o->pts[m*2] * U - cx, ez = o->pts[m*2+1] * U - cz;

            float den = dx * ez - dz * ex;
            if (den > -1e-7f && den < 1e-7f) continue;    /* parallel */

            float ta = ((cx - ax) * ez - (cz - az) * ex) / den;
            float tb = ((cx - ax) * dz - (cz - az) * dx) / den;
            /* Interior crossings only: an endpoint cut splits nothing, and
               floating-point noise there would make zero-length pieces. */
            if (ta <= 0.0005f || ta >= 0.9995f) continue;
            if (tb < 0.0f || tb > 1.0f) continue;
            t[n++] = ta;
        }
    }

    /* Insertion sort: n is a handful, and the pieces have to come out in
       order for the sub-ranges to tile the edge. */
    for (int i = 1; i < n; i++) {
        float v = t[i]; int k = i - 1;
        while (k >= 0 && t[k] > v) { t[k+1] = t[k]; k--; }
        t[k+1] = v;
    }
    return n;
}

int level_edge_spans(const Level *l, int si, int e, EdgeSpan *out, int max) {
    const Sector *s = &l->sectors[si];
    int j = (e + 1) % s->n;
    float ax = s->pts[e*2] * U, az = s->pts[e*2+1] * U;
    float dx = s->pts[j*2] * U - ax, dz = s->pts[j*2+1] * U - az;
    v3 nrm = edge_normal(s, e);

    float cut[LVL_MAX_SPANS];
    int   n_cut = edge_cuts(l, si, e, cut, LVL_MAX_SPANS - 1);
    int   n = 0;

    for (int c = 0; c <= n_cut && n < max; c++) {
        float t0 = c == 0     ? 0.0f : cut[c-1];
        float t1 = c == n_cut ? 1.0f : cut[c];
        if (t1 - t0 < 0.001f) continue;

        /* Ask at the middle of this piece, a hair outside the edge. */
        float tm = (t0 + t1) * 0.5f;
        const Sector *nb = sector_at(l, ax + dx * tm + nrm.x * 0.02f,
                                        az + dz * tm + nrm.z * 0.02f);
        int ni = (!nb || nb == s) ? -1 : (int)(nb - l->sectors);

        if (ni < 0) {
            /* Nothing beyond: solid floor to ceiling, seen from inside. */
            out[n].t0 = t0; out[n].t1 = t1;
            out[n].y0 = s->floor * U; out[n].y1 = s->ceil * U;
            out[n].face_out = 0; n++;
            continue;
        }

        /* Two sectors sharing a boundary each see the other, so only the
           later-declared one owns the step and it is not built twice. A pit
           works because it is declared after the room it is cut into -- the
           room has no edge there at all. */
        if (ni > si) continue;

        /* Only the height difference is solid; the rest is the opening
           between the two. Which side it faces depends on which way the step
           goes: the side of a platform is seen from outside, the side of a
           pit from inside. */
        if (s->floor != nb->floor && n < max) {
            int up = s->floor > nb->floor;
            out[n].t0 = t0; out[n].t1 = t1;
            out[n].y0 = (up ? nb->floor : s->floor) * U;
            out[n].y1 = (up ? s->floor : nb->floor) * U;
            out[n].face_out = up; n++;
        }
        if (s->ceil != nb->ceil && n < max) {
            int dn = s->ceil < nb->ceil;
            out[n].t0 = t0; out[n].t1 = t1;
            out[n].y0 = (dn ? s->ceil : nb->ceil) * U;
            out[n].y1 = (dn ? nb->ceil : s->ceil) * U;
            out[n].face_out = dn; n++;
        }
    }
    return n;
}

/* Bakes every level light into the vertices, once, at build time.
 *
 * ENGLISH
 * -------
 * This is Quake's static lighting in the shape this engine can hold. The
 * fragment shader carries eight point lights, which is enough for muzzle
 * flashes and explosions and nowhere near enough to light a room: a level with
 * a ninth lamp simply does not have it, and everything outside the eight radii
 * is not dark but UNLIT.
 *
 * Baked at the VERTEX rather than into a lightmap because the geometry here is
 * sectors -- few faces, each large -- and a lightmap would need a second
 * texture, a second set of UVs, and a packer to fit them. The cost of this is
 * that light varies smoothly across a wall instead of casting a shaped pool on
 * it, which is the trade Quake made in the other direction because its walls
 * were subdivided into 16-unit patches and ours are not.
 *
 * SHADOWED WITH THE SAME TRACE THE MONSTERS SEE WITH, so a lamp behind a wall
 * does not light the room in front of it. That trace is the expensive part and
 * it runs once per vertex per light at load, not per frame.
 *
 * 한국어
 * ------
 * 이 엔진이 담을 수 있는 모양으로 옮긴 Quake의 정적 조명입니다. 프래그먼트 셰이더는 점광원
 * 여덟 개를 담으며, 총구 섬광과 폭발에는 충분하고 방을 밝히기에는 턱없이 부족합니다. 아홉
 * 번째 등이 있는 레벨은 그것을 그냥 갖지 못하고, 여덟 반경 밖은 어두운 것이 아니라 조명이
 * *없습니다*.
 *
 * 라이트맵이 아니라 *정점*에 굽는 이유는 이곳의 지오메트리가 섹터, 즉 크고 적은 면이기
 * 때문입니다. 라이트맵은 두 번째 텍스처와 두 번째 UV, 그리고 그것을 채울 패커를 요구합니다.
 * 대가는 빛이 벽에 모양 있는 웅덩이를 드리우지 않고 매끄럽게 변한다는 것입니다. Quake는
 * 벽을 16단위 조각으로 나누었기에 반대 방향으로 거래했고, 우리 벽은 나뉘어 있지 않습니다.
 *
 * 몬스터가 보는 것과 *같은* 판정으로 그림자를 처리하므로, 벽 뒤의 등이 앞의 방을 밝히지
 * 않습니다. 그 판정이 비싼 부분이며 프레임마다가 아니라 로드 시 정점당 광원당 한 번
 * 돌아갑니다. */
/* ------------------------------------------------ the baked-light cache
 *
 * ENGLISH
 * -------
 * A door moves sectors, the drawn geometry has to follow, and following it
 * means running the bake above again -- over EVERY vertex, on EVERY frame the
 * door is in motion, however far from the door they are. levelbench measures
 * what that costs: on `arena` the bake is 0.92ms of a 0.97ms rebuild, 5.5% of
 * a 60fps frame, paid for the whole of a door's travel.
 *
 * Almost all of that work computes an answer it has already computed. When a
 * door moves, the level is re-triangulated from scratch and the triangle list
 * comes out different -- but the VERTICES mostly do not move. tools\leveltest.c
 * measures this too: with arena's doors at half travel, 79.9% of the vertices
 * sit exactly where a vertex already sat.
 *
 * So the cache is keyed on where a vertex is and which way it faces, and on
 * nothing else. That is the whole of what the bake reads about a vertex, which
 * is what makes the key sufficient rather than merely convenient.
 *
 * WHY NOT SPLIT THE MESH INSTEAD. The obvious alternative is to build the
 * door-owned surfaces separately and never rebuild the rest. It was tried and
 * measured before this was written, and it is much worse HERE: ::add_cap
 * subtracts every later-declared sector's outline from the floor it is
 * triangulating, so a sliding door re-carves the floor of every room it passes
 * over, and a rule that is safe has to call 76.7% of arena moving. The split
 * reasons about which surfaces MIGHT change; this asks which vertices DID.
 *
 * @note An empty cache reproduces the old behaviour exactly, vertex for
 *       vertex, which is why a level looks the same on the frame it loads as
 *       it always did.
 * @warning What the cache changes is that a vertex which does not move keeps
 *          the light it was given, so an opening door no longer spills baked
 *          light into the room beyond it. That is Quake's behaviour rather
 *          than a regression from it -- a lightmap does not change because a
 *          door moved -- and the per-frame relight this replaces was a side
 *          effect of rebuilding everything, never a decision.
 *
 * 한국어
 * ------
 * 문이 섹터를 움직이면 그려지는 지오메트리가 따라가야 하고, 따라간다는 것은 위의 베이크를
 * 다시 돌린다는 뜻입니다. 문이 움직이는 *매 프레임*, 문에서 아무리 멀리 있든 *모든* 정점에
 * 대해서 말입니다. levelbench가 그 비용을 잽니다. `arena`에서 0.97ms짜리 재생성 중
 * 0.92ms가 베이크이며, 60fps 프레임의 5.5%를 문이 움직이는 내내 치릅니다.
 *
 * 그 일의 거의 전부가 이미 계산한 답을 다시 계산합니다. 문이 움직이면 레벨은 처음부터 다시
 * 삼각형화되고 삼각형 목록은 달라져 나오지만, *정점*은 대부분 움직이지 않습니다.
 * tools\leveltest.c가 이것도 잽니다. arena의 문이 절반쯤 열린 상태에서 정점의 79.9%가
 * 이미 정점이 있던 자리에 그대로 놓입니다.
 *
 * 그래서 캐시의 키는 정점이 어디에 있고 어느 쪽을 향하는지, 그리고 그 외에는 아무것도
 * 아닙니다. 베이크가 한 정점에 대해 읽는 것이 그게 전부이며, 그 사실이 이 키를 편리한 것이
 * 아니라 *충분한* 것으로 만듭니다.
 *
 * 왜 메시를 분할하지 않았는가. 자명한 대안은 문이 소유한 표면을 따로 만들고 나머지는 다시
 * 만들지 않는 것입니다. 이 글을 쓰기 전에 시도하고 측정했으며, *이곳에서는* 훨씬
 * 나쁩니다. ::add_cap은 삼각형화 중인 바닥에서 뒤에 선언된 모든 섹터의 외곽선을 빼므로,
 * 미끄러지는 문은 지나가는 모든 방의 바닥을 다시 깎아 내고, 안전한 규칙은 arena의 76.7%를
 * 움직인다고 불러야 합니다. 분할은 어느 표면이 바뀔 *수도 있는지*를 추론하고, 이것은 어느
 * 정점이 실제로 바뀌었는지를 묻습니다.
 *
 * @note 빈 캐시는 이전 동작을 정점 하나까지 그대로 재현합니다. 레벨이 로드되는 프레임에
 *       늘 그랬던 것과 똑같아 보이는 이유입니다.
 * @warning 캐시가 바꾸는 것은, 움직이지 않은 정점이 받았던 빛을 그대로 유지한다는 점이며,
 *          따라서 열리는 문이 더 이상 구워진 빛을 너머의 방으로 흘려보내지 않습니다.
 *          이는 그로부터의 퇴보가 아니라 Quake의 동작입니다. 라이트맵은 문이 움직였다고
 *          바뀌지 않습니다. 그리고 이것이 대체하는 프레임별 재조명은 전부를 다시 만드는
 *          일의 부작용이었을 뿐, 결정이었던 적이 없습니다.
 */

/* Power of two, and comfortably above what a level reaches: the largest map in
   hand builds 3,222 vertices, so this stays under half full and the linear
   probe below stays short. Lives in .bss, which the floppy budget does not
   count -- see the size report's "on disk?" column.
   2의 거듭제곱이며 레벨이 도달하는 값보다 넉넉히 큽니다. 손에 든 가장 큰 맵이 정점 3,222개를
   만들므로 절반도 차지 않고, 아래의 선형 탐사도 짧게 유지됩니다. .bss에 있으며, 플로피
   예산은 .bss를 세지 않습니다. */
/* Overridable from the build, so a second binary can be compiled with a table
   too small for the level it loads. A cap that cannot be reached is a cap that
   has never been tested, and the overflow path below -- trace anyway, store
   nothing, count it -- is exactly the kind of code that is written once and
   then never executed again. build.ps1 builds leveltest_tinylcache for this,
   the same way it builds textest_tinycache.
   빌드에서 재정의할 수 있게 하여, 로드하는 레벨에 비해 너무 작은 테이블로 두 번째
   바이너리를 컴파일할 수 있게 합니다. 도달할 수 없는 상한은 시험된 적 없는 상한이며,
   아래의 초과 경로(그래도 판정하고, 저장하지 않고, 센다)는 한 번 작성된 뒤 다시는
   실행되지 않는 종류의 코드입니다. */
#ifndef LIGHT_CACHE_SLOTS
#define LIGHT_CACHE_SLOTS 8192
#endif

/* The probe below masks with SLOTS-1 instead of dividing, which is only the
   same thing for a power of two. Checked here rather than trusted, because an
   override that is not one would not fail -- it would silently visit a subset
   of the table and look like a cache with a poor hit rate.
   아래의 탐사는 나눗셈 대신 SLOTS-1로 마스크하며, 이는 2의 거듭제곱일 때만 같은
   연산입니다. 신뢰하지 않고 이곳에서 검사하는 이유는, 거듭제곱이 아닌 재정의가 실패하지
   않고 테이블의 일부만 조용히 방문하여 적중률 나쁜 캐시처럼 보이기 때문입니다. */
_Static_assert((LIGHT_CACHE_SLOTS & (LIGHT_CACHE_SLOTS - 1)) == 0,
               "LIGHT_CACHE_SLOTS must be a power of two");

typedef struct {
    float px, py, pz;   /**< Where the vertex was. / 정점이 있던 자리. */
    float nx, ny, nz;   /**< Which way it faced. / 향하던 방향. */
    float lr, lg, lb;   /**< What the bake produced there. / 그곳에서 베이크가 만든 값. */

    /**
     * @brief Which fill of the table this entry belongs to.
     *
     * A slot is OCCUPIED when this equals ::g_lcache_gen and free otherwise,
     * which is what lets ::level_light_cache_reset be one increment instead of
     * a walk over every slot. The table is 8192 entries and the reset runs on
     * every ::level_load -- including once per hop of world.c's level-chain
     * scan, which is up to ::WORLD_STAGE_MAX_HOPS of them for one stage-select.
     *
     * A zero normal used to mark a slot free. It could, because a normal is
     * always unit length, and it cost nothing extra -- but it made "free"
     * a property of the vertex data rather than of the table, so the only way
     * to empty the table was to write over all of it.
     *
     * 이 항목이 테이블의 몇 번째 채움에 속하는지입니다.
     *
     * 이 값이 ::g_lcache_gen과 같으면 슬롯이 *사용 중*이고 아니면 비어 있습니다. 그것이
     * ::level_light_cache_reset을 모든 슬롯 순회가 아니라 증가 한 번으로 만듭니다. 테이블은
     * 8192개이고 초기화는 모든 ::level_load에서 실행되며, 여기에는 world.c의 레벨 사슬 스캔이
     * 구간마다 하는 호출도 포함됩니다. 스테이지 선택 한 번에 최대
     * ::WORLD_STAGE_MAX_HOPS번입니다.
     *
     * 이전에는 0 법선이 빈 슬롯을 표시했습니다. 법선은 언제나 단위 길이이므로 그럴 수 있었고
     * 추가 비용도 없었지만, "비어 있음"을 테이블이 아니라 *정점 데이터*의 성질로 만들었습니다.
     * 그래서 테이블을 비우는 유일한 방법이 전체를 덮어쓰는 것이었습니다.
     */
    unsigned gen;
} LightSlot;

static LightSlot g_lcache[LIGHT_CACHE_SLOTS];
static int       g_lcache_used;

/* STARTS AT 1, so the zeroed table every process begins with is already empty:
   every slot holds generation 0 and nothing will ever match it again.
   1에서 시작하므로 모든 프로세스가 시작할 때 갖는 0으로 초기화된 테이블이 이미 비어
   있습니다. 모든 슬롯이 세대 0을 담고 있고 그것과 다시 일치하는 것은 없습니다. */
static unsigned g_lcache_gen = 1;

/* Switched off, ::bake_light is the function it was before the cache existed:
   every vertex traced, nothing looked up, nothing kept. That is not a debug
   convenience -- it is the only way to state the claim this whole thing rests
   on as something a test can run. "An empty cache reproduces the old
   behaviour, vertex for vertex" was a sentence in a comment until there was a
   way to build BOTH and compare them.
   꺼 두면 ::bake_light는 캐시가 있기 전의 그 함수입니다. 모든 정점을 판정하고, 아무것도
   찾아보지 않고, 아무것도 남기지 않습니다. 이는 디버그 편의가 아니라, 이 모든 것이 딛고 선
   주장을 테스트가 실행할 수 있는 형태로 진술하는 유일한 방법입니다. "빈 캐시는 이전 동작을
   정점 하나까지 재현한다"는 *양쪽을 다 만들어 비교할 방법*이 생기기 전까지는 주석 속
   문장이었습니다. */
static int g_lcache_on = 1;

/* A slot from an earlier fill is indistinguishable from one that was never
   written, and that is the point: both are free. See ::LightSlot::gen.
   이전 채움에 속한 슬롯은 한 번도 기록된 적 없는 슬롯과 구별되지 않으며, 그것이 요점입니다.
   둘 다 비어 있습니다. ::LightSlot::gen을 참조하십시오. */
static int slot_empty(const LightSlot *s) {
    return s->gen != g_lcache_gen;
}

static unsigned light_hash(const Vtx *v) {
    const float *f = &v->px;
    unsigned h = 2166136261u;
    for (int k = 0; k < 6; k++) {          /* px..nz -- position and normal */
        /* Through a union rather than a cast: the compiler may assume a float
           and an unsigned never alias, and reading one through a pointer to
           the other is where that assumption bites.
           캐스트가 아니라 공용체를 거칩니다. 컴파일러는 float와 unsigned가 서로
           앨리어싱하지 않는다고 가정해도 되며, 한쪽을 다른 쪽의 포인터로 읽는 것이 바로 그
           가정이 무는 지점입니다. */
        union { float f; unsigned u; } cv;
        cv.f = f[k];
        h = (h ^ cv.u) * 16777619u;
    }
    return h;
}

static int light_same(const LightSlot *s, const Vtx *v) {
    return s->px == v->px && s->py == v->py && s->pz == v->pz
        && s->nx == v->nx && s->ny == v->ny && s->nz == v->nz;
}

void level_light_cache_reset(void) {
    /* One increment empties the table: every entry now names a fill that is
       over. See ::LightSlot::gen.
       증가 한 번이 테이블을 비웁니다. 이제 모든 항목이 끝난 채움을 가리킵니다.
       ::LightSlot::gen을 참조하십시오. */
    g_lcache_gen++;

    /* WRAPPED, after four billion resets. Vanishingly unlikely and not
       impossible, and the failure it would produce is the one this whole file
       is careful about elsewhere: generation 0 would match every slot that was
       never written, so a fresh table would read as full of entries whose
       colours belong to nothing. Cheaper to spend one walk here than to leave
       a case that cannot be tested and cannot be explained when it happens.
       사십억 번의 초기화 뒤에 순환합니다. 극히 일어나기 어렵지만 불가능하지는 않으며, 그때
       발생할 고장은 이 파일이 다른 곳에서 조심하고 있는 바로 그것입니다. 세대 0은 한 번도
       기록된 적 없는 모든 슬롯과 일치하므로, 새 테이블이 아무것에도 속하지 않는 색을 지닌
       항목으로 가득 찬 것처럼 읽힙니다. 테스트할 수도 없고 일어났을 때 설명할 수도 없는
       경우를 남기는 것보다 이곳에서 한 번 순회하는 편이 쌉니다. */
    if (g_lcache_gen == 0) {
        for (int i = 0; i < LIGHT_CACHE_SLOTS; i++) g_lcache[i].gen = 0;
        g_lcache_gen = 1;
    }

    g_lcache_used = 0;
}

int level_light_cache_count(void) { return g_lcache_used; }
int level_light_cache_slots(void) { return LIGHT_CACHE_SLOTS; }
int level_light_cache_bytes(void) { return (int)sizeof(g_lcache); }

void level_light_cache_enable(int on) {
    g_lcache_on = on ? 1 : 0;
}

/* Finds the slot this vertex belongs in: the one holding it, or the first free
   one after its hash. Returns -1 only when the table is full and the vertex is
   not in it, which is the case DIAG_LIGHT_CACHE counts.
   이 정점이 속할 슬롯을 찾습니다. 그것을 담고 있는 슬롯이거나, 해시 이후 첫 번째 빈
   슬롯입니다. -1은 테이블이 가득 찼고 그 정점이 안에 없을 때만 반환하며, 그 경우를
   DIAG_LIGHT_CACHE가 셉니다. */
static int light_slot(const Vtx *v) {
    unsigned i = light_hash(v) & (LIGHT_CACHE_SLOTS - 1);
    for (int probe = 0; probe < LIGHT_CACHE_SLOTS; probe++) {
        LightSlot *s = &g_lcache[i];
        if (slot_empty(s))       return (int)i;
        if (light_same(s, v))    return (int)i;
        i = (i + 1) & (LIGHT_CACHE_SLOTS - 1);
    }
    return -1;
}


/* Is anything OPAQUE between here and the sky in this direction?
 *
 * ENGLISH
 * -------
 * WHY THE ORDINARY TRACE IS THE WRONG ONE FOR A SUN. `lqdm1` is an outdoor map
 * and its sky is brushwork: this engine has no sky pass, so a `sky5_blu` face
 * is drawn and collided with as the solid it physically is. The player must not
 * walk out through it, and every ray toward the sun hits it first. Measured by
 * tools/lightprobe.c before this existed: of the vertices that FACE the sun,
 * 98% were shadowed and 1% were lit -- shadowed by the sky itself.
 *
 * Quake's compiler answers this by treating a ray that reaches a sky face as a
 * ray that reached the sun. This does the same thing from the other side: it
 * traces, and when the thing it hit is sky, it steps past and traces again.
 *
 * BOUNDED, because a loop that continues on a condition the geometry controls
 * is a loop a map can hang. Four passes clears a shell, a light well and a
 * gap between two roofs; a map that needs a fifth gets a shadow it did not
 * quite earn, which is a wrong pixel rather than a frozen load.
 *
 * ::LVL_LIGHT_BIAS is why it steps rather than resuming exactly: a ray
 * restarted on the surface it just hit is inside that surface, which is the
 * same reason ::bake_light lifts its origin off the wall before tracing at all.
 *
 * 한국어
 * ------
 * *왜 평범한 판정이 태양에게는 틀린 판정인가.* `lqdm1`은 야외 맵이고 그 하늘은 브러시입니다.
 * 이 엔진에는 하늘 패스가 없으므로 `sky5_blu` 면은 물리적으로 그것인 고체로 그려지고 충돌합니다.
 * 플레이어는 그것을 통과해 걸어 나가면 안 되고, 태양을 향하는 모든 광선은 그것에 먼저 부딪힙니다.
 * 이것이 생기기 전 tools/lightprobe.c로 잰 값: 태양을 *마주 보는* 정점 중 98%가 그늘이고 1%가
 * 빛을 받았습니다. 하늘 자신에게 가려져서입니다.
 *
 * Quake의 컴파일러는 하늘 면에 닿은 광선을 태양에 닿은 광선으로 취급하여 이에 답합니다. 이것은
 * 반대편에서 같은 일을 합니다. 판정하고, 부딪힌 것이 하늘이면 그것을 지나쳐 다시 판정합니다.
 *
 * *횟수를 제한합니다.* 지오메트리가 제어하는 조건으로 계속되는 루프는 맵이 멈춰 세울 수 있는
 * 루프이기 때문입니다. 네 번이면 껍질과 채광정과 지붕 둘 사이의 틈을 지납니다. 다섯 번째가
 * 필요한 맵은 온전히 얻지 못한 그림자를 하나 얻으며, 그것은 멈춘 로드가 아니라 틀린 픽셀입니다. */
static int light_blocked(const Level *l, v3 from, v3 dir, float max_dist) {
    if (!l->brushes) return level_blocked(l, from, dir, max_dist);

    v3    at   = from;
    float left = max_dist;

    for (int pass = 0; pass < LVL_LIGHT_SKY_PASSES; pass++) {
        BrushTrace t;
        v3 end = v3add(at, v3scale(dir, left));
        brush_trace(l->brushes, 0, l->brushes->n_brushes, at, end,
                    v3f(0, 0, 0), v3f(0, 0, 0), &t);

        if (!t.hit && !t.start_solid) return 0;      /* reached the sky */
        if (!brush_is_sky(l->brushes, t.brush)) return 1;

        /* PAST THE WHOLE BRUSH, not past its near face.
           A skybox wall is a BOX, and stepping a couple of centimetres beyond
           the surface the ray touched leaves the ray inside it. The next pass
           then starts solid, reports the same brush, and steps another couple
           of centimetres -- so four passes advanced eight centimetres into a
           brush metres thick and the ray never came out. Measured while it was
           wrong: passing sky lifted the sunlit vertices from 174 to 209 of
           12,504, which is the shape of a fix that is not fixing anything.
           The exit is a slab test against the brush's own bounding box, which
           is already computed and is exactly the question "where does this ray
           leave this brush".
           *면이 아니라 브러시 전체를 지나갑니다.* 스카이박스의 벽은 *상자*이고, 광선이 닿은
           표면 너머로 몇 센티미터를 나아가는 것은 광선을 여전히 그 안에 남겨 둡니다. 다음
           패스는 고체 안에서 시작해 같은 브러시를 보고하고 또 몇 센티미터를 나아갑니다.
           그래서 네 번의 패스가 수 미터 두께의 브러시 안으로 8센티미터를 나아갔고 광선은
           결코 빠져나오지 못했습니다. 틀린 채로 잰 값: 하늘을 통과시키자 햇빛 받는 정점이
           12,504개 중 174개에서 209개가 되었으며, 그것은 아무것도 고치지 않는 수정의
           모습입니다. */
        const Brush *sb = &l->brushes->brushes[t.brush];
        float exit = t.t * left;
        for (int ax = 0; ax < 3; ax++) {
            float o  = ax == 0 ? at.x  : ax == 1 ? at.y  : at.z;
            float d  = ax == 0 ? dir.x : ax == 1 ? dir.y : dir.z;
            float lo = ax == 0 ? sb->min.x : ax == 1 ? sb->min.y : sb->min.z;
            float hi = ax == 0 ? sb->max.x : ax == 1 ? sb->max.y : sb->max.z;
            if (d > 1e-6f || d < -1e-6f) {
                float far_ = ((d > 0.0f ? hi : lo) - o) / d;
                if (far_ > exit) exit = far_;
            }
        }
        float went = exit + LVL_LIGHT_BIAS;
        if (went >= left) return 0;
        at   = v3add(at, v3scale(dir, went));
        left -= went;
    }
    return 1;
}

/* Does the sun reach this point? ::bake_light's own question, asked of the same
   walk rather than a copy of it.
 *
 * WHY THIS IS PUBLIC AND light_blocked IS NOT. tools/lightprobe.c is the only
 * thing in this project that checks where light lands, and its first version
 * replicated the walk above so it could ask. That is a test that agrees with
 * itself: the copy would have had the same two-centimetre step as the original,
 * passed, and said nothing. One narrow entry point -- a point, and yes or no --
 * costs less than the duplicate and cannot drift away from what ships.
 *
 * `from` is a point already lifted off the surface; the caller does that,
 * because the caller is the one that knows the normal.
 *
 * *이것은 공개이고 light_blocked는 아닌 이유.* tools/lightprobe.c는 이 프로젝트에서 빛이
 * 어디에 닿는지 검사하는 유일한 것이며, 그 첫 판은 묻기 위해 위의 걸음을 복제했습니다.
 * 그것은 자기 자신과 일치하는 테스트입니다. 복제본도 같은 2cm 걸음을 가졌을 것이고,
 * 통과했을 것이며, 아무것도 말하지 않았을 것입니다. 좁은 입구 하나가 복제본보다 싸고
 * 출하되는 것에서 멀어질 수 없습니다. */
int level_sun_reaches(const Level *l, v3 from) {
    if (l->sun_power <= 0) return 0;
    v3 sd = v3f(l->sun[0], l->sun[1], l->sun[2]);
    return !light_blocked(l, from, sd, LVL_SUN_REACH);
}

static void bake_light(MeshBuf *b, const Level *l, int first) {
    /* A level with no lamps bakes nothing, so there is nothing to look up and
       nothing worth remembering. Without this the cache charges such a level
       a hash and a probe per vertex to be told what it already knew -- which
       is most of the imported Freedoom map, and measurable: levelbench put it
       at 0.08ms of a 3.8ms build, spent entirely on filing away zeroes.
       등이 없는 레벨은 아무것도 굽지 않으므로 찾아볼 것도, 기억할 가치가 있는 것도 없습니다.
       이것이 없으면 캐시는 그런 레벨에 이미 알고 있던 사실을 듣기 위해 정점마다 해시와 탐사
       비용을 물립니다. 임포트한 Freedoom 맵 대부분이 그러하며 측정도 됩니다. levelbench는
       3.8ms 생성 중 0.08ms로 쟀고, 전부 0을 정리해 넣는 데 쓰입니다. */
    if (l->n_lights < 1) return;

    for (int vi = first; vi < b->count; vi++) {
        Vtx *v = &b->v[vi];

        /* Asked before anything is traced, because a hit is the whole point:
           the trace below is what this exists to avoid.
           무엇을 판정하기도 전에 묻습니다. 적중이 존재 이유 전부이며, 아래의 판정이 바로
           이것이 피하려는 대상이기 때문입니다. */
        /* A vertex with no normal is unlit by construction -- the facing test
           below rejects it against every light -- so the answer is always zero
           and there is nothing worth a slot. Skipped rather than special
           cased, and not counted as an overflow, because the table is fine.

           This used to be a correctness requirement as well as a saving: a zero
           normal marked a slot FREE, so storing such a vertex erased itself.
           ::LightSlot::gen holds occupancy now and the hazard is gone, but the
           saving is the same and so is the reasoning.

           법선이 없는 정점은 구조적으로 빛을 받지 않습니다. 아래의 방향 검사가 모든 광원에
           대해 기각하므로 답은 언제나 0이고 슬롯을 쓸 가치가 없습니다. 특수 처리가 아니라
           건너뛰며, 테이블에는 아무 문제가 없으므로 초과로 세지도 않습니다.

           이전에는 절약일 뿐 아니라 정확성 요구이기도 했습니다. 0 법선이 슬롯을 *비어 있음*으로
           표시했으므로 그런 정점을 저장하면 자기 자신을 지웠습니다. 이제 사용 여부는
           ::LightSlot::gen이 담당하며 그 위험은 사라졌지만, 절약도 그 근거도 그대로입니다. */
        int no_normal = (v->nx == 0.0f && v->ny == 0.0f && v->nz == 0.0f);
        int slot      = (!g_lcache_on || no_normal) ? -1 : light_slot(v);

        if (slot >= 0 && !slot_empty(&g_lcache[slot])) {
            v->lr = g_lcache[slot].lr;
            v->lg = g_lcache[slot].lg;
            v->lb = g_lcache[slot].lb;
            continue;
        }
        /* Only a FULL TABLE is worth reporting. A switched-off cache and a
           vertex with no normal both arrive here with slot < 0 and neither is
           an overflow -- counting them would make the counter fire in the one
           configuration that deliberately has no cache at all.
           보고할 가치가 있는 것은 *테이블이 가득 찬* 경우뿐입니다. 꺼진 캐시와 법선이 없는
           정점 둘 다 slot < 0으로 이곳에 도달하지만 어느 쪽도 초과가 아닙니다. 그것을 세면
           의도적으로 캐시가 아예 없는 바로 그 구성에서 카운터가 발생하게 됩니다. */
        if (slot < 0 && !no_normal && g_lcache_on) DIAG(DIAG_LIGHT_CACHE);

        v3 p = v3f(v->px, v->py, v->pz);
        v3 n = v3f(v->nx, v->ny, v->nz);

        /* Lifted off the surface before tracing. A point exactly on a wall is
           inside that wall as far as the trace is concerned, so every vertex
           would shadow itself and the whole level would bake black.
           판정 전에 표면에서 띄웁니다. 벽에 정확히 놓인 점은 판정에게는 벽 *안*이므로,
           모든 정점이 자기 자신을 가리고 레벨 전체가 검게 구워집니다. */
        v3 from = v3add(p, v3scale(n, 0.05f));

        /* --- the sun, and the sky it hangs in ---------------------------
         *
         * TRACED LIKE A LAMP, WHICH IS THE WHOLE POINT. A directional term with
         * no trace is `dot(n, dir)` and nothing else -- constant across a face,
         * which is what the shader's fixed key already gives and what makes
         * every wall one flat tone. What makes light read as SHAPED is that
         * some of the wall is in shadow and some is not, and the only thing
         * that can say which is the same ray ::level_blocked already casts for
         * the lamps.
         *
         * The ray runs a level's width rather than to a point, because a
         * directional light has no position: what matters is whether anything
         * at all stands between this vertex and the sky in that direction.
         *
         * THE SKY IS A SEPARATE TERM AND A CRUDER ONE. `_sunlight2` is a dome,
         * not a direction, and sampling a dome per vertex is a bake this
         * project cannot afford. One ray straight up answers the question that
         * matters -- is this surface under the open sky or under a roof -- and
         * the `0.5 + 0.5 * n.y` weight is the hemisphere the surface can see:
         * a floor gets all of it, a wall half, a ceiling none.
         *
         * *램프처럼 판정하며, 그것이 요점 전부입니다.* 판정 없는 방향성 항은 `dot(n, dir)`
         * 뿐이고 면 안에서 상수입니다. 셰이더의 고정 주광이 이미 주는 것이며 모든 벽을 하나의
         * 평평한 톤으로 만드는 것입니다. 빛이 *모양 있게* 읽히게 하는 것은 벽의 일부가
         * 그늘이고 일부가 아니라는 사실이며, 어느 쪽인지 말할 수 있는 것은 ::level_blocked가
         * 램프를 위해 이미 쏘는 그 광선뿐입니다.
         *
         * *하늘은 별개의 항이고 더 거친 항입니다.* `_sunlight2`는 방향이 아니라 돔이고, 돔을
         * 정점마다 표본추출하는 것은 이 프로젝트가 감당할 수 없는 베이크입니다. 수직 위로 쏘는
         * 광선 하나가 중요한 질문에 답합니다. 이 표면은 열린 하늘 아래인가 지붕 아래인가.
         * 그리고 `0.5 + 0.5 * n.y` 가중치는 표면이 볼 수 있는 반구입니다. 바닥은 전부, 벽은
         * 절반, 천장은 없습니다. */
        if (l->sun_power > 0) {
            v3 sd = v3f(l->sun[0], l->sun[1], l->sun[2]);
            float lam = v3dot(n, sd);
            if (lam > 0.0f && !light_blocked(l, from, sd, LVL_SUN_REACH)) {
                float e = lam * (l->sun_power * LVL_SUN_SCALE);
                v->lr += e; v->lg += e; v->lb += e;
            }
        }
        if (l->sky_power > 0) {
            v3 up = v3f(0.0f, 1.0f, 0.0f);
            float open = 0.5f + 0.5f * n.y;
            if (open > 0.0f && !light_blocked(l, from, up, LVL_SUN_REACH)) {
                float e = open * (l->sky_power * LVL_SUN_SCALE);
                v->lr += e; v->lg += e; v->lb += e;
            }
        }

        for (int li = 0; li < l->n_lights; li++) {
            const Light *lt = &l->lights[li];
            v3 lp = v3f(lt->x * U, lt->y * U, lt->z * U);
            v3 d  = v3sub(lp, from);
            float dist = v3len(d);
            float rad  = lt->radius * U;
            if (rad <= 0.0f || dist > rad) continue;

            /* Facing away is unlit before anything is traced, which is also
               the cheap test that skips most of the tracing.
               등지고 있으면 판정 이전에 이미 어둡습니다. 대부분의 판정을 건너뛰는 값싼
               검사이기도 합니다. */
            v3 dir = v3scale(d, 1.0f / (dist > 0.001f ? dist : 0.001f));
            float lam = v3dot(n, dir);
            if (lam <= 0.0f) continue;

            if (level_blocked(l, from, dir, dist)) continue;

            float att = 1.0f - dist / rad;
            att *= att;
            float e = att * lam * (lt->power * 0.01f);

            v->lr += e * (lt->r * (1.0f / 255.0f));
            v->lg += e * (lt->g * (1.0f / 255.0f));
            v->lb += e * (lt->b * (1.0f / 255.0f));
        }

        /* Kept, so the next rebuild finds it. Written after every light has
           been summed rather than per light, because what a later frame wants
           back is the answer, not the working.

           A vertex traced while a door happened to be open keeps that reading
           for as long as the level is loaded. That is the same rule the ones
           traced at load follow -- what a surface was lit like when it first
           existed -- rather than a second one, and it is what makes the
           picture depend on the level rather than on how many times something
           has been rebuilt since.

           다음 재생성이 찾을 수 있도록 보관합니다. 광원마다가 아니라 모든 광원을 합한 뒤에
           쓰는 이유는, 나중 프레임이 돌려받고 싶은 것이 풀이 과정이 아니라 답이기
           때문입니다.

           마침 문이 열려 있을 때 판정된 정점은 레벨이 로드되어 있는 동안 그 값을 유지합니다.
           이는 두 번째 규칙이 아니라 로드 시 판정된 정점들이 따르는 것과 같은 규칙(어떤
           표면이 처음 존재했을 때 어떻게 밝았는가)이며, 화면을 그동안 몇 번 다시
           만들었는지가 아니라 레벨에 의존하게 만드는 것이 바로 이것입니다. */
        if (slot >= 0) {
            LightSlot *s = &g_lcache[slot];
            if (slot_empty(s)) g_lcache_used++;
            s->px = v->px; s->py = v->py; s->pz = v->pz;
            s->nx = v->nx; s->ny = v->ny; s->nz = v->nz;
            s->lr = v->lr; s->lg = v->lg; s->lb = v->lb;

            /* Last, and it is what makes the slot occupied. Written after the
               fields rather than before, so a slot is never live while holding
               half an entry -- which matters for exactly the reason the whole
               generation scheme does: nothing clears this table, so a partial
               entry would be read as a complete one for as long as the level
               is loaded.
               마지막이며, 이것이 슬롯을 사용 중으로 만듭니다. 필드보다 먼저가 아니라 나중에
               쓰므로 슬롯이 항목의 절반만 담은 채 살아 있는 일이 없습니다. 세대 방식 전체가
               존재하는 것과 정확히 같은 이유로 중요합니다. 이 테이블을 비우는 것이 없으므로,
               절반짜리 항목은 레벨이 로드되어 있는 내내 완전한 항목으로 읽힙니다. */
            s->gen = g_lcache_gen;
        }
    }
}

/* Whether any door moves this brush. Linear in doors, which is at most
   ::LVL_MAX_DOORS and is walked once per brush by the run finder below --
   sixteen against a few hundred, against a build that traces light.
   어떤 문이 이 브러시를 움직이는가. 문의 수에 선형이며 그 수는 많아야 ::LVL_MAX_DOORS입니다.
   아래의 구간 탐색기가 브러시마다 한 번씩 순회합니다. 빛을 판정하는 생성 작업에 견주면 수백에
   대한 열여섯입니다. */
static int brush_is_moving(const Level *l, int bi) {
    for (int i = 0; i < l->n_doors; i++) {
        const DoorDef *d = &l->doors[i];
        if (d->sector >= 0) continue;               /* a sector door moves no brush */
        if (bi >= d->first_brush && bi < d->first_brush + d->n_brushes) return 1;
    }
    return 0;
}

int level_geometry_split(const Level *l) {
    if (!l || !l->brushes) return 0;
    for (int i = 0; i < l->n_doors; i++)
        if (l->doors[i].sector < 0 && l->doors[i].n_brushes > 0) return 1;
    return 0;
}

/* One half of a brush level, as maximal runs of brushes that belong to it.
   RUNS RATHER THAN ONE CALL PER BRUSH because ::brush_geometry gathers every
   face of one material into a single range, and it can only see the faces of
   one call -- a call per brush would produce a range per brush per material and
   spend ::LVL_MAX_RANGES on runs that were always going to be drawn together.
   THE STRETCH COUNT IS THEREFORE A COST. Each stretch pays its own set of
   materials, so a level whose doors interleave with its walls draws the same
   material once per stretch, and it is that product -- stretches times
   materials -- that ::LVL_MAX_RANGES is derived from.
   Ascending index order, so STATIC then MOVING is a stable partition of the
   whole build rather than the same vertices shuffled.
   브러시 레벨의 한쪽 절반을, 그쪽에 속하는 브러시들의 최대 연속 구간 단위로 생성합니다.
   브러시마다 한 번씩이 아니라 *구간* 단위인 이유는, ::brush_geometry가 한 재질의 모든 면을
   하나의 구간으로 모으는데 한 호출의 면만 볼 수 있기 때문입니다. 브러시마다 호출하면
   브러시마다 재질마다 구간이 하나씩 생기고, 어차피 함께 그려질 것들에 ::LVL_MAX_RANGES를
   소진하게 됩니다.
   *따라서 덩어리의 수가 곧 비용입니다.* 덩어리마다 자기 재질 집합을 치르므로, 문이 벽과
   뒤섞인 레벨은 같은 재질을 덩어리마다 한 번씩 그리게 되며, ::LVL_MAX_RANGES가 유도되는
   근거가 바로 그 곱(덩어리 수 x 재질 수)입니다.
   인덱스 오름차순이므로 STATIC 다음 MOVING은 전체 생성의 안정 분할이며 같은 정점을 뒤섞은
   것이 아닙니다. */
static int brush_geometry_half(MeshBuf *b, const Level *l, MdlRange *ranges,
                               int max_ranges, int want_moving) {
    const BrushMap *m = l->brushes;
    int n_ranges = 0;

    for (int bi = 0; bi < m->n_brushes; ) {
        if (brush_is_moving(l, bi) != want_moving) { bi++; continue; }

        int run = bi;
        while (run < m->n_brushes && brush_is_moving(l, run) == want_moving) run++;

        /* Offset into the caller's table, so successive runs accumulate rather
           than each overwriting the first entry.
           호출자 표 안으로 오프셋을 주어, 연속된 구간들이 각각 첫 항목을 덮어쓰지 않고
           누적되게 합니다. */
        int left = max_ranges - n_ranges;
        if (left < 0) left = 0;
        n_ranges += brush_geometry(b, m, bi, run - bi,
                                   ranges ? ranges + n_ranges : 0, left);
        bi = run;
    }
    return n_ranges;
}

int level_geometry_part(MeshBuf *b, const Level *l, MdlRange *ranges,
                        int max_ranges, LevelPart part) {
    /* Where this half's vertices begin, so the bake touches only what this
       call appended. ::bake_light has taken a `first` since the cache landed;
       this is the caller that makes the parameter earn its place.
       이 절반의 정점이 시작하는 위치이며, 베이크가 이번 호출이 덧붙인 것만 건드리게 합니다.
       ::bake_light는 캐시가 도입된 이래 `first`를 받아 왔습니다. 그 인자가 제 몫을 하게
       만드는 호출자가 바로 이것입니다. */
    int first = b->count;

    /* A level that does not split builds all of it for either half. Correct,
       and the reason a caller must ask ::level_geometry_split rather than
       assume: asking for MOVING here and drawing only that would draw the whole
       level twice.
       분할되지 않는 레벨은 어느 절반을 요청받든 전체를 생성합니다. 올바르며, 호출자가 가정하지
       않고 ::level_geometry_split에 물어야 하는 이유입니다. 이곳에 MOVING을 요청하고 그것만
       그리면 레벨 전체를 두 번 그리게 됩니다. */
    if (part == LVL_PART_ALL || !level_geometry_split(l))
        return level_geometry(b, l, ranges, max_ranges);

    int n = brush_geometry_half(b, l, ranges, max_ranges,
                                part == LVL_PART_MOVING);
    bake_light(b, l, first);
    return n;
}

int level_geometry(MeshBuf *b, const Level *l, MdlRange *ranges, int max_ranges) {
    /* Brushes bring their own builder, and it needs none of the machinery
       below: a brush face is convex by construction, so there is no ear-clip,
       no wall extrusion and no edge-span cutting.
       THE BAKE IS SHARED, though, and that is the point of doing it here rather
       than inside brush_geometry. ::bake_light reads ::Level::lights and traces
       with ::level_blocked, and both of those already answer for either model
       -- so the lamps a .map placed are shadowed against the brushes it placed
       them among, through the same function and the same cache the sector path
       uses. A second bake would be a second set of rules about what a shadow
       is.
       브러시는 자기 생성기를 가지고 오며, 그것에는 아래의 장치가 하나도 필요 없습니다. 브러시
       면은 구성상 볼록하므로 ear-clip도, 벽 압출도, 모서리 구간 절단도 없습니다.
       다만 *베이크는 공유합니다*. 그것이 brush_geometry 안이 아니라 이곳에서 하는 이유입니다.
       ::bake_light는 ::Level::lights를 읽고 ::level_blocked로 판정하는데, 그 둘은 이미 어느
       모델에 대해서든 답합니다. 따라서 .map이 놓은 등은 그것이 놓인 브러시들에 대해, 섹터
       경로가 쓰는 것과 같은 함수와 같은 캐시를 통해 그림자가 집니다. 두 번째 베이크는 그림자가
       무엇인가에 대한 두 번째 규칙 집합이 됩니다. */
    if (l->brushes) {
        int n = brush_geometry(b, l->brushes, 0, l->brushes->n_brushes,
                               ranges, max_ranges);
        bake_light(b, l, 0);
        return n;
    }

    int n_ranges = 0;

    /* Scratch for cap triangulation. One allocation per build rather than a
       static, so nothing zero-filled lands in .data. */
    MeshBuf tmp;
    mb_init(&tmp, (LVL_MAX_PTS + LVL_MAX_SECTORS * LVL_MAX_PTS) * 3);

    /* Grouped by surface rather than by sector: every floor shares one draw,
       every ceiling another, so a level of fifty sectors is still a handful
       of draw calls. */
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; i < l->n_sectors; i++) {
            const Sector *s = &l->sectors[i];
            int first = b->count;

            if (pass == 0) {
                add_cap(b, &tmp, l, i, s->floor * U, 1);
                if (ranges) push_range(ranges, &n_ranges, max_ranges,
                                       s->mat_floor, first, b->count - first);
            } else if (pass == 1) {
                add_cap(b, &tmp, l, i, s->ceil * U, 0);
                if (ranges) push_range(ranges, &n_ranges, max_ranges,
                                       s->mat_ceil, first, b->count - first);
            } else {
                for (int e = 0; e < s->n; e++) {
                    EdgeSpan sp[LVL_MAX_SPANS];
                    int n = level_edge_spans(l, i, e, sp, LVL_MAX_SPANS);
                    for (int k = 0; k < n; k++) add_wall(b, s, e, &sp[k]);
                }
                if (ranges) push_range(ranges, &n_ranges, max_ranges,
                                       s->mat_wall, first, b->count - first);
            }
        }
    }

    mb_free(&tmp);

    /* After every surface exists, so one pass covers floors, ceilings and
       walls alike rather than three that could disagree about the rule.
       모든 표면이 만들어진 뒤입니다. 바닥·천장·벽을 규칙이 어긋날 수 있는 세 번이 아니라
       한 번의 순회로 처리합니다. */
    bake_light(b, l, 0);

    return n_ranges;
}

/* --------------------------------------------------------------- queries */

int level_ground(const Level *l, float x, float z, float feet, float step,
                 float *out_floor, float *out_ceil) {
    if (l->brushes) return brush_ground(l, x, z, feet, step, out_floor, out_ceil);

    const Sector *s = sector_at(l, x, z);
    if (!s) return 0;                        /* outside the map */

    float f = s->floor * U;
    if (f > feet + step) return 0;           /* the step up is too high */

    *out_floor = f;
    *out_ceil  = s->ceil * U;
    return 1;
}

int level_hazard_at(const Level *l, float x, float y, float z) {
    if (l->brushes) {
        /* The worst of them, not the first. Volumes are allowed to overlap --
           an author who floods a room and then drops a hotter pool in one
           corner has written exactly that -- and taking the first match would
           make the answer depend on the order the entities happen to sit in
           the file.
           첫 번째가 아니라 가장 심한 것입니다. 부피는 겹칠 수 있으며, 방을 채운 뒤 한쪽
           구석에 더 뜨거운 웅덩이를 놓은 제작자는 정확히 그것을 기록한 것입니다. 첫 일치를
           취하면 답이 파일 안 엔티티의 배치 순서에 좌우됩니다. */
        int worst = 0;
        v3 p = v3f(x, y, z);
        for (int i = 0; i < l->n_hazards; i++) {
            const HazardDef *h = &l->hazards[i];
            if (h->dps <= worst) continue;
            if (brush_point_in(l->brushes, h->first_brush, h->n_brushes, p))
                worst = h->dps;
        }
        return worst;
    }

    (void)y;                                 /* a sector's hazard IS its floor */
    const Sector *s = sector_at(l, x, z);
    return s ? s->hurt : 0;
}

/* Is this point in open space? True when some sector contains it in plan and
   its floor/ceiling straddle the height. Overlapping sectors make this the
   whole of solidity: walls, floors, ceilings and platform sides all fall out
   of the same test. */
static int open_at(const Level *l, v3 p) {
    const Sector *s = sector_at(l, p.x, p.z);
    if (!s) return 0;                        /* outside the map is solid */
    return p.y > s->floor * U && p.y < s->ceil * U;
}

static v3 nearest_edge_normal(const Level *l, v3 p) {
    float best = 1e30f;
    v3 n = v3f(0, 1, 0);

    for (int i = 0; i < l->n_sectors; i++) {
        const Sector *s = &l->sectors[i];
        for (int e = 0; e < s->n; e++) {
            int j = (e + 1) % s->n;
            float ax = s->pts[e*2] * U, az = s->pts[e*2+1] * U;
            float bx = s->pts[j*2] * U, bz = s->pts[j*2+1] * U;
            float dx = bx - ax, dz = bz - az;
            float len2 = dx*dx + dz*dz;
            float t = len2 > 0 ? ((p.x-ax)*dx + (p.z-az)*dz) / len2 : 0.0f;
            t = clampf(t, 0.0f, 1.0f);
            float qx = ax + dx*t - p.x, qz = az + dz*t - p.z;
            float d = qx*qx + qz*qz;
            if (d < best) { best = d; n = edge_normal(s, e); }
        }
    }
    return n;
}

/* The marching half of level_trace, shared with level_blocked.
 *
 * ENGLISH
 * -------
 * Walks the ray in STEP increments and reports the last OPEN distance before
 * the first solid sample, which is what both callers need and all that
 * level_blocked needs. Splitting it out is what lets the visibility test skip
 * the bisection and the normal derivation below without keeping a second copy
 * of the marcher -- and a second copy is exactly the thing that would drift,
 * because "how far apart are the samples" is a property of the level's
 * geometry rather than of who is asking.
 *
 * @param[out] out_last Distance of the last sample that was still open. Only
 *                      written when a hit is reported.
 * @return 1 when a solid sample was found within `max_dist`, 0 otherwise.
 *
 * 한국어
 * ------
 * level_trace의 마칭 부분이며 level_blocked와 공유합니다.
 *
 * 광선을 STEP 간격으로 전진시키며 첫 번째 solid 샘플 직전의 마지막 *열린* 거리를
 * 보고합니다. 두 호출자 모두 이 값을 필요로 하며, level_blocked에는 이것으로
 * 충분합니다. 이 부분을 분리한 덕분에 가시성 판정이 아래의 이분 탐색과 법선 유도를
 * 건너뛰면서도 마처의 사본을 두 개 두지 않아도 됩니다. 사본이 두 개면 반드시
 * 어긋나는데, "샘플 간격을 얼마로 하는가"는 묻는 쪽의 성질이 아니라 레벨 지오메트리의
 * 성질이기 때문입니다.
 */
/**
 * @brief Where openness can change along a ray -- and nowhere else can.
 *
 * ENGLISH
 * -------
 * THE WHOLE ARGUMENT FOR THIS FILE'S TRACE, in one paragraph. ::open_at asks
 * two questions: is there a sector under (x,z), and is y between that sector's
 * floor and ceiling. The first answer can only change where the ray crosses
 * some sector's OUTLINE in plan; the second only where it crosses that sector's
 * floor or ceiling HEIGHT. Between two consecutive such crossings the answer is
 * therefore constant, whatever the sectors are doing -- overlapping, nested,
 * last-wins, all of it. So one sample inside each piece is not an
 * approximation of the answer. It is the answer.
 *
 * That is what replaced a fixed 5cm march. The march was not wrong, it was
 * uninformed: it sampled every 5cm whether or not anything could have changed,
 * which on a 40m ray is 800 ::sector_at calls to discover a handful of
 * crossings. levelbench measured one trace at 10.8us on the arena and 16.7us
 * on the converted Doom map, and the capped frame budget -- every monster and
 * every projectile tracing at once -- at 7.3% and 11.3% of a 60fps frame.
 *
 * It also removes the approximation's one real defect: geometry thinner than
 * the step could be stepped clean over. Nothing in the shipped maps is that
 * thin, which is why it never showed, but "no wall is under 5cm" was a
 * constraint on level authors that nobody had written down.
 *
 * @param[in]  l        Level whose sectors are consulted.
 * @param[in]  o        Ray origin.
 * @param[in]  d        Ray direction; assumed unit length.
 * @param[in]  max_dist How far to look, in metres.
 * @param[out] ev       Crossing parameters, unsorted.
 * @return How many were written, or -1 if there were more than ::TRACE_MAX_EVENTS.
 *
 * @note Gathers from every sector the ray's segment could reach, rejected by
 *       the cached bounding box with a slab test. A false positive costs a few
 *       edge intersections; a false negative would lose a wall, so the test is
 *       skipped entirely for a sector whose bounds were never computed -- the
 *       state every hand-assembled fixture in tools/ is in.
 * @note Height crossings are gathered for every candidate sector rather than
 *       only the governing one. Sampling an extra point costs one ::open_at
 *       and working out which sector governs where is the question the samples
 *       are being taken to answer.
 *
 * 한국어
 * ------
 * @brief 광선을 따라 개방 여부가 바뀔 수 있는 지점. 그 외에는 바뀔 수 없습니다.
 *
 * 이 파일의 판정에 대한 논거 전부입니다. ::open_at은 두 가지를 묻습니다. (x,z) 아래에 섹터가
 * 있는가, 그리고 y가 그 섹터의 바닥과 천장 사이인가. 첫 번째 답은 광선이 어떤 섹터의
 * *외곽선*을 평면상에서 넘을 때만 바뀔 수 있고, 두 번째는 그 섹터의 바닥이나 천장 *높이*를
 * 넘을 때만 바뀔 수 있습니다. 따라서 연속된 두 교차 사이에서는 섹터들이 무엇을 하고
 * 있든(겹침, 포개짐, 마지막 선언 우선) 답이 일정합니다. 그러므로 각 조각 안에서의 샘플 하나는
 * 답의 근사가 아닙니다. 그것이 답입니다.
 *
 * 그것이 고정 5cm 마칭을 대체한 것입니다. 마칭은 틀린 것이 아니라 아는 바가 없었습니다.
 * 무언가 바뀔 수 있는지와 무관하게 5cm마다 샘플링했고, 40m 광선에서는 한 줌의 교차를 찾자고
 * ::sector_at을 800번 부르는 일입니다. levelbench는 판정 한 번을 아레나에서 10.8us, 변환된
 * Doom 맵에서 16.7us로 측정했고, 모든 몬스터와 모든 발사체가 동시에 판정하는 상한 프레임
 * 예산을 60fps 프레임의 7.3%와 11.3%로 측정했습니다.
 *
 * 또한 그 근사가 지닌 단 하나의 실제 결함을 없앱니다. 보폭보다 얇은 지오메트리는 그대로 타고
 * 넘을 수 있었습니다. 배포되는 맵에 그렇게 얇은 것이 없어서 드러나지 않았을 뿐이며, "어떤 벽도
 * 5cm 미만이어서는 안 된다"는 아무도 적어 두지 않은 레벨 제작자에 대한 제약이었습니다.
 *
 * @return 기록된 개수. ::TRACE_MAX_EVENTS를 넘으면 -1.
 * @note 광선의 선분이 닿을 수 있는 모든 섹터에서 수집하며, 캐시된 바운딩 박스에 대한 슬랩
 *       판정으로 기각합니다. 잘못된 통과는 모서리 교차 몇 번을 낭비할 뿐이고 잘못된 기각은 벽을
 *       잃으므로, 경계값이 계산된 적 없는 섹터에 대해서는 판정을 통째로 건너뜁니다. tools/의 손으로
 *       조립한 모든 픽스처가 그 상태입니다.
 * @note 높이 교차는 지배하는 섹터만이 아니라 후보 섹터 전부에 대해 수집합니다. 추가 지점 하나를
 *       샘플링하는 비용은 ::open_at 한 번이고, 어디서 어느 섹터가 지배하는지가 바로 그 샘플들이
 *       답하려는 질문입니다.
 */
#define TRACE_MAX_EVENTS 192

/**
 * @brief The narrowest piece worth sampling, in metres.
 *
 * ENGLISH
 * -------
 * BOUNDED FROM BOTH SIDES, which is what makes 1mm a choice rather than a
 * tuning constant.
 *
 * From below: sector coordinates are shorts in CENTIMETRES, so the thinnest
 * solid the format can express is 1cm and this is a tenth of that. A ray meets
 * a thin wall in `thickness / |d . n|` metres, which is longest at a grazing
 * angle and never shorter than the thickness -- so no real wall can produce a
 * piece this narrow, whatever direction it is crossed from.
 *
 * From above: the float noise on a 40m ray in single precision is about 5e-6 m,
 * so this sits two hundred times above the point where the crossings stop being
 * distinguishable at all.
 *
 * What lands in between is the thing this exists for: two outlines that share
 * an edge report the same crossing twice, and a ray through a shared vertex
 * reports several. The piece between them has no inside, so the samples that
 * would decide whether it is solid land on the boundary instead. Measured over
 * 180,000 rays against a 1mm reference march, a floor of 0.1mm still let two of
 * them through; at 1mm none do.
 *
 * 한국어
 * ------
 * @brief 샘플링할 가치가 있는 가장 좁은 조각이며 미터 단위입니다.
 *
 * 양쪽에서 경계가 지어지며, 그것이 1mm를 조정값이 아니라 *선택*으로 만듭니다.
 *
 * 아래쪽에서: 섹터 좌표는 *센티미터* 단위의 short이므로 이 형식이 표현할 수 있는 가장 얇은
 * 고체는 1cm이고 이 값은 그것의 10분의 1입니다. 광선은 얇은 벽을
 * `두께 / |d . n|` 미터에 걸쳐 통과하며, 그 값은 스치는 각도에서 가장 길고 결코 두께보다
 * 짧아지지 않습니다. 따라서 어느 방향에서 지나가든 실제 벽은 이보다 좁은 조각을 만들 수
 * 없습니다.
 *
 * 위쪽에서: 단정밀도에서 40m 광선의 부동소수점 잡음은 약 5e-6 m이므로, 이 값은 교차를 애초에
 * 구별할 수 없게 되는 지점보다 200배 위에 있습니다.
 *
 * 그 사이에 떨어지는 것이 이 상수가 존재하는 이유입니다. 모서리를 공유하는 두 외곽선은 같은
 * 교차를 두 번 보고하고, 공유된 정점을 지나는 광선은 여러 번 보고합니다. 그 사이의 조각에는
 * 안쪽이랄 것이 없으므로, 그것이 막혔는지 판단할 표본이 대신 경계 위에 놓입니다. 1mm 기준
 * 마칭에 대해 광선 180,000개로 측정했을 때 0.1mm 하한에서는 두 건이 여전히 빠져나갔고,
 * 1mm에서는 하나도 빠져나가지 않습니다.
 */
#define TRACE_MIN_SPAN 1e-3f

/* 2D ray-versus-box, in the file units the sector bounds are kept in. Returns
   whether the ray's [0,tmax] span meets the box at all.
   섹터 경계가 보관된 파일 단위에서의 2D 광선-박스 판정입니다. 광선의 [0,tmax] 구간이 박스와
   만나는지 여부를 반환합니다. */
static int ray_hits_box(float ox, float oz, float dx, float dz, float tmax,
                        float bx0, float bz0, float bx1, float bz1) {
    float t0 = 0.0f, t1 = tmax;

    if (dx > -1e-9f && dx < 1e-9f) {
        if (ox < bx0 || ox > bx1) return 0;
    } else {
        float ta = (bx0 - ox) / dx, tb = (bx1 - ox) / dx;
        if (ta > tb) { float s = ta; ta = tb; tb = s; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return 0;
    }

    if (dz > -1e-9f && dz < 1e-9f) {
        if (oz < bz0 || oz > bz1) return 0;
    } else {
        float ta = (bz0 - oz) / dz, tb = (bz1 - oz) / dz;
        if (ta > tb) { float s = ta; ta = tb; tb = s; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return 0;
    }
    return 1;
}

static int trace_events(const Level *l, v3 o, v3 d, float max_dist, float *ev) {
    int n = 0;

    /* The ray in FILE units for the box test, so the comparison is against the
       shorts the sector already stores. U is positive, so the answer is the
       same and there is no multiply per bound.
       박스 판정을 위해 광선을 파일 단위로 둡니다. 섹터가 이미 보관하는 short와 직접
       비교하기 위함입니다. U가 양수이므로 결과는 같고 경계마다 곱셈이 없습니다. */
    float fox = o.x / U, foz = o.z / U;
    float fdx = d.x / U, fdz = d.z / U;

    for (int i = 0; i < l->n_sectors; i++) {
        const Sector *s = &l->sectors[i];
        if (s->n < 3) continue;

        if (s->has_bounds &&
            !ray_hits_box(fox, foz, fdx, fdz, max_dist,
                          s->min_x, s->min_z, s->max_x, s->max_z))
            continue;

        /* --- where the ray crosses this outline, in plan ------------------ */
        for (int e = 0, j = s->n - 1; e < s->n; j = e++) {
            float ax = s->pts[j*2] * U, az = s->pts[j*2+1] * U;
            float bx = s->pts[e*2] * U, bz = s->pts[e*2+1] * U;
            float ex = bx - ax,  ez = bz - az;

            float den = d.x * ez - d.z * ex;
            if (den > -1e-12f && den < 1e-12f) continue;   /* parallel */

            float rx = ax - o.x, rz = az - o.z;
            float t = (rx * ez - rz * ex) / den;
            float u = (rx * d.z - rz * d.x) / den;

            if (u < 0.0f || u > 1.0f)    continue;
            if (t <= 0.0f || t > max_dist) continue;

            if (n >= TRACE_MAX_EVENTS) return -1;
            ev[n++] = t;
        }

        /* --- and where it crosses this sector's floor or ceiling ---------- */
        if (d.y > 1e-9f || d.y < -1e-9f) {
            float h[2] = { s->floor * U, s->ceil * U };
            for (int k = 0; k < 2; k++) {
                float t = (h[k] - o.y) / d.y;
                if (t <= 0.0f || t > max_dist) continue;
                if (n >= TRACE_MAX_EVENTS) return -1;
                ev[n++] = t;
            }
        }
    }
    return n;
}

static int march(const Level *l, v3 origin, v3 dir, float max_dist,
                 float *out_last, float *out_solid, float *out_open) {
    float ev[TRACE_MAX_EVENTS];
    int n = trace_events(l, origin, dir, max_dist, ev);

    if (n < 0) {
        /* MORE CROSSINGS THAN THE TABLE HOLDS, so fall back to the sampler this
           replaced rather than answering from a partial list. A dropped
           crossing is a wall that is not there, which is the one failure this
           whole function exists to avoid -- and the fallback is slower, not
           wrong.
           테이블이 담을 수 있는 것보다 교차가 많으므로, 부분 목록으로 답하는 대신 이것이
           대체한 샘플러로 되돌아갑니다. 버려진 교차는 존재하지 않는 벽이며, 이 함수 전체가
           피하려는 단 하나의 고장입니다. 폴백은 느릴 뿐 틀리지 않습니다. */
        DIAG(DIAG_TRACE_EVENTS);

        float t = 0.0f, last = 0.0f;
        while (t < max_dist) {
            float next = t + TRACE_STEP;
            if (next > max_dist) next = max_dist;
            if (!open_at(l, v3add(origin, v3scale(dir, next)))) {
                *out_last = last;
                if (out_solid) *out_solid = next;
                if (out_open)  *out_open  = last;
                return 1;
            }
            last = next;
            t = next;
        }
        return 0;
    }

    /* Insertion sort. n is a few dozen for any real ray and the list is nearly
       sorted already -- sectors are visited roughly in order along the ray --
       which is the case insertion sort is best at and qsort's call overhead is
       worst at.
       삽입 정렬입니다. 실제 광선에서 n은 수십 개이고 목록은 이미 거의 정렬되어 있습니다.
       섹터가 광선을 따라 대체로 순서대로 방문되기 때문이며, 그것이 삽입 정렬이 가장 잘하는
       경우이자 qsort의 호출 비용이 가장 손해인 경우입니다. */
    for (int i = 1; i < n; i++) {
        float v = ev[i];
        int j = i - 1;
        while (j >= 0 && ev[j] > v) { ev[j + 1] = ev[j]; j--; }
        ev[j + 1] = v;
    }

    /* TWO samples strictly inside each piece, and they have to agree.
       ---------------------------------------------------------------
       One would be enough if the geometry were exact. It is not: two sectors
       that share an edge are two independent outlines that happen to have equal
       coordinates, and ::point_in_sector's crossing test is half-open, so along
       the seam there are points that read as inside NEITHER. That is a hairline
       crack in the sector model rather than in this function -- but sampling
       at a fixed fraction of an interval whose ENDS are geometry puts the
       samples where cracks are, which uniform 5cm sampling did only by luck.
       Measured: a differential run against a 1mm reference march over 180,000
       rays found 95 answers that differed from the old sampler, 90 of them the
       new one getting it right and 5 of them phantom walls from exactly this.

       Two samples, and a piece counts as solid only if both say so. A piece is
       constant in openness by construction, so two readings that disagree are
       not a thin wall -- they are the crack. Reporting open there is the answer
       that does not invent geometry, and it is what the old sampler produced
       almost every time.

       각 조각 안쪽에서 *두 번* 샘플링하며, 둘이 일치해야 합니다.
       지오메트리가 정확하다면 한 번으로 충분합니다. 그렇지 않습니다. 모서리를 공유하는 두
       섹터는 좌표가 우연히 같은 두 개의 독립적인 외곽선이고 ::point_in_sector의 교차 판정은
       반열린 구간이므로, 이음매를 따라 *어느 쪽에도* 속하지 않는 것으로 읽히는 점들이
       있습니다. 이는 이 함수가 아니라 섹터 모델의 실금입니다. 그러나 양 끝이 지오메트리인
       구간의 고정된 비율 지점에서 샘플링하면 표본이 실금이 있는 자리에 놓이며, 균일한 5cm
       샘플링은 그것을 운으로만 피했습니다. 측정하면, 1mm 기준 마칭에 대한 차등 실행에서 광선
       180,000개 중 옛 샘플러와 다른 답이 95개였고 그중 90개는 새 쪽이 옳았으며 5개가 바로
       이것에서 비롯한 유령 벽이었습니다.

       두 번 샘플링하고, 둘 다 막혔다고 할 때만 그 조각을 막힌 것으로 셉니다. 조각은 구성상
       개방 여부가 일정하므로, 서로 다른 두 판독은 얇은 벽이 아니라 실금입니다. 그곳을 열린
       것으로 보고하는 것이 지오메트리를 지어내지 않는 답이며, 옛 샘플러가 거의 언제나 내던
       답이기도 합니다. */
    float prev = 0.0f;

    /* The origin. ::level_trace has already established it is open, and
       ::level_blocked returns before it gets here if it is not.
       시작점입니다. ::level_trace가 이미 그것이 열려 있음을 확인했고,
       ::level_blocked는 그렇지 않으면 이곳에 도달하기 전에 반환합니다. */
    float open_t = 0.0f;

    for (int i = 0; i <= n; i++) {
        float e = (i < n) ? ev[i] : max_dist;

        /* Coincident crossings -- a shared edge is two outlines reporting the
           same t -- leave a piece with no inside to sample. Skipped rather than
           sampled, because the midpoint of a zero-width piece IS the boundary.
           겹치는 교차(공유 모서리는 두 외곽선이 같은 t를 보고합니다)는 안쪽이랄 것이 없는
           조각을 남깁니다. 샘플링하지 않고 건너뜁니다. 폭이 0인 조각의 중점은 곧
           경계이기 때문입니다. */
        if (e < prev + TRACE_MIN_SPAN) {
            if (e > prev) prev = e;
            continue;
        }

        float a = prev + (e - prev) * 0.3f;
        float b = prev + (e - prev) * 0.7f;
        int oa = open_at(l, v3add(origin, v3scale(dir, a)));
        int ob = open_at(l, v3add(origin, v3scale(dir, b)));

        if (!oa && !ob) {
            *out_last = prev;
            if (out_solid) *out_solid = a;
            if (out_open)  *out_open  = open_t;
            return 1;
        }

        /* WHERE THE RAY WAS LAST KNOWN OPEN, which is not `prev`. `prev` is the
           crossing itself -- exactly on a wall -- and a caller that asks what
           the surface there is like has to ask from a point that is definitely
           inside the room. ::level_trace does exactly that: it decides floor
           versus wall by moving only the height and asking again, and asking it
           AT the wall makes the plan position ambiguous and every wall answer
           come back as a floor.
           광선이 마지막으로 열려 있다고 확인된 지점이며 `prev`가 아닙니다. `prev`는 교차 그
           자체, 즉 벽 위이고, 그곳의 표면이 어떤지 묻는 호출자는 확실히 방 안쪽인 지점에서
           물어야 합니다. ::level_trace가 바로 그렇게 합니다. 높이만 옮겨 다시 물어 바닥인지
           벽인지 판단하는데, 그것을 *벽 위에서* 물으면 평면 위치가 모호해지고 모든 벽이
           바닥이라는 답으로 돌아옵니다. */
        open_t = ob ? b : a;
        prev = e;
    }
    return 0;
}

int level_blocked(const Level *l, v3 origin, v3 dir, float max_dist) {
    if (l->brushes) {
        BrushTrace t;
        return brush_ray(l, origin, dir, max_dist, &t);
    }

    /* An origin outside the map is solid, so nothing can be seen from it --
       the same answer level_trace gives by reporting a hit at distance zero.
       맵 바깥의 시작점은 막힌 것이므로 그곳에서는 아무것도 볼 수 없습니다. 거리 0에서
       충돌을 보고하는 level_trace의 답과 동일합니다. */
    if (!open_at(l, origin)) return 1;

    float last;
    return march(l, origin, dir, max_dist, &last, 0, 0);
}

int level_trace(const Level *l, v3 origin, v3 dir, float max_dist,
                float *out_t, v3 *out_normal) {
    if (l->brushes) {
        BrushTrace t;
        if (!brush_ray(l, origin, dir, max_dist, &t)) return 0;

        /* An origin inside a solid hits at once, which is what the sector path
           reports too -- level.h warns about it, because a fixture placed above
           a ceiling then sees nothing and it reads as a physics bug.
           고체 안의 시작점은 즉시 충돌하며 섹터 경로도 그렇게 보고합니다. level.h가 그것을
           경고하는데, 천장 위에 놓인 픽스처가 아무것도 보지 못하게 되고 그것이 물리 버그처럼
           읽히기 때문입니다. */
        if (t.start_solid) { *out_t = 0.0f; *out_normal = v3f(0, 1, 0); return 1; }

        /* A DISTANCE, not the fraction. The sector path bisects its way to one
           and every caller measures metres with it. */
        *out_t = t.t * max_dist;
        *out_normal = t.normal;
        return 1;
    }

    if (!open_at(l, origin)) { *out_t = 0.0f; *out_normal = v3f(0,1,0); return 1; }

    /* THE BISECTION THAT USED TO BE HERE IS GONE, and with it the last of the
       approximation. ::march sampled every ::TRACE_STEP and could only say
       "somewhere in the last 5cm", so ten halvings brought that down to well
       under a millimetre. It now returns the crossing itself -- an exact
       intersection with an edge or a height plane -- and `solid` is a point
       known to be on the far side of it. There is nothing left to narrow.
       이곳에 있던 이분 탐색이 사라졌고, 그와 함께 근사의 마지막 조각도 사라졌습니다.
       ::march는 ::TRACE_STEP마다 샘플링했으므로 "마지막 5cm 어딘가"라고밖에 말할 수 없었고,
       그래서 열 번의 반분으로 그것을 1밀리미터 아래까지 좁혔습니다. 이제는 교차 자체를
       반환합니다. 모서리 또는 높이 평면과의 정확한 교차이며, `solid`는 그 반대편에 있음이
       알려진 지점입니다. 좁힐 것이 남아 있지 않습니다. */
    float lo, hi, in_open;
    if (!march(l, origin, dir, max_dist, &lo, &hi, &in_open)) return 0;

    v3 p = v3add(origin, v3scale(dir, lo));
    v3 q = v3add(origin, v3scale(dir, hi));

    /* ASKED FROM INSIDE THE ROOM, not from the wall. `p` is the crossing and is
       therefore exactly on the surface, where sector_at is a coin toss; `probe`
       is the last point the march knew to be open. The old sampler had no such
       distinction to draw -- its `lo` was a sample it had SEEN open -- and
       handing the exact answer to the same two lines turned every wall into a
       floor. See ::march.
       벽이 아니라 방 안쪽에서 묻습니다. `p`는 교차 지점이므로 표면 위에 정확히 놓이며 그곳에서
       sector_at은 동전 던지기입니다. `probe`는 마칭이 열려 있다고 확인한 마지막 지점입니다.
       옛 샘플러에는 이 구분을 그을 일이 없었습니다. 그쪽의 `lo`는 열린 것을 *본* 표본이었기
       때문입니다. 정확한 답을 같은 두 줄에 그대로 건네자 모든 벽이 바닥이 되었습니다.
       ::march를 참조하십시오. */
    v3 probe = v3add(origin, v3scale(dir, in_open));

    /* Was it the height that changed, or the plan position? Moving only y to
       the far side tells us which. */
    v3 vert = v3f(probe.x, q.y, probe.z);
    if (!open_at(l, vert)) *out_normal = v3f(0.0f, dir.y < 0.0f ? 1.0f : -1.0f, 0.0f);
    else                   *out_normal = nearest_edge_normal(l, p);

    /* Face the normal back along the ray.
     *
     * nearest_edge_normal returns the sector's OUTWARD normal, which is what
     * the geometry builder wants -- it is building the outside of a solid. A
     * ray hitting a wall wants the opposite: the player is INSIDE the room, so
     * the surface they struck faces them, and outward points into the wall.
     *
     * The floor and ceiling case above never had this problem because it picks
     * its normal from the ray's own direction rather than from the polygon, so
     * only the four walls were wrong -- which is exactly the symptom: impact
     * particles were thrown into the wall and never seen, while shooting the
     * floor sparked correctly.
     *
     * Done here rather than in nearest_edge_normal because "outward" is the
     * right answer for that function and for level_geometry, which also calls
     * edge_normal. This is the one place the question is "which way does the
     * surface I just hit face", and that is a property of the ray as well as
     * of the polygon.
     *
     * 법선을 광선 쪽으로 되돌립니다.
     *
     * nearest_edge_normal은 섹터의 *바깥* 법선을 반환하며, 이는 고체의 바깥면을 만드는
     * 지오메트리 생성기가 원하는 값입니다. 그러나 벽에 맞은 광선은 그 반대를 원합니다.
     * 플레이어는 방 *안에* 있으므로 자신이 맞힌 표면은 자신을 향하고 있고, 바깥 방향은
     * 벽 속을 가리킵니다.
     *
     * 위의 바닥·천장 처리는 다각형이 아니라 광선 자체의 방향으로 법선을 정하므로 이
     * 문제가 없었습니다. 그래서 네 벽만 틀렸고, 이것이 정확히 관측된 증상입니다. 피격
     * 파티클이 벽 속으로 던져져 보이지 않았던 반면 바닥을 쏘면 정상적으로 튀었습니다.
     *
     * nearest_edge_normal이 아니라 이곳에서 처리하는 이유는, 그 함수와 edge_normal을
     * 함께 쓰는 level_geometry에게는 "바깥"이 옳은 답이기 때문입니다. "방금 맞힌 표면이
     * 어느 쪽을 향하는가"를 묻는 곳은 여기뿐이며, 그 답은 다각형만이 아니라 광선에도
     * 달려 있습니다. */
    if (v3dot(*out_normal, dir) > 0.0f)
        *out_normal = v3scale(*out_normal, -1.0f);

    *out_t = lo;
    return 1;
}
