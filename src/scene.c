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
#include "post.h"     /* post_begin/post_end/post_size -- this file drives the pass */
#include "menu.h"     /* the rows the ESC menu draws, read rather than copied */
/* The best wave, for the title screen. A saved figure rather than a run's, so
   it cannot come through ::World -- and the title screen is the only place it
   is ever shown. Read the same way ::menu_screen is, three lines up.
   타이틀 화면을 위한 최고 웨이브입니다. 플레이의 값이 아니라 저장된 값이므로 ::World를 통해
   올 수 없으며, 타이틀 화면이 그것이 보이는 유일한 곳입니다. 세 줄 위의 ::menu_screen과 같은
   방식으로 읽습니다. */
#include "save.h"
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
/* OFF. The machinery below is intact and one edit from returning; what
   changed is that this project stopped asking for the look.
   THE ARENA IS WHAT DECIDED IT. Both halves of the PlayStation look scale with
   how much screen a single polygon covers, and ::PSX_AFFINE's own note already
   said where that ends: "a brush level has single faces bigger than a
   PlayStation drew in a whole room -- a wall here is one quad where that
   hardware would have had a dozen. At 1.0 those faces crease along their
   diagonal hard enough to read as a broken renderer." lqdm1's longest edge is
   72 metres and 13% of its faces are over 4x4m, measured by tools/lightprobe.c
   -- geometry coarser than anything those two numbers were tuned against, so
   the wobble stopped reading as a period and started reading as the mesh
   coming apart.
   BOTH, NOT ONE. The note at the call site is explicit that they are halves of
   one thing -- "the vertices wobble and the texture between them swims.
   Turning on either alone reads as a fault in the renderer rather than as a
   period" -- so removing the wobble and leaving the swim would trade one
   artefact for a worse one.
   *꺼졌습니다.* 아래의 기구는 그대로이며 한 번의 편집으로 돌아옵니다. 바뀐 것은 이
   프로젝트가 그 룩을 더 이상 요구하지 않는다는 것입니다.
   *아레나가 그것을 결정했습니다.* 플레이스테이션 룩의 두 절반 모두 폴리곤 하나가 화면을
   얼마나 덮는지에 따라 커지며, ::PSX_AFFINE의 각주가 그 끝을 이미 말해 두었습니다. lqdm1의
   가장 긴 모서리는 72미터이고 면의 13%가 4x4m를 넘습니다. 그 두 수가 맞춰졌던 어떤 것보다
   거친 지오메트리이므로, 흔들림은 시대의 표현이기를 그만두고 메쉬가 무너지는 것으로 읽히기
   시작했습니다.
   *하나가 아니라 둘 다입니다.* 호출 지점의 각주가 둘이 한 가지의 절반이라고 분명히 말하므로,
   흔들림만 없애고 헤엄을 남기는 것은 아티팩트 하나를 더 나쁜 것과 맞바꾸는 일입니다. */
#define PSX_SNAP_COARSE 0.0f

/**
 * @brief How much affine texture swim to use. See ::rd_affine.
 *
 * ENGLISH
 * -------
 * NOT 1.0, and the reason is this project's geometry rather than taste. The
 * warp scales with how much of the screen a polygon covers, and a brush level
 * has single faces bigger than a PlayStation drew in a whole room -- a wall
 * here is one quad where that hardware would have had a dozen. At 1.0 those
 * faces crease along their diagonal hard enough to read as a broken renderer.
 *
 * The number is what a corridor wall looks right at when walked past, which is
 * the same way ::PSX_SNAP_COARSE was chosen. Enough that a floor seen at a
 * glancing angle visibly slides; not so much that a doorway bends.
 *
 * @note Here rather than in the menu on purpose, for now. It is the same class
 *       of value as the snap grid -- something the game HAS rather than
 *       something a player sets -- and the two should be exposed together if
 *       either is.
 *
 * 한국어
 * ------
 * @brief 어파인 텍스처 헤엄을 얼마나 쓸지. ::rd_affine을 참조하십시오.
 *
 * *1.0이 아니며*, 이유는 취향이 아니라 이 프로젝트의 지오메트리입니다. 왜곡은 폴리곤이 화면을
 * 얼마나 덮는지에 비례하는데, 브러시 레벨은 플레이스테이션이 방 하나에 그리던 것보다 큰 단일
 * 면을 가집니다. 이곳의 벽 하나가 그 하드웨어라면 열몇 개였을 것을 사각형 하나로 씁니다.
 * 1.0에서는 그런 면들이 대각선을 따라 충분히 세게 접혀 고장 난 렌더러로 읽힙니다.
 *
 * 이 값은 복도 벽을 지나쳐 걸을 때 알맞아 보이는 값이며, ::PSX_SNAP_COARSE가 정해진 방식과
 * 같습니다. 비스듬히 본 바닥이 눈에 띄게 미끄러질 만큼은 되고, 출입구가 휘어질 만큼은 아닙니다.
 *
 * @note 당분간 의도적으로 메뉴가 아니라 이곳에 둡니다. 스냅 격자와 같은 부류의 값이며, 플레이어가
 *       설정하는 것이 아니라 게임이 *지닌* 것입니다. 둘 중 하나를 노출한다면 함께 노출해야
 *       합니다.
 */
#define PSX_AFFINE 0.0f

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
/* Reaches further than it did, to match the emission tier ::scene_draw_shots
   now draws around the bolt. The two are one event told twice -- to the eye as
   a glow and to the geometry as a light -- and a bolt whose halo is two metres
   across while the wall behind it brightens over one is a bolt with a visible
   seam in it.
   ::scene_draw_shots가 이제 볼트 둘레에 그리는 발광 겹에 맞춰 예전보다 멀리 닿습니다. 둘은
   하나의 사건을 두 번 말하는 것입니다. 눈에게는 발광으로, 지오메트리에게는 빛으로. 헤일로는
   2미터인데 뒤의 벽은 1미터에 걸쳐 밝아지는 볼트는 눈에 보이는 이음매를 가진 볼트입니다. */
#define LIGHT_SHOT_RADIUS    6.0f   ///< @brief Metres a monster bolt lights. / 몬스터 볼트가 밝히는 거리 (미터).
#define LIGHT_SHOT_POWER     0.70f  ///< @brief Bolts read as the brighter thing in a dark room. / 볼트는 어두운 방에서 더 밝은 것으로 읽힙니다.

/* --- the shrine ------------------------------------------------------------
   Generous and warm, because unlike every other entry above this one is not an
   event -- it burns for the whole breather, and what it has to do is be
   findable from wherever the player happened to be standing when the room went
   quiet. Gold rather than the blue the bolts and the hook use: the one thing in
   the room that is a REWARD should not be lit in the colour of the things that
   are about to hurt.
   위의 다른 모든 항목과 달리 이것은 사건이 아니므로 넉넉하고 따뜻합니다. 휴식 내내
   타오르며, 해야 할 일은 방이 조용해질 때 플레이어가 어디에 서 있었든 찾을 수 있게 되는
   것입니다. 볼트와 갈고리가 쓰는 파랑이 아니라 금색인 이유는, 방에서 유일하게 *보상*인 것이
   곧 아프게 할 것들의 색으로 밝혀져서는 안 되기 때문입니다. */
#define LIGHT_ALTAR_RADIUS   9.0f   ///< @brief Metres a burning shrine lights. / 타오르는 제단이 밝히는 거리 (미터).
#define LIGHT_ALTAR_POWER    0.55f  ///< @brief Steady, for as long as it burns. / 타오르는 동안 일정합니다.
#define LIGHT_ALTAR_HEIGHT   0.9f   ///< @brief Metres above the floor it sits, so the floor is what brightens. / 바닥 위 높이(미터). 바닥이 밝아지도록 합니다.

/** @brief Warm white: burnt powder, not a torch. / 따뜻한 백색. 횃불이 아니라 연소한 화약입니다. */
static const float LIGHT_COL_MUZZLE[3] = { 1.00f, 0.86f, 0.62f };
/** @brief The grenade's own hot orange. / 유탄 자신의 뜨거운 주황색. */
static const float LIGHT_COL_PROJ[3]   = { 1.00f, 0.62f, 0.26f };
/**
 * @brief The bolt's cold blue, taken from how ::scene_draw_shots actually draws it.
 *
 * ENGLISH: This said "cold green" and was green, and the bolt it lights is BLUE
 * -- scene_draw_shots paints the halo (0.10,0.42,0.85) and the core
 * (0.85,0.98,1.00), and boltburst cools into the same blue. A light that
 * disagrees with the thing emitting it is worse than no light: the wall says one
 * colour and the bolt in front of it says another, and the eye reads the wall.
 *
 * 한국어: 이 값은 "차가운 녹색"이라 적혀 있었고 실제로 녹색이었는데, 그것이 밝히는 볼트는
 * *파란색*입니다. scene_draw_shots가 헤일로를 (0.10,0.42,0.85)로, 코어를 (0.85,0.98,1.00)로
 * 칠하고 boltburst도 같은 파랑으로 식습니다. 자기를 내는 것과 어긋나는 광원은 광원이 없는 것보다
 * 나쁩니다. 벽은 한 색을 말하고 그 앞의 볼트는 다른 색을 말하는데, 눈은 벽을 믿습니다.
 */
static const float LIGHT_COL_SHOT[3]   = { 0.42f, 0.68f, 1.00f };

