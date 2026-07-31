/**
 * @file render.c
 * @brief Implements the geometry builders, GPU mesh upload, and the shared shader.
 *
 * ENGLISH
 * -------
 * The mb_* half is pure CPU maths and touches no GL at all, which is what
 * lets headless tools verify geometry by reading vertices back out -- the
 * ribbon's UV seam and the extruder's winding are both checked that way,
 * where a screenshot would need a trained eye and a zoom.
 *
 * The mesh_* and rd_* halves own the GL objects and the single shader
 * program. One program with a mode switch beats three: fewer GLSL strings in
 * .rdata, fewer uniform lookups, less code.
 *
 * 한국어
 * ------
 * mb_* 계열은 순수한 CPU 연산이며 GL을 전혀 사용하지 않습니다. 덕분에 헤드리스
 * 도구가 정점을 다시 읽어 지오메트리를 검증할 수 있습니다. 리본의 UV 이음새와
 * 압출기의 감기 순서가 모두 이 방식으로 검증되며, 스크린샷으로 확인하려면 숙련된
 * 눈과 확대가 필요했을 것입니다.
 *
 * mesh_* 및 rd_* 계열은 GL 객체와 단일 셰이더 프로그램을 소유합니다. 모드 전환이
 * 가능한 하나의 프로그램이 세 개보다 낫습니다. .rdata에 들어가는 GLSL 문자열이 줄고,
 * 유니폼 조회가 줄고, 코드도 줄어듭니다.
 */

#include "render.h"
#include "diag.h"

/* ------------------------------------------------------- CPU-side builder */

void mb_init(MeshBuf *b, int cap) {
    b->v = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)cap * sizeof(Vtx));
    b->cap = cap;
    b->count = 0;
}

void mb_free(MeshBuf *b) {
    if (b->v) HeapFree(GetProcessHeap(), 0, b->v);
    b->v = 0; b->cap = b->count = 0;
}

void mb_reset(MeshBuf *b) { b->count = 0; }

void mb_vtx(MeshBuf *b, v3 p, v3 n, float u, float v) {
    /* Full: drop the vertex rather than grow or crash, but say so. A buffer
       that quietly stops accepting geometry produces a hole in the world with
       no other symptom -- see diag.h.
       가득 참: 확장하거나 중단하는 대신 정점을 버리되, 그 사실을 알립니다. 조용히
       지오메트리 수용을 멈춘 버퍼는 다른 증상 없이 월드에 구멍만 남깁니다.
       diag.h를 참조하십시오. */
    if (b->count >= b->cap) { DIAG(DIAG_VERTEX_BUF); return; }
    Vtx *o = &b->v[b->count++];
    o->px = p.x; o->py = p.y; o->pz = p.z;
    o->nx = n.x; o->ny = n.y; o->nz = n.z;
    o->u  = u;   o->v  = v;
}

/* Planar projection onto the plane the normal points out of. This is the same
   dominant-axis rule the fragment shader used to apply, moved to build time so
   the scale can vary per mesh. */
static void planar_uv(v3 p, v3 n, float s, float *u, float *v) {
    float ax = fabsf(n.x), ay = fabsf(n.y), az = fabsf(n.z);
    if (ay > ax && ay > az) { *u = p.x * s; *v = p.z * s; }
    else if (ax > az)       { *u = p.z * s; *v = p.y * s; }
    else                    { *u = p.x * s; *v = p.y * s; }
}

static void mb_planar_vtx(MeshBuf *b, v3 p, v3 n, float uvs) {
    float u, v;
    planar_uv(p, n, uvs, &u, &v);
    mb_vtx(b, p, n, u, v);
}

void mb_quad(MeshBuf *b, v3 a, v3 bb, v3 c, v3 d, v3 n, float uvs) {
    mb_planar_vtx(b, a,  n, uvs);
    mb_planar_vtx(b, bb, n, uvs);
    mb_planar_vtx(b, c,  n, uvs);
    mb_planar_vtx(b, a,  n, uvs);
    mb_planar_vtx(b, c,  n, uvs);
    mb_planar_vtx(b, d,  n, uvs);
}

