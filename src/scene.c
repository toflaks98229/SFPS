/**
 * @file scene.c
 * @brief Implements the per-frame draw passes lifted out of WinMain.
 *
 * ENGLISH
 * -------
 * Nothing here is new behaviour. Each function is one of the blocks that used
 * to sit inline in the frame loop, moved without changing what it draws or the
 * order it draws in -- the pass order is still main.c's decision, and it is
 * still load-bearing.
 *
 * What the move buys: the buffers are now owned and freed by one pair of
 * functions, each pass can be read without scrolling past the others, and the
 * magic numbers that were scattered through the loop are named constants at
 * the top of this file.
 *
 * 한국어
 * ------
 * 이곳에 새로운 동작은 없습니다. 각 함수는 프레임 루프 안에 인라인으로 있던 블록
 * 하나이며, 무엇을 그리는지도 어떤 순서로 그리는지도 바꾸지 않고 옮긴 것입니다. 패스
 * 순서는 여전히 main.c의 결정이며, 여전히 구조적으로 중요합니다.
 *
 * 옮겨서 얻는 것: 버퍼를 한 쌍의 함수가 소유하고 해제하게 되었고, 각 패스를 다른
 * 패스를 지나쳐 스크롤하지 않고 읽을 수 있으며, 루프 곳곳에 흩어져 있던 매직 넘버가
 * 이 파일 상단의 명명된 상수가 되었습니다.
 */

#include "scene.h"
#include "proj.h"   /* the player's grenades and bolts */
#include "enemy.h"
#include "pickup.h"
#include "sprite.h"
#include "font.h"
#include "post.h"     /* post_in_world_pass -- the pass-boundary guards */
#include "menu.h"     /* the rows the ESC menu draws, read rather than copied */
#include "diag.h"

/* ------------------------------------------------------------------ tuning */

/* --- monster projectiles ---
   A bolt is drawn as two groups of camera-facing quads: wide dim petals that
   carry the round shape, and small bright ones that give it a hot core. A
   single quad reads as a glowing SQUARE, which is exactly what it looked like
   before overlapping several of them.
   볼트는 카메라를 향하는 두 그룹의 사각형으로 그려집니다. 넓고 흐린 꽃잎이 둥근 형태를
   만들고, 작고 밝은 것이 뜨거운 중심을 만듭니다. 사각형 하나는 빛나는 *정사각형*으로
   보이며, 여러 개를 겹치기 전에는 정확히 그렇게 보였습니다. */
#define SHOT_HALOS      3       /* wide dim petals -- these carry the round shape */
#define SHOT_CORES      2       /* small bright ones -- a star, not a white square */
#define SHOT_HALO_SIZE  0.62f
#define SHOT_CORE_SIZE  0.22f
#define SHOT_SPIN       2.3f    /* radians per second of remaining life */

/* --- pickups --- */
/* Floor items are drawn as a fixed square in world space, so their apparent
   size lives HERE rather than in the art: every drawing already fills as much
   of its 48x48 cell as the one shared scale allows, and a bigger drawing would
   only clip.
   Up from 0.5m, which had them reading as litter. The cell edge is what a
   drawing filling its whole cell measures, so this is a ceiling and not a
   size every item takes: the launcher fills its cell and comes out 2.00m, the
   medikit 1.00m, a box of shells 0.46m. The item that IS small stays small,
   which is the whole reason they share one scale rather than each fitting its
   own cell.

   바닥 아이템은 월드에서 고정된 정사각형으로 그려지므로, 겉보기 크기는 아트가 아니라
   *이곳*에 있습니다. 모든 그림은 이미 하나의 공용 배율이 허용하는 만큼 48x48 셀을 채우고
   있어, 더 크게 그리면 잘리기만 합니다. 0.5m에서 올렸습니다. 그 크기에서는 쓰레기처럼
   보였습니다. 셀의 변은 셀을 가득 채운 그림이 갖는 크기이므로 이것은 상한이지 모든
   아이템이 갖는 크기가 아닙니다. 발사기는 셀을 채워 2.00m, 구급상자는 1.00m, 산탄 상자는
   0.46m입니다. 작은 것은 작게 남으며, 그것이 각자 자기 셀에 맞추는 대신 하나의 배율을
   공유하는 이유 전부입니다. */
#define PICKUP_SIZE     2.0f    /* billboard edge, metres */

/* Clearance under the item. A fixed gap rather than a fraction of the size,
   because it is a hover cue and not a property of the object -- scaling it
   with the item would have floated the launcher half a metre off the ground.
   아이템 아래의 여유입니다. 크기의 비율이 아니라 고정된 간격인 이유는, 이것이 물체의
   속성이 아니라 떠 있음을 알리는 신호이기 때문입니다. 아이템과 함께 키웠다면 발사기가
   지면에서 반 미터 떠 있었을 것입니다. */
#define PICKUP_FLOAT    0.17f

/* DERIVED, so the two cannot disagree. The billboard is centred on this, so a
   lift written independently has to be kept in step with half the size by
   hand -- and when it is not, the item's bottom half goes through the floor.
   That is exactly what tripling the size did before this became a formula.
   유도된 값이므로 둘이 어긋날 수 없습니다. 빌보드가 이 값을 중심으로 놓이므로, 따로 쓴
   높이는 크기의 절반과 손으로 맞춰 두어야 하며, 맞지 않으면 아이템의 아래 절반이 바닥을
   뚫고 내려갑니다. 이것이 수식이 되기 전에 크기를 3배로 했을 때 실제로 벌어진 일입니다. */
#define PICKUP_LIFT     (PICKUP_SIZE * 0.5f + PICKUP_FLOAT)

#define PICKUP_BOB      0.06f   /* bob amplitude, metres */
#define PICKUP_BOB_RATE 2.2f

/* --- monsters --- */
#define WALK_CYCLE_RATE 8.0f    /* how fast the two-frame walk alternates */
#define CORPSE_FADE     0.6f    /* seconds a corpse spends darkening */

/* --- HUD layout ---
   Pixels from the viewport edge, and glyph scales. Named because they were
   repeated literals in two places each: the health and ammo readouts must
   agree on their baseline or they visibly fail to line up.
   뷰포트 가장자리로부터의 픽셀 거리와 글리프 배율입니다. 각각 두 곳에서 반복되던
   리터럴이므로 이름을 붙였습니다. 체력과 탄약 표시는 기준선이 일치해야 하며, 그렇지
   않으면 눈에 띄게 어긋납니다. */
