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
#include "data.h"
#include "txt.h"
#include "enemy.h"        /* MON_* -- the atlas row order */
#include "pickup.h"       /* PK_* -- the pickup atlas order */
#include "tex.h"          /* tex_hashf, for a little surface grain */
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

/* ------------------------------------------------------------------ imp */

static const unsigned char PAL_IMP[C_COUNT][3] = {
    { 150,  58,  40 }, { 176,  96,  62 }, {  96,  34,  26 },
    { 208, 198, 168 }, { 255, 224,  70 }, {  30,  10,  12 },
    { 200,  60,  60 },   /* C_GLOW: red-hot maw */
};

static int imp_pixel(int fr, float nx, float ny, unsigned char *rgb) {
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
        return shade(win, best, ny / 0.28f, 0.0f, PAL_IMP, rgb);
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
    return shade(win, best, ny, glow, PAL_IMP, rgb);
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

/* ----------------------------------------------------------------- hound */

static const unsigned char PAL_HOUND[C_COUNT][3] = {
    {  86, 108,  66 },   /* body: sickly green */
    { 110, 128,  84 },   /* belly */
    {  56,  70,  44 },   /* legs */
    { 226, 224, 200 },   /* fangs, claws */
    { 240, 255, 110 },   /* eyes: pale yellow-green */
    {  20,  10,  10 },   /* maw */
    { 200,  60,  60 },   /* C_GLOW: red-hot maw */
};

/* A Pinky-style charging beast: a bulky body on stubby legs, mostly a huge
   fanged head. The gaping maw is the whole read -- it says "monster" even a few
   pixels tall, which a low quadruped silhouette does not. */
static int hound_pixel(int fr, float nx, float ny, unsigned char *rgb) {
    float leg = 0.0f, maw = 0.0f, crouch = 0.0f, lunge = 0.0f;
    switch (fr) {
    case SPR_WALK0:  leg =  0.06f; break;
    case SPR_WALK1:  leg = -0.06f; break;
    case SPR_ATTACK: maw = 1.0f; lunge = 0.05f; break;
    case SPR_HURT:   crouch = 0.05f; break;
    default: break;
    }

    if (fr == SPR_DEAD) {
        float best = -1e9f; int win = C_BODY;
        part(ell(nx, ny, 0.0f, 0.11f, 0.78f, 0.16f), C_BODY, &best, &win);
        part(ell(nx, ny, 0.0f, 0.12f, 0.34f, 0.09f), C_MAW,  &best, &win);
        part(cap(nx, ny, -0.55f, 0.14f, -0.66f, 0.06f, 0.03f), C_HORN, &best, &win);
        return shade(win, best, ny / 0.30f, 0.0f, PAL_HOUND, rgb);
    }

    ny += crouch;
    float best = -1e9f; int win = C_BODY;

    /* Four stubby legs. */
    part(cap(nx, ny, -0.24f, 0.24f, -0.26f - leg, 0.0f, 0.075f), C_LIMB, &best, &win);
    part(cap(nx, ny,  0.24f, 0.24f,  0.26f + leg, 0.0f, 0.075f), C_LIMB, &best, &win);
    part(cap(nx, ny, -0.42f, 0.22f, -0.46f + leg, 0.0f, 0.075f), C_LIMB, &best, &win);
    part(cap(nx, ny,  0.42f, 0.22f,  0.46f - leg, 0.0f, 0.075f), C_LIMB, &best, &win);

    /* A muscular hunched body... */
    part(ell(nx, ny, 0.0f, 0.52f, 0.40f, 0.26f), C_BODY,  &best, &win);
    /* ...that is mostly a huge head thrust forward. */
    part(ell(nx, ny, 0.0f, 0.44f, 0.46f, 0.34f), C_BODY,  &best, &win);
    part(ell(nx, ny, 0.0f, 0.40f, 0.30f, 0.20f), C_BELLY, &best, &win);
    /* Two horn nubs on the brow. */
    part(cap(nx, ny, -0.22f, 0.70f, -0.30f, 0.86f, 0.035f), C_HORN, &best, &win);
    part(cap(nx, ny,  0.22f, 0.70f,  0.30f, 0.86f, 0.035f), C_HORN, &best, &win);

    if (best <= 0.0f) return 0;

    /* Small close-set eyes glaring over the mouth. */
    if (ell(nx, ny, -0.13f, 0.58f, 0.055f, 0.06f) > 0.0f ||
        ell(nx, ny,  0.13f, 0.58f, 0.055f, 0.06f) > 0.0f) win = C_EYE;

    /* The maw dominates the face: a wide dark gash, wider when it lunges, with
       a ring of fangs biting in from the lips. */
    float my = 0.30f - lunge;
    float mouth = ell(nx, ny, 0.0f, my, 0.34f, 0.09f + maw * 0.09f);
    if (mouth > 0.0f) {
        win = C_MAW;
        /* Fangs: fold x to the nearest tooth slot, and bite in from whichever
           lip is nearer, so teeth line the top and bottom of the opening. */
        float fx = nx - 0.12f * floorf(nx / 0.12f + 0.5f);
        float lip = 0.09f + maw * 0.09f - fabsf(ny - my);   /* distance inside the maw */
        if (fabsf(fx) < 0.028f && lip < 0.055f) win = C_HORN;
    }

    float glow = (win == C_MAW && maw > 0.0f) ? 1.0f : 0.0f;
    return shade(win, best, ny, glow, PAL_HOUND, rgb);
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
   Tall, narrow and cyan against three squat warm-coloured melee types, so
   "the one that shoots" is legible from across a room, which is the whole job
   of a ranged enemy's silhouette. */
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

/* -------------------------------------------------------------- dispatch */

static int creature_pixel(int type, int fr, float nx, float ny, unsigned char *rgb) {
    switch (type) {
    case MON_BRUTE:  return brute_pixel(fr, nx, ny, rgb);
    case MON_HOUND:  return hound_pixel(fr, nx, ny, rgb);
    case MON_CASTER: return caster_pixel(fr, nx, ny, rgb);
    default:         return imp_pixel(fr, nx, ny, rgb);
    }
}

/* --------------------------------------------------------------- pickups
 *
 * Small floor icons. Same alpha-silhouette trick as the monsters, but a
 * single frame each and simpler shapes: a shell box and a medkit, chosen to
 * read at a glance from across a room. */

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
    } else { /* PK_HEALTH */
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
    }

    if (a <= 0.0f) return 0;
    rgb[0] = (unsigned char)(r > 1 ? 255 : r * 255);
    rgb[1] = (unsigned char)(g > 1 ? 255 : g * 255);
    rgb[2] = (unsigned char)(b > 1 ? 255 : b * 255);
    return 255;
}

