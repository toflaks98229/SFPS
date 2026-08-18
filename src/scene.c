/**
 * @file scene.c
 * @brief Implements the per-frame draw passes, and the order they run in.
 *
 * ENGLISH
 * -------
 * Each pass is one of the blocks that used to sit inline in the frame loop,
 * moved without changing what it draws. ::scene_frame is the sequence they run
 * in, and it moved here second, for the reason main.c's own header comment had
 * already written down and could not act on: the order was declared
 * load-bearing and nothing checked it, because the only way to run it was to
 * open a window and play.
 *
 * The split this file now holds is the same one world.c holds. A pass knows
 * how to draw one thing; ::scene_frame knows what a frame IS. Neither knows
 * about a window -- the caller supplies a viewport in pixels and a ::World,
 * and gets pixels back.
 *
 * What the moves buy: the buffers are owned and freed by one pair of
 * functions, each pass can be read without scrolling past the others, the
 * magic numbers that were scattered through the loop are named constants at
 * the top of this file, and the order is now reachable by anything that can
 * make a GL context -- which every tool in tools\ already can.
 *
 * 한국어
 * ------
 * 각 패스는 프레임 루프 안에 인라인으로 있던 블록 하나이며, 무엇을 그리는지 바꾸지 않고
 * 옮긴 것입니다. ::scene_frame은 그것들이 실행되는 순서이며, main.c 자신의 헤더 주석이
 * 이미 적어 두었으나 실행에 옮기지 못한 이유로 두 번째로 이곳에 옮겨 왔습니다. 그 순서는
 * 구조적으로 중요하다고 선언되었지만 무엇도 그것을 검사하지 않았습니다. 실행하는 유일한
 * 방법이 창을 열고 플레이하는 것이었기 때문입니다.
 *
 * 이 파일이 이제 담고 있는 분리는 world.c가 담고 있는 것과 같습니다. 패스는 한 가지를
 * 어떻게 그리는지 알고, ::scene_frame은 한 프레임이 *무엇인지* 압니다. 어느 쪽도 창에
 * 대해서는 알지 못합니다. 호출자가 픽셀 단위 뷰포트와 ::World를 건네면 픽셀을 돌려받습니다.
 *
 * 옮겨서 얻는 것: 버퍼를 한 쌍의 함수가 소유하고 해제하며, 각 패스를 다른 패스를 지나쳐
 * 스크롤하지 않고 읽을 수 있고, 루프 곳곳에 흩어져 있던 매직 넘버가 이 파일 상단의 명명된
 * 상수가 되었으며, 이제 그 순서에 GL 컨텍스트를 만들 수 있는 무엇이든 도달할 수 있습니다.
 * tools\의 모든 도구가 이미 그렇게 할 수 있습니다.
 */

#include "scene.h"
#include "hook.h"
#include "proj.h"   /* the player's grenades and bolts */
#include "enemy.h"
#include "pickup.h"
#include "sprite.h"
#include "font.h"
#include "post.h"     /* post_in_world_pass -- the pass-boundary guards */
#include "menu.h"     /* the rows the ESC menu draws, read rather than copied */
#include "door.h"     /* the refusal notice, and the names of the key bits */
#include "txt.h"      /* txt_append_int/_str: the HUD's numbers, without user32 */
#include "diag.h"

/* The two passes that draw themselves rather than being drawn by a scene_*
   function: particles and the marks a shot leaves. They own their own pools
   and their own buffers, so ::scene_frame calls them where they belong in the
   order rather than reproducing them here.
   scene_* 함수가 그리는 것이 아니라 스스로 그리는 두 패스입니다. 입자, 그리고 사격이
   남긴 자국입니다. 각자 자신의 풀과 버퍼를 소유하므로, ::scene_frame은 이곳에서 그것을
   재현하는 대신 순서상 제자리에서 호출합니다. */
#include "fx.h"
#include "decal.h"

/* ------------------------------------------------------------------ tuning */

/**
 * @brief How much coarser than the render buffer the vertex snap grid is.
 *
 * ENGLISH
 * -------
 * 1.0 snaps to the offscreen buffer's own pixels, which is what the hardware
 * did and is almost invisible here: the PlayStation ran at 320x240 and this
 * runs at 640x360, so a whole-pixel jump is half the angular size it was.
 * Dividing the grid down makes the jumps bigger without touching the render
 * resolution, which is the dial for how much of the artefact to actually show.
 *
 * @note This is the ONE number to change when tuning the wobble. Raising it
 *       past about 4 starts tearing thin geometry apart, because two vertices
 *       of the same triangle can land on the same grid line and the triangle
 *       collapses.
 * @note Here rather than in main.c, where it began, because ::scene_frame is
 *       now its only reader. A window has no opinion about how coarse a snap
 *       grid is.
 *
 * 한국어
 * ------
 * @brief 정점 스냅 격자가 렌더 버퍼보다 얼마나 성긴지를 나타냅니다.
 *
 * 1.0이면 오프스크린 버퍼의 픽셀에 그대로 맞추며, 이것이 실제 하드웨어의 동작이지만
 * 여기서는 거의 보이지 않습니다. 플레이스테이션은 320x240이었고 이 프로젝트는
 * 640x360이므로, 한 픽셀 도약의 각크기가 절반입니다. 격자를 나누면 렌더 해상도를
 * 건드리지 않고 도약을 키울 수 있으며, 이것이 아티팩트를 얼마나 드러낼지 조정하는
 * 값입니다.
 *
 * @note 흔들림을 조정할 때 바꿔야 할 *유일한* 값입니다. 약 4를 넘기면 얇은 지오메트리가
 *       찢어지기 시작합니다. 같은 삼각형의 두 정점이 같은 격자선에 놓여 삼각형이 붕괴하기
 *       때문입니다.
 * @note 처음 있던 main.c가 아니라 이곳에 두는 이유는 이제 ::scene_frame이 이 값을 읽는
 *       유일한 곳이기 때문입니다. 창은 스냅 격자가 얼마나 성긴지에 대해 아무 견해도 갖지
 *       않습니다.
 */
#define PSX_SNAP_COARSE 2.0f

/* --- moving light ----------------------------------------------------------
 *
 * ENGLISH
 * -------
 * WHAT THESE EIGHT SLOTS ARE FOR, finally used. render.h reserved them for
 * light the bake cannot produce -- a muzzle flash, a grenade in the air, a
 * bolt crossing a dark room -- and ::scene_draw_level's own comment recorded
 * that nothing filled them yet, so every frame ran the shader's light loop
 * with a count of zero. The loop was live, the lamps were baked, and the only
 * light that ever MOVED in this game was the one nobody had written.
 *
 * The sources below are the three that already exist as visible things: the
 * gun going off, the player's projectiles, and the monsters'. Each of those is
 * drawn as a glow already, so a light beside it is the same event told to the
 * geometry rather than a new effect invented here.
 *
 * @note Radii are metres and generous. A muzzle flash lasts ::FLASH_TIME and a
 *       radius that only reaches the wall it is fired at would never be seen
 *       on anything else; the point is the room, not the wall.
 * @note Powers are what ::rd_lights takes in `.a`. They sit under 1 because
 *       the shader adds them on top of the baked light rather than replacing
 *       it, and a corridor already lit to 0.6 does not have 0.4 of headroom
 *       to spare before it clips.
 *
 * 한국어
 * ------
 * *이 여덟 슬롯이 무엇을 위한 것이었는지*, 마침내 쓰입니다. render.h는 베이크가 만들어 낼 수
 * 없는 빛(총구 섬광, 공중의 유탄, 어두운 방을 가로지르는 볼트)을 위해 이것들을 예약해 두었고,
 * ::scene_draw_level 자신의 주석이 아직 그것을 채우는 것이 없다고 적어 두었습니다. 그래서 매
 * 프레임 셰이더의 조명 루프가 개수 0으로 돌았습니다. 루프는 살아 있었고 등은 구워져 있었으며,
 * 이 게임에서 실제로 *움직이는* 유일한 빛은 아무도 쓰지 않은 그것이었습니다.
 *
 * 아래의 광원들은 이미 눈에 보이는 것으로 존재하는 셋입니다. 발사되는 총, 플레이어의 발사체,
 * 그리고 몬스터의 발사체입니다. 각각은 이미 발광체로 그려지고 있으므로, 그 곁의 광원은 이곳에서
 * 새로 발명한 효과가 아니라 같은 사건을 지오메트리에게도 알리는 것입니다.
 *
 * @note 반경은 미터이며 넉넉합니다. 총구 섬광은 ::FLASH_TIME 동안 지속되는데, 겨눈 벽에만 닿는
 *       반경이라면 다른 무엇에서도 보이지 않을 것입니다. 요점은 벽이 아니라 방입니다.
 * @note 세기는 ::rd_lights가 `.a`로 받는 값입니다. 1보다 작은 이유는 셰이더가 이것을 구워진 빛을
 *       대체하지 않고 그 위에 *더하기* 때문이며, 이미 0.6으로 밝은 복도에는 잘리기 전까지 0.4의
 *       여유밖에 없기 때문입니다. */
