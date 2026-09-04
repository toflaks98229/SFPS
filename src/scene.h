/**
 * @file scene.h
 * @brief The per-frame draw passes, and the buffers they reuse.
 *
 * ENGLISH
 * -------
 * Everything here was inline in WinMain. Each pass follows the same three
 * steps the renderer is built around -- build vertices into a ::MeshBuf on the
 * CPU, upload them into a ::Mesh, then select a draw mode and issue the draw --
 * and each was a self-contained block of that shape sitting in the middle of
 * the frame loop. Naming them costs nothing at runtime and makes the loop
 * readable as a sequence of passes rather than as three hundred lines of GL.
 *
 * The buffers live in one struct for a reason beyond tidiness. render.h states
 * that every ::mb_init must be paired with an ::mb_free, and WinMain -- the
 * file every reader copies from when adding a buffer -- initialised six and
 * freed none. Owning them here makes ::scene_init and ::scene_free the pair,
 * so the contract is kept in one place instead of restated at each call site.
 *
 * @note The buffers are reused, never reallocated: each pass calls ::mb_reset
 *       and rebuilds. In steady state a frame performs no heap allocation at
 *       all, which is the property that keeps the frame time flat.
 * @warning Every function here requires a current GL context.
 *
 * 한국어
 * ------
 * 이곳의 모든 것은 원래 WinMain 안에 인라인으로 있었습니다. 각 패스는 렌더러가
 * 기반하는 세 단계를 그대로 따릅니다. CPU에서 정점을 ::MeshBuf에 생성하고, ::Mesh로
 * 업로드한 뒤, 그리기 모드를 선택하고 그리기 명령을 내립니다. 각각은 그 형태를 갖춘
 * 독립적인 블록이었으며 프레임 루프 한가운데에 놓여 있었습니다. 이름을 붙이는 데
 * 런타임 비용은 없으며, 루프를 300줄의 GL 코드가 아니라 패스의 나열로 읽을 수 있게
 * 됩니다.
 *
 * 버퍼를 하나의 구조체에 모은 데에는 정리 이상의 이유가 있습니다. render.h는 모든
 * ::mb_init이 ::mb_free와 짝을 이루어야 한다고 명시하는데, 정작 새 버퍼를 추가하는
 * 사람이 참고하는 WinMain은 여섯 개를 초기화하고 하나도 해제하지 않았습니다. 이곳에서
 * 소유하면 ::scene_init과 ::scene_free가 그 짝이 되므로, 계약이 각 호출 지점마다
 * 반복되지 않고 한 곳에서 지켜집니다.
 *
 * @note 버퍼는 재할당되지 않고 재사용됩니다. 각 패스는 ::mb_reset을 호출한 뒤 다시
 *       생성합니다. 정상 상태에서 한 프레임은 힙 할당을 전혀 수행하지 않으며, 이것이
 *       프레임 시간을 일정하게 유지하는 성질입니다.
 * @warning 이곳의 모든 함수는 활성 GL 컨텍스트를 필요로 합니다.
 */
#ifndef SCENE_H
#define SCENE_H

#include "render.h"
#include "model.h"    /* MdlRange by value -- level.h only forward-declares it */
#include "tex.h"
#include "level.h"
#include "player.h"
#include "weapon.h"
#include "world.h"    /* World: ::scene_frame draws one, and only reads it */
/* StoryPage by value, for ::scene_draw_story. A forward declaration would do
   for the pointer, and would leave the caller unable to reach the lines it is
   asking to have drawn -- the page IS the argument, so its shape belongs here.
   ::scene_draw_story를 위한 값 타입 StoryPage입니다. 포인터에는 전방 선언으로 충분하지만,
   그러면 호출자는 그리라고 요청하는 대상의 줄들에 닿을 수 없습니다. 페이지가 곧 인자이므로 그
   모양은 이곳에 속합니다. */
#include "story.h"
#include "weaponview.h" /* WeaponView: the drawn gun, owned here beside the atlases */

/* Vertices the level's scratch buffer holds. NOT a starting size -- ::mb_init
   takes a capacity "for the buffer's whole life", and ::mb_vtx DROPS vertices
   rather than growing. This comment used to say "starts at" and "large enough
   that a hand-authored level never grows it", and both halves were wrong in a
   way that matters: nothing grows, and the levels that test this are the ones
   nobody here authored. A map past this cap loads, collides and plays with
   holes in its walls.
   Overridable with the brush caps, because it is the same question asked one
   stage later -- brush.h decides whether the map parses, this decides whether
   what parsed can be drawn. tools/mapcap.c measures both.
   레벨 임시 버퍼가 담는 정점 수입니다. *시작 크기가 아닙니다.* ::mb_init은 "버퍼의 전 생애에
   걸친" 용량을 받고 ::mb_vtx는 확장하는 대신 정점을 *버립니다*. 이 주석은 "시작 크기"이며
   "사람이 제작한 레벨이 확장시키지 않을 만큼 충분하다"고 말했는데, 두 절반 모두 중요한 방식으로
   틀렸습니다. 확장하는 것은 없고, 이것을 시험하는 레벨은 이곳의 누구도 제작하지 않은 것들입니다.
   이 상한을 넘는 맵은 벽에 구멍이 뚫린 채로 로드되고 충돌하고 플레이됩니다.
   브러시 상한과 함께 재정의할 수 있습니다. 한 단계 뒤에서 묻는 같은 질문이기 때문입니다.
   brush.h는 맵이 파싱되는지를 정하고, 이것은 파싱된 것이 그려질 수 있는지를 정합니다.
   tools/mapcap.c가 둘 다 잽니다. */
/* WHY 49152. tools/mapcap.c measured lqdm11 wanting 15,411 vertices against
   the old 16,384 -- 94% consumed, 973 short of losing walls, in a map that
   shipped. Three times the widest shipped map, chosen the way every other cap
   in this rewrite was: from the content, not from the largest thing that could
   be made to fit.
   THE ONLY ONE OF THESE THAT IS HEAP RATHER THAN .bss, at 44 bytes a vertex --
   ::mb_init allocates once and ::scene_free gives it back. 2.1MB, paid whether
   the level is spire (612 verts) or lqdm11, which is the argument for measuring
   before tripling rather than after.

   RAISED TO 65536 WHEN THE ARENA BECAME `lqdm4`. Psychofuge wants 42,267
   vertices against lqdm11's 15,411 -- 2.7x the map this cap was measured from,
   and 86% of the old 49152, which tools/mapcap.c refuses because it asks for a
   quarter spare rather than a fourteenth.
   AND THE `THREE TIMES` RULE ABOVE IS NOT APPLIED AGAIN, deliberately. Tripling
   42,267 gives 131,072 and 5.8MB of heap paid on every level including the ones
   with six hundred vertices, to hold room for a map half again bigger than
   anything that has ever shipped here. The rule earned that multiple when the
   widest map was fifteen thousand vertices and the whole buffer was 700KB; at
   this size the same multiple buys the same insurance for eight times the
   premium. 65536 is the enforced margin -- mapcap's quarter -- with the round
   number above it, and it is 2.9MB.
   If a wider map than Psychofuge arrives, this moves again, and mapcap is what
   will say so rather than a wall quietly going missing.
   *왜 49152인가.* tools/mapcap.c가 lqdm11이 옛 16,384에 대해 정점 15,411개를 원한다고
   측정했습니다. 94% 소비, 벽을 잃기까지 973개를 남긴 상태로 출하된 맵입니다. 가장 넓은 출하
   맵의 세 배이며, 이 재작성의 다른 모든 상한과 같은 방식으로 골랐습니다. 들어맞게 만들 수 있는
   가장 큰 것이 아니라 콘텐츠에서 정했습니다.
   *이 중 유일하게 .bss가 아니라 힙이며* 정점당 44바이트입니다. ::mb_init이 한 번 할당하고
   ::scene_free가 돌려줍니다. 2.1MB이고, 레벨이 spire(정점 612개)이든 lqdm11이든 치릅니다.
   그것이 세 배로 늘리기 전에 재야 한다는 논거이지 늘린 뒤에 잴 이유가 아닙니다. */
