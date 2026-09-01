/**
 * @file sprite.c
 * @brief Draws the bestiary into RGBA atlases. No level data, just maths per pixel.
 *
 * ENGLISH
 * -------
 * Every creature is the union of signed-distance parts (a max of their
 * distances); the deepest part at a pixel picks its colour. The union's edge
 * is the silhouette, straight into the alpha channel. Three creatures share
 * the SDF helpers and one shading tail; only the body layout and palette
 * differ, which is what a new monster costs.
 *
 * @note Alpha here is a silhouette mask, NOT gloss as it is everywhere else
 *       in the project. See sprite.h.
 *
 * 한국어
 * ------
 * 모든 생물체는 부호 있는 거리장 부위들의 합집합(거리의 최댓값)이며, 각 픽셀에서
 * 가장 깊은 부위가 색상을 결정합니다. 합집합의 경계가 곧 실루엣이 되어 알파 채널로
 * 바로 들어갑니다. 세 생물체가 SDF 헬퍼와 하나의 셰이딩 마무리 코드를 공유하며,
 * 몸체 배치와 팔레트만 다릅니다. 새 몬스터를 추가하는 비용이 그 정도입니다.
 *
 * @note 여기서 알파는 실루엣 마스크이며, 프로젝트의 다른 모든 곳에서와 달리 광택이
 *       *아닙니다*. sprite.h를 참조하십시오.
 */

#include "sprite.h"
#include <stdlib.h>   /* malloc/calloc/free: this file used to reach these through windows.h */
#include "data.h"
#include "txt.h"
#include "enemy.h"        /* MON_* -- the atlas row order */
#include "weapon.h"       /* WP_TYPES and wp_stats -- the viewmodel row order */
#include "pickup.h"       /* PK_* -- the pickup atlas order */
/* png_decode -- a drawing arrives as the file somebody drew, not as a
   derivative of it. See png.h for what that bought and what it cost.
   png_decode입니다. 그림은 그것의 파생물이 아니라 누군가 그린 파일 그대로 도착합니다.
   그것이 무엇을 사고 무엇을 치렀는지는 png.h를 참조하십시오. */
#include "png.h"
#include "m.h"            /* m_hashf -- surface grain. It used to come from
                             tex.h, which made sprite and tex mutually dependent
                             over one helper. See the note on ::m_hash.
                             m_hashf(표면 잡티)입니다. 이전에는 tex.h에서 왔고, 그
                             탓에 sprite와 tex가 헬퍼 하나 때문에 서로 의존했습니다. */
#include <math.h>

/* ------------------------------------------------------------ SDF helpers */

static float ell(float nx, float ny, float cx, float cy, float rx, float ry) {
    float u = (nx - cx) / rx, v = (ny - cy) / ry;
    return (1.0f - sqrtf(u*u + v*v)) * (rx < ry ? rx : ry);
}

static float cap(float nx, float ny, float ax, float ay, float bx, float by, float r) {
    float ex = bx - ax, ey = by - ay;
    float len2 = ex*ex + ey*ey;
    float t = len2 > 1e-6f ? ((nx-ax)*ex + (ny-ay)*ey) / len2 : 0.0f;
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    float dx = nx - (ax + ex*t), dy = ny - (ay + ey*t);
    return r - sqrtf(dx*dx + dy*dy);
}

static void part(float d, int colour, float *best, int *win) {
    if (d > *best) { *best = d; *win = colour; }
}

/* Colour slots, shared across creatures; each type supplies its own palette.
   C_GLOW is not a part -- it is the light a glowing part throws, added on top
   rather than replacing the colour, so a red-hot maw and a cold arcane bolt
   can use the same code. */
enum { C_BODY, C_BELLY, C_LIMB, C_HORN, C_EYE, C_MAW, C_GLOW, C_COUNT };

/* Turns a won part into a shaded pixel: top light, a darker rim so the form
   reads against any wall, eyes and glowing parts kept at full punch. Returns
   alpha. `best` is the distance to the silhouette edge, `glow` lights the part
   on an attack frame. */
static int shade(int win, float best, float ny, float glow,
                 const unsigned char pal[][3], unsigned char *rgb) {
    if (best <= 0.0f) return 0;
    float sh = 0.72f + 0.28f * ny;
    float rim = best < 0.05f ? best / 0.05f : 1.0f;
    sh *= 0.55f + 0.45f * rim;
    for (int k = 0; k < 3; k++) {
        float c = pal[win][k] * (win == C_EYE ? 1.0f : sh);
        if (glow > 0.0f) c += pal[C_GLOW][k] * glow;
        rgb[k] = (unsigned char)(c < 0 ? 0 : c > 255 ? 255 : c);
    }
    return 255;
}

/* ---------------------------------------------------------- water spirit */

static const unsigned char PAL_SPIRIT[C_COUNT][3] = {
    { 150,  58,  40 }, { 176,  96,  62 }, {  96,  34,  26 },
    { 208, 198, 168 }, { 255, 224,  70 }, {  30,  10,  12 },
    { 200,  60,  60 },   /* C_GLOW: red-hot maw */
};

static int spirit_pixel(int fr, float nx, float ny, unsigned char *rgb) {
    float leg = 0.0f, arm = 0.0f, lean = 0.0f, maw = 0.0f;
    switch (fr) {
    case SPR_WALK0:  leg =  0.10f; break;
    case SPR_WALK1:  leg = -0.10f; break;
    case SPR_ATTACK: arm =  0.55f; maw = 1.0f; break;
    case SPR_HURT:   lean = -0.12f; leg = 0.05f; break;
    default: break;
    }

    if (fr == SPR_DEAD) {
        float best = -1e9f; int win = C_BODY;
        part(ell(nx, ny, 0.0f, 0.10f, 0.72f, 0.16f), C_BODY,  &best, &win);
        part(ell(nx, ny, 0.10f, 0.13f, 0.34f, 0.10f), C_BELLY, &best, &win);
        part(cap(nx, ny, -0.35f, 0.16f, -0.48f, 0.30f, 0.035f), C_HORN, &best, &win);
        part(cap(nx, ny, -0.20f, 0.16f, -0.28f, 0.28f, 0.035f), C_HORN, &best, &win);
        return shade(win, best, ny / 0.28f, 0.0f, PAL_SPIRIT, rgb);
    }

    nx += lean * ny;
    float best = -1e9f; int win = C_BODY;

    part(cap(nx, ny, -0.16f - leg, 0.02f, -0.20f - leg, 0.34f, 0.085f), C_LIMB, &best, &win);
    part(cap(nx, ny,  0.16f + leg, 0.02f,  0.20f + leg, 0.34f, 0.085f), C_LIMB, &best, &win);
    part(cap(nx, ny, -0.20f - leg, 0.02f, -0.30f - leg, 0.02f, 0.05f), C_LIMB, &best, &win);
    part(cap(nx, ny,  0.20f + leg, 0.02f,  0.30f + leg, 0.02f, 0.05f), C_LIMB, &best, &win);

    part(ell(nx, ny, 0.0f, 0.52f, 0.30f, 0.24f), C_BODY,  &best, &win);
    part(ell(nx, ny, 0.0f, 0.46f, 0.19f, 0.15f), C_BELLY, &best, &win);

    float hy = 0.20f + arm * 0.9f;
    part(cap(nx, ny, -0.26f, 0.66f, -0.30f, hy, 0.075f), C_LIMB, &best, &win);
    part(cap(nx, ny,  0.26f, 0.66f,  0.30f, hy, 0.075f), C_LIMB, &best, &win);
    part(cap(nx, ny, -0.30f, hy, -0.39f, hy + 0.02f, 0.03f), C_HORN, &best, &win);
    part(cap(nx, ny,  0.30f, hy,  0.39f, hy + 0.02f, 0.03f), C_HORN, &best, &win);

    part(ell(nx, ny, 0.0f, 0.86f, 0.19f, 0.17f), C_BODY, &best, &win);
    part(cap(nx, ny, -0.12f, 0.96f, -0.26f, 1.12f, 0.035f), C_HORN, &best, &win);
    part(cap(nx, ny,  0.12f, 0.96f,  0.26f, 1.12f, 0.035f), C_HORN, &best, &win);

    if (best <= 0.0f) return 0;

    if (ell(nx, ny, -0.075f, 0.88f, 0.035f, 0.045f) > 0.0f ||
        ell(nx, ny,  0.075f, 0.88f, 0.035f, 0.045f) > 0.0f) win = C_EYE;
    float mouth = ell(nx, ny, 0.0f, 0.78f, 0.09f, 0.03f + maw * 0.05f);
    if (mouth > 0.0f) win = C_MAW;

    float glow = (win == C_MAW && maw > 0.0f) ? 1.0f : 0.0f;
    return shade(win, best, ny, glow, PAL_SPIRIT, rgb);
}

/* ----------------------------------------------------------------- brute */

static const unsigned char PAL_BRUTE[C_COUNT][3] = {
    {  78,  92,  70 },   /* body: grey-green hide */
    {  96, 110,  84 },   /* belly */
    {  52,  62,  48 },   /* limbs: darker */
    { 214, 206, 176 },   /* tusks, claws, spikes: bone */
    { 255,  90,  40 },   /* eyes: small and red */
    {  18,   8,   8 },   /* maw */
    { 200,  60,  60 },   /* C_GLOW: red-hot maw */
};

static int brute_pixel(int fr, float nx, float ny, unsigned char *rgb) {
    float leg = 0.0f, arm = 0.0f, maw = 0.0f, lean = 0.0f;
    switch (fr) {
    case SPR_WALK0:  leg =  0.07f; break;
    case SPR_WALK1:  leg = -0.07f; break;
    case SPR_ATTACK: arm =  0.60f; maw = 1.0f; break;
    case SPR_HURT:   lean = -0.10f; break;
    default: break;
    }

    if (fr == SPR_DEAD) {
        float best = -1e9f; int win = C_BODY;
        part(ell(nx, ny, 0.0f, 0.12f, 0.88f, 0.20f), C_BODY,  &best, &win);
        part(ell(nx, ny, -0.1f, 0.15f, 0.42f, 0.12f), C_BELLY, &best, &win);
        part(cap(nx, ny, 0.40f, 0.18f, 0.55f, 0.30f, 0.045f), C_HORN, &best, &win);
        return shade(win, best, ny / 0.32f, 0.0f, PAL_BRUTE, rgb);
    }

    nx += lean * ny;
    float best = -1e9f; int win = C_BODY;

    /* Tree-trunk legs. */
    part(cap(nx, ny, -0.26f - leg, 0.02f, -0.30f - leg, 0.40f, 0.14f), C_LIMB, &best, &win);
    part(cap(nx, ny,  0.26f + leg, 0.02f,  0.30f + leg, 0.40f, 0.14f), C_LIMB, &best, &win);
    part(cap(nx, ny, -0.30f - leg, 0.02f, -0.44f - leg, 0.02f, 0.07f), C_LIMB, &best, &win);
    part(cap(nx, ny,  0.30f + leg, 0.02f,  0.44f + leg, 0.02f, 0.07f), C_LIMB, &best, &win);

    /* A massive torso, wider at the shoulders than the hips. */
    part(ell(nx, ny, 0.0f, 0.58f, 0.44f, 0.30f), C_BODY,  &best, &win);
    part(ell(nx, ny, 0.0f, 0.48f, 0.28f, 0.18f), C_BELLY, &best, &win);

    /* Long, knuckle-dragging arms that rear forward to strike. */
    float hy = 0.10f + arm * 0.7f;
    part(cap(nx, ny, -0.42f, 0.74f, -0.50f, hy, 0.12f), C_LIMB, &best, &win);
    part(cap(nx, ny,  0.42f, 0.74f,  0.50f, hy, 0.12f), C_LIMB, &best, &win);
    /* Claws. */
    for (int s = -1; s <= 1; s += 2) {
        part(cap(nx, ny, s*0.50f, hy, s*0.58f, hy - 0.08f, 0.035f), C_HORN, &best, &win);
        part(cap(nx, ny, s*0.44f, hy, s*0.50f, hy - 0.09f, 0.035f), C_HORN, &best, &win);
    }

    /* Small head sunk between the shoulders. */
    part(ell(nx, ny, 0.0f, 0.86f, 0.16f, 0.14f), C_BODY, &best, &win);
    /* Tusks curling up from the jaw. */
    part(cap(nx, ny, -0.08f, 0.80f, -0.14f, 0.94f, 0.03f), C_HORN, &best, &win);
    part(cap(nx, ny,  0.08f, 0.80f,  0.14f, 0.94f, 0.03f), C_HORN, &best, &win);
    /* A ridge of back spikes. */
    for (int s = 0; s < 3; s++) {
        float bx = -0.14f + s * 0.14f;
        part(cap(nx, ny, bx, 0.80f, bx, 0.94f, 0.022f), C_HORN, &best, &win);
    }

    if (best <= 0.0f) return 0;

    if (ell(nx, ny, -0.06f, 0.88f, 0.028f, 0.03f) > 0.0f ||
        ell(nx, ny,  0.06f, 0.88f, 0.028f, 0.03f) > 0.0f) win = C_EYE;
    float mouth = ell(nx, ny, 0.0f, 0.80f, 0.08f, 0.02f + maw * 0.05f);
    if (mouth > 0.0f) win = C_MAW;

    float glow = (win == C_MAW && maw > 0.0f) ? 1.0f : 0.0f;
    return shade(win, best, ny, glow, PAL_BRUTE, rgb);
}

/* ---------------------------------------------------------------- caster */

static const unsigned char PAL_CASTER[C_COUNT][3] = {
    {  74,  62, 110 },   /* robe: deep violet */
    {  98,  84, 142 },   /* robe highlight */
    {  48,  40,  74 },   /* hood shadow and sleeves */
    { 206, 202, 214 },   /* bone hands, staff */
    { 120, 240, 255 },   /* eyes: cold cyan, the opposite of every melee type */
    {  14,  10,  22 },   /* the dark inside the hood */
    {  20, 130, 175 },   /* C_GLOW: cold arcane light, not the melee red */
};

