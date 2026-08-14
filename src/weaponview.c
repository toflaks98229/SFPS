/**
 * @file weaponview.c
 * @brief Implements the drawn gun: model, materials, view model and crosshair.
 *
 * ENGLISH
 * -------
 * The half of the old weapon.c that needs a GL context. Everything here either
 * uploads something or draws it; nothing here decides anything about a shot.
 * See weaponview.h for why the two were separated.
 *
 * 한국어
 * ------
 * 기존 weapon.c에서 GL 컨텍스트를 필요로 하던 절반입니다. 이곳의 모든 것은 무언가를
 * 업로드하거나 그립니다. 사격에 대해 무언가를 결정하는 것은 이곳에 없습니다. 둘을 분리한
 * 이유는 weaponview.h를 참조하십시오.
 */

#include "weaponview.h"
#include "hook.h"     /* HOOK_* states the tether is drawn from */
#include "sprite.h"   /* weapon_uv -- the view model's sprite atlas */
#include "txt.h"      /* txt_copy */
#include "post.h"     /* post_in_world_pass -- the pass-boundary guards */
#include "diag.h"

/* Material names are authored against LVL_MAT and looked up through tex_mat,
   whose cache can only hold TEX_NAME_MAX. If the authoring limit ever grew
   past the cache's, every over-long name would miss the cache forever and be
   rebuilt from its recipe on each lookup -- correct on screen, quietly
   expensive, and reported only as a diag counter. Checked here because this
   is a file that already includes both headers and passes level-authored
   names to tex_mat; no new dependency is introduced to make the check possible.

   재질 이름은 LVL_MAT을 기준으로 제작되고 tex_mat을 통해 조회되는데, tex_mat의 캐시는
   TEX_NAME_MAX까지만 담을 수 있습니다. 제작 상한이 캐시의 상한을 넘어서면, 초과하는 모든
   이름이 영원히 캐시 미스가 되어 조회할 때마다 레시피로부터 재생성됩니다. 화면상으로는
   올바르지만 조용히 비용이 들며, diag 카운터로만 보고됩니다. 이 파일이 두 헤더를 이미
   포함하고 있고 레벨에서 제작된 이름을 tex_mat에 전달하므로 이곳에서 검사합니다. 검사를
   위해 새로운 의존성을 추가하지 않습니다. */
_Static_assert(TEX_NAME_MAX >= LVL_MAT,
               "tex_mat's cache must hold any name a level can author");

/* ------------------------------------------------------------------ setup */

void wpview_init(WeaponView *v, Weapon *w) {
    WeaponView zero = {0};
    *v = zero;

    wpview_set_model(v, w, "shotgun");
    v->rope_mat = tex_mat("rope");

    /* FIXED capacities, sized for what is drawn here: the claw, the muzzle
       flash, the view model's sprite, and the crosshair and hook ring in
       lines. The comment this replaces called them "initial capacities only --
       mb_init grows", which was never true: mb_vtx drops vertices and raises
       DIAG_VERTEX_BUF when full. Believing otherwise is how a buffer gets
       sized optimistically and quietly loses geometry -- see render.h.
       *고정* 용량이며, 이곳에서 그리는 것에 맞춰 정했습니다. 클로, 총구 화염, 뷰 모델
       스프라이트, 그리고 선으로 그리는 조준점과 훅 링입니다. 이 자리에 있던 주석은
       "초기 용량일 뿐이며 mb_init은 필요하면 늘어납니다"라고 했지만 사실이 아니었습니다.
       mb_vtx는 가득 차면 정점을 버리고 DIAG_VERTEX_BUF를 올립니다. 그렇지 않다고 믿는
       것이 버퍼를 낙관적으로 잡고 조용히 지오메트리를 잃는 방식입니다. render.h를
       참조하십시오. */
    mb_init(&v->fx_buf,   64);
    mb_init(&v->line_buf, 128);
}

