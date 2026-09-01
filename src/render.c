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
#include "plat.h"    /* a shader the driver refuses is not something to carry on from */
#include <stdlib.h>   /* malloc/calloc/free: this file used to reach these through windows.h */
#include "diag.h"

/* Stringify a macro's VALUE into the shader source, the same two-level trick
   post.c uses for SUPERSAMPLE. One definition of RD_MAX_LIGHTS, no hand-kept
   copy in GLSL to drift out of step -- unlike PROC_*, which still pairs by
   hand and warns about it.
   매크로의 *값*을 셰이더 소스에 문자열로 삽입합니다. post.c가 SUPERSAMPLE에 쓰는 것과
   동일한 2단계 기법입니다. RD_MAX_LIGHTS의 정의는 하나뿐이며 GLSL에 어긋날 수 있는
   수동 사본이 없습니다. 여전히 수동으로 짝을 맞추며 그 사실을 경고하는 PROC_*와는
   다릅니다. */
#define RD_STR_(x) #x
#define RD_STR(x)  RD_STR_(x)

/* ------------------------------------------------------- CPU-side builder */

void mb_init(MeshBuf *b, int cap) {
    b->v = malloc((size_t)cap * sizeof(Vtx));

    /* A FAILED ALLOCATION IS A BUFFER THAT IS ALWAYS FULL, not one that lies
       about its capacity. Recording `cap` here regardless of the result was a
       null dereference waiting on an out-of-memory condition: mb_vtx's only
       guard is `count >= cap`, so a cap of 500 over a null `v` passes the
       guard and writes through the null pointer on the very first vertex.

       Zero instead sends the failure down the path this module already has for
       running out of room -- every vertex dropped, DIAG_VERTEX_BUF raised, the
       world drawn with a hole in it. That is the right outcome twice over: it
       cannot corrupt memory, and the dev build says out loud that geometry went
       missing rather than leaving a silent gap. mesh.c handles its own partial
       allocation the same way, and this is what brings the two into line.

       할당 실패는 용량을 속이는 버퍼가 아니라 *언제나 가득 찬* 버퍼입니다. 결과와 무관하게
       `cap`을 기록하던 이전 방식은 메모리 부족 상황을 기다리는 널 역참조였습니다. mb_vtx의
       유일한 방어선은 `count >= cap`이므로, 널인 `v` 위에 용량 500이 얹히면 그 검사를
       통과해 첫 정점부터 널 포인터에 기록합니다.

       0을 넣으면 이 모듈이 이미 갖추고 있는 "자리가 없을 때"의 경로로 실패가 흘러갑니다.
       모든 정점이 버려지고, DIAG_VERTEX_BUF가 올라가며, 월드는 구멍이 뚫린 채 그려집니다.
       이것이 두 가지 이유로 옳은 결과입니다. 메모리를 손상시킬 수 없고, 개발 빌드가 조용한
       공백을 남기는 대신 지오메트리가 사라졌다고 소리 내어 말합니다. mesh.c는 자신의 부분
       할당을 같은 방식으로 처리하며, 이 변경이 둘을 일치시킵니다. */
    b->cap = b->v ? cap : 0;
    b->count = 0;
}

void mb_free(MeshBuf *b) {
    if (b->v) free(b->v);
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

    /* Zeroed HERE, in the one place a vertex is written, rather than left to
       each builder. The buffer comes from malloc and is not cleared, so a field this
       function did not set would be whatever the last mesh left behind -- and
       the symptom would be a model lit by garbage, flickering as the allocator
       reused memory.
       각 빌더에 맡기지 않고 정점을 기록하는 유일한 이 자리에서 0으로 채웁니다. 버퍼는
       malloc으로 잡고 비우지 않으므로, 이 함수가 설정하지 않은 필드는 직전 메시가
       남긴 값이 됩니다. 증상은 쓰레기 값으로 조명된 모델이며, 할당자가 메모리를 재사용할
       때마다 깜빡입니다. */
    o->lr = o->lg = o->lb = 0.0f;
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
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)32);
        glEnableVertexAttribArray(3);
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