/** @brief The shrine's warm gold, the same hue `altarcore` burns in. / 제단의 따뜻한 금색. `altarcore`가 타는 것과 같은 색조입니다. */
static const float LIGHT_COL_ALTAR[3]  = { 1.00f, 0.82f, 0.46f };

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

    /* The shrine a cleared wave lit, for as long as it burns. Not faded over
       its own timer, unlike the muzzle flash: a marker that dims as the
       breather runs out is dimmest exactly when the player is furthest into
       deciding whether to go and fetch what it marks.
       정리된 웨이브가 켠 제단이며, 타오르는 동안 지속됩니다. 총구 섬광과 달리 자기 타이머에
       맞춰 감쇠하지 *않습니다.* 휴식이 끝나 갈수록 어두워지는 표식은, 플레이어가 그것이
       표시하는 것을 가지러 갈지 말지 결정하는 데 가장 깊이 들어간 바로 그때 가장
       어둡습니다. */
    if (w->run.altar_time > 0.0f)
        light_offer(ls, &n,
                    v3f(w->run.altar_pos.x,
                        w->run.altar_pos.y + LIGHT_ALTAR_HEIGHT,
                        w->run.altar_pos.z),
                    LIGHT_ALTAR_RADIUS, LIGHT_COL_ALTAR, LIGHT_ALTAR_POWER, eye);

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

/* --- the emission ----------------------------------------------------------
 *
 * ENGLISH
 * -------
 * A THIRD TIER, OUTSIDE THE OTHER TWO, and it is not more of the same thing.
 * The halo and the core describe the OBJECT -- how big the bolt is and how hot
 * its middle is -- and both stop at the bolt's own edge. What was missing is
 * the light it is supposed to be throwing: a projectile that glows exactly as
 * far as it is wide reads as a painted sprite moving through a room rather
 * than as something burning in it.
 *
 * Twice the halo's width and a fifth of its opacity, because that is what an
 * emission is: it has to reach past the object and it must not compete with
 * it. Additive at this alpha it is invisible against a lit wall and obvious
 * against a dark corridor, which is exactly where a bolt you have not seen yet
 * is the one that hits you.
 *
 * IT BREATHES, and that is the half that makes it read as emission rather than
 * as a bigger sprite. A steady disc of light is a shape; one that swells and
 * settles is a source. The rate is fast enough to be seen in the half-second a
 * bolt is in the air and slow enough not to strobe -- ::SHOT_SPIN already
 * turns the petals, so this only has to change the size.
 *
 * 한국어
 * ------
 * *다른 둘의 바깥에 있는 세 번째 겹*이며, 같은 것을 더한 것이 아닙니다. 헤일로와 코어는
 * *물체*를 서술합니다. 볼트가 얼마나 큰지, 그 한가운데가 얼마나 뜨거운지이며, 둘 다 볼트
 * 자신의 가장자리에서 멈춥니다. 빠져 있던 것은 그것이 내뿜고 있어야 할 *빛*입니다. 자기 폭만큼만
 * 빛나는 발사체는 방 안에서 타고 있는 무언가가 아니라 방을 가로질러 움직이는 색칠된
 * 스프라이트로 읽힙니다.
 *
 * 헤일로의 두 배 폭과 5분의 1 불투명도인 이유는 그것이 발광이기 때문입니다. 물체 너머까지
 * 닿아야 하고, 물체와 경쟁해서는 안 됩니다. 이 알파의 가산 블렌드는 밝은 벽 앞에서는 보이지
 * 않고 어두운 복도에서는 뚜렷한데, 아직 보지 못한 볼트가 곧 당신을 맞히는 볼트인 곳이 정확히
 * 그곳입니다.
 *
 * *숨을 쉬며*, 그것이 이것을 더 큰 스프라이트가 아니라 발광으로 읽히게 하는 나머지 절반입니다.
 * 일정한 빛의 원반은 형태이고, 부풀었다 가라앉는 것은 광원입니다. */
#define SHOT_GLOWS      2       /* the outer emission: big, dim, and it breathes */
#define SHOT_GLOW_SIZE  1.30f
#define SHOT_GLOW_PULSE 0.22f   /* fraction of the size it swells by */
#define SHOT_GLOW_RATE  9.0f    /* radians per second of remaining life */

/** @brief Quads one bolt is built from, outermost tier first. / 볼트 하나를 이루는 사각형. 가장 바깥 겹부터입니다. */
#define SHOT_QUADS (SHOT_GLOWS + SHOT_HALOS + SHOT_CORES)


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

/** @brief Where an item's billboard sits: lifted clear of the floor, and bobbing. / 아이템 빌보드가 놓이는 자리. 바닥에서 들리고 위아래로 움직입니다. */
static v3 pickup_centre(const Pickup *p) {
    float bob = PICKUP_BOB * sinf(p->anim * PICKUP_BOB_RATE);
    return v3f(p->pos.x, p->pos.y + PICKUP_LIFT + bob, p->pos.z);
}

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
/* Bigger than the hint below it and much smaller than the title above it,
   which is the order the three lines are read in: what happened, what it came
   to, what to do about it. A score at hint size would be skipped; one at title
   size would compete with the word it is qualifying.
   아래의 안내 문구보다 크고 위의 제목보다 훨씬 작으며, 그것이 세 줄이 읽히는 순서입니다.
   무슨 일이 있었는가, 그것이 무엇이 되었는가, 그래서 어떻게 할 것인가입니다. 안내 문구 크기의
   성적은 건너뛰어지고, 제목 크기의 성적은 자신이 수식하는 단어와 경쟁합니다. */
#define DEATH_STAT_SIZE  2.0f
#define DEATH_HINT_SIZE  1.6f

/* The title screen's own header. TITLE_DIM is now only for the case where no
   menu is in front of the title -- a playback, or a ::World driven directly --
   because in the running game the menu's own wash is the dim. See
   ::scene_draw_title.
   The three offsets are measured DOWN from ::menu_title_y, which is where
   menu.c puts the current screen's header and which the title screen widens for
   exactly this block.
   타이틀 화면 자신의 머리글입니다. TITLE_DIM은 이제 타이틀 앞에 메뉴가 없는 경우만을 위한
   것입니다. 재생이거나 ::World를 직접 구동하는 경우입니다. 실행 중인 게임에서는 메뉴 자신의
   워시가 곧 그 어둡게 하기입니다. ::scene_draw_title을 참조하십시오.
   세 오프셋은 ::menu_title_y에서 *아래로* 잰 값입니다. 그곳이 menu.c가 현재 화면의 머리글을 두는
   자리이며, 타이틀 화면은 정확히 이 블록을 위해 그 간격을 넓힙니다. */
#define TITLE_DIM       0.70f
#define TITLE_SIZE      9.0f
#define TITLE_SUB_SIZE  1.8f
#define TITLE_BEST_SIZE 1.6f
#define TITLE_SUB_DY    80.0f   /* below the name's top edge */
#define TITLE_BEST_DY  102.0f
/* Seconds the header takes to arrive. The first frame of the process is the
   one the driver spends waking up, and a name that is simply there on it reads
   as a screenshot rather than as a game starting.
   머리글이 도착하는 데 걸리는 초입니다. 프로세스의 첫 프레임은 드라이버가 깨어나는 데 쓰는
   프레임이며, 그 위에 그냥 존재하는 이름은 시작하는 게임이 아니라 스크린숏으로 읽힙니다. */
#define TITLE_FADE      0.9f

/* --- the cutscene / 컷신 --- */

/* Darker than the menu and lighter than nothing. A cutscene is read rather
   than operated, so the room behind it is context rather than a control; the
   menu's own dim is the floor because a screen that stops the game must not be
   easier to see past than a pause.
   메뉴보다 어둡고 완전한 검정보다는 밝습니다. 컷신은 조작하는 것이 아니라 읽는 것이므로 뒤의
   방은 컨트롤이 아니라 맥락입니다. 메뉴의 어둡기가 바닥인 이유는, 게임을 멈추는 화면이 일시정지
   보다 뒤를 보기 쉬워서는 안 되기 때문입니다. */
#define STORY_DIM        0.82f
#define STORY_TEXT_SIZE  2.0f
#define STORY_LINE_STEP  34.0f  /* between two lines of a page */
#define STORY_HINT_SIZE  1.3f
#define STORY_PIP        7.0f
#define STORY_PIP_GAP    5.0f
/* Seconds at each end of a page. ::BETWEEN_FADE's number and its argument: long
   enough that a page arriving does not flicker, short enough that a page spends
   most of its life at full strength -- ::HUD_NOTICE_FADE's rule, that "the
   instant it appears is the instant it is most needed".
   페이지 양 끝의 초입니다. ::BETWEEN_FADE의 숫자이자 그 논거입니다. 도착하는 페이지가 깜빡이지
   않을 만큼 길고, 페이지가 수명의 대부분을 온전한 세기로 보낼 만큼 짧습니다. "나타나는 순간이
   가장 필요한 순간"이라는 ::HUD_NOTICE_FADE의 규칙입니다. */
#define STORY_FADE       0.45f

/* --- ESC menu / ESC 메뉴 --- */

/* Dimmed harder than the win screen. The win screen wants the last frame
   readable underneath -- it is the point of freezing rather than clearing --
   while the menu wants attention on the rows. The world is still visible, so
   the player can see the game is paused rather than gone.
   승리 화면보다 강하게 어둡게 합니다. 승리 화면은 아래의 마지막 프레임이 보이기를
   원하지만(지우지 않고 정지시키는 이유가 그것입니다), 메뉴는 행에 주목하기를 원합니다.
   월드는 여전히 보이므로 플레이어는 게임이 사라진 것이 아니라 멈췄음을 알 수 있습니다. */
#define MENU_DIM         0.72f

/* What a locked row is drawn at. Faint enough that the eye passes over it when
   reading the list, solid enough that it is plainly a row rather than a
   rendering fault -- and the highlight still lands on it, so a player who wants
   to know what it says can point at it and read it at this alpha with the
   cursor bar behind it. See ::menu_row_locked.
   잠긴 행이 그려지는 값입니다. 목록을 읽을 때 눈이 지나칠 만큼 흐리고, 렌더링 결함이 아니라
   분명히 행으로 보일 만큼 진합니다. 그리고 강조는 여전히 그 위에 놓이므로, 무엇이라고 적혀
   있는지 알고 싶은 플레이어는 그것을 가리켜 뒤에 커서 막대를 둔 채 이 알파로 읽을 수 있습니다.
   ::menu_row_locked를 참조하십시오. */