#ifndef LEVEL_BUF_VERTS
#define LEVEL_BUF_VERTS 65536
#endif

/**
 * @struct Scene
 * @brief The buffers, meshes and atlases the per-frame passes reuse.
 *
 * ENGLISH
 * -------
 * @warning Owns heap memory and GL objects. Pair ::scene_init with
 *          ::scene_free.
 * @note One ::MeshBuf and one ::Mesh per pass rather than one shared pair: the
 *       passes upload with different vertex counts and the sprite passes bind
 *       different atlases, so sharing would mean re-uploading whatever the
 *       previous pass left behind.
 *
 * 한국어
 * ------
 * 프레임별 패스가 재사용하는 버퍼, 메시, 아틀라스입니다.
 * @warning 힙 메모리와 GL 객체를 소유합니다. ::scene_init과 ::scene_free를 짝지으십시오.
 * @note 하나의 쌍을 공유하지 않고 패스마다 ::MeshBuf와 ::Mesh를 따로 두었습니다. 각
 *       패스는 서로 다른 정점 수로 업로드하고 스프라이트 패스는 서로 다른 아틀라스를
 *       바인딩하므로, 공유하면 이전 패스가 남긴 것을 다시 업로드해야 합니다.
 */
typedef struct {
    MeshBuf enemy_buf,  pickup_buf,  shot_buf,  hud_buf;   /**< CPU-side builders. / CPU 측 빌더. */
    Mesh    enemy_mesh, pickup_mesh, shot_mesh, hud_mesh;  /**< Their GPU counterparts. / 대응하는 GPU 측 메시. */
    MeshBuf beam_buf;                                       /**< The ward beams, see ::scene_draw_beams. / 결계 빔. ::scene_draw_beams 참조. */
    Mesh    beam_mesh;
    MeshBuf emb_buf;                                        /**< Weapon pickups, drawn from the emblem atlas. / 문양 아틀라스에서 그리는 무기 아이템. */
    Mesh    emb_mesh;


    GLuint  sprite_tex;   /**< Monster atlas. / 몬스터 아틀라스. */
    GLuint  pickup_tex;   /**< Pickup atlas. / 아이템 아틀라스. */

    /* --- the level's own geometry ---
       Rebuilt by ::scene_build_level whenever the level changes: at startup,
       on an exit transition, and on a hot reload. Those three sites used to
       repeat the same four steps, and a fix applied to one of them was a fix
       applied to one of them.
       레벨이 바뀔 때마다 ::scene_build_level이 재생성합니다. 시작 시, 출구 전환 시,
       핫 리로드 시입니다. 이 세 지점은 동일한 네 단계를 반복하고 있었으며, 그중 하나를
       고치는 것은 말 그대로 하나만 고치는 것이었습니다. */
    MeshBuf  level_buf;                 /**< Scratch the level geometry is built into. / 레벨 지오메트리를 생성하는 임시 버퍼. */
    Mesh     level_mesh;                /**< The uploaded level. / 업로드된 레벨. */
    MdlRange level_ranges[LVL_MAX_RANGES]; /**< One run per material. / 재질별 구간. */
    Mat      level_tex[LVL_MAX_RANGES];    /**< The material each run draws with. / 각 구간이 사용하는 재질. */
    int      level_range_count;         /**< Runs in use. / 사용 중인 구간의 수. */

    /**
     * @brief Where the moving half begins, in vertices and in runs.
     *
     * ENGLISH
     * -------
     * ::scene_build_level builds the static half first and the moving half
     * after it, so everything a door touches is a SUFFIX of one buffer and one
     * range table. These two are that boundary, and they are what
     * ::scene_rebuild_moving truncates back to.
     *
     * Both are 0 on a level that does not split (see ::level_geometry_split),
     * which makes the moving half the whole level and a rebuild the whole
     * rebuild -- the behaviour every level had before the split existed.
     *
     * 한국어
     * ------
     * @brief 움직이는 절반이 시작하는 지점. 정점 단위와 구간 단위 양쪽.
     *
     * ::scene_build_level은 정적인 절반을 먼저, 움직이는 절반을 그 뒤에 생성하므로, 문이
     * 건드리는 모든 것이 하나의 버퍼와 하나의 구간 표의 *접미사*가 됩니다. 이 둘이 그 경계이며,
     * ::scene_rebuild_moving이 되돌리는 지점입니다.
     *
     * 분할되지 않는 레벨(::level_geometry_split 참조)에서는 둘 다 0이며, 그러면 움직이는 절반이
     * 곧 레벨 전체이고 재생성이 곧 전체 재생성입니다. 분할이 존재하기 전 모든 레벨의 동작과
     * 같습니다.
     */
    int      level_static_verts;
    int      level_static_ranges;

    /**
     * @brief The drawn gun: its mesh, materials and the buffers they build in.
     *
     * ENGLISH: Owned here rather than by ::World because it is one gun per GL
     * context, exactly like ::sprite_tex above -- two runs in one process draw
     * the same shotgun. It was file-scope state in weapon.c, which is why that
     * file needed a context and ::world_init could not call ::wp_init.
     *
     * 한국어: ::World가 아니라 이곳이 소유하는 이유는 이것이 GL 컨텍스트당 총 하나이기
     * 때문이며, 위의 ::sprite_tex와 정확히 같습니다. 한 프로세스의 두 플레이는 같은
     * 샷건을 그립니다. 이전에는 weapon.c의 파일 스코프 상태였고, 그래서 그 파일이
     * 컨텍스트를 필요로 했으며 ::world_init이 ::wp_init을 호출할 수 없었습니다.
     */
    WeaponView wpview;
} Scene;

/* --- Lifecycle / 수명 주기 --- */

/**
 * @brief Allocates the per-pass buffers and builds the sprite atlases.
 *
 * ENGLISH
 * -------
 * @param[out] s Scene to initialise.
 * @note Each buffer is sized from the cap of what it draws, so a full level
 *       never grows one mid-frame: ::ENEMY_MAX monsters at six vertices each,
 *       and so on. A buffer that had to grow would allocate during a frame.
 * @warning Requires a current GL context: the atlases are uploaded here.
 *
 * 한국어
 * ------
 * @brief 패스별 버퍼를 할당하고 스프라이트 아틀라스를 생성합니다.
 * @param[out] s 초기화할 장면.
 * @note 각 버퍼는 자신이 그리는 대상의 상한을 기준으로 크기가 정해지므로, 가득 찬
 *       레벨에서도 프레임 도중에 확장되지 않습니다. ::ENEMY_MAX 마리의 몬스터에 각
 *       정점 6개를 곱하는 식입니다. 확장이 필요한 버퍼는 프레임 도중에 할당을
 *       수행하게 됩니다.
 * @warning 활성 GL 컨텍스트가 필요합니다. 아틀라스가 이곳에서 업로드됩니다.
 */
void scene_init(Scene *s, Weapon *w);