#define LIGHT_MUZZLE_RADIUS  7.0f   ///< @brief Metres a muzzle flash reaches. / 총구 섬광이 닿는 거리 (미터).
#define LIGHT_MUZZLE_POWER   0.85f  ///< @brief Peak power, faded over ::FLASH_TIME. / 최대 세기. ::FLASH_TIME에 걸쳐 감쇠합니다.
#define LIGHT_PROJ_RADIUS    5.0f   ///< @brief Metres a player projectile lights. / 플레이어 발사체가 밝히는 거리 (미터).
#define LIGHT_PROJ_POWER     0.55f  ///< @brief Steady, for as long as it is in the air. / 공중에 있는 동안 일정합니다.
#define LIGHT_SHOT_RADIUS    4.5f   ///< @brief Metres a monster bolt lights. / 몬스터 볼트가 밝히는 거리 (미터).
#define LIGHT_SHOT_POWER     0.60f  ///< @brief Bolts read as the brighter thing in a dark room. / 볼트는 어두운 방에서 더 밝은 것으로 읽힙니다.

/** @brief Warm white: burnt powder, not a torch. / 따뜻한 백색. 횃불이 아니라 연소한 화약입니다. */
static const float LIGHT_COL_MUZZLE[3] = { 1.00f, 0.86f, 0.62f };
/** @brief The grenade's own hot orange. / 유탄 자신의 뜨거운 주황색. */
static const float LIGHT_COL_PROJ[3]   = { 1.00f, 0.62f, 0.26f };
/** @brief The bolt's cold green, matching how ::scene_draw_shots draws it. / 볼트의 차가운 녹색. ::scene_draw_shots가 그리는 방식과 맞춥니다. */
static const float LIGHT_COL_SHOT[3]   = { 0.55f, 1.00f, 0.70f };

/* --- how a shake looks -----------------------------------------------------
 * HOW HARD is ::RunState::shake's and the world's; how it LOOKS is this file's,
 * for the same reason ::PSX_SNAP_COARSE lives here rather than in main.c. The
 * simulation has no opinion about whether a jolt is mostly sway or mostly roll.
 *
 * 얼마나 *센가*는 ::RunState::shake와 월드의 것이고, 어떻게 *보이는가*는 이 파일의 것입니다.
 * ::PSX_SNAP_COARSE가 main.c가 아니라 이곳에 있는 것과 같은 이유입니다. 시뮬레이션은 충격이
 * 주로 흔들림인지 주로 회전인지에 대해 아무 견해도 갖지 않습니다. */
#define SHAKE_FREQ 34.0f    ///< @brief Radians per second of world time. / 월드 시간 초당 라디안.
#define SHAKE_SWAY 0.045f   ///< @brief Metres the eye is displaced at full strength. / 최대 세기에서 눈이 밀리는 거리 (미터).
#define SHAKE_ROLL 0.030f   ///< @brief Radians of roll at full strength. / 최대 세기에서의 회전 (라디안).

/**
 * @struct MoveLight
 * @brief One candidate light, and how far it is from the eye.
 *
 * ENGLISH: The distance is kept because there are more things that could light
 * a frame than ::RD_MAX_LIGHTS slots to hold them -- a grenade volley alone can
 * out-number eight -- and something has to lose. Nearest wins, so what is
 * dropped is what was contributing least.
 *
 * 한국어: 거리를 함께 보관하는 이유는, 한 프레임을 밝힐 수 있는 것이 ::RD_MAX_LIGHTS 슬롯보다
 * 많을 수 있고(유탄 일제 사격 하나만으로도 여덟을 넘습니다) 무언가는 탈락해야 하기 때문입니다.
 * 가까운 쪽이 이기므로, 버려지는 것은 가장 적게 기여하던 것입니다.
 */
typedef struct { float pos[4], col[4], d2; } MoveLight;

/**
 * @brief Offers one light to the frame's set, keeping the nearest eight.
 *
 * @note Silently ignores a power of zero, so a caller may hand over a faded
 *       source without testing it first -- a muzzle flash on its last frame is
 *       the normal case, not an error.
 *
 * @brief 한 프레임의 광원 집합에 광원 하나를 제안하며, 가장 가까운 여덟 개를 유지합니다.
 * @note 세기가 0이면 조용히 무시하므로, 호출자는 먼저 검사하지 않고 사그라든 광원을 그대로
 *       건네도 됩니다. 마지막 프레임의 총구 섬광이 오류가 아니라 평범한 경우입니다.
 */
static void light_offer(MoveLight *ls, int *n, v3 p, float radius,
                        const float col[3], float power, v3 eye) {
    if (power <= 0.0f || radius <= 0.0f) return;

    float dx = p.x - eye.x, dy = p.y - eye.y, dz = p.z - eye.z;
    float d2 = dx*dx + dy*dy + dz*dz;

    int slot = *n;
    if (slot >= RD_MAX_LIGHTS) {
        /* Full. Evict the farthest, and only if this one beats it -- otherwise
           a distant grenade at the end of the list would displace the flash
           going off in the player's hands.
           가득 찼습니다. 가장 먼 것을 밀어내되 이것이 그것을 이길 때만입니다. 그러지 않으면
           목록 끝의 먼 유탄이 플레이어 손에서 터지는 섬광을 밀어냅니다. */
        int worst = 0;
        for (int i = 1; i < RD_MAX_LIGHTS; i++)
            if (ls[i].d2 > ls[worst].d2) worst = i;
        if (ls[worst].d2 <= d2) return;
        slot = worst;
    } else {
        (*n)++;
    }

    ls[slot].pos[0] = p.x; ls[slot].pos[1] = p.y;
    ls[slot].pos[2] = p.z; ls[slot].pos[3] = radius;
    ls[slot].col[0] = col[0]; ls[slot].col[1] = col[1];
    ls[slot].col[2] = col[2]; ls[slot].col[3] = power;
    ls[slot].d2 = d2;
}

