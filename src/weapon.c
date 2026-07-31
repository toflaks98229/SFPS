/**
 * @file weapon.c
 * @brief Implements the hitscan shotgun, the grapple rope, the view model and the effects.
 *
 * ENGLISH
 * -------
 * The file divides into two halves that share only the ::Weapon struct and the
 * habit of pushing the player by writing to a caller-owned velocity:
 *
 *  - The shotgun: a Quake-1-style six-pellet hitscan blast, its recoil kick,
 *    and the impact/tracer/decal effects it leaves behind.
 *  - The grapple: a DOOM Eternal Meat Hook -- fire a projectile, get reeled
 *    to what it hits, damage it on arrival, bounce off automatically. See the
 *    tuning banner in weapon.h for why this is a winch where the rope
 *    constraint it replaced deliberately was not.
 *
 * The wp_hook_* family takes the level explicitly and touches no GL, which is
 * what lets tools/hooktest.c drive all four beats with no context. The
 * rendering half necessarily does touch GL and is not testable that way.
 *
 * 한국어
 * ------
 * 이 파일은 ::Weapon 구조체와, 호출자가 소유한 속도에 값을 써서 플레이어를
 * 밀어내는 방식만을 공유하는 두 부분으로 나뉩니다:
 *
 *  - 샷건: Quake 1 스타일의 6발 산탄 히트스캔 사격과 그 반동, 그리고 그것이
 *    남기는 탄흔/예광탄/자국 효과.
 *  - 그래플: DOOM Eternal의 미트 훅입니다. 발사체를 던지고, 맞은 대상 쪽으로
 *    끌려가고, 도달 시 피해를 입히고, 자동으로 튕겨 나옵니다. 이것이 대체한 로프
 *    구속이 의도적으로 피했던 윈치 방식을 왜 여기서는 채택했는지는 weapon.h의
 *    튜닝 배너를 참조하십시오.
 *
 * wp_hook_* 계열은 레벨을 명시적으로 받고 GL을 사용하지 않으므로,
 * tools/hooktest.c가 컨텍스트 없이 네 단계 전부를 구동할 수 있습니다. 반면
 * 렌더링 부분은 필연적으로 GL을 사용하므로 그런 방식으로는 테스트할 수 없습니다.
 */

#include "weapon.h"
#include "tex.h"
#include "model.h"
#include "audio.h"
#include "level.h"
#include "enemy.h"
#include "player.h"   /* PLAYER_GRAVITY -- the pull cancels it while reeling.
                         Constants only, and only in the .c: weapon.h stays
                         free of player.h so neither header depends on the
                         other. enemy.c and pickup.c borrow constants the same
                         way.
                         PLAYER_GRAVITY 때문입니다. 견인 중에 중력을 상쇄합니다.
                         상수만 사용하며 .c 파일에서만 포함하므로, weapon.h는
                         player.h로부터 자유롭게 유지되어 두 헤더가 서로 의존하지
                         않습니다. enemy.c와 pickup.c도 같은 방식으로 상수를
                         빌려 씁니다. */

/* ------------------------------------------------------------------ tuning */

/* Quake 1 shotgun: a slow, heavy, six-pellet hitscan blast. The pellets are
   what make it a shotgun -- a single ray with a wide spread would just feel
   like an inaccurate rifle. */
#define PELLETS        6
#define PELLET_DAMAGE  7        /* six of these -- one point-blank blast -- kills */
#define FIRE_INTERVAL  0.50f
#define RANGE         120.0f
#define PELLET_SPREAD  0.040f   /* fixed cone, not a growing bloom */

#define RECOIL_KICK    0.055f   /* radians added per shot */
#define RECOIL_RETURN  6.5f     /* springback rate */
#define PUNCH_KICK     0.085f   /* metres the view model recoils */
#define PUNCH_RETURN   7.0f
#define FLASH_TIME     0.075f

#define IMPACT_LIFE    6.0f
#define TRACER_LIFE    0.055f
/* Every trigger pull spawns PELLETS of each, so the ring buffers hold a few
   full blasts rather than a few shots. */
#define MAX_IMPACTS   48
#define MAX_TRACERS   24

/* Gun-local space: +x right, +y up, the barrel points down -z.
   The muzzle now comes from the model (see g_muzzle below), so redrawing the
   gun moves the flash and the tracers with it. */

/* The model is built at life size (~1.3m nose to stock). Drawn at that scale
   30cm from the eye it swallows half the screen, so the view model gets its
   own scale and a narrower FOV than the world camera. */
/* Doom-style: the weapon sits centred and points straight down the camera
   axis, with no base yaw or pitch. An earlier pose yawed it ~20 degrees so
   the flat side of the extrusion would show, which read as a gun held
   crooked. What makes the straight-on view work now is material contrast --
   a blued barrel over a steel receiver over a walnut fore-end separates the
   parts that the silhouette alone no longer can.

   pivot_* puts sway rotation at the stock rather than the gun's origin.
   Pivoting at the origin leaves the stock much nearer the camera than the
   muzzle, and perspective then makes the *stock* whip across the screen while
   the muzzle barely moves -- backwards from how a shouldered weapon reads. */
GunPose g_gun_pose = {
    .scale = 0.45f, .fov = 0.95f,
    .off_x = 0.0f,   .off_y = -0.150f, .off_z = -0.480f,
    .yaw   = 0.0f,   .pitch = 0.0f,
    .pivot_y = -0.030f, .pivot_z = 0.380f
};

/* mouse_dx is in pixels, so the raw value has to be scaled hard and then
   clamped -- a brisk 30px flick used to produce a 170 degree gun rotation. */
#define SWAY_PER_PIXEL 0.0018f
#define SWAY_MAX       0.045f  /* metres of translation */
#define SWAY_ROT       3.0f    /* radians of pitch/yaw per metre of sway */
#define SWAY_ROLL      1.5f

#define SPARK_TIME     0.06f

/* ------------------------------------------------------------ module state */

/* --- File-local types / 파일 지역 타입 --- */

/**
 * @struct Impact
 * @brief One bullet hole or blood splat left on a surface.
 *
 * ENGLISH
 * -------
 * @note `blood` selects the material: a hit on a monster splatters rather
 *       than chipping the wall.
 *
 * 한국어
 * ------
 * 표면에 남은 탄흔 또는 혈흔 하나입니다.
 * @note `blood`가 재질을 결정합니다. 몬스터에 명중하면 벽이 파이는 대신 피가
 *       튑니다.
 */
typedef struct { v3 p, n; float life; int blood; } Impact;

/**
 * @struct Tracer
 * @brief One short-lived line from the muzzle to a pellet's impact point.
 *
 * ENGLISH
 * -------
 * One short-lived line from the muzzle to a pellet's impact point.
 *
 * 한국어
 * ------
 * 총구에서 산탄이 명중한 지점까지 이어지는 짧은 수명의 선 하나입니다.
 */
typedef struct { v3 a, b; float life; } Tracer;

/* --- Level reference / 레벨 참조 --- */

/** @brief Level shots are traced against. Borrowed from wp_init; not owned. / 사격 판정 대상 레벨. wp_init에서 빌려 온 것이며 소유하지 않습니다. */
static const Level *g_level;

/* --- Gun model and materials / 총기 모델 및 재질 --- */

/** @brief The gun's vertex buffer, shared by every material run. / 총기의 정점 버퍼. 모든 재질 구간이 공유합니다. */
static Mesh     g_gun_mesh;

/* The gun is drawn as one run per material, so a blued barrel, a steel
   receiver and a walnut grip can share a single vertex buffer. */
/** @brief Index ranges, one per material. / 재질별 인덱스 구간. */
static MdlRange g_gun_ranges[MDL_MAX_RANGES];
/** @brief Materials matching ::g_gun_ranges entry for entry. / ::g_gun_ranges의 각 항목에 대응하는 재질. */
static Mat      g_gun_tex[MDL_MAX_RANGES];
/** @brief How many entries of the two arrays above are in use. / 위 두 배열에서 사용 중인 항목의 개수. */
static int      g_gun_range_count;

/* Muzzle in gun-local units, loaded with the model. */
/** @brief Muzzle position in gun-local units, loaded with the model. / 총기 로컬 좌표계의 총구 위치. 모델과 함께 로드됩니다. */
static v3      g_muzzle = {0.0f, 0.01f, -1.02f};

/* The tether's material. Loaded once and refreshed alongside the gun's own
   materials, the same way g_gun_tex is -- see wp_reload_texture. */
