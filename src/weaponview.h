/**
 * @file weaponview.h
 * @brief The gun you see: its mesh, its materials, and the buffers they draw from.
 *
 * ENGLISH
 * -------
 * Split out of weapon.c, which held two modules in one file: the rules a shot
 * obeys, and the gun drawn in the corner of the screen. They shared a name and
 * nothing else. The rules are arithmetic against a Level and belong in a
 * headless test; the view model is a mesh, four materials and two vertex
 * buffers, and belongs to whichever GL context is going to draw them.
 *
 * WHAT THE SPLIT WAS ACTUALLY COSTING. ::wp_init loaded the gun model, so it
 * needed a GL context, so ::world_init could not call it, so every caller had
 * to know to call ::wp_init after making a context and before the first
 * ::world_load_level -- a rule stated at length in world.h, in main.c, and in
 * tools\scenetest.c, and worked around in tools\hooktest.c by never calling
 * ::wp_init at all and zeroing a ::Weapon by hand. Four files explaining one
 * ordering constraint is what a seam in the wrong place looks like. Loading a
 * model is now this file's job and nothing in weapon.c touches GL, so the
 * constraint has no reason to exist and those paragraphs are gone.
 *
 * @note ONE GUN PER GL CONTEXT, not per run -- which is why ::WeaponView is
 *       owned by ::Scene beside the sprite atlases rather than by ::World
 *       beside the ::Pools. Two runs drawn by one process draw the same
 *       shotgun; what differs between them is ammo, recoil and what the shot
 *       hit, and all of that is in ::Weapon.
 * @note The one fact that crosses the seam is the muzzle. Where the barrel
 *       ends comes from the model, which only this file can read, and firing
 *       needs it to place tracers -- so ::wpview_set_model writes it into the
 *       ::Weapon. Until a model is loaded the weapon holds
 *       ::WP_MUZZLE_DEFAULT, which is what a headless fixture fires from.
 *
 * 한국어
 * ------
 * @brief 눈에 보이는 총입니다. 메시와 재질, 그리고 그것들이 그려지는 버퍼입니다.
 *
 * 두 모듈을 한 파일에 담고 있던 weapon.c에서 분리했습니다. 사격이 따르는 규칙과, 화면
 * 구석에 그려지는 총입니다. 둘은 이름만 공유했을 뿐입니다. 규칙은 Level에 대한 산술이며
 * 헤드리스 테스트에 속하고, 뷰 모델은 메시 하나와 재질 넷과 정점 버퍼 둘이며 그것을 그릴
 * GL 컨텍스트에 속합니다.
 *
 * 이 분리가 실제로 치르고 있던 비용. ::wp_init이 총기 모델을 로드했으므로 GL 컨텍스트가
 * 필요했고, 그래서 ::world_init이 그것을 호출할 수 없었으며, 따라서 모든 호출자가
 * "컨텍스트를 만든 뒤, 첫 ::world_load_level 이전에 ::wp_init을 호출하라"를 알고 있어야
 * 했습니다. world.h와 main.c와 tools\scenetest.c가 각각 길게 설명하던 규칙이며,
 * tools\hooktest.c는 ::wp_init을 아예 호출하지 않고 ::Weapon을 손으로 0으로 만드는 것으로
 * 우회했습니다. 하나의 순서 제약을 네 개의 파일이 설명하고 있다면 그것이 바로 이음매가
 * 잘못된 자리에 있는 모습입니다. 이제 모델 로드는 이 파일의 일이고 weapon.c는 GL을 전혀
 * 건드리지 않으므로, 그 제약은 존재할 이유가 없어졌고 해당 문단들도 사라졌습니다.
 *
 * @note GL 컨텍스트당 총 하나이며 플레이당 하나가 아닙니다. ::WeaponView가 ::Pools 옆의
 *       ::World가 아니라 스프라이트 아틀라스 옆의 ::Scene에 속하는 이유입니다. 한
 *       프로세스가 그리는 두 플레이는 같은 샷건을 그립니다. 둘 사이에서 다른 것은 탄약과
 *       반동과 무엇을 맞혔는가이며, 그 전부는 ::Weapon에 있습니다.
 * @note 이음매를 넘는 유일한 사실은 총구입니다. 총열이 어디서 끝나는지는 모델에서 오고 그
 *       모델은 이 파일만 읽을 수 있는데, 발사는 예광탄을 배치하기 위해 그 값이
 *       필요합니다. 그래서 ::wpview_set_model이 그것을 ::Weapon에 기록합니다. 모델이
 *       로드되기 전까지 무기는 ::WP_MUZZLE_DEFAULT를 지니며, 헤드리스 픽스처가 발사하는
 *       위치가 바로 그것입니다.
 */