/**
 * @brief Releases the buffers ::scene_init allocated.
 *
 * ENGLISH
 * -------
 * @param[in,out] s Scene to release. Safe to call twice.
 * @note This is the ::mb_free half of render.h's contract for all four
 *       buffers. The GL atlases are owned by sprite.c and are not freed here.
 *
 * 한국어
 * ------
 * @brief ::scene_init이 할당한 버퍼를 해제합니다.
 * @param[in,out] s 해제할 장면. 두 번 호출해도 안전합니다.
 * @note 네 개 버퍼 모두에 대해 render.h 계약의 ::mb_free 쪽을 담당합니다. GL
 *       아틀라스는 sprite.c가 소유하므로 이곳에서 해제하지 않습니다.
 */
void scene_free(Scene *s);

/* --- Level geometry / 레벨 지오메트리 --- */

/**
 * @brief Rebuilds the level's geometry and materials from a parsed level.
 *
 * ENGLISH
 * -------
 * @param[in,out] s       Scene receiving the geometry.
 * @param[in]     l       Level to build.
 * @param[in]     dynamic Non-zero when replacing geometry already uploaded --
 *                        a transition or a hot reload -- so the existing
 *                        allocation is reused. Zero on the first build.
 * @note The four steps -- build, upload, resolve materials, record the run
 *       count -- have to happen together and in this order. They were repeated
 *       at three call sites, which is why ::LVL_MAX_RANGES being too small
 *       could be fixed in one of them and still drop walls in the other two.
 * @warning Requires a current GL context.
 *
 * 한국어
 * ------
 * @brief 파싱된 레벨로부터 지오메트리와 재질을 다시 생성합니다.
 * @param[in,out] s       지오메트리를 받을 장면.
 * @param[in]     l       생성할 레벨.
 * @param[in]     dynamic 이미 업로드된 지오메트리를 교체하는 경우(전환 또는 핫 리로드)
 *                        0이 아닌 값을 전달하여 기존 할당을 재사용합니다. 최초
 *                        생성 시에는 0입니다.
 * @note 생성, 업로드, 재질 해석, 구간 수 기록의 네 단계는 반드시 함께, 이 순서로
 *       수행되어야 합니다. 세 곳의 호출 지점에서 반복되고 있었으며, 그래서
 *       ::LVL_MAX_RANGES가 너무 작다는 문제를 한 곳에서 고쳐도 나머지 두 곳에서는
 *       여전히 벽이 누락될 수 있었습니다.
 * @warning 활성 GL 컨텍스트가 필요합니다.
 */
void scene_build_level(Scene *s, const Level *l, int dynamic);

/**
 * @brief Rebuilds only the geometry a door moves, and re-sends only that.
 *
 * ENGLISH
 * -------
 * The per-frame half of ::scene_build_level. The static half is left exactly
 * as it was -- not rebuilt, not re-baked and not re-uploaded -- and the moving
 * half is built again from where the doors are now.
 *
 * @param[in,out] s Scene holding the level built by ::scene_build_level.
 * @param[in]     l The same level, with its doors moved.
 * @note FALLS BACK TO A WHOLE REBUILD rather than refusing, in the two cases it
 *       cannot do the cheap thing: a level that does not split, and a moving
 *       half whose vertex count changed under it. Both leave the screen correct
 *       and merely cost what the frame used to cost, which is the right way for
 *       an optimisation to fail.
 * @warning Requires a current GL context, and requires ::scene_build_level to
 *          have run for this level first. Calling it for a level the scene has
 *          not built rebuilds the wrong geometry, not nothing.
 *
 * 한국어
 * ------
 * @brief 문이 움직이는 지오메트리만 다시 만들고, 그것만 다시 보냅니다.
 *
 * ::scene_build_level의 프레임별 절반입니다. 정적인 절반은 있던 그대로 둡니다. 다시 만들지도,
 * 다시 굽지도, 다시 올리지도 않습니다. 움직이는 절반만 문이 지금 있는 자리를 기준으로 다시
 * 만듭니다.
 *
 * @param[in,out] s ::scene_build_level이 생성한 레벨을 보유한 장면.
 * @param[in]     l 문이 움직인, 그 같은 레벨.
 * @note 값싼 경로를 택할 수 없는 두 경우에 거절하는 대신 *전체 재생성으로 되돌아갑니다.*
 *       분할되지 않는 레벨, 그리고 움직이는 절반의 정점 수가 바뀐 경우입니다. 둘 다 화면을
 *       올바르게 유지하며 단지 이전에 치르던 비용을 치를 뿐입니다. 최적화가 실패해야 하는
 *       올바른 방식입니다.
 * @warning 활성 GL 컨텍스트가 필요하며, 이 레벨에 대해 ::scene_build_level이 먼저 실행되어
 *          있어야 합니다. 장면이 생성한 적 없는 레벨에 대해 호출하면 아무 일도 없는 것이 아니라
 *          엉뚱한 지오메트리를 다시 만듭니다.
 */
void scene_rebuild_moving(Scene *s, const Level *l);

/* --- One frame, in one function / 한 프레임을 담은 하나의 함수 --- */

/**
 * @brief Draws one frame of a ::World, world first and UI second. The whole
 *        per-frame draw in one function, so that its ORDER is a thing a test
 *        can reach.
 *
 * ENGLISH
 * -------
 * The counterpart of ::world_step, and it exists for the same reason. That
 * order used to live in the body of main.c's `frame_draw`, where main.c's own
 * header comment declared it load-bearing -- the pass boundary the gun sits
 * above, the culling that comes off for exactly two billboard passes, the snap
 * that must be released before any text -- and then nothing checked any of it,
 * because the only way to run it was to open a window and play.
 *
 * Every reason for the order below is written at the line it governs. A caller
 * gets the whole sequence or none of it; there is no way to draw the world and
 * forget the resolve, because the resolve is not the caller's to remember.
 *
 * @param[in]     w      The world to draw. Read only: drawing decides nothing.
 * @param[in,out] sc     The draw passes and the buffers they build into.
 * @param[in]     vw     Client width, pixels.
 * @param[in]     vh     Client height, pixels. At least 1.
 * @param[in]     frozen What ::world_step returned. NOT re-derived here -- the
 *                       step may have set `dead` this very frame, and asking
 *                       again would hide the crosshair one frame before the
 *                       death screen it belongs to appears.
 *
 * @note ::post_end is the world/UI boundary and everything about the order here
 *       is arranged around it. Above it the frame goes through the pixelise and
 *       dither pass; below it, nothing does -- 5x7 glyphs magnified four times
 *       are unreadable and dithered text is worse. The gun is deliberately
 *       ABOVE the line: it is part of the scene's lighting, and a crisp weapon
 *       over a pixelated world reads as a bug. diag.h's DIAG_PASS_ORDER watches
 *       this at runtime, and now watches a sequence a test can drive.
 * @warning Requires a current GL context. This is the one thing that keeps the
 *          drawing side from being as headless as ::world_step: a pass order
 *          can be tested, but only against a context something else created.
 *
 * 한국어
 * ------
 * @brief ::World의 한 프레임을 그립니다. 월드가 먼저, UI가 나중입니다. 프레임별 그리기
 *        전체를 담은 하나의 함수이며, 그 *순서*를 테스트가 도달할 수 있는 것으로 만들기
 *        위함입니다.
 *
 * ::world_step의 짝이며, 존재하는 이유도 같습니다. 그 순서는 이전에 main.c의
 * `frame_draw` 본문 안에 있었고, main.c 자신의 헤더 주석은 그것을 구조적으로 중요하다고
 * 선언했습니다. 총기가 그 위에 놓이는 패스 경계, 정확히 두 개의 빌보드 패스를 위해 꺼지는
 * 컬링, 어떤 텍스트보다 먼저 해제되어야 하는 스냅. 그런데 그중 어느 것도 검사되지
 * 않았습니다. 실행하는 유일한 방법이 창을 열고 플레이하는 것이었기 때문입니다.
 *
 * 아래 순서의 모든 근거는 그것이 지배하는 줄에 적혀 있습니다. 호출자는 전체 순서를 얻거나
 * 아무것도 얻지 못합니다. 월드를 그리고 해상을 잊는 방법은 없습니다. 해상은 호출자가
 * 기억할 몫이 아니기 때문입니다.
 *
 * @param[in]     w      그릴 월드. 읽기 전용입니다. 그리기는 아무것도 결정하지 않습니다.
 * @param[in,out] sc     드로우 패스와 그것들이 사용하는 버퍼.
 * @param[in]     vw     클라이언트 영역 너비 (픽셀).
 * @param[in]     vh     클라이언트 영역 높이 (픽셀). 최소 1입니다.
 * @param[in]     frozen ::world_step이 반환한 값입니다. 이곳에서 다시 유도하지 *않습니다*.
 *                       갱신이 바로 이번 프레임에 `dead`를 설정했을 수 있으며, 다시 물으면
 *                       그에 해당하는 사망 화면이 나타나기 한 프레임 전에 조준점이
 *                       사라집니다.
 *
 * @note ::post_end가 월드와 UI의 경계이며 이곳 순서의 모든 것이 그것을 중심으로 배치되어
 *       있습니다. 그 위쪽은 픽셀화와 디더 패스를 거치고, 아래쪽은 거치지 않습니다. 5x7
 *       글리프를 4배 확대하면 읽을 수 없고 디더링된 텍스트는 더 나쁩니다. 총기는 의도적으로
 *       경계 *위쪽*에 있습니다. 총기는 장면 조명의 일부이며, 픽셀화된 월드 위의 선명한
 *       무기는 버그처럼 보입니다. diag.h의 DIAG_PASS_ORDER가 이를 런타임에 감시하며, 이제
 *       테스트가 구동할 수 있는 순서를 감시합니다.
 * @warning 활성 GL 컨텍스트가 필요합니다. 그리기 쪽이 ::world_step만큼 헤드리스가 되지
 *          못하는 유일한 이유입니다. 패스 순서는 테스트할 수 있지만, 다른 무언가가 만든
 *          컨텍스트에 대해서만 가능합니다.
 */