#define HUD_MARGIN      18.0f
#define HUD_BASELINE    40.0f   /* up from the bottom edge */
#define HUD_TEXT_SIZE   3.5f
#define HURT_FLASH_MAX  0.4f    /* alpha of the full-screen wash at full hurt */

/* --- win screen --- */
#define WIN_DIM         0.55f   /* how far the frozen world is darkened */

/* --- Death and title screens / 사망 및 타이틀 화면 --- */

#define DEATH_DIM       0.62f   /* darker than the win screen, and red */
#define DEATH_FADE      1.2f    /* seconds for the overlay to reach full */
#define DEATH_TITLE_SIZE 7.0f
#define DEATH_HINT_SIZE  1.6f

#define TITLE_DIM       0.70f
#define TITLE_SIZE      9.0f
#define TITLE_SUB_SIZE  1.8f
#define TITLE_HINT_SIZE 1.6f

/* --- ESC menu / ESC 메뉴 --- */

/* Dimmed harder than the win screen. The win screen wants the last frame
   readable underneath -- it is the point of freezing rather than clearing --
   while the menu wants attention on the rows. The world is still visible, so
   the player can see the game is paused rather than gone.
   승리 화면보다 강하게 어둡게 합니다. 승리 화면은 아래의 마지막 프레임이 보이기를
   원하지만(지우지 않고 정지시키는 이유가 그것입니다), 메뉴는 행에 주목하기를 원합니다.
   월드는 여전히 보이므로 플레이어는 게임이 사라진 것이 아니라 멈췄음을 알 수 있습니다. */
#define MENU_DIM         0.72f
#define MENU_TITLE_SIZE  4.5f
#define MENU_ROW_SIZE    2.6f
#define MENU_ROW_STEP    38.0f  /* pixels between rows */
#define MENU_HINT_SIZE   1.3f

/* The value column sits at a fixed offset from the centre rather than being
   right-aligned to the longest label. Right-aligning makes the whole column
   jump when a value changes width -- "ON" to "OFF" is enough -- and a menu
   whose layout moves while being read is harder to use than one slightly
   uneven column.
   값 열은 가장 긴 레이블에 맞춰 오른쪽 정렬하지 않고 중앙에서 고정된 거리에 놓입니다.
   오른쪽 정렬하면 값의 폭이 바뀔 때마다 열 전체가 움직이는데("ON"에서 "OFF"로 바뀌는
   것만으로 충분합니다), 읽는 도중 배치가 움직이는 메뉴는 약간 고르지 않은 열보다 쓰기
   어렵습니다. */
#define MENU_LABEL_X   (-150.0f)
#define MENU_VALUE_X     40.0f
#define WIN_TITLE_SIZE  7.0f
#define WIN_STAT_SIZE   2.2f
#define WIN_HINT_SIZE   1.4f

/* ---------------------------------------------------------------- lifecycle */

/* Vertices the level's scratch buffer starts at. Large enough that a
   hand-authored level never grows it, and it is freed at shutdown either way.
   레벨 임시 버퍼의 초기 정점 수입니다. 사람이 제작한 레벨이 이를 확장시키지 않을 만큼
   충분히 크며, 어느 쪽이든 종료 시 해제됩니다. */
#define LEVEL_BUF_VERTS 16384

void scene_init(Scene *s) {
    /* Sized from the cap of what each draws, so a full level never grows one
       mid-frame. mb_vtx drops vertices rather than growing, so a buffer that
       is too small loses geometry instead of allocating -- which is reported,
       but is still a hole in the world.
       각각이 그리는 대상의 상한을 기준으로 크기를 정하므로, 가득 찬 레벨에서도 프레임
       도중에 확장되지 않습니다. mb_vtx는 확장하는 대신 정점을 버리므로, 너무 작은
       버퍼는 할당 대신 지오메트리를 잃습니다. 보고되기는 하지만 여전히 월드에 뚫린
       구멍입니다. */
    mb_init(&s->enemy_buf,  ENEMY_MAX * 6);
    mb_init(&s->pickup_buf, PICKUP_MAX * 6);
    mb_init(&s->shot_buf,   ENEMY_MAX_SHOTS * (SHOT_HALOS + SHOT_CORES) * 6);
    /* 1024 vertices = 170 glyphs, because text_run draws a whole line through
       this buffer and mb_vtx DROPS vertices rather than growing. At the old
       256 a line was cut at 42 characters, which the credits notice hit in the
       middle of a word -- a licence that visibly trails off is worse than none.
       Reported by DIAG_VERTEX_BUF, but a HUD nobody profiles is where a
       reported overflow goes unread.
       1024 정점 = 글리프 170개입니다. text_run이 이 버퍼로 한 줄 전체를 그리는데 mb_vtx는
       확장하지 않고 정점을 *버리기* 때문입니다. 이전의 256에서는 42자에서 줄이 잘렸고,
       크레딧 고지가 단어 중간에서 그 한계에 걸렸습니다. 눈에 띄게 끊기는 라이선스는 없는
       것보다 나쁩니다. */
    mb_init(&s->hud_buf,    1024);
    mb_init(&s->level_buf,  LEVEL_BUF_VERTS);

    s->enemy_mesh  = (Mesh){0};
    s->pickup_mesh = (Mesh){0};
    s->shot_mesh   = (Mesh){0};
    s->hud_mesh    = (Mesh){0};
    s->level_mesh  = (Mesh){0};
    s->level_range_count = 0;

    s->sprite_tex = sprite_atlas();
    s->pickup_tex = pickup_atlas();
}

void scene_free(Scene *s) {
    /* mb_free is safe on an already-freed buffer, so this is safe twice. */
    mb_free(&s->enemy_buf);
    mb_free(&s->pickup_buf);
    mb_free(&s->shot_buf);
    mb_free(&s->hud_buf);
    mb_free(&s->level_buf);
}

/* ----------------------------------------------------------- level geometry */

void scene_build_level(Scene *s, const Level *l, int dynamic) {
    mb_reset(&s->level_buf);
    s->level_range_count = level_geometry(&s->level_buf, l,
                                          s->level_ranges, LVL_MAX_RANGES);
    mesh_upload(&s->level_mesh, &s->level_buf, dynamic);

    /* Materials last: level_geometry decides how many runs there are, and each
       run names the material it wants.
       재질은 마지막입니다. 구간의 개수는 level_geometry가 결정하며, 각 구간이 사용할
       재질의 이름을 지정합니다. */
    for (int i = 0; i < s->level_range_count; i++)
        s->level_tex[i] = tex_mat(s->level_ranges[i].mat);
}