void mb_box(MeshBuf *b, Box box, float uvs) {
    v3 lo = v3sub(box.c, box.h), hi = v3add(box.c, box.h);
    float s = box.inward ? -1.0f : 1.0f;

    v3 p000 = v3f(lo.x, lo.y, lo.z), p100 = v3f(hi.x, lo.y, lo.z);
    v3 p110 = v3f(hi.x, hi.y, lo.z), p010 = v3f(lo.x, hi.y, lo.z);
    v3 p001 = v3f(lo.x, lo.y, hi.z), p101 = v3f(hi.x, lo.y, hi.z);
    v3 p111 = v3f(hi.x, hi.y, hi.z), p011 = v3f(lo.x, hi.y, hi.z);

    if (box.inward) {
        mb_quad(b, p101, p001, p011, p111, v3f(0,0,-s), uvs);
        mb_quad(b, p000, p100, p110, p010, v3f(0,0, s), uvs);
        mb_quad(b, p100, p101, p111, p110, v3f(-s,0,0), uvs);
        mb_quad(b, p001, p000, p010, p011, v3f( s,0,0), uvs);
        mb_quad(b, p010, p110, p111, p011, v3f(0,-s,0), uvs);
        mb_quad(b, p001, p101, p100, p000, v3f(0, s,0), uvs);
    } else {
        mb_quad(b, p001, p101, p111, p011, v3f(0,0, 1), uvs);
        mb_quad(b, p100, p000, p010, p110, v3f(0,0,-1), uvs);
        mb_quad(b, p101, p100, p110, p111, v3f( 1,0,0), uvs);
        mb_quad(b, p000, p001, p011, p010, v3f(-1,0,0), uvs);
        mb_quad(b, p011, p111, p110, p010, v3f(0, 1,0), uvs);
        mb_quad(b, p000, p100, p101, p001, v3f(0,-1,0), uvs);
    }
}

void mb_box_mirror(MeshBuf *b, Box box, float uvs) {
    mb_box(b, box, uvs);
    /* Reflecting an axis-aligned box is just negating its centre on x --
       mb_box regenerates correct winding from centre and extent, so no
       triangle order fixup is needed. Parts already on x=0 would double up,
       so callers pass those to mb_box directly. */
    box.c.x = -box.c.x;
    mb_box(b, box, uvs);
}

/* ------------------------------------------------------------- extrusion */

/* Twice the signed area of triangle abc in the (z,y) plane. Positive means
   the turn a->b->c is counter-clockwise. */
static float tri_cross(float az, float ay, float bz, float by,
                       float cz, float cy) {
    return (bz - az) * (cy - ay) - (by - ay) * (cz - az);
}

static int point_in_tri(float pz, float py,
                        float az, float ay, float bz, float by,
                        float cz, float cy) {
    float d1 = tri_cross(az, ay, bz, by, pz, py);
    float d2 = tri_cross(bz, by, cz, cy, pz, py);
    float d3 = tri_cross(cz, cy, az, ay, pz, py);
    /* Strictly inside: all three turns agree in sign. */
    return (d1 > 0 && d2 > 0 && d3 > 0) || (d1 < 0 && d2 < 0 && d3 < 0);
}

/* Ear clipping. Expects a counter-clockwise simple polygon; writes 3*(n-2)
   indices and returns the triangle count. */
static int ear_clip(const float *z, const float *y, int n, unsigned char *out) {
    unsigned char idx[MB_MAX_SILHOUETTE];
    for (int i = 0; i < n; i++) idx[i] = (unsigned char)i;

    int m = n, tris = 0, guard = n * n;

    while (m > 3 && guard-- > 0) {
        int clipped = 0;
        for (int i = 0; i < m; i++) {
            int a = idx[(i + m - 1) % m], b = idx[i], c = idx[(i + 1) % m];

            /* Reflex corners cannot be ears. */
            if (tri_cross(z[a], y[a], z[b], y[b], z[c], y[c]) <= 0) continue;

            int blocked = 0;
            for (int k = 0; k < m && !blocked; k++) {
                int p = idx[k];
                if (p == a || p == b || p == c) continue;
                blocked = point_in_tri(z[p], y[p], z[a], y[a],
                                       z[b], y[b], z[c], y[c]);
            }
            if (blocked) continue;

            out[tris * 3 + 0] = (unsigned char)a;
            out[tris * 3 + 1] = (unsigned char)b;
            out[tris * 3 + 2] = (unsigned char)c;
            tris++;

            for (int k = i; k < m - 1; k++) idx[k] = idx[k + 1];
            m--;
            clipped = 1;
            break;
        }
        if (!clipped) break;   /* self-intersecting or degenerate outline */
    }

    if (m == 3) {
        out[tris * 3 + 0] = idx[0];
        out[tris * 3 + 1] = idx[1];
        out[tris * 3 + 2] = idx[2];
        tris++;
    }
    return tris;
}

void mb_extrude(MeshBuf *b, const short *pts, int n, float hx, float uvs) {
    mb_extrude_taper(b, pts, 0, n, hx, uvs);
}

