/* mapcap -- what a map actually costs, against every cap that could refuse it.
 *
 * WHY THIS EXISTS. The caps in brush.h and level.h are RAM budgets, and until
 * this file there was no way to answer "how much of one does a map use" except
 * by loading it and seeing whether anything looked wrong. That works for the
 * caps that make a load FAIL and not at all for the ones that do not:
 *
 *   BR_MAX_BRUSHES        refuses the brush and reports. Loud.
 *   BR_MAX_TOTAL_FACES    refuses the face and reports. Loud.
 *   LVL_MAX_LIGHTS        drops the light and reports. The room is dimmer.
 *   LVL_MAX_HAZARDS       drops the volume, silently as far as a player can
 *                         tell -- a pool of lava that does not hurt looks
 *                         exactly like a pool that was authored not to.
 *   LEVEL_BUF_VERTS       drops VERTICES. The level loads, collides and plays
 *                         with holes in its walls, and nothing says so.
 *
 * The last one is the reason this is a tool rather than a paragraph. mb_vtx
 * does not grow its buffer -- render.h is explicit that the capacity is "for
 * the buffer's whole life" -- so a level past that cap is a level with pieces
 * missing from the picture and present in the collision.
 *
 * WHAT IT ASSERTS: that every map this project ships fits every cap with
 * headroom, and that the headroom is not zero. A cap a shipped map sits exactly
 * on is a cap the next edit overruns.
 *
 * WHAT IT PRINTS: the usage, so raising a cap is a decision made against a
 * number.
 *
 * MEASURING A MAP THIS PROJECT DOES NOT SHIP takes two steps and is deliberately
 * not a build variant: drop the .map into assets\maps\ (a tool build is
 * HOT_RELOAD, so it is found on disk without a bake) and compile with
 * -DMAPCAP_OVERSIZE plus whatever caps it needs, e.g.
 *
 *     -DBR_MAX_BRUSHES=4096 -DBR_MAX_TOTAL_FACES=16384 -DLEVEL_BUF_VERTS=131072
 *
 * ::PROBE_RANGES is independent of ::LVL_MAX_RANGES, so the run column is
 * measured whatever that cap is set to. There is no $toolVariants entry because
 * a variant that needs three files the repository does not carry is a build
 * that fails for everyone who did not stage them first.
 *
 * 이 프로젝트가 출하하지 않는 맵을 재려면 두 단계가 필요하며, 의도적으로 빌드 변형이 아닙니다.
 * .map을 assets\maps\에 넣고(도구 빌드는 HOT_RELOAD이므로 굽지 않아도 디스크에서 찾습니다),
 * -DMAPCAP_OVERSIZE와 필요한 상한들을 함께 컴파일하십시오. ::PROBE_RANGES는
 * ::LVL_MAX_RANGES와 무관하므로 구간 열은 그 상한이 무엇이든 측정됩니다. $toolVariants 항목이
 * 없는 이유는, 저장소가 지니지 않은 파일 셋을 필요로 하는 변형은 그것들을 먼저 갖다 놓지 않은
 * 모든 사람에게 실패하는 빌드이기 때문입니다.
 *
 * 이 파일이 존재하는 이유. brush.h와 level.h의 상한은 RAM 예산이며, 이 파일이 생기기 전에는
 * "어떤 맵이 그중 얼마를 쓰는가"에 답할 방법이 로드해 보고 이상해 보이는지 확인하는 것뿐이었습니다.
 * 그 방법은 로드를 *실패*시키는 상한에 대해서는 통하고, 그렇지 않은 상한에 대해서는 전혀 통하지
 * 않습니다. 특히 LEVEL_BUF_VERTS는 *정점*을 버립니다. 레벨은 벽에 구멍이 뚫린 채 로드되고
 * 충돌하고 플레이되며, 아무것도 그렇다고 말하지 않습니다.
 */