void scene_frame(const World *w, Scene *sc, int vw, int vh, int frozen);

/**
 * @brief Draws the level: one textured run per material.
 *
 * ENGLISH
 * -------
 * @param[in] s   Scene holding the built level.
 * @param[in] vp  Combined view-projection matrix.
 * @param[in] eye Camera position, for lighting and fog.
 * @note The level is no longer a parameter. It was here to supply the point
 *       lights, and ::scene_lights uploads every light ONCE for the whole
 *       frame before any pass runs, so by the time this is called they are
 *       already in the shader. The only thing this needs from a Level is the
 *       mesh built from it, which the Scene already holds.
 *
 * 한국어
 * ------
 * @brief 레벨을 그립니다. 재질마다 하나의 텍스처 구간을 그립니다.
 * @param[in] s   생성된 레벨을 보유한 장면.
 * @param[in] vp  뷰-투영 결합 행렬.
 * @param[in] eye 조명과 안개 계산에 사용되는 카메라 위치.
 * @note 레벨은 더 이상 인자가 아닙니다. 점광원을 제공하기 위해 있었는데, ::scene_lights가
 *       어떤 패스가 돌기도 전에 프레임 전체에 대해 모든 광원을 *한 번* 업로드하므로 이
 *       함수가 호출될 시점에는 이미 셰이더 안에 들어 있습니다. 이 함수가 Level로부터 필요로
 *       하는 유일한 것은 그것으로 만들어진 메시이며, 그것은 Scene이 이미 보유하고 있습니다.
 */
void scene_draw_level(const Scene *s, mat4 vp, v3 eye);


/* --- World passes: before post_end / 월드 패스: post_end 이전 --- */

/**
 * @brief Draws the monsters as alpha-tested, camera-facing billboards.
 *
 * ENGLISH
 * -------
 * @param[in,out] s         Scene supplying the buffer and atlas.
 * @param[in]     vp        Combined view-projection matrix.
 * @param[in]     eye       Camera position, for fog.
 * @param[in]     cam_right Camera right basis vector.
 * @note One draw call per monster, because each carries its own tint: a hit
 *       flashes white and a corpse fades and sinks. At a few dozen monsters
 *       the uniform change costs less than the per-vertex colour that would
 *       let them batch.
 * @note Culling is disabled for the pass -- a billboard is single-sided and
 *       may face either way -- and restored before returning.
 *
 * 한국어
 * ------
 * @brief 몬스터를 알파 테스트된 카메라 지향 빌보드로 그립니다.
 * @param[in,out] s         버퍼와 아틀라스를 제공하는 장면.
 * @param[in]     vp        뷰-투영 결합 행렬.
 * @param[in]     eye       안개 계산에 사용되는 카메라 위치.
 * @param[in]     cam_right 카메라의 우측 기저 벡터.
 * @note 몬스터마다 고유한 색조를 가지므로 그리기 호출도 한 번씩 발생합니다. 피격 시
 *       흰색으로 번쩍이고 시체는 어두워지며 가라앉습니다. 몇십 마리 수준에서는 유니폼
 *       변경 비용이, 일괄 처리를 가능하게 할 정점별 색상보다 저렴합니다.
 * @note 빌보드는 단면이며 어느 쪽을 향할지 알 수 없으므로 이 패스 동안 컬링을
 *       비활성화하고, 반환 전에 복원합니다.
 */
void scene_draw_enemies(Scene *s, const Pools *pl, mat4 vp, v3 eye, v3 cam_right);

/* --- the ward beams --------------------------------------------------------
 *
 * WHAT TIES THE WARDS TO THE BOSS, on screen. The rule is already in enemy.c
 * -- the boss cannot be hurt while a ward stands -- but a rule nobody can see
 * is a boss that seems to ignore damage for no reason. So each standing ward
 * runs a beam from its gem to the boss's centre, the way the End's crystals
 * feed its dragon: break the ward and the beam is gone with it, and the boss
 * visibly loses a line.
 *
 * ATTACHED AT THE GEM BY THE DRAWING, not by a number here. The ward's art
 * carries a marker pixel at the gem and ::sprite_anchor turns it into a
 * fraction of the sprite's height, so the beam stays on the gem through a
 * resize of the ward or a redraw of the pillar. A kind with no marker gets
 * the top of its sprite.
 *
 * THREE ADDITIVE RIBBONS, the same core / halo / glow the bolts are built
 * from, in the ward's cold blue so it reads as the ward feeding the boss and
 * not the boss firing at the ward. Pulsed off the boss's own clock: one heart
 * for every line.
 *
 * *결계석을 보스에 묶는 것*을 화면에 보입니다. 규칙은 이미 enemy.c에 있습니다. 결계석이
 * 서 있는 동안 보스는 다치지 않습니다. 그러나 아무도 볼 수 없는 규칙은 이유 없이 피해를
 * 무시하는 것처럼 보이는 보스입니다. 그래서 서 있는 결계석마다 자기 보석에서 보스의
 * 중심으로 빔을 냅니다. 엔드의 수정이 드래곤을 먹이는 방식입니다. 결계석을 부수면 빔도
 * 함께 사라지고, 보스는 눈에 보이게 선 하나를 잃습니다.
 * *보석에 붙는 것은 그림이 정합니다*, 이곳의 숫자가 아니라. 결계석의 아트는 보석에 표식
 * 픽셀을 지니고 ::sprite_anchor가 그것을 스프라이트 높이의 비율로 바꾸므로, 결계석의
 * 크기 변경이나 기둥의 다시 그리기를 지나도 빔은 보석 위에 남습니다. 표식이 없는 종류는
 * 스프라이트의 꼭대기를 받습니다.
 * *가산 리본 셋*이며, 볼트가 만들어지는 것과 같은 심/후광/발광입니다. 결계석의 차가운
 * 파랑이므로 보스가 결계석을 쏘는 것이 아니라 결계석이 보스를 먹이는 것으로 읽힙니다.
 * 보스 자신의 시계로 맥동합니다. 모든 선에 하나의 심장입니다. */