void mb_extrude_taper(MeshBuf *b, const short *pts, const short *thick,
                      int n, float hx, float uvs) {
    if (n < 3 || n > MB_MAX_SILHOUETTE) return;

    float z[MB_MAX_SILHOUETTE], y[MB_MAX_SILHOUETTE], th[MB_MAX_SILHOUETTE];
    for (int i = 0; i < n; i++) {
        z[i]  = pts[i * 2 + 0] / 100.0f;
        y[i]  = pts[i * 2 + 1] / 100.0f;
        th[i] = thick ? thick[i] / 100.0f : hx;
    }

    /* Normalise to counter-clockwise so the normal and ear-clip rules below
       hold regardless of how the outline was authored. */
    float area2 = 0.0f;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        area2 += z[i] * y[j] - z[j] * y[i];
    }
    if (area2 < 0.0f) {
        for (int i = 0, j = n - 1; i < j; i++, j--) {
            float tmp;
            tmp = z[i];  z[i]  = z[j];  z[j]  = tmp;
            tmp = y[i];  y[i]  = y[j];  y[j]  = tmp;
            tmp = th[i]; th[i] = th[j]; th[j] = tmp;  /* thickness rides along */
        }
    }

    /* --- skirt: one quad per edge, u running along the perimeter ---
       With a taper the four corners of an edge quad are not coplanar, so each
       triangle takes its own normal from a cross product. For constant
       thickness this reduces to exactly the old (0, -dz, dy). */
    float u = 0.0f;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float dz = z[j] - z[i], dy = y[j] - y[i];
        float len = sqrtf(dz * dz + dy * dy);
        if (len < 1e-6f) continue;

        float u0 = u * uvs, u1 = (u + len) * uvs;

        v3 a  = v3f(-th[i], y[i], z[i]), bb = v3f( th[i], y[i], z[i]);
        v3 c  = v3f( th[j], y[j], z[j]), d  = v3f(-th[j], y[j], z[j]);

        v3 n1 = v3norm(v3cross(v3sub(bb, a), v3sub(c, a)));
        v3 n2 = v3norm(v3cross(v3sub(c,  a), v3sub(d, a)));

        mb_vtx(b, a,  n1, u0, -th[i] * uvs);
        mb_vtx(b, bb, n1, u0,  th[i] * uvs);
        mb_vtx(b, c,  n1, u1,  th[j] * uvs);

        mb_vtx(b, a,  n2, u0, -th[i] * uvs);
        mb_vtx(b, c,  n2, u1,  th[j] * uvs);
        mb_vtx(b, d,  n2, u1, -th[j] * uvs);

        u += len;
    }

    /* --- caps: the wide flat sides, which are most of what you see --- */
    unsigned char tri[(MB_MAX_SILHOUETTE - 2) * 3];
    int tris = ear_clip(z, y, n, tri);

    for (int k = 0; k < tris; k++) {
        int i0 = tri[k * 3 + 0], i1 = tri[k * 3 + 1], i2 = tri[k * 3 + 2];

        v3 p0p = v3f( th[i0], y[i0], z[i0]), p1p = v3f( th[i1], y[i1], z[i1]);
        v3 p2p = v3f( th[i2], y[i2], z[i2]);
        v3 p0n = v3f(-th[i0], y[i0], z[i0]), p1n = v3f(-th[i1], y[i1], z[i1]);
        v3 p2n = v3f(-th[i2], y[i2], z[i2]);

        /* A tapered cap is not flat either, so these are cross products too. */
        v3 np = v3norm(v3cross(v3sub(p2p, p0p), v3sub(p1p, p0p)));
        v3 nn = v3norm(v3cross(v3sub(p1n, p0n), v3sub(p2n, p0n)));

        /* The two caps face opposite ways, so one takes the reversed order. */
        mb_vtx(b, p0p, np, z[i0]*uvs, y[i0]*uvs);
        mb_vtx(b, p2p, np, z[i2]*uvs, y[i2]*uvs);
        mb_vtx(b, p1p, np, z[i1]*uvs, y[i1]*uvs);

        mb_vtx(b, p0n, nn, z[i0]*uvs, y[i0]*uvs);
        mb_vtx(b, p1n, nn, z[i1]*uvs, y[i1]*uvs);
        mb_vtx(b, p2n, nn, z[i2]*uvs, y[i2]*uvs);
    }
}