#ifndef WEAPONVIEW_H
#define WEAPONVIEW_H

#include "render.h"   /* Mesh, MeshBuf */
#include "model.h"    /* MdlRange, MDL_MAX_RANGES */
#include "tex.h"      /* Mat */
#include "weapon.h"   /* Weapon, and Level through it */

/**
 * @struct WeaponView
 * @brief Everything needed to DRAW the gun, owned by whoever owns the context.
 *
 * @warning Owns heap and GL objects: pair every ::wpview_init with a
 *          ::wpview_free. The two ::MeshBuf members were previously file-scope
 *          in weapon.c with no free at all -- the one unpaired ::mb_init in the
 *          project, harmless only because the process was exiting.
 *
 * 한국어: 총을 *그리는* 데 필요한 모든 것이며, 컨텍스트를 소유한 쪽이 소유합니다.
 * @warning 힙과 GL 객체를 소유합니다. 모든 ::wpview_init은 ::wpview_free와 짝을 이루어야
 *          합니다. 두 ::MeshBuf 멤버는 이전에 weapon.c의 파일 스코프에 있으면서 해제가
 *          전혀 없었습니다. 이 프로젝트에서 짝이 없던 유일한 ::mb_init이며, 프로세스가
 *          종료 중이었기 때문에만 무해했습니다.
 */
typedef struct {
    Mesh     gun_mesh;                  /**< The gun's vertices, shared by every material run. / 총기의 정점. 모든 재질 구간이 공유합니다. */
    MdlRange gun_ranges[MDL_MAX_RANGES];/**< One index range per material. / 재질별 인덱스 구간. */
    Mat      gun_tex[MDL_MAX_RANGES];   /**< The material each range draws with. / 각 구간이 사용하는 재질. */
    int      gun_range_count;           /**< Ranges in use. / 사용 중인 구간의 수. */
    Mat      rope_mat;                  /**< The tether's material. / 로프의 재질. */

    Mesh     fx_mesh,  line_mesh;       /**< GPU meshes for billboards and for line geometry. / 빌보드와 선 지오메트리용 GPU 메시. */
    MeshBuf  fx_buf,   line_buf;        /**< The CPU builders feeding them, rebuilt every frame. / 그것들에 데이터를 공급하는 CPU 빌더. 매 프레임 재구성됩니다. */

    char     model_name[32];            /**< Loaded model's name, so hot reload can rebuild it. / 로드된 모델의 이름. 핫 리로드가 재생성할 수 있게 합니다. */
} WeaponView;

/* --- Public function prototypes: lifecycle / 공개 함수 프로토타입: 수명 주기 --- */

/**
 * @brief Loads the gun model and allocates the buffers it is drawn from.
 *
 * ENGLISH
 * -------
 * @param[out] v View to bring up.
 * @param[out] w Weapon whose ::Weapon::muzzle is written from the loaded model.
 * @warning Requires a current GL context.
 *
 * 한국어
 * ------
 * @param[out] v 준비할 뷰.
 * @param[out] w 로드된 모델로부터 ::Weapon::muzzle을 기록받을 무기.
 * @warning 활성 GL 컨텍스트가 필요합니다.
 */
void wpview_init(WeaponView *v, Weapon *w);

/**
 * @brief Releases the buffers and GL objects the view owns.
 * / 뷰가 소유한 버퍼와 GL 객체를 해제합니다.
 */
void wpview_free(WeaponView *v);

/**
 * @brief Uploads the named model as the gun mesh, and records its muzzle.
 *
 * ENGLISH
 * -------
 * @param[out] v    View receiving the mesh, ranges and materials.
 * @param[out] w    Weapon receiving the model's muzzle. May be null when no
 *                  weapon is being driven -- the preview tool passes null.
 * @param[in]  name Model name to look up in the baked model text.
 * @note Does nothing if the model does not parse, leaving the previous one in
 *       place: an authoring typo should not blank the gun.
 * @warning Requires a current GL context.
 *
 * 한국어
 * ------
 * @param[out] v    메시·구간·재질을 받을 뷰.
 * @param[out] w    모델의 총구 위치를 받을 무기. 구동 중인 무기가 없으면 널이어도 됩니다.
 *                  프리뷰 도구는 널을 전달합니다.
 * @param[in]  name 내장된 모델 텍스트에서 찾을 모델 이름.
 * @note 모델이 파싱되지 않으면 아무 동작도 하지 않고 이전 모델을 유지합니다. 제작 중의
 *       오타가 총을 지워서는 안 됩니다.
 * @warning 활성 GL 컨텍스트가 필요합니다.
 */