#define BEAM_GLOW_W   0.40f   ///< @brief Outer ribbon width, metres. / 바깥 리본 너비(미터).
#define BEAM_HALO_W   0.20f   ///< @brief Middle ribbon width, metres. / 가운데 리본 너비(미터).
#define BEAM_CORE_W   0.06f   ///< @brief Core ribbon width, metres. / 심 리본 너비(미터).
#define BEAM_PULSE_RATE 5.0f  ///< @brief Radians per second of ::Enemy::anim. / ::Enemy::anim 초당 라디안.
void scene_draw_beams(Scene *s, const Pools *pl, mat4 vp, v3 eye);

/* --- weapon pickups: the magic circle on the floor -------------------------
 *
 * A weapon lying on the floor is its magic circle, drawn from the SAME atlas
 * the wand draws it from: row 0 the ring, row 1 the gem, two billboards over
 * one point. The ring turns at the weapon's own idle rate -- ::WPN_SPIN_RATE
 * over its cooldown, the rate the wand shows when it is not firing -- and the
 * gem holds still, so the floor and the hand agree about what this thing is.
 *
 * Rotated by turning the billboard's basis, not its corners: the quad is
 * square and `right`/`up` are orthonormal, so there is no shear to fall into.
 *
 * ::scene_draw_pickups skips these kinds; their cells in the pickup atlas are
 * not drawn by anything.
 *
 * 바닥에 놓인 무기는 그 마법진이며, 지팡이가 그리는 *같은* 아틀라스에서 그립니다. 0번 줄이
 * 고리, 1번 줄이 보석이고, 한 점 위에 빌보드 둘입니다. 고리는 무기 자신의 휴지 속도로
 * 돕니다. ::WPN_SPIN_RATE를 쿨다운으로 나눈 값이며, 지팡이가 쏘지 않을 때 보이는 속도입니다.
 * 보석은 가만히 있으므로 바닥과 손이 이것이 무엇인지에 대해 일치합니다.
 * 모서리가 아니라 빌보드의 기저를 돌려 회전합니다. 사각형이 정사각형이고 `right`/`up`이
 * 정규직교이므로 빠질 전단이 없습니다.
 * ::scene_draw_pickups는 이 종류들을 건너뛰며, 아이템 아틀라스의 그 칸은 무엇도 그리지
 * 않습니다. */
#define PICKUP_BOB_RATE 2.2f      ///< @brief Radians per second of ::Pickup::anim for the bob; public so a test can hold the bob still. / 보브에 대한 ::Pickup::anim의 초당 라디안. 검사가 보브를 고정할 수 있도록 공개.
#define PICKUP_EMBLEM_SIZE 1.6f   ///< @brief Billboard edge for a floor circle, metres. / 바닥 마법진 빌보드의 변(미터).
void scene_draw_weapon_pickups(Scene *s, const Pools *pl, mat4 vp, v3 eye, v3 cam_right);

/**
 * @brief Draws the pickups as bobbing billboards on the same sprite path.
 *
 * ENGLISH
 * -------
 * @param[in,out] s         Scene supplying the buffer and atlas.
 * @param[in]     vp        Combined view-projection matrix.
 * @param[in]     eye       Camera position, for fog.
 * @param[in]     cam_right Camera right basis vector.
 * @note A single draw call, unlike the monsters: pickups share one tint, so
 *       nothing forces them apart.
 *
 * 한국어
 * ------
 * @brief 아이템을 위아래로 흔들리는 빌보드로 동일한 스프라이트 경로에서 그립니다.
 * @param[in,out] s         버퍼와 아틀라스를 제공하는 장면.
 * @param[in]     vp        뷰-투영 결합 행렬.
 * @param[in]     eye       안개 계산에 사용되는 카메라 위치.
 * @param[in]     cam_right 카메라의 우측 기저 벡터.
 * @note 몬스터와 달리 그리기 호출이 한 번입니다. 아이템은 색조를 공유하므로 나눌
 *       이유가 없습니다.
 */
void scene_draw_pickups(Scene *s, const Pools *pl, mat4 vp, v3 eye, v3 cam_right);

/**
 * @brief Draws monster projectiles as additive, untextured billboard rosettes.
 *
 * ENGLISH
 * -------
 * @param[in,out] s         Scene supplying the buffer.
 * @param[in]     vp        Combined view-projection matrix.
 * @param[in]     cam_right Camera right basis vector.
 * @param[in]     cam_up    Camera up basis vector.
 * @note A bolt is a light source, so it is drawn the way the muzzle flash is:
 *       several camera-facing quads rotated against each other and blended
 *       additively. One quad reads as a glowing SQUARE; overlapping them gives
 *       a rosette that brightens toward the middle and reads round, for no
 *       texture and no new geometry helper.
 * @note Writes no depth -- a glow does not occlude -- and restores the depth
 *       mask, blending and culling before returning.
 *
 * 한국어
 * ------
 * @brief 몬스터 발사체를 텍스처 없는 가산 블렌드 빌보드 로제트로 그립니다.
 * @param[in,out] s         버퍼를 제공하는 장면.
 * @param[in]     vp        뷰-투영 결합 행렬.
 * @param[in]     cam_right 카메라의 우측 기저 벡터.
 * @param[in]     cam_up    카메라의 상향 기저 벡터.
 * @note 볼트는 광원이므로 총구 화염과 동일한 방식으로 그립니다. 카메라를 향하는 여러
 *       사각형을 서로 회전시켜 가산 블렌딩합니다. 사각형 하나는 빛나는 *정사각형*으로
 *       보이지만, 겹치면 가운데로 갈수록 밝아지는 로제트가 되어 둥글게 보입니다.
 *       텍스처도 새로운 지오메트리 헬퍼도 필요하지 않습니다.
 * @note 깊이를 기록하지 않으며(발광체는 가리지 않습니다) 반환 전에 깊이 마스크,
 *       블렌딩, 컬링을 복원합니다.
 */
void scene_draw_shots(Scene *s, const Pools *pl, mat4 vp, v3 cam_right, v3 cam_up);

/**
 * @brief Draws the player's grenades and bolts.
 *
 * @note Reuses the shot buffer: the two passes never overlap in a frame, and
 *       a second buffer of the same size would be memory held for nothing.
 * @note A grenade reddens as its fuse burns, which is the only warning the
 *       player gets that one at their feet is about to go off.
 *
 * @brief 플레이어의 유탄과 탄을 그립니다.
 * @note 발사체 버퍼를 재사용합니다. 두 패스가 한 프레임 안에서 겹치지 않으며, 같은 크기의
 *       두 번째 버퍼는 아무것도 아닌 것을 위해 붙잡아 두는 메모리입니다.
 * @note 유탄은 도화선이 타면서 붉어집니다. 발밑의 유탄이 곧 터진다는 것에 대해 플레이어가
 *       받는 유일한 경고입니다.
 */
void scene_draw_proj(Scene *s, const Pools *pl, mat4 vp, v3 cam_right, v3 cam_up);

/* --- UI passes: after post_end / UI 패스: post_end 이후 --- */