void wpview_free(WeaponView *v) {
    /* The pair the old file never had. Both mb_init calls above lived in
       wp_init as file-scope buffers with no mb_free anywhere in the project --
       harmless only because the process was exiting. Owned by Scene now, so
       scene_free reaches them.
       기존 파일에 없던 짝입니다. 위 두 mb_init은 wp_init 안에서 파일 스코프 버퍼로
       존재했고 이 프로젝트 어디에도 대응하는 mb_free가 없었습니다. 프로세스가 종료
       중이었기 때문에만 무해했습니다. 이제 Scene이 소유하므로 scene_free가 닿습니다. */
    mb_free(&v->fx_buf);
    mb_free(&v->line_buf);
}


void wpview_reload_texture(WeaponView *v) {
    /* Materials are named by the model, so a recipe change means rebuilding
       the whole lookup rather than one hardcoded texture. */
    tex_flush();
    for (int i = 0; i < v->gun_range_count; i++)
        v->gun_tex[i] = tex_mat(v->gun_ranges[i].mat);
    v->rope_mat = tex_mat("rope");
}


void wpview_set_model(WeaponView *v, Weapon *w, const char *name) {
    Model m;
    if (!mdl_load(name, &m)) return;

    txt_copy(v->model_name, sizeof(v->model_name), name, -1);

    MeshBuf gun;
    mb_init(&gun, MDL_MAX_VERTS);
    v->gun_range_count = mdl_geometry(&gun, &m, v->gun_ranges, MDL_MAX_RANGES);
    mesh_upload(&v->gun_mesh, &gun, 0);
    mb_free(&gun);

    for (int r = 0; r < v->gun_range_count; r++)
        v->gun_tex[r] = tex_mat(v->gun_ranges[r].mat);

    /* Null when nothing is being driven -- the preview tool loads models with
       no Weapon behind them. The mesh still loads; only the muzzle has nowhere
       to go.
       구동 중인 무기가 없으면 널입니다. 프리뷰 도구는 뒤에 Weapon 없이 모델을 로드합니다.
       메시는 그대로 로드되며, 총구 위치만 갈 곳이 없을 뿐입니다. */
    if (w) w->muzzle = v3f(m.muzzle[0] / 100.0f, m.muzzle[1] / 100.0f,
                           m.muzzle[2] / 100.0f);
}

/* ---------------------------------------------------------- world effects */