/**
 * @brief Gathers this frame's moving lights and hands them to the shader.
 *
 * ENGLISH
 * -------
 * @param[in] w   The world, read for the gun and the things in the air.
 * @param[in] eye Camera position: what "nearest" is measured from.
 *
 * @note Uploaded ONCE for the whole frame rather than per pass. Every pass
 *       after this one -- the level, the monsters, the pickups -- reads the
 *       same eight, which is what makes a grenade light a wall and the monster
 *       standing against it by the same amount. ::rd_use first, because
 *       ::rd_lights sets uniforms and says nothing about which program is bound.
 * @note The muzzle flash is placed at the EYE rather than at the gun's muzzle.
 *       They are about half a metre apart and the light reaches seven, so the
 *       difference is invisible -- and solving the muzzle would mean repeating
 *       ::wp_update's view-model projection here, for a result no one can see.
 *
 * 한국어
 * ------
 * @brief 이번 프레임의 움직이는 광원을 모아 셰이더에 건넵니다.
 * @param[in] w   월드. 총과 공중에 있는 것들을 읽습니다.
 * @param[in] eye 카메라 위치. "가깝다"를 재는 기준입니다.
 *
 * @note 패스마다가 아니라 프레임 전체에 대해 *한 번* 업로드합니다. 이후의 모든 패스(레벨,
 *       몬스터, 아이템)가 같은 여덟 개를 읽으며, 그것이 유탄이 벽과 그 앞에 선 몬스터를 같은
 *       양만큼 밝히게 하는 것입니다. ::rd_lights는 유니폼을 설정할 뿐 어느 프로그램이
 *       바인딩되어 있는지에 대해 아무 말도 하지 않으므로 ::rd_use를 먼저 호출합니다.
 * @note 총구 섬광은 총구가 아니라 *눈*에 놓습니다. 둘은 0.5미터쯤 떨어져 있고 빛은 7미터를
 *       가므로 차이가 보이지 않습니다. 총구를 구하려면 이곳에서 ::wp_update의 뷰 모델 투영을
 *       반복해야 하는데, 아무도 볼 수 없는 결과를 위해서입니다.
 */
static void scene_lights(const World *w, v3 eye) {
    MoveLight ls[RD_MAX_LIGHTS];
    int n = 0;

    /* The gun. Faded over its own timer so the light leaves with the flash
       sprite rather than snapping off a frame later.
       총입니다. 자기 타이머에 맞춰 감쇠하므로, 빛이 한 프레임 뒤에 뚝 꺼지지 않고 섬광
       스프라이트와 함께 사라집니다. */
    if (w->weapon.flash > 0.0f) {
        float f = w->weapon.flash / FLASH_TIME;
        if (f > 1.0f) f = 1.0f;
        light_offer(ls, &n, eye, LIGHT_MUZZLE_RADIUS, LIGHT_COL_MUZZLE,
                    LIGHT_MUZZLE_POWER * f, eye);
    }

    /* The player's grenades and bolts, for as long as they are in the air. */
    for (int i = 0, pn = proj_count(&w->pools); i < pn; i++) {
        const Proj *p = proj_at(&w->pools, i);
        if (!p->active) continue;
        light_offer(ls, &n, p->pos, LIGHT_PROJ_RADIUS, LIGHT_COL_PROJ,
                    LIGHT_PROJ_POWER, eye);
    }

    /* The monsters'. Drawn as additive rosettes already -- see
       ::scene_draw_shots -- so this is the same bolt told to the geometry. */
    for (int i = 0, sn = enemy_shot_count(&w->pools); i < sn; i++) {
        const Shot *sh = enemy_shot_at(&w->pools, i);
        if (!sh->active) continue;
        light_offer(ls, &n, sh->pos, LIGHT_SHOT_RADIUS, LIGHT_COL_SHOT,
                    LIGHT_SHOT_POWER, eye);
    }

    /* Flattened into the two arrays ::rd_lights takes. Kept apart until here
       because the eviction above needs the distance beside the light, and
       ::rd_lights wants neither.
       ::rd_lights가 받는 두 배열로 펼칩니다. 이곳까지 나누어 둔 이유는 위의 축출이 광원 곁의
       거리를 필요로 하는데 ::rd_lights는 그 어느 것도 원하지 않기 때문입니다. */
    float pos[RD_MAX_LIGHTS * 4], col[RD_MAX_LIGHTS * 4];
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 4; k++) {
            pos[i*4 + k] = ls[i].pos[k];
            col[i*4 + k] = ls[i].col[k];
        }

    rd_use();
    rd_lights(pos, col, n);
}

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

/* The locked-door notice. Above centre rather than on it: a crosshair sits at
   the middle and a line printed through it is read as part of the reticle.
   HUD_NOTICE_FADE is shorter than DOOR_NOTICE_TIME so the message holds at full
   strength first and only fades over its last moments -- fading across the
   whole life would start it already dimmed, and the instant it appears is the
   instant it is most needed.
   잠긴 문 알림입니다. 정중앙이 아니라 그 위입니다. 가운데에는 조준점이 있고 그것을 통과해
   찍힌 문장은 조준선의 일부로 읽힙니다. HUD_NOTICE_FADE는 DOOR_NOTICE_TIME보다 짧으므로
   메시지는 먼저 온전한 밝기로 머물고 마지막 순간에만 사라집니다. 수명 전체에 걸쳐
   페이드하면 시작부터 흐릿한데, 나타나는 순간이야말로 가장 필요한 순간입니다. */
#define HUD_NOTICE_SIZE 2.2f
#define HUD_NOTICE_Y    0.34f   /* fraction of viewport height */
#define HUD_NOTICE_FADE 0.6f    /* seconds of fade at the tail */

/* The between-levels screen. Dimmer than the win screen's wash, because the
   level behind it is not over -- the player is passing through, not stopping,
   and a full blackout would say otherwise.
   레벨 사이 화면입니다. 승리 화면의 막보다 옅습니다. 뒤의 레벨이 끝난 것이 아니기
   때문입니다. 플레이어는 멈추는 것이 아니라 지나가는 중이며, 완전한 암전은 그 반대를
   말하게 됩니다. */
#define BETWEEN_DIM        0.72f
#define BETWEEN_FADE       0.45f   /* seconds at each end */
#define BETWEEN_LABEL_SIZE 2.0f
#define BETWEEN_NAME_SIZE  4.5f
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

void scene_init(Scene *s, Weapon *w) {
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

    /* The gun, and the one fact that crosses back: loading the model is how
       the Weapon learns where its barrel ends. `w` is what makes this
       function take a Weapon at all -- see weaponview.h.
       총이며, 되돌아 건너오는 하나의 사실입니다. 모델을 로드하는 것이 Weapon이 자기
       총열이 어디서 끝나는지 알게 되는 경로입니다. 이 함수가 Weapon을 받는 이유가
       `w`이며, weaponview.h를 참조하십시오. */
    wpview_init(&s->wpview, w);
}

void scene_free(Scene *s) {
    /* mb_free is safe on an already-freed buffer, so this is safe twice. */
    mb_free(&s->enemy_buf);
    mb_free(&s->pickup_buf);
    mb_free(&s->shot_buf);
    mb_free(&s->hud_buf);
    mb_free(&s->level_buf);
    wpview_free(&s->wpview);
}

/* ----------------------------------------------------------- level geometry */

/* The material half of both builders. Split out because ::scene_rebuild_moving
   resolves only the runs after the boundary, and a second loop written out by
   hand is how the two come to disagree about what a run's material is.
   두 생성기의 재질 담당 절반입니다. ::scene_rebuild_moving이 경계 이후의 구간만 해석하므로
   분리했습니다. 손으로 다시 쓴 두 번째 루프는 구간의 재질이 무엇인지에 대해 둘이 서로 다른
   말을 하게 되는 방식입니다. */
static void resolve_mats(Scene *s, int from) {
    for (int i = from; i < s->level_range_count; i++)
        s->level_tex[i] = tex_mat(s->level_ranges[i].mat);
}

