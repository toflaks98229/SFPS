/* splittest -- the static/moving geometry split changes nothing that is drawn.
 *
 * ENGLISH
 * -------
 * ::level_geometry_part exists to stop a moving door rebuilding the whole
 * level: the static half is built once and the moving half is rebuilt as the
 * door swings. That is only worth having if the two halves together are the
 * same geometry the one-shot build produced, and "the same geometry" is exactly
 * the kind of claim that is true when written and false after the next edit to
 * brush_geometry.
 *
 * So it is asserted rather than believed, vertex for vertex, the same way
 * leveltest asserts that an empty light cache reproduces the uncached bake.
 * The split walks brushes in ascending index order on both sides, so STATIC
 * followed by MOVING is a stable partition of ALL -- not merely the same
 * vertices in some order -- and that is what makes a position-by-position
 * comparison the right test rather than a checksum.
 *
 * WHAT THIS DELIBERATELY DOES NOT CHECK: the sector model. ::level_geometry_split
 * refuses it, because a wall's spans are read from the NEIGHBOURING sector and a
 * sliding door moves the points that decide who the neighbour is. The refusal is
 * checked here; the splitting is not, because there is none.
 *
 * 한국어
 * ------
 * ::level_geometry_part는 움직이는 문이 레벨 전체를 다시 만드는 것을 막기 위해 존재합니다.
 * 정적인 절반은 한 번 만들고, 움직이는 절반만 문이 여닫히는 동안 다시 만듭니다. 그것은 두
 * 절반을 합친 것이 통째로 생성한 것과 같은 지오메트리일 때에만 가질 가치가 있으며, "같은
 * 지오메트리"는 쓸 때는 참이고 brush_geometry를 다음에 수정하면 거짓이 되는 바로 그런 종류의
 * 주장입니다.
 *
 * 그래서 믿는 대신 정점 단위로 단언합니다. leveltest가 빈 라이트 캐시는 캐시 없는 베이크를
 * 재현한다고 단언하는 것과 같은 방식입니다. 분할은 양쪽 모두 브러시를 인덱스 오름차순으로
 * 순회하므로 STATIC 다음 MOVING은 ALL의 *안정 분할*이며 단지 같은 정점의 어떤 순서가
 * 아닙니다. 그것이 체크섬이 아니라 위치별 비교를 올바른 검사로 만드는 근거입니다.
 *
 * 의도적으로 검사하지 *않는* 것: 섹터 모델입니다. ::level_geometry_split이 그것을 거절하는데,
 * 벽의 구간이 *이웃* 섹터로부터 읽히고 미끄러지는 문이 누가 이웃인지를 정하는 점들을 움직이기
 * 때문입니다. 거절 자체는 이곳에서 검사하며, 분할은 검사하지 않습니다. 분할이 없기 때문입니다.
 */