void wpview_draw_world(WeaponView *v, const Weapon *w, mat4 view_proj,
                   v3 cam_pos, v3 cam_right, v3 cam_up) {
    rd_mvp(view_proj);
    rd_mode(RD_FLAT);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);

    /* The marks a shot left are drawn by decal.c, before this and with the
       same camera -- see main.c. What is left here is the one thing in the
       world that belongs to the WEAPON rather than to the shot: the tether,
       which exists only while this weapon's hook is out.
       사격이 남긴 자국은 decal.c가 그립니다. 이보다 먼저, 같은 카메라로 그립니다. main.c를
       참조하십시오. 이곳에 남은 것은 월드에서 사격이 아니라 *무기*에 속하는 유일한 것,
       즉 로프입니다. 로프는 이 무기의 훅이 나가 있는 동안에만 존재합니다. */

    /* --- the grapple tether: a textured rope, not a flat-coloured line ---
       wp_draw_world is only ever given cam_right/cam_up, not the full camera
       basis, so fwd is recovered from them rather than growing the signature:
       cam_up = cross(cam_right, cam_fwd) inverts to cam_fwd = cross(cam_up,
       cam_right), since right and fwd are orthogonal unit vectors.

       mb_ribbon gives the strip real UVs (u along its length, v across its
       width), so it is drawn with a real material -- RD_SWATCH, unlit and
       unfogged, the same mode the editor's palette swatches use -- rather
       than a solid rd_color. `rope`'s twist bands (see textures.txt) wrap
       around the strip as utile grows with distance, so the tether visibly
       stretches instead of just getting a longer flat line. Swapping the rope
       for a chain, a cable, anything else tileable is a texture-recipe edit,
       not a rendering change. */
    if (w->hook_state != HOOK_IDLE) {
        v3 fwd = v3cross(cam_up, cam_right);
        v3 muzzle = wp_muzzle_world_at(w, wp_hook_muzzle(w), cam_pos, cam_right, cam_up, fwd);

        /* Drawn to the CLAW while it flies and to the anchor once it has
           landed. hook_pos tracks the projectile in flight; on a hit both it
           and hook_target hold the impact point, so the far end is correct in
           either state without a branch. A tether drawn straight to the
           target during flight would arrive before the claw does, which gives
           the throw away.
           비행 중에는 *클로*까지, 착지 후에는 고정점까지 그립니다. hook_pos는 비행
           중인 발사체를 추적하며, 명중 시에는 hook_pos와 hook_target이 모두 충돌
           지점을 담으므로 분기 없이 어느 상태에서든 끝점이 올바릅니다. 비행 중에
           대상까지 곧바로 로프를 그리면 클로보다 먼저 도착해 버려 투척 연출이
           무너집니다. */
        v3 far_end = (w->hook_state == HOOK_FLYING) ? w->hook_pos : w->hook_target;

        float len = v3len(v3sub(far_end, muzzle));
        mb_reset(&v->line_buf);
        mb_ribbon(&v->line_buf, muzzle, far_end, cam_pos,
                 ROPE_WIDTH, len / ROPE_TILE_LENGTH);
        mesh_upload(&v->line_mesh, &v->line_buf, 1);

        rd_mode(RD_SWATCH);
        glActiveTexture(GL_TEXTURE0);
        tex_use(&v->rope_mat);
        mesh_draw(&v->line_mesh);
        rd_mode(RD_FLAT);   /* restore -- the rest of this function assumes it */

        /* The claw itself.
         *
         * Without this the throw is a rope that grows out of the gun toward
         * nothing -- the flight time is there and the tether tracks it, but
         * the thing supposedly doing the travelling is invisible, so the
         * projectile reads as a stretching line rather than as an object
         * thrown. A billboard at hook_pos is what makes the flight legible.
         *
         * It SPINS while flying and stops on impact. The spin is the whole
         * animation: a rotating quad at this pixel size reads as a tumbling
         * hook, and stopping it the instant the claw bites is what sells the
         * bite. hook_timer is already counting for the pull timeout, so the
         * angle costs nothing extra to track.
         *
         * 클로 자체입니다.
         *
         * 이것이 없으면 투척은 총에서 아무것도 없는 곳을 향해 자라나는 로프일 뿐입니다.
         * 비행 시간도 있고 로프도 그것을 추적하지만, 정작 날아간다는 대상이 보이지
         * 않으므로 발사체가 아니라 늘어나는 선처럼 읽힙니다. hook_pos 위치의 빌보드가
         * 비행을 눈에 보이게 만듭니다.
         *
         * 비행 중에는 *회전*하고 충돌 시 멈춥니다. 회전이 곧 애니메이션 전부입니다. 이
         * 픽셀 크기에서 회전하는 사각형은 구르는 갈고리로 읽히며, 클로가 박히는 순간
         * 회전을 멈추는 것이 그 물림을 설득력 있게 만듭니다. hook_timer는 견인 시간
         * 초과 판정을 위해 이미 카운트되고 있으므로 각도 추적에 추가 비용이 들지
         * 않습니다. */
        {
            /* Spinning while in flight, frozen once it has bitten. */
            float spin = (w->hook_state == HOOK_FLYING)
                       ? w->hook_timer * HOOK_CLAW_SPIN : 0.0f;
            float cs = cosf(spin), sn = sinf(spin);

            /* Rotate the billboard's own axes rather than the quad, so the
               claw tumbles in the screen plane while still facing the camera.
               사각형이 아니라 빌보드 자체의 축을 회전시킵니다. 그래야 클로가 카메라를
               향한 채로 화면 평면 안에서 구릅니다. */
            v3 r = v3add(v3scale(cam_right,  cs), v3scale(cam_up, sn));
            v3 u = v3add(v3scale(cam_right, -sn), v3scale(cam_up, cs));

            mb_reset(&v->fx_buf);
            mb_billboard(&v->fx_buf, far_end, r, u,
                         HOOK_CLAW_SIZE, HOOK_CLAW_SIZE);
            mesh_upload(&v->fx_mesh, &v->fx_buf, 1);

            rd_color(0.72f, 0.74f, 0.80f, 1.0f);   /* bare steel, unlit */
            mesh_draw(&v->fx_mesh);
        }
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/**
 * @brief Draws the hand-drawn viewmodel: a screen-aligned quad, Doom-style.
 *
 * ENGLISH
 * -------
 * @param[in] w      The weapon.
 * @param[in] aspect Viewport aspect, so the quad keeps its proportions.
 *
 * The same bob, sway and punch the 3D path uses, applied as 2D offsets rather
 * than as a transform. That is what keeps the feel across the switch: the
 * numbers driving the motion are the weapon's, not the renderer's, so a gun
 * that felt right as a model feels the same as a drawing.
 *
 * @note Drawn under an ORTHO projection covering the viewport, so the sprite
 *       occupies a fixed share of the screen at any resolution. A perspective
 *       projection would make the gun's apparent size depend on the FOV, which
 *       is correct for a 3D model sitting in front of the eye and wrong for a
 *       drawing that is meant to be a fixed part of the frame.
 * @note Alpha-tested through ::RD_SPRITE rather than blended, because the art's
 *       alpha is a silhouette mask. Blending would leave a fringe of half-lit
 *       pixels around every edge the artist drew sharp.
 *
 * 한국어
 * ------
 * @brief 손으로 그린 뷰 모델을 그립니다. Doom 방식의 화면 정렬 쿼드입니다.
 *
 * 3D 경로가 쓰는 것과 동일한 흔들림·스웨이·반동을 변환 행렬이 아니라 2D 오프셋으로
 * 적용합니다. 그것이 전환을 넘어 감각을 유지하는 방법입니다. 움직임을 구동하는 수치는
 * 렌더러가 아니라 무기의 것이므로, 모델일 때 좋았던 총기는 그림이 되어도 같게 느껴집니다.
 *
 * @note 뷰포트를 덮는 *정사영* 투영으로 그리므로, 스프라이트가 어떤 해상도에서도 화면의
 *       일정한 비율을 차지합니다. 원근 투영은 총기의 겉보기 크기를 시야각에 의존하게
 *       만드는데, 눈앞에 놓인 3D 모델에는 옳지만 프레임의 고정된 일부여야 하는 그림에는
 *       틀립니다.
 * @note 블렌딩이 아니라 ::RD_SPRITE의 알파 테스트를 씁니다. 아트의 알파가 실루엣
 *       마스크이기 때문입니다. 블렌딩하면 아티스트가 선명하게 그린 모든 가장자리에 반쯤
 *       밝은 픽셀의 테두리가 남습니다.
 */
static void draw_view_sprite(WeaponView *v, const Weapon *w, float aspect) {
    /* A 1x1 box with y up, so every offset below is a fraction of the screen
       and none of them has to know the pixel size.
       y가 위로 향하는 1x1 상자입니다. 아래의 모든 오프셋이 화면에 대한 비율이 되며, 어느
       것도 픽셀 크기를 알 필요가 없습니다. */
    mat4 proj = mat4_ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);

    /* THE QUAD IS DOOM'S SCREEN, not a box sized to taste.
       The cell spans Doom's full 320-unit width and its bottom 144 rows, and
       the frame's place in the cell is its place on that screen -- so this has
       to put the cell exactly where Doom's screen would be, or the offsets the
       art was drawn with land somewhere else.
       Doom's 320x200 was displayed at 4:3, so the screen is (4/3)*height wide
       whatever the window's shape, and the weapon layer is letterboxed inside
       a widescreen viewport rather than stretched across it. Height is the
       cell's share of those 200 rows. Neither number is a taste dial: change
       one and every weapon moves off the position its artist chose.
       쿼드는 취향껏 정한 상자가 아니라 *Doom의 화면*입니다. 셀은 Doom의 320단위 너비
       전체와 아래 144행을 덮으며, 프레임의 셀 안 위치가 곧 그 화면에서의 위치입니다.
       따라서 이 코드는 셀을 Doom 화면이 있었을 자리에 정확히 놓아야 하고, 그러지 않으면
       아트가 지니고 온 오프셋이 엉뚱한 곳에 떨어집니다. Doom의 320x200은 4:3으로
       표시되었으므로 창의 모양과 무관하게 화면 너비는 높이의 4/3이며, 무기 레이어는
       와이드스크린 뷰포트에 늘어나지 않고 레터박스로 들어갑니다. */
    /* The viewport IS Doom's 3D view, so the cell's share of it is the cell's
       share of those 168 rows. Width follows from the 320x200 screen being
       displayed at 4:3: our height covers VIEW rows, so the full screen is
       (4/3)*FULL/VIEW of it across. Matching FULL instead of VIEW is what left
       the shotgun a fifth too small with its bottom balanced on the edge.
       뷰포트가 곧 Doom의 3D 뷰이므로, 셀이 차지하는 비율은 그 168행 중 셀의 비율입니다.
       너비는 320x200 화면이 4:3으로 표시된다는 사실에서 나옵니다. 우리 높이가 VIEW행을
       담으므로 화면 전체는 그 (4/3)*FULL/VIEW배만큼 넓습니다. VIEW가 아니라 FULL에
       맞춘 것이 샷건을 5분의 1만큼 작게, 아래를 가장자리에 걸터앉게 만든 원인입니다. */
    const float SH = (float)(WPN_DOOM_VIEW - WPN_DOOM_TOP) / (float)WPN_DOOM_VIEW;
    float sw = (4.0f / 3.0f) * (float)WPN_DOOM_FULL / (float)WPN_DOOM_VIEW
             / (aspect > 0.01f ? aspect : 1.0f);

    /* The weapon's own motion, as screen fractions. The scales are small
       because a viewmodel that swings a visible fraction of the screen reads
       as the camera being loose rather than as a held weapon.
       무기 자신의 움직임을 화면 비율로 표현합니다. 배율이 작은 이유는, 화면의 눈에 띄는
       비율만큼 흔들리는 뷰 모델은 쥐고 있는 무기가 아니라 카메라가 헐거운 것처럼 읽히기
       때문입니다. */
    float bx = sinf(w->bob_phase)         * 0.012f;
    float by = -fabsf(cosf(w->bob_phase)) * 0.010f;
    float cx = 0.5f + bx + w->sway_x * 0.9f;
    float cy = 0.0f + by + w->sway_y * 0.9f - w->punch * 0.35f;

    float x0 = cx - sw * 0.5f, x1 = cx + sw * 0.5f;
    float y0 = cy,             y1 = cy + SH;

    int frame = wp_sprite_frame(w);
    float u0, v0, u1, v1;
    weapon_uv(w->cur, frame, &u0, &v0, &u1, &v1);

    mb_reset(&v->fx_buf);
    v3 n = v3f(0, 0, 1);
    mb_vtx(&v->fx_buf, v3f(x0, y0, 0), n, u0, v0);
    mb_vtx(&v->fx_buf, v3f(x1, y0, 0), n, u1, v0);
    mb_vtx(&v->fx_buf, v3f(x1, y1, 0), n, u1, v1);
    mb_vtx(&v->fx_buf, v3f(x0, y0, 0), n, u0, v0);
    mb_vtx(&v->fx_buf, v3f(x1, y1, 0), n, u1, v1);
    mb_vtx(&v->fx_buf, v3f(x0, y1, 0), n, u0, v1);
    mesh_upload(&v->fx_mesh, &v->fx_buf, 1);

    rd_mvp(proj);
    rd_mode(RD_SPRITE2D);
    rd_color(1.0f, 1.0f, 1.0f, 0.0f);
    rd_snap(0.0f, 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, weapon_atlas());
    glDisable(GL_CULL_FACE);
    mesh_draw(&v->fx_mesh);
    glEnable(GL_CULL_FACE);

    /* --- the muzzle flash, at the point the DRAWING marked ---
       Skipped entirely when the art carried no marker: a flash at a guessed
       position is worse than none, because it tells the artist the marker is
       working when it is not.
       그림이 표식을 담지 않았으면 완전히 건너뜁니다. 추측한 위치의 화염은 없는 것보다
       나쁩니다. 표식이 동작하지 않는데도 동작한다고 아티스트에게 알려 주기 때문입니다. */
    float mu, mv;
    if (w->flash > 0.0f && weapon_muzzle(w->cur, frame, &mu, &mv)) {
        float k  = w->flash / FLASH_TIME;
        float fs = (0.10f + 0.07f * k) * w->flash_scale;
        float fx = x0 + mu * (x1 - x0);
        float fy = y0 + mv * (y1 - y0);

        mb_reset(&v->fx_buf);
        for (int i = 0; i < 3; i++) {
            float a = w->flash_roll + i * (M_PI_F / 3.0f);
            v3 r = v3f(cosf(a) / (aspect > 0.01f ? aspect : 1.0f), sinf(a), 0.0f);
            v3 u = v3f(-sinf(a) / (aspect > 0.01f ? aspect : 1.0f), cosf(a), 0.0f);
            mb_billboard(&v->fx_buf, v3f(fx, fy, 0.0f), r, u, fs, fs);
        }
        mesh_upload(&v->fx_mesh, &v->fx_buf, 1);

        rd_mode(RD_FLAT);
        rd_color(1.0f, 0.80f, 0.38f, k * 0.85f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        mesh_draw(&v->fx_mesh);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}


void wpview_draw_view(WeaponView *v, const Weapon *w, float aspect) {
    /* The view model belongs to the WORLD pass: it shares the scene's
       lighting, and a crisp weapon over a pixelated world reads as a bug.
       뷰 모델은 *월드* 패스에 속합니다. 장면의 조명을 공유하며, 픽셀화된 월드 위의
       선명한 무기는 버그처럼 보입니다. */
    DIAG_WANT_WORLD_PASS(post_in_world_pass());

    /* --- hand-drawn art REPLACES the model, when it exists ---------------
       One question, asked once a frame, answered by whether the files are
       there. Adding art is dropping `gun0.png` into assets/sprites/ and
       removing it is deleting the file; nothing else in the project changes
       and there is no flag to keep in agreement with the directory.

       The depth buffer is still cleared first, because the sprite path draws
       over the world exactly as the model did.

       손으로 그린 아트가 있으면 모델을 *대체*합니다. 프레임마다 한 번 묻는 하나의
       질문이며, 답은 파일이 있는지 여부입니다. 아트를 추가하는 것은 assets/sprites/에
       `gun0.png`를 넣는 것이고 제거하는 것은 파일을 지우는 것입니다. 프로젝트의 다른
       무엇도 바뀌지 않으며, 디렉터리와 일치시켜야 할 플래그도 없습니다.

       깊이 버퍼는 여전히 먼저 지웁니다. 스프라이트 경로도 모델과 똑같이 월드 위에
       그리기 때문입니다. */
    if (weapon_has_art()) {
        glClear(GL_DEPTH_BUFFER_BIT);
        draw_view_sprite(v, w, aspect);
        return;
    }

    /* A narrower FOV than the world camera keeps the gun from looking
       fish-eyed, and a fresh depth buffer stops it clipping into walls. */
    glClear(GL_DEPTH_BUFFER_BIT);
    mat4 proj  = mat4_perspective(g_gun_pose.fov, aspect, 0.005f, 4.0f);
    mat4 model = wp_gun_matrix(w);

    rd_mvp(mat4_mul(proj, model));
    rd_mode(RD_VIEWMODEL);
    /* No vertex snap on the gun. It sits at a fixed distance in the centre of
       the screen, so snapping makes it vibrate continuously in the one place
       the eye is least willing to forgive it -- the world wobbles because the
       camera moves relative to it, but the gun never moves relative to the
       camera at all.
       총기에는 정점 스냅을 적용하지 않습니다. 화면 중앙의 고정된 거리에 있으므로 스냅하면
       눈이 가장 용납하지 않는 바로 그 위치에서 계속 진동합니다. 월드는 카메라가 그에 대해
       상대적으로 움직이기 때문에 흔들리지만, 총기는 카메라에 대해 전혀 움직이지 않습니다. */
    rd_snap(0.0f, 0.0f);
    glActiveTexture(GL_TEXTURE0);

    /* One draw per material. At ~200 vertices the extra calls cost nothing,
       and this avoids a texture atlas -- which would break badly here, since
       the UVs tile several times per unit and would sample across cells. */
    for (int r = 0; r < v->gun_range_count; r++) {
        tex_use(&v->gun_tex[r]);
        mesh_draw_range(&v->gun_mesh, v->gun_ranges[r].first, v->gun_ranges[r].count);
    }

    /* --- muzzle flash: a star of quads sharing the muzzle plane, each
           rotated within it, plus one flare lying along the barrel --- */
    if (w->flash > 0.0f) {
        float k = w->flash / FLASH_TIME;
        float s = (0.16f + 0.10f * k) * w->flash_scale;

        mb_reset(&v->fx_buf);
        v3 tip = w->muzzle;
        for (int i = 0; i < 3; i++) {
            float a = w->flash_roll + i * (M_PI_F / 3.0f);
            v3 r = v3f(cosf(a), sinf(a), 0.0f);
            v3 u = v3f(-sinf(a), cosf(a), 0.0f);
            mb_billboard(&v->fx_buf, tip, r, u, s, s);
        }
        /* A short flare along the barrel sells the light better than the
           star alone. */
        mb_billboard(&v->fx_buf,
                     v3f(tip.x, tip.y, tip.z - s * 0.35f),
                     v3f(1, 0, 0), v3f(0, 0, -1), s * 0.7f, s * 1.4f);

        mesh_upload(&v->fx_mesh, &v->fx_buf, 1);
        rd_mode(RD_FLAT);
        rd_color(1.0f, 0.80f, 0.38f, k * 0.85f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        /* The star quads always face the camera, but the barrel flare is
           edge-on and its winding flips with the view -- culling would drop
           it half the time. */
        glDisable(GL_CULL_FACE);
        mesh_draw(&v->fx_mesh);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}

/* ------------------------------------------------------------------- HUD */

void wpview_draw_hud(WeaponView *v, const Weapon *w, float aspect, int hook_ready) {
    /* The crosshair belongs to the UI pass: a dithered, magnified reticle is
       unreadable, and the range brackets are one pixel wide.
       조준점은 *UI* 패스에 속합니다. 디더링되고 확대된 조준선은 읽을 수 없으며, 사거리
       괄호는 1픽셀 폭입니다. */
    DIAG_WANT_UI_PASS(post_in_world_pass());
    /* Drawn straight in clip space: uMVP only corrects for aspect so the
       crosshair stays square. */
    mat4 ndc = mat4_scale(v3f(1.0f / aspect, 1.0f, 1.0f));
    rd_mvp(ndc);
    rd_mode(RD_FLAT);

    /* Gap tracks spread, so the crosshair bloom is the actual accuracy. */
    float gap = 0.012f + w->spread * 1.1f;
    float len = 0.022f;

    mb_reset(&v->line_buf);
    mb_line(&v->line_buf, v3f(-gap - len, 0, 0), v3f(-gap, 0, 0));
    mb_line(&v->line_buf, v3f( gap, 0, 0),       v3f( gap + len, 0, 0));
    mb_line(&v->line_buf, v3f(0, -gap - len, 0), v3f(0, -gap, 0));
    mb_line(&v->line_buf, v3f(0,  gap, 0),       v3f(0,  gap + len, 0));
    mesh_upload(&v->line_mesh, &v->line_buf, 1);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);
    rd_color(0.95f, 0.97f, 1.0f, 0.75f);
    mesh_draw_lines(&v->line_mesh);

    /* --- the hook's range indicator --------------------------------------
       Four corner brackets around the crosshair, drawn only when a throw
       right now would connect. HOOK_RANGE is 40m and nothing else on screen
       says where that ends, so without this the only way to learn the range
       is to throw and miss -- and a miss costs the cooldown.

       Brackets rather than a colour change on the crosshair itself: the
       crosshair already encodes the shotgun's spread through its gap, and
       overloading the same four lines with a second meaning would make both
       harder to read. A separate mark can be ignored when you are not
       thinking about the hook.

       Drawn OUTSIDE the crosshair's own arms, so it never collides with the
       spread bloom however wide that grows.

       훅의 사거리 표시입니다.

       조준점 주위의 네 모서리 괄호이며, 지금 발사하면 명중하는 경우에만 그려집니다.
       HOOK_RANGE는 40m인데 화면의 어떤 요소도 그 끝이 어디인지 알려 주지 않으므로, 이
       표시가 없으면 사거리를 아는 유일한 방법은 던져서 빗맞히는 것뿐이며 빗나감은
       쿨다운을 소모합니다.

       조준점 자체의 색을 바꾸지 않고 괄호를 쓴 이유: 조준점은 이미 간격으로 샷건의
       산포도를 표현하고 있으며, 같은 네 개의 선에 두 번째 의미를 겹치면 양쪽 모두
       읽기 어려워집니다. 별도의 표식은 훅을 생각하지 않을 때 무시할 수 있습니다.

       조준점의 팔 *바깥쪽*에 그리므로, 산포도가 아무리 넓어져도 겹치지 않습니다. */
    if (hook_ready) {
        float r = gap + len + 0.018f;   /* clear of the widest bloom */
        float a = 0.010f;               /* arm length of each bracket */

        mb_reset(&v->line_buf);
        /* Top-left, top-right, bottom-left, bottom-right: two strokes each,
           so the mark reads as a frame rather than as four more ticks.
           좌상, 우상, 좌하, 우하 각각 두 획씩입니다. 그래야 표식이 눈금 네 개가 아니라
           하나의 틀로 읽힙니다. */
        mb_line(&v->line_buf, v3f(-r, r, 0), v3f(-r + a, r, 0));
        mb_line(&v->line_buf, v3f(-r, r, 0), v3f(-r, r - a, 0));
        mb_line(&v->line_buf, v3f( r, r, 0), v3f( r - a, r, 0));
        mb_line(&v->line_buf, v3f( r, r, 0), v3f( r, r - a, 0));
        mb_line(&v->line_buf, v3f(-r, -r, 0), v3f(-r + a, -r, 0));
        mb_line(&v->line_buf, v3f(-r, -r, 0), v3f(-r, -r + a, 0));
        mb_line(&v->line_buf, v3f( r, -r, 0), v3f( r - a, -r, 0));
        mb_line(&v->line_buf, v3f( r, -r, 0), v3f( r, -r + a, 0));
        mesh_upload(&v->line_mesh, &v->line_buf, 1);

        glLineWidth(1.0f);
        rd_color(0.45f, 0.95f, 0.60f, 0.85f);   /* green: the hook will bite */
        mesh_draw_lines(&v->line_mesh);
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