void scene_draw_level(const Scene *s, mat4 vp, v3 eye, const Level *l) {
    DIAG_WANT_WORLD_PASS(post_in_world_pass());

    /* Convert the level's lights into the two vec4 arrays the shader wants.
       Done here rather than at load because the packing is a rendering detail:
       Level stores what the author wrote, in file units, and nothing in the
       simulation half should have to know the shader's uniform layout.
       레벨의 광원을 셰이더가 요구하는 두 개의 vec4 배열로 변환합니다. 로드 시점이 아니라
       이곳에서 하는 이유는 이 패킹이 렌더링의 세부 사항이기 때문입니다. Level은 제작자가
       작성한 것을 파일 단위 그대로 저장하며, 시뮬레이션 영역의 어떤 것도 셰이더의 유니폼
       배치를 알 필요가 없습니다. */
    float lpos[RD_MAX_LIGHTS * 4], lcol[RD_MAX_LIGHTS * 4];
    int nl = l ? l->n_lights : 0;
    if (nl > RD_MAX_LIGHTS) nl = RD_MAX_LIGHTS;
    for (int i = 0; i < nl; i++) {
        const Light *L = &l->lights[i];
        lpos[i*4+0] = L->x * 0.01f;
        lpos[i*4+1] = L->y * 0.01f;
        lpos[i*4+2] = L->z * 0.01f;
        lpos[i*4+3] = L->radius * 0.01f;
        lcol[i*4+0] = L->r / 255.0f;
        lcol[i*4+1] = L->g / 255.0f;
        lcol[i*4+2] = L->b / 255.0f;
        lcol[i*4+3] = L->power * 0.01f;
    }

    rd_mode(RD_WORLD);
    rd_mvp(vp);
    rd_eye(eye);
    rd_lights(lpos, lcol, nl);
    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < s->level_range_count; i++) {
        tex_use(&s->level_tex[i]);
        mesh_draw_range(&s->level_mesh, s->level_ranges[i].first,
                        s->level_ranges[i].count);
    }
}

/* --------------------------------------------------------------- world pass */

void scene_draw_enemies(Scene *s, mat4 vp, v3 eye, v3 cam_right) {
    DIAG_WANT_WORLD_PASS(post_in_world_pass());

    int n = enemy_count();
    mb_reset(&s->enemy_buf);

    for (int i = 0; i < n; i++) {
        const Enemy *m = enemy_at(i);
        if (!m->active) continue;

        const MonType *S = mon_stats(m->type);

        /* Frame from state, walk cycle from the animation clock. */
        int fr = SPR_WALK0;
        if (m->state == E_DEAD)        fr = SPR_DEAD;
        else if (m->state == E_HURT)   fr = SPR_HURT;
        else if (m->state == E_ATTACK) fr = (m->timer < S->windup)
                                            ? SPR_ATTACK : SPR_WALK0;
        else if (m->state == E_CHASE)
            fr = (sinf(m->anim * WALK_CYCLE_RATE) > 0.0f) ? SPR_WALK0 : SPR_WALK1;

        float u0, v0, u1, v1;
        sprite_uv(m->type, fr, &u0, &v0, &u1, &v1);

        float h = S->height;
        float w = h * S->aspect;
        v3 centre = v3f(m->pos.x, m->pos.y + h * 0.5f, m->pos.z);
        mb_billboard_uv(&s->enemy_buf, centre, cam_right, v3f(0,1,0),
                        w, h, u0, v0, u1, v1);
    }

    if (!s->enemy_buf.count) return;

    mesh_upload(&s->enemy_mesh, &s->enemy_buf, 1);
    rd_mode(RD_SPRITE);
    rd_mvp(vp);
    rd_eye(eye);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s->sprite_tex);
    glDisable(GL_CULL_FACE);

    /* Per-monster tint: a hit flashes white, a corpse fades to dark and sinks
       over its half-second. One draw call each, so each gets its own uColor --
       there are at most a few dozen.
       몬스터별 색조입니다. 피격은 흰색으로 번쩍이고, 시체는 0.5초에 걸쳐 어두워지며
       가라앉습니다. 각각 그리기 호출이 하나이므로 고유한 uColor를 받습니다. 많아야 몇십
       마리뿐입니다. */
    glBindVertexArray(s->enemy_mesh.vao);
    int q = 0;
    for (int i = 0; i < n; i++) {
        const Enemy *m = enemy_at(i);
        if (!m->active) continue;
        float flash = m->flash > 0.0f ? m->flash : 0.0f;
        float shade = 1.0f;
        if (m->state == E_DEAD) shade = 0.35f + 0.65f * (m->timer / CORPSE_FADE);
        rd_color(shade, shade, shade, flash);
        glDrawArrays(GL_TRIANGLES, q * 6, 6);
        q++;
    }
    glEnable(GL_CULL_FACE);
}

void scene_draw_pickups(Scene *s, mat4 vp, v3 eye, v3 cam_right) {
    DIAG_WANT_WORLD_PASS(post_in_world_pass());

    int pn = pickup_count();
    mb_reset(&s->pickup_buf);

    for (int i = 0; i < pn; i++) {
        const Pickup *p = pickup_at(i);
        if (!p->active) continue;
        float u0, v0, u1, v1;
        pickup_uv(p->kind, &u0, &v0, &u1, &v1);
        float bob = PICKUP_BOB * sinf(p->anim * PICKUP_BOB_RATE);
        v3 centre = v3f(p->pos.x, p->pos.y + PICKUP_LIFT + bob, p->pos.z);
        mb_billboard_uv(&s->pickup_buf, centre, cam_right, v3f(0,1,0),
                        PICKUP_SIZE, PICKUP_SIZE, u0, v0, u1, v1);
    }

    if (!s->pickup_buf.count) return;

    mesh_upload(&s->pickup_mesh, &s->pickup_buf, 1);
    rd_mode(RD_SPRITE);
    rd_mvp(vp);
    rd_eye(eye);
    rd_color(1.0f, 1.0f, 1.0f, 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s->pickup_tex);
    glDisable(GL_CULL_FACE);
    mesh_draw(&s->pickup_mesh);
    glEnable(GL_CULL_FACE);
}