int mesh_upload_from(Mesh *m, const MeshBuf *b, int first) {
    /* Nothing to update into. A first upload has to be a whole one: there is
       no store to write a sub-range of yet.
       갱신해 넣을 대상이 없습니다. 최초 업로드는 전체여야 합니다. 아직 부분 범위를 쓸 저장
       공간 자체가 없기 때문입니다. */
    if (!m->vao || !m->vbo) return 0;

    /* THE STORE IS EXACTLY `count` VERTICES LONG, because ::mesh_upload sizes
       glBufferData from b->count and not from b->cap. So a buffer whose total
       has changed is a buffer this cannot write into -- past the end if it
       grew, and leaving a stale tail behind if it shrank.
       저장 공간의 길이는 정확히 `count` 정점입니다. ::mesh_upload가 glBufferData의 크기를
       b->cap이 아니라 b->count로 정하기 때문입니다. 따라서 총량이 달라진 버퍼는 이것이 쓸 수
       없는 버퍼입니다. 늘었다면 끝을 넘어서고, 줄었다면 낡은 꼬리를 남깁니다. */
    if (b->count != m->count) return 0;
    if (first < 0 || first > b->count) return 0;

    GLsizeiptr bytes = (GLsizeiptr)(b->count - first) * (GLsizeiptr)sizeof(Vtx);
    /* A half with nothing in it is a success with nothing to do, not a
       failure: a level whose doors are all shut still has a moving half.
       비어 있는 절반은 실패가 아니라 할 일이 없는 성공입니다. 문이 모두 닫힌 레벨에도
       움직이는 절반은 존재합니다. */
    if (!bytes) return 1;

    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferSubData(GL_ARRAY_BUFFER,
                    (GLintptr)first * (GLintptr)sizeof(Vtx), bytes, b->v + first);
    return 1;
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

/* The vertex snap.
 *
 * ENGLISH
 * -------
 * The PlayStation's GTE transformed vertices with 16-bit fixed-point maths and
 * emitted integer screen coordinates. There was no subpixel precision, so a
 * vertex sat exactly on a pixel and jumped to the next one as the camera
 * moved. That jump is the wobble everyone recognises, and it is the single
 * most identifiable thing about the look -- more than the dithering, which
 * this project already had.
 *
 * Snapped in NDC, which is why the division and multiplication by w are here
 * rather than a plain floor on gl_Position.xy. Clip space is pre-divide, so
 * quantising it directly quantises by an amount that shrinks with distance:
 * simulated on a 640-wide grid, a point at w=1 moves 5.0e-04 either way, but
 * at w=60 the direct version moves it 5.2e-06 while the correct one still
 * moves the full 5.0e-04. The wobble would fade out with range, which is the
 * opposite of the artefact.
 *
 * uSnap is the grid, in pixels, and 0 disables the whole thing. It is a
 * uniform rather than a constant because the wobble cannot be judged from a
 * screenshot -- it only exists while the camera moves -- so it has to be
 * changeable in a running game.
 *
 * 한국어
 * ------
 * 플레이스테이션의 GTE는 16비트 고정소수점으로 정점을 변환하고 정수 화면 좌표를
 * 내놓았습니다. 서브픽셀 정밀도가 없었으므로 정점이 정확히 한 픽셀 위에 놓였다가
 * 카메라가 움직이면 다음 픽셀로 건너뛰었습니다. 그 도약이 누구나 알아보는 흔들림이며,
 * 이 룩에서 가장 식별하기 쉬운 요소입니다. 이 프로젝트가 이미 갖고 있던 디더링보다도
 * 더 그렇습니다.
 *
 * NDC에서 스냅하므로 gl_Position.xy에 단순히 floor를 적용하지 않고 w로 나누었다가 다시
 * 곱합니다. 클립 공간은 나누기 이전이므로 그대로 양자화하면 거리에 따라 줄어드는 양으로
 * 양자화됩니다. 640 격자에서 시뮬레이션한 결과, w=1인 점은 어느 방식이든 5.0e-04만큼
 * 움직이지만 w=60에서는 직접 방식이 5.2e-06인 반면 올바른 방식은 여전히 5.0e-04입니다.
 * 흔들림이 거리에 따라 사라지는데, 이는 아티팩트와 정반대입니다.
 *
 * uSnap은 픽셀 단위 격자이며 0이면 전체가 비활성화됩니다. 상수가 아니라 유니폼인 이유는
 * 흔들림을 스크린샷으로 판단할 수 없기 때문입니다. 카메라가 움직이는 동안에만 존재하므로
 * 실행 중인 게임에서 바꿀 수 있어야 합니다.
 */
static const char *VS_SRC =
"#version 330 core\n"
"layout(location=0) in vec3 aPos;\n"
"layout(location=1) in vec3 aNrm;\n"
"layout(location=2) in vec2 aUV;\n"
"layout(location=3) in vec3 aLit;\n"
"uniform mat4 uMVP;\n"
"uniform vec2 uSnap;\n"
"out vec3 vPos; out vec3 vNrm; out vec2 vUV; out vec3 vLit;\n"
/* THE SAME UV, INTERPOLATED THE OTHER WAY.
 *
 * `vUV` above is interpolated perspective-correctly, which is what every GPU
 * since the late nineties does and what the PlayStation could not: it had no
 * per-pixel divide, so it stepped texture coordinates linearly across a
 * triangle in SCREEN space. On a large polygon seen at an angle that is
 * visibly wrong -- the texture swims and creases along the diagonal the
 * triangle was split on -- and it is the single most recognisable artifact of
 * the era. `uSnap` and the dither pass already reproduce two of the other
 * three; this is the one that was missing.
 *
 * TWO VARYINGS RATHER THAN ONE, because the interpolation qualifier is fixed
 * at compile time and the strength has to be adjustable: a level built from
 * TrenchBroom brushes has faces far larger than anything the hardware being
 * imitated could draw, so full affine on this geometry warps more than it
 * evokes. The fragment shader mixes the two by `uAffine`, which is the only
 * way to have a dial at all.
 *
 * 같은 UV를 다른 방식으로 보간한 것입니다.
 *
 * 위의 `vUV`는 원근 보정 보간이며, 90년대 후반 이후의 모든 GPU가 하는 일이자 플레이스테이션이
 * *할 수 없었던* 일입니다. 픽셀당 나눗셈이 없었으므로 텍스처 좌표를 삼각형을 가로질러 *화면*
 * 공간에서 선형으로 밟았습니다. 비스듬히 본 큰 폴리곤에서 그것은 눈에 띄게 틀리며(텍스처가
 * 헤엄치고 삼각형이 나뉜 대각선을 따라 접힙니다) 그 시대의 가장 알아보기 쉬운 아티팩트입니다.
 * `uSnap`과 디더 패스가 나머지 셋 중 둘을 이미 재현하고 있으며, 이것이 빠져 있던 하나입니다.
 *
 * 하나가 아니라 *두 개*의 varying인 이유는 보간 한정자가 컴파일 시점에 고정되는데 강도는 조절
 * 가능해야 하기 때문입니다. TrenchBroom 브러시로 만든 레벨은 흉내 내려는 하드웨어가 그릴 수
 * 있던 것보다 훨씬 큰 면을 가지므로, 이 지오메트리에 전면 어파인을 적용하면 연상시키기보다
 * 일그러뜨립니다. 프래그먼트 셰이더가 `uAffine`으로 둘을 섞으며, 그것이 조절 손잡이를 갖는
 * 유일한 방법입니다. */
"noperspective out vec2 vUVa;\n"
"void main(){\n"
"  vPos=aPos; vNrm=aNrm; vUV=aUV; vUVa=aUV; vLit=aLit;\n"
"  vec4 p=uMVP*vec4(aPos,1.0);\n"
/* w <= 0 is behind the eye, where NDC is meaningless and the division would
   mirror the vertex across the screen. Those vertices are clipped anyway, so
   they are passed through untouched.
   w <= 0은 눈 뒤쪽이며 NDC가 무의미하고 나누기가 정점을 화면 반대편으로 반사시킵니다.
   어차피 클리핑되는 정점이므로 손대지 않고 통과시킵니다. */
"  if(uSnap.x>0.0 && p.w>0.0){\n"
"    vec2 ndc=p.xy/p.w;\n"
"    p.xy=floor(ndc*uSnap+0.5)/uSnap*p.w;\n"
"  }\n"
"  gl_Position=p;\n"
"}\n";

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

/* `oxide`, not `patch`: patch is a GLSL RESERVED WORD -- tessellation, 4.0
   and up -- and a fragment shader that never tessellates still may not use it
   as a variable name. NVIDIA's compiler accepts it in a #version 330 shader
   and Intel, AMD and Mesa reject it, so this built and ran perfectly here
   while failing on somebody else's machine with

       ERROR: 1:65: error(#132) Syntax error: "patch" parse error

   which is the worst shape a bug can have: nothing local reproduces it.
   build.ps1 scans for this now rather than leaving it to whoever has the
   stricter driver.

   `patch`가 아니라 `oxide`입니다. patch는 GLSL의 *예약어*(테셀레이션, 4.0 이상)이며,
   테셀레이션을 하지 않는 프래그먼트 셰이더라도 이를 변수명으로 쓸 수 없습니다. NVIDIA
   컴파일러는 #version 330에서 이를 받아 주고 Intel, AMD, Mesa는 거부합니다. 따라서 이
   코드는 이곳에서 완벽히 빌드되고 실행되면서 다른 사람의 기계에서는 실패했으며, 이는
   버그가 가질 수 있는 최악의 형태입니다. 국소적으로 재현되지 않기 때문입니다. */
"vec3 pRust(vec2 uv, vec3 base){\n"
"  float r = fbm(uv*2.4);\n"
"  float oxide = smoothstep(0.42,0.72,r);\n"
"  vec3 metal = vec3(0.34,0.35,0.38)*(0.85+0.3*n2(uv*22.0));\n"
"  vec3 c = mix(metal, base, oxide);\n"
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

/* Molten rock: a dark crust broken by glowing cracks.
 *
 * ENGLISH
 * -------
 * The crust is fbm thresholded into plates; the cracks are what is left
 * BETWEEN them, which is why this measures distance from the threshold rather
 * than drawing veins directly -- a vein drawn as a line has to be given a
 * width and a path, where a gap between plates gets both for free and always
 * closes on itself.
 *
 * Returned ABOVE 1.0 in the crack colour on purpose. Everything else here
 * returns a reflectance, which the lighting then scales down; lava emits, so
 * its brightest parts have to survive being multiplied by a dim room. That is
 * also what pushes it past the post pass's BLOOM_KNEE, so a lava floor is the
 * one surface in the game that actually blooms.
 *
 * 한국어
 * ------
 * 녹은 암석입니다. 어두운 표면 껍질이 빛나는 균열로 갈라져 있습니다.
 *
 * 껍질은 fbm을 임계값으로 잘라 만든 판이고, 균열은 그 판들 *사이에* 남는 것입니다.
 * 그래서 정맥을 직접 그리지 않고 임계값으로부터의 거리를 재는데, 선으로 그린 정맥은 폭과
 * 경로를 지정해야 하지만 판 사이의 틈은 그 둘을 저절로 얻으며 언제나 스스로 닫히기
 * 때문입니다.
 *
 * 균열 색을 의도적으로 1.0을 *넘겨* 반환합니다. 이곳의 다른 모든 것은 반사율을
 * 반환하고 조명이 그것을 낮추지만, 용암은 스스로 발광하므로 가장 밝은 부분이 어두운 방에
 * 곱해지고도 살아남아야 합니다. 그것이 또한 포스트 패스의 BLOOM_KNEE를 넘게 만들어,
 * 용암 바닥이 이 게임에서 실제로 블룸이 생기는 유일한 표면이 되게 합니다. */
/* --- and it FLOWS ----------------------------------------------------------
 *
 * ENGLISH
 * -------
 * Three motions, at three rates, because a single scrolling offset reads as
 * the texture sliding rather than as the rock moving:
 *
 *   DRIFT  the whole field creeps in one direction. This is the convection
 *          current under the crust, and it is deliberately the slowest thing
 *          here -- a lava floor that visibly races is a river, and this is a
 *          pool.
 *   SHEAR  the second, finer field drifts the OTHER way and at a different
 *          rate. Two fields moving together are one field; moving them apart
 *          is what makes plates appear to break up and re-form rather than
 *          slide as a sheet.
 *   PULSE  the crack threshold breathes, so a given spot opens and closes over
 *          time. Without it the pattern of glowing veins is fixed and only its
 *          position changes, which reads as a photograph being panned.
 *
 * The rates are deliberately not multiples of each other. Two motions at 1:2
 * repeat visibly every few seconds, and a lava floor is something the player
 * stands next to for a long time.
 *
 * 한국어
 * ------
 * 세 가지 움직임을 서로 다른 세 속도로 적용합니다. 스크롤 오프셋 하나만으로는 암석이
 * 움직이는 것이 아니라 텍스처가 미끄러지는 것으로 읽히기 때문입니다.
 *
 *   DRIFT  전체 필드가 한 방향으로 기어갑니다. 껍질 아래의 대류이며, 이곳에서 의도적으로
 *          가장 느립니다. 눈에 띄게 빠른 용암 바닥은 강이지 웅덩이가 아닙니다.
 *   SHEAR  두 번째의 더 고운 필드가 *반대* 방향으로 다른 속도로 흐릅니다. 함께 움직이는
 *          두 필드는 하나의 필드입니다. 서로 어긋나게 해야 판이 한 장으로 미끄러지는
 *          대신 부서졌다 다시 붙는 것처럼 보입니다.
 *   PULSE  균열 임계값이 호흡하므로 같은 자리가 시간에 따라 열리고 닫힙니다. 이것이
 *          없으면 빛나는 정맥의 패턴이 고정된 채 위치만 바뀌어, 사진을 좌우로 미는 것처럼
 *          읽힙니다.
 *
 * 속도는 의도적으로 서로 배수가 아닙니다. 1:2인 두 움직임은 몇 초마다 눈에 띄게 반복되며,
 * 용암 바닥은 플레이어가 오래 곁에 서 있는 대상입니다. */
"vec3 pLava(vec2 uv, vec3 base){\n"
/* DRIFT and SHEAR were here. They offset two copies of `uv` by uTime and used
   them as the fbm domain, so the crust pattern SCROLLED -- standing in for heat
   haze, and unable to do that job. The domain they scrolled in is the tiled,
   world-projected UV, so the shimmer was locked to the material's TILING: it moved
   with the texture instead of with the air above it, and on a floor tiled a dozen
   times across it read as the surface sliding under itself.
   Haze belongs to the air between the eye and the lava rather than to the lava's
   surface, which means it belongs to a screen-space pass. It is post.c's now, and
   what is left here is what lava IS: a crust fixed to the rock.
   DRIFT와 SHEAR가 이곳에 있었습니다. `uv`의 사본 둘을 uTime으로 밀어 fbm의 정의역으로 썼으므로
   지각 패턴이 *스크롤*했습니다. 열 아지랑이를 대신하고 있었고 그 일을 할 수 없었습니다.
   스크롤하던 정의역이 타일링된 월드 투영 UV이므로 일렁임이 재질의 *타일링*에 묶였고, 공기가
   아니라 텍스처를 따라 움직였습니다. 열두 번쯤 타일링된 바닥에서는 표면이 자기 밑으로
   미끄러지는 것으로 읽혔습니다.
   아지랑이는 용암의 표면이 아니라 눈과 용암 사이의 *공기*에 속하며, 곧 화면 공간 패스에
   속한다는 뜻입니다. 이제 post.c의 것이고, 이곳에 남은 것은 용암이 무엇인가, 즉 바위에 고정된
   지각입니다. */
"  const float PULSE = 0.45;\n"


"  float f = fbm(uv*1.7);\n"
/* Distance from the plate threshold: 0 in the middle of a crack, 1 well
   inside a plate. */
/* The window sits LOW so most of the surface is crust and the glow is the
   exception. Centred on the field's own mean the two came out roughly equal,
   which reads as open flow with rocks in it rather than as cooled rock with
   fire underneath -- and the second is what a lava floor you can occasionally
   cross should look like.
   대부분이 껍질이고 발광이 예외가 되도록 구간을 *낮게* 잡습니다. 필드 자체의 평균에
   맞추면 둘이 대략 비슷해지는데, 그러면 불 위에 식은 암석이 아니라 돌이 섞인 용암류처럼
   읽힙니다. 가끔 건널 수 있는 용암 바닥은 후자여야 합니다. */
/* The threshold breathes, so a spot opens and closes rather than merely
   drifting past. Half the pulse either side of the static window.
   임계값이 호흡하므로 한 지점이 단지 스쳐 지나가는 것이 아니라 열리고 닫힙니다. 정적인
   구간의 양쪽으로 맥동의 절반씩입니다. */
"  float pb = sin(uTime * PULSE) * 0.03;\n"
"  float crust = smoothstep(0.30 + pb, 0.42 + pb, f);\n"
/* A second, finer field breaks the plates up so they do not read as one
   continuous sheet with holes in it. */
"  crust *= 0.75 + 0.25*smoothstep(0.35, 0.65, fbm(uv*5.5));\n"
/* Grain sampled from the STATIC uv, not a drifting one: this is the surface of
   the rock itself, and rock does not flow. Drifting this too made the whole
   floor read as one scrolling texture rather than as plates moving on a melt.
   흐르는 좌표가 아니라 *정적인* uv에서 결을 샘플링합니다. 이것은 암석 자체의 표면이며
   암석은 흐르지 않습니다. 이것까지 흘리면 바닥 전체가 용융물 위에서 움직이는 판이 아니라
   하나의 스크롤하는 텍스처로 읽혔습니다. */
"  vec3 rock = vec3(0.10,0.075,0.075)*(0.7+0.6*n2(uv*26.0));\n"
/* The crack runs white-hot at its centre and falls to the base colour at its
   edges, which is what gives it depth rather than a flat orange. */
"  vec3 hot  = mix(vec3(1.9,1.35,0.45), base*2.2, smoothstep(0.0,0.5,crust));\n"
"  return mix(hot, rock, crust);\n"
"}\n"

/* --- A glazed facade, lit from inside ---------------------------------------
 *
 * ENGLISH
 * -------
 * A curtain wall: a grid of panes held in metal members, where SOME of the
 * rooms behind the glass have their lights on and the rest do not. The lit
 * ones are emissive; the dark ones are ordinary dark glass.
 *
 * The random lit/unlit draw IS the effect. A facade whose windows are all the
 * same reads as a texture no matter how well the glass itself is drawn,
 * because a real building's windows differ from each other and nothing else
 * about a facade does. Everything below is detail on top of that one idea.
 *
 * EMISSION HERE MEANS RETURNING ABOVE 1.0, the same way pLava does and for the
 * same reason: there is no separate emissive term in this shader. The world
 * pass computes c *= lum*tint with lum banded into five levels whose floor is
 * AMBIENT rounded to 0.25, so a lit pane has to come back bright enough to
 * survive being quartered. GLOW is set from that: base*GLOW at its dimmest
 * variation still lands above the post pass's BLOOM_KNEE of 0.25 after the
 * multiply, so a lit window blooms in a dark room -- which is the point -- and
 * clamps to a warm white in a bright one.
 *
 * The honest cost of doing it this way: a lit pane is still MULTIPLIED by the
 * room's light, so the same window is four times brighter under a bright band
 * than under a dark one. That is backwards for something that emits. It is
 * invisible on a facade, where every pane shares one wall's lighting and only
 * the differences between panes are read, and it is why this is a facade
 * material rather than a general "emissive" one.
 *
 * `base` IS THE LIT ROOM'S COLOUR, not the glass and not the frame -- the same
 * convention pLava uses for its cracks. The dark glass and the members supply
 * their own colours, because a recipe that could recolour those would let a
 * facade be authored that no light explains.
 *
 * The members are measured in UV UNITS rather than in cell fractions, unlike
 * pBrick's mortar. A cell here is half again as tall as it is wide, so
 * measuring in fractions would make the transoms 1.5x thicker than the
 * mullions -- and a window frame whose horizontal and vertical members differ
 * in thickness is the one part of this that the eye checks.
 *
 * Give this NO `bump`. It is emissive for the same reason pgrid is: a lit pane
 * is bright because light comes out of it, not because it faces a light, and
 * differencing its luminance would tilt the normal away from the glow.
 *
 * 한국어
 * ------
 * 유리 외벽입니다. 금속 창살에 끼워진 창유리 격자이며, 유리 뒤의 방 중 *일부*는 불이 켜져
 * 있고 나머지는 꺼져 있습니다. 켜진 쪽은 발광하고, 꺼진 쪽은 평범한 어두운 유리입니다.
 *
 * 켜짐과 꺼짐을 무작위로 뽑는 것이 효과의 *전부*입니다. 창이 모두 같은 외벽은 유리를 아무리
 * 잘 그려도 텍스처로 읽힙니다. 실제 건물의 창은 서로 다르고, 외벽의 다른 무엇도 그렇지 않기
 * 때문입니다. 아래의 모든 것은 그 하나의 발상 위에 얹은 세부입니다.
 *
 * *이곳의 발광은 1.0을 넘겨 반환하는 것*을 뜻하며, pLava와 같은 방식이고 같은 이유입니다.
 * 이 셰이더에는 별도의 발광 항이 없습니다. 월드 패스는 c *= lum*tint를 계산하고 lum은 다섯
 * 단계로 나뉘며 그 하한은 AMBIENT를 반올림한 0.25입니다. 따라서 불 켜진 창유리는 4분의 1로
 * 줄어들고도 살아남을 만큼 밝게 돌아와야 합니다. GLOW는 그 기준으로 정했습니다. 가장 어두운
 * 변화에서도 base*GLOW는 곱셈 이후 포스트 패스의 BLOOM_KNEE 0.25를 넘으므로, 어두운 방에서는
 * 불 켜진 창에 블룸이 생기고(이것이 핵심입니다) 밝은 방에서는 따뜻한 흰색으로 포화합니다.
 *
 * 이 방식의 정직한 대가: 불 켜진 창유리도 여전히 방의 빛에 *곱해지므로*, 같은 창이 밝은 단계
 * 아래에서 어두운 단계보다 네 배 밝습니다. 발광하는 것에게는 거꾸로 된 동작입니다. 모든 창이 한
 * 벽의 조명을 공유하고 창들 *사이의 차이*만 읽히는 외벽에서는 보이지 않으며, 이것이 이 재질이
 * 범용 "발광" 재질이 아니라 외벽 재질인 이유입니다.
 *
 * *`base`는 불 켜진 방의 색*이며 유리도 창틀도 아닙니다. pLava가 균열에 쓰는 것과 같은
 * 규약입니다. 어두운 유리와 창살은 자기 색을 스스로 정하는데, 그것들까지 다시 칠할 수 있는
 * 레시피는 어떤 빛으로도 설명되지 않는 외벽을 만들 수 있게 하기 때문입니다.
 *
 * 창살은 pBrick의 줄눈과 달리 셀 비율이 아니라 *UV 단위*로 잽니다. 이곳의 셀은 너비의 1.5배
 * 높이이므로 비율로 재면 가로재가 세로재보다 1.5배 두꺼워집니다. 그리고 가로재와 세로재의
 * 두께가 다른 창틀은 이 재질에서 눈이 실제로 확인하는 유일한 부분입니다.
 *
 * 여기에는 `bump`를 주지 *마십시오*. pgrid와 같은 이유로 발광합니다. 불 켜진 창유리가 밝은
 * 것은 빛을 향하고 있어서가 아니라 빛이 나오기 때문이며, 그 휘도를 차분하면 법선이 발광에서
 * 멀어지게 기울어집니다. */
"vec3 pWindow(vec2 uv, vec3 base){\n"
/* A glazing module, taller than it is wide. 1.5 lands on 48 texels at
   RD_PROC_TEXELS, so a row boundary falls on a texel edge rather than inside
   one -- a cell height that does not divide evenly leaves every other row of
   windows a pixel taller than its neighbour.
   유리 모듈이며 너비보다 높습니다. 1.5는 RD_PROC_TEXELS에서 48텍셀에 떨어지므로 행 경계가
   텍셀 안이 아니라 텍셀 가장자리에 놓입니다. 나누어떨어지지 않는 셀 높이는 창의 매 다른
   행을 이웃보다 한 픽셀씩 높게 남깁니다. */
"  const vec2  CELL   = vec2(1.0, 1.5);\n"
/* Two texels of metal each side of a pane, so neighbouring windows are parted
   by four. An exact texel multiple as well, for the reason above.
   창유리 양쪽에 두 텍셀씩의 금속이므로 이웃한 창은 네 텍셀로 나뉩니다. 위와 같은 이유로 이
   값도 텍셀의 정확한 배수입니다. */
"  const float MEMBER = 0.0625;\n"
"  const float SOFT   = 0.03125;\n"
/* Just under half the building is awake. Past half it reads as an office at
   six o'clock rather than a tower at midnight: the dark panes become the
   exception, and the eye starts reading THEM as the pattern.
   건물의 절반에 조금 못 미치는 만큼이 깨어 있습니다. 절반을 넘으면 자정의 탑이 아니라 여섯
   시의 사무실로 읽힙니다. 어두운 창이 예외가 되고, 눈이 *그쪽*을 패턴으로 읽기 시작합니다. */
"  const float AWAKE  = 0.45;\n"
"  const float GLOW   = 2.4;\n"
"  vec2 g = uv / CELL;\n"
"  vec2 id = floor(g), f = fract(g);\n"
/* Distance to the nearest cell edge, carried back into UV units. */
"  vec2 e = (0.5 - abs(f - 0.5)) * CELL;\n"
"  float pane = smoothstep(MEMBER, MEMBER + SOFT, min(e.x, e.y));\n"
/* One hash per window, offset off the origin: h21 is sin-based and h21(0,0) is
   exactly 0, which would make the window at the UV origin reliably dark.
   창마다 해시 하나이며 원점에서 비켜 둡니다. h21은 sin 기반이라 h21(0,0)이 정확히 0이고,
   그러면 UV 원점의 창이 언제나 어둡게 나옵니다. */
"  float r = h21(id + 0.5);\n"
/* --- the dark glass: a reflection, not a colour --------------------------
   The unlit panes take a slow fbm sampled across the WHOLE facade rather than
   per cell, so the reflection runs continuously over the members the way a
   reflection in real glass does. Per-cell noise would give every pane its own
   independent shade and read as dirty glass instead.
   어두운 유리는 색이 아니라 반사입니다. 꺼진 창은 셀 단위가 아니라 외벽 *전체*에 걸친 느린
   fbm을 취하므로, 실제 유리의 반사처럼 창살을 가로질러 이어집니다. 셀 단위 노이즈는 창마다
   독립된 명암을 주어 더러운 유리로 읽힙니다. */
"  vec3 sky  = vec3(0.13,0.16,0.23) * (0.35 + 1.30*fbm(uv*0.30));\n"
/* Brighter toward the top of each pane: on a wall planar_uv puts world y into
   v, so f.y runs upward, and glass catches more sky the higher up you look.
   각 창유리의 위쪽이 더 밝습니다. 벽에서 planar_uv는 월드 y를 v에 넣으므로 f.y가 위로
   가고, 유리는 위를 볼수록 하늘을 더 많이 받습니다. */
"  vec3 dark = sky * (0.70 + 0.60*f.y);\n"
/* --- the lit rooms -------------------------------------------------------
   Three more draws off the one hash, spread by primes. Calling h21 again would
   be another sin() each time; a lit window only needs its three choices to be
   uncorrelated WITH EACH OTHER across the lit range, which multiplying by a
   prime and taking the fraction already achieves.
   하나의 해시에서 소수를 곱해 세 개의 뽑기를 더 얻습니다. h21을 다시 부르면 그때마다 sin()이
   한 번 더입니다. 불 켜진 창은 세 선택이 켜진 구간에서 *서로* 무관하기만 하면 되며, 소수를
   곱해 소수부를 취하는 것으로 이미 충분합니다. */
"  vec3 tint = mix(vec3(1.00,0.82,0.52), vec3(0.68,0.86,1.00), step(0.80, fract(r*17.0)));\n"
"  vec3 room = base * GLOW * tint * (0.70 + 0.55*fract(r*29.0));\n"
/* Light falls off toward the floor of the room, as a GRADIENT rather than the
   hard band this first was. The blind below already cuts a pane with one hard
   horizontal line, and a second one at a fixed height turned every window into
   a little landscape with a horizon in it -- two hard lines per pane is one
   more than a window has.
   방의 바닥 쪽으로 갈수록 빛이 약해지며, 처음의 단단한 밴드가 아니라 *그러데이션*입니다.
   아래의 블라인드가 이미 창유리를 가로선 하나로 자르는데, 고정된 높이에 두 번째 선을 더하니
   창마다 지평선이 있는 작은 풍경이 되었습니다. 창유리당 두 개의 뚜렷한 가로선은 창이 가진
   것보다 하나 많습니다. */
"  room *= 0.78 + 0.36*f.y;\n"
/* A blind, pulled down from the top over some of the lit windows. The min
   leaves most of them uncovered: the expression reaches past 1.0 for the
   larger part of the draw, and a cut at or above 1.0 covers nothing.
   불 켜진 창 일부에 위에서 내려온 블라인드입니다. min이 대부분을 덮이지 않게 둡니다. 이 식은
   뽑기의 큰 쪽에서 1.0을 넘어가고, 1.0 이상의 경계는 아무것도 덮지 않습니다. */
"  float cut = min(1.0, 0.62 + 0.62*fract(r*53.0));\n"
/* The blind is LIT FROM BEHIND rather than dark: a drawn blind with a lamp on
   the far side of it glows, and cutting to the dark-glass colour instead would
   read as the top of the window being a different window.
   블라인드는 어둡지 않고 *뒤에서 빛을 받습니다*. 내린 블라인드 너머에 등이 켜져 있으면
   블라인드가 빛나며, 대신 어두운 유리 색으로 자르면 창의 윗부분이 다른 창인 것처럼 읽힙니다. */
"  vec3 inner = mix(room*0.26, room, step(f.y, cut));\n"
"  vec3 glass = mix(dark, inner, step(1.0 - AWAKE, r));\n"
/* The members stay dark and NEVER emit, which is what gives a lit pane an edge
   to be bright against.
   창살은 어둡게 남고 결코 발광하지 않으며, 그것이 불 켜진 창유리가 밝게 대비될 가장자리를
   만듭니다. */
"  vec3 member = vec3(0.15,0.16,0.18) * (0.88 + 0.24*n2(uv*24.0));\n"
"  return mix(member, glass, pane);\n"
"}\n"

/* --- Tangent space, derived rather than stored ------------------------------
 *
 * ENGLISH
 * -------
 * A normal map is expressed in TANGENT space -- x along the surface's u axis,
 * y along its v axis, z out of the surface -- so applying one needs to know
 * which way u and v point in the world. The usual answer is a per-vertex
 * tangent, computed at build time and stored alongside the normal.
 *
 * That is not needed here, and storing it would be a real cost: Vtx is 32
 * bytes and a tangent would make it 44, on every vertex of every mesh, for a
 * value that is already implied. This project's world UVs come from
 * planar_uv() -- a dominant-axis projection, where a surface facing mostly up
 * takes its u from world x and its v from world z, and so on. The mapping from
 * normal to (u,v) axes is a fixed rule with three cases, so the tangent frame
 * can simply be recomputed from the normal in the shader.
 *
 * Deriving it also means the frame cannot go stale. A stored tangent has to be
 * regenerated whenever geometry is rebuilt -- which happens on every level
 * load, every hot reload and every frame of a drag in the editor -- and a
 * tangent that disagrees with its UVs lights the surface as though the texture
 * were rotated, with nothing to say why.
 *
 * The three cases MUST match planar_uv exactly. If they disagree the map is
 * applied along the wrong axis and the surface lights as though lit from
 * somewhere it is not -- which reads as a lighting bug rather than as a
 * mapping one, and is why the two are commented as a pair.
 *
 * 한국어
 * ------
 * 노멀 맵은 *탄젠트* 공간으로 표현됩니다. x는 표면의 u축, y는 v축, z는 표면 바깥
 * 방향입니다. 따라서 노멀 맵을 적용하려면 u와 v가 월드에서 어느 방향인지 알아야 합니다.
 * 통상적인 답은 빌드 시점에 계산해 법선과 함께 저장하는 정점별 탄젠트입니다.
 *
 * 이곳에서는 그것이 필요 없으며, 저장하면 실제 비용이 듭니다. Vtx는 32바이트인데
 * 탄젠트를 넣으면 44바이트가 되고, 그것도 모든 메시의 모든 정점에 대해, 이미 함축되어
 * 있는 값 때문에 그렇게 됩니다. 이 프로젝트의 월드 UV는 planar_uv()에서 나옵니다.
 * 지배적 축 투영이며, 주로 위를 향하는 표면은 u를 월드 x에서, v를 월드 z에서 가져오는
 * 식입니다. 법선에서 (u,v) 축으로 가는 매핑이 경우가 셋뿐인 고정된 규칙이므로, 탄젠트
 * 프레임은 셰이더에서 법선으로부터 다시 계산하기만 하면 됩니다.
 *
 * 유도하면 프레임이 낡을 수도 없습니다. 저장된 탄젠트는 지오메트리가 재생성될 때마다
 * 다시 만들어야 하는데, 그것은 레벨 로드마다, 핫 리로드마다, 에디터의 드래그 프레임마다
 * 일어납니다. 그리고 UV와 어긋난 탄젠트는 텍스처가 회전한 것처럼 표면을 조명하며, 그
 * 이유를 알려 주는 것은 아무것도 없습니다.
 *
 * 세 경우는 planar_uv와 *정확히* 일치해야 합니다. 어긋나면 맵이 잘못된 축으로 적용되어
 * 표면이 실제와 다른 곳에서 빛을 받는 것처럼 조명됩니다. 이는 매핑 버그가 아니라 조명
 * 버그로 읽히며, 그래서 둘을 한 쌍으로 주석 처리했습니다. */
"void tangentFrame(vec3 n, out vec3 T, out vec3 B){\n"
"  vec3 a = abs(n);\n"
/* Mirrors planar_uv() in render.c. Keep the three branches in step.
   render.c의 planar_uv()를 반영합니다. 세 분기를 동기화된 상태로 유지하십시오. */
"  if(a.y > a.x && a.y > a.z){ T = vec3(1.0,0.0,0.0); B = vec3(0.0,0.0,1.0); }\n"
"  else if(a.x > a.z)        { T = vec3(0.0,0.0,1.0); B = vec3(0.0,1.0,0.0); }\n"
"  else                      { T = vec3(1.0,0.0,0.0); B = vec3(0.0,1.0,0.0); }\n"
"}\n"

/* --- Texel snap: what makes a computed material a PIXEL material -----------
 *
 * ENGLISH
 * -------
 * The pattern functions above are continuous -- feed them a UV a millionth of
 * a unit apart and they return two slightly different colours. That is what
 * gave them their infinite resolution, and it is exactly what a pixel-art
 * presentation cannot use: a brick wall that keeps resolving finer as the
 * player walks toward it never looks like it was drawn at the same scale as
 * the rest of the game.
 *
 * Snapping the UV to a grid before evaluation makes the pattern hold one
 * colour across each cell, which is what a texel IS. The pattern code itself
 * is untouched and does not know this happened -- it is still asked for the
 * colour at a point, just never at a point between two texel centres.
 *
 * The half-texel offset samples each cell's CENTRE rather than its corner.
 * Sampling the corner puts the sample exactly on the boundary between four
 * cells, which is where every one of these patterns has its discontinuities:
 * pBrick's mortar joints, pTile's grout and pPanel's seams all land on integer
 * boundaries by construction, so a corner sample sits precisely on the seam
 * and picks up mortar colour across the entire surface. Measured on brick, the
 * corner version came out uniformly grey -- the pattern was still there, but
 * every sample landed in a joint.
 *
 * 한국어
 * ------
 * 위의 패턴 함수들은 연속적입니다. 백만분의 1 단위만큼 떨어진 UV를 주면 미세하게 다른 두
 * 색을 반환합니다. 그것이 무한한 해상도를 준 요인이며, 동시에 픽셀 아트 표현이 쓸 수 없는
 * 바로 그 성질입니다. 플레이어가 다가갈수록 계속 더 세밀해지는 벽돌 벽은 게임의 나머지와
 * 같은 크기로 그려진 것처럼 결코 보이지 않습니다.
 *
 * 계산 전에 UV를 격자에 맞추면 패턴이 각 셀 전체에 하나의 색을 유지하는데, 그것이 곧
 * 텍셀입니다. 패턴 코드 자체는 그대로이며 이 일이 일어났다는 것을 모릅니다. 여전히 한
 * 지점의 색을 요청받을 뿐이고, 다만 두 텍셀 중심 사이의 지점에서는 결코 요청받지
 * 않습니다.
 *
 * 텍셀 절반의 오프셋은 셀의 모서리가 아니라 *중심*을 샘플링합니다. 모서리를 샘플링하면
 * 네 셀의 경계에 정확히 놓이는데, 이 패턴들 모두가 바로 그곳에 불연속을 갖습니다.
 * pBrick의 줄눈, pTile의 메지, pPanel의 이음매가 전부 구조상 정수 경계에 놓이므로,
 * 모서리 샘플은 정확히 이음매 위에 앉아 표면 전체에서 줄눈 색을 집어 옵니다. 벽돌에서
 * 측정한 결과 모서리 방식은 균일한 회색으로 나왔습니다. 패턴은 여전히 있었지만 모든
 * 샘플이 줄눈에 떨어진 것입니다. */
"vec2 texelSnap(vec2 uv){\n"
"  const float T = " RD_STR(RD_PROC_TEXELS) ";\n"
/* T of 0 disables the snap and restores the continuous behaviour. */
"  if(T <= 0.0) return uv;\n"
"  return (floor(uv * T) + 0.5) / T;\n"
"}\n"

"vec3 procColour(int id, vec2 uv, vec3 base){\n"
/* Every pattern sees a snapped UV, so none of them has to opt in and none can
   be forgotten. A pattern added later is pixelated for free.
   모든 패턴이 스냅된 UV를 받으므로 각각이 따로 참여할 필요가 없고 빠뜨릴 수도 없습니다.
   나중에 추가되는 패턴도 비용 없이 픽셀화됩니다. */
"  uv = texelSnap(uv);\n"
"  if(id==1) return pBrick(uv, base);\n"
"  if(id==2) return pTile(uv, base);\n"
"  if(id==3) return pPanel(uv, base);\n"
"  if(id==4) return pWood(uv, base);\n"
"  if(id==5) return pHex(uv, base);\n"
"  if(id==6) return pMarble(uv, base);\n"
"  if(id==7) return pRust(uv, base);\n"
"  if(id==8) return pGrid(uv, base);\n"
"  if(id==9) return pLava(uv, base);\n"
"  return pWindow(uv, base);\n"
"}\n"

/* --- The normal map itself, computed rather than sampled --------------------
 *
 * ENGLISH
 * -------
 * There is no normal-map TEXTURE. Storing one would cost what the whole
 * project is built to avoid -- a second 256KB image per material -- and the
 * procedural materials have no texture to pair it with in the first place.
 *
 * Instead the height is the material's own luminance, and the normal is its
 * gradient. That works because these patterns already encode their relief in
 * their brightness: pBrick's mortar joints are darker than its bricks, pPanel's
 * seams are darker than its plates, pHex's lattice lines are darker than its
 * cells. Sampling the pattern either side of a texel and differencing gives
 * the slope directly, which is what a normal map stores.
 *
 * Sobel would be the textbook filter and is not used: it needs eight taps
 * where forward differencing needs two, and at RD_PROC_TEXELS the pattern is
 * quantised into flat cells anyway -- the extra taps land inside the same
 * texel and return the same value, so seven of the eight are wasted.
 *
 * The offset is exactly one texel. Smaller and both taps land in the same
 * quantised cell and the gradient is always zero; larger and the relief
 * detaches from the pattern that produced it.
 *
 * 한국어
 * ------
 * 노멀 맵 *텍스처*가 없습니다. 저장하면 이 프로젝트 전체가 피하려고 만들어진 비용,
 * 즉 재질마다 두 번째 256KB 이미지를 치르게 되며, 애초에 절차적 재질에는 짝지을
 * 텍스처 자체가 없습니다.
 *
 * 대신 높이는 재질 자신의 휘도이고, 법선은 그 기울기입니다. 이 패턴들이 이미 밝기로
 * 요철을 표현하고 있기에 가능합니다. pBrick의 줄눈은 벽돌보다 어둡고, pPanel의 이음매는
 * 판보다 어두우며, pHex의 격자선은 셀보다 어둡습니다. 텍셀 양쪽에서 패턴을 샘플링해
 * 차분하면 기울기가 바로 나오는데, 그것이 노멀 맵이 저장하는 값입니다.
 *
 * 교과서적인 필터인 소벨은 쓰지 않습니다. 전방 차분이 2회면 되는 곳에 8회가 필요하고,
 * RD_PROC_TEXELS에서 패턴은 어차피 평평한 셀로 양자화되어 있습니다. 추가 샘플이 같은
 * 텍셀 안에 떨어져 같은 값을 반환하므로 여덟 중 일곱이 낭비입니다.
 *
 * 오프셋은 정확히 텍셀 하나입니다. 더 작으면 두 샘플이 같은 양자화 셀에 떨어져 기울기가
 * 항상 0이 되고, 더 크면 요철이 그것을 만들어 낸 패턴에서 분리됩니다. */
"float procHeight(int id, vec2 uv, vec3 base){\n"
"  vec3 c = procColour(id, uv, base);\n"
"  return dot(c, vec3(0.299, 0.587, 0.114));\n"
"}\n"

/* The gain the recipe's 0..1 strength is multiplied by.
 *
 * ENGLISH
 * -------
 * A luminance difference across a texel is a small number -- a mortar joint is
 * perhaps 0.3 darker than the brick beside it -- and using it directly as a
 * slope tilts the normal by a couple of degrees. That is the correct physical
 * scale and it is invisible here, because the world pass quantises
 * illumination into LIGHT_BANDS levels before anything is drawn. A band is
 * 1/(5-1) = 0.25 of the luminance range, so a perturbation has to move the
 * shading by a QUARTER of full brightness to change even one pixel; measured
 * against the key light on a wall, that needs roughly a 30 degree tilt. At the
 * physical scale the relief was computed correctly, quantised away, and 167 of
 * 20000 pixels differed -- the feature was working and could not be seen.
 *
 * So the slope is amplified. This is not a fudge factor hiding a bug: the
 * banding is a deliberate part of the look, and any surface detail that wants
 * to survive it has to be exaggerated to the same degree the lighting is
 * simplified. The same reasoning already sets NOISE_AMOUNT to 0.20 rather than
 * the 0.055 that was tried first.
 *
 * 한국어
 * ------
 * 텍셀 간 휘도 차이는 작은 값입니다. 줄눈은 옆의 벽돌보다 0.3 정도 어두울 뿐이며, 그것을
 * 기울기로 그대로 쓰면 법선이 2~3도 기울어집니다. 그것이 물리적으로 올바른 크기이고
 * 이곳에서는 보이지 않습니다. 월드 패스가 무엇을 그리기도 전에 조도를 LIGHT_BANDS
 * 단계로 양자화하기 때문입니다. 밴드 하나가 휘도 범위의 1/(5-1) = 0.25이므로, 교란이
 * 픽셀 하나라도 바꾸려면 음영을 전체 밝기의 *4분의 1*만큼 움직여야 합니다. 벽에 닿는
 * 주광 기준으로 측정하면 약 30도의 기울기가 필요합니다. 물리적 크기에서는 요철이
 * 올바르게 계산된 뒤 양자화로 사라졌고, 20000픽셀 중 167개만 달랐습니다. 기능은
 * 동작했지만 볼 수 없었습니다.
 *
 * 그래서 기울기를 증폭합니다. 이는 버그를 감추는 임시방편이 아닙니다. 밴딩은 이 룩의
 * 의도된 일부이며, 그것을 견디려는 모든 표면 디테일은 조명이 단순화된 만큼 과장되어야
 * 합니다. NOISE_AMOUNT를 처음 시도한 0.055가 아니라 0.20으로 정한 것과 동일한
 * 논리입니다. */
"const float BUMP_GAIN = 14.0;\n"

"vec3 procNormal(int id, vec2 uv, vec3 base, vec3 n, float strength){\n"
"  if(strength <= 0.0) return n;\n"
"  const float T = " RD_STR(RD_PROC_TEXELS) ";\n"
"  float step = (T > 0.0) ? (1.0 / T) : 0.01;\n"
"  strength *= BUMP_GAIN;\n"

"  float h  = procHeight(id, uv, base);\n"
"  float hu = procHeight(id, uv + vec2(step, 0.0), base);\n"
"  float hv = procHeight(id, uv + vec2(0.0, step), base);\n"

/* The gradient points UPHILL, so the perturbation is its negative: a surface
   that gets brighter along +u is sloping away from the light in that
   direction, and its normal must lean back toward -u.
   기울기는 오르막을 가리키므로 교란은 그 반대 부호입니다. +u 방향으로 밝아지는 표면은
   그 방향으로 빛에서 멀어지며 기울어 있고, 그 법선은 -u 쪽으로 기울어야 합니다. */
"  vec3 T3, B3;\n"
"  tangentFrame(n, T3, B3);\n"
"  vec3 p = n - (T3 * (hu - h) + B3 * (hv - h)) * strength;\n"
"  return normalize(p);\n"
"}\n";

static const char *FS_SRC =
"#version 330 core\n"
"in vec3 vPos; in vec3 vNrm; in vec2 vUV; in vec3 vLit;\n"
"noperspective in vec2 vUVa;\n"
/* 0 is the perspective-correct texturing every other engine does; 1 is the
 * PlayStation's. A uniform rather than a constant for the reason uSnap is one:
 * the two can be compared side by side while the game runs, which is most of
 * how a value like this gets chosen.
 * 0은 다른 모든 엔진이 하는 원근 보정 텍스처링이고 1은 플레이스테이션의 것입니다. 상수가
 * 아니라 유니폼인 이유는 uSnap이 그런 이유와 같습니다. 게임이 도는 동안 둘을 나란히 비교할 수
 * 있으며, 이런 값이 정해지는 방법의 대부분이 그것입니다. */
"uniform float uAffine;\n"
/* How far the texture may slide, in UV units -- which are TILES here.
 *
 * THE BUG THIS FIXES. Affine error is proportional to the UV RANGE across a
 * triangle, and this project's UVs are world-space projections: brush_face_uv
 * divides map units by ::BRUSH_TEXELS, so a 512-unit wall spans two tiles and
 * a large room spans a dozen or more. A face mapped 0..1 warped subtly and a face mapped
 * 0..16 warped sixteen times as hard, off the same uAffine -- so the materials
 * that tile most were exactly the ones that came apart. That is not a period
 * artifact, it is a texture that has stopped being attached to its surface.
 *
 * Bounding the DEVIATION rather than scaling it makes the warp an absolute
 * slide instead of a proportional one: every face slides by up to this much,
 * whether it tiles once or twenty times. That is also closer to what the
 * hardware did -- its polygons were small, so its UV ranges were small, and the
 * error it produced was small in absolute terms rather than relative ones.
 *
 * Saturates smoothly (d*L/(L+|d|)) rather than clamping. A clamp would put a
 * hard crease across a face at the point it engaged, which trades one visible
 * fault for another; this is near-identity for small deviations and eases into
 * the limit for large ones.
 *
 * 텍스처가 미끄러질 수 있는 최대치이며 단위는 UV, 즉 이곳에서는 *타일*입니다.
 *
 * *이것이 고치는 결함.* 어파인 오차는 삼각형을 가로지르는 UV *범위*에 비례하는데, 이
 * 프로젝트의 UV는 월드 공간 투영입니다. brush_face_uv가 맵 단위를 ::BRUSH_TEXELS로 나누므로
 * 512유닛 벽은 2타일, 큰 방은 열 몇 타일에 걸칩니다. 0..1로 매핑된 면은 미묘하게 일그러지고 0..16으로
 * 매핑된 면은 같은 uAffine에서 열여섯 배로 일그러졌습니다. 그래서 가장 많이 타일링되는 재질이
 * 정확히 무너지는 재질이었습니다. 그것은 시대의 아티팩트가 아니라 표면에 붙어 있기를 그만둔
 * 텍스처입니다.
 *
 * 비율로 조정하지 않고 *편차*를 제한하면 왜곡이 비례가 아니라 절대적인 미끄러짐이 됩니다. 한
 * 번 타일링되든 스무 번 타일링되든 모든 면이 이만큼까지만 미끄러집니다. 이것이 실제 하드웨어에
 * 더 가깝기도 합니다. 그 폴리곤은 작았으므로 UV 범위도 작았고, 만들어 내는 오차도 상대적이
 * 아니라 절대적으로 작았습니다.
 *
 * 잘라 내지 않고 부드럽게 포화시킵니다(d*L/(L+|d|)). 잘라 내면 그것이 걸리는 지점에서 면을
 * 가로지르는 뚜렷한 접힘이 생겨, 눈에 띄는 결함 하나를 다른 하나와 바꾸는 셈입니다. 이 식은
 * 작은 편차에서는 거의 항등이고 큰 편차에서는 한계로 완만히 들어갑니다. */
"const float AFF_LIMIT = 0.35;\n"
"uniform sampler2D uTex;\n"
"uniform vec3 uEye;\n"
"uniform int uMode;\n"
"uniform vec4 uColor;\n"
/* Procedural surface selection: which shader, its base colour, how many
   pattern cells per world unit, and a spare triple for per-material tweaks. */
"uniform int uProc;\n"
"uniform vec3 uPCol;\n"
"uniform float uPScale;\n"
/* Seconds, for the one material that moves. See rd_time in render.h.
   움직이는 유일한 재질을 위한 초 단위 시간입니다. render.h의 rd_time을 참조하십시오. */
"uniform float uTime;\n"
"uniform vec3 uPParam;\n"
/* Point lights. Packed as two arrays rather than a struct array: a struct of
   vec3+float pads to two vec4s per element on some drivers, and the uniform
   count matters more here than the tidiness.
   점광원입니다. 구조체 배열이 아니라 두 개의 배열로 담습니다. vec3+float 구조체는 일부
   드라이버에서 원소당 vec4 두 개로 패딩되며, 이곳에서는 정돈됨보다 유니폼 개수가 더
   중요합니다.
     uLightPos.xyz = world position, .w = radius
     uLightCol.rgb = colour,         .a = power */
"uniform int  uNumLights;\n"
"uniform vec4 uLightPos[" RD_STR(RD_MAX_LIGHTS) "];\n"
"uniform vec4 uLightCol[" RD_STR(RD_MAX_LIGHTS) "];\n"
/* How much of a moving light a billboard takes, against what the world takes.
 *
 * BELOW ONE ON PURPOSE. The world's lighting multiplies by a Lambert term that
 * averages well under half over a surface facing every which way; a sprite has
 * no usable normal and skips that term entirely (see uMode==5), so at equal
 * power it would come out brighter than the wall behind it and read as
 * self-illuminated. This is the factor that puts the two back on the same
 * footing, chosen by eye against a muzzle flash at arm's length.
 *
 * 빌보드가 움직이는 빛을 얼마나 받는지, 월드가 받는 것에 대한 비율입니다.
 *
 * *의도적으로 1보다 작습니다.* 월드의 조명은 온갖 방향을 향한 표면 전체에 대해 평균이 0.5에
 * 한참 못 미치는 램버트 항을 곱하지만, 스프라이트는 쓸 만한 법선이 없어 그 항을 통째로
 * 건너뜁니다(uMode==5 참조). 같은 세기라면 뒤의 벽보다 밝게 나와 스스로 빛나는 것처럼
 * 읽힙니다. 이 계수가 둘을 같은 기준에 되돌려 놓으며, 팔 길이 거리의 총구 섬광에 맞춰 눈으로
 * 골랐습니다. */
"const float SPRITE_LIGHT = 0.55;\n"
"out vec4 oCol;\n";

/* Split here so FS_PROC can be spliced in between without any string
   concatenation at runtime -- glShaderSource takes an array of pieces. */
/* HOW FAR YOU CAN SEE, and it is this rather than the far plane.
 *
 * scene.c projects to a far plane of 200m, which sounds like the sight range
 * and is not: the fog below reaches FULL at FOG_FAR, so everything past it is
 * the fog colour whatever the projection allows. The arena is 82m across, so
 * the far plane has never been what a player runs into.
 *
 * DOUBLED FROM 30. At 30 a room 82m across was mostly fog, and the fog is
 * mixed toward a FIXED colour -- vec3(0.05,0.06,0.09), luminance 0.067 -- which
 * is brighter than a dark wall under ambient alone (albedo 0.126 x AMBIENT 0.32
 * = 0.040). So distance was LIFTING the far half of the room toward a flat
 * navy instead of dropping it into shadow, and the banded light it was washing
 * out is the whole mechanism by which a lamp reads as a pool. 60 puts the
 * arena's far wall at f=0.7 of the ramp rather than past its end.
 *
 * ONE CONSTANT, TWO USERS. The world branch and the sprite branch both fog,
 * and they have to agree: a monster fogged on a different curve from the wall
 * behind it separates from the wall as it walks away, which is exactly the
 * failure RD_SPRITE's own note describes from the other direction.
 *
 * *얼마나 멀리 보이는가*이며, 원거리 평면이 아니라 이것입니다.
 *
 * scene.c는 200m 원거리 평면으로 투영하며, 그것은 시야 거리처럼 들리지만 아닙니다. 아래의
 * 안개가 FOG_FAR에서 *가득* 차므로, 그 너머는 투영이 무엇을 허락하든 안개 색입니다. 아레나는
 * 82m 폭이므로 원거리 평면은 플레이어가 부딪히는 것이었던 적이 없습니다.
 *
 * *30에서 두 배로.* 30에서는 82m짜리 방의 대부분이 안개였고, 안개는 *고정된* 색
 * vec3(0.05,0.06,0.09)로 섞입니다. 그 휘도 0.067은 주변광만 받는 어두운 벽(알베도 0.126 x
 * AMBIENT 0.32 = 0.040)보다 *밝습니다.* 그래서 거리는 방의 먼 절반을 그림자로 떨어뜨리는
 * 대신 평평한 남색으로 *들어 올리고* 있었고, 그것이 씻어 내던 계단화된 빛이야말로 등이
 * 웅덩이로 읽히게 하는 기법 전부입니다.
 *
 * *상수 하나, 사용처 둘.* 월드 갈래와 스프라이트 갈래가 모두 안개를 칠하며 둘은 일치해야
 * 합니다. 뒤의 벽과 다른 곡선으로 안개가 낀 몬스터는 멀어질수록 벽에서 떨어져 나옵니다. */
static const char *FS_FOG =
"const float FOG_FAR = 60.0;\n"
"vec3 fogged(vec3 c, vec3 p, vec3 eye){\n"
"  float f=clamp(length(p-eye)/FOG_FAR,0.0,1.0);\n"
"  return mix(c, vec3(0.05,0.06,0.09), f*f);\n"
"}\n";

static const char *FS_MAIN =
/* --- a surface that moves ------------------------------------------------
 *
 * ENGLISH
 * -------
 * TWO EFFECTS OFF ONE WAVE FIELD, and they are one function because they must
 * agree: the texture drifts where the surface tilts, so a crest is a place the
 * crust is being carried rather than a place it is merely brighter. Driven by
 * WORLD POSITION rather than by UV, so the field is continuous across faces
 * that were mapped independently and does not stretch with the texture scale --
 * a lava sea assembled from several brushes swells as one sea.
 *
 * THE NORMAL IS THE HALF THAT SHOWS. Displacing the geometry is the obvious
 * way to make a liquid slosh and it is the wrong one here: the lava brush IS
 * the floor, collision is its flat top, and ::LVL_HAZARD_UNDERFOOT is five
 * centimetres -- so any displacement honest enough not to lie about where the
 * surface is would be too small to see from standing height. What this renderer
 * has instead is ::LIGHT_BANDS: five levels, so a normal rocked by a couple of
 * degrees walks the shading across a band edge and back. The band boundary
 * moving IS the swell, and it is visible across a whole sea from the far side
 * of the room -- which four centimetres of real geometry would not have been.
 *
 * @note Gated on RD_WORLD as well as on the material, because `uPParam`
 *       persists: ::rd_proc is called from ::tex_use and nothing clears it
 *       between passes, so a sprite drawn after the lava would inherit its
 *       flow and ripple.
 *
 * 한국어
 * ------
 * *하나의 파동장에서 두 효과가 나오며*, 둘이 한 함수인 이유는 서로 일치해야 하기 때문입니다.
 * 표면이 기우는 곳에서 텍스처가 흐르므로, 마루는 단지 더 밝은 곳이 아니라 껍질이 실려 가는
 * 곳입니다. UV가 아니라 *월드 좌표*로 구동되므로 독립적으로 매핑된 면들을 가로질러 장이
 * 연속이고 텍스처 배율에 따라 늘어나지 않습니다. 여러 브러시로 이루어진 용암 바다가 하나의
 * 바다로 출렁입니다.
 *
 * *보이는 쪽은 노멀입니다.* 지오메트리를 변위시키는 것이 액체를 출렁이게 하는 뻔한 방법이지만
 * 이곳에서는 틀린 방법입니다. 용암 브러시가 *곧 바닥*이고 충돌은 그 평평한 윗면이며
 * ::LVL_HAZARD_UNDERFOOT는 5센티미터입니다. 표면이 어디인지에 대해 거짓말하지 않을 만큼
 * 정직한 변위는 서 있는 눈높이에서 보이기에는 너무 작습니다. 대신 이 렌더러가 가진 것은
 * ::LIGHT_BANDS입니다. 다섯 단계이므로 몇 도 흔들린 노멀이 음영을 밴드 경계 너머로 걸어
 * 보냈다가 되돌립니다. *밴드 경계가 움직이는 것이 곧 출렁임*이며, 방 반대편에서 바다 전체에
 * 걸쳐 보입니다. 실제 지오메트리 4센티미터로는 그러지 못했을 것입니다.
 *
 * @note 재질뿐 아니라 RD_WORLD에도 게이트가 걸려 있습니다. `uPParam`이 남기 때문입니다.
 *       ::rd_proc는 ::tex_use에서 호출되고 패스 사이에 아무도 지우지 않으므로, 용암 뒤에
 *       그려지는 스프라이트가 그 흐름을 물려받아 일렁이게 됩니다. */
"vec2 flowField(vec3 p, float t){\n"
/* Two crossing waves at frequencies that do not divide each other, so the
   pattern does not repeat on a grid the eye can find. Returned as the GRADIENT
   rather than as a height -- the height is never wanted, only which way the
   surface leans. */
"  float a = cos(p.x*0.85 + t*1.30)*0.85 + cos((p.x+p.z)*0.42 + t*0.70)*0.42;\n"
"  float b = cos(p.z*0.73 - t*1.10)*0.73 + cos((p.x+p.z)*0.42 + t*0.70)*0.42;\n"
"  return vec2(a,b);\n"
"}\n"
"void main(){\n"
"  if(uMode==2){ oCol=uColor; return; }\n"
/* Text: one white atlas serves every colour, because the glyph lives in
   alpha and the colour comes from the uniform. */
"  if(uMode==3){ oCol=vec4(uColor.rgb, uColor.a*texture(uTex,vUV).a); return; }\n"
"  vec3 n=normalize(vNrm);\n"
/* The swell, applied before anything reads `n`. uPParam.z is the material's
   flow speed and doubles as the switch: every material that never says
   `flow` leaves this untouched. The 0.06 is the tilt in radians-ish -- small
   enough that a face still reads as flat, large enough to cross a band.
   출렁임이며, `n`을 읽는 그 무엇보다도 먼저 적용됩니다. uPParam.z는 재질의 흐름 속도이자
   스위치를 겸합니다. `flow`를 말하지 않는 모든 재질은 이것을 건드리지 않습니다. 0.06은
   기울기이며, 면이 여전히 평평하게 읽힐 만큼 작고 밴드 하나를 넘을 만큼 큽니다. */
"  if(uMode==0 && uPParam.z>0.0){\n"
"    vec2 g=flowField(vPos, uTime*uPParam.z);\n"
"    n=normalize(n+vec3(g.x,0.0,g.y)*0.060);\n"
"  }\n"
/* UVs now arrive per vertex, so extruded silhouettes can carry a real
   parameterisation instead of everything being guessed from world position. */
/* WORLD GEOMETRY ONLY, and the gate is here rather than left to the caller
   because the other modes are not merely uninteresting -- they are wrong.
   uMode==1 and uMode==6 are the view model, which is part of the FRAME the way
   the HUD is: it sits inches from the eye, fills a quarter of the screen and is
   hand-drawn, so warping it reads as the gun melting rather than as a period.
   uMode==5 is a billboard, generated to face the camera, so `w` is very nearly
   constant across it and the two interpolations already agree -- applying the
   mix there buys nothing and leaves a surprise for the one frame that tilts a
   sprite, which the death camera's roll does. uMode==4 is the editor's swatch,
   which exists to show a material exactly as it is.

   scene.c ALSO sets uAffine to 0 around the UI passes, and that is not
   redundant with this: that call says "this section of the frame does not
   warp", this says "within a section that does, only the level does". Two
   granularities, and losing either one loses a different thing.

   *월드 지오메트리 전용*이며, 게이트를 호출자에게 맡기지 않고 이곳에 두는 이유는 다른 모드가
   단지 무의미한 것이 아니라 *틀리기* 때문입니다. uMode==1과 uMode==6은 뷰 모델이며 HUD와 같은
   의미에서 *프레임*의 일부입니다. 눈에서 몇 센티미터 거리에 있고 화면의 4분의 1을 채우며 손으로
   그린 것이므로, 그것을 일그러뜨리면 시대가 아니라 총이 녹아내리는 것으로 읽힙니다. uMode==5는
   카메라를 향하도록 생성된 빌보드이므로 `w`가 거의 일정하고 두 보간이 이미 일치합니다. 그곳에
   섞기를 적용해도 얻는 것이 없으며, 스프라이트가 기울어지는 한 프레임(사망 카메라의 회전이 그렇게
   합니다)에 놀라움만 남깁니다. uMode==4는 재질을 있는 그대로 보여 주려고 존재하는 에디터
   스와치입니다.

   scene.c도 UI 패스 주변에서 uAffine을 0으로 설정하며, 그것은 이것과 중복이 아닙니다. 그 호출은
   "프레임의 이 구간은 왜곡되지 않는다"고 말하고, 이것은 "왜곡되는 구간 안에서도 레벨만
   왜곡된다"고 말합니다. 두 개의 입도이며, 어느 하나를 잃으면 서로 다른 것을 잃습니다. */
"  vec2 dUV = vUVa - vUV;\n"
"  dUV *= AFF_LIMIT / (AFF_LIMIT + abs(dUV));\n"
"  vec2 uv = vUV + dUV * ((uMode==0) ? uAffine : 0.0);\n"
/* The flow. A straight scroll alone reads as a conveyor belt, so most of the
   motion is the WARP: the same field that tilts the normal also pushes the
   lookup sideways, which turns the crust over instead of sliding it. The
   scroll is kept small and diagonal so the sea still has a direction.
   흐름입니다. 곧은 스크롤만으로는 컨베이어 벨트로 읽히므로 움직임의 대부분은 *뒤틀림*입니다.
   노멀을 기울이는 것과 같은 장이 조회 위치를 옆으로 밀며, 그것이 껍질을 미끄러뜨리는 대신
   뒤집습니다. 스크롤은 작고 대각선으로 유지되어 바다가 방향을 갖습니다. */
"  if(uMode==0 && uPParam.z>0.0){\n"
"    float ft=uTime*uPParam.z;\n"
"    uv += vec2(ft*0.035, ft*0.021) + flowField(vPos, ft)*0.030;\n"
"  }\n"
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
/* --- moving light on a billboard ---------------------------------------
   ADDED, NEVER SUBTRACTED, which is the whole of why this is safe. A sprite
   carries no baked light (::Vtx::lr is 0 on everything but level geometry),
   so lighting one the way the world is lit would mean a monster in an unlit
   room going black -- darker than before the change, and worse. Adding a
   contribution instead leaves the artwork's own shading as the floor and
   lets a muzzle flash or an explosion raise it.

   NO N-dot-L, and that is not a shortcut. A billboard's normal is generated
   to face the camera, so it carries no information about which way the
   creature is turned; a Lambert term against it would say "lit from
   wherever the player is standing", which is exactly wrong for a light off
   to one side. Distance attenuation alone is the honest half of the model.

   Modulated by `sp.rgb` rather than added flat, so a dark creature stays
   dark. Adding light to a black silhouette would paint the light's own
   colour onto it and turn every monster into a lantern at close range.

   빌보드 위의 움직이는 빛입니다. *더하기만 하고 빼지 않으며*, 그것이 이것이 안전한
   이유의 전부입니다. 스프라이트는 구워진 빛을 지니지 않으므로(::Vtx::lr은 레벨
   지오메트리 외에는 전부 0), 월드와 같은 방식으로 조명하면 불 꺼진 방의 몬스터가
   검게 됩니다. 변경 전보다 어두워지는 것이며 더 나쁩니다. 대신 기여분을 더하면 그림
   자체의 음영이 바닥으로 남고, 총구 섬광이나 폭발이 그것을 들어 올립니다.

   N·L이 없으며 이는 편법이 아닙니다. 빌보드의 법선은 카메라를 향하도록 생성되므로
   그 생물이 어느 쪽을 향하고 있는지에 대한 정보를 담고 있지 않습니다. 그것에 대한
   램버트 항은 "플레이어가 서 있는 쪽에서 비춘다"고 말하게 되는데, 옆쪽의 광원에
   대해서는 정확히 틀린 말입니다. 거리 감쇠만이 이 모델의 정직한 절반입니다.

   평평하게 더하지 않고 `sp.rgb`로 변조하므로 어두운 생물은 어둡게 남습니다. 검은
   실루엣에 빛을 더하면 광원 자신의 색이 칠해져, 가까이서 모든 몬스터가 등불이
   됩니다. */
"    vec3 add=vec3(0.0);\n"
"    for(int i=0;i<uNumLights;i++){\n"
"      vec3  d=uLightPos[i].xyz-vPos;\n"
"      float dist=length(d);\n"
"      float rad=uLightPos[i].w;\n"
"      if(dist>rad) continue;\n"
"      float att=1.0-dist/rad; att*=att;\n"
"      add+=uLightCol[i].rgb*(att*uLightCol[i].a);\n"
"    }\n"
"    c+=sp.rgb*add*SPRITE_LIGHT;\n"
"    c=mix(c, vec3(1.0,0.92,0.92), uColor.a);\n"
"    oCol=vec4(fogged(c, vPos, uEye), 1.0);\n"
"    return;\n"
"  }\n"
/* Flat sprite: the same hard cutout, with neither of the two things RD_SPRITE
   does to a sprite that STANDS somewhere. A hand-drawn viewmodel is not in the
   world -- it is a fixed part of the frame -- so:
     - no fog, because its vertices are screen coordinates and the distance
       from the eye to them is not a distance at all. Reusing RD_SPRITE washed
       the gun to the fog colour, which is the same shape of mistake as lighting
       a HUD element.
     - no hit flash, because uColor.a means "how white is this monster right
       now", and a viewmodel passing a sensible-looking alpha of 1.0 asked for
       a fully white gun and got one.
   평면 스프라이트: 동일한 하드 컷아웃이되, RD_SPRITE가 *월드에 서 있는* 스프라이트에
   하는 두 가지를 모두 하지 않습니다. 손으로 그린 뷰 모델은 월드에 있지 않고 프레임의
   고정된 일부입니다. 안개는 정점이 화면 좌표이므로 눈까지의 거리가 거리가 아니기에
   적용하지 않고, 피격 섬광은 uColor.a가 "이 몬스터가 지금 얼마나 하얀가"를 뜻하므로
   적용하지 않습니다. */
"  if(uMode==6){\n"
"    vec4 sp=texture(uTex,uv);\n"
"    if(sp.a<0.5) discard;\n"
"    oCol=vec4(sp.rgb*uColor.rgb, 1.0);\n"
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

/* --- the world's light ------------------------------------------------
 *
 * A fixed key direction, plus whatever point lights the level declared, then
 * QUANTISED into bands before anything else touches it.
 *
 * The banding is the point, not an artefact. The resolve pass quantises to a
 * handful of levels, so the tone budget is small: continuous shading over
 * 0.32..1.0 lands in a few of those steps, and a point light added on top just
 * shifts pixels between neighbouring ones. The result reads as noise gaining
 * density rather than as light falling on a surface.
 *
 * THIS PARAGRAPH SAID "FOUR" AND POST_LEVELS_DEFAULT HAS BEEN 12. The count
 * moved when post.c gained its gamma-space quantiser and this reasoning did
 * not follow it, so anyone re-deriving the tone budget from here got a number
 * three times too pessimistic -- which is exactly what happened, twice, during
 * an investigation into the lighting. The argument survives the correction
 * whole: it never depended on the count being four, only on it being small.
 * Written without a number now, because the number lives in post.h and a
 * second copy of it here is the thing that just went stale.
 * *이 문단은 "넷"이라고 적고 있었고 POST_LEVELS_DEFAULT는 12였습니다.* post.c가 감마 공간
 * 양자화기를 얻으면서 그 수가 움직였는데 이 추론은 따라가지 않았고, 그래서 이곳에서 톤
 * 예산을 다시 유도한 사람은 3배 비관적인 수를 얻었습니다. 조명 조사 도중 실제로 두 번
 * 그런 일이 있었습니다. 논거 자체는 그대로입니다. 그 수가 넷인 것에 기댄 적이 없고 작다는
 * 것에만 기댔습니다. 이제 수 없이 적습니다. 그 수는 post.h에 있고, 이곳의 두 번째 사본이야
 * 말로 방금 낡아 버린 그것이기 때문입니다.
 *
 * Rounding the light to a fixed number of steps FIRST makes each step a
 * region with an edge, and an edge is what the eye reads as a layer. It is the
 * same reason the dither quantises in gamma space: with few levels, deciding
 * where the boundaries go matters more than resolution does.
 *
 * LIGHT_BANDS is deliberately not equal to the dither's count. The dither
 * quantises the FINAL colour, this quantises the illumination before the
 * material is applied, so a dark material and a bright one in the same band
 * still differ -- which is what keeps the level readable rather than posterised
 * into flat shapes.
 *
 * 고정 주광 방향에 레벨이 선언한 점광원을 더한 뒤, 다른 처리가 닿기 전에 단계로
 * *양자화*합니다.
 *
 * 이 계단화가 부산물이 아니라 목적입니다. 해상 패스가 4단계로 디더링하므로 톤 예산이
 * 매우 작습니다. 0.32~1.0에 걸친 연속 음영은 그중 약 2.5단계에 놓이며, 그 위에 점광원을
 * 더해 봐야 같은 두 단계 사이에서 픽셀이 옮겨 다닐 뿐입니다. 결과는 표면에 빛이 떨어지는
 * 것이 아니라 잡음의 밀도가 올라가는 것으로 읽힙니다.
 *
 * 빛을 *먼저* 정해진 수의 단계로 반올림하면 각 단계가 경계를 가진 영역이 되고, 눈은 그
 * 경계를 레이어로 읽습니다. 디더가 감마 공간에서 양자화하는 것과 같은 이유입니다. 단계가
 * 적을 때는 해상도보다 경계를 어디에 둘지가 더 중요합니다.
 *
 * LIGHT_BANDS를 디더의 4와 일부러 다르게 둡니다. 디더는 *최종 색상*을 양자화하지만
 * 이것은 재질이 적용되기 전의 *조도*를 양자화하므로, 같은 단계에 있는 어두운 재질과 밝은
 * 재질이 여전히 구분됩니다. 그것이 레벨을 평평한 도형으로 포스터화하지 않고 읽을 수 있게
 * 유지합니다. */
/* The key light alone must still reach 1.0, or adding point lights makes the
   whole level darker than it was before they existed. A first pass used
   0.22 + 0.58*key, which topped out at 0.80 and banded down to 0.75 -- every
   surface a torch did not reach came out a quarter dimmer than the old fixed
   lighting, which is not "adding lights", it is dimming the level and then
   patching some of it back.
   주광만으로도 1.0에 도달해야 합니다. 그렇지 않으면 점광원을 더하는 것이 레벨 전체를
   그것들이 없던 때보다 어둡게 만듭니다. 첫 시도는 0.22 + 0.58*key였는데 최대 0.80에서
   멈추고 0.75 단계로 내려앉았습니다. 횃불이 닿지 않는 모든 표면이 기존 고정 조명보다
   25% 어두워졌는데, 이는 "광원을 더하는 것"이 아니라 레벨을 어둡게 만든 뒤 일부를 다시
   덧대는 것입니다. */
/* --- how bright a surface nothing is lighting comes out ---------------------
 *
 * ENGLISH
 * -------
 * RAISED FROM 0.32, AND THE REASON IS THAT THE LAMPS LEFT. This number is the
 * floor: what a wall is worth with the key light edge-on and nothing in the
 * air. It was chosen when a level's own lamps lit the room and the floor only
 * had to be dark rather than black. They light nothing now -- scene.c's note
 * above ::MoveLight says why -- so between shots the ambient IS the lighting,
 * and 0.32 of an albedo that is itself under half reads as a room the player
 * cannot navigate rather than as a room that is dark.
 *
 * THE KEY'S SHARE COMES DOWN BY EXACTLY WHAT THE FLOOR GOES UP, so the two
 * still sum to 1.0 and a fully key-lit surface is still worth the whole range.
 * That constraint is not arithmetic tidiness: the comment above this block
 * records that a first attempt used 0.22 + 0.58, topped out at 0.80, banded
 * down to 0.75, and made every surface a light did not reach a quarter dimmer
 * than before. Anything that does not sum to 1.0 dims the level and then
 * patches some of it back.
 *
 * WHAT IT COSTS IS TONAL RANGE, and it is worth saying rather than
 * discovering. `lum` now spans 0.45..1.0 instead of 0.32..1.0, which is 2.2
 * bands of the five instead of 2.7 -- so a face has a little less room to
 * shade across before it lands in the same band as its neighbour. Judged
 * against the alternative, which is a room lit to 0.32 by nothing at all.
 *
 * 한국어
 * ------
 * *0.32에서 올렸고, 이유는 등이 떠났기 때문입니다.* 이 수는 바닥값입니다. 주광이 스치듯
 * 닿고 공중에 아무것도 없을 때 벽이 지니는 값입니다. 이 값은 레벨 자신의 등이 방을 밝히던
 * 때에 골라졌고, 그때 바닥값은 검지만 않으면 되었습니다. 이제 등은 아무것도 밝히지
 * 않으므로(scene.c의 ::MoveLight 위 설명을 참조하십시오) 사격과 사격 사이에는 주변광이 *곧*
 * 조명이며, 그 자체로 절반에 못 미치는 알베도의 0.32는 어두운 방이 아니라 플레이어가 길을
 * 찾을 수 없는 방으로 읽힙니다.
 *
 * *주광의 몫은 바닥값이 오른 만큼 정확히 내려가므로* 둘의 합은 여전히 1.0이고, 주광을 온전히
 * 받는 표면은 여전히 전 범위의 값어치입니다. 이 제약은 산술적 정돈이 아닙니다. 이 블록 위의
 * 주석이 기록하듯 첫 시도는 0.22 + 0.58이었고 최대 0.80에서 멈춰 0.75 단계로 내려앉았으며,
 * 빛이 닿지 않는 모든 표면을 이전보다 25% 어둡게 만들었습니다. 합이 1.0이 아닌 것은 레벨을
 * 어둡게 만든 뒤 일부를 다시 덧대는 것입니다.
 *
 * *대가는 톤의 폭이며*, 발견되기보다 적혀 있어야 합니다. `lum`은 이제 0.32~1.0이 아니라
 * 0.45~1.0을 지나며, 다섯 단계 중 2.7이 아니라 2.2단계입니다. 면 하나가 이웃과 같은 단계에
 * 들어가기 전까지 음영이 변할 여유가 조금 줄어듭니다. 그 대안이 *아무것도* 밝히지 않는 0.32의
 * 방이라는 점에 비추어 판단했습니다. */
"    const float LIGHT_BANDS = 5.0;\n"
"    const float AMBIENT     = 0.45;\n"

/* --- normal mapping ---------------------------------------------------------
 *
 * ENGLISH
 * -------
 * The shading normal is perturbed by the material's own relief before ANY
 * lighting is computed, so every term below -- the key light, the point
 * lights, and the banding that quantises them -- sees the bumpy surface rather
 * than the flat polygon. Applying it to only one of them would light the
 * relief from one direction and the polygon from another.
 *
 * Only the world pass. The view model is lit in gun space by a fixed light for
 * readability, sprites have no surface to speak of, and the swatch deliberately
 * shows the material with nothing done to it.
 *
 * uPParam.y carries the strength, so a material opts in through its recipe
 * with no new uniform and no per-draw cost for the ones that do not. A pixel
 * material (uProc==0) is left flat: its relief is painted into the texture by
 * whoever drew it, and differencing an authored image would fight the shading
 * already in it rather than add to it.
 *
 * 한국어
 * ------
 * 셰이딩 법선은 조명이 계산되기 *전에* 재질 자신의 요철로 교란됩니다. 따라서 아래의 모든
 * 항(주광, 점광원, 그리고 그것들을 양자화하는 밴딩)이 평평한 다각형이 아니라 울퉁불퉁한
 * 표면을 보게 됩니다. 그중 하나에만 적용하면 요철과 다각형이 서로 다른 방향에서 빛을
 * 받게 됩니다.
 *
 * 월드 패스에만 적용합니다. 뷰 모델은 가독성을 위해 총기 공간의 고정 광원으로 조명되고,
 * 스프라이트에는 논할 표면이 없으며, 스와치는 의도적으로 아무 처리도 하지 않은 재질을
 * 보여 줍니다.
 *
 * uPParam.y가 강도를 나릅니다. 덕분에 재질이 새로운 유니폼 없이 레시피를 통해 참여하며,
 * 참여하지 않는 재질에는 그리기당 비용이 없습니다. 픽셀 재질(uProc==0)은 평평하게
 * 둡니다. 그 요철은 그린 사람이 텍스처에 칠해 넣은 것이며, 제작된 이미지를 차분하면 이미
 * 그 안에 있는 음영에 더해지는 것이 아니라 그것과 싸우게 됩니다. */
/* The UV is passed UNSNAPPED, exactly as the colour lookup passes it.
   procNormal offsets by a texel and then lets procColour snap each sample --
   snapping here as well would quantise the offset away and every difference
   would be zero.
   콜러 조회가 전달하는 것과 정확히 같이 스냅되지 *않은* UV를 전달합니다. procNormal이
   텍셀만큼 오프셋한 뒤 각 샘플을 procColour가 스냅하게 합니다. 이곳에서도 스냅하면
   오프셋이 양자화되어 사라지고 모든 차분이 0이 됩니다. */
"    if(uProc != 0 && uPParam.y > 0.0)\n"
"      n = procNormal(uProc, uv*uPScale, uPCol, n, uPParam.y);\n"

"    float key=max(dot(n,normalize(vec3(0.40,0.90,0.25))),0.0);\n"
"    float lum=AMBIENT+(1.0-AMBIENT)*key;\n"
"    vec3  tint=vec3(1.0);\n"

/* Point lights. Distance attenuation is (1 - d/r)^2 rather than the physical
   inverse square: inverse square never reaches zero, so every light would
   touch every surface in the level and the bands would never close. A radius
   that actually ends is what lets a torch light one alcove.
   점광원입니다. 거리 감쇠는 물리적인 역제곱이 아니라 (1 - d/r)^2입니다. 역제곱은 결코
   0에 도달하지 않으므로 모든 광원이 레벨의 모든 표면에 닿고 단계가 닫히지 않습니다.
   실제로 끝나는 반경이 있어야 횃불 하나가 벽감 하나를 비출 수 있습니다. */
"    float lit=0.0;\n"
"    for(int i=0;i<uNumLights;i++){\n"
"      vec3  d=uLightPos[i].xyz-vPos;\n"
"      float dist=length(d);\n"
"      float rad=uLightPos[i].w;\n"
"      if(dist>rad) continue;\n"
"      float att=1.0-dist/rad; att*=att;\n"
"      float lam=max(dot(n,d/max(dist,0.001)),0.0);\n"
"      float e=att*lam*uLightCol[i].a;\n"
"      lum+=e;\n"
"      lit+=e;\n"
"      tint=mix(tint,uLightCol[i].rgb,clamp(e,0.0,1.0));\n"
"    }\n"

/* The baked light, folded in the same way a dynamic one is. It arrives already
   shadowed -- the bake cast that ray at load, which is the one thing this
   shader cannot do and therefore the one thing still baked -- so all that is
   left is to add it. What is in it is the sun and the sky dome, and nothing
   else: a level's point lamps are not baked and are not in the loop above
   either. See level.c's bake_light and scene.c's note above ::MoveLight.
   Its own luminance drives the tint, so a room lit warm by the sun and crossed
   by a white muzzle flash blends between them exactly as two dynamic lights
   would.
   구워 넣은 조명이며 동적 광원과 같은 방식으로 합칩니다. 그림자는 로드 시점에 이미
   처리되었고, 그 광선이야말로 이 셰이더가 할 수 없는 유일한 일이자 그래서 여전히 구워지는
   유일한 것입니다. 남은 일은 더하는 것뿐입니다. 그 안에 담긴 것은 태양과 하늘 돔이고 그
   외에는 없습니다. 레벨의 점광원은 구워지지도 않고 위의 반복문에 있지도 않습니다. level.c의
   bake_light와 scene.c의 ::MoveLight 위 설명을 참조하십시오. 자기 휘도가 색조를 이끌므로, 태양으로 따뜻하게 밝은 방을 흰 총구 섬광이
   가로지르면 두 동적 광원과 똑같이 섞입니다. */
"    float bl=dot(vLit,vec3(0.299,0.587,0.114));\n"
"    if(bl>0.0){\n"
"      lit+=bl;\n"
"      tint=mix(tint,vLit/max(bl,0.001),clamp(bl,0.0,1.0));\n"
"    }\n"

/* Break the band edges up with noise, BEFORE the quantisation.
 *
 * The bands above are clean steps with hard edges, and hard edges are what
 * makes them read as layers -- that was the point. But a perfectly straight
 * boundary running across a floor reads as a rendering artefact rather than as
 * light, because nothing in a real room has an edge like that.
 *
 * The hardware's bands were broken up for free: the PSX dithered the final
 * colour at 15-bit, so every boundary came out stippled. This project dithers
 * too, but at the resolve pass and only to four levels, which is too coarse to
 * dissolve a band edge -- the edge falls between two dither levels and survives
 * intact.
 *
 * Perturbing the ILLUMINATION before it bands is what does it. A boundary
 * that would have been a straight line now wanders by up to NOISE_AMOUNT of a
 * band, so it comes out as a stippled, broken edge instead. Applying the same
 * noise AFTER the quantisation would only add grain on top of clean steps,
 * which reads as film grain over the image rather than as the light itself
 * being uneven.
 *
 * Sampled from vPos.xz -- WORLD space, and horizontal. Three consequences,
 * each of which is the reason for a choice that looks arbitrary:
 *
 *  - World space rather than screen space means the noise is attached to the
 *    surface. Screen-space noise crawls as the camera moves, which is the one
 *    thing that reliably reads as "post-processing" rather than as the room.
 *  - The xz plane rather than a triplanar projection: floors and ceilings are
 *    most of what is visible at this camera height, and they are exactly the
 *    surfaces the xz plane samples correctly. Walls get a vertically streaked
 *    pattern, which at this band count is indistinguishable from any other
 *    noise and costs two texture-free lookups instead of six.
 *  - n2 rather than fbm: fbm is four octaves for a pattern that is about to be
 *    quantised into five levels, so three of those octaves land inside a band
 *    and are never seen.
 *
 * 밴드 경계를 노이즈로 깨뜨립니다. 양자화 *이전에* 수행합니다.
 *
 * 위의 밴드는 경계가 뚜렷한 깨끗한 계단이며, 그 뚜렷함이 레이어로 읽히게 만드는
 * 요소였습니다. 그러나 바닥을 가로지르는 완벽한 직선 경계는 빛이 아니라 렌더링
 * 아티팩트로 읽힙니다. 실제 방의 무엇도 그런 모서리를 갖지 않기 때문입니다.
 *
 * 하드웨어의 밴드는 저절로 깨졌습니다. PSX는 15비트로 최종 색상을 디더링했으므로 모든
 * 경계가 스티플로 나왔습니다. 이 프로젝트도 디더링하지만 해상 패스에서 4단계로만
 * 수행하므로 밴드 경계를 녹이기엔 너무 성깁니다. 경계가 두 디더 단계 사이에 놓여 그대로
 * 살아남습니다.
 *
 * 밴딩 이전에 *조도*를 교란하는 것이 해법입니다. 직선이었을 경계가 이제 밴드의
 * NOISE_AMOUNT만큼 흔들려, 스티플 처리된 깨진 모서리로 나옵니다. 같은 노이즈를 양자화
 * *이후에* 적용하면 깨끗한 계단 위에 입자만 얹혀, 빛 자체가 고르지 않은 것이 아니라
 * 이미지 위의 필름 그레인으로 읽힙니다.
 *
 * vPos.xz에서 샘플링합니다. *월드* 공간이며 수평면입니다. 세 가지 귀결이 있고, 각각이
 * 임의로 보이는 선택의 이유입니다:
 *
 *  - 화면 공간이 아닌 월드 공간이므로 노이즈가 표면에 붙어 있습니다. 화면 공간 노이즈는
 *    카메라가 움직이면 기어 다니는데, 이것이야말로 방이 아니라 "후처리"로 읽히게 만드는
 *    가장 확실한 요소입니다.
 *  - 삼중 투영이 아닌 xz 평면인 이유: 이 카메라 높이에서 보이는 것의 대부분이 바닥과
 *    천장이며, 그것이 정확히 xz 평면이 올바르게 샘플링하는 표면입니다. 벽에는 수직으로
 *    늘어진 패턴이 생기지만, 이 밴드 수에서는 다른 어떤 노이즈와도 구분되지 않으면서
 *    조회 비용이 6회가 아닌 2회입니다.
 *  - fbm이 아닌 n2인 이유: fbm은 4옥타브인데 곧 5단계로 양자화될 패턴이므로, 그중 세
 *    옥타브는 밴드 안쪽에 놓여 결코 보이지 않습니다. */
/* NOISE_AMOUNT is in the same units as lum, and a band is 1/(LIGHT_BANDS-1)
 * = 0.25 wide, so this perturbs by up to 40% of a band either way.
 *
 * That is far larger than the first attempt, which used 0.055 on the reasoning
 * that a tenth of a band would be enough to stipple an edge. Measured against
 * a zero-noise reference frame, 0.055 changed nothing at all and 0.10 changed
 * 0.05% of the image -- almost every surface sits near the MIDDLE of its band,
 * not near an edge, so a small perturbation moves nothing across a boundary.
 * The response only becomes visible once the noise can reach the edge from
 * mid-band: 0.20 changes 7.2% of the frame and 0.35 changes 16.1%.
 *
 * Past about half a band the bands stop being bands, so 0.20 is the useful
 * end of that range rather than the middle of it.
 *
 * NOISE_AMOUNT는 lum과 같은 단위이고 밴드 하나의 폭이 1/(LIGHT_BANDS-1) = 0.25이므로,
 * 이 값은 밴드의 최대 40%만큼 양방향으로 교란합니다.
 *
 * 첫 시도의 0.055보다 훨씬 큰데, 그때는 밴드의 10분의 1이면 경계를 스티플하기에 충분하다고
 * 판단했습니다. 노이즈 0인 기준 프레임과 비교 측정한 결과 0.055는 아무것도 바꾸지 못했고
 * 0.10은 화면의 0.05%만 바꿨습니다. 거의 모든 표면이 밴드의 *중앙* 부근에 있지 가장자리에
 * 있지 않으므로, 작은 교란은 아무것도 경계 너머로 옮기지 못합니다. 노이즈가 밴드 중앙에서
 * 가장자리까지 닿을 수 있어야 반응이 보이기 시작합니다. 0.20은 7.2%, 0.35는 16.1%를
 * 바꿉니다.
 *
 * 밴드 절반을 넘어서면 밴드가 더 이상 밴드가 아니게 되므로, 0.20은 그 범위의 중간이
 * 아니라 쓸모 있는 상한입니다. */
"    const float NOISE_SCALE  = 1.7;\n"
"    const float NOISE_AMOUNT = 0.20;\n"
"    const float NOISE_FLOOR  = 0.25;\n"

/* Weighted by how much POINT LIGHT actually reaches this surface.
 *
 * Unweighted, the noise was added to lum everywhere, so a wall with no light
 * on it at all got the same stippled band edges as one directly under a torch.
 * That reads as the light scattering into directions it never reached -- the
 * pattern says "something is lit here" on surfaces that are lit by nothing but
 * the ambient term.
 *
 * `lit` is the sum of the point lights' contributions, which is already
 * computed for lum and costs nothing extra. The floor keeps a trace of noise
 * on unlit surfaces so they do not become perfectly flat bands, which is its
 * own artefact -- the aim is for the noise to FOLLOW the light, not to vanish
 * where the light does.
 *
 * This is the same rule `tint` already followed: it mixes toward a light's
 * colour in proportion to `e`, so an unlit surface keeps its neutral tint.
 * The noise was the one term that ignored that, which is why it was the one
 * that looked wrong.
 *
 * 이 표면에 실제로 도달한 *점광원*의 양으로 가중합니다.
 *
 * 가중하지 않으면 노이즈가 어디서나 lum에 더해지므로, 빛이 전혀 닿지 않는 벽도 횃불
 * 바로 아래의 벽과 똑같이 스티플된 밴드 경계를 갖습니다. 이는 빛이 도달한 적 없는
 * 방향으로 산란되는 것처럼 읽힙니다. 주변광 외에는 아무것도 비추지 않는 표면에서 패턴이
 * "여기 뭔가 비춰지고 있다"고 말하는 셈입니다.
 *
 * `lit`은 점광원 기여도의 합이며 lum을 위해 이미 계산되므로 추가 비용이 없습니다.
 * 하한값은 빛이 없는 표면에도 노이즈의 흔적을 남겨 완전히 평평한 밴드가 되지 않게
 * 합니다. 그것 자체가 또 다른 아티팩트이기 때문입니다. 목표는 노이즈가 빛을 *따라가는*
 * 것이지, 빛이 없는 곳에서 사라지는 것이 아닙니다.
 *
 * 이는 `tint`가 이미 따르던 규칙과 같습니다. tint는 `e`에 비례해 광원 색으로
 * 섞이므로 빛이 없는 표면은 중성 색조를 유지합니다. 노이즈만이 그 규칙을 무시했고,
 * 그래서 노이즈만 잘못되어 보였습니다. */
"    float nw=NOISE_FLOOR+(1.0-NOISE_FLOOR)*clamp(lit,0.0,1.0);\n"
"    lum+=(n2(vPos.xz*NOISE_SCALE)-0.5)*NOISE_AMOUNT*nw;\n"

/* Band it. floor(x*N)/N would never reach 1.0 and the brightest surfaces would
   sit one step below full; the +0.5 rounds to nearest and (N-1) puts the top
   band exactly at 1.0 -- the same off-by-one the dither's own comment warns
   about.
   단계로 나눕니다. floor(x*N)/N은 1.0에 결코 도달하지 못해 가장 밝은 표면이 한 단계
   아래에 머무릅니다. +0.5로 반올림하고 (N-1)로 나누면 최상위 단계가 정확히 1.0이 됩니다.
   디더 자신의 주석이 경고하는 것과 같은 off-by-one입니다. */
"    lum=clamp(lum,0.0,1.0);\n"
/* --- and the daylight goes on AFTER the bands ------------------------------
 *
 * ENGLISH
 * -------
 * THE BANDING IS A STYLE AND THE SUN IS A FACT, and that is the whole of why
 * this one term is exempt. Everything above the quantiser is invented light:
 * AMBIENT is a constant, `key` is a hardcoded direction that exists so a wall
 * is not flat, and the dynamic lights are events. Banding invented light into
 * five levels is the era's look. `vLit` is not invented -- it is what
 * level.c's bake_light measured by tracing the map's own `_sunlight` and
 * `_sunlight2` against the map's own geometry, which is why 11% of the arena
 * is lit and 38% is in shadow.
 *
 * WHAT QUANTISING IT COST: a shaft of daylight through a hole in the ceiling
 * arrives as a gradient and is the one thing in the frame whose SHAPE carries
 * information -- where the opening is, how far in the light reaches, which
 * part of the floor is safe to stand on and be seen. Rounded to a quarter of
 * the range it stops being a shaft and becomes a polygon: two or three flat
 * plateaux with hard edges that follow the quantiser rather than the opening.
 * The noise then breaks those edges up, which is exactly right for a band
 * boundary in stylised shading and exactly wrong for the edge of a sunbeam --
 * it reads as the sunlight itself being ragged.
 *
 * SO THE TWO ARE SUMMED IN DIFFERENT PLACES rather than given different
 * curves. `lit` still counts the daylight, so a sunlit surface still gets the
 * fuller noise weight on its BANDED half; `tint` is still mixed toward it, so
 * a warm sun still warms the room. Only the quantiser is stepped around, and
 * the clamp after the add is what keeps a bright shaft from running past 1.0
 * into the post pass's bloom knee.
 *
 * @note This is the ONE exemption. A second one turns the banding into a
 *       suggestion, and the look stops being a look -- the relief slopes and
 *       NOISE_AMOUNT are both amplified specifically to survive five levels,
 *       so every term that leaves the quantiser makes those two wrong for the
 *       terms that stay.
 *
 * 한국어
 * ------
 * *밴딩은 양식이고 태양은 사실이며*, 그것이 이 항 하나만 면제되는 이유의 전부입니다.
 * 양자화기 위의 모든 것은 지어낸 빛입니다. AMBIENT는 상수이고, `key`는 벽이 평평해 보이지
 * 않게 하려고 존재하는 고정 방향이며, 동적 광원은 사건입니다. 지어낸 빛을 다섯 단계로
 * 나누는 것이 그 시대의 외양입니다. `vLit`는 지어낸 것이 아닙니다. level.c의 bake_light가
 * 맵 자신의 `_sunlight`와 `_sunlight2`를 맵 자신의 지오메트리에 대고 추적해 *측정한* 값이며,
 * 그래서 아레나의 11%가 밝고 38%가 그림자입니다.
 *
 * *양자화가 치르게 한 값:* 천장 구멍으로 들어오는 햇살은 그러데이션으로 도착하며, 프레임에서
 * 그 *모양*이 정보를 나르는 유일한 것입니다. 구멍이 어디인지, 빛이 얼마나 안쪽까지 닿는지,
 * 바닥의 어느 부분에 서면 보이는지입니다. 범위의 4분의 1로 반올림되면 그것은 햇살이기를
 * 그만두고 다각형이 됩니다. 구멍이 아니라 양자화기를 따라가는 딱딱한 모서리를 가진 평평한
 * 고원 두셋입니다. 그다음 노이즈가 그 모서리를 부수는데, 그것은 양식화된 음영의 밴드
 * 경계에는 정확히 옳고 햇살의 가장자리에는 정확히 틀립니다. 햇빛 자체가 너덜너덜한 것으로
 * 읽히기 때문입니다.
 *
 * *그래서 둘에게 다른 곡선을 주는 대신 다른 자리에서 더합니다.* `lit`는 여전히 자연광을
 * 세므로 햇빛이 든 표면은 자기 *밴딩된* 절반에 대해 여전히 더 큰 노이즈 가중치를 받고,
 * `tint`도 여전히 그쪽으로 섞이므로 따뜻한 태양은 여전히 방을 따뜻하게 합니다. 오직
 * 양자화기만 비켜 갑니다. 더한 뒤의 clamp는 밝은 햇살이 1.0을 넘어 후처리의 블룸 문턱으로
 * 달려가지 않게 붙듭니다.
 *
 * @note *면제는 이것 하나입니다.* 두 번째가 생기면 밴딩은 권고가 되고 외양은 외양이기를
 *       그만둡니다. 부조 기울기와 NOISE_AMOUNT는 둘 다 다섯 단계를 견디도록 특별히
 *       증폭되어 있으므로, 양자화기를 떠나는 항이 생길 때마다 남아 있는 항들에 대해 그 둘이
 *       틀린 값이 됩니다. */
"    lum=floor(lum*(LIGHT_BANDS-1.0)+0.5)/(LIGHT_BANDS-1.0);\n"
"    lum=clamp(lum+bl,0.0,1.0);\n"

"    c*=lum*tint;\n"
"    oCol=vec4(fogged(c, vPos, uEye),1.0);\n"
"  }\n"
"}\n";

static GLuint g_prog;
static GLint  g_u_mvp, g_u_eye, g_u_mode, g_u_color;
static GLint  g_u_nlights, g_u_lpos, g_u_lcol;

/* How many dynamic lights the last ::rd_lights left in the shader. Kept so a
   test can ask, and what it is asking has changed twice, which is the reason
   to keep it rather than to inline it: a level's lamps were baked and the
   interesting number was zero, then they were gathered per frame and the
   interesting number was that a lit level reached this loop, and now they are
   switched off and it is zero again -- but for a different reason, and with a
   grenade in the air it must not be. Every one of those failures is the same
   kind of invisible. A room lit twice does not look broken, it looks bright;
   a room lit zero times looks like a room with the lights off. Neither says
   which half moved. 마지막 ::rd_lights가 셰이더에 남긴 동적 광원의 수입니다. 테스트가
   물어볼 수 있도록 보관하며, 무엇을 묻는지가 이미 두 번 바뀌었다는 것이 인라인하지 않고
   보관하는 이유입니다. 레벨의 등은 구워졌고 그때 흥미로운 값은 0이었으며, 다음에는
   프레임마다 모여 조명이 있는 레벨이 이 반복문에 도달하는지가 흥미로웠고, 이제는 꺼져서
   다시 0입니다. 다만 이유가 다르고, 유탄이 공중에 있을 때는 0이어서는 안 됩니다. 그 모든
   실패가 같은 종류로 보이지 않습니다. 두 번 밝혀진 방은 고장 나 보이지 않고 *밝아* 보이며,
   0번 밝혀진 방은 그저 불이 꺼진 방으로 보입니다. 어느 쪽도 어느 절반이 움직였는지 말해
   주지 않습니다. */
static int    g_n_lights;
static GLint  g_u_snap;
static GLint  g_u_affine;
static GLint  g_u_proc, g_u_pcol, g_u_pscale, g_u_pparam;
static GLint  g_u_time;

static GLuint compile(GLenum type, const char **src, int n) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, n, src, 0);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), 0, log);
        plat_fatal("shader compile", log);
    }
    return s;
}