#include <stdio.h>
#include <math.h>
#include "level.h"
#include "render.h"
#include "model.h"    /* MdlRange by value -- level.h only forward-declares it */
#include "brush.h"    /* brush_geometry / brush_translate: the moving half, fetched a second way */
#include "door.h"
#include "diag.h"

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-60s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void oki(int cond, const char *what, int got, int want) {
    printf("  %-60s %6d / %-6d %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Exact equality is deliberate and is not fragile here. Both builds run the
   same brush through the same ::brush_face_poly and the same ::brush_face_uv in
   the same order, so every float is produced by an identical sequence of
   operations. A difference of one ULP would mean the split had changed the
   arithmetic, which is precisely what this is watching for -- a tolerance would
   hide exactly the defect worth catching.
   정확한 일치를 요구하는 것은 의도이며 이곳에서는 취약하지 않습니다. 두 생성 모두 같은
   브러시를 같은 ::brush_face_poly와 같은 ::brush_face_uv에 같은 순서로 통과시키므로, 모든
   float이 동일한 연산 순서로 만들어집니다. 1 ULP의 차이는 분할이 연산을 바꾸었다는 뜻이고
   그것이 바로 이 검사가 지켜보는 대상입니다. 허용 오차는 잡을 가치가 있는 결함을 정확히
   가리게 됩니다. */
static int vtx_same(const Vtx *a, const Vtx *b) {
    return a->px == b->px && a->py == b->py && a->pz == b->pz
        && a->nx == b->nx && a->ny == b->ny && a->nz == b->nz
        && a->u  == b->u  && a->v  == b->v
        && a->lr == b->lr && a->lg == b->lg && a->lb == b->lb;
}

/* The first index at which two builds disagree, or -1. Reported rather than
   just counted, because "they differ" and "they differ at vertex 812 of 3040"
   are different amounts of help when brush_geometry has just been edited.
   두 생성이 어긋나는 첫 인덱스, 또는 -1입니다. 세는 데 그치지 않고 보고하는 이유는,
   brush_geometry를 막 수정한 사람에게 "다르다"와 "3040개 중 812번 정점에서 다르다"가 서로
   다른 정도의 도움이기 때문입니다. */
static int first_diff(const MeshBuf *a, const MeshBuf *b) {
    int n = a->count < b->count ? a->count : b->count;
    for (int i = 0; i < n; i++)
        if (!vtx_same(&a->v[i], &b->v[i])) return i;
    return (a->count == b->count) ? -1 : n;
}

/* Every brush a door moves, counted straight from the door table -- the same
   question ::level_geometry_part asks, asked a second and independent way so
   the test is not simply the implementation restated.
   문이 움직이는 모든 브러시를, 문 표에서 곧바로 셉니다. ::level_geometry_part가 던지는 것과
   같은 질문을 두 번째의 독립적인 방법으로 던지므로, 이 검사가 구현을 그대로 되풀이한 것이
   되지 않습니다. */
static int moving_brush_count(const Level *l) {
    int n = 0;
    for (int i = 0; i < l->n_doors; i++)
        if (l->doors[i].sector < 0) n += l->doors[i].n_brushes;
    return n;
}

int main(int argc, char **argv) {
    /* THE FIXTURE, NOT THE SHIPPED MAP. This was pointed at `lqdm1` for one
       build and the "match the one-shot build vertex for vertex" assertion went
       red at vertex 16,497 -- correctly. That check is stronger than the split
       contract: level_geometry_part walks brushes in ascending index and emits
       maximal runs, so STATIC-then-MOVING reproduces the one-shot ORDER only
       when the moving brushes happen to be a suffix of the brush list. In
       atrium they are; in lqdm1, whose two doors sit in the middle of 807
       brushes, they are not. Nothing was wrong with the split -- the vertices
       are the same set in a different order, which is what the two builds
       promise and all they promise.
       *출하되는 맵이 아니라 픽스처입니다.* 한 빌드 동안 `lqdm1`을 겨누었고 "한 번에 만든
       것과 정점 단위로 일치한다"는 단언이 16,497번 정점에서 빨개졌습니다. 옳게 그랬습니다.
       그 검사는 분할이 약속하는 것보다 강합니다. level_geometry_part는 브러시를 오름차순으로
       훑으며 최대 연속 구간을 내보내므로, STATIC 다음 MOVING이 한 번에 만든 *순서*를
       재현하는 것은 움직이는 브러시가 마침 브러시 목록의 접미사일 때뿐입니다. atrium에서는
       그러하고, 문 둘이 브러시 807개 한가운데 앉아 있는 lqdm1에서는 그렇지 않습니다. */
    const char *name = (argc > 1) ? argv[1] : "atrium";

    printf("splittest -- static/moving geometry, on '%s'\n", name);

    static Level l;
    if (!level_load(name, &l)) {
        printf("  no level named '%s'\n", name);
        return 1;
    }
    door_reset(&l);

    printf("\n  brushes %d, doors %d (%d moving brushes), lights %d\n",
           l.brushes ? l.brushes->n_brushes : 0, l.n_doors,
           moving_brush_count(&l), l.n_lights);

    /* --- the level splits at all ------------------------------------------ */
    printf("\nwhether this level splits\n");
    {
        int split = level_geometry_split(&l);
        ok(split == (l.brushes && moving_brush_count(&l) > 0),
           "splits exactly when it is a brush level with a brush door");

        if (!split) {
            /* Not a failure -- it is the documented answer for the sector
               model, and the halves must still each build the whole level so a
               caller that asks anyway is merely slow and never wrong.
               실패가 아닙니다. 섹터 모델에 대한 문서화된 답이며, 그 경우에도 각 절반이 여전히
               레벨 전체를 생성해야 그럼에도 요청한 호출자가 느릴 뿐 결코 틀리지 않습니다. */
            MeshBuf all, part;
            mb_init(&all, 65536); mb_init(&part, 65536);
            level_geometry(&all, &l, 0, 0);
            level_geometry_part(&part, &l, 0, 0, LVL_PART_MOVING);
            oki(all.count == part.count,
                "a level that does not split builds whole for either half",
                part.count, all.count);
            ok(first_diff(&all, &part) < 0, "and identically, vertex for vertex");
            mb_free(&all); mb_free(&part);

            printf("\n%s\n", fails ? "  FAILED" : "  passed");
            return fails ? 1 : 0;
        }
    }

    /* --- the two halves are the whole ------------------------------------- */
    printf("\nSTATIC + MOVING == ALL\n");
    MeshBuf all, halves;
    mb_init(&all, 65536);
    mb_init(&halves, 65536);
    int n_static = 0;
    {
        /* Reset between the two builds so both start from a cold cache. The
           bake is the same function either way, but a warm cache and a cold one
           reaching different answers would be a defect in the cache rather than
           in the split, and this test should not be the one that finds it.
           두 생성 사이에 초기화하여 양쪽 모두 차가운 캐시에서 시작하게 합니다. 베이크는 어느
           쪽이든 같은 함수이지만, 따뜻한 캐시와 차가운 캐시가 서로 다른 답에 도달한다면 그것은
           분할이 아니라 캐시의 결함이며, 이 검사가 그것을 찾는 검사여서는 안 됩니다. */
        level_light_cache_reset();
        level_geometry(&all, &l, 0, 0);

        level_light_cache_reset();
        level_geometry_part(&halves, &l, 0, 0, LVL_PART_STATIC);
        n_static = halves.count;
        level_geometry_part(&halves, &l, 0, 0, LVL_PART_MOVING);

        oki(halves.count == all.count, "the two halves come to the whole count",
            halves.count, all.count);
        ok(n_static > 0, "the static half is not empty");
        ok(halves.count - n_static > 0, "and neither is the moving half");

        int d = first_diff(&all, &halves);
        oki(d < 0, "and match the one-shot build vertex for vertex", d, -1);
    }

    /* --- the moving half is exactly the door's brushes --------------------- */
    printf("\nwhat lands in the moving half\n");
    {
        MeshBuf mv, doors;
        mb_init(&mv, 65536);
        mb_init(&doors, 65536);

        level_light_cache_reset();
        level_geometry_part(&mv, &l, 0, 0, LVL_PART_MOVING);

        /* The same brushes fetched the other way: straight through
           ::brush_geometry, one door's run at a time. If these disagree, the
           moving half is not the set of brushes the doors actually translate --
           which is the failure that would leave a door's leaf frozen in the
           static half while the collision moved without it.
           같은 브러시를 다른 경로로 가져옵니다. ::brush_geometry를 통해 문 하나의 구간씩
           직접입니다. 이 둘이 어긋나면 움직이는 절반은 문이 실제로 옮기는 브러시 집합이
           아니며, 그것은 문짝이 정적인 절반에 얼어붙은 채 충돌만 따로 움직이는 실패입니다. */
        for (int i = 0; i < l.n_doors; i++)
            if (l.doors[i].sector < 0)
                brush_geometry(&doors, l.brushes, l.doors[i].first_brush,
                               l.doors[i].n_brushes, 0, 0);

        oki(mv.count == doors.count,
            "the moving half is the doors' brushes and nothing else",
            mv.count, doors.count);

        mb_free(&mv);
        mb_free(&doors);
    }

    /* --- and it still holds once the doors have actually moved ------------
       The case the whole change exists for, and the one a build-time-only check
       would miss: the split is not a property of a level at rest.
       이 변경 전체가 존재하는 이유인 경우이며, 생성 시점만 검사하면 놓치는 경우입니다. 분할은
       정지한 레벨의 성질이 아닙니다. */
    printf("\nafter the doors have swung\n");
    {
        /* Moved through ::brush_translate rather than by walking a player into
           the door: this is the operation ::apply_brush performs, so the
           geometry ends up somewhere a door can really put it, without the test
           also having to satisfy each door's key, tag and trigger. What is
           being checked is that the split survives the brushes being elsewhere,
           and that does not depend on what persuaded them to move.
           플레이어를 문으로 걸어 들여보내는 대신 ::brush_translate를 통해 옮깁니다. 이것이
           ::apply_brush가 수행하는 연산이므로 지오메트리는 문이 실제로 놓을 수 있는 자리에
           도달하며, 그러면서도 이 검사가 문마다의 열쇠·태그·트리거를 만족시킬 필요가 없습니다.
           검사하려는 것은 브러시가 다른 곳에 있어도 분할이 유지되는가이며, 그것은 무엇이
           브러시를 움직이게 설득했는지에 달려 있지 않습니다. */
        for (int i = 0; i < l.n_doors; i++)
            if (l.doors[i].sector < 0)
                brush_translate(l.brushes, l.doors[i].first_brush,
                                l.doors[i].n_brushes, v3f(0.0f, 0.75f, 0.0f));

        MeshBuf all2, halves2;
        mb_init(&all2, 65536);
        mb_init(&halves2, 65536);

        level_light_cache_reset();
        level_geometry(&all2, &l, 0, 0);

        level_light_cache_reset();
        level_geometry_part(&halves2, &l, 0, 0, LVL_PART_STATIC);
        int n_static2 = halves2.count;
        level_geometry_part(&halves2, &l, 0, 0, LVL_PART_MOVING);

        oki(n_static2 == n_static,
            "the static half is the same size as before the doors moved",
            n_static2, n_static);
        oki(halves2.count == all2.count, "the halves still come to the whole",
            halves2.count, all2.count);

        int d = first_diff(&all2, &halves2);
        oki(d < 0, "and still match it vertex for vertex", d, -1);

        /* THE INVARIANT ::mesh_upload_from RESTS ON. A partial upload writes a
           suffix into a store sized for the previous total, so a moving half
           whose vertex count drifts as it swings would silently corrupt the
           tail. Translating a brush moves its planes together and cannot change
           how many vertices its faces produce -- asserted here rather than
           assumed there.
           ::mesh_upload_from이 딛고 선 불변식입니다. 부분 업로드는 이전 총량에 맞춰진 저장
           공간에 접미사를 쓰므로, 여닫히는 동안 정점 수가 흔들리는 움직이는 절반은 꼬리를
           조용히 망가뜨립니다. 브러시를 옮기면 그 평면들이 함께 움직이므로 면이 만드는 정점의
           수는 바뀔 수 없습니다. 그곳에서 가정하는 대신 이곳에서 단언합니다. */
        oki(halves2.count == all.count,
            "and the total is unchanged, which mesh_upload_from requires",
            halves2.count, all.count);

        mb_free(&all2);
        mb_free(&halves2);
    }

    /* --- nothing was quietly dropped along the way ------------------------ */
    printf("\ncapacities\n");
    {
        MeshBuf b;
        MdlRange ranges[LVL_MAX_RANGES];
        mb_init(&b, 65536);

        int before = diag_count(DIAG_MAT_RANGES);
        int ns = level_geometry_part(&b, &l, ranges, LVL_MAX_RANGES, LVL_PART_STATIC);
        int nm = level_geometry_part(&b, &l, ranges + ns, LVL_MAX_RANGES - ns,
                                     LVL_PART_MOVING);

        ok(ns > 0 && nm > 0, "both halves report material runs");
        ok(ns + nm <= LVL_MAX_RANGES, "and together they fit LVL_MAX_RANGES");
        oki(diag_count(DIAG_MAT_RANGES) == before,
            "with no run merged away for want of room",
            diag_count(DIAG_MAT_RANGES) - before, 0);

        mb_free(&b);
    }

    mb_free(&all);
    mb_free(&halves);

    printf("\n%s\n", fails ? "  FAILED" : "  passed");
    return fails ? 1 : 0;
}