/** @brief The tether's material, refreshed alongside the gun's own. / 로프의 재질. 총기 재질과 함께 갱신됩니다. */
static Mat     g_rope_mat;

/* --- Per-frame camera setup / 프레임별 카메라 설정 --- */

/* Camera setup for the current frame, so effects spawned during firing can be
   placed in the world. Set by wp_update. */
/** @brief World camera fov and aspect for this frame. Set by wp_update. / 이번 프레임의 월드 카메라 시야각과 종횡비. wp_update가 설정합니다. */
static float   g_world_fov = 1.5708f, g_aspect = 1.777f;

/* --- Effect geometry / 효과 지오메트리 --- */

/** @brief Reusable GPU meshes for billboarded effects and for line/ribbon geometry. / 빌보드 효과와 선/리본 지오메트리를 위한 재사용 GPU 메시. */
static Mesh    g_fx_mesh, g_line_mesh;
/** @brief CPU-side builders feeding the two meshes above, rebuilt every frame. / 위 두 메시에 데이터를 공급하는 CPU 측 빌더. 매 프레임 재구성됩니다. */
static MeshBuf g_fx_buf,  g_line_buf;

/* --- Effect ring buffers / 효과 링 버퍼 --- */

/** @brief Decals, overwritten oldest-first once full. / 자국. 가득 차면 오래된 것부터 덮어씁니다. */
static Impact g_impacts[MAX_IMPACTS];
/** @brief Tracers, overwritten oldest-first once full. / 예광탄. 가득 차면 오래된 것부터 덮어씁니다. */
static Tracer g_tracers[MAX_TRACERS];
/** @brief Write cursors into the two ring buffers above. / 위 두 링 버퍼의 쓰기 커서. */
static int    g_impact_next, g_tracer_next;

/* --- Muzzle flash randomisation / 총구 화염 무작위화 --- */

/** @brief Randomised size of the current flash, so no two shots look alike. / 현재 화염의 무작위 크기. 두 발이 똑같아 보이지 않게 합니다. */
static float g_flash_scale = 1.0f;
/** @brief Randomised roll of the current flash, in radians. / 현재 화염의 무작위 회전 (라디안). */
static float g_flash_roll;

/* ---------------------------------------------------------------- raycast */

/**
 * @brief Traces a ray against the recorded level, always producing a usable result.
 *
 * ENGLISH
 * -------
 * @param[in]  o     Ray origin.
 * @param[in]  d     Ray direction; expected to be unit length.
 * @param[out] out_t Receives the hit distance, or ::RANGE on a miss.
 * @param[out] out_n Receives the surface normal, or straight up on a miss.
 * @return 1 when a surface was hit, 0 on a miss or with no level recorded.
 * @note Writes usable fallback values even when it returns 0, so a caller
 *       drawing a tracer can always place its far end without branching.
 *
 * 한국어
 * ------
 * @brief 기록된 레벨에 대해 광선을 투사하며, 항상 사용 가능한 결과를 만들어 냅니다.
 * @param[in]  o     광선의 시작점.
 * @param[in]  d     광선의 방향. 단위 길이여야 합니다.
 * @param[out] out_t 명중 거리를 받습니다. 빗나가면 ::RANGE가 반환됩니다.
 * @param[out] out_n 표면 법선을 받습니다. 빗나가면 수직 상향 벡터가 반환됩니다.
 * @return 표면에 명중하면 1, 빗나가거나 기록된 레벨이 없으면 0.
 * @note 0을 반환하는 경우에도 사용 가능한 대체 값을 기록하므로, 예광탄을 그리는
 *       호출자는 분기 없이 항상 끝점을 배치할 수 있습니다.
 */
static int trace(v3 o, v3 d, float *out_t, v3 *out_n) {
    if (!g_level) return 0;
    if (level_trace(g_level, o, d, RANGE, out_t, out_n)) return 1;
    /* Miss: report the full range and an arbitrary upward normal, so the
       caller still has somewhere to draw to.
       빗나감: 최대 사거리와 임의의 상향 법선을 보고하여, 호출자가 그릴 지점을
       확보할 수 있게 합니다. */
    *out_t = RANGE;
    *out_n = v3f(0, 1, 0);
    return 0;
}

/* ------------------------------------------------------------------- rng */

/**
 * @brief Draws the next pseudo-random number in [0, 1).
 *
 * ENGLISH
 * -------
 * @param[in,out] w Weapon carrying the generator state.
 * @return A value in the range [0, 1).
 * @note A linear congruential generator seeded per weapon, so pellet spread
 *       is reproducible in a headless test rather than depending on a global.
 *       The low bits of an LCG are the weakest, which is why the state is
 *       shifted down by 8 before use.
 *
 * 한국어
 * ------
 * @brief [0, 1) 범위의 다음 의사 난수를 생성합니다.
 * @param[in,out] w 생성기 상태를 보유한 무기.
 * @return [0, 1) 범위의 값.
 * @note 무기별로 시드가 설정된 선형 합동 생성기이므로, 산탄의 산포가 전역 상태에
 *       의존하지 않고 헤드리스 테스트에서 재현 가능합니다. LCG는 하위 비트가 가장
 *       취약하므로, 사용 전에 상태를 8비트만큼 시프트합니다.
 */
static float frand(Weapon *w) {
    w->rng = w->rng * 1664525u + 1013904223u;
    return (w->rng >> 8) * (1.0f / 16777216.0f);
}

/**
 * @brief Draws the next pseudo-random number in [-1, 1).
 *
 * ENGLISH
 * -------
 * @param[in,out] w Weapon carrying the generator state.
 * @return A value in the range [-1, 1), for symmetric offsets like spread.
 *
 * 한국어
 * ------
 * @brief [-1, 1) 범위의 다음 의사 난수를 생성합니다.
 * @param[in,out] w 생성기 상태를 보유한 무기.
 * @return [-1, 1) 범위의 값. 산포처럼 좌우 대칭인 편차에 사용됩니다.
 */
static float frand_signed(Weapon *w) { return frand(w) * 2.0f - 1.0f; }

/* ------------------------------------------------------------------ setup */

/** @brief Name of the currently loaded gun model, kept so hot reload can rebuild it. / 현재 로드된 총기 모델의 이름. 핫 리로드가 재생성할 수 있도록 보관합니다. */
static char g_model_name[32] = "shotgun";

void wp_reload_texture(void) {
    /* Materials are named by the model, so a recipe change means rebuilding
       the whole lookup rather than one hardcoded texture. */
    tex_flush();
    for (int i = 0; i < g_gun_range_count; i++)
        g_gun_tex[i] = tex_mat(g_gun_ranges[i].mat);
    g_rope_mat = tex_mat("rope");
}

void wp_set_model(const char *name) {
    Model m;
    if (!mdl_load(name, &m)) return;

    int i = 0;
    for (; name[i] && i < (int)sizeof(g_model_name) - 1; i++)
        g_model_name[i] = name[i];
    g_model_name[i] = 0;

    MeshBuf gun;
    mb_init(&gun, MDL_MAX_VERTS);
    g_gun_range_count = mdl_geometry(&gun, &m, g_gun_ranges, MDL_MAX_RANGES);
    mesh_upload(&g_gun_mesh, &gun, 0);
    mb_free(&gun);

    for (int r = 0; r < g_gun_range_count; r++)
        g_gun_tex[r] = tex_mat(g_gun_ranges[r].mat);

    g_muzzle = v3f(m.muzzle[0] / 100.0f, m.muzzle[1] / 100.0f,
                   m.muzzle[2] / 100.0f);
}

void wp_init(Weapon *w, const Level *level) {
    Weapon zero = {0};
    *w = zero;
    w->rng = 0x2545f491u;
    w->ammo = WEAPON_START_AMMO;
    /* Not zero: 0 is a valid monster index, so a zeroed struct would read as
       "hooked to monster 0" and deal damage to a bystander on the first
       arrival. HOOK_IDLE makes that unreachable, but the field should still
       say what it means.
       0이 아닙니다. 0은 유효한 몬스터 인덱스이므로, 0으로 초기화된 구조체는 "0번
       몬스터에 걸림"으로 읽혀 첫 도달 시 무관한 대상에게 피해를 줄 수 있습니다.
       HOOK_IDLE이 이를 도달 불가능하게 만들지만, 필드 자체가 의미를 말해야 합니다. */
    w->hook_enemy = -1;

    g_level = level;

    wp_set_model("shotgun");
    g_rope_mat = tex_mat("rope");

    mb_init(&g_fx_buf,   MAX_IMPACTS * 6 + 64);
    mb_init(&g_line_buf, MAX_TRACERS * 2 + 32);
}