void scene_draw_shots(Scene *s, mat4 vp, v3 cam_right, v3 cam_up) {
    DIAG_WANT_WORLD_PASS(post_in_world_pass());

    const int quads  = SHOT_HALOS + SHOT_CORES;
    const int stride = quads * 6;
    int sn = enemy_shot_count(), live = 0;

    mb_reset(&s->shot_buf);
    for (int i = 0; i < sn; i++) {
        const Shot *sh = enemy_shot_at(i);
        if (!sh->active) continue;

        /* Spin each bolt by its own remaining life, so a volley does not look
           like one sprite stamped several times. */
        for (int q = 0; q < quads; q++) {
            int   core = q >= SHOT_HALOS;
            int   n    = core ? SHOT_CORES : SHOT_HALOS;
            int   k    = core ? q - SHOT_HALOS : q;
            float size = core ? SHOT_CORE_SIZE : SHOT_HALO_SIZE;
            /* Spread the quads over a QUARTER turn, not a half: a square maps
               onto itself every 90 degrees, so two quads 180/2 apart are the
               same square drawn twice -- which is why the core first came out
               as a plain diamond.
               사각형을 반 바퀴가 아니라 *4분의 1* 바퀴에 걸쳐 배치합니다. 정사각형은
               90도마다 자기 자신과 겹치므로, 180/2도 떨어진 두 사각형은 같은 사각형을
               두 번 그린 것입니다. 중심부가 처음에 평범한 마름모로 나온 이유입니다. */
            float a = sh->life * SHOT_SPIN + k * (M_PI_F * 0.5f / n);
            v3 r = v3add(v3scale(cam_right,  cosf(a)),
                         v3scale(cam_up,     sinf(a)));
            v3 u = v3add(v3scale(cam_right, -sinf(a)),
                         v3scale(cam_up,     cosf(a)));
            mb_billboard(&s->shot_buf, sh->pos, r, u, size, size);
        }
        live++;
    }

    if (!live) return;

    mesh_upload(&s->shot_mesh, &s->shot_buf, 1);
    rd_mode(RD_FLAT);
    rd_mvp(vp);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);          /* glows do not occlude */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBindVertexArray(s->shot_mesh.vao);
    for (int k = 0; k < live; k++) {
        rd_color(0.10f, 0.42f, 0.85f, 0.30f);   /* halo petals */
        glDrawArrays(GL_TRIANGLES, k * stride, SHOT_HALOS * 6);
        rd_color(0.85f, 0.98f, 1.00f, 0.85f);   /* hot core */
        glDrawArrays(GL_TRIANGLES, k * stride + SHOT_HALOS * 6,
                     SHOT_CORES * 6);
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}

/* ------------------------------------------------------------------ UI pass */

/**
 * @brief Uploads and draws one text run in the given colour.
 *
 * ENGLISH
 * -------
 * @param[in,out] s    Scene supplying the HUD buffer and mesh.
 * @param[in]     x    Left edge in pixels.
 * @param[in]     y    Baseline in pixels.
 * @param[in]     size Glyph scale.
 * @param[in]     str  Text to draw.
 * @param[in]     r,g,b,a Colour the glyph alpha is masked into.
 * @note The four text runs on screen differ only in these arguments, so they
 *       share this rather than repeating the reset/build/upload/draw sequence
 *       four times. The caller must have selected ::RD_TEXT and bound the font
 *       texture; both are set once per block rather than per run.
 *
 * 한국어
 * ------
 * @brief 지정된 색상으로 텍스트 한 줄을 업로드하고 그립니다.
 * @param[in,out] s    HUD 버퍼와 메시를 제공하는 장면.
 * @param[in]     x    좌측 가장자리 (픽셀).
 * @param[in]     y    기준선 (픽셀).
 * @param[in]     size 글리프 배율.
 * @param[in]     str  그릴 텍스트.
 * @param[in]     r,g,b,a 글리프 알파가 마스킹될 색상.
 * @note 화면의 텍스트 네 줄은 이 인자들만 다르므로, 초기화·생성·업로드·그리기 과정을
 *       네 번 반복하는 대신 이 함수를 공유합니다. 호출자가 ::RD_TEXT를 선택하고 폰트
 *       텍스처를 바인딩해 두어야 하며, 둘 다 줄마다가 아니라 블록당 한 번 설정됩니다.
 */
static void text_run(Scene *s, float x, float y, float size, const char *str,
                     float r, float g, float b, float a) {
    mb_reset(&s->hud_buf);
    font_text(&s->hud_buf, x, y, size, str);
    mesh_upload(&s->hud_mesh, &s->hud_buf, 1);
    rd_color(r, g, b, a);
    mesh_draw(&s->hud_mesh);
}

/**
 * @brief Draws a full-screen quad in a flat colour, for washes and dimming.
 *
 * ENGLISH
 * -------
 * @param[in,out] s  Scene supplying the HUD buffer and mesh.
 * @param[in]     vw Viewport width in pixels.
 * @param[in]     vh Viewport height in pixels.
 * @param[in]     r,g,b,a Colour to fill with.
 *
 * 한국어
 * ------
 * @brief 전체 화면을 단일 색상 사각형으로 채웁니다. 섬광과 어둡게 처리에 사용됩니다.
 * @param[in,out] s  HUD 버퍼와 메시를 제공하는 장면.
 * @param[in]     vw 뷰포트 너비 (픽셀).
 * @param[in]     vh 뷰포트 높이 (픽셀).
 * @param[in]     r,g,b,a 채울 색상.
 */
static void full_screen_wash(Scene *s, int vw, int vh,
                             float r, float g, float b, float a) {
    mb_reset(&s->hud_buf);
    mb_billboard(&s->hud_buf, v3f(vw * 0.5f, vh * 0.5f, 0),
                 v3f(1,0,0), v3f(0,1,0), (float)vw, (float)vh);
    mesh_upload(&s->hud_mesh, &s->hud_buf, 1);
    rd_mode(RD_FLAT);
    rd_color(r, g, b, a);
    mesh_draw(&s->hud_mesh);
}

