/**
 * @file model.c
 * @brief Parses the model text and turns parsed models into geometry.
 *
 * ENGLISH
 * -------
 * Two clearly separate steps, and deliberately so: ::mdl_load produces a
 * ::Model struct with its points intact, and ::mdl_geometry consumes one to
 * emit triangles. The editor needs the points back rather than a finished
 * mesh, which a single combined "text to vertices" pass could not provide.
 *
 * 한국어
 * ------
 * 의도적으로 명확히 분리된 두 단계로 구성됩니다. ::mdl_load는 점 데이터가 보존된
 * ::Model 구조체를 만들고, ::mdl_geometry는 그것을 받아 삼각형을 생성합니다.
 * 에디터는 완성된 메시가 아니라 점 데이터 자체를 필요로 하며, "텍스트에서 정점으로"
 * 한 번에 처리하는 방식으로는 이를 제공할 수 없습니다.
 */

#include "model.h"
#include "mesh.h"
#include "data.h"
#include "txt.h"
#include "diag.h"

#define MAX_PTS MB_MAX_SILHOUETTE

/* The model library itself lives in assets/models.txt -- see data.h for how
   that text reaches this parser in release versus hot-reload builds. */

/* ----------------------------------------------------------------- parser */

/* --------------------------------------------- mdl_load, in its three stages */

/**
 * @struct PartState
 * @brief The settings a part inherits from the ones before it.
 *
 * ENGLISH
 * -------
 * `th`, `segments`, `at`, `rot` and `mat` all persist across parts, so a run of
 * parts sharing a thickness says `th` once. That carry-forward is the whole
 * reason ::mdl_load was three hundred lines: six loose locals, mutated by four
 * opcodes and read by a fifth, with nothing naming them as a group.
 *
 * Named as a struct, the point-list handler takes one parameter instead of six,
 * and adding a seventh setting does not widen its signature.
 *
 * 한국어
 * ------
 * @brief 한 파트가 그 앞의 파트들로부터 물려받는 설정입니다.
 *
 * `th`, `segments`, `at`, `rot`, `mat`은 모두 파트를 넘어 유지되므로, 같은 두께를 쓰는
 * 파트들이 이어질 때 `th`는 한 번만 씁니다. 그 이월이 ::mdl_load가 삼백 줄이었던 이유
 * 전부입니다. 흩어진 지역 변수 여섯 개를 네 개의 opcode가 변경하고 다섯 번째가 읽는데,
 * 그것들을 하나의 묶음으로 부르는 이름이 없었습니다.
 *
 * 구조체로 이름을 붙이면 점 목록 핸들러가 인자를 여섯 개가 아니라 하나만 받으며, 일곱
 * 번째 설정이 추가되어도 서명이 넓어지지 않습니다.
 */
typedef struct {
    int   th;         /**< Half thickness, 1/100 units. / 절반 두께. 1/100 단위. */
    int   segments;   /**< Lathe segments. / 선반 회전 분할 수. */
    int   kind;       /**< ::MDL_EXTRUDE or ::MDL_LATHE. / 압출 또는 선반. */
    short at[3];      /**< Part origin offset. / 파트 원점 오프셋. */
    short rot;        /**< Rotation about the lathe axis. / 선반 축 기준 회전. */
    char  mat[16];    /**< Material name this part draws with. / 이 파트가 사용하는 재질 이름. */
} PartState;

/**
 * @brief Empties a ::Model so a parse starts from a known state.
 * / 파싱이 알려진 상태에서 시작하도록 ::Model을 비웁니다.
 */
static void mdl_clear(Model *out) {
    out->n_parts    = 0;
    out->uv         = 100;
    out->name[0]    = 0;
    out->has_muzzle = 0;
    out->muzzle[0]  = out->muzzle[1] = out->muzzle[2] = 0;
}