static GLuint g_pickup_atlas;

GLuint pickup_atlas(void) {
    if (g_pickup_atlas) return g_pickup_atlas;

    int W = PK_CW * PK_KINDS, H = PK_CH;
    unsigned char *buf = HeapAlloc(GetProcessHeap(), 0, W * H * 4);

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

    glGenTextures(1, &g_pickup_atlas);
    glBindTexture(GL_TEXTURE_2D, g_pickup_atlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    HeapFree(GetProcessHeap(), 0, buf);
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
static int mon_type_for_prefix(const char *s, int len) {
    if (len == 3 && s[0]=='i' && s[1]=='m' && s[2]=='p')                     return MON_IMP;
    if (len == 5 && s[0]=='b' && s[1]=='r' && s[2]=='u' && s[3]=='t' && s[4]=='e') return MON_BRUTE;
    if (len == 5 && s[0]=='h' && s[1]=='o' && s[2]=='u' && s[3]=='n' && s[4]=='d') return MON_HOUND;
    if (len == 6 && s[0]=='c' && s[1]=='a' && s[2]=='s' && s[3]=='t' && s[4]=='e' && s[5]=='r') return MON_CASTER;
    return -1;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
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
static void overlay_drawn_sprites(unsigned char *buf, int W, int H) {
    const char *p = data_text(DATA_SPRITES);
    if (!p || !*p) return;

    unsigned char pal[16][3] = {{0,0,0}};
    int n_pal = 0;

    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;

        /* The shared palette, once, before any sprite. */
        if (txt_is(t, len, "pal")) {
            int ok = 1;
            p = txt_read_int(p, &n_pal, &ok);
            if (!ok) break;
            if (n_pal > 16) n_pal = 16;
            for (int i = 0; i < n_pal; i++) {
                const char *h = txt_token(p, &len);
                if (!h || len < 6) { i = n_pal; break; }
                p = h + len;
                for (int k = 0; k < 3; k++) {
                    int hi = hexval(h[k*2]), lo = hexval(h[k*2+1]);
                    pal[i][k] = (unsigned char)((hi < 0 ? 0 : hi) * 16 + (lo < 0 ? 0 : lo));
                }
            }
            continue;
        }

        if (!txt_is(t, len, "s")) continue;

        /* s <name> <w> <h> */
        const char *nm = txt_token(p, &len);
        if (!nm) break;
        int nm_len = len;
        p = nm + len;

        int sw = 0, sh = 0, ok = 1;
        p = txt_read_int(p, &sw, &ok);
        p = txt_read_int(p, &sh, &ok);
        if (!ok || sw <= 0 || sh <= 0) continue;

        /* The name is "<monster><frame>": the trailing digit is the frame and
           what precedes it selects the row. Parsed here rather than looked up
           in a table so a new drawing needs no code change.
           이름은 "<몬스터><프레임>" 형식입니다. 끝의 숫자가 프레임이고 그 앞부분이
           행을 결정합니다. 새 그림에 코드 수정이 필요 없도록 테이블 조회 대신 이곳에서
           해석합니다. */
        int frame = nm[nm_len - 1] - '0';
        if (frame < 0 || frame >= SPR_FRAMES) frame = 0;

        int type = mon_type_for_prefix(nm, nm_len - 1);

        /* The data line: 'd' run-length, 'f' one digit per pixel. */
        const char *op = txt_token(p, &len);
        if (!op) break;
        int is_rle = txt_is(op, len, "d");
        p = op + len;

        const char *data = txt_token(p, &len);
        if (!data) break;
        p = data + len;

        /* Decode straight into the atlas cell. Out-of-range types still
           consume their data so the stream stays in sync -- the same reason
           mesh.c keeps parsing faces of meshes it is not building.
           아틀라스 셀에 바로 디코딩합니다. 범위를 벗어난 종류도 데이터를 소비하여
           스트림 동기화를 유지하는데, 이는 mesh.c가 생성하지 않는 메시의 면도 계속
           파싱하는 것과 같은 이유입니다. */
        if (type < 0) continue;

        int ox = frame * SPR_CW, oy = type * SPR_CH;
        int px_i = 0, total = sw * sh;

        for (int i = 0; i < len && px_i < total; ) {
            int count, index;
            if (is_rle) {
                if (i + 1 >= len) break;
                count = hexval(data[i]);
                index = hexval(data[i+1]);
                i += 2;
                if (count < 0 || index < 0) break;
            } else {
                index = hexval(data[i]);
                i += 1;
                count = 1;
                if (index < 0) break;
            }

            for (int r = 0; r < count && px_i < total; r++, px_i++) {
                if (index == 0) continue;          /* transparent: leave the SDF pixel */

                int sx = px_i % sw, sy = px_i / sw;
                /* Centre the drawing in its cell horizontally and sit it on
                   the cell's bottom, so a 32x32 sprite in a 64x96 cell stands
                   on the ground rather than floating at the top.
                   그림을 셀 안에서 가로로 가운데 맞추고 셀 바닥에 놓습니다. 그래야
                   64x96 셀 안의 32x32 스프라이트가 위쪽에 떠 있지 않고 지면에
                   섭니다. */
                int ax = ox + (SPR_CW - sw) / 2 + sx;
                int ay = oy + (SPR_CH - sh)     + sy;
                if (ax < 0 || ax >= W || ay < 0 || ay >= H) continue;

                unsigned char *q = &buf[(ay * W + ax) * 4];
                q[0] = pal[index][0];
                q[1] = pal[index][1];
                q[2] = pal[index][2];
                q[3] = 255;
            }
        }
    }
}

GLuint sprite_atlas(void) {
    if (g_atlas) return g_atlas;

    int W = SPR_CW * SPR_FRAMES, H = SPR_CH * MON_TYPES;
    unsigned char *buf = HeapAlloc(GetProcessHeap(), 0, W * H * 4);

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
                        float n = tex_hashf((unsigned)(x*131 + y*977 + fr*613 + type*29)) - 0.5f;
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
    overlay_drawn_sprites(buf, W, H);

    glGenTextures(1, &g_atlas);
    glBindTexture(GL_TEXTURE_2D, g_atlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    HeapFree(GetProcessHeap(), 0, buf);
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
    unsigned char *buf = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)W * H * 4);
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

    overlay_drawn_sprites(buf, W, H);

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

    HeapFree(GetProcessHeap(), 0, buf);
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