void mb_polygon(MeshBuf *b, const short *pts, int n, float y, int up, float uvs) {
    if (n < 3 || n > MB_MAX_SILHOUETTE) return;

    /* The clipper works in a generic 2D pair; here that pair is (x, z). */
    float ax[MB_MAX_SILHOUETTE], az[MB_MAX_SILHOUETTE];
    for (int i = 0; i < n; i++) {
        ax[i] = pts[i * 2 + 0] / 100.0f;
        az[i] = pts[i * 2 + 1] / 100.0f;
    }

    float area2 = 0.0f;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        area2 += ax[i] * az[j] - ax[j] * az[i];
    }
    if (area2 < 0.0f) {
        for (int i = 0, j = n - 1; i < j; i++, j--) {
            float t;
            t = ax[i]; ax[i] = ax[j]; ax[j] = t;
            t = az[i]; az[i] = az[j]; az[j] = t;
        }
    }

    unsigned char tri[(MB_MAX_SILHOUETTE - 2) * 3];
    int tris = ear_clip(ax, az, n, tri);

    v3 nrm = v3f(0.0f, up ? 1.0f : -1.0f, 0.0f);

    for (int k = 0; k < tris; k++) {
        int i0 = tri[k * 3 + 0], i1 = tri[k * 3 + 1], i2 = tri[k * 3 + 2];
        /* A floor is seen from above and a ceiling from below, so one of the
           two takes the reversed winding. */
        int a = i0, bIdx = up ? i2 : i1, c = up ? i1 : i2;

        mb_vtx(b, v3f(ax[a],    y, az[a]),    nrm, ax[a]    * uvs, az[a]    * uvs);
        mb_vtx(b, v3f(ax[bIdx], y, az[bIdx]), nrm, ax[bIdx] * uvs, az[bIdx] * uvs);
        mb_vtx(b, v3f(ax[c],    y, az[c]),    nrm, ax[c]    * uvs, az[c]    * uvs);
    }
}

/* ---------------------------------------------------------------- lathe */

void mb_lathe(MeshBuf *b, const short *pts, int n, int segments, float uvs) {
    if (n < 2 || n > MB_MAX_SILHOUETTE) return;
    if (segments < 3)  segments = 3;
    if (segments > 64) segments = 64;

    for (int i = 0; i < n - 1; i++) {
        float z0 = pts[i * 2 + 0] / 100.0f, r0 = pts[i * 2 + 1] / 100.0f;
        float z1 = pts[(i+1) * 2 + 0] / 100.0f, r1 = pts[(i+1) * 2 + 1] / 100.0f;

        for (int s = 0; s < segments; s++) {
            float a0 = M_TAU * s / segments;
            float a1 = M_TAU * (s + 1) / segments;
            float c0 = cosf(a0), s0 = sinf(a0);
            float c1 = cosf(a1), s1 = sinf(a1);

            v3 p00 = v3f(c0 * r0, s0 * r0, z0), p10 = v3f(c1 * r0, s1 * r0, z0);
            v3 p11 = v3f(c1 * r1, s1 * r1, z1), p01 = v3f(c0 * r1, s0 * r1, z1);

            /* u wraps around the revolve, v runs along the profile. */
            float u0 = (float)s / segments, u1 = (float)(s + 1) / segments;
            float v0 = z0 * uvs, v1 = z1 * uvs;

            /* Radial normals, so a lathed barrel shades as a cylinder rather
               than as a ring of flat facets. A degenerate end cap (r == 0)
               has no radial direction, so it falls back to the axis. */
            v3 n00 = (r0 > 1e-5f) ? v3f(c0, s0, 0) : v3f(0, 0, z0 < z1 ? -1 : 1);
            v3 n10 = (r0 > 1e-5f) ? v3f(c1, s1, 0) : n00;
            v3 n11 = (r1 > 1e-5f) ? v3f(c1, s1, 0) : v3f(0, 0, z1 < z0 ? -1 : 1);
            v3 n01 = (r1 > 1e-5f) ? v3f(c0, s0, 0) : n11;

            mb_vtx(b, p00, n00, u0, v0);
            mb_vtx(b, p10, n10, u1, v0);
            mb_vtx(b, p11, n11, u1, v1);

            mb_vtx(b, p00, n00, u0, v0);
            mb_vtx(b, p11, n11, u1, v1);
            mb_vtx(b, p01, n01, u0, v1);
        }
    }
}

/* Billboards and lines are only ever drawn in RD_FLAT, which ignores the
   texture, so their UVs are corner markers rather than a mapping. */
void mb_billboard(MeshBuf *b, v3 centre, v3 right, v3 up, float w, float h) {
    v3 r = v3scale(right, w * 0.5f), u = v3scale(up, h * 0.5f);
    v3 n = v3norm(v3cross(right, up));
    v3 p00 = v3sub(v3sub(centre, r), u), p10 = v3sub(v3add(centre, r), u);
    v3 p11 = v3add(v3add(centre, r), u), p01 = v3add(v3sub(centre, r), u);

    mb_vtx(b, p00, n, 0, 0); mb_vtx(b, p10, n, 1, 0); mb_vtx(b, p11, n, 1, 1);
    mb_vtx(b, p00, n, 0, 0); mb_vtx(b, p11, n, 1, 1); mb_vtx(b, p01, n, 0, 1);
}

