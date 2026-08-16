/**
 * @file weapon.c
 * @brief Implements the hitscan shotgun, the view model, and the effects it leaves.
 *
 * ENGLISH
 * -------
 * The shotgun: a Quake-1-style six-pellet hitscan blast and its recoil kick.
 * Plus the view model and the HUD, which are the parts of this file that
 * necessarily touch GL.
 *
 * The marks a shot leaves -- bullet holes, blood, the spark that announces a
 * hit, and tracers -- used to live here too, and are now decal.c. They were
 * spawned by whatever landed the hit and drawn a frame or six later by whoever
 * was drawing the world, and this file was holding both ends of that plus the
 * pools, the lifetimes and two hundred lines of GL in the middle. What is left
 * of them here is two calls: ::decal_hit and ::decal_tracer.
 * 사격이 남기는 자국(탄흔, 혈흔, 명중을 알리는 스파크, 예광탄)도 이곳에 있었고 이제
 * decal.c입니다. 그것들은 명중시킨 무엇이든 생성하고 한 프레임에서 여섯 프레임 뒤에 월드를
 * 그리는 누군가가 그렸는데, 이 파일이 그 양쪽 끝에 더해 풀과 수명과 그 사이의 GL 200줄을 함께
 * 들고 있었습니다. 이곳에 남은 것은 ::decal_hit과 ::decal_tracer 두 호출입니다.
 *
 * @note The grapple used to live here too and now lives in hook.c. The two
 *       shared the ::Weapon struct and the habit of pushing the player by
 *       writing to a caller-owned velocity, and nothing else -- so the split
 *       moved no logic and changed no call site. This file still DRAWS the
 *       tether and the flying claw, because drawing them needs the gun's
 *       model-space muzzle, which is a property of the weapon rather than of
 *       the hook.
 * @note ::wp_fire and the hook both push the player through a `v3 *player_vel`
 *       the caller passes in, so a shot and a grapple pull are two things doing
 *       the same kind of push rather than two mechanisms that both happen to
 *       move the player.
 *
 * 한국어
 * ------
 * 샷건입니다. Quake 1 스타일의 6발 산탄 히트스캔 사격과 그 반동, 그리고 그것이 남기는
 * 탄흔·예광탄·자국 효과입니다. 여기에 뷰 모델과 HUD가 더해지며, 이 파일에서 필연적으로
 * GL을 사용하는 부분이 그것들입니다.
 *
 * @note 그래플도 이곳에 있었으나 이제 hook.c에 있습니다. 둘은 ::Weapon 구조체와,
 *       호출자가 소유한 속도에 값을 써서 플레이어를 밀어내는 방식만을 공유했으므로,
 *       분리하면서 로직을 옮기지도 호출 지점을 바꾸지도 않았습니다. 로프와 비행 중인
 *       클로를 *그리는* 것은 여전히 이 파일입니다. 그리려면 총기의 모델 공간 총구가
 *       필요한데, 그것은 훅이 아니라 무기의 속성이기 때문입니다.
 * @note ::wp_fire와 훅은 모두 호출자가 넘겨준 `v3 *player_vel`을 통해 플레이어를
 *       밀어냅니다. 따라서 사격과 그래플 견인은 우연히 둘 다 플레이어를 움직이는 서로
 *       다른 두 장치가 아니라, 같은 종류의 밀어내기를 하는 두 가지입니다.
 */

/* No tex.h, model.h, sprite.h or post.h: this file no longer uploads, looks up
   or draws anything. That is the whole point of the split -- see weaponview.h.
   tex.h, model.h, sprite.h, post.h가 없습니다. 이 파일은 더 이상 무엇도 업로드하거나
   조회하거나 그리지 않습니다. 그것이 분리의 목적 전부입니다. weaponview.h를 참조하십시오. */
#include "weapon.h"
#include "pools.h"   /* firing reaches the projectile pool, and the blast the monsters */
#include "hook.h"
#include "audio.h"
#include "level.h"
#include "enemy.h"
#include "fx.h"       /* authored particle effects, spawned by name */
#include "diag.h"
#include "proj.h"    /* grenades and bolts: everything that travels */
#include "decal.h"   /* the marks a shot leaves; this file only spawns them */
/* player.h is deliberately absent. It was here for PLAYER_GRAVITY, which the
   grapple's pull cancels while reeling -- and the pull now lives in hook.c,
   which includes it for that reason. Nothing left in this file names a
   PLAYER_* constant, so keeping the include would leave the header graph
   describing a dependency that no longer exists. Same rule as main.c's note
   about model.h/tex.h/sprite.h.
   player.h는 의도적으로 제외했습니다. 견인이 감는 동안 상쇄하는 PLAYER_GRAVITY 때문에
   있었는데, 그 견인이 이제 hook.c에 있고 그 파일이 같은 이유로 이것을 포함합니다. 이
   파일에는 PLAYER_* 상수를 언급하는 것이 더 이상 남아 있지 않으므로, include를 남겨 두면
   헤더 그래프가 존재하지 않는 의존성을 설명하게 됩니다. model.h/tex.h/sprite.h에 대한
   main.c의 주석과 같은 규칙입니다. */

/* ------------------------------------------------------------------ tuning */

/* Quake 1 shotgun: a slow, heavy, six-pellet hitscan blast. The pellets are
   what make it a shotgun -- a single ray with a wide spread would just feel
   like an inaccurate rifle. */
#define PELLETS        6
#define PELLET_DAMAGE  7        /* six of these -- one point-blank blast -- kills */
#define FIRE_INTERVAL  0.50f

/* How much of a weapon's cooldown its recovery animation runs for.
   A SHARE rather than a duration, because every weapon now animates off this
   timer and their cooldowns span 0.085s to 0.85s -- one fixed duration would
   leave the rapid weapon mid-swing when it was ready to fire again, and the
   grenade launcher finished animating long before it was.
   무기의 재사용 대기 시간 중 회복 애니메이션이 차지하는 비율입니다. 지속 시간이 아니라
   *비율*인 이유는, 이제 모든 무기가 이 타이머로 애니메이션하고 그 대기 시간이 0.085초
   에서 0.85초에 걸쳐 있기 때문입니다. 고정된 지속 시간이라면 연사 무기는 다시 쏠 수
   있는데도 동작 중이고, 유탄 발사기는 그보다 한참 전에 동작이 끝나 있게 됩니다. */
#define PUMP_SHARE    0.55f

