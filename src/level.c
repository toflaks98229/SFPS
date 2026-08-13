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
_Static_assert(RD_MAX_LIGHTS >= LVL_MAX_LIGHTS,
               "the shader must be able to evaluate every light a level can declare");

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

int level_load(const char *name, Level *out) {
    const char *p = data_text(DATA_LEVELS);
    int found = 0, len;
    Sector *cur = 0;

    out->n_sectors = 0;
    out->n_ents = 0;
    out->n_lights = 0;
    out->n_doors  = 0;
    out->name[0] = 0;
    out->next[0] = 0;
    out->start[0] = out->start[1] = out->start[2] = 0;

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
            const char *ax = txt_token(p, &len);
            if (!ax) continue;
            p = ax + len;

            int axis = -1;
            if      (txt_is(ax, len, "up"))   axis = DOOR_UP;
            else if (txt_is(ax, len, "down")) axis = DOOR_DOWN;
            else if (txt_is(ax, len, "x"))    axis = DOOR_X;
            else if (txt_is(ax, len, "z"))    axis = DOOR_Z;
            if (axis < 0) continue;

            int amount, ok = 1;
            p = txt_read_int(p, &amount, &ok);
            if (!ok) continue;

            /* Defaults chosen so `door up 300` alone is a complete, sensible
               door: it opens on touch, needs no key, and travels at a speed
               that reads as a door rather than as a lift.
               `door up 300`만으로도 완결된 문이 되도록 기본값을 정했습니다. 접촉 시
               열리고, 열쇠가 필요 없으며, 승강기가 아니라 문으로 읽히는 속도로
               움직입니다. */
            int speed = 300, tag = 0, key = KEY_NONE;

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

            if (!cur) continue;
            if (out->n_doors >= LVL_MAX_DOORS) { DIAG(DIAG_DOOR_CAP); continue; }

            DoorDef *d = &out->doors[out->n_doors++];
            d->sector = (short)(cur - out->sectors);
            d->axis   = (short)axis;
            d->amount = (short)amount;
            d->speed  = (short)speed;
            d->tag    = (short)tag;
            d->key    = (short)key;
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
            const char *kind = txt_token(p, &len);
            if (!kind) break;
            int klen = len;
            p = kind + len;

            int x, z, ok;
            p = txt_read_int(p, &x, &ok);
            if (!ok) continue;
            p = txt_read_int(p, &z, &ok);
            if (!ok) continue;

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

            if (found && out->n_ents < LVL_MAX_ENTS) {
                Entity *e = &out->ents[out->n_ents++];
                copy_name(e->kind, LVL_MAT, kind, klen);
                e->x = (short)x;
                e->z = (short)z;
                for (int i = 0; i < LVL_ENT_PARAMS; i++) e->p[i] = (short)par[i];
            }
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

    /* A sector with fewer than three points cannot be triangulated; drop it
       rather than letting it produce degenerate geometry later. */
    int w = 0;
    for (int i = 0; i < out->n_sectors; i++)
        if (out->sectors[i].n >= 3) out->sectors[w++] = out->sectors[i];
    out->n_sectors = w;

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

    /* And the lookup grid, which is built FROM those boxes -- so it has to
       follow them, and both have to precede any query.
       그리고 조회 격자입니다. 그 박스들로부터 생성되므로 반드시 그 뒤에 와야 하며, 둘
       모두 어떤 질의보다도 앞서야 합니다. */
    level_grid_build(out);

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
    for (int j = si + 1; j < l->n_sectors; j++)
        cap_triangles(tmp, &l->sectors[j], y, 1);
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

v3 level_edge_normal(const Level *l, int sector, int edge) {
    return edge_normal(&l->sectors[sector], edge);
}

/* Where every other sector's outline crosses this edge, as parameters along
   it. These are the only places the answer to "what is beyond?" can change,
   so they are exactly where the edge has to be cut. */
static int edge_cuts(const Level *l, int si, int e, float *t, int max) {
    const Sector *s = &l->sectors[si];
    int j = (e + 1) % s->n;
    float ax = s->pts[e*2] * U, az = s->pts[e*2+1] * U;
    float dx = s->pts[j*2] * U - ax, dz = s->pts[j*2+1] * U - az;

    int n = 0;
    for (int k = 0; k < l->n_sectors && n < max; k++) {
        if (k == si) continue;
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

int level_geometry(MeshBuf *b, const Level *l, MdlRange *ranges, int max_ranges) {
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
    return n_ranges;
}

/* --------------------------------------------------------------- queries */

int level_ground(const Level *l, float x, float z, float feet, float step,
                 float *out_floor, float *out_ceil) {
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