void mb_billboard_uv(MeshBuf *b, v3 centre, v3 right, v3 up, float w, float h,
                     float u0, float v0, float u1, float v1) {
    v3 r = v3scale(right, w * 0.5f), u = v3scale(up, h * 0.5f);
    v3 n = v3norm(v3cross(right, up));
    v3 p00 = v3sub(v3sub(centre, r), u), p10 = v3sub(v3add(centre, r), u);
    v3 p11 = v3add(v3add(centre, r), u), p01 = v3add(v3sub(centre, r), u);

    mb_vtx(b, p00, n, u0, v0); mb_vtx(b, p10, n, u1, v0); mb_vtx(b, p11, n, u1, v1);
    mb_vtx(b, p00, n, u0, v0); mb_vtx(b, p11, n, u1, v1); mb_vtx(b, p01, n, u0, v1);
}

void mb_ribbon(MeshBuf *b, v3 a, v3 bpt, v3 cam_pos, float width, float utile) {
    v3 axis = v3sub(bpt, a);
    float len = v3len(axis);
    if (len < 1e-5f) return;                 /* zero-length segment: nothing to draw */
    axis = v3scale(axis, 1.0f / len);

    /* The width axis is perpendicular to the segment AND to the eye, which is
       what makes the strip present its widest face to the camera -- the same
       "rotate around one axis only" trick a laser beam uses. Looking straight
       down the segment leaves nothing perpendicular to fall back to a fixed
       side axis instead of collapsing to a zero-width sliver. */
    v3 mid = v3scale(v3add(a, bpt), 0.5f);
    v3 to_cam = v3sub(cam_pos, mid);
    v3 side = v3cross(axis, to_cam);
    float sl = v3len(side);
    if (sl < 1e-4f) {
        v3 hint = (fabsf(axis.y) > 0.9f) ? v3f(1,0,0) : v3f(0,1,0);
        side = v3cross(axis, hint);
        sl = v3len(side);
    }
    side = v3scale(side, (width * 0.5f) / sl);

    v3 n = v3norm(v3cross(axis, side));
    v3 pa0 = v3sub(a, side),   pa1 = v3add(a, side);
    v3 pb0 = v3sub(bpt, side), pb1 = v3add(bpt, side);

    mb_vtx(b, pa0, n, 0.0f,  0.0f); mb_vtx(b, pa1, n, 0.0f,  1.0f); mb_vtx(b, pb1, n, utile, 1.0f);
    mb_vtx(b, pa0, n, 0.0f,  0.0f); mb_vtx(b, pb1, n, utile, 1.0f); mb_vtx(b, pb0, n, utile, 0.0f);
}

void mb_line(MeshBuf *b, v3 a, v3 bb) {
    v3 n = v3f(0, 1, 0);
    mb_vtx(b, a, n, 0, 0); mb_vtx(b, bb, n, 1, 0);
}

/* ------------------------------------------------------- GPU-side meshes */

void mesh_upload(Mesh *m, const MeshBuf *b, int dynamic) {
    if (!m->vao) {
        glGenVertexArrays(1, &m->vao);
        glBindVertexArray(m->vao);
        glGenBuffers(1, &m->vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)12);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)24);
        glEnableVertexAttribArray(2);
    } else {
        glBindVertexArray(m->vao);
        glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    }

    m->count = b->count;
    m->cap   = b->cap;
    /* glBufferData always allocates a fresh store, so respecifying every frame
       is itself the orphaning idiom -- the driver never stalls waiting for the
       previous frame to finish reading the old one. */
    GLsizeiptr bytes = (GLsizeiptr)b->count * (GLsizeiptr)sizeof(Vtx);
    if (bytes)
        glBufferData(GL_ARRAY_BUFFER, bytes, b->v,
                     dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
}

void mesh_draw(const Mesh *m) {
    if (!m->count) return;
    glBindVertexArray(m->vao);
    glDrawArrays(GL_TRIANGLES, 0, m->count);
}

void mesh_draw_range(const Mesh *m, int first, int count) {
    if (count <= 0 || first + count > m->count) return;
    glBindVertexArray(m->vao);
    glDrawArrays(GL_TRIANGLES, first, count);
}

void mesh_draw_lines(const Mesh *m) {
    if (!m->count) return;
    glBindVertexArray(m->vao);
    glDrawArrays(GL_LINES, 0, m->count);
}

/* ------------------------------------------------------------- the shader */

static const char *VS_SRC =
"#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNrm;\n"
"layout(location=2) in vec2 aUV;\n"
"uniform mat4 uMVP;\n"
"out vec3 vPos; out vec3 vNrm; out vec2 vUV;\n"
"void main(){ vPos=aPos; vNrm=aNrm; vUV=aUV; gl_Position=uMVP*vec4(aPos,1.0); }\n";