/**
 * @brief Enters the 2D overlay state the UI passes draw in.
 *
 * ENGLISH
 * -------
 * @param[in] vw Viewport width in pixels.
 * @param[in] vh Viewport height in pixels.
 * @return The orthographic matrix, already set as the current MVP.
 * @note post_end leaves depth testing off and does not restore culling, so
 *       these passes set up their own state rather than assuming any.
 *
 * 한국어
 * ------
 * @brief UI 패스가 그리는 2D 오버레이 상태로 진입합니다.
 * @param[in] vw 뷰포트 너비 (픽셀).
 * @param[in] vh 뷰포트 높이 (픽셀).
 * @return 현재 MVP로 설정된 직교 투영 행렬.
 * @note post_end는 깊이 테스트를 끈 상태로 두고 컬링도 복원하지 않으므로, 이 패스들은
 *       어떤 상태도 가정하지 않고 스스로 설정합니다.
 */
static mat4 ui_begin(int vw, int vh) {
    mat4 hud = mat4_ortho(0.0f, (float)vw, (float)vh, 0.0f, -1.0f, 1.0f);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    rd_mvp(hud);
    return hud;
}

/**
 * @brief Restores the state the world pass expects for the next frame.
 *
 * 한국어
 * ------
 * @brief 다음 프레임의 월드 패스가 기대하는 상태를 복원합니다.
 */