/* How long one pass of the idle cycle takes. Doom holds each chainsaw frame
   for 4 tics, so the pair is 8 of the 35 a second: a visible shudder rather
   than a flicker.
   대기 주기 한 바퀴에 걸리는 시간입니다. Doom은 전기톱의 각 프레임을 4틱씩 유지하므로
   한 쌍이 초당 35틱 중 8틱이며, 깜빡임이 아니라 눈에 보이는 떨림이 됩니다. */
#define IDLE_CYCLE_TIME (8.0f / 35.0f)
#define RANGE         120.0f
#define PELLET_SPREAD  0.040f   /* fixed cone, not a growing bloom */

/**
 * @brief The roster. One row per weapon; see ::WeaponType for the rules.
 *
 * ENGLISH
 * -------
 * The numbers are tuned against the bestiary rather than against each other,
 * because "is this weapon good" is not a question that has an answer on its
 * own. An imp has 40hp, a brute 120, a hound 18:
 *
 *   shotgun  6 x 7 = 42 point blank, so one blast kills an imp and three are
 *            needed for a brute. Unchanged from before this table existed.
 *   grenade  55 in a radius, so it kills a clustered pair of imps outright and
 *            takes a brute to half. Travel time is what it pays for that.
 *   rapid    9 a shot at 12/sec = 108 dps sustained, the highest here, against
 *            a magazine that empties in under four seconds of holding fire.
 *   axe      45 a swing kills an imp in one and a hound without thinking, and
 *            asks you to be within 2.2m of something trying to hit you.
 *
 * 한국어
 * ------
 * 수치는 서로가 아니라 몬스터 도감을 기준으로 조정했습니다. "이 무기가 좋은가"는 그
 * 자체로는 답이 있는 질문이 아니기 때문입니다. 임프는 체력 40, 브루트는 120, 하운드는
 * 18입니다.
 */
static const WeaponType WEAPONS[WP_TYPES] = {
    /* EVERY ROW USED TO SAY "shot". The shotgun was the only weapon when this
       table was written and a row needs some sound, so the other three
       inherited a 12-gauge blast -- a placeholder that stopped being one the
       moment there were four weapons and started actively lying: a chainsaw
       that goes off like a shotgun tells the player their weapon is something
       it is not, which is the same fault the axe's muzzle flash was.
       모든 행이 "shot"이었습니다. 이 표를 쓸 당시 무기가 샷건뿐이었고 행에는 어떤 소리든
       필요했으므로 나머지 셋이 12게이지 발사음을 물려받았습니다. 무기가 넷이 된 순간
       임시방편이기를 그만두고 적극적으로 거짓말을 시작했습니다. 샷건처럼 터지는 전기톱은
       플레이어에게 자기 무기가 아닌 것을 말하며, 도끼의 총구 섬광과 같은 결함입니다. */
    /* name       model      snd      draw     reload  start max pick dmg  cool   spread  pel  spd   grav  melee  recoil punch hook */
    { "shotgun", "shotgun", "shot",   0   ,    "pump",     20,  50,   8,   7, 0.50f, 0.040f,  6, 0.0f,  0.0f, 0.0f, 0.055f, 0.085f, 1 },

    /* Arcs and bounces, so it reaches what you cannot see. The fuse is long
       enough to bank a shot off a wall and short enough that a grenade at your
       feet is your problem.
       곡선을 그리며 튕기므로 보이지 않는 것에 닿습니다. 도화선은 벽에 튕겨 넣을 만큼
       길고, 발밑의 유탄이 스스로의 문제가 될 만큼 짧습니다. */
    { "grenade", "shotgun", "launch", 0   ,    0   ,        6,  20,   3,  55, 0.85f, 0.010f,  0, 26.0f, 26.0f, 0.0f, 0.075f, 0.130f, 1 },

    /* No hitscan: the bolts travel, so a moving target has to be led. That is
       the cost of the highest sustained damage in the roster.
       히트스캔이 아닙니다. 탄이 날아가므로 움직이는 표적은 예측 사격이 필요합니다.
       구성 내 최고 지속 피해량의 대가입니다. */
    { "rapid",   "shotgun", "plasma", 0   ,    0   ,       80, 200,  40,   9, 0.085f, 0.030f, 0, 70.0f,  0.0f, 0.0f, 0.012f, 0.022f, 1 },

    /* Melee, and the only row with no hook: right-click leaps instead. Its
       "ammo" is slam charges, which is why it is not simply free.
       근접이며 훅이 없는 유일한 행입니다. 우클릭이 대신 도약합니다. "탄약"은 내려찍기
       충전량이며, 그래서 완전히 공짜는 아닙니다. */
    { "axe",     "shotgun", "saw",    "sawup", 0   ,        3,   6,   2,  45, 0.42f, 0.0f,    0, 0.0f,  0.0f, 2.2f, 0.090f, 0.150f, 0 },
};

const WeaponType *wp_stats(int type) {
    if (type < 0 || type >= WP_TYPES) type = WP_SHOTGUN;
    return &WEAPONS[type];
}