void rd_init(void) {
    /* FS_FOG before FS_MAIN because main() calls fogged(): GLSL needs the
       definition first, and splicing it as its own part keeps the one
       constant out of both call sites.
       main()이 fogged()를 부르므로 FS_FOG가 FS_MAIN보다 앞입니다. GLSL은 정의가
       먼저여야 하며, 별도의 조각으로 이어 붙이면 그 상수 하나가 두 호출 지점
       어디에도 적히지 않습니다. */
    const char *fs_parts[4] = {FS_SRC, FS_PROC, FS_FOG, FS_MAIN};
    GLuint vs = compile(GL_VERTEX_SHADER, &VS_SRC, 1);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_parts, 4);
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);

    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(g_prog, sizeof(log), 0, log);
        plat_fatal("program link", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    glUseProgram(g_prog);
    g_u_mvp   = glGetUniformLocation(g_prog, "uMVP");
    g_u_eye   = glGetUniformLocation(g_prog, "uEye");
    g_u_snap    = glGetUniformLocation(g_prog, "uSnap");
    g_u_affine  = glGetUniformLocation(g_prog, "uAffine");
    g_u_nlights = glGetUniformLocation(g_prog, "uNumLights");
    g_u_lpos    = glGetUniformLocation(g_prog, "uLightPos");
    g_u_lcol    = glGetUniformLocation(g_prog, "uLightCol");
    g_u_mode  = glGetUniformLocation(g_prog, "uMode");
    g_u_color = glGetUniformLocation(g_prog, "uColor");
    g_u_proc   = glGetUniformLocation(g_prog, "uProc");
    g_u_pcol   = glGetUniformLocation(g_prog, "uPCol");
    g_u_pscale = glGetUniformLocation(g_prog, "uPScale");
    g_u_pparam = glGetUniformLocation(g_prog, "uPParam");
    g_u_time   = glGetUniformLocation(g_prog, "uTime");
    glUniform1i(glGetUniformLocation(g_prog, "uTex"), 0);
}

