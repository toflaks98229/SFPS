/**
 * @file mesh.c
 * @brief Parses the baked integer mesh text into renderable triangles.
 *
 * ENGLISH
 * -------
 * The whole format is integers, which is what removes any need for a
 * floating-point parser: positions and texture coordinates arrive scaled by
 * 1000 and are divided back down on load. See mesh.h for the grammar.
 *
 * 한국어
 * ------
 * 형식 전체가 정수로 이루어져 있으며, 이것이 부동소수점 파서를 불필요하게
 * 만드는 요인입니다. 위치와 텍스처 좌표는 1000이 곱해진 상태로 들어와 로드
 * 시점에 다시 나누어집니다. 문법은 mesh.h를 참조하십시오.
 */

#include "mesh.h"
#include <stdlib.h>   /* malloc/calloc/free: this file used to reach these through windows.h */
#include "data.h"
#include "txt.h"
#include "diag.h"

/* --- File-local macros and constants / 파일 지역 매크로 및 상수 --- */

#define MAX_POS  2048   ///< @brief Maximum positions held per mesh while parsing. / 파싱 중 메시당 보관하는 최대 위치 수.
#define MAX_UV   2048   ///< @brief Maximum texture coordinates held per mesh while parsing. / 파싱 중 메시당 보관하는 최대 텍스처 좌표 수.

/* --- Static function prototypes / 정적 함수 프로토타입 --- */

static const char *read_run(const char *p, short *out, int cap, int *count);

/* --- Public function definitions / 공개 함수 정의 --- */

int mesh_build(MeshBuf *b, const char *name) {
    const char *p = data_text(DATA_MESHES);
    if (!p || !*p) return 0;

    /* Scratch tables for one mesh's shared vertex data. Heap rather than
       stack: 2048 entries of each would be ~20KB of stack frame.
       메시 하나의 공유 정점 데이터를 담을 임시 테이블입니다. 각각 2048개
       항목이면 스택 프레임이 약 20KB에 달하므로 스택 대신 힙을 사용합니다. */
    short *pos = malloc(MAX_POS * 3 * sizeof(short));
    short *uv  = malloc(MAX_UV  * 2 * sizeof(short));
    /* Partial allocation must still release whichever half succeeded.
       일부만 할당에 성공한 경우에도 성공한 쪽은 반드시 해제해야 합니다. */
    if (!pos || !uv) {
        if (pos) free(pos);
        if (uv)  free(uv);
        return 0;
    }

    int n_pos = 0, n_uv = 0, tris = 0, found = 0;

    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;

        /* 'x' opens a mesh. Reaching a second one after the target has been
           processed means the requested mesh is complete.
           'x'는 메시를 시작합니다. 대상 메시를 처리한 뒤 두 번째 'x'에
           도달했다면 요청된 메시가 완성된 것입니다. */
        if (txt_is(t, len, "x")) {
            if (found) break;              /* next mesh: this one is complete */
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;
            found = txt_is(nm, len, name);
            /* Index lists are per-mesh, so they reset at every boundary.
               인덱스 목록은 메시별로 관리되므로 경계마다 초기화됩니다. */
            n_pos = n_uv = 0;
            continue;
        }

        /* Counts are divided by the stride because read_run returns raw
           integers, not tuples.
           read_run은 튜플이 아닌 원시 정수를 반환하므로 개수를 스트라이드로
           나눕니다. */
        if (txt_is(t, len, "p")) { int c; p = read_run(p, pos, MAX_POS * 3, &c);
                                  n_pos = c / 3; continue; }
        if (txt_is(t, len, "t")) { int c; p = read_run(p, uv,  MAX_UV  * 2, &c);
                                  n_uv = c / 2; continue; }

        if (txt_is(t, len, "f")) {
            /* Six integers per triangle: three (position, uv) pairs. */
            for (;;) {
                int idx[6], ok = 1;
                /* Remember where this triangle began: a short read means the
                   face list has ended and the token must be re-examined by
                   the outer loop.
                   이 삼각형이 시작된 위치를 기억합니다. 읽기가 중단되면 면
                   목록이 끝난 것이므로 해당 토큰을 바깥 루프가 다시 검사해야
                   합니다. */
                const char *save = p;
                for (int i = 0; i < 6 && ok; i++) p = txt_read_int(p, &idx[i], &ok);
                if (!ok) { p = save; break; }
                /* Keep consuming faces of non-matching meshes to stay in sync
                   with the stream, but do not emit their geometry.
                   일치하지 않는 메시의 면도 스트림 동기화를 위해 계속
                   소비하되, 해당 지오메트리는 생성하지 않습니다. */
                if (!found) continue;

                v3 v[3];
                float tu[3], tv[3];
                int bad = 0;

                for (int c = 0; c < 3; c++) {
                    int pi = idx[c * 2], ti = idx[c * 2 + 1];
                    /* Reject out-of-range position indices: a corrupt asset
                       must not read past the scratch table.
                       범위를 벗어난 위치 인덱스를 거부합니다. 손상된 에셋이
                       임시 테이블 바깥을 읽어서는 안 됩니다. */
                    if (pi < 0 || pi >= n_pos) { bad = 1; break; }
                    v[c] = v3f(pos[pi*3 + 0] / 1000.0f,
                               pos[pi*3 + 1] / 1000.0f,
                               pos[pi*3 + 2] / 1000.0f);
                    /* A missing UV index is tolerated and collapses to the
                       texture origin, unlike a missing position.
                       위치와 달리 UV 인덱스가 없는 경우는 허용되며 텍스처
                       원점으로 축소됩니다. */
                    if (ti >= 0 && ti < n_uv) {
                        tu[c] = uv[ti*2 + 0] / 1000.0f;
                        /* OBJ's V axis runs bottom-up; a texture uploaded with
                           glTexImage2D has its first row at v = 0. Without
                           this flip every authored mapping arrives upside
                           down, which is the classic silent import bug. */
                        tv[c] = 1.0f - uv[ti*2 + 1] / 1000.0f;
                    } else {
                        tu[c] = tv[c] = 0.0f;
                    }
                }
                if (bad) continue;

                /* Flat per-face normal. The exporter's normals are a third of
                   the vertex data and this look wants faceting anyway. */
                v3 n = v3norm(v3cross(v3sub(v[1], v[0]), v3sub(v[2], v[0])));

                for (int c = 0; c < 3; c++) mb_vtx(b, v[c], n, tu[c], tv[c]);
                tris++;
            }
            continue;
        }
    }

    free(pos);
    free(uv);
    /* A mesh that was never found reports zero even if triangles from other
       meshes happened to be counted.
       찾지 못한 메시는 다른 메시의 삼각형이 집계되었더라도 0을 보고합니다. */
    return found ? tris : 0;
}