void wpview_set_model(WeaponView *v, Weapon *w, const char *name);

/**
 * @brief Regenerates the gun's materials from the current recipe text.
 *
 * ENGLISH
 * -------
 * @warning Requires a current GL context. Flushes the texture cache, so any
 *          cached binding taken before this call becomes stale.
 *
 * 한국어
 * ------
 * @warning 활성 GL 컨텍스트가 필요합니다. 텍스처 캐시를 비우므로, 이 호출 이전에 취한
 *          캐시된 바인딩은 무효가 됩니다.
 */
void wpview_reload_texture(WeaponView *v);

/* --- Public function prototypes: drawing / 공개 함수 프로토타입: 그리기 --- */

/**
 * @brief Draws what the weapon left in the world: the claw and the tether.
 *
 * ENGLISH
 * -------
 * @param[in,out] v         View supplying the buffers and the rope material.
 * @param[in]     w         Weapon supplying the hook's live state.
 * @param[in]     view_proj World view-projection matrix.
 * @param[in]     cam_pos   Camera position.
 * @param[in]     cam_right Camera right basis vector.
 * @param[in]     cam_up    Camera up basis vector.
 * @note Belongs to the WORLD pass: it is pixelised and dithered with
 *       everything else. See scene.h on the pass boundary.
 *
 * 한국어
 * ------
 * @param[in,out] v         버퍼와 로프 재질을 제공하는 뷰.
 * @param[in]     w         훅의 실시간 상태를 제공하는 무기.
 * @param[in]     view_proj 월드 뷰-투영 행렬.
 * @param[in]     cam_pos   카메라 위치.
 * @param[in]     cam_right 카메라의 우측 기저 벡터.
 * @param[in]     cam_up    카메라의 상향 기저 벡터.
 * @note *월드* 패스에 속합니다. 나머지 전부와 함께 픽셀화되고 디더링됩니다. 패스 경계에
 *       대해서는 scene.h를 참조하십시오.
 */
void wpview_draw_world(WeaponView *v, const Weapon *w, mat4 view_proj,
                       v3 cam_pos, v3 cam_right, v3 cam_up);

/**
 * @brief Draws the gun in the corner, in its own projection over cleared depth.
 *
 * ENGLISH
 * -------
 * @param[in,out] v      View supplying the mesh and materials.
 * @param[in]     w      Weapon supplying bob, sway, recoil and the flash.
 * @param[in]     aspect Aspect ratio to draw against.
 * @note Belongs to the WORLD pass, so the gun is pixelised like the world it
 *       is held in front of.
 *
 * 한국어
 * ------
 * @param[in,out] v      메시와 재질을 제공하는 뷰.
 * @param[in]     w      흔들림·스웨이·반동·화염을 제공하는 무기.
 * @param[in]     aspect 그릴 대상 종횡비.
 * @note *월드* 패스에 속하므로, 총은 그것이 들려 있는 월드와 같이 픽셀화됩니다.
 */
void wpview_draw_view(WeaponView *v, const Weapon *w, float aspect);

/**
 * @brief Draws the crosshair and the hook-ready ring.
 *
 * ENGLISH
 * -------
 * @param[in,out] v          View supplying the line buffer.
 * @param[in]     w          Weapon supplying spread and hook state.
 * @param[in]     aspect     Aspect ratio to draw against.
 * @param[in]     hook_ready Non-zero when something is in hook range.
 * @note Belongs to the UI pass: drawn after the resolve so it stays sharp.
 *
 * 한국어
 * ------
 * @param[in,out] v          선 버퍼를 제공하는 뷰.
 * @param[in]     w          탄퍼짐과 훅 상태를 제공하는 무기.
 * @param[in]     aspect     그릴 대상 종횡비.
 * @param[in]     hook_ready 훅 사거리 안에 무언가 있으면 0이 아닙니다.
 * @note *UI* 패스에 속합니다. 리졸브 이후에 그려지므로 선명하게 유지됩니다.
 */
void wpview_draw_hud(WeaponView *v, const Weapon *w, float aspect, int hook_ready);

#endif