/**
 * @brief Draws the hurt flash and the health and ammo readouts.
 *
 * ENGLISH
 * -------
 * @param[in,out] s  Scene supplying the buffer.
 * @param[in]     vw Viewport width in pixels.
 * @param[in]     vh Viewport height in pixels.
 * @param[in]     p  Player, for health and the hurt timer.
 * @param[in]     w  Weapon, for the ammo count.
 * @note Native resolution, deliberately: this runs after ::post_end because
 *       5x7 glyphs magnified four times are unreadable and dithered text is
 *       worse still.
 * @note Health is green when healthy and red when low, so a glance at the
 *       colour says as much as the number does.
 *
 * 한국어
 * ------
 * @brief 피격 섬광과 체력·탄약 표시를 그립니다.
 * @param[in,out] s  버퍼를 제공하는 장면.
 * @param[in]     vw 뷰포트 너비 (픽셀).
 * @param[in]     vh 뷰포트 높이 (픽셀).
 * @param[in]     p  체력과 피격 타이머를 제공하는 플레이어.
 * @param[in]     w  탄약 수를 제공하는 무기.
 * @note 의도적으로 원해상도에서 그립니다. 5x7 글리프를 4배 확대하면 읽을 수 없고
 *       디더링된 텍스트는 더 나쁘므로 ::post_end 이후에 실행됩니다.
 * @note 체력은 충분하면 초록, 낮으면 빨강이므로 색만 봐도 숫자만큼의 정보를 얻습니다.
 */
void scene_draw_hud(Scene *s, int vw, int vh, const Level *l,
                    const Player *p, const Weapon *w);

/**
 * @brief Draws the win screen over the frozen world.
 *
 * ENGLISH
 * -------
 * @param[in,out] s  Scene supplying the buffer.
 * @param[in]     vw Viewport width in pixels.
 * @param[in]     vh Viewport height in pixels.
 * @param[in]     p  Player, for the final health figure.
 * @param[in]     w  Weapon, for the final ammo figure.
 * @param[in]     run What the run amounted to, already worded by
 *                   ::run_summary. A finished string rather than a ::RunState,
 *                   for the reason `since` below is a float rather than one:
 *                   this pass lays text out, and deciding which facts a run
 *                   reports is not a layout decision.
 * @note The world keeps drawing underneath, dimmed rather than cleared: the
 *       last frame stays on screen so the ending reads as an overlay on the
 *       game rather than as a separate screen.
 * @note Two stat lines, not one: the belt is what the player finished HOLDING
 *       and the summary is what they DID. Running them together would make the
 *       ammo count look like part of the score.
 *
 * 한국어
 * ------
 * @brief 정지된 월드 위에 승리 화면을 그립니다.
 * @param[in,out] s  버퍼를 제공하는 장면.
 * @param[in]     vw 뷰포트 너비 (픽셀).
 * @param[in]     vh 뷰포트 높이 (픽셀).
 * @param[in]     p  최종 체력 수치를 제공하는 플레이어.
 * @param[in]     w  최종 탄약 수치를 제공하는 무기.
 * @param[in]     run 이번 플레이가 무엇이었는지. ::run_summary가 이미 문구로 만든 것입니다.
 *                   ::RunState가 아니라 완성된 문자열인 이유는 아래의 `since`가 플레이가 아니라
 *                   float인 이유와 같습니다. 이 패스는 텍스트를 배치하며, 플레이가 어떤 사실을
 *                   보고하는지는 배치의 결정이 아닙니다.
 * @note 월드는 지워지지 않고 어둡게 처리된 채 계속 그려집니다. 마지막 프레임이 화면에
 *       남아, 결말이 별도의 화면이 아니라 게임 위에 덧씌워진 것으로 읽힙니다.
 * @note 한 줄이 아니라 두 줄입니다. 탄약대는 플레이어가 끝낼 때 *들고 있던* 것이고 요약은
 *       그들이 *한 일*입니다. 붙여 놓으면 탄약 수가 성적의 일부처럼 보입니다.
 */
void scene_draw_win(Scene *s, int vw, int vh, const Player *p, const Weapon *w,
                    const char *run);

/**
 * @brief Draws the screen between two levels: what was cleared, what is next.
 *
 * ENGLISH
 * -------
 * @param[in,out] s        Scene supplying the HUD buffer and mesh.
 * @param[in]     vw,vh    Viewport size in pixels.
 * @param[in]     cleared  Name of the level just finished.
 * @param[in]     entering Name of the level it leads to.
 * @param[in]     t        Seconds this screen has been up.
 * @param[in]     total    How long it stays up, for the fade at each end.
 * @note Takes the two names rather than the World, so it cannot accidentally
 *       read the level that is CURRENTLY loaded -- which during this screen is
 *       still the finished one, and would put the wrong name under "ENTERING".
 *
 * 한국어
 * ------
 * @brief 두 레벨 사이의 화면을 그립니다. 무엇을 끝냈고 다음은 무엇인지 표시합니다.
 * @note World가 아니라 이름 둘을 받으므로, *현재* 로드된 레벨을 실수로 읽을 수 없습니다.
 *       이 화면이 떠 있는 동안 그것은 여전히 방금 끝낸 레벨이며, "ENTERING" 아래에 틀린
 *       이름을 넣게 됩니다.
 */
void scene_draw_between(Scene *s, int vw, int vh, const char *cleared,
                        const char *entering, float t, float total);

/**
 * @brief Draws the ESC menu over the frozen world.
 *
 * ENGLISH
 * -------
 * @param[in,out] s  Scene supplying the buffer.
 * @param[in]     vw Viewport width in pixels.
 * @param[in]     vh Viewport height in pixels.
 * @note Reads the rows from menu.c rather than listing them here. A second
 *       copy of the row list would drift from the navigation, and the symptom
 *       would be the highlight sitting on one row while a different one
 *       activates.
 * @note A UI pass like the HUD, so it runs after ::post_end and is neither
 *       pixelised nor dithered -- the menu has to stay readable at every pixel
 *       preset, including the chunkiest one.
 * @note Draws nothing when the menu is closed, so the caller can call it
 *       unconditionally.
 *
 * 한국어
 * ------
 * @brief 정지된 월드 위에 ESC 메뉴를 그립니다.
 * @param[in,out] s  버퍼를 제공하는 장면.
 * @param[in]     vw 뷰포트 너비 (픽셀).
 * @param[in]     vh 뷰포트 높이 (픽셀).
 * @note 행 목록을 이곳에 나열하지 않고 menu.c에서 읽어 옵니다. 사본이 두 개면 탐색과
 *       어긋나게 되며, 증상은 강조된 행과 실제로 실행되는 행이 다른 형태로 나타납니다.
 * @note HUD와 같은 UI 패스이므로 ::post_end 이후에 실행되며 픽셀화도 디더링도 되지
 *       않습니다. 메뉴는 가장 픽셀이 큰 프리셋을 포함한 모든 설정에서 읽을 수 있어야
 *       합니다.
 * @note 메뉴가 닫혀 있으면 아무것도 그리지 않으므로, 호출자가 조건 없이 호출해도 됩니다.
 */
void scene_draw_menu(Scene *s, int vw, int vh);