/* Procedural surface shaders.
 *
 * Everything here is computed from the UV, so a wall has no texture behind it
 * at all: it stays sharp at any distance, costs no memory, and can be changed
 * by swapping one integer.
 *
 * The noise is the same hash-and-smoothstep value noise the CPU recipes in
 * tex.c use, so a material looks the same whichever path draws it. */
static const char *FS_PROC =
"float h21(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }\n"
"float n2(vec2 p){\n"
"  vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
"  return mix(mix(h21(i),h21(i+vec2(1,0)),f.x),\n"
"             mix(h21(i+vec2(0,1)),h21(i+vec2(1,1)),f.x), f.y);\n"
"}\n"
"float fbm(vec2 p){ float s=0.0,a=0.5;\n"
"  for(int k=0;k<4;k++){ s+=a*n2(p); p*=2.03; a*=0.5; } return s; }\n"

/* Running bond, with mortar joints, a fake bevel and a per-brick tint. */
"vec3 pBrick(vec2 uv, vec3 base){\n"
"  vec2 g = uv / vec2(1.0,0.45);\n"
"  g.x += mod(floor(g.y),2.0)*0.5;\n"
"  vec2 f = fract(g), id = floor(g);\n"
"  float m = 0.05;\n"
"  float e = min(min(f.x,1.0-f.x), min(f.y,1.0-f.y));\n"
"  float brick = smoothstep(m, m+0.012, e);\n"
"  vec3 c = mix(vec3(0.40,0.40,0.43), base*(0.78+0.42*h21(id)), brick);\n"
/* Light from the top-left of each brick, shade at the bottom-right. */
"  float bev = smoothstep(m,m+0.10,f.y)-smoothstep(1.0-m-0.10,1.0-m,f.y);\n"
"  c *= 1.0 - bev*0.16*brick;\n"
"  return c*(0.90+0.20*n2(uv*38.0));\n"
"}\n"

"vec3 pTile(vec2 uv, vec3 base){\n"
"  vec2 f = fract(uv), id = floor(uv);\n"
"  float e = min(min(f.x,1.0-f.x), min(f.y,1.0-f.y));\n"
"  float t = smoothstep(0.03,0.05,e);\n"
"  vec3 c = mix(vec3(0.22,0.23,0.25), base*(0.85+0.30*h21(id)), t);\n"
"  c *= 1.0 - (1.0-smoothstep(0.05,0.17,e))*0.18;\n"
"  return c*(0.94+0.12*n2(uv*24.0));\n"
"}\n"

/* Machined plate: beveled seams with a rivet at every corner. */
"vec3 pPanel(vec2 uv, vec3 base){\n"
"  vec2 f = fract(uv);\n"
"  float e = min(min(f.x,1.0-f.x), min(f.y,1.0-f.y));\n"
"  float seam = smoothstep(0.02,0.035,e);\n"
"  float bev  = 1.0 - smoothstep(0.035,0.13,e);\n"
"  vec3 c = mix(base*0.45, base, seam) * (1.0 + bev*0.22);\n"
"  float r = length(f - clamp(round(f), vec2(0.0), vec2(1.0)));\n"
"  float rivet = 1.0 - smoothstep(0.035,0.055,r);\n"
"  c = mix(c, base*1.5, rivet*0.8);\n"
"  return c*(0.93+0.14*fbm(uv*6.0));\n"
"}\n"

/* Sawn timber: bands warped by a stretched noise field. Straight bands read
   as corduroy; the warp is what makes them read as grain. */
"vec3 pWood(vec2 uv, vec3 base){\n"
"  float w = fbm(vec2(uv.x*0.35, uv.y*3.0))*3.2;\n"
"  float rings = sin((uv.y*7.0 + w)*3.14159)*0.5+0.5;\n"
"  rings *= rings;\n"
"  vec3 c = base*(1.0 - rings*0.42);\n"
"  return c*(0.93+0.14*n2(vec2(uv.x*60.0, uv.y*4.0)));\n"
"}\n"

/* Hex grid. A hexagonal lattice is two offset rectangular ones, so tiling it
   means taking whichever of the two candidate cell centres is nearer and
   measuring a hexagonal, not euclidean, distance from it. */
"vec3 pHex(vec2 uv, vec3 base){\n"
"  vec2 r = vec2(1.0,1.7320508), h = r*0.5;\n"
"  vec2 a = mod(uv,r)-h, b = mod(uv-h,r)-h;\n"
"  vec2 g = dot(a,a) < dot(b,b) ? a : b;\n"
"  vec2 q = abs(g);\n"
"  float d = max(dot(q, vec2(0.5,0.8660254)), q.x);\n"
"  float line = smoothstep(0.44,0.50,d);\n"
"  vec3 c = mix(base, base*0.30, line);\n"
"  return c*(0.92+0.16*n2(uv*20.0));\n"
"}\n"