/* A robed, hovering figure -- no legs at all, just a robe tapering to a point.
   Tall, narrow and cyan against two squat warm-coloured floor-bound types, so
   "the one that shoots" is legible from across a room, which is the whole job
   of a ranged enemy's silhouette. The missing legs are not decoration now: this
   is the kind that carries MON_FLIES, and a silhouette ending in a point is the
   only thing telling the player it is not standing on anything. */
static int caster_pixel(int fr, float nx, float ny, unsigned char *rgb) {
    float arm = 0.0f, glowamt = 0.0f, lean = 0.0f, bobp = 0.0f;
    switch (fr) {
    case SPR_WALK0:  bobp =  0.03f; break;      /* it drifts rather than walks */
    case SPR_WALK1:  bobp = -0.03f; break;
    case SPR_ATTACK: arm = 1.0f; glowamt = 1.0f; break;
    case SPR_HURT:   lean = -0.14f; bobp = -0.05f; break;
    default: break;
    }

    if (fr == SPR_DEAD) {
        /* A collapsed heap of robe -- no bones, it simply falls in on itself. */
        float best = -1e9f; int win = C_BODY;
        part(ell(nx, ny, 0.0f, 0.09f, 0.62f, 0.14f), C_BODY,  &best, &win);
        part(ell(nx, ny, -0.12f, 0.11f, 0.28f, 0.08f), C_BELLY, &best, &win);
        part(ell(nx, ny, 0.30f, 0.10f, 0.13f, 0.07f), C_MAW,   &best, &win);
        return shade(win, best, ny / 0.26f, 0.0f, PAL_CASTER, rgb);
    }

    ny -= bobp;                       /* hover */
    nx += lean * ny;
    float best = -1e9f; int win = C_BODY;

    /* The robe: wide at the shoulders, tapering to a point above the floor. */
    part(cap(nx, ny, 0.0f, 0.16f, 0.0f, 0.74f, 0.055f), C_LIMB, &best, &win);
    part(ell(nx, ny, 0.0f, 0.50f, 0.30f, 0.30f), C_BODY,  &best, &win);
    part(ell(nx, ny, 0.0f, 0.46f, 0.17f, 0.20f), C_BELLY, &best, &win);
    /* Trailing hem, wider than the body so it reads as cloth. */
    part(ell(nx, ny, 0.0f, 0.26f, 0.26f, 0.20f), C_BODY, &best, &win);

    /* Sleeves out to the sides, raised on the attack. */
    float hy = 0.52f + arm * 0.30f;
    float hx = 0.34f + arm * 0.06f;
    part(cap(nx, ny, -0.22f, 0.68f, -hx, hy, 0.075f), C_LIMB, &best, &win);
    part(cap(nx, ny,  0.22f, 0.68f,  hx, hy, 0.075f), C_LIMB, &best, &win);

    /* Hood: a cowl with a dark void where a face would be. */
    part(ell(nx, ny, 0.0f, 0.88f, 0.20f, 0.18f), C_BODY, &best, &win);
    part(cap(nx, ny, 0.0f, 0.98f, 0.0f, 1.10f, 0.045f), C_BODY, &best, &win);

    if (best <= 0.0f) return 0;

    /* Inside the hood, then the two eyes burning in it. Set well apart: any
       closer and they merge into one bar at this cell size. */
    if (ell(nx, ny, 0.0f, 0.86f, 0.12f, 0.12f) > 0.0f) win = C_MAW;
    if (ell(nx, ny, -0.09f, 0.87f, 0.033f, 0.055f) > 0.0f ||
        ell(nx, ny,  0.09f, 0.87f, 0.033f, 0.055f) > 0.0f) win = C_EYE;

    /* Bone hands at the sleeve ends. */
    if (ell(nx, ny, -hx, hy, 0.055f, 0.055f) > 0.0f ||
        ell(nx, ny,  hx, hy, 0.055f, 0.055f) > 0.0f) win = C_HORN;

    /* On the attack frame, the bolt gathering at chest height between the
       raised hands -- the telegraph the player reads. Deliberately below the
       hood: over the face it hides the eyes, which are what identify the
       thing in the first place. */
    if (arm > 0.0f && ell(nx, ny, 0.0f, hy - 0.13f, 0.115f, 0.115f) > 0.0f)
        win = C_EYE;

    /* The eyes and the gathering bolt are light sources, so they keep their
       punch rather than being shaded down with the cloth. */
    float glow = (win == C_EYE) ? (0.35f + 0.65f * glowamt) : 0.0f;
    return shade(win, best, ny, glow, PAL_CASTER, rgb);
}

/* ------------------------------------------------------------------- maw
 *
 * NOT A CREATURE SHAPE, and that is the point of it. Every other body here is
 * a silhouette with a head on top and something under it; this one fills its
 * cell corner to corner, because it is a piece of the wall that opened. The
 * read a player needs from across a room is "that is not a monster, that is the
 * room", and a silhouette with air around it cannot say that.
 *
 * IT HAS NO WALK. Frames 0 and 1 breathe instead -- the lips of the slit part
 * and close by a few hundredths -- which is the only motion an anchored thing
 * can have and is enough to keep it from reading as scenery.
 *
 * 생물의 형태가 아니며, 그것이 요점입니다. 이곳의 다른 모든 몸통은 위에 머리가 있고 그 아래에
 * 무언가가 있는 실루엣이지만, 이것은 자기 칸을 모서리까지 채웁니다. 열린 벽의 일부이기
 * 때문입니다. 플레이어가 방 건너에서 얻어야 할 읽힘은 "저것은 몬스터가 아니라 방이다"이고,
 * 주위에 공기가 있는 실루엣은 그렇게 말할 수 없습니다.
 *
 * 걷기가 없습니다. 프레임 0과 1은 대신 호흡합니다. 갈라진 틈의 입술이 수백분의 일만큼
 * 벌어졌다 닫히며, 그것이 고정된 것이 가질 수 있는 유일한 움직임이자 배경으로 읽히지 않게
 * 하기에 충분한 양입니다. */

static const unsigned char PAL_MAW[C_COUNT][3] = {
    {  74,  30,  34 }, { 104,  46,  46 }, {  46,  18,  22 },
    { 208, 190, 172 }, { 255, 170,  70 }, {  10,   4,   6 },
    { 255, 120,  40 },   /* C_GLOW: furnace light */
};

static int maw_pixel(int fr, float nx, float ny, unsigned char *rgb) {
    float gape = 0.0f, glowamt = 0.35f, clench = 0.0f;
    switch (fr) {
    case SPR_WALK0:  gape =  0.015f; break;              /* breathing, not walking */
    case SPR_WALK1:  gape = -0.015f; break;
    case SPR_ATTACK: gape =  0.11f; glowamt = 1.0f; break;
    case SPR_HURT:   clench = 0.06f; glowamt = 0.7f; break;
    case SPR_DEAD:   gape = -0.10f; glowamt = 0.0f; break;
    default: break;
    }

    float best = -1e9f; int win = C_BODY;

    /* The mass. Two overlapping slabs rather than one, so the edge is not a
       clean ellipse -- a clean edge reads as an object sitting in front of the
       wall, and this is meant to read as the wall itself having a hole in it.
       덩어리입니다. 하나가 아니라 겹친 두 판이므로 가장자리가 깔끔한 타원이 아닙니다. 깔끔한
       가장자리는 벽 앞에 놓인 물체로 읽히는데, 이것은 벽 자체에 구멍이 난 것으로 읽혀야
       합니다. */
    part(ell(nx, ny, 0.0f, 0.50f, 0.50f, 0.52f), C_BODY,  &best, &win);
    part(ell(nx, ny, -0.16f, 0.62f, 0.34f, 0.36f), C_BELLY, &best, &win);
    part(ell(nx, ny,  0.18f, 0.38f, 0.32f, 0.34f), C_BELLY, &best, &win);

    if (best <= 0.0f) return 0;

    /* The rim: a ring of flesh a little darker than the face, so the slit does
       not float. */
    if (ell(nx, ny, 0.0f, 0.50f, 0.40f, 0.42f) > 0.0f) win = C_LIMB;

    /* THE SLIT, which is the whole silhouette's job. Two lips meeting on a
       horizontal axis; `gape` moves them apart and `clench` pulls them past
       each other so a hurt frame is unmistakably shut.
       틈이며, 실루엣 전체의 임무가 그것입니다. 수평축에서 만나는 두 입술입니다. `gape`가 둘을
       벌리고 `clench`는 서로를 지나치도록 당겨서, 피격 프레임이 명백하게 닫히게 합니다. */
    float half = 0.13f + gape - clench;
    if (half > 0.0f && ell(nx, ny, 0.0f, 0.50f, 0.30f, half) > 0.0f) win = C_MAW;

    /* Teeth along both lips, as pairs rather than a comb: three each, so the
       count reads at 64 pixels wide instead of turning into a grey band. */
    for (int i = -1; i <= 1; i++) {
        float tx = (float)i * 0.15f;
        if (ell(nx, ny, tx, 0.50f + half - 0.03f, 0.045f, 0.055f) > 0.0f ||
            ell(nx, ny, tx + 0.075f, 0.50f - half + 0.03f, 0.045f, 0.055f) > 0.0f)
            win = C_HORN;
    }

    /* The furnace behind the teeth. Only when actually open, so a shut maw is
       not a lamp.
       이빨 뒤의 화로입니다. 실제로 열려 있을 때만이므로, 닫힌 아귀는 등불이 아닙니다. */
    if (half > 0.08f && ell(nx, ny, 0.0f, 0.50f, 0.16f, half * 0.55f) > 0.0f)
        win = C_EYE;

    float glow = (win == C_EYE) ? glowamt : 0.0f;
    return shade(win, best, ny, glow, PAL_MAW, rgb);
}

/* ------------------------------------------------------------------ ward
 *
 * ONE BODY FOR BOTH KINDS. What an author places is two markers and what the
 * player fights is two summon tables; the ward itself is the same object in
 * both cases, so drawing it twice would be two files to keep in step for a
 * difference the player reads off what walks out of it.
 *
 * A HARD SHAPE, deliberately unlike every other row here. Wards are shot at
 * from across the room while something else is arriving, and a soft organic
 * blob at 64 pixels is indistinguishable from a monster at that distance. The
 * facets say "object" and the core says "shoot this".
 *
 * *두 종류에 하나의 몸통입니다.* 제작자가 배치하는 것은 표식 둘이고 플레이어가 상대하는 것은
 * 소환표 둘이지만, 결계핵 자체는 두 경우 모두 같은 물체입니다. 두 번 그리면, 정작 플레이어는
 * 거기서 걸어 나오는 것으로 읽는 차이를 위해 보조를 맞출 파일이 둘이 됩니다.
 *
 * *단단한 형태이며, 이곳의 다른 모든 행과 의도적으로 다릅니다.* 결계핵은 다른 무언가가 도착하는
 * 동안 방 건너에서 쏘게 되는데, 64픽셀에서 부드러운 유기적 덩어리는 그 거리의 몬스터와 구별되지
 * 않습니다. 각진 면이 "물체"라고 말하고 핵이 "이걸 쏴라"라고 말합니다. */

static const unsigned char PAL_WARD[C_COUNT][3] = {
    {  92,  84,  52 }, { 132, 120,  70 }, {  56,  50,  30 },
    { 214, 206, 168 }, { 255, 226, 120 }, {  22,  20,  12 },
    { 255, 210,  90 },   /* C_GLOW: the core */
};

static int ward_pixel(int fr, float nx, float ny, unsigned char *rgb) {
    float spin = 0.0f, glowamt = 0.6f, crack = 0.0f;
    switch (fr) {
    case SPR_WALK0:  spin =  0.02f; break;               /* it turns in place */
    case SPR_WALK1:  spin = -0.02f; break;
    case SPR_ATTACK: glowamt = 1.0f; break;              /* while it is paying out */
    case SPR_HURT:   crack = 0.05f; glowamt = 0.9f; break;
    default: break;
    }

    if (fr == SPR_DEAD) {
        /* It does not leave a corpse -- it leaves shards on the floor, which is
           how a destroyed OBJECT differs from a killed creature. Nothing here
           is above the lower third of the cell.
           시체를 남기지 않고 바닥에 파편을 남깁니다. 그것이 파괴된 *물체*가 죽은 생물과
           다른 점입니다. 이곳의 무엇도 칸의 아래 3분의 1을 넘지 않습니다. */
        float best = -1e9f; int win = C_LIMB;
        part(ell(nx, ny, -0.14f, 0.05f, 0.11f, 0.04f), C_LIMB, &best, &win);
        part(ell(nx, ny,  0.03f, 0.07f, 0.09f, 0.05f), C_BODY, &best, &win);
        part(ell(nx, ny,  0.17f, 0.04f, 0.07f, 0.03f), C_LIMB, &best, &win);
        if (best <= 0.0f) return 0;
        return shade(win, best, ny / 0.12f, 0.0f, PAL_WARD, rgb);
    }

    float best = -1e9f; int win = C_BODY;

    /* A stubby column with a wider band at its waist: two facets top and
       bottom, so it has an up and a down without having a head.
       허리에 더 넓은 띠가 있는 짧고 굵은 기둥입니다. 위아래에 각진 면이 둘 있어, 머리 없이도
       위와 아래를 가집니다. */
    part(ell(nx, ny + spin, 0.0f, 0.50f, 0.20f, 0.30f), C_BODY,  &best, &win);
    part(ell(nx, ny + spin, 0.0f, 0.50f, 0.30f, 0.12f), C_BELLY, &best, &win);
    part(ell(nx, ny, 0.0f, 0.14f, 0.16f, 0.10f), C_LIMB, &best, &win);

    if (best <= 0.0f) return 0;

    /* Two bands of casing, which is what gives it facets rather than curves. */
    if (ell(nx, ny + spin, 0.0f, 0.68f, 0.15f, 0.10f) > 0.0f ||
        ell(nx, ny + spin, 0.0f, 0.32f, 0.15f, 0.10f) > 0.0f) win = C_HORN;

    /* The core, and it is the reason this is drawn at all: a bright point at
       eye height that survives the dither pass and is visible past whatever the
       ward has just summoned in front of it.
       핵이며, 이것을 그리는 이유 자체입니다. 눈높이의 밝은 점 하나가 디더 패스를 견디고,
       결계핵이 방금 자기 앞에 소환한 것 너머로도 보입니다. */
    if (ell(nx, ny + spin, 0.0f, 0.50f, 0.10f - crack, 0.13f - crack) > 0.0f)
        win = C_EYE;

    float glow = (win == C_EYE) ? glowamt : 0.0f;
    return shade(win, best, ny, glow, PAL_WARD, rgb);
}