void scene_build_level(Scene *s, const Level *l, int dynamic) {
    mb_reset(&s->level_buf);

    /* THE STATIC HALF FIRST, so what a door moves is a suffix of both the
       vertex buffer and the range table. That ordering is the whole mechanism:
       it is what lets a rebuild truncate rather than search, and what lets the
       upload be a sub-range rather than a whole store.

       A level that does not split reports no static half at all, so the moving
       half is everything and both the truncate and the sub-upload degrade into
       what they replaced.

       *정적인 절반을 먼저* 생성하여, 문이 움직이는 것이 정점 버퍼와 구간 표 양쪽의 접미사가
       되게 합니다. 그 순서가 기법의 전부입니다. 재생성이 탐색이 아니라 잘라내기가 되게 하고,
       업로드가 전체 저장이 아니라 부분 범위가 되게 하는 것이 그것입니다.

       분할되지 않는 레벨은 정적인 절반을 아예 보고하지 않으므로 움직이는 절반이 전체가 되며,
       잘라내기와 부분 업로드 모두 그것들이 대체한 동작으로 자연히 되돌아갑니다. */
    if (level_geometry_split(l)) {
        s->level_range_count = level_geometry_part(&s->level_buf, l,
                                                   s->level_ranges,
                                                   LVL_MAX_RANGES,
                                                   LVL_PART_STATIC);
        s->level_static_verts  = s->level_buf.count;
        s->level_static_ranges = s->level_range_count;

        s->level_range_count += level_geometry_part(
            &s->level_buf, l,
            s->level_ranges + s->level_range_count,
            LVL_MAX_RANGES - s->level_range_count, LVL_PART_MOVING);
    } else {
        s->level_static_verts  = 0;
        s->level_static_ranges = 0;
        s->level_range_count   = level_geometry(&s->level_buf, l,
                                                s->level_ranges, LVL_MAX_RANGES);
    }

    mesh_upload(&s->level_mesh, &s->level_buf, dynamic);

    /* Materials last: level_geometry decides how many runs there are, and each
       run names the material it wants.
       재질은 마지막입니다. 구간의 개수는 level_geometry가 결정하며, 각 구간이 사용할
       재질의 이름을 지정합니다. */
    resolve_mats(s, 0);
}

void scene_rebuild_moving(Scene *s, const Level *l) {
    /* Nothing to be clever about: rebuild the lot. Also the path a level with
       no static half takes, which is the same statement said the other way.
       영리하게 굴 것이 없습니다. 전부 다시 만듭니다. 정적인 절반이 없는 레벨이 택하는 경로이기도
       하며, 같은 말을 다르게 표현한 것입니다. */
    if (!level_geometry_split(l)) { scene_build_level(s, l, 1); return; }

    /* Back to the boundary, discarding the previous frame's moving half. The
       static half in front of it is untouched -- not rebuilt and, which is the
       expensive half, not re-baked.
       경계로 되돌려 이전 프레임의 움직이는 절반을 버립니다. 그 앞의 정적인 절반은 손대지
       않습니다. 다시 만들지 않으며, 비싼 쪽인 베이크도 다시 하지 않습니다. */
    s->level_buf.count   = s->level_static_verts;
    s->level_range_count = s->level_static_ranges;

    s->level_range_count += level_geometry_part(
        &s->level_buf, l, s->level_ranges + s->level_range_count,
        LVL_MAX_RANGES - s->level_range_count, LVL_PART_MOVING);

    /* Sub-upload if the total still matches what the GPU store was sized for,
       and a whole one if it does not. Translating a brush cannot change its
       vertex count, so the fallback is for the case where something other than
       a door changed the level under us.
       총량이 GPU 저장 공간이 상정한 값과 여전히 같으면 부분 업로드하고, 다르면 전체를
       올립니다. 브러시를 옮기는 것은 정점 수를 바꿀 수 없으므로, 이 되돌림은 문이 아닌
       무언가가 우리 아래에서 레벨을 바꾼 경우를 위한 것입니다. */
    if (!mesh_upload_from(&s->level_mesh, &s->level_buf, s->level_static_verts))
        mesh_upload(&s->level_mesh, &s->level_buf, 1);

    resolve_mats(s, s->level_static_ranges);
}

void scene_draw_level(const Scene *s, mat4 vp, v3 eye) {
    DIAG_WANT_WORLD_PASS(post_in_world_pass());

    /* --- the level's own lamps are NOT uploaded here ---------------------
       They used to be, and for a while they had to be: lighting was eight
       point lights and nothing else. Since the static bake they are compiled
       into the vertices when the level loads -- every one of them, shadowed
       against the walls, with no eight-at-a-time limit -- so sending them here
       as well applied each lamp TWICE: once smoothly and shadowed from the
       bake, once per-pixel and unshadowed from the shader's loop.

       Nothing about that looks like a fault. The room is brighter than it
       should be and reads as a lighting choice, which is why it survived the
       commit that introduced the bake and why rd_light_count exists for
       scenetest to assert against.

       What the slots are for now is what the bake cannot do: light that MOVES.
       A muzzle flash, an explosion, a projectile in flight -- none of which
       exist in a level file, none of which can be baked, and eight of which is
       plenty. Nothing feeds them yet, so the world pass runs with none.

       레벨 자신의 등은 이곳에서 업로드하지 *않습니다*.

       한때는 그랬고 그럴 수밖에 없었습니다. 조명이 점광원 여덟 개가 전부였기 때문입니다.
       정적 베이크 이후로 등은 레벨이 로드될 때 정점에 구워집니다. 여덟 개 제한 없이 전부,
       벽에 대한 그림자까지 포함해서입니다. 그러므로 이곳에서 또 보내면 각 등이 *두 번*
       적용되었습니다. 한 번은 베이크에서 부드럽고 그림자가 진 채로, 한 번은 셰이더 반복문에서
       픽셀 단위로 그림자 없이.

       그중 어느 것도 결함처럼 보이지 않습니다. 방은 마땅한 것보다 밝고 그것은 조명 연출로
       읽힙니다. 베이크를 도입한 커밋에서 이것이 살아남은 이유이며, scenetest가 단언할 수
       있도록 rd_light_count가 존재하는 이유입니다.

       이제 이 슬롯의 용도는 베이크가 할 수 없는 것, 즉 *움직이는* 빛입니다. 총구 섬광, 폭발,
       날아가는 발사체입니다. 어느 것도 레벨 파일에 없고, 어느 것도 구울 수 없으며, 여덟 개면
       충분합니다. 아직 그것을 채우는 것이 없으므로 월드 패스는 0개로 돌아갑니다. */
    /* rd_lights(0, 0, 0) was here, and it is what kept the paragraph above true
       for as long as it was: the slots stayed empty because this line emptied
       them, every frame, after anything that might have filled them. The set is
       ::scene_lights's now and is uploaded once for the whole frame, before
       this pass -- so the level, the monsters and the pickups are all lit by
       the same eight rather than the level being lit by none.
       rd_lights(0, 0, 0)이 이곳에 있었고, 위 문단이 참으로 남아 있던 이유가 바로 그것입니다.
       슬롯이 비어 있던 것은 그것을 채울 수 있는 무엇보다도 뒤에서 이 줄이 매 프레임 비웠기
       때문입니다. 이제 그 집합은 ::scene_lights의 것이며 이 패스보다 앞서 프레임 전체에 대해
       한 번 업로드됩니다. 그래서 레벨과 몬스터와 아이템이 모두 같은 여덟 개로 조명되며, 레벨이
       하나도 없이 조명되지 않습니다. */
    rd_mode(RD_WORLD);
    rd_mvp(vp);
    rd_eye(eye);
    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < s->level_range_count; i++) {
        tex_use(&s->level_tex[i]);
        mesh_draw_range(&s->level_mesh, s->level_ranges[i].first,
                        s->level_ranges[i].count);
    }
}

/* --------------------------------------------------------------- world pass */