#include <stdio.h>
#include <time.h>
#include "world.h"
#include "level.h"
#include "brush.h"
#include "render.h"
/* For ::LEVEL_BUF_VERTS, which is the cap this tool exists to measure against.
   It moved out of scene.c to be reachable from here -- a capacity nothing but
   its own file can see is a capacity nothing can check.
   ::LEVEL_BUF_VERTS를 위해서이며, 이 도구가 대조하려고 존재하는 상한입니다. 이곳에서 닿을 수
   있도록 scene.c에서 옮겨 나왔습니다. 자기 파일 말고는 아무도 볼 수 없는 용량은 아무도 검사할
   수 없는 용량입니다. */
#include "scene.h"
#include "diag.h"

static int fails;

/** @brief Whether the last measured level's runs covered its vertices exactly. / 마지막으로 잰 레벨의 구간이 정점을 정확히 덮었는지. */
static int g_partitioned;

static void ok(int cond, const char *what) {
    printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Every map this project ships. Named rather than enumerated because there is
   nothing to enumerate: the maps are baked into one blob and reached by name,
   and data_map takes a name. A list is the honest shape of that.
   이 프로젝트가 출하하는 모든 맵입니다. 열거하지 않고 이름을 적는 이유는 열거할 것이 없기
   때문입니다. 맵은 하나의 블롭에 구워져 이름으로 도달하며, data_map은 이름을 받습니다. 목록이
   그것의 정직한 형태입니다. */
static const char *const MAPS[] = {
    "lqdm4",
#ifdef MAPCAP_OVERSIZE
    /* Maps this project does NOT ship, staged only while the caps are being
       chosen. They are here to be too big: the question a cap answers is "what
       does it refuse", and a sweep over five maps that all fit answers it for
       none of them.
       이 프로젝트가 출하하지 *않는* 맵들이며, 상한을 고르는 동안만 배치합니다. 너무 크라고 있는
       것입니다. 상한이 답하는 질문은 "무엇을 거절하는가"인데, 전부 들어가는 맵 다섯을 훑는 것은
       그중 어느 것에 대해서도 그 질문에 답하지 않습니다. */
    "lqdm2", "lqdm4", "lqdm3", "lqdm11", "lqdm13",
#endif
};

/* One world for the whole sweep. A brush level claims one of LVL_BRUSH_SLOTS
   and KEEPS the claim across loads, so five maps through one World cost one
   slot -- while re-initialising between them abandons a claim each time and the
   third map fails to load. wavetest learned this the same way.
   전체 훑기에 월드 하나입니다. 브러시 레벨은 LVL_BRUSH_SLOTS 중 하나를 주장하고 로드를 거쳐도
   그 주장을 유지하므로, 월드 하나로 도는 맵 다섯은 슬롯 하나가 듭니다. 그 사이에 다시
   초기화하면 매번 주장을 버리게 되고 세 번째 맵이 로드에 실패합니다. wavetest가 같은 방식으로
   이것을 배웠습니다. */
static World W;

/* The geometry a level would hand the renderer, measured without one.
 *
 * ::level_geometry writes into a MeshBuf, and a MeshBuf drops what does not
 * fit. So the buffer here is sized far past ::LEVEL_BUF_VERTS on purpose: what
 * is being measured is what the level WANTS, and a buffer that refused would
 * measure the refusal instead. That is the whole trick -- the shipped scene
 * cannot tell you it lost vertices, because losing them is all it does.
 *
 * 레벨이 렌더러에 건넬 지오메트리를, 렌더러 없이 잽니다.
 *
 * ::level_geometry는 MeshBuf에 쓰고, MeshBuf는 들어가지 않는 것을 버립니다. 그래서 이곳의
 * 버퍼는 의도적으로 ::LEVEL_BUF_VERTS를 한참 넘겨 잡습니다. 재려는 것은 레벨이 *원하는* 양이며,
 * 거절하는 버퍼는 그 거절을 재게 됩니다. 그것이 요령의 전부입니다. 출하되는 장면은 정점을
 * 잃었다고 말해 줄 수 없습니다. 잃는 것이 그것이 하는 일의 전부이기 때문입니다. */
#define PROBE_VERTS   (LEVEL_BUF_VERTS * 8)
#define PROBE_RANGES  4096

/* Do the ranges cover every vertex exactly once?
 *
 * THE CHECK THE REORDERING NEEDED. ::brush_geometry emits one material at a
 * time now rather than one brush at a time, which is a permutation of the same
 * triangles -- and a permutation is exactly the kind of change that can look
 * right in a count and be wrong in a picture. The vertex TOTAL staying at
 * 15,411 says nothing about whether the ranges still point at the right
 * vertices.
 *
 * What must hold is that the runs partition the buffer: sorted by `first`, run
 * n starts where run n-1 ended, the first starts at the block's own start, and
 * the last ends at its end. A run pointing into another run's vertices, a gap
 * nothing draws, or an overlap drawn twice all break it.
 *
 * 구간들이 모든 정점을 정확히 한 번씩 덮는가?
 *
 * *재정렬이 필요로 한 검사입니다.* ::brush_geometry는 이제 브러시가 아니라 재질을 한 번에
 * 하나씩 내보내며, 그것은 같은 삼각형들의 *치환*입니다. 그리고 치환이야말로 개수에서는 옳아
 * 보이고 그림에서는 틀릴 수 있는 종류의 변경입니다. 정점 *합계*가 15,411로 남아 있다는 것은
 * 구간들이 여전히 옳은 정점을 가리키는지에 대해 아무것도 말해 주지 않습니다.
 *
 * 성립해야 하는 것은 구간들이 버퍼를 *분할*한다는 것입니다. `first`로 정렬했을 때 n번 구간은
 * n-1번이 끝난 자리에서 시작하고, 첫 구간은 블록 자신의 시작에서 시작하며, 마지막은 그 끝에서
 * 끝납니다. 다른 구간의 정점을 가리키는 구간, 아무도 그리지 않는 틈, 두 번 그려지는 겹침이
 * 모두 이것을 깨뜨립니다. */
static int ranges_partition(const MdlRange *r, int n, int base, int total) {
    if (n <= 0) return total == 0;

    /* Insertion sort by `first`. The ranges arrive in material order now, which
       is not vertex order, and n is single digits.
       `first`로 삽입 정렬합니다. 구간은 이제 재질 순서로 도착하며 그것은 정점 순서가 아닙니다.
       그리고 n은 한 자릿수입니다. */
    /* PROBE_RANGES, not LVL_MAX_RANGES. The table this walks is the oversized
       probe one, so sizing this from the shipped cap would overrun it by
       exactly the amount that makes the probe worth having.
       LVL_MAX_RANGES가 아니라 PROBE_RANGES입니다. 이것이 훑는 표는 크게 잡은 프로브 쪽이므로,
       출하 상한으로 크기를 정하면 프로브를 가질 가치가 있게 만드는 바로 그만큼 넘칩니다. */
    static int order[PROBE_RANGES];
    if (n > PROBE_RANGES) return 0;
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++) {
        int k = order[i], j = i - 1;
        while (j >= 0 && r[order[j]].first > r[k].first) { order[j + 1] = order[j]; j--; }
        order[j + 1] = k;
    }

    int at = base;
    for (int i = 0; i < n; i++) {
        const MdlRange *x = &r[order[i]];
        if (x->first != at) return 0;
        if (x->count <= 0) return 0;
        at += x->count;
    }
    return at == base + total;
}