/**
 * @brief Reads a part's point list: numbers until the next non-numeric word.
 *
 * ENGLISH
 * -------
 * @param[in]     p      Cursor just past the `p`/`pt`/`rev` opcode.
 * @param[out]    pts    Scratch, three shorts per point: z, y, thickness.
 * @param[in,out] n_pts  Points written.
 * @param[in]     stride 2 for `p` and `rev`, 3 for `pt` (per-point thickness).
 * @param[in]     th     The current thickness, used when `stride` is 2.
 * @param[in]     found  Non-zero while the wanted model is being read.
 * @return The cursor, left exactly where the first non-numeric word starts.
 *
 * @note Variable length rather than a count followed by a blob, which is what
 *       keeps an outline readable as a column of coordinates. `save`/restore is
 *       how it stops without consuming the word that ended it.
 *
 * 한국어
 * ------
 * @brief 파트의 점 목록을 읽습니다. 숫자가 아닌 단어가 나올 때까지의 수치들입니다.
 * @param[in]     p      `p`/`pt`/`rev` opcode 바로 뒤의 커서.
 * @param[out]    pts    임시 버퍼. 점마다 short 세 개(z, y, 두께)입니다.
 * @param[in,out] n_pts  기록된 점의 수.
 * @param[in]     stride `p`와 `rev`는 2, `pt`는 3(점별 두께)입니다.
 * @param[in]     th     `stride`가 2일 때 사용할 현재 두께.
 * @param[in]     found  원하는 모델을 읽는 동안 0이 아닙니다.
 * @return 숫자가 아닌 첫 단어가 시작되는 바로 그 자리의 커서.
 *
 * @note 개수 뒤에 덩어리를 두지 않고 가변 길이로 두는 것이 외곽선을 좌표의 열로 읽을 수
 *       있게 합니다. `save` 후 복원이 목록을 끝낸 그 단어를 소비하지 않고 멈추는 방법입니다.
 */
static const char *read_point_list(const char *p, short *pts, int *n_pts,
                                   int stride, int th, int found) {
    int len;
    for (;;) {
        int vals[3];
        int got = 0;
        const char *save = p;
        for (; got < stride; got++) {
            const char *a = txt_token(p, &len);
            if (!a || !txt_is_number(a, len)) break;
            vals[got] = txt_to_int(a, len);
            p = a + len;
        }
        if (got < stride) { p = save; break; }

        if (found && *n_pts < MAX_PTS) {
            pts[*n_pts * 3 + 0] = (short)vals[0];
            pts[*n_pts * 3 + 1] = (short)vals[1];
            pts[*n_pts * 3 + 2] = (short)(stride == 3 ? vals[2] : th);
            (*n_pts)++;
        } else if (found) {
            /* Silhouette longer than MB_MAX_SILHOUETTE: the tail is
               dropped, which deforms the part rather than removing it
               -- easy to mistake for a modelling error.
               실루엣이 MB_MAX_SILHOUETTE보다 긴 경우, 뒷부분이 버려져 부품이
               사라지는 대신 변형됩니다. 모델링 실수로 오인하기 쉽습니다. */
            DIAG(DIAG_MODEL_POINTS);
        }
    }
    return p;
}

/**
 * @brief Turns the points just read into a finished part.
 *
 * ENGLISH
 * -------
 * @param[in,out] out    Model receiving the part.
 * @param[in]     pts    The scratch ::read_point_list filled.
 * @param[in]     n_pts  How many points it holds.
 * @param[in]     st     The settings this part inherits; see ::PartState.
 * @param[in]     is_pt  Non-zero for a per-point thickness (`pt`).
 * @param[in]     is_rev Non-zero for a lathe (`rev`), which needs two points
 *                       rather than three.
 *
 * @note Each point block is a complete part carrying whatever settings are
 *       current, which is what lets one model mix a 4-unit barrel with a
 *       7-unit receiver.
 *
 * 한국어
 * ------
 * @brief 방금 읽은 점들을 완성된 파트로 만듭니다.
 * @param[in,out] out    파트를 받을 모델.
 * @param[in]     pts    ::read_point_list가 채운 임시 버퍼.
 * @param[in]     n_pts  그것이 담은 점의 수.
 * @param[in]     st     이 파트가 물려받는 설정. ::PartState를 참조하십시오.
 * @param[in]     is_pt  점별 두께(`pt`)이면 0이 아닙니다.
 * @param[in]     is_rev 선반(`rev`)이면 0이 아니며, 점이 셋이 아니라 둘만 필요합니다.
 *
 * @note 각 점 블록은 그 시점의 설정을 지닌 완결된 파트이며, 그것이 하나의 모델에서 4단위
 *       총열과 7단위 기관부를 섞을 수 있게 합니다.
 */