/**
 * @brief Draws the death screen over the frozen world.
 *
 * ENGLISH
 * -------
 * @param[in,out] s     Scene supplying the buffer.
 * @param[in]     vw    Viewport width in pixels.
 * @param[in]     vh    Viewport height in pixels.
 * @param[in]     since Seconds since the player died, driving the fade.
 * @param[in]     ready Non-zero once input is accepted, which is when the
 *                      prompt appears. Passed in rather than derived from
 *                      `since` so the delay is decided in one place.
 * @param[in]     run   What the run amounted to, already worded by
 *                      ::run_summary. See ::scene_draw_win for why this is a
 *                      string and not a ::RunState.
 * @note Fades IN rather than appearing at once. Death is the one moment the
 *       player most wants to see what happened, and a screen that lands
 *       instantly hides the frame that explains it.
 * @note Tinted red, where the win screen is neutral: the two endings must not
 *       be distinguishable only by reading their text.
 * @note THE SUMMARY FADES WITH THE TITLE, not with the prompt. It is part of
 *       what the screen says happened rather than an instruction about what to
 *       do next, and a score that appeared later than the word DIED would read
 *       as a second event.
 *
 * 한국어
 * ------
 * @brief 정지된 월드 위에 사망 화면을 그립니다.
 * @param[in,out] s     버퍼를 제공하는 장면.
 * @param[in]     vw    뷰포트 너비 (픽셀).
 * @param[in]     vh    뷰포트 높이 (픽셀).
 * @param[in]     since 사망 이후 경과 시간(초). 페이드를 구동합니다.
 * @param[in]     ready 입력을 받기 시작하면 0이 아닌 값이며, 그때 안내 문구가 나타납니다.
 *                      지연 시간이 한 곳에서 결정되도록 `since`에서 유도하지 않고 인자로
 *                      받습니다.
 * @param[in]     run   이번 플레이가 무엇이었는지. ::run_summary가 이미 문구로 만든 것입니다.
 *                      ::RunState가 아니라 문자열인 이유는 ::scene_draw_win을 참조하십시오.
 * @note 즉시 나타나지 않고 서서히 나타납니다. 사망은 플레이어가 무슨 일이 있었는지 가장
 *       보고 싶어 하는 순간이며, 즉시 덮이는 화면은 그것을 설명하는 프레임을 가립니다.
 * @note 승리 화면이 중립적인 것과 달리 붉은 색조를 띱니다. 두 결말이 텍스트를 읽어야만
 *       구분되어서는 안 됩니다.
 * @note *요약은 안내 문구가 아니라 제목과 함께 나타납니다.* 다음에 무엇을 할지에 대한
 *       지시가 아니라 무슨 일이 있었는지에 대한 서술의 일부이며, DIED보다 늦게 나타나는
 *       성적은 두 번째 사건처럼 읽힙니다.
 */
void scene_draw_death(Scene *s, int vw, int vh, float since, int ready,
                      const char *run);

/**
 * @brief Draws the game's name above the title menu's rows.
 *
 * ENGLISH
 * -------
 * @param[in,out] s    Scene supplying the buffer.
 * @param[in]     vw   Viewport width in pixels.
 * @param[in]     vh   Viewport height in pixels.
 * @param[in]     t    Seconds the title has been up, for the fade in.
 * @param[in]     best The best wave any run has reached, or 0 for none -- a
 *                     figure this screen is the only place to see, because it
 *                     outlives every run that produced it.
 *
 * AFTER ::scene_draw_menu RATHER THAN BEFORE IT, and that is the only unusual
 * thing here. The menu dims the world behind it, and this is the screen's own
 * art rather than something that dim should push back -- drawn first, the title
 * would be darkened by the menu it heads. When there is no menu in front of the
 * title -- a playback, or a ::World a tool is driving -- this pass draws the dim
 * itself.
 *
 * @note CALLED FOR EVERY TITLE FRAME THE TITLE IS THE SCREEN OF, which is not
 *       only the ones with a menu over them, and that is load-bearing rather
 *       than tidy. ::scene_frame's other UI passes are all skipped while
 *       `title` is set, so on a title frame with no menu -- a playback, or a
 *       ::World a tool is driving -- this is the only one left, and ::ui_end is
 *       what restores the depth test and the culling the NEXT frame's world
 *       pass expects. A title frame that drew no UI pass would corrupt the
 *       frame after it.
 *
 * @note NOT CALLED FOR SETTINGS OR CREDITS, though `title` is still set while
 *       either is up: they are reached FROM the title and the menu's own header
 *       takes this block's place -- ::menu_title_y places both -- so the name
 *       drawn as well would be a second header printed over the first. The caller makes that
 *       call, because the caller is what knows both facts. Those frames keep
 *       their UI pass from ::scene_draw_menu, which is what draws them.
 *
 * @note WHERE IT SITS COMES FROM menu.c. ::menu_title_y is where the header of
 *       the current screen goes, and the title screen widens that gap for
 *       exactly this block; laying it out from a constant here would be a
 *       second layout, and the first thing two layouts disagree about is
 *       whether the header overlaps the first row.
 * @note THE PROMPT IS GONE, and its absence is the point. This used to say
 *       "press any key to begin", which stopped being true the moment there was
 *       something to choose: a key does a specific thing now, and the rows say
 *       what.
 *
 * 한국어
 * ------
 * @brief 타이틀 메뉴의 행들 위에 게임의 이름을 그립니다.
 * @param[in,out] s    버퍼를 제공하는 장면.
 * @param[in]     vw   뷰포트 너비 (픽셀).
 * @param[in]     vh   뷰포트 높이 (픽셀).
 * @param[in]     t    타이틀이 떠 있던 시간(초). 서서히 나타나는 데 사용됩니다.
 * @param[in]     best 어떤 플레이든 도달한 최고 웨이브. 없으면 0입니다. 그것을 만들어 낸 모든
 *                     플레이보다 오래 살아남기에, 이 화면이 그것을 볼 수 있는 유일한 곳입니다.
 *
 * *::scene_draw_menu보다 앞이 아니라 뒤이며*, 이곳에서 유일하게 특이한 점입니다. 메뉴는 뒤의
 * 월드를 어둡게 하는데, 이것은 그 어둡게 하기가 뒤로 밀어내야 할 것이 아니라 화면 자신의
 * 아트입니다. 먼저 그리면 제목이 자기가 머리글로 있는 메뉴에 의해 어두워집니다. 타이틀 앞에
 * 메뉴가 없을 때(재생이거나 도구가 구동하는 ::World일 때)는 이 패스가 직접 어둡게 합니다.
 *
 * @note *타이틀이 그 화면인 모든 프레임에 대해 호출되며*, 메뉴가 위에 있는 프레임에만이
 *       아닙니다. 이것은 단정함이 아니라 구조적으로 중요합니다. ::scene_frame의 다른 UI 패스는
 *       `title`이 서 있는 동안 전부 건너뛰어지므로, 메뉴가 없는 타이틀 프레임(재생이거나 도구가
 *       구동하는 ::World)에서는 남는 것이 이것뿐이며, 다음 프레임의 월드 패스가 기대하는 깊이
 *       검사와 컬링을 복원하는 것이 ::ui_end입니다. UI 패스를 하나도 그리지 않는 타이틀 프레임은
 *       그 다음 프레임을 망칩니다.
 *
 * @note *설정과 크레딧에서는 호출되지 않습니다.* 둘 중 하나가 떠 있는 동안에도 `title`은 여전히
 *       서 있지만, 그 둘은 타이틀에서 들어가는 곳이고 메뉴 자신의 머리글이 이 블록의 자리를
 *       차지합니다. 둘 다 ::menu_title_y가 놓습니다. 그러므로 이름까지 그리면 첫 머리글 위에
 *       두 번째 머리글을 찍는 셈이 됩니다. 그 판단은 호출자가 합니다. 두 사실을 모두 아는 것이 호출자이기
 *       때문입니다. 그 프레임들은 자신을 그리는 ::scene_draw_menu에서 UI 패스를 받습니다.
 *
 * @note *어디에 놓이는지는 menu.c에서 옵니다.* ::menu_title_y는 현재 화면의 머리글이 가는
 *       자리이며, 타이틀 화면은 정확히 이 블록을 위해 그 간격을 넓힙니다. 이곳의 상수로
 *       배치하면 두 번째 배치가 되고, 두 배치가 가장 먼저 어긋나는 것은 머리글이 첫 행과
 *       겹치는가입니다.
 * @note *안내 문구는 사라졌고* 그 부재가 요점입니다. 이곳은 "press any key to begin"이라고
 *       말했는데, 고를 것이 생긴 순간 그것은 참이기를 그만두었습니다. 이제 키는 특정한 일을
 *       하고, 무엇인지는 행들이 말합니다.
 */