static int geometry_verts(const Level *l, int *out_ranges, int *out_split) {
    static MeshBuf probe;
    static int made;
    if (!made) { mb_init(&probe, PROBE_VERTS); made = 1; }
    mb_reset(&probe);

    /* The range table is oversized for the same reason the vertex buffer is:
       ::brush_geometry MERGES a run it has no slot for into the one before it,
       so a table at ::LVL_MAX_RANGES measures the cap and not the map. The
       merge is deliberate -- brush.c says the surplus drawing with the wrong
       texture is visible where a deleted wall would not be -- which makes it
       exactly the kind of refusal that never reaches a player as a bug report.
       구간 표를 크게 잡는 이유는 정점 버퍼와 같습니다. ::brush_geometry는 자리가 없는 구간을
       직전 구간에 *병합*하므로, ::LVL_MAX_RANGES 크기의 표는 맵이 아니라 상한을 재게 됩니다.
       병합은 의도된 것입니다. 초과분이 틀린 텍스처로 그려지는 것은 눈에 보이고 지워진 벽은
       보이지 않는다고 brush.c가 말합니다. 그리고 그 점이 이것을 플레이어에게 결함 보고로 결코
       도달하지 않는 종류의 거절로 만듭니다. */
    static MdlRange ranges[PROBE_RANGES];
    int n = level_geometry(&probe, l, ranges, PROBE_RANGES);
    if (out_ranges) *out_ranges = n;
    int verts = probe.count;

    g_partitioned = ranges_partition(ranges, n, 0, verts);

    /* AND AGAIN THROUGH THE PATH THE GAME ACTUALLY TAKES, gate included.
       ::scene_build_level does not call ::level_geometry on a level with a
       brush door: it asks ::level_geometry_split, and on a yes it calls
       ::level_geometry_part twice. Each call gets its own runs, because
       ::brush_geometry can only group within one call -- and ::level_geometry_part
       itself calls it once per contiguous stretch of brushes on its side of the
       split, so the demand is the distinct materials of each stretch summed,
       not of the level.
       THE GATE IS PART OF THE PATH. The first version of this ran both halves
       unconditionally and reported spire -- which has no door at all -- at
       eighteen runs instead of nine, because a level that does not split
       answers MOVING with everything. It made the three maps with no doors look
       like the expensive ones and the two with doors look free, which is the
       measurement exactly inverted.
       *그리고 게임이 실제로 가는 경로로, 관문까지 포함해 다시 잽니다.* ::scene_build_level은
       브러시 문이 있는 레벨에 ::level_geometry를 부르지 않습니다. ::level_geometry_split에
       묻고, 그렇다는 답이면 ::level_geometry_part를 두 번 부릅니다. 각 호출이 자기 구간
       집합을 갖습니다. ::brush_geometry는 한 호출 안에서만 묶을 수 있기 때문이며,
       ::level_geometry_part 자신도 자기 쪽 브러시의 연속된 덩어리마다 한 번씩 그것을
       부르므로, 요구량은 레벨의 서로 다른 재질 수가 아니라 각 덩어리의 그것을 더한 값입니다.
       *관문도 경로의 일부입니다.* 이 코드의 첫 판은 두 절반을 조건 없이 돌렸고, 문이 아예
       없는 spire를 아홉이 아니라 열여덟 구간으로 보고했습니다. 분할하지 않는 레벨은 MOVING에
       전체로 답하기 때문입니다. 그 결과 문이 없는 맵 셋이 비싼 쪽으로, 문이 있는 맵 둘이
       공짜로 보였고, 그것은 측정이 정확히 뒤집힌 것입니다. */
    if (out_split) {
        if (!level_geometry_split(l)) {
            *out_split = n;
        } else {
            mb_reset(&probe);
            static MdlRange sr[PROBE_RANGES];
            int ns = level_geometry_part(&probe, l, sr, PROBE_RANGES,
                                         LVL_PART_STATIC);
            ns += level_geometry_part(&probe, l, sr + ns, PROBE_RANGES - ns,
                                      LVL_PART_MOVING);
            *out_split = ns;

            /* The two halves have to be the whole, and they have to partition
               it. A split that loses a vertex loses a wall on every level with
               a door, which is every level the player opens one on.
               두 절반은 전체여야 하고, 전체를 분할해야 합니다. 정점을 잃는 분할은 문이 있는
               모든 레벨에서 벽을 잃으며, 그것은 플레이어가 문을 여는 모든 레벨입니다. */
            if (probe.count != verts) g_partitioned = 0;
            if (!ranges_partition(sr, ns, 0, probe.count)) g_partitioned = 0;
        }
    }
    return verts;
}