/* -------------------------------------------------------------- dispatch */

static int creature_pixel(int type, int fr, float nx, float ny, unsigned char *rgb) {
    switch (type) {
    case MON_BRUTE:  return brute_pixel(fr, nx, ny, rgb);
    case MON_CASTER: return caster_pixel(fr, nx, ny, rgb);
    case MON_MAW:    return maw_pixel(fr, nx, ny, rgb);
    case MON_WARD:   return ward_pixel(fr, nx, ny, rgb);
    default:         return spirit_pixel(fr, nx, ny, rgb);
    }
}

/* --------------------------------------------------------------- pickups
 *
 * Small floor icons. Same alpha-silhouette trick as the monsters, but a
 * single frame each and simpler shapes: a shell box and a medkit, chosen to
 * read at a glance from across a room.
 *
 * THESE ARE THE FALLBACK NOW, not the shipped look. Freedoom's own floor
 * sprites are imported as `item*` drawings and replace the cells they fill;
 * what stays here is what a kind nobody drew still shows, which is the same
 * bargain the monsters get. Kept rather than deleted for that reason, and
 * because the argument they were written for is still sound: a WEAPON'S
 * VIEWMODEL makes a bad floor item, since art drawn to be read at arm's length
 * is a dark smear across a room. Doom's pickups are not viewmodels -- MEDI and
 * SHEL were drawn for exactly this distance -- so importing them answers that
 * objection rather than ignoring it.
 *
 * 이것들은 이제 폴백이며 배포되는 모습이 아닙니다. Freedoom 자신의 바닥 스프라이트가
 * `item*` 그림으로 이식되어 자기가 채우는 셀을 대체하며, 여기 남은 것은 아무도 그리지
 * 않은 종류가 여전히 보여 주는 모습입니다. 몬스터가 받는 것과 같은 거래입니다. 지우지
 * 않고 두는 이유는 그것과, 이것이 쓰인 근거가 여전히 타당하기 때문입니다. *무기의 뷰
 * 모델*은 나쁜 바닥 아이템이 됩니다. 팔 길이에서 읽히도록 그린 아트는 방 건너에서
 * 어두운 얼룩이기 때문입니다. Doom의 픽업은 뷰 모델이 아니며 바로 이 거리를 위해
 * 그려졌으므로, 그것을 이식하는 일은 그 반론을 무시하는 것이 아니라 해소합니다. */

/* Rounded-box coverage: >0 inside, in the same [-1,1] units. */
static float rbox(float nx, float ny, float hw, float hh, float r) {
    float dx = fabsf(nx) - (hw - r), dy = fabsf(ny) - (hh - r);
    float ox = dx > 0 ? dx : 0, oy = dy > 0 ? dy : 0;
    float outside = sqrtf(ox*ox + oy*oy);
    float inside = (dx > dy ? dx : dy); if (inside > 0) inside = 0;
    return r - (outside + inside);
}

/* Fills one pickup pixel. nx,ny in [-1,1], centred. Returns alpha. */
static int pickup_pixel(int kind, float nx, float ny, unsigned char *rgb) {
    float r = 0, g = 0, b = 0, a = 0;

    if (kind == PK_AMMO) {
        /* A brass-banded box of shells. */
        float box = rbox(nx, ny + 0.12f, 0.62f, 0.42f, 0.10f);
        if (box > 0.0f) {
            a = 1.0f;
            /* Body red-brown, a lighter lid, and a brass band across it. */
            r = 0.42f; g = 0.16f; b = 0.10f;
            if (ny > 0.10f)                 { r = 0.55f; g = 0.24f; b = 0.14f; }
            if (fabsf(ny + 0.08f) < 0.07f)  { r = 0.80f; g = 0.62f; b = 0.20f; }
            float rim = box < 0.06f ? box / 0.06f : 1.0f;   /* dark outline */
            r *= 0.5f + 0.5f * rim; g *= 0.5f + 0.5f * rim; b *= 0.5f + 0.5f * rim;
        }
        /* Three shell tops poking out of the lid. */
        for (int s = -1; s <= 1; s++) {
            float sx = s * 0.34f;
            float d = (nx - sx) * (nx - sx) + (ny - 0.44f) * (ny - 0.44f);
            if (d < 0.11f * 0.11f) { a = 1.0f; r = 0.90f; g = 0.72f; b = 0.22f; }
            if (d < 0.05f * 0.05f) { r = 0.30f; g = 0.16f; b = 0.10f; }  /* primer */
        }
    } else if (kind >= PK_POWER0 && kind <= PK_POWER_LAST) {
        /* AN ARTIFACT IS A FLOATING SIGIL, and all three share the shape on
           purpose: a ring with a glyph inside it, differing only in colour and
           in the glyph. A player who has learned that a hovering ring is worth
           crossing the room for should not have to learn it three times, and
           the thing they then need to read at a glance is WHICH one -- which is
           what colour does faster than silhouette.
           Drawn rather than lit. There is no emissive term in the world pass
           (see render.c's window facade note), so "glowing" here means a bright
           core and a rim that falls off, composited by the same additive rules
           every other sprite uses.
           *아티팩트는 떠 있는 인장이며*, 셋이 형태를 공유하는 것은 의도적입니다. 고리 하나에
           그 안의 문양이며, 색과 문양만 다릅니다. 떠 있는 고리가 방을 가로지를 값어치가
           있다는 것을 익힌 플레이어가 그것을 세 번 익힐 필요는 없고, 그다음 한눈에 읽어야 할
           것은 *어느 것인가*입니다. 그것은 실루엣보다 색이 빠르게 답합니다.
           빛나는 것이 아니라 그려집니다. 월드 패스에는 발광 항이 없으므로(render.c의 창 파사드
           설명 참조) 이곳의 "빛남"은 밝은 중심과 떨어지는 테두리이며, 다른 모든 스프라이트가
           쓰는 것과 같은 가산 규칙으로 합성됩니다. */
        int which = kind - PK_POWER0;
        float d    = sqrtf(nx * nx + ny * ny);
        float ring = 0.72f;
        float band = fabsf(d - ring);

        /* The ring itself. */
        if (band < 0.16f) {
            float k = 1.0f - band / 0.16f;
            a = 1.0f;
            if (which == PW_QUAD)        { r = 0.35f; g = 0.55f; b = 1.00f; }
            else if (which == PW_SHADOW) { r = 0.55f; g = 0.35f; b = 0.85f; }
            else                         { r = 0.95f; g = 0.72f; b = 0.25f; }
            r *= 0.45f + 0.55f * k;
            g *= 0.45f + 0.55f * k;
            b *= 0.45f + 0.55f * k;
        }

        /* The glyph: a bolt for the quad, a hollow for the ring of shadows, a
           chevron for the aegis. Each is the cheapest mark that reads at the
           size a pickup is actually seen at, which is a few dozen pixels.
           문양입니다. 쿼드는 번개, 그림자 반지는 빈 구멍, 아이기스는 갈매기표입니다. 각각은
           획득물이 실제로 보이는 크기(수십 픽셀)에서 읽히는 가장 값싼 표식입니다. */
        if (d < 0.52f) {
            int on = 0;
            if (which == PW_QUAD) {
                float t = nx - ny * 0.45f;
                on = fabsf(t) < 0.14f && fabsf(ny) < 0.46f;
            } else if (which == PW_SHADOW) {
                on = d > 0.26f;             /* a hollow: the mark IS the absence */
            } else {
                on = fabsf(fabsf(nx) - (0.34f - ny * 0.30f)) < 0.13f && ny > -0.42f;
            }
            if (on) {
                a = 1.0f;
                r = 0.98f; g = 0.98f; b = 0.98f;
                if (which == PW_QUAD)        { r = 0.72f; g = 0.88f; }
                else if (which == PW_SHADOW) { r = 0.80f; b = 0.98f; g = 0.66f; }
                else                         { b = 0.66f; }
            }
        }
    } else if (kind == PK_HEALTH) {
        float box = rbox(nx, ny, 0.60f, 0.52f, 0.12f);
        if (box > 0.0f) {
            a = 1.0f;
            /* Off-white case with a darker rim. */
            r = g = b = 0.90f;
            float rim = box < 0.06f ? box / 0.06f : 1.0f;
            r *= 0.45f + 0.55f * rim; g = r; b = r;
            /* A red cross. */
            int cross = (fabsf(nx) < 0.14f && fabsf(ny) < 0.34f) ||
                        (fabsf(ny) < 0.14f && fabsf(nx) < 0.34f);
            if (cross) { r = 0.85f; g = 0.10f; b = 0.10f; }
        }
    } else {
        /* --- one belt per weapon, and the weapons themselves --------------
         *
         * ENGLISH
         * -------
         * Colour is what tells them apart at a glance, so it comes from the
         * WEAPON rather than from the kind: an ammo box and the weapon it
         * feeds share a hue, and a player who learns that grenades are orange
         * has learned it for both. Doom taught its colours the same way.
         *
         * The shape says which of the two it is -- a squat box for ammo, an
         * upright shard for a weapon -- because colour alone cannot be read by
         * everyone, and at this sprite size a silhouette is more legible than
         * any detail drawn inside it.
         *
         * These stay GENERATED. The obvious next step was to draw the pickup
         * with the weapon's own viewmodel sprite -- the art already exists, it
         * would cost no new pixels, and "the thing on the floor is the thing
         * you pick up" sounds right. It was tried and it is worse, so this is
         * the decision rather than a stop on the way to that one.
         *
         * A viewmodel is drawn to be seen from ONE angle, filling the bottom of
         * the screen, lit as though it were in your hands. On the floor at a
         * distance it is a small dark smear: the silhouette that reads as a
         * weapon when it is 400 pixels tall reads as debris at 40, and the
         * detail that sells it up close is the first thing the art resolution
         * throws away. Four of them at range are four smudges you have to walk
         * onto to identify.
         *
         * The generated icons answer the question the floor actually asks --
         * "what is that, and do I want it" -- from across a room, because they
         * were designed for that distance instead of borrowed from another one.
         * Colour carries which weapon; the shard-versus-box silhouette carries
         * whether it is the weapon or its ammunition. Both survive being small.
         *
         * 한국어
         * ------
         * 이 아이콘들은 *생성된 채로 유지됩니다*. 뻔한 다음 단계는 무기의 뷰 모델
         * 스프라이트로 아이템을 그리는 것이었습니다. 아트가 이미 있고, 새 픽셀 비용이 없고,
         * "바닥에 있는 것이 곧 줍는 것"은 옳게 들립니다. 시도했고 더 나빴으므로, 이것은
         * 그쪽으로 가는 도중의 정거장이 아니라 결론입니다.
         *
         * 뷰 모델은 *한* 각도에서, 화면 아래를 채우며, 손에 든 것처럼 조명된 상태로 보이도록
         * 그려집니다. 멀리 떨어진 바닥에서 그것은 작고 어두운 얼룩입니다. 400픽셀 높이에서
         * 무기로 읽히는 실루엣이 40픽셀에서는 잔해로 읽히고, 가까이서 설득력을 만드는 디테일이
         * 아트 해상도가 가장 먼저 버리는 것입니다. 멀리 있는 네 개는 정체를 알려면 밟아 봐야
         * 하는 네 개의 얼룩입니다.
         *
         * 생성된 아이콘은 바닥이 실제로 던지는 질문("저게 뭐고, 내가 원하는가")에 방 건너에서
         * 답합니다. 다른 거리에서 빌려 온 것이 아니라 그 거리를 위해 설계되었기 때문입니다.
         * 색이 어떤 무기인지를, 조각 대 상자의 실루엣이 무기인지 탄약인지를 전달합니다. 둘 다
         * 작아져도 살아남습니다.
         *
         * 한국어
         * ------
         * 색이 한눈에 구분해 주므로, 종류가 아니라 *무기*에서 색을 가져옵니다. 탄약 상자와
         * 그것이 채우는 무기가 같은 색조를 공유하며, 유탄이 주황색임을 익힌 플레이어는
         * 양쪽 모두를 익힌 셈입니다. Doom이 색을 가르친 방식과 같습니다.
         *
         * 둘 중 무엇인지는 형태가 말합니다. 탄약은 납작한 상자, 무기는 세로로 선
         * 조각입니다. 색만으로는 모두가 읽을 수 없고, 이 크기의 스프라이트에서는 안에 그린
         * 어떤 디테일보다 실루엣이 잘 읽히기 때문입니다.
         *
         * 몬스터와 마찬가지로 *생성*되며, 손으로 그린 아트가 같은 방식으로 대체합니다.
         * 아무도 그리지 않은 무기라도 레벨에 배치할 수 있게 하는 폴백입니다. */
        static const float HUE[WP_TYPES][3] = {
            { 0.85f, 0.66f, 0.24f },   /* shotgun: brass */
            { 0.90f, 0.45f, 0.14f },   /* grenade: orange */
            { 0.30f, 0.72f, 0.95f },   /* rapid:   cold blue */
            { 0.70f, 0.78f, 0.86f },   /* axe:     steel */
        };

        /* --- keycards ----------------------------------------------------
           A flat card, wider than tall, in the colour the door asks for by
           name. Deliberately unlike both other shapes here: a key is neither
           ammunition nor a weapon, and a player scanning a room for the red
           one should not have to tell it apart from a red ammo crate.
           납작한 카드이며 높이보다 폭이 넓고, 문이 이름으로 요구하는 색입니다. 이곳의 다른
           두 형태와 의도적으로 다릅니다. 열쇠는 탄약도 무기도 아니며, 붉은 열쇠를 찾아 방을
           훑는 플레이어가 그것을 붉은 탄약 상자와 구별하느라 애쓸 필요는 없습니다. */
        int km = PK_KEY_MASK(kind);
        if (km != KEY_NONE) {
            static const float KEYCOL[KEY_KINDS][3] = {
                { 0.90f, 0.18f, 0.20f },   /* red */
                { 0.24f, 0.46f, 0.95f },   /* blue */
                { 0.94f, 0.82f, 0.22f },   /* yellow */
            };
            int ki = kind - PK_KEY0;
            if (ki < 0 || ki >= KEY_KINDS) ki = 0;
            const float *K = KEYCOL[ki];

            float card = rbox(nx, ny, 0.56f, 0.34f, 0.07f);
            if (card > 0.0f) {
                a = 1.0f;
                r = K[0]; g = K[1]; b = K[2];
                float rim = card < 0.06f ? card / 0.06f : 1.0f;
                r *= 0.5f + 0.5f * rim; g *= 0.5f + 0.5f * rim; b *= 0.5f + 0.5f * rim;
                /* A pale stripe: a blank slab of colour reads as a wall tile,
                   and the stripe is what makes it an object.
                   옅은 줄무늬입니다. 단색 판은 벽 타일로 읽히며, 줄무늬가 그것을 물체로
                   만듭니다. */
                if (fabsf(ny - 0.12f) < 0.06f && fabsf(nx) < 0.40f) {
                    r = 0.95f; g = 0.96f; b = 0.98f;
                }
            }
            if (a <= 0.0f) return 0;
            rgb[0] = (unsigned char)(r > 1 ? 255 : r * 255);
            rgb[1] = (unsigned char)(g > 1 ? 255 : g * 255);
            rgb[2] = (unsigned char)(b > 1 ? 255 : b * 255);
            return 255;
        }

        int aw = PK_AMMO_WEAPON(kind), ww = PK_WEAPON_WEAPON(kind);
        int which = aw >= 0 ? aw : ww;
        if (which < 0 || which >= WP_TYPES) which = 0;
        const float *H = HUE[which];

        if (aw >= 0) {
            /* A squat box, banded in the weapon's colour. */
            float box = rbox(nx, ny + 0.10f, 0.60f, 0.38f, 0.10f);
            if (box > 0.0f) {
                a = 1.0f;
                r = 0.24f; g = 0.24f; b = 0.26f;              /* dark crate */
                if (fabsf(ny + 0.08f) < 0.09f) { r = H[0]; g = H[1]; b = H[2]; }
                float rim = box < 0.06f ? box / 0.06f : 1.0f;
                r *= 0.5f + 0.5f * rim; g *= 0.5f + 0.5f * rim; b *= 0.5f + 0.5f * rim;
            }
        } else {
            /* An upright shard: taller than it is wide, so it never reads as
               a box even in silhouette.
               세로로 선 조각입니다. 폭보다 높이가 커서 실루엣만으로도 상자로 읽히지
               않습니다. */
            float body = rbox(nx, ny, 0.26f, 0.66f, 0.09f);
            if (body > 0.0f) {
                a = 1.0f;
                r = H[0]; g = H[1]; b = H[2];
                /* A lit top face and a dark base, so it has a direction. */
                float t = (ny + 0.66f) / 1.32f;
                float sh = 0.55f + 0.55f * t;
                r *= sh; g *= sh; b *= sh;
                float rim = body < 0.07f ? body / 0.07f : 1.0f;
                r *= 0.45f + 0.55f * rim; g *= 0.45f + 0.55f * rim; b *= 0.45f + 0.55f * rim;
            }
            /* A pale collar, which is what stops four coloured shards from
               looking like four of the same object.
               옅은 띠입니다. 네 개의 색 조각이 같은 물체 네 개로 보이지 않게 합니다. */
            if (a > 0.0f && fabsf(ny - 0.18f) < 0.07f && fabsf(nx) < 0.24f) {
                r = 0.92f; g = 0.94f; b = 0.98f;
            }
        }
    }

    if (a <= 0.0f) return 0;
    rgb[0] = (unsigned char)(r > 1 ? 255 : r * 255);
    rgb[1] = (unsigned char)(g > 1 ? 255 : g * 255);
    rgb[2] = (unsigned char)(b > 1 ? 255 : b * 255);
    return 255;
}