/* ------------------------------------------------------------------ firing */

/**
 * @brief Re-projects a gun-local point into the world so effects leave the gun on screen.
 *
 * ENGLISH
 * -------
 * @param[in] w     Weapon supplying the live view model transform.
 * @param[in] local Point in gun-local space to project.
 * @param[in] eye   Camera position.
 * @param[in] right Camera right basis vector.
 * @param[in] up    Camera up basis vector.
 * @param[in] fwd   Camera forward basis vector.
 * @return A world position that lines up with the drawn point on screen.
 * @note The view model is drawn with its own, narrower projection, so no
 *       point on it has an honest world position. To make an effect leave the
 *       gun *on screen* anyway, take that point's normalised device
 *       coordinates under the view-model projection and re-project them into
 *       the world camera at a fixed distance. The alternative -- hand-tuned
 *       world offsets -- drifts the moment anyone moves the gun or redraws
 *       the model.
 * @note Takes an arbitrary gun-local point rather than hardcoding ::g_muzzle,
 *       so the same projection serves both the barrel (tracers) and the hook
 *       launcher slung underneath it (the tether) without two copies of this
 *       maths.
 * @warning Reads ::g_world_fov and ::g_aspect, which ::wp_update sets. Calling
 *          this before the first wp_update uses the initial defaults.
 *
 * 한국어
 * ------
 * @brief 총기 로컬 좌표의 점을 월드로 재투영하여, 효과가 화면상 총기에서 나가도록 합니다.
 * @param[in] w     실시간 뷰 모델 변환을 제공하는 무기.
 * @param[in] local 투영할 총기 로컬 공간의 점.
 * @param[in] eye   카메라 위치.
 * @param[in] right 카메라의 우측 기저 벡터.
 * @param[in] up    카메라의 상향 기저 벡터.
 * @param[in] fwd   카메라의 전방 기저 벡터.
 * @return 화면에 그려진 지점과 일치하는 월드 좌표.
 * @note 뷰 모델은 더 좁은 자체 투영으로 그려지므로, 그 위의 어떤 점도 정확한 월드
 *       좌표를 갖지 않습니다. 그럼에도 효과가 *화면상* 총기에서 나가도록 하려면,
 *       해당 점의 뷰 모델 투영 기준 정규화 장치 좌표를 구한 뒤 이를 고정 거리에서
 *       월드 카메라로 재투영합니다. 대안인 수동 조정 월드 오프셋 방식은 누군가
 *       총기를 옮기거나 모델을 다시 그리는 즉시 어긋납니다.
 * @note ::g_muzzle을 고정하지 않고 임의의 총기 로컬 점을 받으므로, 동일한 투영이
 *       총열(예광탄)과 그 아래에 매달린 훅 발사기(로프) 양쪽에 이 수식의 사본
 *       없이 사용됩니다.
 * @warning ::wp_update가 설정하는 ::g_world_fov와 ::g_aspect를 읽습니다. 첫
 *          wp_update 이전에 호출하면 초기 기본값이 사용됩니다.
 */
static v3 muzzle_world_at(const Weapon *w, v3 local, v3 eye, v3 right, v3 up, v3 fwd) {
    v3 mv = mat4_mul_pt(wp_gun_matrix(w), local);
    /* A point at or behind the near plane would divide by ~0 and fling the
       result off screen.
       근평면에 있거나 그 뒤에 있는 점은 0에 가까운 값으로 나누게 되어 결과가
       화면 밖으로 튕겨 나갑니다. */
    if (mv.z > -1e-3f) mv.z = -1e-3f;               /* must stay in front */

    /* Perspective divide under the VIEW MODEL's projection, yielding NDC.
       뷰 모델의 투영을 기준으로 원근 나눗셈을 수행하여 NDC를 얻습니다. */
    float t_vm  = tanf(g_gun_pose.fov * 0.5f);
    float ndc_x = (mv.x / -mv.z) / (t_vm * g_aspect);
    float ndc_y = (mv.y / -mv.z) / t_vm;

    /* Rebuild a world point at a fixed distance under the WORLD projection.
       The distance is arbitrary -- any value puts the effect on the same
       screen pixel; this one simply keeps it clear of the near plane.
       월드 투영을 기준으로 고정 거리에 월드 좌표를 재구성합니다. 이 거리는
       임의의 값입니다. 어떤 값이든 효과는 화면상 동일한 픽셀에 놓이며, 이 값은
       단지 근평면에서 충분히 떨어뜨리기 위한 것입니다. */
    const float dist = 0.7f;
    float t_w = tanf(g_world_fov * 0.5f);

    return v3add(eye,
             v3add(v3scale(right, ndc_x * t_w * g_aspect * dist),
               v3add(v3scale(up, ndc_y * t_w * dist),
                     v3scale(fwd, dist))));
}

/**
 * @brief Projects the barrel's own muzzle into the world.
 *
 * ENGLISH
 * -------
 * @param[in] w     Weapon supplying the view model transform.
 * @param[in] eye   Camera position.
 * @param[in] right Camera right basis vector.
 * @param[in] up    Camera up basis vector.
 * @param[in] fwd   Camera forward basis vector.
 * @return The world position of the drawn muzzle, where tracers begin.
 *
 * 한국어
 * ------
 * @brief 총열 자체의 총구를 월드 좌표로 투영합니다.
 * @param[in] w     뷰 모델 변환을 제공하는 무기.
 * @param[in] eye   카메라 위치.
 * @param[in] right 카메라의 우측 기저 벡터.
 * @param[in] up    카메라의 상향 기저 벡터.
 * @param[in] fwd   카메라의 전방 기저 벡터.
 * @return 예광탄이 시작되는, 화면에 그려진 총구의 월드 좌표.
 */
static v3 muzzle_world(const Weapon *w, v3 eye, v3 right, v3 up, v3 fwd) {
    return muzzle_world_at(w, g_muzzle, eye, right, up, fwd);
}

/* Where the hook launches from, in gun-local units: the main barrel's own
   muzzle, dropped and pulled back a little, as if a separate launcher were
   slung underneath it. Purely a visual anchor for the tether -- the aim
   itself still traces from the eye (see wp_hook_fire), the same way the
   shotgun's own pellets do, so the crosshair stays honest about what gets
   hooked regardless of where the tether appears to leave the gun. */
static v3 hook_muzzle(void) {
    return v3f(g_muzzle.x, g_muzzle.y - HOOK_MUZZLE_DROP, g_muzzle.z + HOOK_MUZZLE_BACK);
}

/**
 * @brief Fires one shotgun blast: pellets, damage, recoil kick and effects.
 *
 * ENGLISH
 * -------
 * @param[in,out] w               Weapon firing. Its timers, spread and RNG advance.
 * @param[in]     eye             Player's eye, the origin of the hitscan.
 * @param[in]     yaw             Aim yaw in radians.
 * @param[in]     pitch           Aim pitch in radians, recoil already included
 *                                by the caller.
 * @param[in,out] player_vel      Receives the recoil kick directly.
 * @param[in]     player_grounded Selects the ground kick over the air kick.
 * @note Ammunition is decremented by the caller, not here, so the dry-fire
 *       path can rate-limit its click without spending a shell.
 * @note Traces from the eye while drawing tracers from the muzzle: the
 *       crosshair stays honest about what you hit, and the effect still looks
 *       like it came out of the barrel.
 *
 * 한국어
 * ------
 * @brief 샷건 한 발을 발사합니다. 산탄, 피해, 반동, 효과를 모두 처리합니다.
 * @param[in,out] w               발사하는 무기. 타이머, 산포도, 난수 상태가 진행됩니다.
 * @param[in]     eye             히트스캔의 시작점인 플레이어의 눈 위치.
 * @param[in]     yaw             조준 방향 (라디안).
 * @param[in]     pitch           조준 피치 (라디안). 반동은 호출자가 이미 포함시킨
 *                                상태입니다.
 * @param[in,out] player_vel      반동을 직접 전달받습니다.
 * @param[in]     player_grounded 공중 반동 대신 지상 반동을 선택합니다.
 * @note 탄약 감소는 여기가 아니라 호출자가 처리합니다. 그래야 빈 탄창 발사 경로가
 *       탄환을 소모하지 않고 클릭음의 빈도만 제한할 수 있습니다.
 * @note 판정은 눈에서 시작하고 예광탄은 총구에서 그립니다. 그래야 조준점은 실제
 *       명중 대상에 대해 정직하게 유지되면서도, 효과는 총열에서 나온 것처럼
 *       보입니다.
 */
