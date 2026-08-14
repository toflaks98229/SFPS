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
static void level_clear(Level *out) {
    out->n_sectors = 0;
    out->n_ents    = 0;
    out->n_lights  = 0;
    out->n_doors   = 0;
    out->name[0]   = 0;
    out->next[0]   = 0;
    out->start[0]  = out->start[1] = out->start[2] = 0;
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
            if (found && out->n_sectors < LVL_MAX_SECTORS) {
                cur = &out->sectors[out->n_sectors++];
                cur->n = 0;
                cur->floor = 0;
                cur->ceil = 300;
                cur->hurt = 0;      /* safe unless the file says otherwise */
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
 * @brief Storage for the brush maps that loaded levels point at.
 *
 * ENGLISH
 * -------
 * TWO, because two is how many levels are live at once: the one being played
 * and the scratch ::Level world.c walks the level chain with. A third would be
 * something new, and ::DIAG_LEVEL_SLOTS is there to say so rather than let it
 * pass.
 *
 * A pool rather than a field on ::Level because a ::BrushMap is 420KB against
 * Level's 24KB and Levels are stack locals throughout the test suite -- see the
 * note on ::Level::brushes. A pool rather than the heap because nothing here
 * has a destructor to free one, and a level that is loaded and abandoned is the
 * normal case rather than the exception.
 *
 * 한국어
 * ------
 * @brief 로드된 레벨들이 가리키는 브러시 맵의 저장 공간입니다.
 *
 * 둘인 이유는 동시에 살아 있는 레벨이 둘이기 때문입니다. 플레이 중인 것과, world.c가 레벨
 * 사슬을 걸을 때 쓰는 임시 ::Level입니다. 셋째는 새로운 무언가일 것이고, 그것을 그냥 지나가게
 * 두는 대신 ::DIAG_LEVEL_SLOTS가 말해 줍니다.
 *
 * ::Level의 필드가 아니라 풀인 이유는 ::BrushMap이 Level의 24KB에 대해 420KB이고 Level이 테스트
 * 묶음 전반에서 스택 지역 변수이기 때문입니다. ::Level::brushes의 설명을 참조하십시오. 힙이
 * 아니라 풀인 이유는 이곳의 무엇도 그것을 해제할 소멸자를 갖고 있지 않고, 로드된 뒤 버려지는
 * 레벨이 예외가 아니라 평범한 경우이기 때문입니다.
 */
#define LVL_BRUSH_SLOTS 2
static BrushMap g_brush_pool[LVL_BRUSH_SLOTS];
static Level   *g_brush_owner[LVL_BRUSH_SLOTS];

static BrushMap *brush_slot_for(Level *out) {
    /* The slot this Level already had, so reloading the running level reuses
       its own storage rather than taking the scan's.
       이 Level이 이미 가지고 있던 슬롯입니다. 실행 중인 레벨을 다시 로드할 때 스캔의 것을
       빼앗지 않고 자기 저장 공간을 재사용합니다. */
    for (int i = 0; i < LVL_BRUSH_SLOTS; i++)
        if (g_brush_owner[i] == out) return &g_brush_pool[i];

    for (int i = 0; i < LVL_BRUSH_SLOTS; i++)
        if (!g_brush_owner[i]) { g_brush_owner[i] = out; return &g_brush_pool[i]; }

    /* Full. The oldest is evicted and, crucially, TOLD: its pointer is cleared
       so it becomes a level with no geometry rather than a level reading
       somebody else's. Both are broken; only one of them is quiet.
       가득 찼습니다. 가장 오래된 것을 축출하되 결정적으로 그 사실을 *알립니다*. 포인터를
       비워 다른 레벨의 것을 읽는 레벨이 아니라 지오메트리가 없는 레벨이 되게 합니다. 둘 다
       망가진 상태이지만 조용한 쪽은 하나뿐입니다. */
    DIAG(DIAG_LEVEL_SLOTS);
    if (g_brush_owner[0]) g_brush_owner[0]->brushes = 0;
    g_brush_owner[0] = out;
    return &g_brush_pool[0];
}

/* The player start, in the units ::Level already speaks: centimetres and
   millidegrees. The 90 is Quake's angle convention meeting this engine's yaw --
   tools/mapview.c derives it and says why it is a conversion and not a copy. */
static void brush_start_of(Level *out, const BrushMap *bm) {
    for (int i = 0; i < bm->n_ents; i++) {
        const BrushEnt *e = &bm->ents[i];
        const char *cn = brush_ent_value(e, "classname");
        if (!cn || !txt_is(cn, 17, "info_player_start")) continue;

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
static void brush_lights_of(Level *out, const BrushMap *bm) {
    for (int i = 0; i < bm->n_ents; i++) {
        const BrushEnt *e = &bm->ents[i];
        const char *cn = brush_ent_value(e, "classname");
        if (!cn || !txt_is(cn, 5, "light")) continue;

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
static void brush_doors_of(Level *out, const BrushMap *bm) {
    for (int i = 0; i < bm->n_ents; i++) {
        const BrushEnt *e = &bm->ents[i];
        const char *cn = brush_ent_value(e, "classname");
        if (!cn || !txt_is(cn, 9, "func_door")) continue;
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
        d->tag         = 0;                      /* triggers are entities, step 4 */
        d->key         = KEY_NONE;

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

static int load_brush_level(const char *name, Level *out) {
    int len = 0;
    const char *text = data_map(name, &len);
    if (!text) return 0;                     /* no .map of that name */

    BrushMap *bm = brush_slot_for(out);
    if (!brush_parse(text, len, bm)) return 0;

    out->brushes = bm;
    copy_name(out->name, sizeof(out->name), name, -1);
    brush_start_of(out, bm);
    brush_lights_of(out, bm);
    brush_doors_of(out, bm);
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

int level_load(const char *name, Level *out) {
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
    if (load_brush_level(name, out)) return 1;

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
    float nx, ny, nz;   /**< Which way it faced. Zero means the slot is empty. / 향하던 방향. 0이면 빈 슬롯입니다. */
    float lr, lg, lb;   /**< What the bake produced there. / 그곳에서 베이크가 만든 값. */
} LightSlot;

static LightSlot g_lcache[LIGHT_CACHE_SLOTS];
static int       g_lcache_used;

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

/* A normal is always unit length, so all-zero cannot be a real entry and needs
   no separate occupancy flag or clearing pass beyond zeroing the table.
   법선은 언제나 단위 길이이므로 전부 0인 값은 실제 항목일 수 없으며, 테이블을 0으로 만드는
   것 외에 별도의 사용 플래그도 초기화 순회도 필요하지 않습니다. */
static int slot_empty(const LightSlot *s) {
    return s->nx == 0.0f && s->ny == 0.0f && s->nz == 0.0f;
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
    for (int i = 0; i < LIGHT_CACHE_SLOTS; i++) {
        g_lcache[i].nx = g_lcache[i].ny = g_lcache[i].nz = 0.0f;
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
        /* A zero normal is what marks a slot free, so a vertex carrying one
           cannot be stored without erasing itself. It is also unlit by
           construction -- the facing test below rejects it against every light
           -- so there is nothing worth storing. Skipped rather than special
           cased, and not counted as an overflow, because the table is fine.
           빈 슬롯을 표시하는 것이 0 법선이므로, 그것을 지닌 정점은 자기 자신을 지우지 않고는
           저장할 수 없습니다. 또한 구조적으로 빛을 받지 않습니다. 아래의 방향 검사가 모든
           광원에 대해 기각합니다. 따라서 저장할 가치가 있는 것이 없습니다. 특수 처리가 아니라
           건너뛰며, 테이블에는 아무 문제가 없으므로 초과로 세지도 않습니다. */
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
        }
    }
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

int level_hazard_at(const Level *l, float x, float z) {
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
static int march(const Level *l, v3 origin, v3 dir, float max_dist,
                 float *out_last) {
    float t = 0.0f, last = 0.0f;

    /* Marching rather than intersecting every wall quad: with overlapping
       sectors the solid set is awkward to express as surfaces, but trivial to
       sample. */
    while (t < max_dist) {
        float next = t + TRACE_STEP;
        if (next > max_dist) next = max_dist;
        if (!open_at(l, v3add(origin, v3scale(dir, next)))) {
            *out_last = last;
            return 1;
        }
        last = next;
        t = next;
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
    return march(l, origin, dir, max_dist, &last);
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

    const float STEP = TRACE_STEP;

    if (!open_at(l, origin)) { *out_t = 0.0f; *out_normal = v3f(0,1,0); return 1; }

    float last;
    if (!march(l, origin, dir, max_dist, &last)) return 0;

    /* Bisect the last open/solid interval down to well under a millimetre. */
    float lo = last, hi = last + STEP;
    for (int i = 0; i < 10; i++) {
        float mid = (lo + hi) * 0.5f;
        if (open_at(l, v3add(origin, v3scale(dir, mid)))) lo = mid; else hi = mid;
    }

    v3 p = v3add(origin, v3scale(dir, lo));
    v3 q = v3add(origin, v3scale(dir, hi));

    /* Was it the height that changed, or the plan position? Moving only y to
       the far side tells us which. */
    v3 vert = v3f(p.x, q.y, p.z);
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