static GLuint g_pickup_atlas;

/* Which atlas a drawing is addressed to. The decoder is shared and only the
   cell size and the name-to-row rule differ, so this is the one thing it
   branches on.
   그림이 어느 아틀라스로 향하는지입니다. 디코더는 공유되며 셀 크기와 이름-행 규칙만
   다르므로, 이것이 디코더가 분기하는 유일한 값입니다. */
enum { SPR_DEST_MONSTER, SPR_DEST_WEAPON, SPR_DEST_PICKUP, SPR_DEST_WALL };

/* Defined below, next to the decoder it wraps; declared here because the
   pickup atlas is built above it and the alternative is moving a 200-line
   function to satisfy an ordering rule.
   아래에서 감싸는 디코더 옆에 정의되어 있습니다. 아이템 아틀라스가 그보다 위에서
   생성되며, 대안은 순서 규칙을 만족시키려고 200줄짜리 함수를 옮기는 것뿐이라 이곳에
   선언합니다. */
static void overlay_drawn_sprites(unsigned char *buf, int W, int H, int dest);

GLuint pickup_atlas(void) {
    if (g_pickup_atlas) return g_pickup_atlas;

    int W = PK_CW * PK_KINDS, H = PK_CH;
    unsigned char *buf = malloc(W * H * 4);

    for (int kind = 0; kind < PK_KINDS; kind++)
      for (int y = 0; y < PK_CH; y++)
        for (int x = 0; x < PK_CW; x++) {
            float nx = ((x + 0.5f) / PK_CW - 0.5f) * 2.0f;
            float ny = ((y + 0.5f) / PK_CH - 0.5f) * 2.0f;   /* image-space, +y down */
            unsigned char rgb[3] = {0,0,0};
            int a = pickup_pixel(kind, nx, -ny, rgb);        /* flip so +y is up */
            unsigned char *p = &buf[(y * W + kind * PK_CW + x) * 4];
            p[0] = rgb[0]; p[1] = rgb[1]; p[2] = rgb[2]; p[3] = (unsigned char)a;
        }

    /* Drawn art replaces the generated icon in the cells it fills, the same
       rule the monsters follow -- a kind nobody drew keeps its icon, so a
       half-imported set still shows every item.
       그려진 아트가 자기가 채우는 셀의 생성된 아이콘을 대체합니다. 몬스터와 같은
       규칙이며, 아무도 그리지 않은 종류는 아이콘을 그대로 지녀 절반만 이식된 세트도
       모든 아이템을 보여 줍니다. */
    overlay_drawn_sprites(buf, W, H, SPR_DEST_PICKUP);

    glGenTextures(1, &g_pickup_atlas);
    glBindTexture(GL_TEXTURE_2D, g_pickup_atlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* NEAREST ON BOTH, and this is the atlas the comment at the bottom of this
       file was already claiming: "the monsters take the same treatment for the
       same reason". They did not. These two atlases were the last GL_LINEAR
       left on hand-drawn art, and linear magnification is what was mixing the
       palette -- a drawing quantised to sixteen colours, shown through a filter
       that averages neighbouring texels, puts colours on screen that are in no
       palette and smears every edge the artist drew. The clumping read as a
       quantisation problem and was a filtering one.

       Mipmapping goes with it. This is an ATLAS: every level above zero
       averages across cell borders, so a distant monster picks up the sprite
       stored next to it. That is a second way the same artwork loses its
       colours, and it cannot be fixed by choosing better mip filters.
       Minification aliasing is what mipmaps would have bought, and the
       pixelise pass already resolves the whole world to a small buffer -- the
       sprite is a handful of texels there either way.

       양쪽 모두 NEAREST이며, 이 파일 하단의 주석이 이미 주장하고 있던 바입니다. "몬스터도
       같은 이유로 같은 처리를 받는다." 실제로는 아니었습니다. 이 두 아틀라스가 손으로 그린
       아트에 남아 있던 마지막 GL_LINEAR였고, 선형 확대가 팔레트를 섞고 있었습니다. 16색으로
       양자화된 그림을 이웃 텍셀을 평균 내는 필터로 보여 주면, 어느 팔레트에도 없는 색이
       화면에 나타나고 아티스트가 그린 모든 가장자리가 뭉개집니다. 색 뭉침은 양자화 문제로
       보였지만 필터링 문제였습니다.

       밉맵도 함께 없앱니다. 이것은 *아틀라스*입니다. 0보다 높은 모든 레벨이 셀 경계를 가로질러
       평균을 내므로, 멀리 있는 몬스터가 옆에 저장된 스프라이트의 색을 끌어옵니다. 같은 그림이
       색을 잃는 두 번째 경로이며, 밉 필터를 잘 고른다고 해결되지 않습니다. 밉맵이 사 주는 것은
       축소 시의 에일리어싱인데, 픽셀화 패스가 이미 월드 전체를 작은 버퍼로 리졸브하므로
       스프라이트는 그곳에서 어차피 몇 개의 텍셀입니다. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    free(buf);
    return g_pickup_atlas;
}

void pickup_uv(int kind, float *u0, float *v0, float *u1, float *v1) {
    if (kind < 0) kind = 0;
    if (kind >= PK_KINDS) kind = PK_KINDS - 1;
    float cw = 1.0f / PK_KINDS;
    float insu = 0.5f / (PK_CW * PK_KINDS), insv = 0.5f / PK_CH;
    *u0 = kind * cw + insu;
    *u1 = (kind + 1) * cw - insu;
    *v0 = 1.0f - insv;   /* bottom of the image -> billboard bottom */
    *v1 = insv;
}

/* ---------------------------------------------------------------- atlas */

static GLuint g_atlas;

/**
 * @brief Reads one hex digit, or -1 if the character is not one.
 *
 * ENGLISH
 * -------
 * @param[in] c Character to convert.
 * @return 0..15, or -1 for anything else.
 * @note The sprite data is a flat hex stream rather than the space-separated
 *       integers every other asset language here uses. At one character per
 *       pixel the separators would be half the payload, and there is nothing
 *       to read them for -- every value is exactly one digit wide.
 *
 * 한국어
 * ------
 * @brief 16진수 한 자리를 읽습니다. 해당하지 않는 문자면 -1을 반환합니다.
 * @param[in] c 변환할 문자.
 * @return 0..15, 그 외에는 -1.
 * @note 스프라이트 데이터는 이 프로젝트의 다른 에셋 언어들이 쓰는 공백 구분 정수가
 *       아니라 평면 16진 스트림입니다. 픽셀당 한 문자인 상황에서 구분자는 전체
 *       데이터의 절반을 차지하게 되며, 모든 값이 정확히 한 자리이므로 구분자를 읽을
 *       이유도 없습니다.
 */
/**
 * @brief Maps a sprite-name prefix to a monster type index.
 *
 * ENGLISH
 * -------
 * @param[in] s   Name characters, not null-terminated.
 * @param[in] len How many of them form the prefix (the trailing frame digit
 *                is excluded by the caller).
 * @return The MON_* index, or -1 when the prefix names no monster.
 * @note Compared character by character rather than with strncmp, for the
 *       same reason pickup.c does: pulling the C string functions into a
 *       size-bound build to save four comparisons is a bad trade.
 * @note An unknown prefix returns -1 and the sprite is skipped, so a stray
 *       PNG in assets/sprites/ is ignored rather than painted over whichever
 *       monster happened to be at index 0.
 *
 * 한국어
 * ------
 * @brief 스프라이트 이름의 접두사를 몬스터 종류 인덱스로 변환합니다.
 * @param[in] s   이름 문자들. 널로 끝나지 않습니다.
 * @param[in] len 접두사를 이루는 문자 수. 끝의 프레임 숫자는 호출자가 제외합니다.
 * @return MON_* 인덱스. 접두사가 어떤 몬스터도 가리키지 않으면 -1.
 * @note pickup.c와 같은 이유로 strncmp가 아니라 문자 단위로 비교합니다. 비교 네 번을
 *       줄이려고 크기가 제한된 빌드에 C 문자열 함수를 끌어들이는 것은 손해입니다.
 * @note 알 수 없는 접두사는 -1을 반환하여 해당 스프라이트를 건너뜁니다. 따라서
 *       assets/sprites/에 잘못 들어온 PNG는 0번 인덱스의 몬스터 위에 덧그려지는 대신
 *       무시됩니다.
 */
/* Which atlas a pass of the sprite text is filling.
   스프라이트 텍스트를 한 번 훑을 때 어떤 아틀라스를 채우는지입니다. */

/* Muzzle point per weapon frame, in cell pixels; -1 when the drawing did
   not mark one. Filled by the same pass that decodes the pixels.

   Zero-initialised and then set to -1 in the decode, rather than carrying a
   brace list that has to have exactly WPN_FRAMES rows: a list like that is a
   second place recording how many frames there are, and it silently disagrees
   with the enum the moment a drawing is added or removed.
   무기 프레임별 총구 지점(셀 픽셀 단위)입니다. 그림이 표시하지 않았으면 -1입니다.
   WPN_FRAMES개의 행을 정확히 가져야 하는 중괄호 목록 대신 0으로 초기화한 뒤 디코드에서
   -1로 설정합니다. 그런 목록은 프레임 수를 기록하는 두 번째 장소이며, 그림이 하나
   추가되거나 빠지는 순간 열거형과 조용히 어긋납니다. */
static int g_weapon_muz[WP_TYPES][WPN_FRAMES][2];

/* The viewmodel's name prefix. "gun0" is frame 0 of the weapon, the same way
   "brute0" is frame 0 of the brute -- the name carries the placement, so adding
   art needs no table anywhere.
   뷰 모델의 이름 접두사입니다. "brute0"이 브루트의 프레임 0인 것과 같은 방식으로 "gun0"은
   무기의 프레임 0입니다. 이름이 배치 정보를 담고 있으므로 아트를 추가하는 데 어떤 표도
   필요하지 않습니다. */
/* Which weapon a drawing belongs to, matched against the WEAPONS table's own
   names rather than a list kept here. The table already calls that field the
   sprite prefix, so adding a weapon adds its row and its art with no third
   place to update -- and a name that matches nothing is skipped rather than
   painted over whichever weapon happens to be first.
   그림이 어느 무기의 것인지를, 이곳에 둔 목록이 아니라 WEAPONS 표 자신의 이름과 대조해
   정합니다. 표는 이미 그 필드를 스프라이트 접두사라고 부르므로, 무기를 추가하면 행과
   아트가 함께 생기고 갱신할 세 번째 장소가 없습니다. */
static int weapon_type_for_prefix(const char *s, int len) {
    for (int t = 0; t < WP_TYPES; t++) {
        const char *n = wp_stats(t)->name;
        int i = 0;
        while (i < len && n[i] && n[i] == s[i]) i++;
        if (i == len && !n[i]) return t;
    }
    return -1;
}

/* WALKS THE TABLE, exactly as ::weapon_type_for_prefix above walks wp_stats.
   This was four spelled-out comparisons, which meant a monster's name existed
   twice -- once in enemy.c's TYPES and once here -- and adding a fifth compiled
   clean, ran, and simply never found its art. The sprite would fall back to the
   SDF and look like a creature nobody had drawn yet rather than like a bug.
   ::mon_stats already hands out the name, so there is nothing to keep in step.
   위의 ::weapon_type_for_prefix가 wp_stats를 순회하는 것과 정확히 같이 표를 순회합니다.
   이전에는 철자를 적은 비교 넷이었고, 그 말은 몬스터의 이름이 두 번 존재했다는 뜻입니다.
   enemy.c의 TYPES에 한 번, 이곳에 한 번입니다. 다섯 번째를 추가하면 컴파일도 되고 실행도 되지만
   자기 아트를 결코 찾지 못합니다. 스프라이트는 SDF로 되돌아가고, 결함이 아니라 아직 아무도 그리지
   않은 크리처처럼 보입니다. ::mon_stats가 이미 이름을 내주므로 맞춰 둘 것이 없습니다. */
static int mon_type_for_prefix(const char *s, int len) {
    for (int t = 0; t < MON_TYPES; t++) {
        const char *n = mon_stats(t)->name;
        int i = 0;
        while (i < len && n[i] && n[i] == s[i]) i++;
        if (i == len && !n[i]) return t;
    }
    return -1;
}




/**
 * @brief Blanks one atlas cell before a drawing is painted into it.
 *
 * ENGLISH: A drawn frame OWNS its cell -- the generated creature underneath is
 * erased rather than layered under, because leaving the SDF version below made
 * it bleed out as a halo everywhere the drawing was narrower. Per CELL rather
 * than per atlas, so a half-drawn bestiary still shows generated creatures for
 * the frames no art reached.
 *
 * 한국어: 그려진 프레임이 자기 셀을 *소유*합니다. 아래의 생성된 생물은 겹쳐지지 않고
 * 지워집니다. SDF 버전을 남겨 두면 그림이 더 좁은 모든 곳에서 후광처럼 비쳐 나오기
 * 때문입니다. 아틀라스가 아니라 *셀* 단위이므로, 절반만 그려진 도감도 아트가 닿지 않은
 * 프레임에 대해서는 생성된 생물을 그대로 보여 줍니다.
 */
static void clear_cell(unsigned char *buf, int W, int H,
                       int ox, int oy, int cell_w, int cell_h) {
    for (int cy = 0; cy < cell_h; cy++) {
        int ay = oy + cy;
        if (ay < 0 || ay >= H) continue;
        for (int cx = 0; cx < cell_w; cx++) {
            int ax = ox + cx;
            if (ax < 0 || ax >= W) continue;
            unsigned char *q = &buf[(ay * W + ax) * 4];
            q[0] = q[1] = q[2] = q[3] = 0;
        }
    }
}

/**
 * @struct SprTarget
 * @brief Where a decoded drawing lands, so ::blit_pixels takes five arguments
 *        instead of eleven.
 * / 디코드된 그림이 놓이는 위치입니다. ::blit_pixels가 인자를 열한 개가 아니라 다섯 개만
 *   받도록 합니다.
 */
typedef struct {
    unsigned char *buf;   /**< Atlas being filled. / 채우고 있는 아틀라스. */
    int W, H;             /**< Its dimensions. / 아틀라스의 크기. */
    int x, y;             /**< Where the drawing's top-left lands. / 그림의 좌상단이 놓이는 위치. */
    int sw;               /**< The drawing's width, which wraps its rows. / 그림의 너비. 행을 감는 기준입니다. */
    int total;            /**< Pixels the drawing holds. / 그림이 담은 픽셀 수. */
} SprTarget;


/**
 * @brief Splits a drawing's name into its subject and the frame it names.
 *
 * ENGLISH
 * -------
 * A NAME THAT ENDS IN A LETTER MEANS EVERY FRAME. `brute2` is one frame of the
 * brute; `brute` is all of them.
 *
 * That is not a convenience, it is what a half-finished creature needs. A new
 * monster arrives as ONE drawing -- somebody draws it standing before they
 * draw it walking, attacking and dying -- and until this rule existed the only
 * way to see it in the game was to copy the same file four times under four
 * names. Four identical pictures in the tree, four copies in the binary, and
 * four files to delete one at a time as the real frames arrive.
 *
 * The override falls out of the sort. bake.ps1 emits drawings in name order
 * and `.` sorts before `0`, so `brute` is always decoded before `brute0` --
 * which means a subject-wide drawing lays down every frame and each numbered
 * one painted after it replaces exactly its own. Adding `brute4` later is
 * dropping in a file; nothing else changes.
 *
 * @param[in]  nm       First character of the name.
 * @param[in]  nm_len   How many characters it has.
 * @param[out] body_len The subject: the name with any frame digit removed.
 * @return The frame, or -1 for "every frame of this subject".
 *
 * 한국어
 * ------
 * @brief 그림 이름을 주제와 그것이 가리키는 프레임으로 나눕니다.
 *
 * *글자로 끝나는 이름은 모든 프레임을 뜻합니다.* `brute2`는 브루트의 한 프레임이고
 * `brute`는 전부입니다.
 *
 * 편의가 아니라 절반만 완성된 생물에게 필요한 것입니다. 새 몬스터는 그림 *하나*로
 * 도착합니다. 누군가는 걷고 공격하고 죽는 모습을 그리기 전에 서 있는 모습을 먼저 그립니다.
 * 이 규칙이 생기기 전에는 그것을 게임에서 볼 유일한 방법이 같은 파일을 네 이름으로 네 번
 * 복사하는 것이었습니다. 트리에 똑같은 그림 넷, 바이너리에 사본 넷, 그리고 진짜 프레임이
 * 도착할 때마다 하나씩 지워야 할 파일 넷입니다.
 *
 * 덮어쓰기는 정렬에서 저절로 나옵니다. bake.ps1이 이름 순으로 내보내고 `.`이 `0`보다 앞서므로
 * `brute`는 언제나 `brute0`보다 먼저 디코딩됩니다. 즉 주제 전체 그림이 모든 프레임을 깔고,
 * 그 뒤에 칠해지는 번호 붙은 그림이 정확히 자기 것만 대체합니다. 나중에 `brute4`를 더하는
 * 일은 파일 하나를 떨어뜨리는 일이며, 그 밖에는 아무것도 바뀌지 않습니다.
 *
 * @param[in]  nm       이름의 첫 문자.
 * @param[in]  nm_len   이름의 길이.
 * @param[out] body_len 주제. 프레임 숫자를 뗀 이름입니다.
 * @return 프레임 번호. "이 주제의 모든 프레임"이면 -1.
 */
static int name_frame(const char *nm, int nm_len, int *body_len) {
    char last = (nm_len > 0) ? nm[nm_len - 1] : 0;
    if (last >= '0' && last <= '9') {
        *body_len = nm_len - 1;
        return last - '0';
    }
    *body_len = nm_len;
    return -1;
}

/** @brief How many frames a destination's atlas holds per subject. / 대상의 아틀라스가 주제마다 담는 프레임 수. */
static int frames_in(int dest) {
    return (dest == SPR_DEST_WEAPON) ? WPN_FRAMES
         : (dest == SPR_DEST_MONSTER) ? SPR_FRAMES : 1;
}

/**
 * @brief Works out which atlas slot a sprite's name asks for.
 *
 * ENGLISH
 * -------
 * @param[in]     dest   Which atlas is being filled.
 * @param[in]     nm     Sprite name token.
 * @param[in]     nm_len Its length.
 * @param[in]     want   The one name a WALL pass is looking for; ignored by
 *                       every other destination.
 * @param[in,out] frame  In: the digit split off the name. Out: clamped to the
 *                       destination's range, or zeroed for the destinations
 *                       that are one drawing each.
 * @return The slot index, or -1 when this sprite belongs to another atlas.
 *
 * @note THE ONLY PLACE THE FOUR DESTINATIONS DISAGREE. Everything else in
 *       ::decode_sprites -- the tokenizer, the palette, the RLE, the cell blit
 *       -- is identical for all four, which is why one parser can serve four
 *       atlases. Out here that is visible. Inside, it was forty lines of
 *       if/else in the middle of a three-hundred-line function, and the shared
 *       part looked like it might be destination-specific too.
 *
 * 한국어
 * ------
 * @brief 스프라이트 이름이 요구하는 아틀라스 슬롯을 결정합니다.
 * @param[in]     dest   채우고 있는 아틀라스.
 * @param[in]     nm     스프라이트 이름 토큰.
 * @param[in]     nm_len 그 길이.
 * @param[in]     want   *벽* 패스가 찾는 단 하나의 이름. 다른 대상은 무시합니다.
 * @param[in,out] frame  입력: 이름에서 떼어 낸 숫자. 출력: 대상의 범위로 제한되거나, 그림이
 *                       하나뿐인 대상에 대해서는 0입니다.
 * @return 슬롯 인덱스. 이 스프라이트가 다른 아틀라스에 속하면 -1입니다.
 *
 * @note *네 대상이 서로 달라지는 유일한 지점*입니다. ::decode_sprites의 나머지 전부
 *       (토크나이저, 팔레트, RLE, 셀 배치)는 넷 모두에 동일하며, 그래서 하나의 파서가 네
 *       아틀라스를 담당할 수 있습니다. 밖으로 나오면 그 사실이 보입니다. 안에서는 삼백 줄짜리
 *       함수 한가운데의 마흔 줄 if/else였고, 공유되는 부분마저 대상별로 다른 것처럼
 *       보였습니다.
 */
static int sprite_slot_for(int dest, const char *nm, int nm_len,
                           const char *want, int *frame) {
    int type;
    if (dest == SPR_DEST_PICKUP) {
        /* An `item` prefix, then the very name a level uses to place the
           thing. The prefix is not decoration: without it a drawing called
           `shotgun0` would be both the shotgun's VIEWMODEL and the shotgun
           lying on the floor, and one of the two would silently be the
           other. With it the collision cannot be written.
           `item` 접두사 뒤에 레벨이 그 물건을 배치할 때 쓰는 바로 그 이름이 옵니다.
           접두사는 장식이 아닙니다. 그것이 없으면 `shotgun0`이라는 그림이 샷건의
           *뷰 모델*이자 바닥에 놓인 샷건이 되고, 둘 중 하나가 조용히 다른 하나가
           됩니다. 접두사가 있으면 그 충돌을 쓸 수조차 없습니다. */
        int klen; (void)name_frame(nm, nm_len, &klen);
        const char *k = nm;
        if (klen > 4 && k[0]=='i' && k[1]=='t' && k[2]=='e' && k[3]=='m') {
            type = pickup_kind_for_n(k + 4, klen - 4);
        } else {
            type = -1;
        }
        /* Pickups are one drawing each; the digit only keeps the naming
           rule uniform. */
        *frame = 0;
    } else if (dest == SPR_DEST_WALL) {
        /* THE WHOLE NAME, with no frame digit split off it. A surface is
           one drawing and `wall_brick` ends in a letter, so the trailing
           character is part of the name rather than a frame number -- the
           split every other destination performs would ask for
           `wall_bric` and find nothing.
           Matched against ONE requested name rather than a table, because
           a wall is fetched on demand by the material that wants it: there
           is no atlas of every surface to fill, and building one would
           carry every texture in the game for a level that uses three.
           프레임 숫자를 떼지 않은 *이름 전체*입니다. 표면은 그림 하나이고
           `wall_brick`은 글자로 끝나므로 마지막 문자는 프레임 번호가 아니라 이름의
           일부입니다. 다른 대상들이 하는 분리는 `wall_bric`을 찾게 됩니다.
           표가 아니라 요청된 이름 *하나*와 대조하는 이유는, 벽이 그것을 원하는 재질에
           의해 필요할 때 가져와지기 때문입니다. 채워야 할 전체 표면 아틀라스가 없으며,
           만든다면 셋만 쓰는 레벨을 위해 게임의 모든 텍스처를 싣게 됩니다. */
        type  = (want && txt_is(nm, nm_len, want)) ? 0 : -1;
        *frame = 0;
    } else {
        /* The split decides both halves: which subject the name belongs to and
           which of its frames it fills. A name that ends in a letter leaves
           `*frame` at -1, which ::decode_sprites reads as "all of them".
           분리가 양쪽 절반을 모두 정합니다. 이름이 어느 주제에 속하는지와 그 주제의 어느
           프레임을 채우는지입니다. 글자로 끝나는 이름은 `*frame`을 -1로 남기며,
           ::decode_sprites가 그것을 "전부"로 읽습니다. */
        int blen;
        int f = name_frame(nm, nm_len, &blen);
        type  = (dest == SPR_DEST_WEAPON) ? weapon_type_for_prefix(nm, blen)
                                          : mon_type_for_prefix(nm, blen);
        /* A digit past the end of the atlas is frame 0 rather than a refusal:
           `imp7` is a naming mistake and showing the creature is a better
           answer than showing nothing while saying nothing.
           아틀라스 끝을 넘는 숫자는 거부가 아니라 프레임 0입니다. `imp7`은 이름 실수이며,
           아무 말 없이 아무것도 보여 주지 않는 것보다 생물을 보여 주는 편이 낫습니다. */
        if (f >= frames_in(dest)) f = 0;
        *frame = f;
    }
    return type;
}

/**
 * @brief Paints hand-drawn PNG sprites over the generated atlas.
 *
 * ENGLISH
 * -------
 * @param[in,out] buf Atlas pixels, RGBA, already filled by the SDF path.
 * @param[in]     W   Atlas width in pixels.
 * @param[in]     H   Atlas height in pixels.
 *
 * @note Overlays rather than replaces, which is the whole point. The SDF
 *       creatures stay as the fallback, so a monster with no drawing keeps
 *       the one the code generates and the art can be replaced one sprite at
 *       a time instead of all at once. Deleting sprite.c's generators would
 *       mean drawing every frame of every monster before the game runs again.
 *
 * @note A sprite is placed by NAME: "imp0" is monster type 0, frame 0. The
 *       name carries the position rather than a separate table, so adding a
 *       drawing is dropping a file into assets/sprites/ and nothing else.
 *
 * @note Index 0 is transparent and is skipped, so a sprite smaller than the
 *       cell leaves the generated pixels showing around it rather than
 *       punching a hole. That also means a half-finished drawing composites
 *       over its SDF version, which is usually what you want while working.
 *
 * @warning Requires the baked ASSET_SPRITES text. Silently does nothing when
 *          there are no sprites, which is the state this project ships in
 *          until drawings exist.
 *
 * 한국어
 * ------
 * @brief 손으로 그린 PNG 스프라이트를 생성된 아틀라스 위에 덧그립니다.
 * @param[in,out] buf 아틀라스 픽셀(RGBA). SDF 경로가 이미 채워 둔 상태입니다.
 * @param[in]     W   아틀라스 너비(픽셀).
 * @param[in]     H   아틀라스 높이(픽셀).
 *
 * @note 교체가 아니라 *덧그리기*이며, 이것이 핵심입니다. SDF 생물체가 폴백으로 남으므로
 *       그림이 없는 몬스터는 코드가 생성한 모습을 유지하고, 아트를 한 번에 전부가
 *       아니라 스프라이트 하나씩 교체할 수 있습니다. sprite.c의 생성기를 지우면 게임을
 *       다시 실행하기 전에 모든 몬스터의 모든 프레임을 그려야 합니다.
 *
 * @note 스프라이트는 *이름*으로 배치됩니다. "imp0"은 몬스터 종류 0의 프레임 0입니다.
 *       별도의 테이블이 아니라 이름이 위치 정보를 담고 있으므로, 그림을 추가하는 것은
 *       assets/sprites/에 파일을 넣는 것이 전부입니다.
 *
 * @note 인덱스 0은 투명이며 건너뜁니다. 따라서 셀보다 작은 스프라이트는 구멍을 뚫는
 *       대신 주변에 생성된 픽셀이 그대로 보이게 됩니다. 덕분에 미완성 그림도 SDF
 *       버전 위에 합성되는데, 작업 중에는 대개 그편이 유용합니다.
 *
 * @warning 구워진 ASSET_SPRITES 텍스트가 필요합니다. 스프라이트가 없으면 조용히 아무
 *          동작도 하지 않으며, 그림이 존재하기 전까지 이 프로젝트가 배포되는 상태가
 *          바로 그것입니다.
 */
/* The shipped entry point: decode whatever bake.ps1 produced.
   배포 진입점입니다. bake.ps1이 생성한 것을 디코딩합니다. */
static int decode_sprites(const char *p, unsigned char *buf, int W, int H, int dest, const char *want);

static void overlay_drawn_sprites(unsigned char *buf, int W, int H, int dest) {
    decode_sprites(data_text(DATA_SPRITES), buf, W, H, dest, 0);
}

int sprite_wall(const char *name, unsigned char *rgba) {
    /* Cleared first, so a name that matches nothing leaves a known buffer
       rather than whatever the caller's allocation happened to hold. A missing
       texture should look like a missing texture, not like noise.
       먼저 지웁니다. 어떤 것과도 일치하지 않는 이름이 호출자의 할당에 남아 있던 값이
       아니라 알려진 버퍼를 남기도록 하기 위해서입니다. 없는 텍스처는 잡음이 아니라 없는
       텍스처처럼 보여야 합니다. */
    for (int i = 0; i < SPR_WALL * SPR_WALL * 4; i++) rgba[i] = 0;
    return decode_sprites(data_text(DATA_SPRITES), rgba,
                          SPR_WALL, SPR_WALL, SPR_DEST_WALL, name) > 0;
}

/* The decoder proper, over a caller-supplied text.
 *
 * ENGLISH
 * -------
 * Split from ::overlay_drawn_sprites so a test can hand it a sprite it wrote
 * by hand. The shipped path reads ::data_text, which for sprites is always the
 * baked blob -- there is no file behind DATA_SPRITES, because the PNGs are
 * converted at build time -- so without this parameter the only way to exercise
 * the decoder would be to rebuild the project with different art and look at
 * the screen. That is how this codec came to have no test at all while its
 * format was being changed underneath it.
 *
 * 한국어
 * ------
 * 테스트가 직접 작성한 스프라이트를 넘길 수 있도록 ::overlay_drawn_sprites에서
 * 분리했습니다. 배포 경로는 ::data_text를 읽는데, 스프라이트의 경우 그것은 항상 구워진
 * 텍스트입니다. PNG가 빌드 시점에 변환되므로 DATA_SPRITES 뒤에는 파일이 없습니다.
 * 따라서 이 매개변수가 없으면 디코더를 실행해 볼 유일한 방법은 다른 아트로 프로젝트를
 * 다시 빌드해서 화면을 보는 것뿐입니다. 이 코덱이 형식이 바뀌는 동안에도 테스트가 전혀
 * 없었던 경위가 그것입니다.
 */
/**
 * @brief The drawing the record names, decoded and standing in the atlas.
 *
 * ENGLISH
 * -------
 * Big enough for any PNG ::png_decode will accept, which is what makes the
 * two agree without a shared constant to keep in step. .bss, so it is zeroed
 * at load and costs the floppy nothing.
 *
 * 한국어
 * ------
 * ::png_decode가 받아들이는 어떤 PNG도 담을 만큼 큽니다. 그것이 보조를 맞춰야 할 공용
 * 상수 없이도 둘이 일치하게 만듭니다. .bss이므로 로드 시 0으로 채워지고 플로피 용량이
 * 들지 않습니다.
 */
static unsigned char g_png[PNG_MAX_SIDE * PNG_MAX_SIDE * 4];

/**
 * @brief Paints a decoded drawing into its atlas cell, finding the muzzle.
 *
 * ENGLISH
 * -------
 * THE MUZZLE IS FOUND HERE RATHER THAN RECORDED BY THE BAKE, and that is the
 * half of this change that is not about bytes. bake.ps1 used to scan for the
 * magenta pixel and write its position into the stream, which meant a drawing
 * read straight off disk -- which is what a hot reload now does -- would have
 * carried no marker at all. Reading it out of the pixels means the drawing is
 * the only thing that has to be right.
 *
 * The pixel itself is never drawn. It is a marker, and one magenta dot at the
 * end of a barrel would be the brightest thing on the weapon.
 *
 * 한국어
 * ------
 * @brief 디코딩된 그림을 자기 아틀라스 셀에 칠하고, 총구를 찾습니다.
 *
 * *총구를 베이크가 기록하지 않고 이곳에서 찾으며*, 그것이 이 변경에서 바이트와 무관한
 * 절반입니다. 예전에는 bake.ps1이 마젠타 픽셀을 훑어 그 위치를 스트림에 적었는데, 그러면
 * 디스크에서 바로 읽은 그림(핫 리로드가 지금 하는 일입니다)은 표식을 전혀 지니지 못했을
 * 것입니다. 픽셀에서 읽어 내면 옳아야 하는 것은 그림 하나뿐입니다.
 *
 * 그 픽셀 자체는 결코 그려지지 않습니다. 표식이며, 총열 끝의 마젠타 점 하나는 그 무기에서
 * 가장 밝은 것이 될 것입니다.
 */
static void blit_rgba(const unsigned char *px, int sw, int sh,
                      const SprTarget *t, int *muz_x, int *muz_y) {
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            const unsigned char *q = px + (y * sw + x) * 4;
            int r = q[0], g = q[1], b = q[2], a = q[3];

            /* The same threshold bake.ps1 used when it owned this, kept to the
               number: a marker recognised differently by the two would be a
               muzzle that moves when the art is reloaded.
               bake.ps1이 이것을 소유했을 때 쓰던 것과 같은 임계값이며 숫자까지 그대로입니다.
               둘이 다르게 알아보는 표식은 아트를 다시 읽을 때 움직이는 총구입니다. */
            if (a >= 128 && r > 240 && g < 16 && b > 240) {
                if (muz_x) *muz_x = x;
                if (muz_y) *muz_y = y;
                continue;                       /* recorded, never painted */
            }
            if (a < 128) continue;              /* transparent: leave the cell */

            int dx = t->x + x, dy = t->y + y;
            if (dx < 0 || dy < 0 || dx >= t->W || dy >= t->H) continue;

            unsigned char *o = t->buf + (dy * t->W + dx) * 4;
            o[0] = (unsigned char)r; o[1] = (unsigned char)g;
            o[2] = (unsigned char)b; o[3] = 255;
        }
    }
}