static void fire(Weapon *w, v3 eye, float yaw, float pitch,
                 v3 *player_vel, int player_grounded) {
    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    v3 fwd   = v3f(-sy * cp, sp, -cy * cp);
    v3 right = v3f(cy, 0, -sy);
    v3 up    = v3cross(right, fwd);

    /* Recoil jumping: every shot kicks the player back along -aim, harder in
       the air. Aiming down and firing therefore launches you up-and-back --
       the same trick a rocket jump is, for free, because -fwd already has
       whatever vertical component the aim does. No proximity check: unlike an
       explosion this is a direct momentum transfer from the gun, so it fires
       every time regardless of what (if anything) the pellets hit. */
    float kick = player_grounded ? RECOIL_MOVE_GROUND : RECOIL_MOVE_AIR;
    *player_vel = v3add(*player_vel, v3scale(fwd, -kick));

    /* Tracers start at the barrel, not the eye, or they look like they come
       out of the player's forehead. The hitscan itself still traces from the
       eye -- that is what keeps the crosshair honest about what you hit. */
    v3 muzzle = muzzle_world(w, eye, right, up, fwd);

    int hits = 0;

    for (int i = 0; i < PELLETS; i++) {
        v3 dir = v3norm(v3add(fwd,
                    v3add(v3scale(right, frand_signed(w) * PELLET_SPREAD),
                          v3scale(up,    frand_signed(w) * PELLET_SPREAD))));

        float t; v3 n;
        int hit = trace(eye, dir, &t, &n);

        /* A monster nearer than the wall stops the pellet. The trace above
           gives the wall distance (or RANGE on a miss); anything the enemy
           hitscan finds inside that is what the pellet actually strikes. */
        float et; int eidx;
        int blood = enemy_hitscan(eye, dir, hit ? t : RANGE, &et, &eidx);

        if (blood) {
            enemy_hurt(eidx, PELLET_DAMAGE, dir);
            t = et;
            hit = 1;
        }
        hits += hit;
        v3 end = v3add(eye, v3scale(dir, t));

        if (hit) {
            Impact *im = &g_impacts[g_impact_next];
            g_impact_next = (g_impact_next + 1) % MAX_IMPACTS;
            /* Blood sprays back toward the shooter; a wall decal sits on the
               surface, nudged off it so the decal wins the depth test. */
            im->p = blood ? v3sub(end, v3scale(dir, 0.05f))
                          : v3add(end, v3scale(n, 0.012f));
            im->n = blood ? v3scale(dir, -1.0f) : n;
            im->life = IMPACT_LIFE;
            im->blood = blood;
        }

        Tracer *tr = &g_tracers[g_tracer_next];
        g_tracer_next = (g_tracer_next + 1) % MAX_TRACERS;
        tr->a = muzzle; tr->b = end; tr->life = TRACER_LIFE;
    }

    audio_play("shot", 100);

    /* One impact for the whole volley, louder when more pellets connect.
       Six separate impact sounds fired on the same frame just smear into
       noise and steal every voice. */
    if (hits) audio_play("impact", 35 + hits * 8);

    /* The pump is what gives a shotgun its rhythm, so it lands partway
       through the cooldown rather than on the trigger pull. */
    w->pump_timer = FIRE_INTERVAL * 0.55f;

    w->cooldown = FIRE_INTERVAL;
    w->recoil  += RECOIL_KICK * (0.85f + frand(w) * 0.3f);
    w->punch   += PUNCH_KICK;
    w->flash    = FLASH_TIME;

    g_flash_scale = 1.1f + frand(w) * 0.7f;
    g_flash_roll  = frand(w) * M_TAU;
}

/* -------------------------------------------------------------- the hook
 *
 * ENGLISH
 * -------
 * A DOOM Eternal Meat Hook: fire, pull, damage, launch. See the tuning banner
 * in weapon.h for the design; this is the machinery.
 *
 * The claw is a PROJECTILE, so this file owns a small simulation of it -- one
 * position stepped forward each frame and tested against the level and the
 * monsters. That is the only reason wp_hook_update needs the level: the throw
 * has not resolved yet when wp_hook_fire returns.
 *
 * The pull is a WINCH, which is what the previous rope-constraint version
 * deliberately was not. It accelerates the player at the target and caps the
 * result, because a Meat Hook closes distance rather than holding a length.
 * If you are looking for the swing physics, they are gone on purpose -- see
 * the note in weapon.h on why the two are different mechanics.
 *
 * These functions touch no GL and take the level explicitly, so
 * tools/hooktest.c drives the whole four-beat cycle with no context.
 *
 * 한국어
 * ------
 * DOOM Eternal의 미트 훅입니다. 발사, 견인, 피해, 도약으로 구성됩니다. 설계는
 * weapon.h의 튜닝 배너를 참조하십시오. 이곳은 그 구현입니다.
 *
 * 클로는 *발사체*이므로 이 파일이 그에 대한 작은 시뮬레이션을 소유합니다. 매 프레임
 * 전진하는 위치 하나를 레벨 및 몬스터와 충돌 검사합니다. wp_hook_update가 레벨을
 * 필요로 하는 유일한 이유가 이것입니다. wp_hook_fire가 반환되는 시점에는 투척이 아직
 * 해결되지 않았기 때문입니다.
 *
 * 견인은 *윈치*이며, 이는 이전의 로프 구속 버전이 의도적으로 피했던 방식입니다.
 * 플레이어를 대상 쪽으로 가속하고 결과에 상한을 둡니다. 미트 훅은 길이를 유지하는
 * 것이 아니라 거리를 좁히는 도구이기 때문입니다. 스윙 물리를 찾고 계신다면, 그것은
 * 의도적으로 제거되었습니다. 두 방식이 왜 다른 메커니즘인지는 weapon.h의 설명을
 * 참조하십시오.
 *
 * 이 함수들은 GL을 사용하지 않고 레벨을 명시적으로 받으므로, tools/hooktest.c가
 * 컨텍스트 없이 4단계 주기 전체를 실행합니다.
 */

/* Reeling in has to be able to reach the arrival test, or a completed pull
   would never fire its launch. Checked here rather than trusted to whoever
   next retunes the block in weapon.h. (A _Static_assert rather than #if: the
   preprocessor cannot compare floating constants.) */
_Static_assert(HOOK_ARRIVE_DIST > 0.0f,
               "HOOK_ARRIVE_DIST must be positive or the pull never arrives");
_Static_assert(HOOK_REFIRE_DELAY > HOOK_COOLDOWN,
               "a connected hook must cost more than a clean miss");

/**
 * @brief Ends the hook cycle and charges the rearm delay. The single exit path.
 *
 * ENGLISH
 * -------
 * @param[in,out] w Weapon to reset to ::HOOK_IDLE.
 * @note Every path that ends a hook goes through here -- arrival, timeout,
 *       a miss, and an explicit cancel -- so the rearm delay cannot be
 *       remembered in three places and forgotten in the fourth.
 * @note Only ever lengthens the cooldown: a hook that ended while the
 *       launcher happened to be on a longer timer should not shorten it.
 *
 * 한국어
 * ------
 * @brief 훅 주기를 종료하고 재장전 지연을 부과합니다. 유일한 종료 경로입니다.
 * @param[in,out] w ::HOOK_IDLE로 되돌릴 무기.
 * @note 훅을 끝내는 모든 경로(도달, 시간 초과, 빗나감, 명시적 취소)가 이 함수를
 *       거치므로, 재장전 지연을 세 곳에서 처리하다가 네 번째에서 빠뜨리는 일이
 *       발생할 수 없습니다.
 * @note 쿨다운은 늘리기만 합니다. 발사기가 마침 더 긴 타이머 상태일 때 훅이 끝났다고
 *       해서 그 시간을 줄여서는 안 되기 때문입니다.
 */
static void hook_end(Weapon *w) {
    w->hook_state = HOOK_IDLE;
    w->hook_enemy = -1;
    w->hook_timer = 0.0f;
    if (w->hook_cooldown < HOOK_REFIRE_DELAY) w->hook_cooldown = HOOK_REFIRE_DELAY;
}