static void commit_part(Model *out, const short *pts, int n_pts,
                        const PartState *st, int is_pt, int is_rev) {
    /* Each point block is a complete part, carrying whatever settings
       are current. That is what lets one model mix a 4-unit barrel
       with a 7-unit receiver. */
    int need = is_rev ? 2 : 3;
    if (n_pts >= need && out->n_parts < MDL_MAX_PARTS) {
        MdlPart *part = &out->parts[out->n_parts++];
        for (int i = 0; i < n_pts; i++) {
            part->pts[i * 2 + 0] = pts[i * 3 + 0];
            part->pts[i * 2 + 1] = pts[i * 3 + 1];
            part->thick[i]       = pts[i * 3 + 2];
        }
        part->n        = n_pts;
        part->th       = st->th;
        part->tapered  = is_pt;
        part->kind     = st->kind;
        part->segments = st->segments;
        part->rot      = st->rot;
        for (int i = 0; i < 3; i++) part->at[i] = st->at[i];
        for (int i = 0; i < (int)sizeof(part->mat); i++) part->mat[i] = st->mat[i];
    }
}

/**
 * @brief Puts a muzzle on a model whose text never named one.
 *
 * ENGLISH
 * -------
 * Just past the front-most point, on the centreline at that point's height.
 * Guessing beats a hardcoded constant in weapon.c, and the editor can drag it
 * from there.
 *
 * Out of ::mdl_load because it is not parsing: it runs once, after the text is
 * exhausted, and reads only what the parse produced. Exactly the shape
 * ::level_load's four derived stages have.
 *
 * 한국어
 * ------
 * @brief 텍스트가 총구를 명시하지 않은 모델에 총구를 부여합니다.
 *
 * 가장 앞쪽 점의 바로 너머, 그 점 높이의 중심선 위입니다. 추측하는 편이 weapon.c에 상수를
 * 박아 두는 것보다 낫고, 에디터가 거기서부터 끌어 옮길 수 있습니다.
 *
 * ::mdl_load 바깥으로 뺀 이유는 이것이 파싱이 아니기 때문입니다. 텍스트가 소진된 뒤 한 번
 * 실행되며, 파싱이 만들어 낸 것만 읽습니다. ::level_load의 파생 단계 네 개와 정확히 같은
 * 형태입니다.
 */
static void mdl_derive_muzzle(Model *out) {
    /* Only outline parts carry points; a model made entirely of meshes
       has none, and guessing from an untouched sentinel would put the
       muzzle 327 units away. */
    int best_z = 32767, any = 0;
    for (int p2 = 0; p2 < out->n_parts; p2++)
        for (int i = 0; i < out->parts[p2].n; i++) {
            any = 1;
            if (out->parts[p2].pts[i*2] < best_z)
                best_z = out->parts[p2].pts[i*2];
        }
    if (!any) best_z = 6;   /* leaves the muzzle at the origin below */

    /* Average the height of everything at the front face, so the muzzle
       lands mid-bore rather than on whichever corner was seen first. */
    int sum_y = 0, count = 0;
    for (int p2 = 0; p2 < out->n_parts; p2++)
        for (int i = 0; i < out->parts[p2].n; i++)
            if (out->parts[p2].pts[i*2] == best_z) {
                sum_y += out->parts[p2].pts[i*2 + 1];
                count++;
            }

    out->muzzle[0] = 0;
    out->muzzle[1] = (short)(count ? sum_y / count : 0);
    out->muzzle[2] = (short)(best_z - 6);
}