/* Uniforms are cheap but not free, and most draws are plain textures. Skip the
   parameter uploads entirely when the shader is off. */
void rd_proc(int proc, const float rgb[3], float scale, const float params[3]) {
    glUniform1i(g_u_proc, proc);

    /* ABOVE THE EARLY RETURN, unlike the other three, because `flow`
       (params.z) is read on the TEXTURED path too -- `star_lava3` is an
       image and it is the lava the arena is named for. Below the return
       it was never uploaded for such a material, so the shader saw
       whatever the last procedural material had left behind and no
       imported texture could ever move.
       Uploading it always also makes the staleness harmless in the other
       direction: a still material now actively says it is still, rather
       than inheriting the last thing that flowed.
       *다른 셋과 달리 이른 반환보다 위입니다.* `flow`(params.z)는 *텍스처* 경로에서도
       읽히기 때문입니다. `star_lava3`은 이미지이며 이 투기장의 이름이 된 바로 그
       용암입니다. 반환 아래에 있으면 그런 재질에 대해서는 결코 업로드되지 않았고,
       셰이더는 마지막 절차적 재질이 남긴 값을 보았으며 가져온 텍스처는 어느 것도
       움직일 수 없었습니다.
       항상 올리는 것은 반대 방향의 잔류도 무해하게 만듭니다. 정지한 재질이 마지막으로
       흐른 것을 물려받는 대신 스스로 정지해 있다고 말하게 됩니다. */
    glUniform3fv(g_u_pparam, 1, params);

    if (proc == PROC_TEXTURE) return;
    glUniform3fv(g_u_pcol, 1, rgb);
    glUniform1f (g_u_pscale, scale);
}