static void ui_end(void) {
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void scene_draw_hud(Scene *s, int vw, int vh, const Player *p, const Weapon *w) {
    DIAG_WANT_UI_PASS(post_in_world_pass());

    ui_begin(vw, vh);

    /* A full-screen wash, strongest right after the hit. */
    if (p->hurt > 0.0f) {
        float a = p->hurt; if (a > 1.0f) a = 1.0f;
        full_screen_wash(s, vw, vh, 0.7f, 0.0f, 0.0f, a * HURT_FLASH_MAX);
    }

    rd_mode(RD_TEXT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_texture());

    /* Health, bottom-left. Green when healthy, red when low, so a glance at
       the colour says as much as the number. */
    char hp[16];
    wsprintfA(hp, "%d", p->health);
    float lo = p->health / (float)PLAYER_MAX_HP;
    text_run(s, HUD_MARGIN, vh - HUD_BASELINE, HUD_TEXT_SIZE, hp,
             1.0f - lo * 0.6f, 0.25f + lo * 0.7f, 0.25f, 1.0f);

    /* Ammo, bottom-right, and red when the gun is empty. */
    char am[16];
    wsprintfA(am, "%d", w->ammo[w->cur]);
    float aw = font_width(HUD_TEXT_SIZE, am);
    if (w->ammo[w->cur] == 0)
        text_run(s, vw - HUD_MARGIN - aw, vh - HUD_BASELINE, HUD_TEXT_SIZE, am,
                 0.9f, 0.2f, 0.2f, 1.0f);
    else
        text_run(s, vw - HUD_MARGIN - aw, vh - HUD_BASELINE, HUD_TEXT_SIZE, am,
                 0.9f, 0.85f, 0.4f, 1.0f);

    /* --- the roster, above the ammo -------------------------------------
     *
     * ENGLISH
     * -------
     * A bare number was enough while there was one weapon; with four it says
     * nothing, because "16" could be shells or grenades and those are very
     * different amounts of remaining fight. The row names what is in hand and
     * dims what is not carried, so the question "what have I got" is answered
     * without opening anything.
     *
     * Ordered by WP_*, which is the order the number keys select them, so the
     * position of a name is also the key that reaches it.
     *
     * 한국어
     * ------
     * 무기가 하나일 때는 숫자만으로 충분했지만 넷이 되면 아무것도 말해 주지 않습니다.
     * "16"이 산탄일 수도 유탄일 수도 있는데, 그 둘은 남은 전투량이 크게 다릅니다. 이 행은
     * 손에 든 것의 이름을 표시하고 보유하지 않은 것을 흐리게 하므로, "내가 무엇을 가지고
     * 있는가"에 아무것도 열지 않고 답합니다.
     *
     * WP_* 순서이며 이는 숫자 키가 선택하는 순서이므로, 이름의 위치가 곧 그것에 닿는
     * 키입니다.
     */
    {
        float x = HUD_MARGIN;
        float y = vh - HUD_BASELINE - HUD_TEXT_SIZE * 9.0f;
        for (int i = 0; i < WP_TYPES; i++) {
            const char *nm = wp_stats(i)->name;
            float wd = font_width(1.0f, nm);
            if (!w->owned[i])
                text_run(s, x, y, 1.0f, nm, 0.30f, 0.32f, 0.36f, 1.0f);
            else if (i == w->cur)
                text_run(s, x, y, 1.0f, nm, 1.00f, 0.85f, 0.35f, 1.0f);
            else
                text_run(s, x, y, 1.0f, nm, 0.55f, 0.58f, 0.64f, 1.0f);
            x += wd + 10.0f;
        }
    }

    ui_end();
}

void scene_draw_win(Scene *s, int vw, int vh, const Player *p, const Weapon *w) {
    DIAG_WANT_UI_PASS(post_in_world_pass());

    ui_begin(vw, vh);

    /* The world stays frozen underneath, dimmed rather than cleared. */
    full_screen_wash(s, vw, vh, 0.0f, 0.0f, 0.0f, WIN_DIM);

    rd_mode(RD_TEXT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_texture());

    const char *title = "YOU WIN";
    float tw = font_width(WIN_TITLE_SIZE, title);
    text_run(s, (vw - tw) * 0.5f, vh * 0.5f - 60.0f, WIN_TITLE_SIZE, title,
             1.0f, 0.85f, 0.30f, 1.0f);

    /* Final stats, so the ending says something rather than just stopping. */
    char line[64];
    wsprintfA(line, "health %d   ammo %d", p->health, w->ammo[w->cur]);
    float lw = font_width(WIN_STAT_SIZE, line);
    text_run(s, (vw - lw) * 0.5f, vh * 0.5f + 4.0f, WIN_STAT_SIZE, line,
             0.85f, 0.85f, 0.85f, 1.0f);

    const char *hint = "ESC for menu";
    float hw = font_width(WIN_HINT_SIZE, hint);
    text_run(s, (vw - hw) * 0.5f, vh * 0.5f + 40.0f, WIN_HINT_SIZE, hint,
             0.55f, 0.55f, 0.58f, 1.0f);

    ui_end();
}

void scene_draw_death(Scene *s, int vw, int vh, float since, int ready) {
    DIAG_WANT_UI_PASS(post_in_world_pass());

    ui_begin(vw, vh);

    /* Fades in over DEATH_FADE. The frame that killed the player is the one
       they most want to see, and an overlay that lands instantly hides it.
       DEATH_FADE에 걸쳐 서서히 나타납니다. 플레이어를 죽인 그 프레임이야말로 그들이 가장
       보고 싶어 하는 것이며, 즉시 덮이는 오버레이는 그것을 가립니다. */
    float k = since / DEATH_FADE;
    if (k > 1.0f) k = 1.0f;
    if (k < 0.0f) k = 0.0f;

    /* Red rather than black. The win screen dims neutrally, and if these two
       differed only in their wording a glance would not tell them apart.
       검정이 아니라 빨강입니다. 승리 화면은 중립적으로 어둡게 처리되므로, 둘이 문구로만
       달랐다면 한눈에 구분되지 않았을 것입니다. */
    full_screen_wash(s, vw, vh, 0.22f, 0.0f, 0.0f, DEATH_DIM * k);

    rd_mode(RD_TEXT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_texture());

    const char *title = "YOU DIED";
    float tw = font_width(DEATH_TITLE_SIZE, title);
    text_run(s, (vw - tw) * 0.5f, vh * 0.5f - 50.0f, DEATH_TITLE_SIZE, title,
             0.85f, 0.16f, 0.16f, k);

    /* The prompt appears only once the input it describes is actually live.
       Showing it during the grace period would be the screen lying about what
       a press would do.
       안내 문구는 그것이 설명하는 입력이 실제로 살아 있을 때만 나타납니다. 유예 시간 동안
       표시하면, 화면이 지금 누르면 무슨 일이 일어나는지에 대해 거짓말을 하는 셈입니다. */
    if (ready) {
        const char *hint = "press any key to try again";
        float hw = font_width(DEATH_HINT_SIZE, hint);
        text_run(s, (vw - hw) * 0.5f, vh * 0.5f + 30.0f, DEATH_HINT_SIZE, hint,
                 0.72f, 0.62f, 0.62f, 1.0f);
    }

    ui_end();
}

void scene_draw_title(Scene *s, int vw, int vh, float t) {
    DIAG_WANT_UI_PASS(post_in_world_pass());

    ui_begin(vw, vh);

    full_screen_wash(s, vw, vh, 0.0f, 0.0f, 0.0f, TITLE_DIM);

    rd_mode(RD_TEXT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_texture());

    /* PLACEHOLDER. Text standing in for artwork that has not been drawn yet --
       see the note in scene.h. The layout is the part worth keeping: a title
       block above centre, a prompt below it, and the level visible behind.
       임시입니다. 아직 그리지 않은 아트워크를 대신하는 텍스트입니다. scene.h의 참고
       사항을 확인하십시오. 여기서 남길 가치가 있는 것은 배치입니다. 중앙 위쪽의 제목
       블록, 그 아래의 안내 문구, 그리고 뒤로 보이는 레벨입니다. */
    const char *title = "SFPS";
    float tw = font_width(TITLE_SIZE, title);
    text_run(s, (vw - tw) * 0.5f, vh * 0.42f - 60.0f, TITLE_SIZE, title,
             1.0f, 0.82f, 0.28f, 1.0f);

    const char *sub = "a shooter that fits on a floppy disk";
    float sw = font_width(TITLE_SUB_SIZE, sub);
    text_run(s, (vw - sw) * 0.5f, vh * 0.42f + 24.0f, TITLE_SUB_SIZE, sub,
             0.66f, 0.64f, 0.60f, 1.0f);

    /* Pulsed, so the screen reads as waiting for the player rather than as
       stopped. A static prompt on a frozen world looks like a hang.
       명멸시켜 화면이 멈춘 것이 아니라 플레이어를 기다리는 것으로 읽히게 합니다. 정지된
       월드 위의 고정된 문구는 멈춘 것처럼 보입니다. */
    float pulse = 0.62f + 0.38f * (0.5f + 0.5f * sinf(t * 3.0f));
    const char *hint = "press any key to begin";
    float hw = font_width(TITLE_HINT_SIZE, hint);
    text_run(s, (vw - hw) * 0.5f, vh * 0.72f, TITLE_HINT_SIZE, hint,
             0.90f, 0.86f, 0.78f, pulse);

    const char *esc = "ESC for options";
    float ew = font_width(1.2f, esc);
    text_run(s, (vw - ew) * 0.5f, vh * 0.72f + 30.0f, 1.2f, esc,
             0.48f, 0.48f, 0.52f, 1.0f);

    ui_end();
}

void scene_draw_menu(Scene *s, int vw, int vh) {
    DIAG_WANT_UI_PASS(post_in_world_pass());

    if (!menu_is_open()) return;

    ui_begin(vw, vh);

    /* The world stays visible underneath -- paused, not gone. */
    full_screen_wash(s, vw, vh, 0.0f, 0.0f, 0.0f, MENU_DIM);

    rd_mode(RD_TEXT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_texture());

    int rows = menu_row_count();
    int cur  = menu_cursor();
    float cx = vw * 0.5f;

    /* Every position comes from menu_row_bounds rather than being recomputed
       here. The mouse hit test reads the same function, so what the eye sees
       and what the click selects cannot disagree -- see the note in menu.h.
       모든 위치를 이곳에서 다시 계산하지 않고 menu_row_bounds에서 가져옵니다. 마우스
       히트 판정이 같은 함수를 읽으므로, 눈에 보이는 것과 클릭이 선택하는 것이 어긋날 수
       없습니다. menu.h의 참고 사항을 확인하십시오. */
    const char *title = (menu_screen() == MENU_SETTINGS) ? "SETTINGS"
                      : (menu_screen() == MENU_CREDITS)  ? "CREDITS"
                      : "PAUSED";
    float tw = font_width(MENU_TITLE_SIZE, title);
    text_run(s, cx - tw * 0.5f, menu_title_y(vw, vh), MENU_TITLE_SIZE, title,
             1.0f, 0.85f, 0.30f, 1.0f);

    /* --- the notices ----------------------------------------------------
     *
     * ENGLISH
     * -------
     * This is the licence obligation being met, not a vanity screen. SFPS
     * ships as one executable with nothing beside it, so "accompany the binary
     * distribution" can only mean "be inside the game", and a notice the
     * player cannot reach is a weaker claim than one they can read from the
     * menu.
     *
     * Held as a table of lines rather than one string with newlines, because
     * font_text draws a line at a time and a wrapper that split on '
' would
     * be a second place deciding where the breaks go. The lines are written
     * pre-broken to the width this screen has.
     *
     * 한국어
     * ------
     * 이것은 허영을 위한 화면이 아니라 이행되고 있는 라이선스 의무입니다. SFPS는 옆에
     * 아무것도 없는 실행 파일 하나로 배포되므로 "바이너리 배포에 동반한다"는 것은 "게임
     * 안에 있다"는 뜻일 수밖에 없으며, 플레이어가 닿을 수 없는 고지는 메뉴에서 읽을 수
     * 있는 것보다 약한 주장입니다.
     *
     * 개행이 든 하나의 문자열이 아니라 줄의 표로 보관합니다. font_text가 한 번에 한 줄을
     * 그리므로, '
'으로 나누는 래퍼는 줄바꿈 위치를 정하는 두 번째 장소가 됩니다. */
    if (menu_screen() == MENU_CREDITS) {
        static const char *NOTICE[] = {
            "Artwork from the Freedoom project.",
            "Copyright (c) 2001-2024 Contributors to",
            "the Freedoom project. All rights reserved.",
            "",
            "Redistribution and use in source and binary",
            "forms, with or without modification, are",
            "permitted provided that the following",
            "conditions are met:",
            "",
            "* Redistributions of source code must retain",
            "  the above copyright notice, this list of",
            "  conditions and the following disclaimer.",
            "",
            "* Redistributions in binary form must",
            "  reproduce the above copyright notice, this",
            "  list of conditions and the following",
            "  disclaimer in the documentation and/or",
            "  other materials provided with the",
            "  distribution.",
            "",
            "* Neither the name of the Freedoom project",
            "  nor the names of its contributors may be",
            "  used to endorse or promote products derived",
            "  from this software without specific prior",
            "  written permission.",
            /* The second column starts here; see NOTICE_SPLIT. */
            "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT",
            "HOLDERS AND CONTRIBUTORS \"AS IS\" AND ANY",
            "EXPRESS OR IMPLIED WARRANTIES, INCLUDING,",
            "BUT NOT LIMITED TO, THE IMPLIED WARRANTIES",
            "OF MERCHANTABILITY AND FITNESS FOR A",
            "PARTICULAR PURPOSE ARE DISCLAIMED. IN NO",
            "EVENT SHALL THE COPYRIGHT OWNER OR",
            "CONTRIBUTORS BE LIABLE FOR ANY DIRECT,",
            "INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR",
            "CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT",
            "LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS",
            "OR SERVICES; LOSS OF USE, DATA, OR PROFITS;",
            "OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND",
            "ON ANY THEORY OF LIABILITY, WHETHER IN",
            "CONTRACT, STRICT LIABILITY, OR TORT",
            "(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING",
            "IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,",
            "EVEN IF ADVISED OF THE POSSIBILITY OF SUCH",
            "DAMAGE.",
            "",
            "Contributors: freedoom.github.io  /  CREDITS",
            "Full licence text: docs/LICENSE-Freedoom.txt",
        };
        /* Where the block breaks into two columns. An index into NOTICE
           rather than a count of the lines before it, so moving a line across
           the break is a one-number edit and cannot disagree with the array.
           본문이 두 단으로 갈라지는 지점. 앞선 줄의 개수가 아니라 NOTICE의 인덱스이므로,
           줄 하나를 단 너머로 옮기는 일이 숫자 하나를 고치는 일이 되고 배열과 어긋날 수
           없습니다. */
        static const int NOTICE_SPLIT = 25;
       const int n = (int)(sizeof(NOTICE) / sizeof(NOTICE[0]));

        /* Below the last row, not over it. The row positions come from
           menu_row_bounds -- the same function the mouse hit test reads -- so
           the notice cannot end up on top of the button that dismisses it
           however the menu's layout constants change.
           마지막 행 위가 아니라 *아래*에 둡니다. 행 위치는 마우스 히트 판정이 읽는 것과
           같은 menu_row_bounds에서 가져오므로, 메뉴의 배치 상수가 어떻게 바뀌어도 고지가
           그것을 닫는 버튼 위에 놓일 수 없습니다. */
        float bx0, by0, bx1, by1;
        float ny = menu_title_y(vw, vh) + 60.0f;
        if (menu_row_bounds(rows - 1, vw, vh, &bx0, &by0, &bx1, &by1))
            ny = by1 + 16.0f;

        /* Two columns, because the whole licence in one is taller than the
           screen and the part that would fall off the bottom is the warranty
           disclaimer -- the one paragraph the licence says must be reproduced
           in full. Both columns are as wide as the widest line and are
           left-aligned, measured rather than assumed, so re-wrapping the text
           moves the columns instead of overflowing them.
           두 단으로 놓습니다. 라이선스 전문을 한 단에 넣으면 화면보다 길어지고, 아래로
           잘려 나가는 부분이 하필 보증 부인 조항 -- 라이선스가 전문 그대로 실으라고
           명시한 그 문단 -- 이기 때문입니다. 두 단의 너비는 가정하지 않고 가장 긴 줄을
           실제로 재서 정하므로, 본문을 다시 줄바꿈하면 단이 넘치는 대신 움직입니다. */
        float wmax = 0.0f;
        for (int i = 0; i < n; i++) {
            float w = font_width(1.0f, NOTICE[i]);
            if (w > wmax) wmax = w;
        }
        const float gutter = 28.0f;
        float lx = cx - wmax - gutter * 0.5f;
        float rx = cx + gutter * 0.5f;

        for (int i = 0; i < n; i++) {
            int second = (i >= NOTICE_SPLIT);
            float x = second ? rx : lx;
            float y = ny + (i - (second ? NOTICE_SPLIT : 0)) * 11.0f;

            /* Brighter for the attribution, dimmer for the licence body: the
               notice must be present and legible, not shouted.
               귀속 표시는 밝게, 라이선스 본문은 흐리게 합니다. 고지는 존재하고 읽을 수
               있어야 하지 소리쳐야 하는 것은 아닙니다. */
            float t = (i <= 2) ? 0.84f : 0.56f;
            text_run(s, x, y, 1.0f, NOTICE[i], t, t * 0.98f, t * 0.92f, 1.0f);
        }
    }

    for (int i = 0; i < rows; i++) {
        const char *value;
        const char *label = menu_row_text(i, &value);

        float bx0, by0, bx1, by1;
        if (!menu_row_bounds(i, vw, vh, &bx0, &by0, &bx1, &by1)) continue;

        int on = (i == cur);

        /* The highlighted row gets a filled bar as well as a brighter colour.
           Colour alone is not enough: the dither and the scanlines both eat
           contrast, and this menu is the one screen that must stay legible
           with every graphics setting turned on at once. The bar is also what
           makes the row's CLICKABLE extent visible -- with the mouse driving
           the menu, a highlight narrower than the hit box would invite clicks
           that land on nothing.
           강조된 행은 더 밝은 색과 함께 채워진 막대를 받습니다. 색만으로는 부족합니다.
           디더와 주사선이 둘 다 대비를 갉아먹으며, 이 메뉴는 모든 그래픽 설정을 한꺼번에
           켜도 반드시 읽혀야 하는 유일한 화면입니다. 또한 막대는 행의 *클릭 가능한*
           범위를 보이게 합니다. 마우스로 메뉴를 조작하는데 강조 표시가 히트 박스보다
           좁으면, 아무것도 맞지 않는 클릭을 유도하게 됩니다. */
        if (on) {
            mb_reset(&s->hud_buf);
            mb_billboard(&s->hud_buf,
                         v3f((bx0 + bx1) * 0.5f, (by0 + by1) * 0.5f, 0),
                         v3f(1,0,0), v3f(0,1,0), bx1 - bx0, by1 - by0);
            mesh_upload(&s->hud_mesh, &s->hud_buf, 1);
            rd_mode(RD_FLAT);
            rd_color(1.0f, 0.85f, 0.30f, 0.16f);
            mesh_draw(&s->hud_mesh);

            /* Back to text mode and the font: the bar above swapped both. */
            rd_mode(RD_TEXT);
            glBindTexture(GL_TEXTURE_2D, font_texture());
        }

        float r = on ? 1.00f : 0.62f;
        float g = on ? 0.92f : 0.62f;
        float b = on ? 0.55f : 0.66f;

        /* Text sits on the row's own baseline, derived from the same box, so
           moving a row moves its label with it. */
        float y = by0 + 4.0f;

        if (on)
            text_run(s, bx0 + 8.0f, y, MENU_ROW_SIZE, ">", r, g, b, 1.0f);

        text_run(s, cx + MENU_LABEL_X, y, MENU_ROW_SIZE, label, r, g, b, 1.0f);

        if (value[0])
            text_run(s, cx + MENU_VALUE_X, y, MENU_ROW_SIZE, value, r, g, b, 1.0f);
    }

    /* Names the mouse first, because that is what a player reaches for when a
       cursor appears. The keys stay listed -- both drive the same menu.
       커서가 나타나면 플레이어가 먼저 잡는 것이 마우스이므로 마우스를 먼저 적습니다.
       키도 계속 표시하며, 둘 다 같은 메뉴를 조작합니다. */
    /* The credits screen gets no hint. Its hint would be drawn where the
       notice now is, and there is nothing to explain: one row that says BACK,
       and ESC does the same. A line of help over a licence is worse than no
       line of help.
       크레딧 화면에는 안내를 두지 않습니다. 안내가 지금 고지가 있는 자리에 그려지며,
       설명할 것도 없습니다. BACK이라고 적힌 행 하나가 있고 ESC도 같은 일을 합니다.
       라이선스 위에 겹친 도움말 한 줄은 도움말이 없는 것보다 나쁩니다. */
    if (menu_screen() != MENU_CREDITS) {
        const char *hint = (menu_screen() == MENU_SETTINGS)
            ? "CLICK to change   RIGHT-CLICK reverses   W/S A/D   ESC back"
            : "CLICK to choose   W/S select   ENTER   ESC resume";
        float hw = font_width(MENU_HINT_SIZE, hint);
        text_run(s, cx - hw * 0.5f, menu_hint_y(vw, vh), MENU_HINT_SIZE, hint,
                 0.52f, 0.52f, 0.56f, 1.0f);
    }

    ui_end();
}

void scene_draw_proj(Scene *s, mat4 vp, v3 cam_right, v3 cam_up) {
    DIAG_WANT_WORLD_PASS(post_in_world_pass());

    int n = proj_count(), live = 0;

    mb_reset(&s->shot_buf);
    for (int i = 0; i < n; i++) {
        const Proj *p = proj_at(i);
        if (!p || !p->active) continue;

        /* A grenade tumbles and a bolt does not, which is the same distinction
           the simulation draws: `gravity` is what separates them, so the
           drawing reads the same field rather than a second flag that could
           disagree with it.
           유탄은 구르고 탄은 구르지 않습니다. 시뮬레이션이 긋는 것과 같은 구분입니다.
           `gravity`가 둘을 가르므로, 그림도 어긋날 수 있는 두 번째 플래그가 아니라 같은
           필드를 읽습니다. */
        int   arcs = p->gravity > 0.0f;
        float size = arcs ? 0.30f : 0.16f;
        float a    = arcs ? p->spin * 6.0f : 0.0f;

        v3 r = v3add(v3scale(cam_right,  cosf(a)), v3scale(cam_up,  sinf(a)));
        v3 u = v3add(v3scale(cam_right, -sinf(a)), v3scale(cam_up,  cosf(a)));
        mb_billboard(&s->shot_buf, p->pos, r, u, size, size);
        live++;
    }
    if (!live) return;

    mesh_upload(&s->shot_mesh, &s->shot_buf, 1);
    rd_mode(RD_FLAT);
    rd_mvp(vp);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBindVertexArray(s->shot_mesh.vao);

    /* Drawn one at a time so each carries its own colour: a grenade about to
       go off is not the same object as one that was just thrown, and the fuse
       is the only warning the player gets.
       각자 자신의 색을 갖도록 하나씩 그립니다. 곧 터질 유탄은 방금 던져진 유탄과 같은
       물체가 아니며, 도화선이 플레이어가 받는 유일한 경고입니다. */
    int k = 0;
    for (int i = 0; i < n; i++) {
        const Proj *p = proj_at(i);
        if (!p || !p->active) continue;

        if (p->gravity > 0.0f) {
            /* Cooling from white toward red as the fuse runs out. */
            float t = p->fuse > 0.0f ? p->fuse / PROJ_FUSE : 0.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            rd_color(1.0f, 0.30f + 0.55f * t, 0.12f + 0.60f * t, 0.90f);
        } else {
            rd_color(0.55f, 0.85f, 1.00f, 0.85f);
        }
        glDrawArrays(GL_TRIANGLES, k * 6, 6);
        k++;
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}