void scene_draw_enemies(Scene *s, const Pools *pl, mat4 vp, v3 eye, v3 cam_right) {
    DIAG_WANT_WORLD_PASS(post_in_world_pass());

    int n = enemy_count(pl);
    mb_reset(&s->enemy_buf);

    for (int i = 0; i < n; i++) {
        const Enemy *m = enemy_at(pl, i);
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
        const Enemy *m = enemy_at(pl, i);
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

void scene_draw_pickups(Scene *s, const Pools *pl, mat4 vp, v3 eye, v3 cam_right) {
    DIAG_WANT_WORLD_PASS(post_in_world_pass());

    int pn = pickup_count(pl);
    mb_reset(&s->pickup_buf);

    for (int i = 0; i < pn; i++) {
        const Pickup *p = pickup_at(pl, i);
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

void scene_draw_shots(Scene *s, const Pools *pl, mat4 vp, v3 cam_right, v3 cam_up) {
    DIAG_WANT_WORLD_PASS(post_in_world_pass());

    const int quads  = SHOT_HALOS + SHOT_CORES;
    const int stride = quads * 6;
    int sn = enemy_shot_count(pl), live = 0;

    mb_reset(&s->shot_buf);
    for (int i = 0; i < sn; i++) {
        const Shot *sh = enemy_shot_at(pl, i);
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

void scene_draw_hud(Scene *s, int vw, int vh, const Level *l,
                    const Player *p, const Weapon *w) {
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
    hp[txt_append_int(hp, sizeof(hp), 0, p->health)] = 0;
    float lo = p->health / (float)PLAYER_MAX_HP;
    text_run(s, HUD_MARGIN, vh - HUD_BASELINE, HUD_TEXT_SIZE, hp,
             1.0f - lo * 0.6f, 0.25f + lo * 0.7f, 0.25f, 1.0f);

    /* Ammo, bottom-right, and red when the gun is empty. */
    char am[16];
    am[txt_append_int(am, sizeof(am), 0, w->ammo[w->cur])] = 0;
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

    /* --- the keycards, under the roster ----------------------------------
     *
     * ENGLISH
     * -------
     * Same idiom as the weapon row above, for the same reason: what you are
     * carrying should be answerable at a glance rather than by trying a door.
     * A player who cannot see which cards they hold has to walk back to a
     * locked door to find out, and the walk teaches them nothing.
     *
     * Each name is drawn in ITS OWN COLOUR when held, because the card, the
     * door and this label are the same colour in the world -- naming a red key
     * in white would make the player translate. Unheld cards go to the same
     * dim grey the unowned weapons use, so "dim means you do not have it" is
     * one rule the HUD applies twice rather than two conventions to learn.
     *
     * The whole row is skipped when the level has no locked doors at all.
     * Three greyed words that never light up are three words of clutter
     * telling the player about a mechanic this map does not use.
     *
     * 한국어
     * ------
     * 위의 무기 행과 같은 방식이며 이유도 같습니다. 무엇을 지니고 있는지는 문을 열어 보는
     * 것이 아니라 한눈에 답할 수 있어야 합니다. 어떤 카드를 가졌는지 볼 수 없는
     * 플레이어는 잠긴 문까지 되돌아가야 알 수 있고, 그 왕복은 아무것도 가르치지 않습니다.
     *
     * 보유한 이름은 *자기 색*으로 그립니다. 카드와 문과 이 글자가 세계 안에서 같은
     * 색이기 때문입니다. 빨간 열쇠를 흰색으로 쓰면 플레이어가 머릿속에서 번역해야 합니다.
     * 없는 카드는 보유하지 않은 무기와 같은 흐린 회색이므로, "흐리면 없는 것"은 HUD가 두
     * 번 적용하는 하나의 규칙이지 배워야 할 두 가지 관례가 아닙니다.
     *
     * 잠긴 문이 하나도 없는 레벨에서는 행 전체를 건너뜁니다. 끝내 켜지지 않는 흐린 단어
     * 셋은 이 맵이 쓰지 않는 장치를 설명하는 잡동사니입니다.
     */
    {
        static const float KEY_RGB[KEY_KINDS][3] = {
            { 0.92f, 0.24f, 0.24f },   /* RED    */
            { 0.36f, 0.55f, 0.95f },   /* BLUE   */
            { 0.95f, 0.85f, 0.30f },   /* YELLOW */
        };

        /* Which kinds this level can ask for. A door needing a key the map
           never places would be a design fault, but it would show up here as a
           name that stays grey, which is information rather than clutter.
           이 레벨이 요구할 수 있는 종류입니다. 맵이 배치하지 않는 열쇠를 요구하는 문은
           설계 결함이지만, 여기서는 계속 회색인 이름으로 드러나므로 잡동사니가 아니라
           정보입니다. */
        int wanted = door_keys_used(l);

        if (wanted != KEY_NONE) {
            float x = HUD_MARGIN;
            float y = vh - HUD_BASELINE - HUD_TEXT_SIZE * 7.4f;
            for (int i = 0; i < KEY_KINDS; i++) {
                int bit = 1 << i;
                if (!(wanted & bit)) continue;
                const char *nm = door_key_name(bit);
                float wd = font_width(1.0f, nm);
                if (p->keys & bit)
                    text_run(s, x, y, 1.0f, nm,
                             KEY_RGB[i][0], KEY_RGB[i][1], KEY_RGB[i][2], 1.0f);
                else
                    text_run(s, x, y, 1.0f, nm, 0.30f, 0.32f, 0.36f, 1.0f);
                x += wd + 10.0f;
            }
        }
    }

    /* --- the refusal, centred and fading ---------------------------------
     *
     * ENGLISH
     * -------
     * The door already reported this and nothing was listening: door_refused
     * has existed since doors did, with a comment saying "so the HUD can say
     * which key", and the HUD never said anything. A locked door that gives no
     * reason is indistinguishable from a door that is broken, and the player's
     * next move -- shoot it, look for a switch, walk away -- depends entirely
     * on knowing which.
     *
     * Centred rather than tucked into a corner, because it answers something
     * the player just did and their eyes are on the door, not on the HUD.
     * Faded over its last moments so it reads as an answer being given rather
     * than a glitch.
     *
     * 한국어
     * ------
     * 문은 이미 이것을 보고하고 있었고 아무도 듣지 않았습니다. door_refused는 문이 생긴
     * 이래로 "HUD가 어떤 열쇠인지 말할 수 있도록"이라는 주석과 함께 존재했지만 HUD는
     * 아무 말도 하지 않았습니다. 이유를 말하지 않는 잠긴 문은 고장 난 문과 구별되지
     * 않으며, 플레이어의 다음 행동(쏜다, 스위치를 찾는다, 떠난다)은 전적으로 그 구분에
     * 달려 있습니다.
     *
     * 구석이 아니라 가운데인 이유는 방금 한 행동에 대한 답이고 시선이 HUD가 아니라 문에
     * 있기 때문입니다. 마지막 순간에 서서히 사라져 결함이 아니라 주어진 답으로 읽힙니다.
     */
    {
        int k = door_notice_key(l);
        if (k != KEY_NONE) {
            char line[48];
            int  n = txt_append_str(line, sizeof(line), 0, door_key_name(k));
            n = txt_append_str(line, sizeof(line), n, " KEYCARD REQUIRED");
            line[n] = 0;
            float lw = font_width(HUD_NOTICE_SIZE, line);

            /* Fades only over the tail. Fading across the whole life would
               start it already dimmed, and the moment it is most needed is the
               moment it appears.
               *끝부분*에서만 사라집니다. 수명 전체에 걸쳐 페이드하면 시작부터 흐릿하며,
               가장 필요한 순간은 나타나는 그 순간입니다. */
            float a = door_notice_left(l) / HUD_NOTICE_FADE;
            if (a > 1.0f) a = 1.0f;

            text_run(s, (vw - lw) * 0.5f, vh * HUD_NOTICE_Y, HUD_NOTICE_SIZE,
                     line, 0.95f, 0.80f, 0.35f, a);
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
    int  n = txt_append_str(line, sizeof(line), 0, "health ");
    n = txt_append_int(line, sizeof(line), n, p->health);
    n = txt_append_str(line, sizeof(line), n, "   ammo ");
    n = txt_append_int(line, sizeof(line), n, w->ammo[w->cur]);
    line[n] = 0;
    float lw = font_width(WIN_STAT_SIZE, line);
    text_run(s, (vw - lw) * 0.5f, vh * 0.5f + 4.0f, WIN_STAT_SIZE, line,
             0.85f, 0.85f, 0.85f, 1.0f);

    const char *hint = "ESC for menu";
    float hw = font_width(WIN_HINT_SIZE, hint);
    text_run(s, (vw - hw) * 0.5f, vh * 0.5f + 40.0f, WIN_HINT_SIZE, hint,
             0.55f, 0.55f, 0.58f, 1.0f);

    ui_end();
}

void scene_draw_between(Scene *s, int vw, int vh, const char *cleared,
                        const char *entering, float t, float total) {
    DIAG_WANT_UI_PASS(post_in_world_pass());

    ui_begin(vw, vh);

    /* FADES IN AND BACK OUT, rather than appearing and vanishing. The level
       behind it is still drawn and still lit; a screen that cut in over it
       would read as the game having changed mode, and what actually happened
       is that the player finished something. The fade is the difference
       between a transition and a jump.
       나타났다 사라지는 것이 아니라 서서히 들어오고 다시 나갑니다. 뒤의 레벨은 여전히
       그려지고 여전히 밝습니다. 그 위로 잘라 들어오는 화면은 게임이 모드를 바꾼 것으로
       읽히지만, 실제로 일어난 일은 플레이어가 무언가를 끝냈다는 것입니다. 페이드가
       전환과 도약의 차이입니다. */
    float k = 1.0f;
    if (total > 0.0f) {
        float in  = t / BETWEEN_FADE;
        float out = (total - t) / BETWEEN_FADE;
        k = in < out ? in : out;
        if (k > 1.0f) k = 1.0f;
        if (k < 0.0f) k = 0.0f;
    }

    full_screen_wash(s, vw, vh, 0.0f, 0.0f, 0.0f, BETWEEN_DIM * k);

    rd_mode(RD_TEXT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_texture());

    /* Two lines, each a label over a name, because the two facts are not the
       same kind of thing: one is what the player did and the other is where
       they are going. Running them together as "ARENA -> VAULT" would be
       shorter and would say neither.
       각각 이름 위에 표제가 붙은 두 줄입니다. 두 사실이 같은 종류가 아니기 때문입니다.
       하나는 플레이어가 한 일이고 다른 하나는 갈 곳입니다. "ARENA -> VAULT"로 붙이면 더
       짧지만 둘 다 말하지 못합니다. */
    const char *l1 = "CLEARED";
    float w1 = font_width(BETWEEN_LABEL_SIZE, l1);
    text_run(s, (vw - w1) * 0.5f, vh * 0.5f - 96.0f, BETWEEN_LABEL_SIZE, l1,
             0.55f, 0.58f, 0.62f, k);

    float wc = font_width(BETWEEN_NAME_SIZE, cleared);
    text_run(s, (vw - wc) * 0.5f, vh * 0.5f - 58.0f, BETWEEN_NAME_SIZE, cleared,
             0.95f, 0.85f, 0.35f, k);

    const char *l2 = "ENTERING";
    float w2 = font_width(BETWEEN_LABEL_SIZE, l2);
    text_run(s, (vw - w2) * 0.5f, vh * 0.5f + 22.0f, BETWEEN_LABEL_SIZE, l2,
             0.55f, 0.58f, 0.62f, k);

    float we = font_width(BETWEEN_NAME_SIZE, entering);
    text_run(s, (vw - we) * 0.5f, vh * 0.5f + 60.0f, BETWEEN_NAME_SIZE, entering,
             0.60f, 0.80f, 0.95f, k);

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

/**
 * @brief Draws the credits screen's licence notices.
 *
 * ENGLISH: The one menu screen whose body is prose rather than rows, and the
 * only reason ::scene_draw_menu was three times the size of any other draw
 * function here. A licence has to be shown in full, so it does not shrink --
 * which is exactly the argument for giving it its own function rather than
 * letting it dominate the one that draws every screen.
 *
 * 한국어: 본문이 행이 아니라 산문인 유일한 메뉴 화면이며, ::scene_draw_menu가 이곳의 다른
 * 어떤 그리기 함수보다 세 배 컸던 이유 전부입니다. 라이선스는 온전히 보여야 하므로 줄어들지
 * 않습니다. 그것이 바로 모든 화면을 그리는 함수를 지배하게 두는 대신 자기 함수를 주어야
 * 한다는 논거입니다.
 */
static void draw_menu_notices(Scene *s, int vw, int vh, float cx, int rows) {
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
}

/**
 * @brief Draws the menu's rows, with the highlighted one filled.
 * / 메뉴의 행들을 그리며, 선택된 행은 채워진 막대를 함께 그립니다.
 */
static void draw_menu_rows(Scene *s, int vw, int vh, float cx, int rows, int cur) {
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
}

/**
 * @brief Draws the control hint under the rows.
 *
 * ENGLISH: Names the mouse first, because that is what a player reaches for
 * once the cursor appears. The credits screen gets none -- its hint would be
 * drawn where the notice text already is.
 *
 * 한국어: 마우스를 먼저 적습니다. 커서가 나타나면 플레이어가 손을 뻗는 것이 그것이기
 * 때문입니다. 크레딧 화면에는 없습니다. 그 힌트는 이미 고지 문구가 있는 자리에 그려집니다.
 */
static void draw_menu_hint(Scene *s, int vw, int vh, float cx) {
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

    /* Prose rather than rows, and long enough to have its own function. */
    if (menu_screen() == MENU_CREDITS) draw_menu_notices(s, vw, vh, cx, rows);

    draw_menu_rows(s, vw, vh, cx, rows, cur);

    if (menu_screen() != MENU_CREDITS) draw_menu_hint(s, vw, vh, cx);

    ui_end();
}

void scene_draw_proj(Scene *s, const Pools *pl, mat4 vp, v3 cam_right, v3 cam_up) {
    DIAG_WANT_WORLD_PASS(post_in_world_pass());

    int n = proj_count(pl), live = 0;

    mb_reset(&s->shot_buf);
    for (int i = 0; i < n; i++) {
        const Proj *p = proj_at(pl, i);
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
        const Proj *p = proj_at(pl, i);
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

/* --------------------------------------------------------------- one frame */

void scene_frame(const World *w, Scene *sc, int vw, int vh, int frozen) {
    float aspect = (float)vw / (float)vh;

    /* Bind the offscreen target, if there is one. It returns the aspect of the
       small buffer rather than the window's, and the world must be rendered
       with THAT -- using the window's here stretches everything, because the
       offscreen buffer's proportions need not match exactly once its width has
       been rounded to whole pixels.
       오프스크린 타깃이 있으면 바인딩합니다. 창이 아닌 작은 버퍼의 종횡비를 반환하며,
       월드는 *그 값으로* 렌더링해야 합니다. 여기서 창의 값을 쓰면 전체가 늘어나는데,
       오프스크린 버퍼의 너비가 정수 픽셀로 반올림되고 나면 그 비율이 창과 정확히 일치하지
       않을 수 있기 때문입니다. */
    float post_aspect = post_begin();
    if (post_aspect > 0.0f) aspect = post_aspect;
    else                    glViewport(0, 0, vw, vh);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* The clock animated materials run against. Advanced by ::world_step and
       only read here, so a lava floor stops churning when the world stops. */
    rd_time(w->run.world_time);

    /* Vertex snapping, on for the world and off again before the UI. The grid
       is the offscreen buffer, so the quantisation lands on the pixels the
       image is actually rasterised into. post_size reports 0,0 when the pass is
       off, which disables the snap -- the right answer, since with no
       pixelisation there is no grid to snap to.
       정점 스냅입니다. 월드에 대해 켜고 UI 이전에 다시 끕니다. 격자는 오프스크린 버퍼이므로
       양자화가 이미지가 실제 래스터화되는 픽셀에 놓입니다. 패스가 꺼져 있으면 post_size가
       0,0을 보고하여 스냅이 비활성화되는데, 픽셀화가 없으면 맞출 격자도 없으므로 올바른
       동작입니다. */
    {
        int sw, sh;
        post_size(&sw, &sh);
        rd_snap((float)sw / PSX_SNAP_COARSE, (float)sh / PSX_SNAP_COARSE);
    }

    /* Recoil rides on top of the player's own pitch and springs back. */
    float aim_pitch = w->pitch + w->weapon.recoil;

    /* --- the death collapse ----------------------------------------------
       Applied to the CAMERA rather than to the player's position, so the body
       the simulation knows about never moves. Sinking the real position would
       put the eye inside the floor, where level_trace reports an immediate hit
       at zero range -- the failure mode hooktest's fixture note describes --
       and would leave the player somewhere they could not legally stand if the
       run were ever resumed.
       플레이어 위치가 아니라 *카메라*에 적용하므로 시뮬레이션이 아는 몸은 움직이지
       않습니다. 실제 위치를 내리면 눈이 바닥 안으로 들어가고, 그곳에서 level_trace는
       거리 0에서 즉시 충돌을 보고합니다. hooktest의 픽스처 주석이 설명하는 실패 양상이며,
       플레이가 재개된다면 플레이어가 합법적으로 설 수 없는 곳에 남게 됩니다. */
    v3    eye_pos   = w->player.pos;
    float cam_pitch = aim_pitch;
    float cam_roll  = 0.0f;
    if (w->run.dead) {
        float k = w->run.death_time / DEATH_ANIM_TIME;
        if (k > 1.0f) k = 1.0f;
        /* Ease out: 1-(1-k)^2. Fast at the start, settling at the end --
           a body falls, it does not descend.
           감속 이징입니다. 처음에 빠르고 끝에서 안착합니다. 몸은 떨어지는 것이지
           내려가는 것이 아닙니다. */
        float e = 1.0f - (1.0f - k) * (1.0f - k);
        eye_pos.y  -= DEATH_DROP * e;
        cam_pitch  -= DEATH_PITCH * e;
        cam_roll    = DEATH_ROLL * e;
    }

    /* --- the shake --------------------------------------------------------
       AFTER the collapse and on top of it, because they answer different
       questions: the collapse says where the body fell to, the shake says the
       room is still ringing. A death that ends in a jolt reads as the two
       happening to the same camera, which is what they are.

       Displaces the EYE and rolls the view; it never touches ::World::yaw or
       ::World::pitch. Those are the aim, ::world_step owns them, and a shake
       that moved where the shots go would make the player fight their own gun.
       See ::RunState::shake.

       The phase comes from ::RunState::world_time rather than from a clock kept
       here, so the shake is a pure function of state a headless step already
       produces -- a recorded demo shakes identically on playback, and nothing
       had to be added to ::World to make that true. The three multipliers are
       deliberately not whole ratios: three sine waves at 1:2:3 would beat back
       into a single visible period, which reads as a wobble rather than a jolt.

       흔들림입니다. 쓰러짐 *뒤에* 그 위로 얹는 이유는 둘이 서로 다른 질문에 답하기
       때문입니다. 쓰러짐은 몸이 어디로 떨어졌는지를 말하고, 흔들림은 방이 아직 울리고 있음을
       말합니다. 충격으로 끝나는 죽음은 그 둘이 같은 카메라에 일어나는 것으로 읽히며, 실제로
       그렇습니다.

       *눈*을 옮기고 시야를 굴릴 뿐 ::World::yaw나 ::World::pitch는 결코 건드리지 않습니다.
       그것들은 조준이고 ::world_step의 것이며, 탄착점을 옮기는 흔들림은 플레이어가 자기 총과
       싸우게 만듭니다. ::RunState::shake를 참조하십시오.

       위상은 이곳에 둔 시계가 아니라 ::RunState::world_time에서 옵니다. 그래서 흔들림은
       헤드리스 스텝이 이미 만들어 내는 상태만의 순수 함수이며, 기록된 데모는 재생 시 동일하게
       흔들리고 그것을 참으로 만들기 위해 ::World에 무엇도 추가할 필요가 없었습니다. 세 배수를
       일부러 정수비로 두지 않은 이유는, 1:2:3의 사인파 셋은 하나의 눈에 띄는 주기로 맞물려
       충격이 아니라 흔들거림으로 읽히기 때문입니다. */
    if (w->run.shake > 0.0f) {
        float s = w->run.shake;
        float t = w->run.world_time * SHAKE_FREQ;
        eye_pos.x += sinf(t * 1.00f) * s * SHAKE_SWAY;
        eye_pos.y += sinf(t * 1.71f) * s * SHAKE_SWAY;
        cam_roll  += sinf(t * 1.31f) * s * SHAKE_ROLL;
    }

    /* One derivation, two users. The billboards below span along `cam.right`
       and `cam.up`, and they have to be the axes the view matrix was built
       from: this used to call mat4_fps_view_roll and then re-derive the
       identical trigonometry by hand, roll included, a dozen lines later. The
       drift that invites is silent and specific -- sprites keep facing where
       the camera used to be and turn edge-on as it rolls, so the monsters
       vanish exactly as the player dies. See ::CamBasis.
       하나의 유도, 두 사용처입니다. 아래의 빌보드는 `cam.right`와 `cam.up`을 따라
       펼쳐지며, 그것은 뷰 행렬이 만들어진 축이어야 합니다. 이곳은 이전에
       mat4_fps_view_roll을 호출한 뒤 열두 줄 아래에서 롤까지 포함해 동일한 삼각함수
       계산을 손으로 다시 유도하고 있었습니다. 그것이 부르는 어긋남은 조용하고
       구체적입니다. 스프라이트가 카메라가 있던 곳을 계속 향하다가 롤링에 따라 옆으로
       서 버리므로, 플레이어가 죽는 바로 그 순간에 몬스터가 사라집니다. ::CamBasis를
       참조하십시오. */
    CamBasis cam = cam_basis(w->yaw, cam_pitch, cam_roll);

    mat4 proj = mat4_perspective(WORLD_FOV, aspect, 0.05f, 200.0f);
    mat4 vp   = mat4_mul(proj, mat4_view_of(eye_pos, cam));

    /* --- world ---
       Lit and fogged from the camera's real position, which during the
       collapse is the fallen eye rather than the standing one.
       카메라의 실제 위치를 기준으로 조명과 안개를 적용합니다. 쓰러지는 동안 그것은
       서 있던 눈이 아니라 넘어진 눈입니다. */
    /* BEFORE the level and therefore before every sprite pass below it, which
       is the whole point: a grenade has to light the wall and the monster
       standing against it by the same amount, or the monster reads as pasted
       on. See ::scene_lights.
       레벨보다 먼저이며 따라서 그 아래의 모든 스프라이트 패스보다 먼저입니다. 그것이 요점의
       전부입니다. 유탄은 벽과 그 앞에 선 몬스터를 같은 양만큼 밝혀야 하며, 그러지 않으면
       몬스터가 붙여 놓은 것처럼 읽힙니다. ::scene_lights를 참조하십시오. */
    scene_lights(w, eye_pos);

    scene_draw_level(sc, vp, eye_pos);

    /* --- monsters, pickups and projectiles ---
       Sprite passes, each building its billboards on the CPU and uploading
       once. They stay on the world side of the pass boundary so they are
       pixelised and dithered along with everything else -- see scene.h. */
    scene_draw_enemies(sc, &w->pools, vp, eye_pos, cam.right);
    scene_draw_pickups(sc, &w->pools, vp, eye_pos, cam.right);
    scene_draw_shots  (sc, &w->pools, vp, cam.right, cam.up);
    scene_draw_proj   (sc, &w->pools, vp, cam.right, cam.up);
    fx_draw(&w->pools, vp, cam.right, cam.up);

    /* --- what shots left behind, and the rope, still in world space ---
       Both are billboards with one winding, so culling comes off for the pair.
       decal_draw goes first because that is where these two used to sit inside
       one function, and the order they were written in is the order they blend
       correctly in.
       둘 다 감김 방향이 하나인 빌보드이므로 두 호출을 위해 컬링을 끕니다. decal_draw가 먼저인
       이유는 이 둘이 원래 한 함수 안에서 그 순서로 있었고, 작성된 순서가 곧 올바르게 블렌딩되는
       순서이기 때문입니다. */
    glDisable(GL_CULL_FACE);
    decal_draw(&w->pools, vp, eye_pos, cam.right, cam.up);
    wpview_draw_world(&sc->wpview, &w->weapon, vp, eye_pos, cam.right, cam.up);

    /* --- the gun, over a cleared depth buffer ---
       Dropped the moment the player dies. The view model is drawn in its own
       space with its own projection, so it does not roll or fall with the
       camera -- it would hang perfectly level in the middle of the screen while
       the world tipped over behind it, which is a far louder tell than simply
       not being there. A dead hand lets go.
       플레이어가 죽는 순간 사라집니다. 뷰 모델은 자체 공간에서 자체 투영으로 그려지므로
       카메라와 함께 기울거나 떨어지지 않습니다. 뒤에서 월드가 넘어가는 동안 화면 한가운데에
       완벽히 수평으로 떠 있게 되는데, 그것은 그냥 없는 것보다 훨씬 요란한 표시입니다.
       죽은 손은 놓습니다. */
    glEnable(GL_CULL_FACE);
    if (!w->run.dead) wpview_draw_view(&sc->wpview, &w->weapon, aspect);

    /* --- resolve the offscreen buffer to the window ---------------------
       A no-op when the effect is off or unavailable, in which case the frame
       was drawn straight to the window and this changes nothing. See the note
       on this function for what the boundary means.
       효과가 꺼져 있거나 사용할 수 없으면 아무 동작도 하지 않으며, 그 경우 프레임은 창에
       직접 그려졌으므로 달라지는 것이 없습니다. 이 경계의 의미는 이 함수의 참고 사항을
       확인하십시오. */
    post_end(vw, vh);

    /* --- crosshair ---
       Hidden while frozen, for the same reason it is hidden on the win screen:
       a crosshair implies you can still act.
       정지 중에는 숨깁니다. 승리 화면에서 숨기는 것과 같은 이유이며, 조준점은 아직
       행동할 수 있음을 암시하기 때문입니다. */
    if (!frozen) {
        glDisable(GL_CULL_FACE);
        /* post_end leaves depth testing off and does not restore culling, so
           the UI passes below set up their own state -- which they already did,
           because the HUD never wanted depth anyway.
           post_end는 깊이 테스트를 끈 상태로 두고 컬링도 복원하지 않으므로, 아래 UI
           패스들이 자체적으로 상태를 설정합니다. HUD는 원래 깊이 테스트를 필요로 하지
           않았으므로 이미 그렇게 하고 있었습니다. */
        /* The range test traces the level, so it happens here rather than
           inside the draw call -- see the note on wpview_draw_hud.
           사거리 판정은 레벨을 탐색하므로 그리기 호출 내부가 아니라 이곳에서
           수행합니다. wpview_draw_hud의 참고 사항을 확인하십시오. */
        int hook_ready = wp_hook_in_range(&w->weapon, &w->pools, &w->level,
                                          w->player.pos, w->yaw, w->pitch);
        wpview_draw_hud(&sc->wpview, &w->weapon, aspect, hook_ready);
        glEnable(GL_CULL_FACE);
    }

    /* The UI is drawn at native resolution and must not wobble: text snapped to
       a coarse grid loses whole glyph rows.
       UI는 원해상도로 그려지며 흔들려서는 안 됩니다. 성긴 격자에 스냅된 텍스트는
       글리프의 행 전체를 잃습니다. */
    rd_snap(0.0f, 0.0f);

    /* The HUD is skipped on the title screen: health and ammo belong to a run,
       and showing them before one has started says the game is already in
       progress.

       Otherwise it draws unconditionally and the end screens go OVER it, dim
       included. Making them exclusive would be the obvious simplification and
       it is wrong: the readouts belong to the frozen frame underneath, and the
       dimming is what pushes them back rather than removing them.

       타이틀 화면에서는 HUD를 건너뜁니다. 체력과 탄약은 진행 중인 플레이에 속하며, 시작하기도
       전에 표시하면 게임이 이미 진행 중이라고 말하는 셈입니다.

       그 외에는 조건 없이 그리고, 종료 화면들이 흐리게 처리된 부분까지 포함해 그 *위에*
       놓입니다. 둘을 배타적으로 만드는 것이 자명한 단순화처럼 보이지만 틀렸습니다. 그 수치들은
       아래에 정지된 프레임에 속하며, 흐리게 처리하는 것은 그것들을 제거하는 것이 아니라 뒤로
       밀어내는 일입니다. */
    if (!w->run.title) scene_draw_hud(sc, vw, vh, &w->level, &w->player, &w->weapon);

    /* The three end/start screens are mutually exclusive by construction:
       `title` is cleared before a run can begin, and a run that has been won
       cannot also have been lost.
       세 개의 시작·종료 화면은 구조적으로 상호 배타적입니다. `title`은 플레이가
       시작되기 전에 해제되며, 승리한 플레이가 동시에 패배할 수는 없습니다. */
    if (w->run.title)
        scene_draw_title(sc, vw, vh, w->run.title_time);
    else if (w->run.won)
        scene_draw_win(sc, vw, vh, &w->player, &w->weapon);
    else if (w->run.between)
        /* Before the death screen in this chain and after the win screen,
           which is the order the states can actually co-exist in: the world is
           frozen during the intermission so nothing can kill the player, but a
           terminal level sets `won` instead of `between` and both must not be
           reachable at once.
           이 사슬에서 사망 화면보다 앞, 승리 화면보다 뒤입니다. 상태들이 실제로 공존할 수
           있는 순서가 그렇습니다. 인터미션 동안 월드가 정지하므로 플레이어가 죽을 수 없고,
           종착 레벨은 `between`이 아니라 `won`을 세우므로 둘이 동시에 성립해서는
           안 됩니다. */
        scene_draw_between(sc, vw, vh, w->run.cleared, w->run.entering,
                           w->run.between_time, WORLD_BETWEEN_TIME);
    else if (w->run.dead)
        /* The overlay's clock starts when the COLLAPSE ends, not when the
           player dies. Fading a red wash in over the fall would hide the
           animation behind it, and the fall is the part that says what
           happened.
           오버레이의 시계는 플레이어가 죽을 때가 아니라 *쓰러짐이 끝날 때* 시작합니다.
           넘어지는 동안 붉은 막을 덮으면 그 뒤로 애니메이션이 가려지는데, 무슨 일이
           있었는지 말해 주는 것이 바로 그 넘어짐입니다. */
        scene_draw_death(sc, vw, vh, w->run.death_time - DEATH_ANIM_TIME,
                         w->run.death_time > DEATH_INPUT_DELAY);

    /* Last, so it sits over the HUD and the end screens both. A menu opened
       from the win screen has to be readable too, and it is the thing the
       player is currently operating.
       마지막에 그려 HUD와 종료 화면 양쪽 위에 놓입니다. 승리 화면에서 연 메뉴도 읽을
       수 있어야 하며, 그것이 플레이어가 지금 조작하고 있는 대상입니다. */
    scene_draw_menu(sc, vw, vh);
}