void rd_use  (void)      { glUseProgram(g_prog); }
void rd_mode (int mode)  { glUniform1i(g_u_mode, mode); }
void rd_mvp  (mat4 mvp)  { glUniformMatrix4fv(g_u_mvp, 1, GL_FALSE, mvp.m); }
void rd_eye  (v3 eye)    { glUniform3fv(g_u_eye, 1, &eye.x); }

void rd_time(float t) { glUniform1f(g_u_time, t); }

void rd_snap(float grid_w, float grid_h) {
    /* Both axes or neither. A grid with one axis zeroed would snap x and leave
       y continuous, which reads as vertical tearing rather than as a wobble.
       두 축 모두이거나 둘 다 아니거나입니다. 한 축만 0인 격자는 x만 스냅하고 y는 연속으로
       두어, 흔들림이 아니라 수직으로 찢어지는 것처럼 보입니다. */
    if (grid_w < 1.0f || grid_h < 1.0f) { grid_w = 0.0f; grid_h = 0.0f; }

    /* Halved because NDC spans -1..1 -- two units across a buffer that is
       `grid_w` pixels wide. Passing the pixel count directly would snap to
       half-pixels and halve the effect for no stated reason.
       NDC가 -1..1로 2단위이고 버퍼 폭이 `grid_w` 픽셀이므로 절반으로 나눕니다. 픽셀 수를
       그대로 전달하면 반 픽셀 단위로 스냅되어, 명시되지 않은 이유로 효과가 절반이
       됩니다. */
    glUniform2f(g_u_snap, grid_w * 0.5f, grid_h * 0.5f);
}