void wp_hook_arm(Weapon *w) {
    w->hook_latched = 0;
}

int wp_hook_locks_aim(const Weapon *w) {
    return w->hook_state != HOOK_IDLE;
}

int wp_hook_in_range(const Weapon *w, const Level *l,
                     v3 eye, float yaw, float pitch) {
    /* "Would a throw connect right now" -- so the same refusals wp_hook_fire
       applies count as out of range. A crosshair that lights up while the
       launcher is on cooldown is lying about what the button will do.
       "지금 발사하면 명중하는가"이므로, wp_hook_fire가 적용하는 것과 동일한 거부
       조건들이 사거리 밖으로 간주됩니다. 발사기가 쿨다운 중인데 조준점이 켜진다면
       버튼이 무엇을 할지에 대해 거짓말을 하는 것입니다. */
    if (w->hook_state != HOOK_IDLE) return 0;
    if (w->hook_latched)            return 0;
    if (w->hook_cooldown > 0.0f)    return 0;

    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    v3 fwd = v3f(-sy * cp, sp, -cy * cp);

    /* Monsters first, then geometry -- the same priority hook_fly uses, so
       the indicator agrees with what the claw would actually hit.
       몬스터를 먼저, 그다음 지오메트리를 확인합니다. hook_fly와 동일한 우선순위이므로
       표시가 클로가 실제로 맞힐 대상과 일치합니다. */
    float t; int idx;
    if (enemy_hitscan(eye, fwd, HOOK_RANGE, &t, &idx)) return 1;

    v3 n;
    return (l && level_trace(l, eye, fwd, HOOK_RANGE, &t, &n)) ? 1 : 0;
}

void wp_hook_release(Weapon *w) {
    hook_end(w);
}

int wp_hook_fire(Weapon *w, v3 eye, float yaw, float pitch) {
    if (w->hook_state != HOOK_IDLE) return 0;   /* one claw in the air at a time */
    if (w->hook_latched) return 0;              /* this press has had its throw */
    if (w->hook_cooldown > 0.0f) return 0;

    /* The claw is away the moment the trigger goes, so the cooldown is spent
       here rather than when it lands. A throw that finds nothing still threw.
       방아쇠를 당기는 순간 클로가 떠나므로, 착지 시점이 아니라 여기서 쿨다운을
       소모합니다. 아무것도 맞히지 못한 투척도 투척한 것입니다. */
    w->hook_cooldown = HOOK_COOLDOWN;
    w->hook_latched  = 1;

    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    v3 fwd = v3f(-sy * cp, sp, -cy * cp);

    /* The claw starts at the eye and flies along the aim. Starting at the
       drawn muzzle would look better for the first metre and then diverge
       from the crosshair, which is the wrong trade -- the tether is drawn
       from the muzzle regardless (see hook_muzzle), so the visual is honest
       without the flight path having to be.
       클로는 눈에서 출발하여 조준 방향으로 날아갑니다. 화면에 그려진 총구에서
       출발하면 첫 1미터는 더 나아 보이겠지만 이후 조준점과 어긋나게 되며, 이는 잘못된
       거래입니다. 로프는 어차피 총구에서 그려지므로(hook_muzzle 참조), 비행 경로가
       그럴 필요 없이도 시각적으로 정직합니다. */
    w->hook_state  = HOOK_FLYING;
    w->hook_pos    = eye;
    w->hook_target = v3add(eye, v3scale(fwd, HOOK_RANGE));
    w->hook_enemy  = -1;
    w->hook_timer  = 0.0f;

    audio_play("hook", 75);
    return 1;
}

/**
 * @brief Steps the claw forward one frame and tests what it hits. Beat 1.
 *
 * ENGLISH
 * -------
 * @param[in,out] w  Weapon whose claw is in flight.
 * @param[in]     l  Level to collide against; may be NULL for monsters only.
 * @param[in]     dt Timestep in seconds.
 * @return 1 while still flying or on a hit, 0 when the throw missed and the
 *         hook has been ended.
 * @note Sub-steps the flight in ::HOOK_FLY_STEP increments rather than one
 *       jump per frame. At 90 m/s a single 60 Hz step is 1.5 m, which is wide
 *       enough to pass straight through a monster or a thin wall -- the
 *       classic fast-projectile tunnelling bug.
 * @note Monsters are tested before geometry over the same sub-step, so a
 *       monster standing against a wall is hooked rather than the wall behind
 *       it. That is the whole point of the mechanic, and getting the priority
 *       backwards would make it feel broken near cover.
 *
 * 한국어
 * ------
 * @brief 클로를 한 프레임 전진시키고 무엇에 맞았는지 검사합니다. 1단계입니다.
 * @param[in,out] w  클로가 비행 중인 무기.
 * @param[in]     l  충돌 판정 대상 레벨. NULL이면 몬스터만 검사합니다.
 * @param[in]     dt 시간 간격 (초).
 * @return 아직 비행 중이거나 명중하면 1, 빗나가서 훅이 종료되면 0.
 * @note 프레임당 한 번 도약하는 대신 ::HOOK_FLY_STEP 단위로 세분하여 전진합니다.
 *       초속 90m에서 60Hz 한 스텝은 1.5m이며, 이는 몬스터나 얇은 벽을 그대로
 *       통과하기에 충분한 거리입니다. 고속 발사체의 전형적인 터널링 버그입니다.
 * @note 동일한 세부 스텝 내에서 지오메트리보다 몬스터를 먼저 검사하므로, 벽에 붙어
 *       선 몬스터가 있으면 뒤쪽 벽이 아니라 몬스터가 걸립니다. 그것이 이 메커니즘의
 *       핵심이며, 우선순위를 반대로 하면 엄폐물 근처에서 고장 난 것처럼 느껴집니다.
 */
static int hook_fly(Weapon *w, const Level *l, float dt) {
    v3 to_end = v3sub(w->hook_target, w->hook_pos);
    float remaining = v3len(to_end);
    if (remaining < 1e-4f) { hook_end(w); return 0; }   /* reached max range */

    v3 dir = v3scale(to_end, 1.0f / remaining);
    float travel = HOOK_FLY_SPEED * dt;
    if (travel > remaining) travel = remaining;

    /* Walk the segment in short steps so nothing thin is skipped over.
       얇은 대상을 건너뛰지 않도록 선분을 짧은 스텝으로 나누어 진행합니다. */
    float done = 0.0f;
    while (done < travel) {
        float step = travel - done;
        if (step > HOOK_FLY_STEP) step = HOOK_FLY_STEP;

        v3 from = w->hook_pos;
        v3 next = v3add(from, v3scale(dir, step));

        /* Monsters first: a demon in front of a wall is the target, not the
           wall. 몬스터 우선: 벽 앞의 악마가 대상이지 벽이 아닙니다. */
        float et; int ei;
        if (enemy_hitscan(from, dir, step, &et, &ei)) {
            w->hook_pos    = v3add(from, v3scale(dir, et));
            w->hook_target = w->hook_pos;
            w->hook_enemy  = ei;
            w->hook_state  = HOOK_PULLING;
            w->hook_timer  = 0.0f;
            return 1;
        }

        /* Then geometry. A wall is a perfectly good anchor -- it just deals
           no damage on arrival.
           다음으로 지오메트리입니다. 벽도 훌륭한 고정점이며, 다만 도달 시 피해를
           주지 않을 뿐입니다. */
        float lt; v3 ln;
        if (l && level_trace(l, from, dir, step, &lt, &ln)) {
            w->hook_pos    = v3add(from, v3scale(dir, lt));
            w->hook_target = w->hook_pos;
            w->hook_enemy  = -1;
            w->hook_state  = HOOK_PULLING;
            w->hook_timer  = 0.0f;
            return 1;
        }

        w->hook_pos = next;
        done += step;
    }

    /* Still travelling. Running out of range is a clean miss.
       아직 비행 중입니다. 사거리를 소진하면 깨끗한 빗나감입니다. */
    if (v3len(v3sub(w->hook_target, w->hook_pos)) < 1e-3f) { hook_end(w); return 0; }
    return 1;
}

