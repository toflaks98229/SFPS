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
void scene_init(Scene *s);

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
 *       lights, and a level's lights are baked into the geometry this draws
 *       rather than uploaded per frame -- so the only thing this needs from a
 *       Level is the mesh built from it, which the Scene already holds.
 *
 * 한국어
 * ------
 * @brief 레벨을 그립니다. 재질마다 하나의 텍스처 구간을 그립니다.
 * @param[in] s   생성된 레벨을 보유한 장면.
 * @param[in] vp  뷰-투영 결합 행렬.
 * @param[in] eye 조명과 안개 계산에 사용되는 카메라 위치.
 * @note 레벨은 더 이상 인자가 아닙니다. 점광원을 제공하기 위해 있었는데, 레벨의 광원은
 *       프레임마다 업로드되는 것이 아니라 이 함수가 그리는 지오메트리에 구워져 있습니다.
 *       따라서 이 함수가 Level로부터 필요로 하는 유일한 것은 그것으로 만들어진 메시이며,
 *       그것은 Scene이 이미 보유하고 있습니다.
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
void scene_draw_enemies(Scene *s, mat4 vp, v3 eye, v3 cam_right);

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
void scene_draw_pickups(Scene *s, mat4 vp, v3 eye, v3 cam_right);

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
void scene_draw_shots(Scene *s, mat4 vp, v3 cam_right, v3 cam_up);

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
void scene_draw_proj(Scene *s, const Pools *w, mat4 vp, v3 cam_right, v3 cam_up);

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
void scene_draw_hud(Scene *s, int vw, int vh, const Player *p, const Weapon *w);

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
 * @note The world keeps drawing underneath, dimmed rather than cleared: the
 *       last frame stays on screen so the ending reads as an overlay on the
 *       game rather than as a separate screen.
 *
 * 한국어
 * ------
 * @brief 정지된 월드 위에 승리 화면을 그립니다.
 * @param[in,out] s  버퍼를 제공하는 장면.
 * @param[in]     vw 뷰포트 너비 (픽셀).
 * @param[in]     vh 뷰포트 높이 (픽셀).
 * @param[in]     p  최종 체력 수치를 제공하는 플레이어.
 * @param[in]     w  최종 탄약 수치를 제공하는 무기.
 * @note 월드는 지워지지 않고 어둡게 처리된 채 계속 그려집니다. 마지막 프레임이 화면에
 *       남아, 결말이 별도의 화면이 아니라 게임 위에 덧씌워진 것으로 읽힙니다.
 */
void scene_draw_win(Scene *s, int vw, int vh, const Player *p, const Weapon *w);

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
 * @note Fades IN rather than appearing at once. Death is the one moment the
 *       player most wants to see what happened, and a screen that lands
 *       instantly hides the frame that explains it.
 * @note Tinted red, where the win screen is neutral: the two endings must not
 *       be distinguishable only by reading their text.
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
 * @note 즉시 나타나지 않고 서서히 나타납니다. 사망은 플레이어가 무슨 일이 있었는지 가장
 *       보고 싶어 하는 순간이며, 즉시 덮이는 화면은 그것을 설명하는 프레임을 가립니다.
 * @note 승리 화면이 중립적인 것과 달리 붉은 색조를 띱니다. 두 결말이 텍스트를 읽어야만
 *       구분되어서는 안 됩니다.
 */
void scene_draw_death(Scene *s, int vw, int vh, float since, int ready);

/**
 * @brief Draws the title screen over the frozen world.
 *
 * ENGLISH
 * -------
 * @param[in,out] s  Scene supplying the buffer.
 * @param[in]     vw Viewport width in pixels.
 * @param[in]     vh Viewport height in pixels.
 * @param[in]     t  Seconds since startup, for the prompt's pulse.
 * @note PLACEHOLDER ART. The layout is deliberately plain -- a title, a
 *       subtitle and a prompt over the dimmed level -- because the real title
 *       art is still to be drawn. Everything it will need is already here:
 *       this is a UI pass at native resolution with the world behind it, so
 *       replacing the text with artwork changes this function and nothing
 *       else.
 *
 * 한국어
 * ------
 * @brief 정지된 월드 위에 타이틀 화면을 그립니다.
 * @param[in,out] s  버퍼를 제공하는 장면.
 * @param[in]     vw 뷰포트 너비 (픽셀).
 * @param[in]     vh 뷰포트 높이 (픽셀).
 * @param[in]     t  시작 이후 경과 시간(초). 안내 문구의 명멸에 사용됩니다.
 * @note *임시 아트입니다.* 실제 타이틀 아트를 아직 그리지 않았으므로 배치를 의도적으로
 *       단순하게 두었습니다. 어둡게 처리된 레벨 위의 제목, 부제, 안내 문구입니다. 필요한
 *       것은 이미 전부 갖춰져 있습니다. 뒤에 월드를 둔 원해상도 UI 패스이므로, 텍스트를
 *       아트워크로 교체하는 것은 이 함수만 바꾸면 됩니다.
 */
void scene_draw_title(Scene *s, int vw, int vh, float t);

#endif
