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

/* ----------------------------------------------------------------- parser */

static void copy_name(char *dst, int cap, const char *src, int len) {
    int i = 0;
    for (; i < len && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

int level_load(const char *name, Level *out) {
    const char *p = data_text(DATA_LEVELS);
    int found = 0, len;
    Sector *cur = 0;

    out->n_sectors = 0;
    out->n_ents = 0;
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

            if (found && out->n_ents < LVL_MAX_ENTS) {
                Entity *e = &out->ents[out->n_ents++];
                copy_name(e->kind, LVL_MAT, kind, klen);
                e->x = (short)x;
                e->z = (short)z;
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

    return found;
}

/* -------------------------------------------------------------- 2D helpers */

static int point_in_sector(const Sector *s, float x, float z) {
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
   about where a floor is. */
static const Sector *sector_at(const Level *l, float x, float z) {
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

int level_trace(const Level *l, v3 origin, v3 dir, float max_dist,
                float *out_t, v3 *out_normal) {
    const float STEP = 0.05f;

    if (!open_at(l, origin)) { *out_t = 0.0f; *out_normal = v3f(0,1,0); return 1; }

    float t = 0.0f, last = 0.0f;
    int hit = 0;

    /* Marching rather than intersecting every wall quad: with overlapping
       sectors the solid set is awkward to express as surfaces, but trivial to
       sample. A shot is a few thousand cheap tests, twice a second. */
    while (t < max_dist) {
        float next = t + STEP;
        if (next > max_dist) next = max_dist;
        if (!open_at(l, v3add(origin, v3scale(dir, next)))) { hit = 1; break; }
        last = next;
        t = next;
    }
    if (!hit) return 0;

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

    *out_t = lo;
    return 1;
}