"vec3 pMarble(vec2 uv, vec3 base){\n"
"  float t = fbm(uv*1.6);\n"
"  float v = sin((uv.x+uv.y)*2.2 + t*7.0)*0.5+0.5;\n"
"  v = pow(v, 3.0);\n"
"  return mix(base*0.62, base*1.25, v) * (0.95+0.10*n2(uv*30.0));\n"
"}\n"

"vec3 pRust(vec2 uv, vec3 base){\n"
"  float r = fbm(uv*2.4);\n"
"  float patch = smoothstep(0.42,0.72,r);\n"
"  vec3 metal = vec3(0.34,0.35,0.38)*(0.85+0.3*n2(uv*22.0));\n"
"  vec3 c = mix(metal, base, patch);\n"
"  return c*(0.90+0.18*fbm(uv*9.0));\n"
"}\n"

/* Emissive: the lines ignore lighting entirely, which is what sells them. */
"vec3 pGrid(vec2 uv, vec3 base){\n"
"  vec2 f = abs(fract(uv)-0.5);\n"
"  float d = 0.5 - max(f.x,f.y);\n"
"  float line = 1.0 - smoothstep(0.0,0.045,d);\n"
"  float node = 1.0 - smoothstep(0.0,0.12,length(f-vec2(0.5)));\n"
"  return vec3(0.05,0.06,0.08) + base*(line*0.9 + node*0.5);\n"
"}\n"

"vec3 procColour(int id, vec2 uv, vec3 base){\n"
"  if(id==1) return pBrick(uv, base);\n"
"  if(id==2) return pTile(uv, base);\n"
"  if(id==3) return pPanel(uv, base);\n"
"  if(id==4) return pWood(uv, base);\n"
"  if(id==5) return pHex(uv, base);\n"
"  if(id==6) return pMarble(uv, base);\n"
"  if(id==7) return pRust(uv, base);\n"
"  return pGrid(uv, base);\n"
"}\n";

static const char *FS_SRC =
"#version 330 core\n"
"in vec3 vPos; in vec3 vNrm; in vec2 vUV;\n"
"uniform sampler2D uTex;\n"
"uniform vec3 uEye;\n"
"uniform int uMode;\n"
"uniform vec4 uColor;\n"
/* Procedural surface selection: which shader, its base colour, how many
   pattern cells per world unit, and a spare triple for per-material tweaks. */
"uniform int uProc;\n"
"uniform vec3 uPCol;\n"
"uniform float uPScale;\n"
"uniform vec3 uPParam;\n"
"out vec4 oCol;\n";

/* Split here so FS_PROC can be spliced in between without any string
   concatenation at runtime -- glShaderSource takes an array of pieces. */
static const char *FS_MAIN =
"void main(){\n"
"  if(uMode==2){ oCol=uColor; return; }\n"
/* Text: one white atlas serves every colour, because the glyph lives in
   alpha and the colour comes from the uniform. */
"  if(uMode==3){ oCol=vec4(uColor.rgb, uColor.a*texture(uTex,vUV).a); return; }\n"
"  vec3 n=normalize(vNrm);\n"
/* UVs now arrive per vertex, so extruded silhouettes can carry a real
   parameterisation instead of everything being guessed from world position. */
"  vec2 uv=vUV;\n"
/* One lookup either way: sampled from a texture, or computed from the UV.
   Alpha carries gloss in both, so the lighting below does not care which. */
"  vec4 s = (uProc==0) ? texture(uTex,uv)\n"
"                      : vec4(procColour(uProc, uv*uPScale, uPCol), uPParam.x);\n"
/* Swatch: the material with nothing done to it, so the editor's palette shows
   the surface itself rather than the surface under some particular light. */
"  if(uMode==4){ oCol=vec4(s.rgb,1.0); return; }\n"
/* Sprite: a billboard whose alpha is a real silhouette mask, so the cutout is
   a hard discard rather than a blend. uColor.rgb tints (death fades it dark),
   uColor.a is a white hit-flash. Fogged like the world so a far monster sits
   in the same haze as the wall behind it, but otherwise unlit -- a flat sprite
   has no surface normal to light. */