/* --- Static helper function definitions / 정적 헬퍼 함수 정의 --- */

/**
 * @brief Reads a consecutive run of integers, stopping at the first non-number.
 *
 * ENGLISH
 * -------
 * @brief Reads a consecutive run of integers, stopping at the first non-number.
 * @param[in]  p     Position in the mesh text to read from.
 * @param[out] out   Destination array receiving the parsed values.
 * @param[in]  cap   Capacity of `out` in elements.
 * @param[out] count Receives how many values were parsed.
 * @return A pointer to the first token that was not a number, so the caller
 *         can resume its own dispatch there.
 * @note Values beyond `cap` are consumed from the text but silently dropped,
 *       and `*count` never exceeds `cap`. This is what keeps a malformed or
 *       oversized mesh from writing past the scratch tables, but it means an
 *       overflowing asset is truncated rather than reported: the caller
 *       cannot distinguish "exactly cap values" from "more than cap".
 *
 * 한국어
 * ------
 * @brief 연속된 정수 구간을 읽으며, 숫자가 아닌 첫 토큰에서 중단합니다.
 * @param[in]  p     읽기를 시작할 메시 텍스트 내 위치.
 * @param[out] out   파싱된 값을 받을 대상 배열.
 * @param[in]  cap   `out`의 용량 (원소 개수).
 * @param[out] count 파싱된 값의 개수를 받습니다.
 * @return 숫자가 아닌 첫 번째 토큰에 대한 포인터. 호출자가 그 지점부터 자체
 *         분기를 재개할 수 있습니다.
 * @note `cap`을 초과하는 값은 텍스트에서 소비되지만 조용히 폐기되며, `*count`는
 *       결코 `cap`을 넘지 않습니다. 이것이 잘못된 형식이거나 크기가 과도한
 *       메시가 임시 테이블 범위 밖에 쓰는 것을 막아 줍니다. 다만 넘치는 에셋은
 *       보고되지 않고 잘려 나가므로, 호출자는 "정확히 cap개"와 "cap개 초과"를
 *       구분할 수 없습니다.
 */
static const char *read_run(const char *p, short *out, int cap, int *count) {
    int n = 0;
    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        /* A non-numeric token marks the end of this run and belongs to the
           caller's grammar, so it is deliberately not consumed.
           숫자가 아닌 토큰은 이 구간의 끝을 표시하며 호출자의 문법에 속하므로
           의도적으로 소비하지 않습니다. */
        if (!t || !txt_is_number(t, len)) break;
        /* Past capacity the value is consumed but dropped -- the stream must
           stay in sync even when the table cannot hold any more. Reported
           because *count cannot distinguish "exactly cap" from "more than
           cap", so an oversized mesh is otherwise silently truncated.
           용량을 넘으면 값을 소비하되 버립니다. 테이블이 더 담을 수 없더라도 스트림
           동기화는 유지되어야 하기 때문입니다. *count로는 "정확히 cap개"와 "cap개
           초과"를 구분할 수 없어 크기가 과도한 메시가 조용히 잘리므로 보고합니다. */
        if (n < cap) out[n++] = (short)txt_to_int(t, len);
        else         DIAG(DIAG_MESH_POINTS);
        p = t + len;
    }
    *count = n;
    return p;
}