static int decode_sprites(const char *p, unsigned char *buf, int W, int H,
                          int dest, const char *want) {
    int placed = 0;
    if (!p || !*p) return 0;

    /* One cell size per destination. The parser is shared because the
       format is: only where a sprite LANDS depends on what is being
       filled, so a second copy of the decoder for the weapon would be the
       same code with a different bug in it.
       대상마다 셀 크기가 다릅니다. 포맷이 같으므로 파서를 공유합니다. 무엇을 채우는지에
       따라 달라지는 것은 스프라이트가 *어디에 놓이는가*뿐이며, 무기를 위한 두 번째
       디코더 사본은 다른 버그를 가진 같은 코드가 될 뿐입니다. */
    const int cell_w = (dest == SPR_DEST_WEAPON) ? WPN_CW
                     : (dest == SPR_DEST_PICKUP) ? PK_CW
                     : (dest == SPR_DEST_WALL)   ? SPR_WALL : SPR_CW;
    const int cell_h = (dest == SPR_DEST_WEAPON) ? WPN_CH
                     : (dest == SPR_DEST_PICKUP) ? PK_CH
                     : (dest == SPR_DEST_WALL)   ? SPR_WALL : SPR_CH;

    /* "No marker" is -1, and this is the one place that can say so for every
       frame without a second copy of how many frames there are.
       "표식 없음"은 -1이며, 프레임이 몇 개인지에 대한 두 번째 사본 없이 모든 프레임에
       대해 그렇게 말할 수 있는 유일한 장소입니다. */
    if (dest == SPR_DEST_WEAPON) {
        for (int i = 0; i < WPN_FRAMES; i++)
            for (int t = 0; t < WP_TYPES; t++)
                g_weapon_muz[t][i][0] = g_weapon_muz[t][i][1] = -1;
    }

    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;

        if (!txt_is(t, len, "s")) continue;

        /* `s <name> <bytes>`, then that many bytes of PNG.
           THE LENGTH IS WHAT SEPARATES RECORDS, not a delimiter -- a PNG can
           contain any byte, including whatever a delimiter would have been,
           and the .map blob is length-delimited for exactly this reason. It is
           also why the tokeniser is only ever asked about the header: it never
           sees a picture, so a NUL inside one cannot end the walk.
           `s <이름> <바이트 수>` 다음에 그만큼의 PNG 바이트가 옵니다.
           *레코드를 가르는 것은 길이이지 구분자가 아닙니다.* PNG는 구분자가 되었을 무엇을
           포함해 어떤 바이트든 담을 수 있으며, .map 블롭이 길이로 구분되는 이유가 정확히
           그것입니다. 토크나이저에게 헤더만 묻는 이유이기도 합니다. 그것은 그림을 결코 보지
           않으므로, 그림 안의 NUL이 순회를 끝낼 수 없습니다. */
        const char *nm = txt_token(p, &len);
        if (!nm) break;
        int nm_len = len;
        p = nm + len;

        int bytes = 0, ok = 1;
        p = txt_read_int(p, &bytes, &ok);
        if (!ok || bytes <= 0) break;

        /* Exactly one space separates the header from the picture, but the
           skip is written as "whitespace" rather than "one byte": a PNG begins
           0x89, which is not whitespace, so this cannot walk into one.
           헤더와 그림 사이에는 정확히 공백 하나가 있지만, 건너뛰기를 "1바이트"가 아니라
           "공백"으로 씁니다. PNG는 0x89로 시작하고 그것은 공백이 아니므로, 이것이 그림
           안으로 걸어 들어갈 수 없습니다. */
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        const unsigned char *png = (const unsigned char *)p;
        p += bytes;

        /* The name is "<subject><frame>": the trailing digit is the frame and
           what precedes it selects the row. Parsed here rather than looked up
           in a table so a new drawing needs no code change.
           이름은 "<주제><프레임>" 형식입니다. 끝의 숫자가 프레임이고 그 앞부분이 행을
           결정합니다. 새 그림에 코드 수정이 필요 없도록 테이블 조회 대신 해석합니다. */
        int frame = -1;
        int type  = sprite_slot_for(dest, nm, nm_len, want, &frame);

        /* A drawing addressed to the other atlas is skipped without being
           decoded. It used to be decoded anyway, because the stream could only
           be advanced by consuming it; a length can be stepped over, so the
           weapon pass no longer pays to unpack every monster.
           다른 아틀라스로 향하는 그림은 디코딩하지 않고 건너뜁니다. 예전에는 스트림을
           소비해야만 진행할 수 있었으므로 어차피 디코딩했습니다. 길이는 건너뛸 수 있으므로,
           이제 무기 패스가 모든 몬스터를 푸는 비용을 치르지 않습니다. */
        if (type < 0) continue;

        int sw = 0, sh = 0;
        if (!png_decode(png, bytes, g_png, (int)sizeof g_png, &sw, &sh))
            continue;                    /* png.c has already raised DIAG_PNG */

        /* -1 is "every frame", which is what a creature that has been drawn
           once but not yet animated says. See ::name_frame.
           -1은 "모든 프레임"이며, 한 번 그려졌지만 아직 애니메이션되지 않은 생물이 하는
           말입니다. ::name_frame을 참조하십시오. */
        int f0 = (frame < 0) ? 0 : frame;
        int f1 = (frame < 0) ? frames_in(dest) : frame + 1;
        placed++;

        for (int f = f0; f < f1; f++) {
        int ox = (dest == SPR_DEST_WALL)   ? 0
               : (dest == SPR_DEST_PICKUP) ? type * cell_w : f * cell_w;
        int oy = (dest == SPR_DEST_WALL)   ? 0
               : (dest == SPR_DEST_PICKUP) ? 0             : type  * cell_h;

        /* A DRAWN FRAME OWNS ITS CELL: clear the generated creature out of it
           before painting. The two are not layers of one picture, and leaving
           the SDF version underneath means every place the drawing is narrower
           than it shows through as a halo -- which is exactly what the first
           Freedoom import looked like, a green shape standing behind the
           creature and a horn poking out above it.
           Cleared per CELL rather than per atlas, so this keeps the property
           it is named for: a bestiary that is only half drawn still shows
           creatures, because a frame with no art never reaches this line and
           keeps its generated one.
           그려진 프레임이 자기 셀을 소유합니다. 칠하기 전에 생성된 생물을 지웁니다. 둘은 한
           그림의 레이어가 아니며, SDF 버전을 아래에 남겨 두면 그림이 더 좁은 모든 곳에서
           그것이 후광처럼 비쳐 나옵니다. 첫 Freedoom 이식이 정확히 그렇게 보였습니다.
           아틀라스가 아니라 *셀* 단위로 지우므로 이름이 가리키는 성질이 유지됩니다. 아트가
           없는 프레임은 이 줄에 닿지 않아 생성된 것을 그대로 갖습니다. */
        clear_cell(buf, W, H, ox, oy, cell_w, cell_h);

        /* WHERE THE DRAWING SITS IN ITS CELL. Every drawing this project ships
           is exactly its cell, so both of these are zero -- the formula is kept
           because a drawing does not have to be: a 32x32 sprite in a 64x96 cell
           should stand on the ground rather than float at the top, and that is
           a rule about cells rather than about the current art.
           The bake used to crop each drawing to its ink and record where the
           crop came from, which is what these two numbers read. Cropping was a
           size optimisation for an encoding that no longer exists.
           그림이 셀 안 어디에 앉는지입니다. 이 프로젝트가 배포하는 모든 그림은 정확히 자기
           셀이므로 둘 다 0입니다. 수식을 남겨 두는 이유는 그림이 반드시 그럴 필요는 없기
           때문입니다. 64x96 셀 안의 32x32 스프라이트는 위에 떠 있지 않고 바닥에 서야 하며,
           그것은 현재 아트가 아니라 셀에 대한 규칙입니다.
           예전에는 베이크가 그림을 잉크에 맞춰 자르고 그 위치를 기록했으며, 이 두 숫자가 그것을
           읽었습니다. 자르기는 이제 존재하지 않는 인코딩을 위한 크기 최적화였습니다. */
        int place_x = (cell_w - sw) / 2;
        int place_y = (cell_h - sh);
        if (place_x < 0) place_x = 0;
        if (place_y < 0) place_y = 0;

        /* A WALL SMALLER THAN ITS CELL TILES INTO IT. Every other destination
           holds a drawing of a thing -- a creature, a gun, a pickup -- and a
           drawing narrower than its cell should sit in the cell, which is what
           the two numbers above compute. A WALL IS NOT A DRAWING OF A THING.
           It is a patch of surface whose whole job is to repeat, so a 64x64
           wall in a 128x128 cell wants four copies and not one copy with a
           border.
           IT USED TO GET THE BORDER, and tools/texprobe.c measured it: a
           material built from `wall_meat`, the one 64x64 surface this project
           ships, came out 75.0% pure black -- exactly (128^2 - 64^2)/128^2, the
           cleared remainder of its cell. textures.txt says of the tile count
           that it is "the buffer's 256 divided by the source's side", which is
           the right sampling rule and was never a fix for undersized art:
           ::fill_from_image samples the whole cell, so what it repeated four
           times was the drawing AND the black around it.
           Nothing pointed at it because no shipped map used `wall_meat`. An
           imported map that names a 64x64 surface would have, on every face --
           and `lqdm1` names six. That is also why `wall_meat` itself is gone:
           once shipped maps walked this branch, the drawing that only a test
           had ever walked it with was carrying nothing but its own bytes.
           벽이 자기 셀보다 작으면 셀 안으로 *타일링*됩니다. 다른 모든 대상은 어떤 것의
           그림을 담습니다. 생물, 총, 획득물이며, 셀보다 좁은 그림은 셀 안에 앉아야 하고
           위의 두 숫자가 그것을 계산합니다. *벽은 어떤 것의 그림이 아닙니다.* 반복하는 것이
           일의 전부인 표면 조각이므로, 128x128 셀 안의 64x64 벽이 원하는 것은 사본 넷이지
           테두리 두른 사본 하나가 아닙니다.
           *예전에는 테두리를 얻었고* tools/texprobe.c가 그것을 쟀습니다. 이 프로젝트가
           출하하는 유일한 64x64 표면인 `wall_meat`로 만든 재질은 75.0%가 순수한 검정으로
           나왔습니다. (128^2 - 64^2)/128^2, 곧 지워진 셀의 나머지와 정확히 같습니다.
           아무것도 이것을 지목하지 않은 이유는 출하되는 어떤 맵도 `wall_meat`를 쓰지 않았기
           때문입니다. 64x64 표면을 지목하는 가져온 맵이라면 모든 면에서 그랬을 것입니다. */
        if (dest == SPR_DEST_WALL && sw > 0 && sh > 0 &&
            (sw < cell_w || sh < cell_h)) {
            for (int ty = 0; ty < cell_h; ty += sh)
                for (int tx = 0; tx < cell_w; tx += sw) {
                    int mx = -1, my = -1;
                    SprTarget t = { buf, W, H, ox + tx, oy + ty, sw, sw * sh };
                    blit_rgba(g_png, sw, sh, &t, &mx, &my);
                }
            continue;
        }

        int muz_x = -1, muz_y = -1;
        SprTarget tgt = { buf, W, H, ox + place_x, oy + place_y, sw, sw * sh };
        blit_rgba(g_png, sw, sh, &tgt, &muz_x, &muz_y);

        /* The muzzle and the pixels agree by construction now: both come out
           of the same decode, placed by the same two numbers. A flash computed
           from a different placement than the barrel it belongs to is the exact
           failure the marker exists to prevent.
           이제 총구와 픽셀은 구조적으로 일치합니다. 둘 다 같은 디코딩에서 나와 같은 두
           숫자로 배치됩니다. 총열과 다른 배치로 계산된 화염은 바로 이 표식이 막으려는
           실패입니다. */
        if (dest == SPR_DEST_WEAPON && muz_x >= 0 && f < WPN_FRAMES) {
            g_weapon_muz[type][f][0] = place_x + muz_x;
            g_weapon_muz[type][f][1] = place_y + muz_y;
        }
        }
    }
    return placed;
}

