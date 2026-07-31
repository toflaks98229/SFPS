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

int mdl_load(const char *name, Model *out) {
    const char *p = data_text(DATA_MODELS);
    int found = 0, len;

    /* State that carries forward from one part to the next, so a run of parts
       sharing a thickness only says `th` once. */
    int th = 5, segments = 12, kind = MDL_EXTRUDE;
    short at[3] = {0, 0, 0};
    short rot = 0;
    char mat[16] = "metal";       /* what every model used before `mat` existed */

    out->n_parts = 0;
    out->uv = 100;
    out->name[0] = 0;
    out->has_muzzle = 0;
    out->muzzle[0] = out->muzzle[1] = out->muzzle[2] = 0;

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
                    if      (which == 0) th = n;
                    else if (which == 1) out->uv = n;
                    else if (which == 2) segments = n;
                    else                 rot = (short)n;
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
                for (; i < len && i < (int)sizeof(mat) - 1; i++) mat[i] = nm[i];
                mat[i] = 0;
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
                part->th = th;
                part->tapered = 0;
                part->kind = MDL_MESH;
                part->segments = segments;
                part->rot = rot;
                for (int i = 0; i < 3; i++) part->at[i] = at[i];
                for (int i = 0; i < (int)sizeof(part->mat); i++) part->mat[i] = mat[i];
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
                if (found) at[i] = (short)txt_to_int(v, len);
            }
            continue;
        }

        /* `p` = closed outline, `pt` = the same with a third number per point
           giving that point's half thickness, `rev` = profile to revolve. */
        int is_p   = txt_is(t, len, "p");
        int is_pt  = txt_is(t, len, "pt");
        int is_rev = txt_is(t, len, "rev");

        if (is_p || is_pt || is_rev) {
            kind = is_rev ? MDL_LATHE : MDL_EXTRUDE;
            int stride = is_pt ? 3 : 2;

            /* Point lists are variable length, so they run until the next
               non-numeric word. That keeps the outline readable as a column
               of coordinates instead of a count followed by a blob. */
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

                if (found && n_pts < MAX_PTS) {
                    pts[n_pts * 3 + 0] = (short)vals[0];
                    pts[n_pts * 3 + 1] = (short)vals[1];
                    pts[n_pts * 3 + 2] = (short)(is_pt ? vals[2] : th);
                    n_pts++;
                } else if (found) {
                    /* Silhouette longer than MB_MAX_SILHOUETTE: the tail is
                       dropped, which deforms the part rather than removing it
                       -- easy to mistake for a modelling error.
                       실루엣이 MB_MAX_SILHOUETTE보다 긴 경우, 뒷부분이 버려져 부품이
                       사라지는 대신 변형됩니다. 모델링 실수로 오인하기 쉽습니다. */
                    DIAG(DIAG_MODEL_POINTS);
                }
            }

            /* Each point block is a complete part, carrying whatever settings
               are current. That is what lets one model mix a 4-unit barrel
               with a 7-unit receiver. */
            int need = is_rev ? 2 : 3;
            if (found && n_pts >= need && out->n_parts < MDL_MAX_PARTS) {
                MdlPart *part = &out->parts[out->n_parts++];
                for (int i = 0; i < n_pts; i++) {
                    part->pts[i * 2 + 0] = pts[i * 3 + 0];
                    part->pts[i * 2 + 1] = pts[i * 3 + 1];
                    part->thick[i]       = pts[i * 3 + 2];
                }
                part->n        = n_pts;
                part->th       = th;
                part->tapered  = is_pt;
                part->kind     = kind;
                part->segments = segments;
                part->rot      = rot;
                for (int i = 0; i < 3; i++) part->at[i] = at[i];
                for (int i = 0; i < (int)sizeof(part->mat); i++) part->mat[i] = mat[i];
            }
            n_pts = 0;
            continue;
        }
    }

    /* No muzzle in the file: put it just past the front-most point, on the
       centreline at that point's height. Guessing beats a hardcoded constant
       in weapon.c, and the editor can drag it from there. */
    if (found && !out->has_muzzle && out->n_parts) {
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