int mdl_load(const char *name, Model *out) {
    const char *p = data_text(DATA_MODELS);
    int found = 0, len;

    /* What one part hands to the next; see ::PartState. */
    PartState st = { 5, 12, MDL_EXTRUDE, {0, 0, 0}, 0, "metal" };

    mdl_clear(out);

    short pts[MAX_PTS * 3];   /* z,y and a per-point thickness */
    int n_pts = 0;

    for (;;) {
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;

        if (txt_is(t, len, "m")) {
            if (found) break;              /* next model: this one is complete */
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;
            found = txt_is(nm, len, name);
            if (found) {
                int i = 0;
                for (; i < len && i < (int)sizeof(out->name) - 1; i++)
                    out->name[i] = nm[i];
                out->name[i] = 0;
            }
            n_pts = 0;
            continue;
        }

        /* Single-operand settings that apply to the parts that follow. */
        {
            static const char *KEYS[] = {"th", "uv", "seg", "rot"};
            int which = -1;
            for (int i = 0; i < 4; i++)
                if (txt_is(t, len, KEYS[i])) { which = i; break; }
            if (which >= 0) {
                const char *v = txt_token(p, &len);
                if (!v) break;
                p = v + len;
                if (found) {
                    int n = txt_to_int(v, len);
                    if      (which == 0) st.th = n;
                    else if (which == 1) out->uv = n;
                    else if (which == 2) st.segments = n;
                    else                 st.rot = (short)n;
                }
                continue;
            }
        }

        /* `mat <name>` -- material for the parts that follow, named after a
           recipe in assets/textures.txt. */
        if (txt_is(t, len, "mat")) {
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;
            if (found) {
                int i = 0;
                for (; i < len && i < (int)sizeof(st.mat) - 1; i++) st.mat[i] = nm[i];
                st.mat[i] = 0;
            }
            continue;
        }

        /* `mesh <name>` -- pull in an authored mesh as a part. It obeys the
           same at/rot placement as everything else, so a Blender piece can be
           bolted onto an extruded body. */
        if (txt_is(t, len, "mesh")) {
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;
            if (found && out->n_parts < MDL_MAX_PARTS) {
                MdlPart *part = &out->parts[out->n_parts++];
                part->n = 0;
                part->th = st.th;
                part->tapered = 0;
                part->kind = MDL_MESH;
                part->segments = st.segments;
                part->rot = st.rot;
                for (int i = 0; i < 3; i++) part->at[i] = st.at[i];
                for (int i = 0; i < (int)sizeof(part->mat); i++) part->mat[i] = st.mat[i];
                int i = 0;
                for (; i < len && i < (int)sizeof(part->mesh) - 1; i++)
                    part->mesh[i] = nm[i];
                part->mesh[i] = 0;
            }
            continue;
        }

        /* `muzzle x y z` -- where shots and the flash come out. */
        if (txt_is(t, len, "muzzle")) {
            for (int i = 0; i < 3; i++) {
                const char *v = txt_token(p, &len);
                if (!v || !txt_is_number(v, len)) break;
                p = v + len;
                if (found) { out->muzzle[i] = (short)txt_to_int(v, len);
                             out->has_muzzle = 1; }
            }
            continue;
        }

        /* `at x y z` places the part that follows, so limbs can be assembled
           without every outline being authored in world coordinates. */
        if (txt_is(t, len, "at")) {
            for (int i = 0; i < 3; i++) {
                const char *v = txt_token(p, &len);
                if (!v || !txt_is_number(v, len)) break;
                p = v + len;
                if (found) st.at[i] = (short)txt_to_int(v, len);
            }
            continue;
        }

        /* `p` = closed outline, `pt` = the same with a third number per point
           giving that point's half thickness, `rev` = profile to revolve. */
        int is_p   = txt_is(t, len, "p");
        int is_pt  = txt_is(t, len, "pt");
        int is_rev = txt_is(t, len, "rev");

        if (is_p || is_pt || is_rev) {
            st.kind = is_rev ? MDL_LATHE : MDL_EXTRUDE;

            /* Read the outline, then bank it as a part. `pt` carries a
               thickness per point; `p` and `rev` take the current one.
               외곽선을 읽은 뒤 파트로 확정합니다. `pt`는 점마다 두께를 지니고, `p`와
               `rev`는 현재 두께를 사용합니다. */
            p = read_point_list(p, pts, &n_pts, is_pt ? 3 : 2, st.th, found);
            if (found) commit_part(out, pts, n_pts, &st, is_pt, is_rev);
            n_pts = 0;
            continue;
        }
    }

    /* --- what the text did not say ---------------------------------------
       A model with no `muzzle` line still needs one, and it can only be
       guessed once every part has been read.
       `muzzle` 줄이 없는 모델에도 총구는 필요하며, 모든 파트를 읽은 뒤에야 추측할 수
       있습니다. */
    if (found && !out->has_muzzle && out->n_parts) mdl_derive_muzzle(out);

    return found;
}