/**
 * @brief Applies the launch impulse that ends a completed hook. Beat 4.
 *
 * ENGLISH
 * -------
 * @param[in]     travel Unit direction the player was moving in on arrival.
 * @param[in]     speed  Arrival speed, m/s.
 * @param[in,out] vel    Player momentum to overwrite with the launch.
 * @note Overwrites rather than adds. The arrival velocity points straight at
 *       whatever was just hit, so keeping it would drive the player into the
 *       target they are supposed to be bouncing off -- the launch has to
 *       replace that motion, not compete with it.
 * @note Two components: ::HOOK_LAUNCH_UP clears the target so the next hook
 *       has somewhere to go, and ::HOOK_LAUNCH_ALONG preserves a fraction of
 *       the travel direction so a chain of hooks keeps its momentum instead
 *       of stopping dead above each one.
 *
 * 한국어
 * ------
 * @brief 완료된 훅을 마무리하는 도약 충격량을 적용합니다. 4단계입니다.
 * @param[in]     travel 도달 시점에 플레이어가 이동하던 단위 방향.
 * @param[in]     speed  도달 속도 (m/s).
 * @param[in,out] vel    도약으로 덮어쓸 플레이어 운동량.
 * @note 더하지 않고 덮어씁니다. 도달 속도는 방금 부딪힌 대상을 정면으로 향하고
 *       있으므로, 그대로 두면 튕겨 나와야 할 대상 쪽으로 플레이어를 밀어 넣게 됩니다.
 *       도약은 그 운동을 대체해야지 경쟁해서는 안 됩니다.
 * @note 두 성분으로 구성됩니다. ::HOOK_LAUNCH_UP은 대상에서 벗어나게 하여 다음 훅이
 *       갈 곳을 확보하고, ::HOOK_LAUNCH_ALONG은 진행 방향의 일부를 보존하여 연속된
 *       훅이 매번 대상 위에서 멈추지 않고 운동량을 유지하게 합니다.
 */
static void hook_launch(v3 travel, float speed, v3 *vel) {
    v3 up    = v3f(0.0f, HOOK_LAUNCH_UP, 0.0f);
    v3 along = v3scale(travel, speed * HOOK_LAUNCH_ALONG);

    v3 out = v3add(up, along);
    float sp = v3len(out);
    if (sp > HOOK_LAUNCH_MAX) out = v3scale(out, HOOK_LAUNCH_MAX / sp);

    *vel = out;
}

int wp_hook_update(Weapon *w, const Level *l, v3 *pos, v3 *vel, float dt) {
    if (w->hook_state == HOOK_IDLE) return 0;

    w->hook_timer += dt;

    /* --- beat 1: the claw is still travelling ---------------------------- */
    if (w->hook_state == HOOK_FLYING)
        return hook_fly(w, l, dt);

    /* --- the target may have moved --------------------------------------- */
    if (w->hook_enemy >= 0) {
        const Enemy *e = enemy_at(w->hook_enemy);
        /* A dead or despawned target ends the hook without a launch: there is
           nothing left to bounce off. Tracking by index rather than position
           is what makes this detectable at all.
           죽었거나 사라진 대상은 도약 없이 훅을 종료시킵니다. 튕겨 나올 대상이 남아
           있지 않기 때문입니다. 위치가 아닌 인덱스로 추적하기에 이를 감지할 수
           있습니다. */
        if (!e || !e->active || e->state == E_DEAD) { hook_end(w); return 0; }
        /* Aim at the centre of mass rather than the feet, or the pull drags
           the player into the floor. Height comes from the type table, since
           Enemy stores only what varies per instance.
           발이 아닌 몸통 중심을 겨냥합니다. 그렇지 않으면 견인이 플레이어를 바닥으로
           끌고 들어갑니다. Enemy는 개체마다 달라지는 값만 보관하므로 신장은 종류
           테이블에서 가져옵니다. */
        const MonType *S = mon_stats(e->type);
        float mid = S ? S->height * 0.5f : 0.8f;
        w->hook_target = v3f(e->pos.x, e->pos.y + mid, e->pos.z);
    }

    v3 to = v3sub(w->hook_target, *pos);
    float dist = v3len(to);

    /* --- beats 3 and 4: arrival, damage, launch ---------------------------
       Two ways to arrive, and the second is not optional. A proximity test
       alone misses whenever one frame's travel exceeds the arrival radius:
       at HOOK_PULL_MAX the player covers 0.63 m per 60Hz frame, and against a
       target the pull can overshoot -- especially a moving one, or when the
       player carries sideways momentum into the hook -- the closest approach
       can land outside HOOK_ARRIVE_DIST entirely. The player then sails past
       and oscillates around the target forever, because the pull keeps
       reversing to chase it. That is exactly what happened here: a straight
       20 m hook overshot to 29 m and never got nearer than 2.07 m against a
       1.6 m threshold.

       So arrival is ALSO "the target is now behind me". Once the direction to
       it has flipped relative to travel, the hook has done its job whether or
       not the radius was ever satisfied.

       도달 판정이 두 가지이며 두 번째는 선택 사항이 아닙니다. 근접 판정만으로는 한
       프레임의 이동 거리가 도달 반경을 넘는 순간 놓치게 됩니다. HOOK_PULL_MAX에서
       플레이어는 60Hz 프레임당 0.63m를 이동하며, 견인이 대상을 지나칠 수 있는 상황
       (특히 대상이 움직이거나 플레이어가 측면 운동량을 가지고 훅에 들어온 경우)에는
       최근접 거리가 HOOK_ARRIVE_DIST 바깥에 머무를 수 있습니다. 그러면 플레이어는
       대상을 지나쳐 날아가고, 견인이 계속 방향을 뒤집어 추격하므로 영원히 진동하게
       됩니다. 실제로 그런 일이 발생했습니다. 20m 직선 훅이 29m까지 지나쳐 갔고 1.6m
       임계값에 대해 2.07m보다 가까워진 적이 없었습니다.

       따라서 "대상이 이제 내 뒤에 있다"도 도달로 간주합니다. 대상을 향한 방향이 진행
       방향 대비 뒤집혔다면, 반경 조건을 만족했든 아니든 훅은 제 역할을 다한 것입니다.

       The overshoot test is deliberately narrow. It asks whether the velocity
       ALONG THE HOOK has reversed -- not whether the total velocity happens to
       point away, which is a different and much looser question. On the first
       pull frame the player is still nearly stationary while gravity
       cancellation has nudged vel.y upward, so a whole-vector test reads that
       tiny upward drift as "the target is behind me" and ends the hook
       instantly, having moved nobody. Requiring real inbound speed first, and
       then testing only the component that matters, is what separates a
       genuine fly-past from noise.

       지나침 판정은 의도적으로 좁게 설계되었습니다. *훅 방향* 속도 성분이 뒤집혔는지를
       묻지, 전체 속도가 우연히 반대쪽을 향하는지를 묻지 않습니다. 후자는 훨씬 느슨한
       다른 질문입니다. 견인 첫 프레임에는 플레이어가 거의 정지해 있는 반면 중력 상쇄가
       vel.y를 위로 살짝 밀어 올리므로, 벡터 전체로 판정하면 그 미세한 상승을 "대상이
       뒤에 있다"로 읽어 아무도 움직이지 않은 채 훅이 즉시 종료됩니다. 먼저 실제로
       접근 중일 것을 요구하고 그다음 관련된 성분만 검사하는 것이, 진짜 지나침과
       잡음을 구분하는 방법입니다. */
    float closing = v3dot(*vel, v3scale(to, 1.0f / (dist > 1e-4f ? dist : 1.0f)));
    int   passed  = (w->hook_timer > 0.15f && closing < -1.0f);
    if (dist < HOOK_ARRIVE_DIST || passed) {
        /* The direction of travel at the moment of arrival, which the launch
           partly preserves. Falls back to the hook direction when the player
           is somehow stationary, so a launch always has a sane axis.
           도달 순간의 이동 방향이며, 도약이 이를 부분적으로 보존합니다. 플레이어가
           어떤 이유로든 정지해 있으면 훅 방향으로 대체하여 도약이 항상 온전한 축을
           갖도록 합니다. */
        float speed = v3len(*vel);
        v3 travel = (speed > 0.1f) ? v3scale(*vel, 1.0f / speed)
                                   : v3norm(to);

        /* Beat 3: hooking a monster is an attack. Dealt once, here, rather
           than per frame of contact.
           3단계: 몬스터를 거는 것은 공격입니다. 접촉하는 매 프레임이 아니라 이곳에서
           한 번만 적용됩니다. */
        if (w->hook_enemy >= 0) {
            enemy_hurt(w->hook_enemy, HOOK_IMPACT_DAMAGE, travel);
            audio_play("ehit", 80);
        } else {
            audio_play("impact", 60);
        }

        /* Beat 4: bounce off automatically. No button, no timing. */
        hook_launch(travel, speed, vel);

        hook_end(w);
        return 0;
    }

    /* --- beat 2: the pull ------------------------------------------------- */
    if (w->hook_timer > HOOK_PULL_TIMEOUT) {
        /* Could not get there -- the target is running, or is somewhere the
           player cannot follow. A failure, so no launch.
           도달할 수 없었습니다. 대상이 도망치고 있거나 플레이어가 따라갈 수 없는 곳에
           있습니다. 실패이므로 도약도 없습니다. */
        hook_end(w);
        return 0;
    }
    if (dist < 1e-4f) return 1;                  /* degenerate; nothing sane to do */

    v3 dir = v3scale(to, 1.0f / dist);           /* player -> target, unit */

    /* Cancel gravity so a long horizontal hook does not sag into the floor
       before it arrives. This runs before player_move applies gravity for the
       frame, so adding it back here is what neutralises it.
       긴 수평 훅이 도달 전에 바닥으로 처지지 않도록 중력을 상쇄합니다. 이 코드는
       player_move가 해당 프레임의 중력을 적용하기 전에 실행되므로, 여기서 중력을 다시
       더하는 것이 상쇄 효과를 냅니다. */
    vel->y += PLAYER_GRAVITY * HOOK_PULL_ANTIGRAV * dt;

    /* The winch. Straight at the target, capped -- see the weapon.h banner on
       why this is a pull rather than a rope constraint.
       윈치입니다. 대상을 정면으로 향하며 상한이 있습니다. 이것이 로프 구속이 아닌
       견인인 이유는 weapon.h의 배너를 참조하십시오. */
    *vel = v3add(*vel, v3scale(dir, HOOK_PULL_ACCEL * dt));

    /* Cap the component along the hook rather than the whole velocity, so
       sideways momentum the player brought with them survives the pull
       instead of being scaled away by a global clamp.
       전체 속도가 아니라 훅 방향 성분만 제한합니다. 그래야 플레이어가 가지고 온 측면
       운동량이 전역 클램프에 의해 깎이지 않고 견인 중에도 유지됩니다. */
    float along = v3dot(*vel, dir);
    if (along > HOOK_PULL_MAX)
        *vel = v3add(*vel, v3scale(dir, HOOK_PULL_MAX - along));

    float sp = v3len(*vel);
    if (sp > HOOK_MAX_SPEED) *vel = v3scale(*vel, HOOK_MAX_SPEED / sp);

    (void)pos;   /* read through `to` above; never written -- the pull works
                    through velocity so it collides like any other movement */
    return 1;
}