int main(void) {
    printf("mapcap\n\n");
    printf("caps: brushes %d  faces %d  ents %d  lvlents %d  lights %d\n",
           BR_MAX_BRUSHES, BR_MAX_TOTAL_FACES, BR_MAX_ENTS,
           LVL_MAX_ENTS, LVL_MAX_LIGHTS);
    printf("      verts %d  ranges %d  doors %d  triggers %d  hazards %d  teleports %d\n\n",
           LEVEL_BUF_VERTS, LVL_MAX_RANGES, LVL_MAX_DOORS,
           LVL_MAX_TRIGGERS, LVL_MAX_HAZARDS, LVL_MAX_TELEPORTS);

    /* `rng` is what the game asks for and `split` says how it asked. A level
       with a brush door builds in two halves and pays for the materials of
       each, so the two columns together are the difference between a number
       and the reason for it.
       `rng`는 게임이 요구하는 값이고 `split`은 어떻게 요구했는지를 말합니다. 브러시 문이 있는
       레벨은 두 절반으로 생성되어 각 절반의 재질을 치르므로, 두 열을 함께 보는 것이 숫자와 그
       숫자의 이유의 차이입니다. */
    printf("%-12s %6s %6s %5s %5s %5s %5s %5s %5s %5s %5s %5s\n",
           "map", "brush", "face", "ent", "lvlent", "light", "vert", "rng",
           "split", "door", "hazrd", "tele");
    printf("%s\n", "------------------------------------------------------"
                   "-------------------------");

    int worst_verts = 0, worst_brush = 0, worst_face = 0, worst_ranges = 0;
    const char *worst_name = "";

    /* WHICH MAP RAISED WHICH COUNTER. diag_count is a running total for the
       process, so a sweep that only read it at the end would say "something was
       refused" and leave five candidates. The delta across one load is the
       attribution, and attribution is the whole difference between a counter
       that names a bug and one that names a suspicion.
       THE WINDOW HAS TO BRACKET THE LOAD, and the first version of this took
       the snapshot after it -- so every refusal that happens while PARSING was
       outside the window and the table blamed nobody for them. The counters
       still said something was wrong and every row said no-refusals, which is a
       worse report than no report: it looks like the maps are clean and the
       assertion is broken.
       *어느 맵이 어느 카운터를 올렸는가.* diag_count는 프로세스 전체의 누계이므로, 끝에서만 읽는
       훑기는 "무언가 거절되었다"고 말하고 후보 다섯을 남깁니다. 한 번의 로드에 대한 증분이 곧
       귀속이며, 귀속이야말로 결함을 지목하는 카운터와 의심을 지목하는 카운터의 차이 전부입니다.
       *창은 로드를 감싸야 하며*, 이 코드의 첫 판은 스냅숏을 로드 *뒤에* 찍었습니다. 그래서
       파싱 중에 일어나는 모든 거절이 창 밖에 있었고 표는 그것들에 대해 아무도 지목하지
       않았습니다. 카운터는 여전히 무언가 잘못되었다고 말하는데 모든 행이 였고, 그것은 보고가
       없는 것보다 나쁜 보고입니다. 맵은 깨끗하고 단언이 고장 난 것처럼 보이기 때문입니다. */
    struct { DiagKind k; const char *n; } WATCH[] = {
        { DIAG_BRUSH_CAP,  "brush" }, { DIAG_MAPENT_CAP, "mapent" },
        { DIAG_ENT_CAP,    "ent"   }, { DIAG_LIGHT_CAP,  "light"  },
        { DIAG_MAT_RANGES, "range" }, { DIAG_VERTEX_BUF, "vtx"    },
        { DIAG_SECTOR_CAP, "sector"}, { DIAG_POINT_CAP,  "secpt"  },
    };
    const int NWATCH = (int)(sizeof WATCH / sizeof WATCH[0]);

    for (int i = 0; i < (int)(sizeof MAPS / sizeof MAPS[0]); i++) {
        if (i == 0) { world_init(&W); W.run.title = 0; }

        int before[8];
        for (int k = 0; k < NWATCH; k++) before[k] = diag_count(WATCH[k].k);

        /* WHAT A BIGGER MAP COSTS IN TIME, not just in RAM. The load parses the
           .map, builds the sectors, and BAKES THE LIGHTING -- one ray per vertex
           per light in range -- so it is the one cost that grows with two caps
           at once. RAM is paid whether a map uses it or not; this is paid only
           by the map that needs it, and it is paid where the player is looking
           at a black screen.
           *더 큰 맵이 시간으로 치르는 비용*이며 RAM만이 아닙니다. 로드는 .map을 파싱하고,
           섹터를 만들고, *조명을 굽습니다*. 정점마다 사거리 안의 등마다 광선 하나이므로, 두 상한이
           동시에 키우는 유일한 비용입니다. RAM은 맵이 쓰든 안 쓰든 치르지만, 이것은 그것을
           필요로 하는 맵만 치르고, 플레이어가 검은 화면을 보고 있는 자리에서 치릅니다. */
        clock_t t0 = clock();

        if (!world_load_level(&W, MAPS[i], WORLD_ENTER_NEW)) {
            printf("%-12s  DID NOT LOAD\n", MAPS[i]);
            fails++;
            continue;
        }

        int load_ms = (int)((clock() - t0) * 1000 / CLOCKS_PER_SEC);

        const Level *l = &W.level;
        const BrushMap *bm = l->brushes;


        int ranges = 0, split = 0;
        int verts = geometry_verts(l, &ranges, &split);

        int nb = bm ? bm->n_brushes : 0;
        int nf = bm ? bm->n_faces : 0;
        int ne = bm ? bm->n_ents : 0;

        char refused[128];
        int rp = 0;
        refused[0] = 0;
        for (int k = 0; k < NWATCH; k++) {
            int d = diag_count(WATCH[k].k) - before[k];
            if (!d) continue;
            rp += snprintf(refused + rp, sizeof refused - (size_t)rp,
                           "%s%s=%d", rp ? " " : "", WATCH[k].n, d);
        }

        printf("%-12s %6d %6d %5d %6d %5d %6d %5d %5s %5d %5d %5d %5dms  %s\n",
               MAPS[i], nb, nf, ne, l->n_ents, l->n_lights, verts, split,
               level_geometry_split(l) ? "yes" : "no",
               l->n_doors, l->n_hazards, l->n_teleports, load_ms,
               refused[0] ? refused : "-");
        if (split != ranges)
            printf("    ^ the two halves want %d runs where one build wants %d\n",
                   split, ranges);
        if (split > worst_ranges) worst_ranges = split;

        if (!g_partitioned) {
            printf("    ^ RANGES DO NOT PARTITION THE VERTICES\n");
            fails++;
        }

        if (verts > worst_verts) { worst_verts = verts; worst_name = MAPS[i]; }
        if (nb > worst_brush) worst_brush = nb;
        if (nf > worst_face)  worst_face = nf;
    }

    printf("\nworst case across the shipped maps: %s\n", worst_name);
    printf("  brushes %d/%d  faces %d/%d  verts %d/%d\n\n",
           worst_brush, BR_MAX_BRUSHES, worst_face, BR_MAX_TOTAL_FACES,
           worst_verts, LEVEL_BUF_VERTS);

    /* HEADROOM, not merely "fits". A shipped map sitting exactly on a cap is a
       cap the next edit overruns, and the edit would be an author moving a wall
       rather than anything that looks like a capacity decision.
       *들어감*이 아니라 *여유*입니다. 상한에 정확히 걸터앉은 출하 맵은 다음 수정이 넘기는
       상한이며, 그 수정은 용량에 대한 결정처럼 보이는 무엇이 아니라 제작자가 벽 하나를 옮기는
       일일 것입니다. */
    ok(worst_brush * 4 <= BR_MAX_BRUSHES * 3,
       "the widest map leaves a quarter of BR_MAX_BRUSHES spare");
    ok(worst_face * 4 <= BR_MAX_TOTAL_FACES * 3,
       "and a quarter of BR_MAX_TOTAL_FACES");
    ok(worst_verts * 4 <= LEVEL_BUF_VERTS * 3,
       "and a quarter of LEVEL_BUF_VERTS -- the cap that drops walls silently");
    ok(worst_ranges * 4 <= LVL_MAX_RANGES * 3,
       "and a quarter of LVL_MAX_RANGES, counted through the split path");

    /* Nothing was refused on the way here. A count above is what a map WANTED
       only if nothing clamped it first, so the counters are the check on the
       numbers rather than a separate one.
       이곳에 오는 동안 거절된 것이 없어야 합니다. 위의 수치가 맵이 *원한* 양인 것은 그보다 먼저
       무언가가 그것을 자르지 않았을 때뿐이므로, 카운터는 별개의 검사가 아니라 그 수치에 대한
       검사입니다. */
    printf("\n");
    ok(diag_count(DIAG_MAPENT_CAP) == 0, "no map entity was refused");
    ok(diag_count(DIAG_ENT_CAP)    == 0, "no level entity was refused");
    ok(diag_count(DIAG_LIGHT_CAP)  == 0, "no light was refused");
    ok(diag_count(DIAG_MAT_RANGES) == 0, "no material run was merged away");
    ok(diag_count(DIAG_VERTEX_BUF) == 0, "and the probe buffer never overflowed");

    /* --- a defect this tool found, and that left with the map ------------
     *
     * ::DIAG_BRUSH_CAP counts three different refusals: a brush or face past
     * its pool, a brush past ::BR_MAX_FACES, and a TEXTURE NAME past ::BR_TEX.
     * The first two are caps this tool is about. The third is content, and
     * `spire` -- the level the game used to boot into -- carried 25 of them:
     *
     *     .freedoom-walls/bigwall   19 faces   23 chars
     *     .freedoom-walls/brick      6 faces   21 chars
     *
     * BR_TEX is 16, so both truncated to `.freedoom-walls` and became the SAME
     * material. Neither name was in assets/textures.txt to begin with, which is
     * why the collision was never visible.
     *
     * THE ALLOWANCE WAS `<= 25` AND IS NOW ZERO. `spire` is deleted -- the game
     * ships one map and that is not it -- so the 25 went with it, and a bound
     * of 25 over a corpus that produces none is not tolerance, it is
     * twenty-five free truncations for whatever arrives next. The tolerance
     * existed to pin a known defect without fixing it; the defect is gone, so
     * the pin comes out rather than staying as slack.
     *
     * 이 도구가 찾았고, 그 맵과 함께 떠난 결함입니다.
     *
     * ::DIAG_BRUSH_CAP은 서로 다른 세 거절을 셉니다. 풀을 넘은 브러시나 면, ::BR_MAX_FACES를
     * 넘은 브러시, 그리고 ::BR_TEX를 넘은 *텍스처 이름*입니다. 앞의 둘은 이 도구가 다루는
     * 상한이고, 셋째는 콘텐츠이며, 게임이 부팅해 들어가던 레벨인 `spire`가 그것을 25개
     * 지니고 있었습니다.
     *
     * *허용치는 `<= 25`였고 이제 0입니다.* `spire`는 삭제되었습니다. 게임은 맵 하나를
     * 출하하고 그것은 이 맵이 아닙니다. 그래서 25도 함께 갔고, 하나도 만들지 않는 자료
     * 집합에 대한 25라는 경계는 관용이 아니라 다음에 오는 무엇에게든 주는 공짜 잘림
     * 스물다섯 개입니다. 그 관용은 알려진 결함을 고치지 않은 채 고정해 두려고 있었습니다.
     * 결함이 사라졌으니 고정 핀은 여유로 남는 대신 빠집니다. */
    printf("\n");
    ok(diag_count(DIAG_BRUSH_CAP) == 0,
       "no texture name is truncated, and no brush is past BR_MAX_FACES");

    printf("\n%s\n", fails ? "SOME MAP CAP CHECKS FAILED" : "all map cap checks passed");
    return fails != 0;
}