GLuint sprite_atlas(void) {
    if (g_atlas) return g_atlas;

    int W = SPR_CW * SPR_FRAMES, H = SPR_CH * MON_TYPES;
    unsigned char *buf = malloc(W * H * 4);

    for (int type = 0; type < MON_TYPES; type++) {
        for (int fr = 0; fr < SPR_FRAMES; fr++) {
            for (int y = 0; y < SPR_CH; y++) {
                /* Bottom image row is the creature's feet (ny=0). */
                float ny = 1.0f - (y + 0.5f) / SPR_CH;
                ny *= 1.15f;                       /* headroom for horns/ears */
                for (int x = 0; x < SPR_CW; x++) {
                    float nx = ((x + 0.5f) / SPR_CW - 0.5f) * 2.0f;

                    unsigned char rgb[3] = {0,0,0};
                    int a = creature_pixel(type, fr, nx, ny, rgb);

                    if (a) {
                        float n = m_hashf((unsigned)(x*131 + y*977 + fr*613 + type*29)) - 0.5f;
                        for (int k = 0; k < 3; k++) {
                            float c = rgb[k] + n * 16.0f;
                            rgb[k] = (unsigned char)(c < 0 ? 0 : c > 255 ? 255 : c);
                        }
                    }

                    int px = fr * SPR_CW + x;
                    int py = type * SPR_CH + y;
                    unsigned char *p = &buf[(py * W + px) * 4];
                    p[0] = rgb[0]; p[1] = rgb[1]; p[2] = rgb[2]; p[3] = (unsigned char)a;
                }
            }
        }
    }

    /* Hand-drawn sprites go on last, over the generated ones. See
       overlay_drawn_sprites for why this composites rather than replaces.
       손으로 그린 스프라이트를 마지막에 생성된 것 위에 덧그립니다. 교체가 아니라
       합성인 이유는 overlay_drawn_sprites를 참조하십시오. */
    overlay_drawn_sprites(buf, W, H, SPR_DEST_MONSTER);

    glGenTextures(1, &g_atlas);
    glBindTexture(GL_TEXTURE_2D, g_atlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* NEAREST ON BOTH, and this is the atlas the comment at the bottom of this
       file was already claiming: "the monsters take the same treatment for the
       same reason". They did not. These two atlases were the last GL_LINEAR
       left on hand-drawn art, and linear magnification is what was mixing the
       palette -- a drawing quantised to sixteen colours, shown through a filter
       that averages neighbouring texels, puts colours on screen that are in no
       palette and smears every edge the artist drew. The clumping read as a
       quantisation problem and was a filtering one.

       Mipmapping goes with it. This is an ATLAS: every level above zero
       averages across cell borders, so a distant monster picks up the sprite
       stored next to it. That is a second way the same artwork loses its
       colours, and it cannot be fixed by choosing better mip filters.
       Minification aliasing is what mipmaps would have bought, and the
       pixelise pass already resolves the whole world to a small buffer -- the
       sprite is a handful of texels there either way.

       양쪽 모두 NEAREST이며, 이 파일 하단의 주석이 이미 주장하고 있던 바입니다. "몬스터도
       같은 이유로 같은 처리를 받는다." 실제로는 아니었습니다. 이 두 아틀라스가 손으로 그린
       아트에 남아 있던 마지막 GL_LINEAR였고, 선형 확대가 팔레트를 섞고 있었습니다. 16색으로
       양자화된 그림을 이웃 텍셀을 평균 내는 필터로 보여 주면, 어느 팔레트에도 없는 색이
       화면에 나타나고 아티스트가 그린 모든 가장자리가 뭉개집니다. 색 뭉침은 양자화 문제로
       보였지만 필터링 문제였습니다.

       밉맵도 함께 없앱니다. 이것은 *아틀라스*입니다. 0보다 높은 모든 레벨이 셀 경계를 가로질러
       평균을 내므로, 멀리 있는 몬스터가 옆에 저장된 스프라이트의 색을 끌어옵니다. 같은 그림이
       색을 잃는 두 번째 경로이며, 밉 필터를 잘 고른다고 해결되지 않습니다. 밉맵이 사 주는 것은
       축소 시의 에일리어싱인데, 픽셀화 패스가 이미 월드 전체를 작은 버퍼로 리졸브하므로
       스프라이트는 그곳에서 어차피 몇 개의 텍셀입니다. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    free(buf);
    return g_atlas;
}

/* Debug-only: pulls in stdio, which is 25KB of CRT this project otherwise
   never touches (the game prints through Win32's wsprintfA). Compiled only
   into the tools and the dev build, never the shipped exe. */
#ifdef HOT_RELOAD
#include <stdio.h>
int sprite_dump_ppm(const char *path) {
    int W = SPR_CW * SPR_FRAMES, H = SPR_CH * MON_TYPES;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", W, H);

    /* Build the SAME buffer sprite_atlas builds, rather than calling
       creature_pixel again here.
       This used to re-derive the image from the SDF generators directly,
       which quietly made it a picture of only half the atlas: hand-drawn
       sprites are composited on afterward, so a drawing could be correctly
       decoded and placed and still be invisible in the dump. That cost real
       debugging time -- the first PNG overlay looked broken when what was
       broken was the tool looking at it.
       여기서 creature_pixel을 다시 호출하지 않고 sprite_atlas가 만드는 것과 *동일한*
       버퍼를 만듭니다.
       이전에는 SDF 생성기로부터 이미지를 다시 유도했는데, 그 탓에 이 도구가 조용히
       아틀라스의 절반만 보여 주게 되었습니다. 손으로 그린 스프라이트는 이후에 합성되므로,
       그림이 올바르게 디코딩되어 배치되었는데도 덤프에서는 보이지 않을 수 있었습니다.
       실제로 디버깅 시간을 소모했습니다. 첫 PNG 오버레이가 고장 난 것처럼 보였지만
       고장 난 것은 그것을 들여다보는 도구였습니다. */
    unsigned char *buf = malloc((size_t)W * H * 4);
    if (!buf) { fclose(f); return 0; }

    for (int type = 0; type < MON_TYPES; type++)
      for (int fr = 0; fr < SPR_FRAMES; fr++)
        for (int y = 0; y < SPR_CH; y++) {
          float ny = (1.0f - (y + 0.5f) / SPR_CH) * 1.15f;
          for (int x = 0; x < SPR_CW; x++) {
            float nx = ((x + 0.5f) / SPR_CW - 0.5f) * 2.0f;
            unsigned char rgb[3] = {0,0,0};
            int a = creature_pixel(type, fr, nx, ny, rgb);
            unsigned char *p = &buf[((type * SPR_CH + y) * W + fr * SPR_CW + x) * 4];
            p[0] = rgb[0]; p[1] = rgb[1]; p[2] = rgb[2];
            p[3] = (unsigned char)a;
          }
        }

    overlay_drawn_sprites(buf, W, H, SPR_DEST_MONSTER);

    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        unsigned char *p = &buf[(y * W + x) * 4];
        unsigned char rgb[3] = { p[0], p[1], p[2] };
        if (!p[3]) {                        /* checkerboard behind the cutout */
            int c = (((x >> 3) ^ (y >> 3)) & 1) ? 60 : 40;
            rgb[0] = rgb[1] = rgb[2] = (unsigned char)c;
        }
        fwrite(rgb, 1, 3, f);
      }

    free(buf);
    fclose(f);
    return 1;
}
#endif  /* HOT_RELOAD */