void wp_update(Weapon *w, float dt, int firing, v3 eye, float yaw, float pitch,
               float move_speed, float mouse_dx, float mouse_dy,
               float world_fov, float aspect, v3 *player_vel, int player_grounded) {
    g_world_fov = world_fov;
    g_aspect    = aspect;

    if (w->cooldown      > 0.0f) w->cooldown      -= dt;
    if (w->hook_cooldown > 0.0f) w->hook_cooldown -= dt;
    if (w->flash         > 0.0f) w->flash         -= dt;

    if (w->pump_timer > 0.0f) {
        w->pump_timer -= dt;
        if (w->pump_timer <= 0.0f) audio_play("pump", 70);
    }
    if (w->dry_timer > 0.0f) w->dry_timer -= dt;

    if (firing && w->cooldown <= 0.0f) {
        if (w->ammo > 0) {
            w->ammo--;
            fire(w, eye, yaw, pitch + w->recoil, player_vel, player_grounded);
        } else if (w->dry_timer <= 0.0f) {
            /* Empty: a click, and a short lockout so holding the trigger does
               not machine-gun the click sound. No cooldown is spent, so the
               instant a pickup tops you up the next pull fires. */
            audio_play("dry", 55);
            w->dry_timer = 0.35f;
        }
    }

    /* Exponential springback, framerate independent. */
    float rr = 1.0f - expf(-RECOIL_RETURN * dt);
    float pr = 1.0f - expf(-PUNCH_RETURN  * dt);
    w->recoil -= w->recoil * rr;
    w->punch  -= w->punch  * pr;

    /* View model lags the mouse, then eases back to centre. The target is
       clamped so a fast flick can't fling the gun off screen. */
    float lag = 1.0f - expf(-11.0f * dt);
    float tx = clampf(-mouse_dx * SWAY_PER_PIXEL, -SWAY_MAX, SWAY_MAX);
    float ty = clampf(-mouse_dy * SWAY_PER_PIXEL, -SWAY_MAX, SWAY_MAX);
    w->sway_x += (tx - w->sway_x) * lag;
    w->sway_y += (ty - w->sway_y) * lag;

    w->bob_phase += move_speed * dt * 1.7f;
    if (w->bob_phase > M_TAU * 64.0f) w->bob_phase -= M_TAU * 64.0f;

    for (int i = 0; i < MAX_IMPACTS; i++)
        if (g_impacts[i].life > 0.0f) g_impacts[i].life -= dt;
    for (int i = 0; i < MAX_TRACERS; i++)
        if (g_tracers[i].life > 0.0f) g_tracers[i].life -= dt;
}

/* ---------------------------------------------------------- world effects */

void wp_draw_world(const Weapon *w, mat4 view_proj,
                   v3 cam_pos, v3 cam_right, v3 cam_up) {
    rd_mvp(view_proj);
    rd_mode(RD_FLAT);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);

    /* --- bullet holes: one upload, then a draw per hole so each can fade
           independently without a per-vertex colour attribute --- */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mb_reset(&g_fx_buf);

    int order[MAX_IMPACTS], n_live = 0;
    for (int i = 0; i < MAX_IMPACTS; i++) {
        if (g_impacts[i].life <= 0.0f) continue;
        v3 n = g_impacts[i].n;
        /* Any vector not parallel to n gives us a tangent basis. */
        v3 hint = (n.y > 0.9f || n.y < -0.9f) ? v3f(1, 0, 0) : v3f(0, 1, 0);
        v3 t = v3norm(v3cross(hint, n));
        v3 b = v3cross(n, t);
        mb_billboard(&g_fx_buf, g_impacts[i].p, t, b, 0.085f, 0.085f);
        order[n_live++] = i;
    }
    if (n_live) {
        mesh_upload(&g_fx_mesh, &g_fx_buf, 1);
        glBindVertexArray(g_fx_mesh.vao);
        for (int k = 0; k < n_live; k++) {
            float a = g_impacts[order[k]].life / IMPACT_LIFE;
            if (a > 1.0f) a = 1.0f;
            /* Blood stains dark red where a wall hole is near-black. */
            if (g_impacts[order[k]].blood) rd_color(0.30f, 0.02f, 0.02f, a * 0.85f);
            else                           rd_color(0.04f, 0.03f, 0.03f, a * 0.85f);
            glDrawArrays(GL_TRIANGLES, k * 6, 6);
        }
    }

    /* --- impact sparks: a brief additive flare so a hit reads instantly.
           A dark bullet hole 30m down a fogged corridor is invisible on its
           own -- the spark is what tells the player they connected. --- */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    mb_reset(&g_fx_buf);

    int sn = 0;
    for (int i = 0; i < MAX_IMPACTS; i++) {
        float age = IMPACT_LIFE - g_impacts[i].life;
        if (g_impacts[i].life <= 0.0f || age > SPARK_TIME) continue;
        /* Billboarded to the camera, and scaled with distance so a far hit
           stays legible instead of shrinking to a single pixel. */
        float dist = v3len(v3sub(g_impacts[i].p, cam_pos));
        float size = 0.12f + dist * 0.012f;
        mb_billboard(&g_fx_buf, g_impacts[i].p, cam_right, cam_up, size, size);
        order[sn++] = i;
    }
    if (sn) {
        mesh_upload(&g_fx_mesh, &g_fx_buf, 1);
        glBindVertexArray(g_fx_mesh.vao);
        for (int k = 0; k < sn; k++) {
            float age = IMPACT_LIFE - g_impacts[order[k]].life;
            float a = 1.0f - age / SPARK_TIME;
            /* A red puff on flesh, a warm spark on stone. */
            if (g_impacts[order[k]].blood) rd_color(0.90f, 0.12f, 0.10f, a * 0.9f);
            else                           rd_color(1.0f, 0.85f, 0.50f, a * 0.9f);
            glDrawArrays(GL_TRIANGLES, k * 6, 6);
        }
    }

    /* --- tracers: additive, very short lived --- */
    mb_reset(&g_line_buf);

    int tn = 0;
    for (int i = 0; i < MAX_TRACERS; i++) {
        if (g_tracers[i].life <= 0.0f) continue;
        mb_line(&g_line_buf, g_tracers[i].a, g_tracers[i].b);
        order[tn++] = i;
    }
    if (tn) {
        mesh_upload(&g_line_mesh, &g_line_buf, 1);
        glBindVertexArray(g_line_mesh.vao);
        glLineWidth(2.0f);
        for (int k = 0; k < tn; k++) {
            float a = g_tracers[order[k]].life / TRACER_LIFE;
            rd_color(1.0f, 0.82f, 0.42f, a * 0.9f);
            glDrawArrays(GL_LINES, k * 2, 2);
        }
    }

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
        v3 muzzle = muzzle_world_at(w, hook_muzzle(), cam_pos, cam_right, cam_up, fwd);

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
        mb_reset(&g_line_buf);
        mb_ribbon(&g_line_buf, muzzle, far_end, cam_pos,
                 ROPE_WIDTH, len / ROPE_TILE_LENGTH);
        mesh_upload(&g_line_mesh, &g_line_buf, 1);

        rd_mode(RD_SWATCH);
        glActiveTexture(GL_TEXTURE0);
        tex_use(&g_rope_mat);
        mesh_draw(&g_line_mesh);
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

            mb_reset(&g_fx_buf);
            mb_billboard(&g_fx_buf, far_end, r, u,
                         HOOK_CLAW_SIZE, HOOK_CLAW_SIZE);
            mesh_upload(&g_fx_mesh, &g_fx_buf, 1);

            rd_color(0.72f, 0.74f, 0.80f, 1.0f);   /* bare steel, unlit */
            mesh_draw(&g_fx_mesh);
        }
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* -------------------------------------------------------------- viewmodel */