void scene_draw_title(Scene *s, int vw, int vh, float t, int best);

/**
 * @brief Draws one page of a cutscene over the stopped world.
 *
 * ENGLISH
 * -------
 * @param[in,out] s     Scene supplying the buffer.
 * @param[in]     vw,vh Viewport size in pixels.
 * @param[in]     page  The page to show. Never null; a caller with nothing to
 *                      show does not call this.
 * @param[in]     index Which page this is, 0-based, for the pips.
 * @param[in]     count How many the cutscene has.
 * @param[in]     alpha Opacity, 0..1, worked out by ::scene_frame from the
 *                      page's clock. Zero draws nothing.
 *
 * TAKES A PAGE, NOT A ::World AND NOT A ::StoryCut, which is this header's
 * standing rule one step further in: which moment is playing and which of its
 * pages is up are world.c's questions, already answered, and a pass handed the
 * whole cutscene could answer them a second and different way.
 *
 * @note THE PIPS ARE THE SAME ARGUMENT THE WARD PIPS ARE. A cutscene stops the
 *       game, and a player who cannot tell whether one page remains or five is
 *       being asked to wait for an unknown length of time. Drawn as tracks for
 *       the ones already spent, because "drawing the whole track means a slider
 *       at zero is still a slider".
 * @note Skippable, and the hint says so. It is not drawn on the first page:
 *       a screen that offers a way out before it has said anything is a screen
 *       that expects to be skipped.
 *
 * 한국어
 * ------
 * @brief 멈춘 월드 위에 컷신의 한 페이지를 그립니다.
 * @param[in,out] s     버퍼를 제공하는 장면.
 * @param[in]     vw,vh 뷰포트 크기(픽셀).
 * @param[in]     page  보여 줄 페이지. 결코 null이 아닙니다. 보여 줄 것이 없는 호출자는 이것을
 *                      부르지 않습니다.
 * @param[in]     index 이것이 몇 번째 페이지인지. 0부터이며 핍에 사용됩니다.
 * @param[in]     count 컷신이 가진 페이지 수.
 * @param[in]     alpha 불투명도(0..1). 페이지의 시계로부터 ::scene_frame이 계산합니다. 0이면
 *                      아무것도 그리지 않습니다.
 *
 * *::World도 ::StoryCut도 아닌 페이지를 받으며*, 이 헤더의 정해진 규칙을 한 걸음 더 들어간
 * 것입니다. 어느 순간이 재생 중이고 그중 어느 페이지가 떠 있는지는 world.c가 이미 답한
 * 질문이며, 컷신 전체를 건네받은 패스는 그것들에 두 번째로, 다르게 답할 수 있습니다.
 *
 * @note *핍은 결계핵 핍과 같은 논거입니다.* 컷신은 게임을 멈추며, 남은 페이지가 한 장인지 다섯
 *       장인지 알 수 없는 플레이어는 알 수 없는 시간만큼 기다리라는 요구를 받는 것입니다. 이미
 *       지나간 것도 트랙으로 그립니다. *"트랙 전체를 그리면 0인 슬라이더도 여전히
 *       슬라이더"*이기 때문입니다.
 * @note 건너뛸 수 있으며 안내가 그렇게 말합니다. 첫 페이지에는 그리지 않습니다. 아무 말도 하기
 *       전에 나갈 길을 제시하는 화면은 건너뛰어질 것을 예상하는 화면입니다.
 */
void scene_draw_story(Scene *s, int vw, int vh, const StoryPage *page,
                      int index, int count, float alpha);

/**
 * @brief The boss fight's top-of-screen readout: health, wards, and one line.
 *
 * ENGLISH
 * -------
 * @param[in,out] s           The scene, for its HUD buffer.
 * @param[in]     vw,vh       Viewport size in pixels.
 * @param[in]     show_bar    Draw the bar and pips. Zero draws the line alone.
 * @param[in]     fill        Boss health as a fraction, 0..1. Clamped here.
 * @param[in]     wards_left  Wards still standing.
 * @param[in]     wards_total Pips to draw, standing or not.
 * @param[in]     groggy      Non-zero while the boss can actually be hurt.
 * @param[in]     line        The sentence to show, or null for none.
 * @param[in]     line_alpha  Its opacity, 0..1. Zero draws nothing.
 *
 * TAKES FINISHED FACTS, NOT A ::World, which is this header's standing rule --
 * see ::scene_draw_between and ::scene_draw_win. Which slot the boss is in and
 * which of the five lines is up are questions world.c has already answered.
 *
 * @note `groggy` changes the fill's COLOUR and nothing else. During the warded
 *       phase the boss takes no damage, so the fill does not move; a bar that
 *       simply sat there would read as broken rather than as invulnerable.
 * @note Drawn with the HUD group so the end screens dim it, for the reason
 *       ::scene_frame gives about readouts belonging to the frozen frame.
 * @note `show_bar` EXISTS BECAUSE THE TWO HALVES HAVE DIFFERENT LIFETIMES. The
 *       bar needs a living boss; the banner outlives one, because the maw's
 *       last line is posted on the frame it dies. A banner drawn only when
 *       there is a boss is a sentence that can never be shown, which reads
 *       exactly like a sentence nobody wrote.
 *
 * 한국어
 * ------
 * @brief 보스전의 화면 상단 계기판. 체력, 결계핵, 그리고 대사 한 줄입니다.
 * @param[in,out] s           HUD 버퍼를 쓸 장면.
 * @param[in]     vw,vh       뷰포트 크기(픽셀).
 * @param[in]     fill        보스 체력의 비율(0..1). 이곳에서 자릅니다.
 * @param[in]     wards_left  아직 서 있는 결계핵의 수.
 * @param[in]     wards_total 서 있든 아니든 그릴 핍의 수.
 * @param[in]     groggy      보스가 실제로 다칠 수 있는 동안 0이 아닌 값.
 * @param[in]     line        보여 줄 문장. 없으면 null.
 * @param[in]     line_alpha  그 불투명도(0..1). 0이면 아무것도 그리지 않습니다.
 *
 * *::World가 아니라 완성된 사실을 받으며*, 이 헤더의 정해진 규칙입니다. ::scene_draw_between과
 * ::scene_draw_win을 참조하십시오. 보스가 어느 슬롯에 있는지, 다섯 대사 중 어느 것이 떠 있는지는
 * world.c가 이미 답한 질문입니다.
 *
 * @note `groggy`는 채움의 *색*만 바꿉니다. 수호 단계 동안 보스는 피해를 받지 않으므로 채움이
 *       움직이지 않는데, 그냥 가만히 있는 바는 무적이 아니라 고장으로 읽힙니다.
 * @note 종료 화면이 어둡게 할 수 있도록 HUD 무리와 함께 그립니다. 계기판이 정지된 프레임에
 *       속한다는 ::scene_frame의 설명이 그 이유입니다.
 */
void scene_draw_boss(Scene *s, int vw, int vh, int show_bar, float fill,
                     int wards_left, int wards_total, int groggy,
                     const char *line, float line_alpha);

#endif