void rd_affine(float amount) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    glUniform1f(g_u_affine, amount);
}

void rd_lights(const float *pos_radius, const float *col_power, int n) {
    if (n < 0) n = 0;
    if (n > RD_MAX_LIGHTS) {
        /* Clamped rather than trusted. These slots are for lights that MOVE --
           a muzzle flash, an explosion -- and a caller that has more of those
           than the shader can hold has to lose some; overrunning the uniform
           array is not the alternative. Reported so the loss is visible.
           신뢰하지 않고 제한합니다. 이 슬롯들은 *움직이는* 광원(총구 섬광, 폭발)을 위한
           것이며, 셰이더가 담을 수 있는 것보다 많이 가진 호출자는 일부를 잃을 수밖에
           없습니다. 유니폼 배열을 넘어서는 것이 대안은 아닙니다. 손실이 보이도록
           보고합니다. */
        DIAG(DIAG_LIGHT_CAP);
        n = RD_MAX_LIGHTS;
    }
    g_n_lights = n;
    glUniform1i(g_u_nlights, n);
    if (n) {
        glUniform4fv(g_u_lpos, n, pos_radius);
        glUniform4fv(g_u_lcol, n, col_power);
    }
}

int rd_light_count(void) { return g_n_lights; }

void rd_color(float r, float g, float b, float a) {
    float c[4] = {r, g, b, a};
    glUniform4fv(g_u_color, 1, c);
}