mat4 wp_gun_matrix(const Weapon *w) {
    const GunPose *g = &g_gun_pose;

    float speed_bob = 0.010f;
    float bx = sinf(w->bob_phase)         * speed_bob;
    float by = -fabsf(cosf(w->bob_phase)) * speed_bob * 0.8f;

    v3 offset = v3f(g->off_x + bx + w->sway_x,
                    g->off_y + by + w->sway_y,
                    g->off_z + w->punch);

    mat4 rot = mat4_mul(mat4_rot_y(g->yaw + w->sway_x * SWAY_ROT),
                mat4_mul(mat4_rot_x(g->pitch + w->sway_y * SWAY_ROT
                                    + w->punch * 3.0f),
                         mat4_rot_z(w->sway_x * SWAY_ROLL)));

    /* T(offset) . T(pivot) . R . T(-pivot) . S
       The pivot is expressed in gun-local units, so it has to be scaled to
       match the already-scaled mesh that sits inside the rotation. */
    v3 pivot = v3f(0.0f, g->pivot_y * g->scale, g->pivot_z * g->scale);

    return mat4_mul(mat4_translate(offset),
             mat4_mul(mat4_translate(pivot),
               mat4_mul(rot,
                 mat4_mul(mat4_translate(v3scale(pivot, -1.0f)),
                          mat4_scale(v3f(g->scale, g->scale, g->scale))))));
}

void wp_draw_view(const Weapon *w, float aspect) {
    /* A narrower FOV than the world camera keeps the gun from looking
       fish-eyed, and a fresh depth buffer stops it clipping into walls. */
    glClear(GL_DEPTH_BUFFER_BIT);
    mat4 proj  = mat4_perspective(g_gun_pose.fov, aspect, 0.005f, 4.0f);
    mat4 model = wp_gun_matrix(w);

    rd_mvp(mat4_mul(proj, model));
    rd_mode(RD_VIEWMODEL);
    glActiveTexture(GL_TEXTURE0);

    /* One draw per material. At ~200 vertices the extra calls cost nothing,
       and this avoids a texture atlas -- which would break badly here, since
       the UVs tile several times per unit and would sample across cells. */
    for (int r = 0; r < g_gun_range_count; r++) {
        tex_use(&g_gun_tex[r]);
        mesh_draw_range(&g_gun_mesh, g_gun_ranges[r].first, g_gun_ranges[r].count);
    }

    /* --- muzzle flash: a star of quads sharing the muzzle plane, each
           rotated within it, plus one flare lying along the barrel --- */
    if (w->flash > 0.0f) {
        float k = w->flash / FLASH_TIME;
        float s = (0.16f + 0.10f * k) * g_flash_scale;

        mb_reset(&g_fx_buf);
        v3 tip = g_muzzle;
        for (int i = 0; i < 3; i++) {
            float a = g_flash_roll + i * (M_PI_F / 3.0f);
            v3 r = v3f(cosf(a), sinf(a), 0.0f);
            v3 u = v3f(-sinf(a), cosf(a), 0.0f);
            mb_billboard(&g_fx_buf, tip, r, u, s, s);
        }
        /* A short flare along the barrel sells the light better than the
           star alone. */
        mb_billboard(&g_fx_buf,
                     v3f(tip.x, tip.y, tip.z - s * 0.35f),
                     v3f(1, 0, 0), v3f(0, 0, -1), s * 0.7f, s * 1.4f);

        mesh_upload(&g_fx_mesh, &g_fx_buf, 1);
        rd_mode(RD_FLAT);
        rd_color(1.0f, 0.80f, 0.38f, k * 0.85f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        /* The star quads always face the camera, but the barrel flare is
           edge-on and its winding flips with the view -- culling would drop
           it half the time. */
        glDisable(GL_CULL_FACE);
        mesh_draw(&g_fx_mesh);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
}

/* ------------------------------------------------------------------- HUD */

void wp_draw_hud(const Weapon *w, float aspect, int hook_ready) {
    /* Drawn straight in clip space: uMVP only corrects for aspect so the
       crosshair stays square. */
    mat4 ndc = mat4_scale(v3f(1.0f / aspect, 1.0f, 1.0f));
    rd_mvp(ndc);
    rd_mode(RD_FLAT);

    /* Gap tracks spread, so the crosshair bloom is the actual accuracy. */
    float gap = 0.012f + w->spread * 1.1f;
    float len = 0.022f;

    mb_reset(&g_line_buf);
    mb_line(&g_line_buf, v3f(-gap - len, 0, 0), v3f(-gap, 0, 0));
    mb_line(&g_line_buf, v3f( gap, 0, 0),       v3f( gap + len, 0, 0));
    mb_line(&g_line_buf, v3f(0, -gap - len, 0), v3f(0, -gap, 0));
    mb_line(&g_line_buf, v3f(0,  gap, 0),       v3f(0,  gap + len, 0));
    mesh_upload(&g_line_mesh, &g_line_buf, 1);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);
    rd_color(0.95f, 0.97f, 1.0f, 0.75f);
    mesh_draw_lines(&g_line_mesh);

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

        mb_reset(&g_line_buf);
        /* Top-left, top-right, bottom-left, bottom-right: two strokes each,
           so the mark reads as a frame rather than as four more ticks.
           좌상, 우상, 좌하, 우하 각각 두 획씩입니다. 그래야 표식이 눈금 네 개가 아니라
           하나의 틀로 읽힙니다. */
        mb_line(&g_line_buf, v3f(-r, r, 0), v3f(-r + a, r, 0));
        mb_line(&g_line_buf, v3f(-r, r, 0), v3f(-r, r - a, 0));
        mb_line(&g_line_buf, v3f( r, r, 0), v3f( r - a, r, 0));
        mb_line(&g_line_buf, v3f( r, r, 0), v3f( r, r - a, 0));
        mb_line(&g_line_buf, v3f(-r, -r, 0), v3f(-r + a, -r, 0));
        mb_line(&g_line_buf, v3f(-r, -r, 0), v3f(-r, -r + a, 0));
        mb_line(&g_line_buf, v3f( r, -r, 0), v3f( r - a, -r, 0));
        mb_line(&g_line_buf, v3f( r, -r, 0), v3f( r, -r + a, 0));
        mesh_upload(&g_line_mesh, &g_line_buf, 1);

        glLineWidth(1.0f);
        rd_color(0.45f, 0.95f, 0.60f, 0.85f);   /* green: the hook will bite */
        mesh_draw_lines(&g_line_mesh);
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