void sprite_uv(int type, int frame, float *u0, float *v0, float *u1, float *v1) {
    if (type  < 0) type  = 0;
    if (type  >= MON_TYPES)  type  = MON_TYPES - 1;
    if (frame < 0) frame = 0;
    if (frame >= SPR_FRAMES) frame = SPR_FRAMES - 1;

    float cw = 1.0f / SPR_FRAMES, ch = 1.0f / MON_TYPES;
    float insu = 0.5f / (SPR_CW * SPR_FRAMES);
    float insv = 0.5f / (SPR_CH * MON_TYPES);

    *u0 = frame * cw + insu;
    *u1 = (frame + 1) * cw - insu;
    /* v flipped within the type's row: image row 0 is the top, but a
       billboard's v grows upward. */
    *v0 = (type + 1) * ch - insv;   /* bottom of the row -> feet */
    *v1 = type * ch + insv;         /* top of the row -> head */
}

/* ------------------------------------------------- the hand-drawn viewmodel */

static GLuint g_weapon_atlas;
static int    g_weapon_built, g_weapon_any;

/**
 * @brief Builds the weapon atlas, and settles whether there is any art at all.
 *
 * ENGLISH
 * -------
 * Unlike the monster atlas there is no generator underneath: a cell with no
 * drawing stays fully transparent, and a weapon with no frames at all reports
 * ::weapon_has_art zero so the caller draws the extruded model instead.
 *
 * @note The buffer starts CLEARED rather than filled, which is what makes the
 *       fallback work by construction. The monsters start filled by the SDF
 *       pass and are drawn over; here there is nothing to draw over, so an
 *       undrawn frame has to be nothing rather than whatever the heap held.
 * @note `g_weapon_any` is decided by whether any pixel arrived, not by whether
 *       the text mentioned a gun. A file that decoded to nothing -- an empty
 *       PNG, a name that did not match -- should fall back rather than draw an
 *       invisible weapon, and "the gun disappeared" is a much worse first
 *       experience of adding art than "the art did not take".
 *
 * 한국어
 * ------
 * @brief 무기 아틀라스를 생성하고, 아트가 존재하는지 여부를 확정합니다.
 *
 * 몬스터 아틀라스와 달리 아래에 깔린 생성기가 없습니다. 그림이 없는 셀은 완전히 투명하게
 * 남고, 프레임이 하나도 없는 무기는 ::weapon_has_art가 0을 보고하므로 호출자가 대신 압출
 * 모델을 그립니다.
 *
 * @note 버퍼를 채우지 않고 *비운 채* 시작하며, 그것이 폴백을 구조적으로 성립시킵니다.
 *       몬스터는 SDF 패스가 채운 위에 덧그리지만 이곳에는 덧그릴 대상이 없으므로, 그려지지
 *       않은 프레임은 힙에 남아 있던 값이 아니라 아무것도 아니어야 합니다.
 * @note `g_weapon_any`는 텍스트가 총기를 언급했는지가 아니라 픽셀이 실제로 도착했는지로
 *       결정됩니다. 아무것도 디코딩되지 않은 파일(빈 PNG, 일치하지 않는 이름)은 보이지 않는
 *       무기를 그리는 대신 폴백해야 합니다. "총이 사라졌다"는 것은 아트를 추가하며 겪는 첫
 *       경험으로는 "아트가 적용되지 않았다"보다 훨씬 나쁩니다.
 */