#define MENU_LOCKED_ALPHA 0.35f

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

/* --- the volume sliders ----------------------------------------------------
 * A bar is drawn where those rows' value text would otherwise sit, and the
 * number follows it rather than being replaced by it: the bar says roughly how
 * loud, the number says which notch, and somebody matching BGM against SFX
 * needs the second. See ::menu_row_slider.
 * 막대는 그 행들의 값 텍스트가 놓일 자리에 그려지고, 숫자는 대체되지 않고 그 뒤에 옵니다.
 * 막대는 대략 얼마나 큰지를, 숫자는 어느 눈금인지를 말하며, BGM을 SFX에 맞추려는 사람에게
 * 필요한 것은 후자입니다. ::menu_row_slider를 참조하십시오. */
#define MENU_BAR_GAP    12.0f   ///< @brief Between the bar and its number. / 막대와 숫자 사이 간격.
/* The bar's own rectangle is NOT here. It is a hit target, so menu.c owns it
   and ::menu_row_bar_bounds answers for it -- the first version of these
   sliders kept the width and height in this file and the result was a control
   that looked draggable and could only be cycled, because the hit test had
   nothing to test against.
   막대 자신의 사각형은 이곳에 없습니다. 히트 대상이므로 menu.c가 소유하고
   ::menu_row_bar_bounds가 답합니다. 이 슬라이더의 첫 판은 너비와 높이를 이 파일에 두었고, 그
   결과는 드래그할 수 있어 보이는데 순환밖에 되지 않는 컨트롤이었습니다. 히트 판정에 판정할
   대상이 없었기 때문입니다. */
#define WIN_TITLE_SIZE  7.0f
#define WIN_STAT_SIZE   2.2f
#define WIN_HINT_SIZE   1.4f