int wp_type_for(const char *name) {
    for (int i = 0; i < WP_TYPES; i++) {
        const char *a = WEAPONS[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}

#define RECOIL_KICK    0.055f   /* radians added per shot */
#define RECOIL_RETURN  6.5f     /* springback rate */
#define PUNCH_KICK     0.085f   /* metres the view model recoils */
#define PUNCH_RETURN   7.0f
/* FLASH_TIME moved to weapon.h: firing sets w->flash against it and the view
   model fades the flash by it, so the two halves of the split both need it.
   FLASH_TIME은 weapon.h로 옮겼습니다. 발사가 이 값을 기준으로 w->flash를 설정하고 뷰
   모델이 그 값으로 화염을 사라지게 하므로, 분리된 두 절반이 모두 필요로 합니다. */

/* Gun-local space: +x right, +y up, the barrel points down -z.
   The muzzle now comes from the model (see w->muzzle below), so redrawing the
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

/* ------------------------------------------------------------ module state */

/* --- File-local types / 파일 지역 타입 --- */

/* WHAT USED TO BE HERE. Sixteen file-scope variables: the level, the gun mesh
   and its materials, the muzzle, the per-frame camera and two vertex buffers.
   Eleven of them were the DRAWN gun and moved to ::WeaponView in weaponview.c;
   the other five were properties of this weapon in this run and moved into
   ::Weapon itself. Nothing mutable is left at file scope in this file.

   이곳에 있던 것. 열여섯 개의 파일 스코프 변수입니다. 레벨, 총기 메시와 재질, 총구,
   프레임별 카메라, 그리고 정점 버퍼 둘입니다. 그중 열한 개는 *그려지는* 총이며
   weaponview.c의 ::WeaponView로 옮겼습니다. 나머지 다섯은 이번 플레이의 이 무기에 속한
   성질이며 ::Weapon 자신으로 옮겼습니다. 이 파일의 파일 스코프에는 가변 상태가 하나도
   남아 있지 않습니다. */

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
static int trace(const Level *lv, v3 o, v3 d, float *out_t, v3 *out_n) {
    if (!lv) return 0;
    if (level_trace(lv, o, d, RANGE, out_t, out_n)) return 1;
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

void wp_start_belt(Weapon *w) {
    /* Every weapon's own belt, and only the shotgun is in hand at the start.
       A roster the player is handed complete has no pickups worth finding.
       무기마다 자신의 탄약이며, 시작 시 손에 든 것은 샷건뿐입니다. 처음부터 전부 쥐여 준
       구성에는 찾을 가치가 있는 아이템이 없습니다. */
    for (int i = 0; i < WP_TYPES; i++) {
        w->ammo[i]  = 0;
        w->owned[i] = 0;
    }
    w->cur = WP_SHOTGUN;
    w->owned[WP_SHOTGUN] = 1;
    w->ammo[WP_SHOTGUN]  = WEAPON_START_AMMO;
}

void wp_init(Weapon *w) {
    Weapon zero = {0};
    *w = zero;
    w->rng = 0x2545f491u;

    wp_start_belt(w);

    /* Not zero: 0 is a valid monster index, so a zeroed struct would read as
       "hooked to monster 0" and deal damage to a bystander on the first
       arrival. HOOK_IDLE makes that unreachable, but the field should still
       say what it means.
       0이 아닙니다. 0은 유효한 몬스터 인덱스이므로, 0으로 초기화된 구조체는 "0번
       몬스터에 걸림"으로 읽혀 첫 도달 시 무관한 대상에게 피해를 줄 수 있습니다.
       HOOK_IDLE이 이를 도달 불가능하게 만들지만, 필드 자체가 의미를 말해야 합니다. */
    w->hook_enemy = -1;

    /* The muzzle a model has not been loaded for yet. wpview_set_model
       overwrites it with the real one the moment there is a context to load a
       model in; until then this is where a headless fixture's shots come from.
       모델이 아직 로드되지 않은 상태의 총구입니다. 모델을 로드할 컨텍스트가 생기는 즉시
       wpview_set_model이 실제 값으로 덮어씁니다. 그 전까지는 헤드리스 픽스처의 사격이
       나가는 자리가 이곳입니다. */
    w->muzzle = WP_MUZZLE_DEFAULT;

    /* Defaults until the first wp_update reports the real camera. */
    w->world_fov = 1.5708f;
    w->aspect    = 1.777f;
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
 * @note Takes an arbitrary gun-local point rather than hardcoding ::w->muzzle,
 *       so the same projection serves both the barrel (tracers) and the hook
 *       launcher slung underneath it (the tether) without two copies of this
 *       maths.
 * @warning Reads ::w->world_fov and ::w->aspect, which ::wp_update sets. Calling
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
v3 wp_muzzle_world_at(const Weapon *w, v3 local, v3 eye, v3 right, v3 up, v3 fwd) {
    v3 mv = mat4_mul_pt(wp_gun_matrix(w), local);
    /* A point at or behind the near plane would divide by ~0 and fling the
       result off screen.
       근평면에 있거나 그 뒤에 있는 점은 0에 가까운 값으로 나누게 되어 결과가
       화면 밖으로 튕겨 나갑니다. */
    if (mv.z > -1e-3f) mv.z = -1e-3f;               /* must stay in front */

    /* Perspective divide under the VIEW MODEL's projection, yielding NDC.
       뷰 모델의 투영을 기준으로 원근 나눗셈을 수행하여 NDC를 얻습니다. */
    float t_vm  = tanf(g_gun_pose.fov * 0.5f);
    float ndc_x = (mv.x / -mv.z) / (t_vm * w->aspect);
    float ndc_y = (mv.y / -mv.z) / t_vm;

    /* Rebuild a world point at a fixed distance under the WORLD projection.
       The distance is arbitrary -- any value puts the effect on the same
       screen pixel; this one simply keeps it clear of the near plane.
       월드 투영을 기준으로 고정 거리에 월드 좌표를 재구성합니다. 이 거리는
       임의의 값입니다. 어떤 값이든 효과는 화면상 동일한 픽셀에 놓이며, 이 값은
       단지 근평면에서 충분히 떨어뜨리기 위한 것입니다. */
    const float dist = 0.7f;
    float t_w = tanf(w->world_fov * 0.5f);

    return v3add(eye,
             v3add(v3scale(right, ndc_x * t_w * w->aspect * dist),
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
    return wp_muzzle_world_at(w, w->muzzle, eye, right, up, fwd);
}

/* Where the hook launches from, in gun-local units: the main barrel's own
   muzzle, dropped and pulled back a little, as if a separate launcher were
   slung underneath it. Purely a visual anchor for the tether -- the aim
   itself still traces from the eye (see wp_hook_fire), the same way the
   shotgun's own pellets do, so the crosshair stays honest about what gets
   hooked regardless of where the tether appears to leave the gun. */
v3 wp_hook_muzzle(const Weapon *w) {
    return v3f(w->muzzle.x, w->muzzle.y - HOOK_MUZZLE_DROP, w->muzzle.z + HOOK_MUZZLE_BACK);
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
static void fire_hitscan(Weapon *w, Pools *pl, const Level *l,
                         v3 eye, float yaw, float pitch,
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
    /* Where the volley's impact sound comes from. Starts at the muzzle so a
       miss has somewhere sane to point, though a miss plays nothing.
       일제사격의 타격음이 나는 자리입니다. 빗나감이 가리킬 곳이 있도록 총구에서
       시작하지만, 빗나가면 아무 소리도 나지 않습니다. */
    v3 hit_at = eye;

    for (int i = 0; i < PELLETS; i++) {
        v3 dir = v3norm(v3add(fwd,
                    v3add(v3scale(right, frand_signed(w) * PELLET_SPREAD),
                          v3scale(up,    frand_signed(w) * PELLET_SPREAD))));

        float t; v3 n;
        int hit = trace(l, eye, dir, &t, &n);

        /* A monster nearer than the wall stops the pellet. The trace above
           gives the wall distance (or RANGE on a miss); anything the enemy
           hitscan finds inside that is what the pellet actually strikes. */
        float et; int eidx;
        int blood = enemy_hitscan(pl, eye, dir, hit ? t : RANGE, &et, &eidx);

        if (blood) {
            enemy_hurt(pl, eidx, PELLET_DAMAGE, dir);
            t = et;
            hit = 1;
        }
        hits += hit;
        v3 end = v3add(eye, v3scale(dir, t));
        if (hit) hit_at = end;

        if (hit) {
            /* Where the mark went is decal.c's decision -- blood is pulled
               back toward the shooter, a wall mark is nudged off the surface --
               and it is handed back so the authored effect below lands on the
               same point instead of recomputing the same offset here.
               자국이 어디로 갔는지는 decal.c의 결정입니다(피는 사수 쪽으로 당겨지고, 벽의
               자국은 표면에서 살짝 띄워집니다). 그리고 그 자리를 돌려받으므로, 아래의 제작된
               이펙트가 이곳에서 같은 오프셋을 다시 계산하는 대신 같은 지점에 놓입니다. */
            DecalPlace at = decal_hit(pl, end, dir, n, blood);

            /* The authored half of the same hit. The built-in mark and spark
               above stay: they are the shotgun's own feedback and are tuned
               against its fire rate. This adds whatever assets\effects.txt
               says a hit on this surface throws off, so the look can be
               retuned without a rebuild.
               같은 명중의 제작된 쪽입니다. 위의 내장 자국과 스파크는 그대로 둡니다.
               그것은 샷건 자체의 피드백이며 발사 속도에 맞춰 조정된 것입니다. 여기서는
               assets\effects.txt가 이 표면 명중이 무엇을 튀기는지 정의한 대로 추가하므로,
               재빌드 없이 룩을 조정할 수 있습니다. */
            fx_spawn(pl, blood ? "blood" : "spark", at.p, at.n);
            /* Stone gets the other three layers TE_SPIKE has -- the puff and
               the chip -- which are what make a hit look like it happened TO
               the surface rather than in front of it. Flesh does not: a puff
               of dust off a monster reads as missing it.
               돌에는 TE_SPIKE가 가진 나머지 층인 퍼프와 파편이 붙습니다. 피탄이 표면
               *앞*이 아니라 표면 *에* 일어난 것처럼 보이게 만드는 것이 그것입니다.
               살에는 붙이지 않습니다. 몬스터에서 이는 먼지는 빗맞은 것으로 읽힙니다. */
            if (!blood) {
                fx_spawn(pl, "smokepuff", at.p, at.n);
                fx_spawn(pl, "debris",    at.p, at.n);
            }
        }

        decal_tracer(pl, muzzle, end);
    }

    audio_play("shot", 100);

    /* One impact for the whole volley, louder when more pellets connect.
       Six separate impact sounds fired on the same frame just smear into
       noise and steal every voice.
       Placed where the pellets LANDED rather than at the muzzle: a hitscan
       carries to 120m, and an impact that played at full volume regardless
       told you that you had hit something without telling you how far off it
       was -- which for the one weapon that reaches across a level is most of
       what the sound was for.
       총구가 아니라 산탄이 *떨어진* 자리에서 재생합니다. 히트스캔은 120m까지 닿는데,
       거리와 무관하게 최대 음량으로 나는 타격음은 무언가를 맞혔다는 사실만 알려 주고
       그것이 얼마나 멀리 있었는지는 알려 주지 않습니다. 레벨을 가로지르는 유일한
       무기에게는 그것이 이 소리의 존재 이유의 대부분입니다. */
    if (hits) audio_play_at("impact", 35 + hits * 8, hit_at);

}

/**
 * @brief The feedback every attack shares: cooldown, kick, flash.
 *
 * ENGLISH
 * -------
 * Pulled out of the shotgun's fire path when there were four weapons rather
 * than one. The numbers come from the table, but WHICH numbers exist -- a
 * cooldown, a camera kick, a view model punch, a flash -- is a property of
 * attacking rather than of any one weapon, and a copy of this per weapon is
 * four places for a springback to be tuned differently by accident.
 *
 * 한국어
 * ------
 * 무기가 하나에서 넷이 되면서 샷건의 사격 경로에서 추출했습니다. 수치는 표에서 오지만,
 * *어떤* 수치가 존재하는지(재사용 대기시간, 카메라 반동, 뷰 모델 후퇴, 화염)는 특정
 * 무기가 아니라 공격 자체의 속성입니다. 무기마다 사본을 두면 복원 속도가 실수로 다르게
 * 조정될 곳이 네 군데가 됩니다.
 */
static void attack_feedback(Weapon *w, const WeaponType *S) {
    w->cooldown = S->cooldown;
    w->recoil  += S->recoil * (0.85f + frand(w) * 0.3f);
    w->punch   += S->punch;

    /* A MUZZLE FLASH NEEDS A MUZZLE. The axe had one because this line did not
       ask, and a burst of fire at the end of a swinging blade reads as the
       weapon discharging -- it tells the player the wrong thing about what
       their weapon is. `melee_range` is already the field that says a weapon
       has no barrel, so it decides here too rather than a second flag that can
       disagree with it.
       The animation is unaffected: `pump_timer` below is the clock every
       weapon's cycle runs on, and the flash fallback in weapon_sprite_frame()
       only applies when that clock is stopped.
       총구 화염에는 총구가 필요합니다. 이 줄이 묻지 않았기 때문에 도끼에도 있었고,
       휘두르는 날 끝의 불꽃은 무기가 발사된 것으로 읽힙니다. 플레이어에게 자기 무기가
       무엇인지 틀리게 말하는 것입니다. `melee_range`가 이미 총열이 없다는 것을 말하는
       필드이므로, 그것과 어긋날 수 있는 두 번째 플래그가 아니라 그것이 여기서도
       결정합니다. 애니메이션에는 영향이 없습니다. 아래의 `pump_timer`가 모든 무기의
       주기가 도는 시계이고, 화염 대체 경로는 그 시계가 멈춰 있을 때만 쓰입니다. */
    if (S->melee_range <= 0) w->flash = FLASH_TIME;

    /* The recovery animation, for every weapon rather than the shotgun alone.
       It lands partway through the cooldown rather than on the trigger pull,
       which is what gives a pump its rhythm and a spin its follow-through.
       회복 애니메이션이며, 샷건만이 아니라 모든 무기의 것입니다. 방아쇠를 당기는 순간이
       아니라 대기 시간의 중간까지 이어지며, 그것이 펌프에 리듬을, 회전에 여운을
       줍니다. */
    w->pump_timer = S->cooldown * PUMP_SHARE;

    w->flash_scale = 1.1f + frand(w) * 0.7f;
    w->flash_roll  = frand(w) * M_TAU;
}

/**
 * @brief Launches one projectile along the aim.
 *
 * ENGLISH
 * -------
 * The grenade and the rapid weapon are the same code with different table
 * rows: `proj_gravity` decides whether what leaves the barrel arcs and bounces
 * or flies flat, and `spread` decides whether it goes exactly where you
 * pointed. See proj.h.
 *
 * @note Launched from the EYE, not the drawn muzzle, for the reason the
 *       hitscan pellets are traced from the eye: the crosshair has to be
 *       honest. A grenade that left the barrel would arc from a point below
 *       and right of where the player is looking, and short throws would miss
 *       low for no visible reason.
 *
 * 한국어
 * ------
 * 유탄과 연사 무기는 표의 행만 다른 같은 코드입니다. `proj_gravity`가 총구를 떠난 것이
 * 곡선을 그리며 튕길지 평평하게 날지를 결정하고, `spread`가 정확히 겨눈 곳으로 갈지를
 * 결정합니다.
 *
 * @note 그려진 총구가 아니라 *눈*에서 발사합니다. 히트스캔 산탄을 눈에서 추적하는 것과
 *       같은 이유이며, 조준점이 정직해야 하기 때문입니다. 총구에서 떠나는 유탄은
 *       플레이어가 보는 지점의 아래·오른쪽에서 호를 그리게 되고, 짧은 투척이 뚜렷한 이유
 *       없이 아래로 빗나갑니다.
 */
static void fire_projectile(Weapon *w, Pools *pl, const WeaponType *S,
                            v3 eye, float yaw, float pitch) {
    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    v3 fwd   = v3f(-sy * cp, sp, -cy * cp);
    v3 right = v3f(cy, 0, -sy);
    v3 up    = v3cross(right, fwd);

    v3 dir = fwd;
    if (S->spread > 0.0f)
        dir = v3norm(v3add(fwd,
                  v3add(v3scale(right, frand_signed(w) * S->spread),
                        v3scale(up,    frand_signed(w) * S->spread))));

    /* A grenade carries a fuse and a blast; a bolt carries neither and hurts
       exactly what it touches. One table row is the whole difference.
       유탄은 도화선과 폭발 반경을 가지고, 탄은 둘 다 없이 닿은 것만 정확히 상하게
       합니다. 표의 한 행이 차이의 전부입니다. */
    int arcs = S->proj_gravity > 0.0f;
    proj_fire(pl, eye, dir, S->proj_speed, S->proj_gravity, S->damage,
              arcs ? PROJ_BLAST_RADIUS : 0.0f,
              arcs ? PROJ_FUSE : 0.0f);

    /* Smoke at the muzzle, for the launcher only. `arcs` is already the field
       that tells a grenade from a bolt, so it decides this too -- a bolt is
       energy and leaves nothing behind, and giving it a puff would say it
       burns propellant, which is a claim about the weapon that is not true.
       Thrown a little ahead of the eye so it does not hang in the near plane,
       and along the aim so it drifts the way the shot went.
       총구의 연기이며 유탄 발사기에만 붙습니다. `arcs`가 이미 유탄과 탄을 구분하는
       필드이므로 이것도 그것이 결정합니다. 탄은 에너지이고 아무것도 남기지 않으며, 퍼프를
       주면 추진제를 태운다고 말하게 되는데 그것은 이 무기에 대한 사실이 아닙니다. 근평면에
       걸리지 않도록 눈보다 조금 앞에, 발사한 방향으로 흐르도록 조준선을 따라 던집니다. */
    if (arcs) fx_spawn(pl, "smokepuff", v3add(eye, v3scale(dir, 0.6f)), dir);

    audio_play(S->fire_snd, arcs ? 100 : 70);
}

/**
 * @brief A swing, and the lunge that carries it.
 *
 * ENGLISH
 * -------
 * @param[in,out] player_vel Receives the dash.
 *
 * The dash is the point. A melee weapon in a game about momentum cannot ask
 * the player to walk into range at walking speed -- everything else here moves
 * faster than that, so the axe would only ever hit things that were already on
 * top of you. Swinging THROWS you at what you are looking at, which turns
 * closing the distance from a cost into the attack itself.
 *
 * @note The dash fires whether or not the swing connects, for the same reason
 *       the shotgun kick does: it is a movement the player asked for, not a
 *       reward for accuracy. Conditional, the axe would move you only when you
 *       did not need it to.
 *
 * 한국어
 * ------
 * 대쉬가 핵심입니다. 운동량을 다루는 게임의 근접 무기가 플레이어에게 걷는 속도로 사거리
 * 안에 들어오라고 요구할 수는 없습니다. 이곳의 다른 모든 것이 그보다 빠르므로, 도끼는
 * 이미 코앞에 있는 것만 때리게 됩니다. 휘두르기가 바라보는 대상 쪽으로 *던져 주면*,
 * 거리를 좁히는 일이 비용이 아니라 공격 자체가 됩니다.
 *
 * @note 명중 여부와 무관하게 대쉬가 발동합니다. 샷건의 반동과 같은 이유로, 플레이어가
 *       요청한 이동이지 명중에 대한 보상이 아니기 때문입니다.
 */
static void fire_melee(Weapon *w, Pools *pl, const WeaponType *S,
                       v3 eye, float yaw, float pitch, v3 *player_vel) {
    (void)w;
    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    v3 fwd = v3f(-sy * cp, sp, -cy * cp);

    /* Forward along the aim, with the vertical component damped: looking up
       and swinging should carry you at the thing, not launch you over it.
       조준 방향으로 전진하되 수직 성분은 감쇠합니다. 위를 보고 휘두르는 것이 대상을 넘어
       날아가는 것이 아니라 대상 쪽으로 데려가야 합니다. */
    v3 dash = v3f(fwd.x, fwd.y * 0.35f, fwd.z);
    *player_vel = v3add(*player_vel, v3scale(v3norm(dash), AXE_DASH_SPEED));

    /* The swing itself: the nearest monster within reach along the aim. A
       hitscan over a short range IS a swing -- the arc is an animation, and a
       real swept volume would miss things the drawing clearly passed through.
       휘두르기 자체입니다. 짧은 사거리에 대한 히트스캔이 곧 휘두르기입니다. 호는
       애니메이션이며, 실제 스윕 판정은 그림이 분명히 지나간 것을 빗나가게 됩니다. */
    float et; int eidx;
    /* A SWING THAT CONNECTS AND ONE THAT DOES NOT ARE DIFFERENT EVENTS, and
       the player has to be able to tell them apart without waiting for a
       health bar to move. So the swing always throws a thin spray along the
       aim -- the saw is running -- and contact adds the hard cone of sparks
       and the blood, at the point the blade actually reached.
       맞은 스윙과 빗나간 스윙은 서로 다른 사건이며, 플레이어는 체력 표시가 움직이기를
       기다리지 않고 둘을 구분할 수 있어야 합니다. 그래서 스윙은 언제나 조준 방향으로
       옅은 분사를 던지고(톱이 돌고 있다는 뜻입니다), 접촉은 날이 실제로 닿은 지점에
       단단한 불꽃 원뿔과 피를 더합니다. */
    v3 reach = v3add(eye, v3scale(fwd, S->melee_range * 0.6f));
    fx_spawn(pl, "sawspark", reach, fwd);

    if (enemy_hitscan(pl, eye, fwd, S->melee_range, &et, &eidx)) {
        enemy_hurt(pl, eidx, S->damage, fwd);
        v3 bite = v3add(eye, v3scale(fwd, et));
        /* Sparks back ALONG the blade rather than away from it: steel grinding
           into something throws them at the person holding it, and thrown
           forward they read as a muzzle flash, which is the thing this weapon
           should not have.
           날에서 멀어지는 방향이 아니라 날을 *따라 되돌아오는* 방향입니다. 무언가에
           갈리는 강철은 그것을 쥔 사람 쪽으로 불꽃을 던지며, 앞으로 던지면 총구 섬광처럼
           읽힙니다. 이 무기가 가져서는 안 되는 바로 그것입니다. */
        fx_spawn(pl, "sawgrind", bite, v3scale(fwd, -1.0f));
        fx_spawn(pl, "blood",    bite, v3scale(fwd, -1.0f));
        /* The saw's own bite rather than the generic impact. Doom separates
           DSSAWFUL from DSSAWHIT for exactly the reason the sparks are
           separate: the swing and the connection are different events, and
           hearing them as one costs the player the feedback that they landed
           it.
           일반 타격음이 아니라 톱 자신의 절삭음입니다. Doom이 DSSAWFUL과 DSSAWHIT를
           나누는 이유는 불꽃을 나눈 이유와 같습니다. 휘두름과 명중은 서로 다른 사건이며,
           하나로 들리면 플레이어는 맞혔다는 피드백을 잃습니다. */
        audio_play_at("sawhit", 95, bite);
    }
    audio_play(S->fire_snd, 80);
}

/**
 * @brief Routes a trigger pull to whichever kind of attack this weapon is.
 *
 * @note Exactly one of `pellets`, `proj_speed` and `melee_range` is non-zero
 *       per table row, so the order of these tests does not matter -- and
 *       tools/weapontest.c asserts that so it stays true.
 *
 * @brief 방아쇠를 이 무기가 어떤 종류의 공격인지에 따라 분배합니다.
 * @note 표의 행마다 `pellets`, `proj_speed`, `melee_range` 중 정확히 하나만 0이 아니므로
 *       검사 순서는 중요하지 않습니다. tools/weapontest.c가 그것이 유지되도록 단언합니다.
 */
static void attack(Weapon *w, Pools *pl, const Level *l,
                   v3 eye, float yaw, float pitch,
                   v3 *player_vel, int player_grounded) {
    const WeaponType *S = wp_stats(w->cur);

    if (S->pellets > 0)          fire_hitscan(w, pl, l, eye, yaw, pitch, player_vel, player_grounded);
    else if (S->proj_speed > 0)  fire_projectile(w, pl, S, eye, yaw, pitch);
    else if (S->melee_range > 0) fire_melee(w, pl, S, eye, yaw, pitch, player_vel);

    attack_feedback(w, S);
}


void wp_update(Weapon *w, Pools *pl, const Level *l,
               float dt, int firing, v3 eye, float yaw, float pitch,
               float move_speed, float mouse_dx, float mouse_dy,
               float world_fov, float aspect, v3 *player_vel, int player_grounded) {
    w->world_fov = world_fov;
    w->aspect    = aspect;

    /* Wrapped rather than left to grow: a float that has been accumulating for
       an hour loses the precision a 0.23s cycle needs.
       무한정 커지게 두지 않고 되감습니다. 한 시간 동안 누적된 float은 0.23초 주기가
       필요로 하는 정밀도를 잃습니다. */
    w->anim_clock += dt;
    if (w->anim_clock > IDLE_CYCLE_TIME) w->anim_clock -= IDLE_CYCLE_TIME;

    if (w->cooldown      > 0.0f) w->cooldown      -= dt;
    if (w->hook_cooldown > 0.0f) w->hook_cooldown -= dt;
    if (w->flash         > 0.0f) w->flash         -= dt;

    if (w->pump_timer > 0.0f) {
        w->pump_timer -= dt;
        if (w->pump_timer <= 0.0f) {
            const char *snd = wp_stats(w->cur)->reload_snd;
            if (snd) audio_play(snd, 70);
        }
    }
    if (w->dry_timer > 0.0f) w->dry_timer -= dt;

    if (firing && w->cooldown <= 0.0f) {
        if (w->ammo[w->cur] > 0) {
            w->ammo[w->cur]--;
            attack(w, pl, l, eye, yaw, pitch + w->recoil, player_vel, player_grounded);
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

    /* Aged here rather than beside fx_update, so the marks freeze exactly when
       the weapon does. They are this frame's shots getting older, and the frame
       that does not advance the shot must not advance its scar either.
       fx_update 옆이 아니라 이곳에서 노화시킵니다. 그래야 무기가 멈추는 바로 그때 자국도
       멈춥니다. 자국은 이번 프레임의 사격이 나이를 먹는 것이며, 사격을 진행시키지 않는
       프레임은 그 흔적도 진행시켜서는 안 됩니다. */
    decal_update(pl, dt);
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

/**
 * @brief Which drawn frame the gun is on, from the gun's own timers.
 *
 * ENGLISH
 * -------
 * @param[in] w The weapon.
 * @return One of the WPN_* frames.
 *
 * @note Read from the timers that already exist rather than from an animation
 *       clock of its own. A second clock would have to be advanced in step with
 *       firing, and the failure when it drifts is a gun whose picture disagrees
 *       with what it is doing -- pumping while idle, or still while it shoots.
 *       The monsters pick their frames from ::EState for the same reason.
 *
 * 한국어
 * ------
 * @brief 총기 자신의 타이머로부터 현재 그려질 프레임을 결정합니다.
 * @return WPN_* 프레임 중 하나.
 *
 * @note 자체 애니메이션 시계가 아니라 이미 존재하는 타이머에서 읽습니다. 두 번째 시계는
 *       발사와 보조를 맞춰 진행되어야 하며, 어긋났을 때의 증상은 그림이 실제 동작과
 *       불일치하는 총기입니다. 대기 중에 펌프질을 하거나, 발사하는데 가만히 있는 식입니다.
 *       몬스터가 ::EState에서 프레임을 고르는 것과 같은 이유입니다.
 */
/* WHAT EACH WEAPON'S DRAWINGS ARE, AND WHEN EACH SHOWS.
 *
 * A slot in the atlas is just a drawing; which drawing means something
 * different per weapon, because Doom drew each weapon with the frames that
 * weapon needed. So the meanings live here, next to the cycles that use them,
 * rather than as one shared enum that every weapon would have to pretend to
 * fit. The counts differ on purpose -- padding the chaingun out to the
 * chainsaw's four would store a duplicate to fill a slot.
 *
 * 아틀라스의 슬롯은 그저 그림이고, *어느* 그림인지는 무기마다 다릅니다. Doom이 각 무기를
 * 그 무기에 필요한 프레임으로 그렸기 때문입니다. 그래서 의미를 모든 무기가 억지로 맞춰야
 * 하는 공유 열거형이 아니라, 그것을 쓰는 주기 옆인 이곳에 둡니다. */
/* A CYCLE IS A TABLE: which drawing, and how far through the action it lasts.
 *
 * Doom's shotgun goes B -> C -> D -> C -> B, and the middle of that is the
 * point: the gun passes through the SAME pose going out and coming back. Two
 * `if` branches could not express it without a third drawing to return to,
 * which is why the atlas used to carry two identical images. A table repeats a
 * pose by naming it twice, and costs nothing per repeat.
 *
 * Fractions of the action rather than seconds, so retuning a weapon's rate
 * keeps its animation's shape and changes only its speed -- a table in seconds
 * would run past the end of a faster action and freeze on its last row.
 *
 * 주기는 표입니다. 어느 그림을, 동작의 어디까지 보일지입니다. Doom의 샷건은
 * B -> C -> D -> C -> B로 가며 핵심은 그 가운데입니다. 총이 나갈 때와 돌아올 때 같은
 * 자세를 지납니다. 초가 아니라 동작에 대한 비율인 이유는, 무기의 속도를 조정해도
 * 애니메이션의 모양은 유지되고 속도만 바뀌게 하기 위함입니다. */
typedef struct { unsigned char frame; float upto; } AnimStep;

/* TRANSCRIBED FROM DOOM'S STATE TABLE, not chosen by looking at the drawings.
 *
 * info.c gives each weapon a chain of states, each naming a frame and a
 * duration in tics; these are those chains as fractions of the recovery. Two
 * things were got wrong by reading the ART instead, and both shipped:
 *
 *   The shotgun idled on its FIRST PUMP FRAME, because SHTGA0 -- the real
 *   idle, S_SGUN with A_WeaponReady -- is drawn as little more than the end of
 *   a barrel and had been dropped as unusable. The pump is A B C D C B A: it
 *   goes out and comes back, and only the middle of that was being played.
 *
 *   The chainsaw had its idle and its cut SWAPPED. SAWG C and D are
 *   S_SAW/S_SAWB, the frames A_WeaponReady alternates between; A and B are
 *   S_SAW1/S_SAW2, the frames A_Saw alternates between. C and D are the wider
 *   drawings, which reads as the lunge and is not.
 *
 * Doom의 상태 표에서 옮긴 것이며 그림을 보고 고른 것이 아닙니다. info.c는 무기마다
 * 프레임과 지속 시간(tic)을 명시한 상태의 사슬을 주며, 아래는 그 사슬을 회복 시간에
 * 대한 비율로 옮긴 것입니다. 아트를 보고 판단해 두 가지를 틀렸고 둘 다 배포되었습니다.
 * 샷건은 첫 펌프 프레임으로 대기했고, 전기톱은 대기와 절단이 서로 뒤바뀌어 있었습니다. */

/* A(3) B(7) C(5) D(4) C(5) B(5) A(3) A(7) -- 39 tics, out and back. */
static const AnimStep CYCLE_SHOTGUN[] = {
    { SG_IDLE,  0.08f },
    { SG_PUMP0, 0.26f },
    { SG_PUMP1, 0.38f },
    { SG_PUMP2, 0.49f },   /* pump fully back */
    { SG_PUMP1, 0.62f },   /* and the same drawings again, coming forward */
    { SG_PUMP0, 0.74f },
    { SG_IDLE,  1.00f },
};
/* B(8) B(12): the launcher holds one firing drawing for the whole shot. */
static const AnimStep CYCLE_GRENADE[] = {
    { LN_FIRE, 0.40f },
    { LN_FIRE, 0.90f },
    { LN_IDLE, 1.00f },
};
/* A(4) B(4): both states fire, so the alternation IS the rate of fire. */
static const AnimStep CYCLE_RAPID[] = {
    { RP_IDLE, 0.50f },
    { RP_SPIN, 1.00f },
};
/* A(4) B(4), A_Saw. The bite, not the rev. */
static const AnimStep CYCLE_AXE[] = {
    { AX_CUT0, 0.50f },
    { AX_CUT1, 1.00f },
};

/* AND AN IDLE THAT IS ITSELF AN ANIMATION, for the one weapon that has one.
 *
 * A_WeaponReady shows a single frame for three of these weapons and alternates
 * two for the chainsaw -- a saw you are holding revs. Modelling "at rest" as a
 * single drawing cannot express that, so rest is a cycle too, and three of the
 * four simply have one row.
 *
 * 그리고 그 자체가 애니메이션인 대기 자세입니다. A_WeaponReady는 세 무기에 대해서는
 * 프레임 하나를 보이고 전기톱에 대해서는 둘을 번갈아 보입니다. 들고 있는 톱은 떨기
 * 때문입니다. "쉬는 중"을 그림 하나로 모델링하면 그것을 표현할 수 없으므로 대기도
 * 주기이며, 넷 중 셋은 그저 행이 하나일 뿐입니다. */
static const AnimStep IDLE_SHOTGUN[] = { { SG_IDLE, 1.00f } };
static const AnimStep IDLE_GRENADE[] = { { LN_IDLE, 1.00f } };
static const AnimStep IDLE_RAPID[]   = { { RP_IDLE, 1.00f } };
static const AnimStep IDLE_AXE[]     = { { AX_REV0, 0.50f }, { AX_REV1, 1.00f } };

/* Rows, keyed by WP_*, so adding a weapon adds a line here beside its line in
   WEAPONS rather than a branch somewhere else.
   WP_*로 색인되는 행입니다. 무기를 추가하면 다른 곳의 분기가 아니라 WEAPONS의 자기 줄
   옆에 이 줄 하나가 늘어납니다. */
static const struct {
    const AnimStep *step;
    int             n;
    const AnimStep *idle;
    int             idle_n;
} ANIM[WP_TYPES] = {
    { CYCLE_SHOTGUN, 7, IDLE_SHOTGUN, 1 },
    { CYCLE_GRENADE, 3, IDLE_GRENADE, 1 },
    { CYCLE_RAPID,   2, IDLE_RAPID,   1 },
    { CYCLE_AXE,     2, IDLE_AXE,     2 },
};


static const AnimStep *anim_pick(const AnimStep *step, int n, float f) {
    for (int i = 0; i < n; i++)
        if (f < step[i].upto) return &step[i];
    return &step[n - 1];
}

int wp_sprite_frame(const Weapon *w) {
    int t = (w->cur >= 0 && w->cur < WP_TYPES) ? w->cur : 0;

    /* The pump timer is the animation clock for every weapon, not just the
       shotgun: it is already set to the weapon's own recovery and already
       counts down in step with firing. A second clock per weapon would be a
       second thing to keep aligned with the trigger, and the failure when it
       drifts is a gun whose picture disagrees with what it is doing.
       펌프 타이머는 샷건만이 아니라 모든 무기의 애니메이션 시계입니다. 이미 무기 자신의
       회복 시간으로 설정되고 발사와 보조를 맞춰 감소합니다. */
    if (w->pump_timer > 0.0f) {
        float len = wp_stats(t)->cooldown * PUMP_SHARE;
        float f = (len > 0.0f) ? 1.0f - w->pump_timer / len : 1.0f;
        return anim_pick(ANIM[t].step, ANIM[t].n, f)->frame;
    }

    /* A weapon that flashes without running its cycle still has to look like
       it fired, so fall back to the cycle's first drawing rather than to rest.
       주기를 돌리지 않고 화염만 내는 무기도 발사한 것처럼 보여야 하므로, 대기 자세가
       아니라 주기의 첫 그림으로 대체합니다. */
    if (w->flash > 0.0f) return ANIM[t].step[0].frame;

    /* At rest, which for the chainsaw is not a single drawing. anim_clock runs
       whether or not the player moves, because a saw revs while you stand
       still -- bob_phase would have been free but it stops when you do.
       쉬는 중이며, 전기톱에게는 그것이 그림 하나가 아닙니다. anim_clock은 플레이어가
       움직이든 아니든 흐릅니다. 가만히 서 있어도 톱은 떨기 때문입니다. bob_phase를 쓸
       수도 있었지만 그것은 플레이어가 멈추면 함께 멈춥니다. */
    float p = w->anim_clock / IDLE_CYCLE_TIME;
    p -= (float)(int)p;
    return anim_pick(ANIM[t].idle, ANIM[t].idle_n, p)->frame;
}



/* ------------------------------------------------------------- the axe leap */

int wp_axe_leaping(const Weapon *w) { return w->leaping; }

int wp_axe_leap(Weapon *w, float yaw, float pitch, v3 *player_vel) {
    if (w->cur != WP_AXE) return 0;
    if (w->leaping) return 0;

    const WeaponType *S = wp_stats(WP_AXE);
    if (w->ammo[WP_AXE] <= 0) {
        if (w->dry_timer <= 0.0f) { audio_play("dry", 55); w->dry_timer = 0.35f; }
        return 0;
    }
    (void)S;

    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    v3 fwd = v3f(-sy * cp, sp, -cy * cp);

    /* Up, plus the horizontal part of the aim. The vertical component of the
       aim is dropped rather than added: the leap's height is a constant so
       the arc is predictable, and looking straight down should not cancel it.
       위쪽에 조준의 수평 성분을 더합니다. 조준의 수직 성분은 더하지 않고 버립니다.
       도약 높이가 상수여야 궤적을 예측할 수 있고, 바로 아래를 본다고 해서 도약이
       취소되어서는 안 됩니다. */
    v3 flat = v3f(fwd.x, 0.0f, fwd.z);
    if (v3len(flat) > 1e-4f) flat = v3norm(flat);

    player_vel->y = AXE_LEAP_UP;
    *player_vel = v3add(*player_vel, v3scale(flat, AXE_LEAP_FWD));

    w->ammo[WP_AXE]--;
    w->leaping    = 1;
    w->leap_timer = 0.0f;
    w->punch     += wp_stats(WP_AXE)->punch;
    audio_play("hook", 85);
    return 1;
}

int wp_axe_land(Weapon *w, Pools *pl, v3 feet, int grounded, float dt) {
    if (!w->leaping) return 0;

    w->leap_timer += dt;

    /* Landing is the ground, or the timeout for a leap that went somewhere it
       could never come down from.
       착지는 바닥에 닿는 것이거나, 결코 내려올 수 없는 곳으로 간 도약에 대한 시간
       초과입니다. */
    if (!grounded && w->leap_timer < AXE_LEAP_TIMEOUT) return 0;

    w->leaping = 0;

    /* Centred on the feet, not the eye: the axe comes down at the floor, and a
       blast centred 1.7m up would reach over a low wall the player is standing
       behind.
       눈이 아니라 발을 중심으로 합니다. 도끼는 바닥으로 내려오며, 1.7m 위를 중심으로 한
       폭발은 플레이어가 뒤에 서 있는 낮은 벽을 넘어갑니다. */
    proj_blast(pl, feet, AXE_SLAM_RADIUS, AXE_SLAM_DAMAGE);
    fx_spawn(pl, "boltburst", feet, v3f(0, 1, 0));
    fx_spawn(pl, "spark", feet, v3f(0, 1, 0));
    audio_play_at("impact", 100, feet);

    w->punch += wp_stats(WP_AXE)->punch * 1.5f;
    return 1;
}

#ifdef HOT_RELOAD
/* --- Exposed for the headless tests / 헤드리스 테스트를 위한 노출 --- */

float weapon_pump_time(int type) {
    if (type < 0 || type >= WP_TYPES) type = 0;
    return wp_stats(type)->cooldown * PUMP_SHARE;
}

int weapon_sprite_frame_at(int type, float flash, float pump_timer,
                           float anim_clock) {
    /* A zeroed weapon with just the two timers set. wp_sprite_frame reads
       nothing else, and saying so here means the test cannot accidentally
       depend on some other field being plausible.
       두 타이머만 설정한 0으로 초기화된 무기입니다. wp_sprite_frame은 그 외에 아무것도
       읽지 않으며, 그것을 여기서 밝혀 두면 테스트가 다른 필드가 그럴듯한지에 실수로
       의존할 수 없게 됩니다. */
    Weapon w = (Weapon){0};
    w.cur        = type;
    w.flash      = flash;
    w.pump_timer = pump_timer;
    w.anim_clock = anim_clock;
    return wp_sprite_frame(&w);
}
#endif