static int same_str(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return !*a && !*b;
}

int mdl_geometry(MeshBuf *b, const Model *m, MdlRange *ranges, int max_ranges) {
    float uvs = m->uv / 100.0f;
    int n_ranges = 0;

    for (int i = 0; i < m->n_parts; i++) {
        const MdlPart *part = &m->parts[i];
        int first = b->count;

        if (part->kind == MDL_MESH)
            /* Authored meshes carry their own UVs, so the model's uv scale
               -- which is texels per unit for the generated shapes -- must
               not be applied on top of them. */
            mesh_build(b, part->mesh);
        else if (part->kind == MDL_LATHE)
            mb_lathe(b, part->pts, part->n, part->segments, uvs);
        else
            /* Passing thick[] unconditionally would make `th` dead data and
               any edit to it a no-op. Only a tapered part reads the array. */
            mb_extrude_taper(b, part->pts,
                             part->tapered ? part->thick : 0,
                             part->n, part->th / 100.0f, uvs);

        /* Extend the open range when the material has not changed, so a run
           of parts sharing one becomes a single draw. */
        if (ranges && b->count > first) {
            if (n_ranges > 0 && same_str(ranges[n_ranges-1].mat, part->mat)) {
                ranges[n_ranges-1].count += b->count - first;
            } else if (n_ranges < max_ranges) {
                MdlRange *r = &ranges[n_ranges++];
                int k = 0;
                for (; part->mat[k] && k < (int)sizeof(r->mat) - 1; k++)
                    r->mat[k] = part->mat[k];
                r->mat[k] = 0;
                r->first = first;
                r->count = b->count - first;
            }
        }

        /* Parts are authored around their own origin and then placed. Doing
           it here rather than in the primitives keeps extrude and lathe
           ignorant of placement. */
        int moved = part->at[0] || part->at[1] || part->at[2] || part->rot;
        if (!moved) continue;

        float ca = cosf(part->rot * 0.0000174533f);
        float sa = sinf(part->rot * 0.0000174533f);
        v3 off = v3f(part->at[0] / 100.0f, part->at[1] / 100.0f,
                     part->at[2] / 100.0f);

        for (int k = first; k < b->count; k++) {
            Vtx *v = &b->v[k];
            float y = v->py * ca - v->pz * sa;
            float z = v->py * sa + v->pz * ca;
            v->py = y + off.y; v->pz = z + off.z; v->px += off.x;

            float ny = v->ny * ca - v->nz * sa;
            float nz = v->ny * sa + v->nz * ca;
            v->ny = ny; v->nz = nz;
        }
    }

    return n_ranges;
}

int mdl_build(MeshBuf *b, const char *name) {
    Model m;
    if (!mdl_load(name, &m)) return 0;
    mdl_geometry(b, &m, 0, 0);
    return m.n_parts;
}