/* ---------------------------------------------------------------- lifecycle */


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
    mb_init(&s->shot_buf,   ENEMY_MAX_SHOTS * SHOT_QUADS * 6);
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
    DIAG_WANT_WORLD_PASS();

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
    DIAG_WANT_WORLD_PASS();

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
    DIAG_WANT_WORLD_PASS();

    int pn = pickup_count(pl);
    mb_reset(&s->pickup_buf);

    for (int i = 0; i < pn; i++) {
        const Pickup *p = pickup_at(pl, i);
        if (!p->active) continue;
        float u0, v0, u1, v1;
        pickup_uv(p->kind, &u0, &v0, &u1, &v1);
        mb_billboard_uv(&s->pickup_buf, pickup_centre(p), cam_right, v3f(0,1,0),
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
    DIAG_WANT_WORLD_PASS();

    /* The three tiers, outermost first, in the order the buffer holds them and
       the order they are drawn. One table rather than the arithmetic that used
       to derive "am I a core" from the quad index: that worked for two tiers
       and was already the least readable line in this function, and a third
       tier would have made it a chain of conditionals nobody could check.
       바깥쪽부터 세 겹이며, 버퍼가 담는 순서이자 그려지는 순서입니다. 사각형 인덱스에서
       "내가 코어인가"를 유도하던 산술 대신 표 하나를 씁니다. 그 산술은 두 겹에서는
       동작했지만 이미 이 함수에서 가장 읽기 어려운 줄이었고, 세 번째 겹은 그것을 아무도
       검사할 수 없는 조건문 사슬로 만들었을 것입니다. */
    struct Tier { int n; float size; float r, g, b, a; };
    const struct Tier tier[3] = {
        { SHOT_GLOWS, SHOT_GLOW_SIZE, 0.16f, 0.46f, 0.95f, 0.14f },  /* the emission */
        { SHOT_HALOS, SHOT_HALO_SIZE, 0.10f, 0.42f, 0.85f, 0.30f },  /* halo petals  */
        { SHOT_CORES, SHOT_CORE_SIZE, 0.85f, 0.98f, 1.00f, 0.85f },  /* hot core     */
    };
    const int stride = SHOT_QUADS * 6;
    int sn = enemy_shot_count(pl), live = 0;

    mb_reset(&s->shot_buf);
    for (int i = 0; i < sn; i++) {
        const Shot *sh = enemy_shot_at(pl, i);
        if (!sh->active) continue;

        /* The emission's breath. Off the SAME clock the spin runs on, so a
           volley of bolts fired at different moments swells out of step -- one
           rate shared by every bolt in the air would pulse them in unison,
           which reads as the renderer doing it rather than as the bolts.
           발광의 호흡입니다. 회전이 도는 것과 *같은* 시계를 쓰므로, 서로 다른 순간에
           발사된 볼트들은 어긋나게 부풀어 오릅니다. 공중의 모든 볼트가 하나의 속도를
           공유하면 한목소리로 맥동하는데, 그것은 볼트가 그러는 것이 아니라 렌더러가
           그러는 것으로 읽힙니다. */
        float breath = 1.0f + SHOT_GLOW_PULSE * sinf(sh->life * SHOT_GLOW_RATE);

        for (int t = 0; t < 3; t++) {
            for (int k = 0; k < tier[t].n; k++) {
                float size = tier[t].size * (t == 0 ? breath : 1.0f);
                /* Spread the quads over a QUARTER turn, not a half: a square
                   maps onto itself every 90 degrees, so two quads 180/2 apart
                   are the same square drawn twice -- which is why the core
                   first came out as a plain diamond.
                   사각형을 반 바퀴가 아니라 *4분의 1* 바퀴에 걸쳐 배치합니다. 정사각형은
                   90도마다 자기 자신과 겹치므로, 180/2도 떨어진 두 사각형은 같은 사각형을
                   두 번 그린 것입니다. 중심부가 처음에 평범한 마름모로 나온 이유입니다. */
                float a = sh->life * SHOT_SPIN
                        + k * (M_PI_F * 0.5f / tier[t].n);
                v3 r = v3add(v3scale(cam_right,  cosf(a)),
                             v3scale(cam_up,     sinf(a)));
                v3 u = v3add(v3scale(cam_right, -sinf(a)),
                             v3scale(cam_up,     cosf(a)));
                mb_billboard(&s->shot_buf, sh->pos, r, u, size, size);
            }
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
    for (int i = 0; i < live; i++) {
        for (int t = 0, first = 0; t < 3; first += tier[t].n, t++) {
            rd_color(tier[t].r, tier[t].g, tier[t].b, tier[t].a);
            glDrawArrays(GL_TRIANGLES, i * stride + first * 6, tier[t].n * 6);
        }
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
    DIAG_WANT_UI_PASS();

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

void scene_draw_win(Scene *s, int vw, int vh, const Player *p, const Weapon *w,
                    const char *run) {
    DIAG_WANT_UI_PASS();

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

    /* And what the run came to, on its own line. The belt above is a snapshot
       of the last moment; this is the whole of it, and the win screen has to
       report the same two numbers the death screen does or the game keeps
       score only when you lose.
       그리고 그 플레이가 무엇이 되었는지를 자기 줄에 씁니다. 위의 탄약대는 마지막 순간의
       스냅숏이고 이것은 전체입니다. 승리 화면은 사망 화면과 같은 두 숫자를 보고해야 하며,
       그러지 않으면 게임은 질 때만 점수를 매기는 셈입니다. */
    float rw = font_width(WIN_STAT_SIZE, run);
    text_run(s, (vw - rw) * 0.5f, vh * 0.5f + 28.0f, WIN_STAT_SIZE, run,
             0.95f, 0.85f, 0.55f, 1.0f);

    const char *hint = "ESC for menu";
    float hw = font_width(WIN_HINT_SIZE, hint);
    text_run(s, (vw - hw) * 0.5f, vh * 0.5f + 56.0f, WIN_HINT_SIZE, hint,
             0.55f, 0.55f, 0.58f, 1.0f);

    ui_end();
}

void scene_draw_between(Scene *s, int vw, int vh, const char *cleared,
                        const char *entering, float t, float total) {
    DIAG_WANT_UI_PASS();

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

void scene_draw_death(Scene *s, int vw, int vh, float since, int ready,
                      const char *run) {
    DIAG_WANT_UI_PASS();

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

    /* WHAT THE RUN CAME TO. "YOU DIED" is the only thing this screen used to
       say, and it is the one fact the player already watched happen. How many
       they took down and how long they lasted are the two they cannot see from
       inside the run -- and in a game about staying up as long as possible,
       they are the result. Without them a death is a stop rather than a score,
       and there is nothing to beat next time.

       Faded on `k` with the title rather than shown on `ready` with the prompt:
       it is part of the same sentence the title is, not an instruction.

       그 플레이가 무엇이 되었는가. 이 화면이 말하던 것은 "YOU DIED"뿐이었고, 그것은 플레이어가
       이미 직접 본 유일한 사실입니다. 몇을 쓰러뜨렸는지와 얼마나 버텼는지는 플레이 안에서는
       볼 수 없는 두 가지이며, 가능한 한 오래 살아남는 게임에서 그것이 곧 결과입니다. 그것이
       없으면 죽음은 성적이 아니라 정지이고, 다음번에 넘어설 대상이 없습니다.

       안내 문구와 함께 `ready`에 나타나지 않고 제목과 함께 `k`로 페이드하는 이유는, 지시가
       아니라 제목과 같은 문장의 일부이기 때문입니다. */
    float rw = font_width(DEATH_STAT_SIZE, run);
    text_run(s, (vw - rw) * 0.5f, vh * 0.5f + 14.0f, DEATH_STAT_SIZE, run,
             0.92f, 0.78f, 0.72f, k);

    /* The prompt appears only once the input it describes is actually live.
       Showing it during the grace period would be the screen lying about what
       a press would do.
       안내 문구는 그것이 설명하는 입력이 실제로 살아 있을 때만 나타납니다. 유예 시간 동안
       표시하면, 화면이 지금 누르면 무슨 일이 일어나는지에 대해 거짓말을 하는 셈입니다. */
    if (ready) {
        const char *hint = "press any key to try again";
        float hw = font_width(DEATH_HINT_SIZE, hint);
        text_run(s, (vw - hw) * 0.5f, vh * 0.5f + 44.0f, DEATH_HINT_SIZE, hint,
                 0.72f, 0.62f, 0.62f, 1.0f);
    }

    ui_end();
}

void scene_draw_title(Scene *s, int vw, int vh, float t, int best) {
    DIAG_WANT_UI_PASS();

    ui_begin(vw, vh);

    /* THE WASH IS THE MENU'S WHEN THERE IS A MENU. ::scene_draw_menu has
       already dimmed the world for this screen -- the title IS a menu in the
       running game -- and a second wash would darken the backdrop twice for one
       screen. That is also why this runs after the menu rather than before it;
       see the note in scene.h.
       WHEN THERE IS NOT ONE, this pass owes the dim. A playback comes up on the
       title with no menu in front of it (::menu_open_title is skipped for a
       replay), and so does any caller driving a ::World directly. Undimmed, the
       name would sit on a fully lit room and read as a HUD rather than as a
       screen.
       *메뉴가 있을 때 워시는 메뉴의 것입니다.* ::scene_draw_menu가 이 화면을 위해 이미 월드를
       어둡게 했습니다. 실행 중인 게임에서 타이틀은 곧 메뉴입니다. 워시가 둘이면 화면 하나 때문에
       배경이 두 번 어두워집니다. 이것이 메뉴보다 앞이 아니라 뒤에서 도는 이유이기도 합니다.
       scene.h의 참고 사항을 보십시오.
       *메뉴가 없을 때는* 이 패스가 어둡게 하기를 빚집니다. 재생은 앞에 메뉴 없이 타이틀로
       올라오며(재생에서는 ::menu_open_title을 건너뜁니다), ::World를 직접 구동하는 호출자도
       그렇습니다. 어둡게 하지 않으면 이름이 온전히 밝은 방 위에 놓여 화면이 아니라 HUD로
       읽힙니다. */
    if (!menu_is_open()) full_screen_wash(s, vw, vh, 0.0f, 0.0f, 0.0f, TITLE_DIM);

    rd_mode(RD_TEXT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_texture());

    /* Arrives rather than appears. Clamped both ways because `t` is a clock the
       caller owns and a negative one would make the first frame brighter than
       full.
       나타나는 것이 아니라 *도착합니다*. 양쪽으로 자르는 이유는 `t`가 호출자가 소유한 시계이고,
       음수인 시계는 첫 프레임을 최대보다 밝게 만들기 때문입니다. */
    float k = t / TITLE_FADE;
    if (k > 1.0f) k = 1.0f;
    if (k < 0.0f) k = 0.0f;

    /* From menu.c, so the header and the rows cannot disagree about where the
       header ends. See ::menu_title_y.
       menu.c에서 가져옵니다. 그래야 머리글과 행들이 머리글이 어디서 끝나는지에 대해 어긋날 수
       없습니다. ::menu_title_y를 참조하십시오. */
    float top = menu_title_y(vw, vh);
    float cx  = vw * 0.5f;

    const char *title = "SFPS";
    float tw = font_width(TITLE_SIZE, title);
    text_run(s, cx - tw * 0.5f, top, TITLE_SIZE, title,
             1.0f, 0.82f, 0.28f, k);

    const char *sub = "a shooter that fits on a floppy disk";
    float sw = font_width(TITLE_SUB_SIZE, sub);
    text_run(s, cx - sw * 0.5f, top + TITLE_SUB_DY, TITLE_SUB_SIZE, sub,
             0.66f, 0.64f, 0.60f, k);

    /* THE ONE PLACE A SAVED NUMBER IS EVER SEEN. Half of what save.c keeps is
       this figure, and a number that is written and never shown is a number
       nobody can tell is wrong. Omitted rather than shown as zero before any
       arena has been played, for ::run_summary's reason about `wave 0`: a
       screen printing a field it has no fact for is a screen inventing one.
       *저장된 숫자가 보이는 유일한 곳입니다.* save.c가 지키는 것의 절반이 이 값이며, 쓰이기만
       하고 보이지 않는 숫자는 아무도 틀렸다고 말할 수 없는 숫자입니다. 아레나를 하기 전에는 0으로
       보여 주지 않고 생략합니다. `wave 0`에 대한 ::run_summary의 이유와 같습니다. 사실이 없는
       필드를 찍는 화면은 사실을 지어내는 화면입니다. */
    if (best > 0) {
        char line[32];
        int n = txt_append_str(line, (int)sizeof(line), 0, "BEST WAVE ");
        txt_append_int(line, (int)sizeof(line), n, best);

        float bw = font_width(TITLE_BEST_SIZE, line);
        text_run(s, cx - bw * 0.5f, top + TITLE_BEST_DY, TITLE_BEST_SIZE, line,
                 0.86f, 0.72f, 0.34f, k);
    }

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

            /* --- the second work, and why it is written out in full ------
               The two licences are the same 3-clause BSD text and differ in
               exactly three places: the year, the project named in the
               copyright line, and the project named in the third condition.
               Sharing one copy and naming both projects would be shorter and
               would not be either licence -- each requires ITS OWN copyright
               notice and ITS OWN "neither the name of" clause to be
               reproduced, and a merged clause 3 is a clause neither grantor
               wrote. bake.ps1 checks both spans verbatim for that reason, so
               the shorter version does not compile.
               두 라이선스는 같은 3조항 BSD 텍스트이며 정확히 세 곳에서 다릅니다. 연도,
               저작권 줄이 지목하는 프로젝트, 그리고 세 번째 조항이 지목하는 프로젝트입니다.
               한 벌을 공유하고 두 프로젝트를 함께 적는 것은 더 짧고, 그리고 그것은 두
               라이선스 중 어느 것도 아닙니다. 각각은 *자기* 저작권 고지와 *자기* "neither
               the name of" 조항이 실릴 것을 요구하며, 합쳐진 3항은 어느 허락자도 쓰지 않은
               조항입니다. bake.ps1이 그 이유로 두 구간을 각각 전문 대조하므로, 짧은 판은
               빌드되지 않습니다. */
            "",
            "The lqdm11 and lqdm13 arenas from LibreQuake,",
            "converted by assets/maps/import-librequake.py.",
            "Copyright (c) 2019-2023 Contributors to",
            "the LibreQuake project. All rights reserved.",
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
            "* Neither the name of the LibreQuake project",
            "  nor the names of its contributors may be",
            "  used to endorse or promote products derived",
            "  from this software without specific prior",
            "  written permission.",
            "",
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
            "Map by ZungryWare.  librequake.queer.sh",
            "Full licence: docs/LICENSE-LibreQuake.txt",
        };
        /* HOW TALL A COLUMN IS, and the count of them follows from it. This
           was ::NOTICE_SPLIT -- one index, naming where the single break fell
           -- which held exactly while there was one licence and one break. A
           second work made it a list of indices to keep in step with the
           array, and a height keeps none: the columns are `n` divided by this,
           and adding, removing or re-wrapping a line moves the break by
           itself.
           단 하나의 높이이며, 단의 개수는 그것에서 따라 나옵니다. 이것은 ::NOTICE_SPLIT,
           즉 유일한 분기점이 어디인지를 지목하는 인덱스 하나였고, 라이선스가 하나이고
           분기가 하나인 동안에만 성립했습니다. 두 번째 저작물은 그것을 배열과 보조를 맞춰야
           하는 인덱스 *목록*으로 만들었을 것이고, 높이는 아무것도 맞출 필요가 없습니다. 단의
           개수는 `n`을 이 값으로 나눈 것이고, 줄을 더하거나 빼거나 다시 줄바꿈하면 분기점이
           스스로 움직입니다. */
        static const int NOTICE_ROWS = 25;

        /* Where each work's attribution begins. Two numbers rather than a
           rule that inspects the text, because "the three lines that name a
           project" is not something a line can be asked about -- the same
           argument ::NOTICE_ROWS is not derived from the words either.
           각 저작물의 귀속 표시가 시작되는 곳입니다. 텍스트를 들여다보는 규칙이 아니라 숫자
           둘인 이유는, "프로젝트를 지목하는 세 줄"이 한 줄에게 물어볼 수 있는 것이 아니기
           때문입니다. ::NOTICE_ROWS도 단어에서 유도하지 않는 것과 같은 논거입니다. */
        static const int NOTICE_LEAD[] = { 0, 48 };

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
        int cols = (n + NOTICE_ROWS - 1) / NOTICE_ROWS;
        if (cols < 1) cols = 1;

        /* SHRUNK TO FIT RATHER THAN ALLOWED TO RUN OFF THE EDGES. Two columns
           of licence fitted the narrowest window this menu is usable in with
           nothing to spare; four do not. A notice whose outer columns are past
           the viewport is not a notice that was reproduced -- it is one the
           player cannot read, which is the only thing the obligation is about.
           Linear because font_width is: the whole block is measured at size 1
           above and the size is the ratio that makes it fit.
           가장자리 밖으로 나가게 두지 않고 *줄여서 맞춥니다.* 라이선스 두 단은 이 메뉴를 쓸 수
           있는 가장 좁은 창에 여유 없이 겨우 들어갔고, 네 단은 들어가지 않습니다. 바깥 단이
           뷰포트를 벗어난 고지는 실린 고지가 아니라 플레이어가 읽을 수 없는 고지이며, 그
           의무가 말하는 것은 오직 그것뿐입니다.
           선형인 이유는 font_width가 선형이기 때문입니다. 위에서 블록 전체를 크기 1로 재었고,
           크기는 그것을 들어가게 만드는 비율입니다. */
        float span = cols * wmax + (cols - 1) * gutter;
        float room = (float)vw - 32.0f;
        float size = (span > room && span > 0.0f) ? room / span : 1.0f;

        float cw   = wmax * size;
        float gap  = gutter * size;
        float step = 11.0f * size;
        float x0   = cx - (cols * cw + (cols - 1) * gap) * 0.5f;

        for (int i = 0; i < n; i++) {
            int col = i / NOTICE_ROWS;
            float x = x0 + col * (cw + gap);
            float y = ny + (i - col * NOTICE_ROWS) * step;

            /* Brighter for the attribution, dimmer for the licence body: the
               notice must be present and legible, not shouted. Each work gets
               its own three bright lines, because each has its own name to say.
               귀속 표시는 밝게, 라이선스 본문은 흐리게 합니다. 고지는 존재하고 읽을 수
               있어야 하지 소리쳐야 하는 것은 아닙니다. 저작물마다 자기 밝은 세 줄을 가지며,
               각각이 말할 자기 이름을 가지고 있기 때문입니다. */
            int lead = 0;
            for (int k = 0; k < (int)(sizeof(NOTICE_LEAD) / sizeof(NOTICE_LEAD[0])); k++)
                if (i >= NOTICE_LEAD[k] && i <= NOTICE_LEAD[k] + 3) lead = 1;

            float t = lead ? 0.84f : 0.56f;
            text_run(s, x, y, size, NOTICE[i], t, t * 0.98f, t * 0.92f, 1.0f);
        }
    }
}

/**
 * @brief Draws the menu's rows, with the highlighted one filled.
 * / 메뉴의 행들을 그리며, 선택된 행은 채워진 막대를 함께 그립니다.
 */
/* One flat quad in HUD space. The highlight bar and both halves of a slider
   are the same four steps -- reset, billboard, upload, draw -- and writing them
   out three times is how the third one ends up with a stale colour.
   HUD 공간의 평면 사각형 하나입니다. 강조 막대와 슬라이더의 양쪽 절반이 모두 같은 네 단계
   (초기화, 빌보드, 업로드, 그리기)이며, 그것을 세 번 적어 두는 것이 세 번째가 낡은 색을 갖게
   되는 방식입니다. */
static void hud_quad(Scene *s, float x0, float y0, float x1, float y1,
                     float r, float g, float b, float a) {
    mb_reset(&s->hud_buf);
    mb_billboard(&s->hud_buf, v3f((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0),
                 v3f(1,0,0), v3f(0,1,0), x1 - x0, y1 - y0);
    mesh_upload(&s->hud_mesh, &s->hud_buf, 1);
    rd_color(r, g, b, a);
    mesh_draw(&s->hud_mesh);
}

void scene_draw_story(Scene *s, int vw, int vh, const StoryPage *page,
                      int index, int count, float alpha) {
    DIAG_WANT_UI_PASS();

    if (!page || alpha <= 0.0f) return;
    if (alpha > 1.0f) alpha = 1.0f;

    ui_begin(vw, vh);

    /* The wash fades with the text rather than landing whole. A page that
       arrived over an already-black screen would give the player no moment in
       which the room and the sentence about it are both visible, and that
       moment is what a cutscene over a live level is for.
       워시가 통째로 내려앉지 않고 텍스트와 함께 페이드합니다. 이미 검은 화면 위에 도착하는
       페이지는 방과 그것에 대한 문장이 함께 보이는 순간을 플레이어에게 주지 않으며, 살아 있는
       레벨 위의 컷신이 존재하는 이유가 그 순간입니다. */
    full_screen_wash(s, vw, vh, 0.0f, 0.0f, 0.0f, STORY_DIM * alpha);

    /* --- every quad first, then ONE restore -----------------------------
       ::hud_quad sets neither the render mode nor the texture and restores
       neither; ::draw_menu_rows names this as "the easiest thing in this loop
       to get wrong and there should only be one of it".
       사각형을 전부 먼저, 그다음 복원을 *한 번*입니다. ::hud_quad는 렌더 모드도 텍스처도
       세우지 않고 되돌리지도 않습니다. ::draw_menu_rows가 이것을 "이 루프에서 가장 틀리기
       쉬운 것이고 그것은 하나만 있어야 한다"고 이름 붙였습니다. */
    float cx = vw * 0.5f;
    float cy = vh * 0.5f;

    /* The block is centred on the viewport rather than hung from the top, so a
       one-line page and a four-line page are read from the same place. Half a
       line step up per line past the first.
       블록은 위에서 매다는 것이 아니라 뷰포트 중앙에 놓이므로, 한 줄짜리 페이지와 네 줄짜리
       페이지를 같은 자리에서 읽습니다. 첫 줄을 넘는 줄마다 반 칸씩 위로 올라갑니다. */
    float y0 = cy - (float)(page->n_lines - 1) * STORY_LINE_STEP * 0.5f
                  - FONT_CH * STORY_TEXT_SIZE * 0.5f;

    if (count > 1) {
        rd_mode(RD_FLAT);

        /* Pips under the text, one per page, filled up to and including the
           one showing. The spent ones are tracks rather than absences, the way
           the ward pips and the volume sliders are: "drawing the whole track
           means a slider at zero is still a slider".
           텍스트 아래의 핍이며 페이지마다 하나입니다. 지금 보이는 것까지 채웁니다. 지나간
           것들은 없음이 아니라 트랙이며, 결계핵 핍과 음량 슬라이더가 그러한 것과 같습니다.
           *"트랙 전체를 그리면 0인 슬라이더도 여전히 슬라이더입니다."* */
        float span = (float)count * STORY_PIP + (float)(count - 1) * STORY_PIP_GAP;
        float px   = cx - span * 0.5f;
        float py   = cy + (float)page->n_lines * STORY_LINE_STEP * 0.5f + 26.0f;

        for (int i = 0; i < count; i++) {
            float x = px + (float)i * (STORY_PIP + STORY_PIP_GAP);
            hud_quad(s, x, py, x + STORY_PIP, py + STORY_PIP,
                     0.30f, 0.27f, 0.22f, 0.55f * alpha);
            if (i <= index)
                hud_quad(s, x + 1.0f, py + 1.0f, x + STORY_PIP - 1.0f,
                         py + STORY_PIP - 1.0f, 0.94f, 0.84f, 0.46f, 0.90f * alpha);
        }
    }

    rd_mode(RD_TEXT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_texture());

    for (int i = 0; i < page->n_lines; i++) {
        const char *line = page->line[i];
        float lw = font_width(STORY_TEXT_SIZE, line);
        text_run(s, cx - lw * 0.5f, y0 + (float)i * STORY_LINE_STEP,
                 STORY_TEXT_SIZE, line, 0.94f, 0.90f, 0.82f, alpha);
    }

    /* Not on the first page: a screen that offers a way out before it has said
       anything is a screen that expects to be skipped.
       첫 페이지에는 두지 않습니다. 아무 말도 하기 전에 나갈 길을 제시하는 화면은 건너뛰어질
       것을 예상하는 화면입니다. */
    if (index > 0) {
        const char *hint = "press to continue";
        float hw = font_width(STORY_HINT_SIZE, hint);
        text_run(s, cx - hw * 0.5f, (float)vh - 42.0f, STORY_HINT_SIZE, hint,
                 0.52f, 0.50f, 0.46f, alpha);
    }

    ui_end();
}

/* --- the boss readout / 보스 계기판 ---------------------------------------
 *
 * ENGLISH
 * -------
 * A SIBLING OF ::scene_draw_hud RATHER THAN A BRANCH INSIDE IT, and it takes
 * finished facts rather than a ::World. That is scene.h's standing rule --
 * ::scene_draw_between "takes the two names rather than the World, so it cannot
 * accidentally read the level that is CURRENTLY loaded", and ::scene_draw_win
 * takes an already-worded string because "deciding which facts a run reports is
 * not a layout decision". Which slot the boss is in, what fraction it has left
 * and which line is up are all world.c's questions.
 *
 * WHY THE BAR IS TWO COLOURS. During the warded phase -- most of the fight --
 * the boss takes no damage, so the fill does not move. A bar that simply sat
 * there while the player emptied a magazine into the maw would read as broken,
 * not as invulnerable. Dimming it is the bar saying WHY it is not moving, and
 * it costs one ternary; ::draw_menu_rows already does exactly this to
 * distinguish an on row from an off one.
 *
 * WHY THE PIPS. R8 asks for a health bar and R6 makes the ward count the only
 * gate on progress -- so for most of the fight the top of the screen would show
 * a frozen bar and nothing about the objective. The pips are the objective. The
 * empty ones are drawn as tracks rather than omitted, for the slider's own
 * reason: "Drawing the whole track means a slider at zero is still a slider."
 *
 * 한국어
 * ------
 * ::scene_draw_hud 안의 분기가 아니라 *형제*이며, ::World가 아니라 완성된 사실을 받습니다.
 * scene.h의 정해진 규칙입니다. ::scene_draw_between은 *"World가 아니라 두 이름을 받으므로,
 * 지금 로드된 레벨을 실수로 읽을 수 없습니다"*이고, ::scene_draw_win이 이미 문장이 된 문자열을
 * 받는 이유는 *"어떤 사실을 보고할지 정하는 것은 배치 결정이 아니"*기 때문입니다. 보스가 어느
 * 슬롯에 있는지, 얼마가 남았는지, 어느 대사가 떠 있는지는 전부 world.c의 질문입니다.
 *
 * *바가 두 색인 이유.* 수호 단계 동안(전투의 대부분입니다) 보스는 피해를 받지 않으므로 채움이
 * 움직이지 않습니다. 플레이어가 아귀에 탄창을 비우는 동안 그냥 가만히 있는 바는 무적이 아니라
 * 고장으로 읽힙니다. 흐리게 하는 것이 바가 스스로 왜 움직이지 않는지 말하는 방법이고, 비용은
 * 삼항 하나입니다. ::draw_menu_rows가 켜진 행과 꺼진 행을 구별하려고 이미 정확히 이것을 합니다.
 *
 * *핍이 있는 이유.* 요구는 체력바를 청하는데 진척의 유일한 관문은 결계핵 수입니다. 그래서
 * 전투의 대부분 동안 화면 위쪽은 얼어붙은 바와, 목표에 대해서는 아무것도 보여 주지 않게 됩니다.
 * 핍이 곧 목표입니다. 빈 것도 생략하지 않고 트랙으로 그리며, 슬라이더 자신의 이유를 따릅니다.
 * *"트랙 전체를 그리면 0인 슬라이더도 여전히 슬라이더입니다."* */

#define BOSS_BAR_TOP    22.0f   /**< Down from the top edge, pixels. / 위쪽 가장자리에서 내려온 거리(픽셀). */
#define BOSS_BAR_W      0.54f   /**< Fraction of the viewport width. / 뷰포트 너비의 비율. */
#define BOSS_BAR_H      11.0f   /**< Bar height, pixels. / 바의 높이(픽셀). */
#define BOSS_PIP        9.0f    /**< Ward pip size, pixels. / 결계핵 핍의 크기(픽셀). */
#define BOSS_PIP_GAP    4.0f    /**< Between two pips. / 핍 사이의 간격. */
#define BOSS_LINE_Y     46.0f   /**< The banner, below the bar. / 배너. 바 아래입니다. */

/* What the five moments say.
 *
 * A TABLE BESIDE THE DRAWING, which is ::draw_menu_notices' own arrangement and
 * for its reason: font_text draws a line at a time, so lines live as lines
 * rather than as one string with newlines in it.
 *
 * ENGLISH ONLY, and that is a constraint rather than a choice. font.c's atlas
 * is `FIRST 32` through `COUNT 96` -- printable ASCII and nothing else -- so
 * there is no Hangul glyph to draw with. Every string this game puts on screen
 * is in the same position. Korean story text needs a font change, which is a
 * larger piece of work than the fight these lines belong to.
 *
 * Kept short for a second reason: each is centred at ::HUD_NOTICE_SIZE, and a
 * line long enough to reach the viewport edges at 2.2 would have to shrink,
 * which would make the loudest sentence in the fight the smallest one.
 *
 * 다섯 순간이 하는 말입니다.
 *
 * *그리기 곁의 표*이며, ::draw_menu_notices 자신의 배치이자 그 이유를 따릅니다. font_text는 한
 * 번에 한 줄을 그리므로, 줄은 개행이 든 문자열 하나가 아니라 줄로 존재합니다.
 *
 * *영어 전용이며, 선택이 아니라 제약입니다.* font.c의 아틀라스는 `FIRST 32`부터 `COUNT 96`까지,
 * 출력 가능한 ASCII뿐입니다. 그릴 한글 글리프가 없습니다. 이 게임이 화면에 올리는 모든 문자열이
 * 같은 처지입니다. 한글 스토리 텍스트는 폰트 작업을 필요로 하며, 그것은 이 대사들이 속한
 * 전투보다 큰 작업입니다.
 *
 * 짧게 유지하는 두 번째 이유는, 각각이 ::HUD_NOTICE_SIZE로 가운데 정렬되기 때문입니다. 2.2에서
 * 뷰포트 가장자리에 닿을 만큼 긴 줄은 줄여야 하고, 그러면 전투에서 가장 큰 목소리의 문장이 가장
 * 작은 문장이 됩니다. */
static const char *boss_line_text(int line) {
    switch (line) {
    case BOSS_LINE_WAKE: return "THE SPIRE OPENS ITS MOUTH";
    case BOSS_LINE_OPEN: return "THE WARDS ARE DOWN -- HURT IT";
    case BOSS_LINE_HIT:  return "IT FEELS THAT";
    case BOSS_LINE_WARD: return "IT PULLS THE WARDS BACK UP";
    case BOSS_LINE_DIE:  return "THE MAW IS QUIET";
    default:             return 0;
    }
}

void scene_draw_boss(Scene *s, int vw, int vh, int show_bar, float fill,
                     int wards_left, int wards_total, int groggy,
                     const char *line, float line_alpha) {
    DIAG_WANT_UI_PASS();

    ui_begin(vw, vh);

    /* CLAMPED, not trusted. The caller divides a health by a table value, and
       the day anything scales a spawned monster's health that ratio goes over
       one with nothing else to catch it -- a fill wider than its own track
       reads as a rendering fault rather than as a buff.
       신뢰하지 않고 자릅니다. 호출자는 체력을 표의 값으로 나누는데, 무언가가 생성된 몬스터의
       체력을 조정하는 날 그 비율은 1을 넘고 그것을 잡을 다른 것이 없습니다. 자기 트랙보다 넓은
       채움은 강화가 아니라 렌더링 결함으로 읽힙니다. */
    if (!(fill > 0.0f)) fill = 0.0f;
    if (fill > 1.0f)    fill = 1.0f;

    float w  = (float)vw * BOSS_BAR_W;
    float x0 = ((float)vw - w) * 0.5f;
    float x1 = x0 + w;
    float y0 = BOSS_BAR_TOP;
    float y1 = y0 + BOSS_BAR_H;

    /* --- every quad first, then ONE restore -----------------------------
       ::hud_quad sets neither the render mode nor the texture and restores
       neither. ::draw_menu_rows names this as "the easiest thing in this loop
       to get wrong and there should only be one of it", so the text below is
       preceded by exactly one restore rather than by a restore per group.
       사각형을 전부 먼저, 그다음 복원을 *한 번*입니다. ::hud_quad는 렌더 모드도 텍스처도
       세우지 않고 되돌리지도 않습니다. ::draw_menu_rows가 이것을 "이 루프에서 가장 틀리기
       쉬운 것이고 그것은 하나만 있어야 한다"고 이름 붙였으므로, 아래의 글씨 앞에는 무리마다의
       복원이 아니라 정확히 하나의 복원만 옵니다. */
    /* THE BAR AND THE BANNER HAVE DIFFERENT LIFETIMES, which is why one of
       them is behind a flag. The bar exists exactly while the maw does. The
       last line it speaks is posted on the frame it dies, and there is no maw
       by then -- so a banner tied to the bar's condition would be a sentence
       written and never shown, which is indistinguishable from a sentence
       nobody wrote.
       바와 배너는 수명이 다르며, 그것이 둘 중 하나가 플래그 뒤에 있는 이유입니다. 바는 정확히
       아귀가 존재하는 동안 존재합니다. 아귀가 말하는 마지막 대사는 그것이 죽는 프레임에
       게시되고 그때는 이미 아귀가 없습니다. 바의 조건에 묶인 배너는 쓰였지만 결코 보이지 않는
       문장이 되고, 그것은 아무도 쓰지 않은 문장과 구별되지 않습니다. */
    if (show_bar) {
        rd_mode(RD_FLAT);

        hud_quad(s, x0 - 2.0f, y0 - 2.0f, x1 + 2.0f, y1 + 2.0f,
                 0.04f, 0.02f, 0.03f, 0.72f);                /* the track */

        if (fill > 0.0f)
            hud_quad(s, x0, y0, x0 + w * fill, y1,
                     groggy ? 1.00f : 0.52f,
                     groggy ? 0.28f : 0.20f,
                     groggy ? 0.16f : 0.18f, 0.94f);

        /* The two cycle boundaries, drawn ON the fill so the player can see
           what a groggy window bought and how much of this one is left.
           두 사이클 경계이며 채움 *위에* 그립니다. 플레이어가 그로기 창이 무엇을 벌었는지,
           그리고 이번 창이 얼마나 남았는지 볼 수 있게 합니다. */
        for (int i = 1; i < BOSS_CYCLES; i++) {
            float tx = x0 + w * ((float)i / (float)BOSS_CYCLES);
            hud_quad(s, tx - 1.0f, y0, tx + 1.0f, y1, 0.02f, 0.01f, 0.02f, 0.85f);
        }

        /* Ward pips, right of the bar. Track always, fill only while it
           stands -- "drawing the whole track means a slider at zero is still a
           slider", and a ward that is gone is exactly the thing the player
           needs to be able to count.
           결계핵 핍이며 바 오른쪽입니다. 트랙은 언제나, 채움은 서 있는 동안만입니다.
           "트랙 전체를 그리면 0인 슬라이더도 여전히 슬라이더"이고, 사라진 결계핵이야말로
           플레이어가 셀 수 있어야 하는 것입니다. */
        for (int i = 0; i < wards_total; i++) {
            float px = x1 + 10.0f + (float)i * (BOSS_PIP + BOSS_PIP_GAP);
            float py = y0 + (BOSS_BAR_H - BOSS_PIP) * 0.5f;
            hud_quad(s, px, py, px + BOSS_PIP, py + BOSS_PIP,
                     0.05f, 0.04f, 0.02f, 0.70f);
            if (i < wards_left)
                hud_quad(s, px + 1.5f, py + 1.5f, px + BOSS_PIP - 1.5f,
                         py + BOSS_PIP - 1.5f, 1.00f, 0.86f, 0.34f, 0.95f);
        }
    }

    rd_mode(RD_TEXT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_texture());

    if (line && line[0] && line_alpha > 0.0f) {
        float lw = font_width(HUD_NOTICE_SIZE, line);
        text_run(s, ((float)vw - lw) * 0.5f, BOSS_LINE_Y, HUD_NOTICE_SIZE,
                 line, 1.0f, 0.88f, 0.62f, line_alpha);
    }

    ui_end();
}

static void draw_menu_rows(Scene *s, int vw, int vh, float cx, int rows, int cur) {
    for (int i = 0; i < rows; i++) {
        const char *value;
        const char *label = menu_row_text(i, &value);

        float bx0, by0, bx1, by1;
        if (!menu_row_bounds(i, vw, vh, &bx0, &by0, &bx1, &by1)) continue;

        int on   = (i == cur);
        int lock = menu_row_locked(i);

        /* HOW A LOCKED ROW LOOKS, and it is one multiplier rather than a
           second set of colours. The row keeps its shape, its place and its
           highlight -- the cursor still lands on it, deliberately, see
           ::menu_row_locked -- and only goes faint, which is the difference
           between "not yours yet" and "broken". A separate palette would let
           the two drift into looking like different kinds of row, and there is
           only one kind here.
           NOT SKIPPED AND NOT HIDDEN. A row nobody can see is a mode nobody
           knows to want, and the whole reason endless is worth unlocking is
           that the player has been looking at it since the first launch.
           *잠긴 행이 어떻게 보이는가*이며, 두 번째 색 조합이 아니라 곱하는 값 하나입니다.
           행은 모양과 자리와 강조를 그대로 유지하고(커서는 의도적으로 여전히 그 위에
           놓입니다. ::menu_row_locked를 보십시오) 흐려지기만 합니다. 그것이 "아직 당신의 것이
           아니다"와 "고장 났다"의 차이입니다. 별도의 팔레트를 두면 둘이 서로 다른 종류의 행처럼
           보이는 쪽으로 어긋날 수 있는데, 이곳에 종류는 하나뿐입니다.
           *건너뛰지도 숨기지도 않습니다.* 아무도 볼 수 없는 행은 아무도 원할 줄 모르는 모드이며,
           무한 모드가 해금할 가치가 있는 이유 전부는 플레이어가 첫 실행 이후로 줄곧 그것을 보고
           있었다는 데 있습니다. */
        float a = lock ? MENU_LOCKED_ALPHA : 1.0f;

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
        /* Every quad this row wants, drawn together. RD_FLAT and the font
           texture are swapped once for the group rather than once per quad,
           because the restore below is the easiest thing in this loop to get
           wrong and there should only be one of it.
           이 행이 원하는 모든 사각형을 함께 그립니다. RD_FLAT과 폰트 텍스처를 사각형마다가
           아니라 그룹마다 한 번 교체합니다. 아래의 복원이 이 루프에서 가장 틀리기 쉬운
           것이고, 그것은 하나만 있어야 하기 때문입니다. */
        float fill = 0.0f;
        int   slider = menu_row_slider(i, &fill);

        if (on || slider) {
            rd_mode(RD_FLAT);

            if (on)
                hud_quad(s, bx0, by0, bx1, by1, 1.0f, 0.85f, 0.30f, 0.16f * a);

            if (slider) {
                float x0, y0, x1, y1;
                menu_row_bar_bounds(i, vw, vh, &x0, &y0, &x1, &y1);

                /* The empty track first, then the filled part over it. Drawing
                   the whole track means a slider at zero is still a slider --
                   an empty row would read as a setting that is missing rather
                   than as one turned down.
                   빈 트랙을 먼저, 그 위에 채워진 부분을 그립니다. 트랙 전체를 그리면 0인
                   슬라이더도 여전히 슬라이더입니다. 빈 행은 꺼진 설정이 아니라 *없는*
                   설정으로 읽힙니다. */
                hud_quad(s, x0, y0, x1, y1,
                         0.30f, 0.30f, 0.34f, 0.55f);

                if (fill > 0.0f)
                    hud_quad(s, x0, y0, x0 + (x1 - x0) * fill, y1,
                             on ? 1.00f : 0.66f,
                             on ? 0.85f : 0.62f,
                             on ? 0.32f : 0.40f, 0.92f);
            }

            /* Back to text mode and the font: the quads above swapped both. */
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
            text_run(s, bx0 + 8.0f, y, MENU_ROW_SIZE, ">", r, g, b, a);

        text_run(s, cx + MENU_LABEL_X, y, MENU_ROW_SIZE, label, r, g, b, a);

        if (value[0]) {
            float vx = cx + MENU_VALUE_X;
            /* Past the bar's real right edge rather than past a width this file
               believes in -- one of them is the truth and it is not this one.
               이 파일이 믿는 너비가 아니라 막대의 실제 오른쪽 끝 너머입니다. 둘 중 하나가
               진실이고 그것은 이쪽이 아닙니다. */
            float sx0, sx1;
            if (slider && menu_row_bar_bounds(i, vw, vh, &sx0, 0, &sx1, 0))
                vx = sx1 + MENU_BAR_GAP;
            text_run(s, vx, y, MENU_ROW_SIZE, value, r, g, b, a);
        }
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
        /* ESC IS NAMED FOR WHAT IT DOES HERE, which is not the same word on
           every screen: it resumes a run, steps back out of settings, and on
           the title screen it does nothing at all -- so the title's hint does
           not mention it. A line that offers a key which is inert is the
           screen teaching the player it is broken.
           *ESC는 이곳에서 하는 일로 이름이 붙으며*, 그것은 화면마다 같은 단어가 아닙니다.
           플레이를 재개하고, 설정에서 물러나며, 타이틀 화면에서는 아무 일도 하지 않습니다.
           그래서 타이틀의 안내는 그것을 언급하지 않습니다. 반응하지 않는 키를 제시하는 줄은
           화면이 플레이어에게 고장 났다고 가르치는 것입니다. */
        const char *hint = (menu_screen() == MENU_SETTINGS)
            ? "CLICK to change   RIGHT-CLICK reverses   W/S A/D   ESC back"
            : (menu_screen() == MENU_TITLE)
            ? "CLICK to choose   W/S select   ENTER"
            : "CLICK to choose   W/S select   ENTER   ESC resume";
        float hw = font_width(MENU_HINT_SIZE, hint);
        text_run(s, cx - hw * 0.5f, menu_hint_y(vw, vh), MENU_HINT_SIZE, hint,
                 0.52f, 0.52f, 0.56f, 1.0f);
    }
}

void scene_draw_menu(Scene *s, int vw, int vh) {
    DIAG_WANT_UI_PASS();

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
    /* THE TITLE SCREEN'S HEADER IS NOT DRAWN HERE. It is the game's name at
       title size with a line under it, it fades in on a clock this pass cannot
       see, and it must not be dimmed by the wash above it -- so it is
       ::scene_draw_title, called after this. The alternative was a fourth
       string in this chain and three arguments added to this function for one
       screen's sake.
       *타이틀 화면의 머리글은 이곳에서 그리지 않습니다.* 그것은 타이틀 크기의 게임 이름과 그
       아래 한 줄이며, 이 패스가 볼 수 없는 시계에 따라 서서히 나타나고, 위의 워시에 의해
       어두워져서는 안 됩니다. 그래서 ::scene_draw_title이며 이 함수 뒤에 불립니다. 대안은 이
       사슬의 네 번째 문자열과, 화면 하나를 위해 이 함수에 추가되는 인자 셋이었습니다. */
    if (menu_screen() != MENU_TITLE) {
        const char *title = (menu_screen() == MENU_SETTINGS) ? "SETTINGS"
                          : (menu_screen() == MENU_CREDITS)  ? "CREDITS"
                          : "PAUSED";
        float tw = font_width(MENU_TITLE_SIZE, title);
        text_run(s, cx - tw * 0.5f, menu_title_y(vw, vh), MENU_TITLE_SIZE, title,
                 1.0f, 0.85f, 0.30f, 1.0f);
    }

    /* Prose rather than rows, and long enough to have its own function. */
    if (menu_screen() == MENU_CREDITS) draw_menu_notices(s, vw, vh, cx, rows);

    draw_menu_rows(s, vw, vh, cx, rows, cur);

    if (menu_screen() != MENU_CREDITS) draw_menu_hint(s, vw, vh, cx);

    ui_end();
}

void scene_draw_proj(Scene *s, const Pools *pl, mat4 vp, v3 cam_right, v3 cam_up) {
    DIAG_WANT_WORLD_PASS();

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
        /* A grid of zero IS the disable, which render.c states of uSnap: "0
           disables the whole thing". Guarded rather than divided by, because
           PSX_SNAP_COARSE is 0 now and the division would be one.
           격자 0이 곧 비활성화이며, render.c가 uSnap에 대해 그렇게 말합니다. 나누지 않고
           분기하는 이유는 PSX_SNAP_COARSE가 이제 0이고 나눗셈이 성립하지 않기 때문입니다. */
        if (PSX_SNAP_COARSE > 0.0f)
            rd_snap((float)sw / PSX_SNAP_COARSE, (float)sh / PSX_SNAP_COARSE);
        else
            rd_snap(0.0f, 0.0f);
    }

    /* Set beside the snap and released beside it, because the two are halves of
       one look: the vertices wobble and the texture between them swims. Turning
       on either alone reads as a fault in the renderer rather than as a period.
       스냅 곁에서 설정하고 곁에서 해제합니다. 둘은 하나의 외형을 이루는 절반들이기
       때문입니다. 정점이 흔들리고 그 사이의 텍스처가 헤엄칩니다. 둘 중 하나만 켜면 시대가
       아니라 렌더러의 결함으로 읽힙니다. */
    rd_affine(PSX_AFFINE);

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

    /* And the UI does not swim either. A glyph atlas is where a UV off by one
       texel fetches the neighbouring letter.
       UI도 헤엄치지 않습니다. 글리프 아틀라스는 UV가 한 텍셀만 어긋나도 옆 글자를 가져오는
       곳입니다. */
    rd_affine(0.0f);

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

    /* --- the boss readout -----------------------------------------------
       AFTER the HUD and BEFORE the end screens, so it is dimmed by them for the
       reason the HUD is: it is a readout belonging to the frozen frame
       underneath, and the wash pushes it back rather than removing it.
       Everything it needs is computed HERE rather than passed as a World, which
       is scene.h's rule for every pass that draws a fact.
       HUD *뒤*, 종료 화면들 *앞*입니다. 그래야 HUD와 같은 이유로 그것들에 의해 어두워집니다.
       아래에 정지된 프레임에 속한 계기판이며, 워시는 그것을 제거하는 대신 뒤로 밀어냅니다.
       필요한 모든 것을 World로 넘기지 않고 *이곳에서* 계산하며, 그것이 사실을 그리는 모든
       패스에 대한 scene.h의 규칙입니다. */
    if (!w->run.title) {
        int bi = enemy_boss_index(&w->pools);

        /* EITHER, not both. The bar needs a boss; the banner outlives one --
           the maw's last line is posted on the frame it dies. Gating the whole
           block on `bi >= 0` is what made that line unshowable.
           둘 중 하나이며 둘 다가 아닙니다. 바에는 보스가 필요하지만 배너는 보스보다 오래
           삽니다. 아귀의 마지막 대사는 그것이 죽는 프레임에 게시됩니다. 블록 전체를
           `bi >= 0`으로 막은 것이 그 대사를 보여 줄 수 없게 만든 원인이었습니다. */
        if (bi >= 0 || w->run.boss_line) {
            const Enemy   *b = bi >= 0 ? enemy_at(&w->pools, bi) : 0;
            const MonType *S = b ? mon_stats(b->type) : 0;
            int wards = enemy_guards_alive(&w->pools);

            /* The banner is suppressed on the end screens while the BAR is not,
               and the two are different things: the bar is a readout of the
               frozen frame, the line is an announcement -- and once the run is
               over there is nothing left to announce. A frozen sentence under a
               death overlay reads as part of the death screen.
               종료 화면에서 배너는 억제하고 *바*는 억제하지 않으며, 둘은 다른 것입니다. 바는
               정지된 프레임의 계기판이고 대사는 공지인데, 플레이가 끝나면 공지할 것이 남아
               있지 않습니다. 사망 오버레이 아래에 얼어붙은 문장은 사망 화면의 일부로 읽힙니다. */
            const char *line = 0;
            float       la   = 0.0f;
            if (!w->run.dead && !w->run.won && w->run.boss_line) {
                line = boss_line_text(w->run.boss_line);
                la   = w->run.boss_line_t / HUD_NOTICE_FADE;
                if (la > 1.0f) la = 1.0f;
            }

            scene_draw_boss(sc, vw, vh, b != 0,
                            b ? (float)b->health / (float)S->hp : 0.0f,
                            wards, BOSS_WARDS, wards == 0, line, la);
        }
    }

    /* The three end/start screens are mutually exclusive by construction:
       `title` is cleared before a run can begin, and a run that has been won
       cannot also have been lost.
       세 개의 시작·종료 화면은 구조적으로 상호 배타적입니다. `title`은 플레이가
       시작되기 전에 해제되며, 승리한 플레이가 동시에 패배할 수는 없습니다. */
    /* Worded once, here, for whichever of the two end screens is up. Built in
       this scope rather than inside each branch because the two screens must
       report the same run the same way -- a second call site is a second place
       the wording can drift, and the cost is one stack buffer on a frame that
       is already drawing text.
       두 종료 화면 중 어느 쪽이 떠 있든, 이곳에서 한 번 문구로 만듭니다. 각 분기 안이 아니라 이
       스코프인 이유는 두 화면이 같은 플레이를 같은 방식으로 보고해야 하기 때문입니다. 두 번째
       호출 지점은 문구가 어긋날 수 있는 두 번째 장소이며, 비용은 이미 텍스트를 그리고 있는
       프레임에서 스택 버퍼 하나입니다. */
    char run_line[RUN_SUMMARY_MAX];
    run_summary(&w->run, run_line, sizeof(run_line));

    /* --- the cutscene, ahead of every end screen -------------------------
       A FOURTH SCREEN AND IT WINS. The three below are mutually exclusive by
       construction; this one is not -- it plays OVER `dead`, and in story mode
       it is what stands between the maw's death and `won`. So it is asked
       first, and when it is up the others are not drawn at all rather than
       drawn under it: two washes and two sets of text would leave the death
       screen's own prompt legible through the words, offering a key that
       ::step_confirm has already given to the cutscene.
       Nothing is drawn on the title screen, because a run that has not begun
       has no moment to hold -- ::STORY_INTRO is started by ::world_begin, which
       is also what clears `title`.
       *네 번째 화면이고 그것이 이깁니다.* 아래의 셋은 구조적으로 상호 배타적이지만 이것은
       아닙니다. `dead` 위에서 재생되며, 스토리 모드에서는 아귀의 죽음과 `won` 사이에 서 있는
       것이 이것입니다. 그래서 먼저 묻고, 이것이 떠 있으면 나머지는 아래에 그려지는 것이 아니라
       아예 그려지지 않습니다. 워시 둘과 텍스트 둘이면 사망 화면 자신의 안내 문구가 글자 사이로
       읽히고, ::step_confirm이 이미 컷신에 준 키를 제시하게 됩니다.
       타이틀 화면에서는 아무것도 그리지 않습니다. 시작되지 않은 플레이에는 붙잡아 둘 순간이
       없기 때문입니다. ::STORY_INTRO는 ::world_begin이 시작하며, `title`을 지우는 것도
       그것입니다. */
    const StoryCut *cut = w->run.cut ? story_for(w->run.cut - 1) : 0;
    if (cut && w->run.cut_page < cut->n_pages) {
        const StoryPage *pg = &cut->page[w->run.cut_page];

        /* In at the start and out at the end, from the one clock world.c
           keeps. ::BETWEEN_FADE's shape: the shorter of the two distances to an
           edge, so a page whose hold is shorter than two fades still reaches
           full strength at its midpoint instead of never arriving.
           한 시계로부터 시작에서 들어오고 끝에서 나갑니다. world.c가 지키는 그 시계입니다.
           ::BETWEEN_FADE의 형태입니다. 가장자리까지의 두 거리 중 짧은 쪽이므로, hold가 페이드
           둘보다 짧은 페이지도 결코 도착하지 못하는 대신 중간 지점에서 온전한 세기에
           닿습니다. */
        float in   = w->run.cut_time;
        float left = pg->hold - w->run.cut_time;
        float a    = (in < left ? in : left) / STORY_FADE;
        if (a > 1.0f) a = 1.0f;
        if (a < 0.0f) a = 0.0f;

        scene_draw_story(sc, vw, vh, pg, w->run.cut_page, cut->n_pages, a);
    }
    else if (w->run.title)
        /* The rows are drawn by ::scene_draw_menu below and the name by
           ::scene_draw_title after it; nothing about the title screen is drawn
           in this chain any more. The branch stays so the three end screens
           cannot be reached while a run has not begun.
           행은 아래의 ::scene_draw_menu가, 이름은 그 뒤의 ::scene_draw_title이 그립니다. 타이틀
           화면에 대한 어떤 것도 더 이상 이 사슬에서 그려지지 않습니다. 이 분기가 남는 이유는
           플레이가 시작되지 않은 동안 세 종료 화면에 도달할 수 없게 하기 위함입니다. */
        ;
    else if (w->run.won)
        scene_draw_win(sc, vw, vh, &w->player, &w->weapon, run_line);
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
                         w->run.death_time > DEATH_INPUT_DELAY, run_line);

    /* Last, so it sits over the HUD and the end screens both. A menu opened
       from the win screen has to be readable too, and it is the thing the
       player is currently operating.
       마지막에 그려 HUD와 종료 화면 양쪽 위에 놓입니다. 승리 화면에서 연 메뉴도 읽을
       수 있어야 하며, 그것이 플레이어가 지금 조작하고 있는 대상입니다. */
    scene_draw_menu(sc, vw, vh);

    /* AFTER THE MENU, and it is the only pass here that is. ::scene_draw_menu
       dims the world for the screen it draws; the title's name is that
       screen's own art rather than something that dim should push back, so it
       goes over the wash instead of under it. See scene.h.
       ON `title` RATHER THAN ON ::MENU_TITLE, which is not the same condition
       and the difference is load-bearing twice over. A playback comes up on the
       title with no menu (::menu_open_title is skipped for one), and so does a
       tool driving a ::World directly -- gated on the menu, both would draw a
       frame with NO UI pass in it at all. That matters beyond a missing title:
       ::ui_end is what restores the depth test and the culling the next frame's
       world pass expects, so a frame that runs no UI pass corrupts the frame
       AFTER it. scenetest found exactly that, as two end screens that stopped
       agreeing with themselves.
       The best wave comes from save.c because it outlives every run that
       produced it -- ::RunState cannot carry it and this pass is the only place
       it is ever shown. Reading a state module for a UI fact is what this
       function already does for ::menu_screen, three lines up.
       *메뉴 뒤이며*, 이곳에서 그런 패스는 이것뿐입니다. ::scene_draw_menu는 자신이 그리는 화면을
       위해 월드를 어둡게 하는데, 타이틀의 이름은 그 어둡게 하기가 뒤로 밀어내야 할 것이 아니라
       그 화면 자신의 아트입니다. 그래서 워시 아래가 아니라 위에 놓입니다. scene.h를 참조하십시오.
       *::MENU_TITLE이 아니라 `title`을 검사하며*, 그것은 같은 조건이 아니고 그 차이는 두 겹으로
       중요합니다. 재생은 메뉴 없이 타이틀로 올라오고(재생에서는 ::menu_open_title을 건너뜁니다)
       ::World를 직접 구동하는 도구도 그렇습니다. 메뉴로 막으면 둘 다 UI 패스가 *하나도* 없는
       프레임을 그리게 됩니다. 그것은 사라진 제목보다 큰 문제입니다. 다음 프레임의 월드 패스가
       기대하는 깊이 검사와 컬링을 복원하는 것이 ::ui_end이므로, UI 패스를 하나도 돌리지 않는
       프레임은 *그 다음* 프레임을 망칩니다. scenetest가 정확히 그것을, 자기 자신과 일치하기를
       그만둔 두 종료 화면으로 찾아냈습니다.
       최고 웨이브가 save.c에서 오는 이유는, 그것이 자신을 만들어 낸 모든 플레이보다 오래
       살아남기 때문입니다. ::RunState는 그것을 나를 수 없고 이 패스가 그것이 보이는 유일한
       곳입니다. UI를 위한 사실을 상태 모듈에서 읽는 것은 이 함수가 세 줄 위에서
       ::menu_screen에 대해 이미 하고 있는 일입니다. */
    if (w->run.title)
        scene_draw_title(sc, vw, vh, w->run.title_time, save_best_wave());
}