static void weapon_build(void) {
    if (g_weapon_built) return;
    g_weapon_built = 1;

    /* A row per weapon, the way the monster atlas has a row per creature.
       One row would mean every weapon drawing the same gun, which is what it
       did while there was only one drawing to draw.
       몬스터 아틀라스가 생물마다 행을 갖듯 무기마다 행을 둡니다. 한 행이면 모든 무기가
       같은 총을 그리게 되며, 그릴 그림이 하나뿐이던 동안은 실제로 그러했습니다. */
    int W = WPN_CW * WPN_FRAMES, H = WPN_CH * WP_TYPES;
    /* calloc: the sheet has one cell per (frame, weapon) and not every pair is
       drawn, so the gaps must already be transparent.
       calloc입니다. 시트는 (프레임, 무기)마다 칸을 하나씩 갖지만 모든 조합이 그려지지는
       않으므로, 빈칸은 이미 투명해야 합니다. */
    unsigned char *buf = calloc((size_t)W * H * 4, 1);
    if (!buf) return;

    overlay_drawn_sprites(buf, W, H, SPR_DEST_WEAPON);

    /* Any opaque pixel means art arrived. */
    for (int i = 0; i < W * H; i++)
        if (buf[i * 4 + 3]) { g_weapon_any = 1; break; }

    if (g_weapon_any) {
        glGenTextures(1, &g_weapon_atlas);
        glBindTexture(GL_TEXTURE_2D, g_weapon_atlas);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, buf);
        /* NEAREST, and no mipmaps: this is pixel art shown at roughly its own
           scale, and any filtering would soften the edges the artist drew.
           The monsters take the same treatment for the same reason.
           NEAREST이며 밉맵도 없습니다. 이것은 대략 자기 크기로 표시되는 픽셀 아트이며,
           어떤 필터링도 아티스트가 그린 가장자리를 뭉갭니다. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    free(buf);
}

int weapon_has_art(void) {
    weapon_build();
    return g_weapon_any;
}

GLuint weapon_atlas(void) {
    weapon_build();
    return g_weapon_atlas;
}

void weapon_uv(int type, int frame, float *u0, float *v0, float *u1, float *v1) {
    if (type < 0) type = 0;
    if (type >= WP_TYPES) type = WP_TYPES - 1;
    if (frame < 0) frame = 0;
    if (frame >= WPN_FRAMES) frame = WPN_FRAMES - 1;

    float cw = 1.0f / WPN_FRAMES, ch = 1.0f / WP_TYPES;
    float insu = 0.5f / (WPN_CW * WPN_FRAMES);
    float insv = 0.5f / (WPN_CH * WP_TYPES);

    *u0 = frame * cw + insu;
    *u1 = (frame + 1) * cw - insu;
    /* v flipped: buffer row 0 is the TOP of the drawing, and the quad's v grows
       upward, so the quad's bottom takes the row's high v and its top the low.
       Rows run DOWN the buffer, so weapon `type` lives at v in [t*ch,(t+1)*ch)
       -- not measured back from 1.0. Getting that backwards selects the LAST
       weapon's row for the first, which is a shotgun that draws a chainsaw.
       v를 뒤집습니다. 버퍼의 0번 행은 그림의 *위쪽*이고 쿼드의 v는 위로 증가하므로,
       쿼드의 아래가 그 행의 높은 v를, 위가 낮은 v를 가져갑니다. 행은 버퍼를 따라
       아래로 진행하므로 무기 `type`은 v가 [t*ch, (t+1)*ch)인 구간에 있으며 1.0에서
       거꾸로 재는 것이 아닙니다. 이를 반대로 하면 첫 무기가 *마지막* 무기의 행을
       고르게 되고, 그 결과가 전기톱을 그리는 샷건입니다. */
    *v0 = (type + 1) * ch - insv;
    *v1 = type * ch + insv;
}

/**
 * @brief Where a weapon frame's muzzle sits, as a fraction of its cell.
 *
 * ENGLISH
 * -------
 * @param[in]  frame One of the WPN_* frames; clamped.
 * @param[out] u     0..1 across the cell, left to right.
 * @param[out] v     0..1 up the cell, BOTTOM to top.
 * @return Non-zero when the drawing marked a muzzle, 0 when it did not.
 *
 * @note Normalised rather than in pixels, because the caller draws the sprite
 *       at whatever size the screen calls for and a pixel offset would only be
 *       right at one scale.
 * @note `v` is measured from the bottom to match the quad it will be placed on,
 *       where the image's own rows run the other way. Doing the flip here means
 *       exactly one place knows about it.
 *
 * 한국어
 * ------
 * @brief 무기 프레임의 총구가 셀 안에서 차지하는 위치를 비율로 반환합니다.
 * @param[in]  frame WPN_* 프레임 중 하나. 범위를 벗어나면 제한됩니다.
 * @param[out] u     셀을 가로지르는 0..1 값. 왼쪽에서 오른쪽.
 * @param[out] v     셀을 올라가는 0..1 값. *아래*에서 위로.
 * @return 그림이 총구를 표시했으면 0이 아닌 값, 표시하지 않았으면 0.
 *
 * @note 픽셀이 아니라 정규화된 값입니다. 호출자가 화면이 요구하는 크기로 스프라이트를
 *       그리므로, 픽셀 오프셋은 한 배율에서만 맞기 때문입니다.
 * @note `v`는 놓이게 될 쿼드에 맞추어 아래에서부터 잽니다. 이미지 자체의 행은 반대
 *       방향으로 진행합니다. 뒤집기를 이곳에서 처리하면 그것을 아는 곳이 정확히 하나가
 *       됩니다.
 */
int weapon_muzzle(int type, int frame, float *u, float *v) {
    weapon_build();
    if (type < 0) type = 0;
    if (type >= WP_TYPES) type = WP_TYPES - 1;
    if (frame < 0) frame = 0;
    if (frame >= WPN_FRAMES) frame = WPN_FRAMES - 1;

    /* Fall back to frame 0's marker: only the firing frame strictly needs one,
       and an artist who marked the idle pose and not the rest should not get a
       flash that jumps to a corner on the frames they skipped.
       0번 프레임의 표식으로 대체합니다. 엄밀히 표식이 필요한 것은 발사 프레임뿐이며,
       대기 자세만 표시하고 나머지를 건너뛴 아티스트가 그 프레임들에서 화염이 구석으로
       튀는 결과를 얻어서는 안 됩니다. */
    int mx = g_weapon_muz[type][frame][0], my = g_weapon_muz[type][frame][1];
    if (mx < 0) { mx = g_weapon_muz[type][0][0]; my = g_weapon_muz[type][0][1]; }
    if (mx < 0) return 0;

    *u = (mx + 0.5f) / (float)WPN_CW;
    *v = 1.0f - (my + 0.5f) / (float)WPN_CH;
    return 1;
}

#ifdef HOT_RELOAD
/* Test hook: decode a sprite text the caller wrote, into a buffer it owns.
   See sprite.h. Guarded like sprite_dump_ppm, so the shipped binary carries
   neither.
   테스트 훅입니다. 호출자가 작성한 스프라이트 텍스트를 호출자 소유 버퍼로 디코딩합니다.
   sprite_dump_ppm과 같이 가드되므로 배포 바이너리에는 둘 다 들어가지 않습니다. */
void sprite_decode_blob(const char *text, unsigned char *rgba, int W, int H,
                        int weapon) {
    decode_sprites(text, rgba, W, H, weapon ? SPR_DEST_WEAPON : SPR_DEST_MONSTER, 0);
}

/* The alphabet, one character at a time, so tools/sprtest.c can hold it against
   the string bake.ps1 encodes with. That contract is between a PowerShell
   script and a C file and no compiler can see it.
   알파벳을 문자 단위로 노출하여 tools/sprtest.c가 bake.ps1이 인코딩에 쓰는 문자열과
   대조할 수 있게 합니다. 그 계약은 PowerShell 스크립트와 C 파일 사이에 있으며 어떤
   컴파일러도 볼 수 없습니다. */

int sprite_weapon_muzzle_px(int type, int frame, int *x, int *y) {
    if (frame < 0 || frame >= WPN_FRAMES) return 0;
    if (g_weapon_muz[type][frame][0] < 0) return 0;
    *x = g_weapon_muz[type][frame][0];
    *y = g_weapon_muz[type][frame][1];
    return 1;
}
#endif