"  if(uMode==5){\n"
"    vec4 sp=texture(uTex,uv);\n"
"    if(sp.a<0.5) discard;\n"
"    vec3 c=sp.rgb*uColor.rgb;\n"
"    c=mix(c, vec3(1.0,0.92,0.92), uColor.a);\n"
"    float fg=clamp(length(vPos-uEye)/30.0,0.0,1.0);\n"
"    oCol=vec4(mix(c, vec3(0.05,0.06,0.09), fg*fg), 1.0);\n"
"    return;\n"
"  }\n"
"  if(uMode==1){\n"
/* The viewmodel is drawn in gun-local space, so its UVs come out of gun
   coordinates and stay put as the player moves. Light is fixed in that same
   space, which keeps the metal reading well at every camera angle. */
"    vec3 L=normalize(vec3(0.35,0.75,0.55));\n"
"    float d=max(dot(n,L),0.0);\n"
"    float rim=pow(1.0-abs(n.z),3.0)*0.10;\n"
/* The gun is unaffected by level lighting on purpose: it has to stay readable
   in the dark corners the player actually fights in. */
/* Keep the gain at or under 1: 0.55+0.75*d peaks at 1.30, which drove the
   gunmetal highlights to white and made the weapon read as bare aluminium. */
"    vec3 lit=s.rgb*(0.42+0.58*d)+rim;\n"
/* Alpha carries gloss -- see tex.h. At this polygon count a highlight that
   travels along an edge as the gun moves says "metal" far more convincingly
   than any amount of detail inside the texture, and wood stays matte for
   free because its recipe leaves alpha at zero. */
"    vec3 V=vec3(0.0,0.0,1.0);\n"
"    float spec=pow(max(dot(n,normalize(L+V)),0.0),28.0)*s.a;\n"
/* Upward faces are where bluing rubs through to bright steel first. */
"    float wear=pow(max(n.y,0.0),3.0)*0.13*s.a;\n"
"    oCol=vec4(lit+spec*0.85+wear,1.0);\n"
"  } else {\n"
"    vec3 c=s.rgb;\n"
"    float d=max(dot(n,normalize(vec3(0.40,0.90,0.25))),0.0);\n"
"    c*=0.32+0.68*d;\n"
"    float f=clamp(length(vPos-uEye)/30.0,0.0,1.0);\n"
"    oCol=vec4(mix(c,vec3(0.05,0.06,0.09),f*f),1.0);\n"
"  }\n"
"}\n";

static GLuint g_prog;
static GLint  g_u_mvp, g_u_eye, g_u_mode, g_u_color;
static GLint  g_u_proc, g_u_pcol, g_u_pscale, g_u_pparam;

static GLuint compile(GLenum type, const char **src, int n) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, n, src, 0);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), 0, log);
        MessageBoxA(0, log, "shader compile", MB_ICONERROR);
        ExitProcess(1);
    }
    return s;
}

void rd_init(void) {
    const char *fs_parts[3] = {FS_SRC, FS_PROC, FS_MAIN};
    GLuint vs = compile(GL_VERTEX_SHADER, &VS_SRC, 1);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_parts, 3);
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);

    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(g_prog, sizeof(log), 0, log);
        MessageBoxA(0, log, "program link", MB_ICONERROR);
        ExitProcess(1);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    glUseProgram(g_prog);
    g_u_mvp   = glGetUniformLocation(g_prog, "uMVP");
    g_u_eye   = glGetUniformLocation(g_prog, "uEye");
    g_u_mode  = glGetUniformLocation(g_prog, "uMode");
    g_u_color = glGetUniformLocation(g_prog, "uColor");
    g_u_proc   = glGetUniformLocation(g_prog, "uProc");
    g_u_pcol   = glGetUniformLocation(g_prog, "uPCol");
    g_u_pscale = glGetUniformLocation(g_prog, "uPScale");
    g_u_pparam = glGetUniformLocation(g_prog, "uPParam");
    glUniform1i(glGetUniformLocation(g_prog, "uTex"), 0);
}

/* Uniforms are cheap but not free, and most draws are plain textures. Skip the
   parameter uploads entirely when the shader is off. */
void rd_proc(int proc, const float rgb[3], float scale, const float params[3]) {
    glUniform1i(g_u_proc, proc);
    if (proc == PROC_TEXTURE) return;
    glUniform3fv(g_u_pcol, 1, rgb);
    glUniform1f (g_u_pscale, scale);
    glUniform3fv(g_u_pparam, 1, params);
}

void rd_use  (void)      { glUseProgram(g_prog); }
void rd_mode (int mode)  { glUniform1i(g_u_mode, mode); }
void rd_mvp  (mat4 mvp)  { glUniformMatrix4fv(g_u_mvp, 1, GL_FALSE, mvp.m); }
void rd_eye  (v3 eye)    { glUniform3fv(g_u_eye, 1, &eye.x); }

void rd_color(float r, float g, float b, float a) {
    float c[4] = {r, g, b, a};
    glUniform4fv(g_u_color, 1, c);
}
