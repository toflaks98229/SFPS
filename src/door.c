/**
 * @file door.c
 * @brief Drives the door state machine and writes each door's travel back into the level.
 *
 * ENGLISH
 * -------
 * THE SECTOR IS WHAT MOVES. There is no door object at run time that anything
 * collides against. A door is a definition (::DoorDef) plus a little motion
 * state (::DoorState), and the only thing this file produces is a rewritten
 * sector -- a raised ceiling, a lowered floor, a slid outline. Everything that
 * collides reads those fields already and needs to know nothing about doors.
 *
 * TWO MODELS, ONE MECHANISM. A sector door is a floor plan and cannot move as
 * a body, so ::DOOR_UP raises its ceiling and leaves its floor. A brush door
 * IS a body and the whole leaf slides, which is what a mapper placing a Quake
 * `func_door` expects. ::apply picks between them; everything above it -- the
 * trigger, the timing, the key check -- is shared and does not know which kind
 * it is driving.
 *
 * MEASURED AGAINST THE CLOSED SHAPE, ALWAYS. ::door_reset snapshots each
 * door's closed outline and every proximity test below uses that snapshot
 * rather than where the door has got to. A sliding door measured from its
 * current position would walk away from the player who opened it and stall
 * halfway.
 *
 * THE STATE AND THE DEFINITION AGREE BY INDEX AND BY NOTHING ELSE.
 * ::door_reset is what agrees them, and a level stepped without one arrives
 * holding another level's closed shapes. That is checked for rather than
 * trusted -- see ::DIAG_DOOR_STALE, which is raised in two places below.
 *
 * @note Touches no GL. The whole machine is arithmetic over structs, so
 *       tools/doortest.c drives a door through its full cycle with no window.
 * @note HOLDS NO STATE OF ITS OWN. Every entry point takes the ::Level whose
 *       doors it is about, and the motion lives in ::Level::door_run. See
 *       door.h for why that matters and what it fixed.
 * @warning ::door_update rebuilds the level's lookup grid when anything moved.
 *          A caller that steps doors and then reads ::sector_at without going
 *          through this function will be consulting a grid that describes
 *          where the doors used to be.
 *
 * 한국어
 * ------
 * *움직이는 것은 섹터입니다.* 실행 시점에 무언가 충돌할 문 객체 같은 것은 없습니다. 문은
 * 정의(::DoorDef)와 약간의 이동 상태(::DoorState)이며, 이 파일이 만들어 내는 것은 다시 쓰인
 * 섹터뿐입니다. 올라간 천장, 내려간 바닥, 미끄러진 외곽선입니다. 충돌하는 모든 것은 이미 그
 * 필드들을 읽으며 문에 대해 아무것도 알 필요가 없습니다.
 *
 * *두 모델, 하나의 기구입니다.* 섹터 문은 평면도이고 하나의 몸으로 움직일 수 없으므로
 * ::DOOR_UP은 천장을 올리고 바닥은 그대로 둡니다. 브러시 문은 *몸 자체*이고 문짝 전체가
 * 미끄러지며, 그것이 Quake의 `func_door`를 놓는 제작자가 기대하는 동작입니다. ::apply가 둘
 * 사이를 고릅니다. 그 위의 모든 것(작동 조건, 시간 진행, 열쇠 검사)은 공유되며 자신이 어느
 * 종류를 구동하는지 알지 못합니다.
 *
 * *언제나 닫힌 형상을 기준으로 잽니다.* ::door_reset이 각 문의 닫힌 외곽선을 스냅숏하고, 아래의
 * 모든 근접 판정은 문이 도달한 위치가 아니라 그 스냅숏을 씁니다. 현재 위치를 기준으로 잰
 * 미닫이문은 그것을 연 플레이어에게서 멀어져 중간에 멈춰 버립니다.
 *
 * *상태와 정의는 인덱스로만 대응하며 다른 무엇으로도 대응하지 않습니다.* 둘을 일치시키는 것은
 * ::door_reset이고, 그것 없이 진행된 레벨은 다른 레벨의 닫힌 형상을 든 채 도달합니다. 그것을
 * 신뢰하지 않고 검사합니다. 아래 두 곳에서 발생시키는 ::DIAG_DOOR_STALE을 참조하십시오.
 *
 * @note GL을 건드리지 않습니다. 기구 전체가 구조체에 대한 연산이므로 tools/doortest.c가 창
 *       없이도 문을 전체 주기에 걸쳐 구동합니다.
 * @note *자체 상태를 지니지 않습니다.* 모든 진입점은 자신이 다루는 문을 지닌 ::Level을 받고,
 *       이동은 ::Level::door_run에 있습니다. 그것이 왜 중요하고 무엇을 고쳤는지는 door.h를
 *       참조하십시오.
 * @warning ::door_update는 무언가 움직였을 때 레벨의 조회 격자를 다시 만듭니다. 이 함수를
 *          거치지 않고 문을 진행시킨 뒤 ::sector_at을 읽는 호출자는, 문이 *예전에* 있던
 *          자리를 서술하는 격자를 참조하게 됩니다.
 */

#include "door.h"

#include <math.h>

#include "audio.h"
#include "brush.h"  /* brush_translate -- what a door moves when it is brushes */
#include "diag.h"   /* DIAG_DOOR_STALE -- state that outlived the level it described */

/* DoorState and DoorSet are declared in level.h, because ::Level holds one.
   That is where the note on why lives; what follows is the only code that
   writes either.
   DoorState와 DoorSet은 level.h에 선언되어 있습니다. ::Level이 그것을 담기 때문입니다. 왜
   그런지에 대한 설명이 그곳에 있으며, 아래는 그 둘에 기록하는 유일한 코드입니다. */

/* --- File-local constants / 파일 지역 상수 --- */

/**
 * @brief Keycard names for the HUD, indexed by BIT POSITION.
 *
 * ENGLISH
 * -------
 * KEY_RED is `1<<0`, so it is entry [0]. Ordered to match the KEY_* enum,
 * which is also the order pickup.h's PK_KEY0..PK_KEY_LAST run in, so a
 * keycard's colour, its pickup sprite and this name are all the same index
 * rather than three lists agreeing by habit.
 *
 * 한국어
 * ------
 * @brief HUD를 위한 키카드 이름. *비트 위치*로 색인합니다.
 *
 * KEY_RED는 `1<<0`이므로 항목 [0]입니다. KEY_* enum의 순서와 같고, 그것은 pickup.h의
 * PK_KEY0..PK_KEY_LAST 순서이기도 하므로, 키카드의 색·아이템 스프라이트·이 이름이 습관으로
 * 일치하는 세 목록이 아니라 같은 색인이 됩니다.
 */
static const char *const KEY_NAME[] = { "RED", "BLUE", "YELLOW" };

/* The table and the enum have to end together. Adding a key without a name
   here would print an empty string on a door nobody can open, which looks like
   a bug in the door rather than a missing line in a table.
   표와 enum은 함께 끝나야 합니다. 여기에 이름 없이 열쇠를 추가하면 아무도 열 수 없는 문에
   빈 문자열이 표시되며, 표에 빠진 한 줄이 아니라 문의 결함처럼 보입니다. */
_Static_assert(sizeof(KEY_NAME) / sizeof(KEY_NAME[0]) == KEY_KINDS,
               "KEY_NAME must name every KEY_* bit");

/**
 * @brief Which way each axis travels, as a unit vector.
 *
 * ENGLISH
 * -------
 * One table, because a leaf goes the same way whichever model it is made of --
 * ::apply_brush slides brushes along it, ::door_travel reports it, and anything
 * stuck to a door follows it.
 *
 * @note Indexed by ::DoorDef::axis, so every read must range-check that field
 *       first. It arrives from a parsed .map file and is not trusted.
 *
 * 한국어
 * ------
 * @brief 각 축이 어느 방향으로 이동하는가를 단위 벡터로 나타냅니다.
 *
 * 표가 하나인 이유는 문짝이 어느 모델로 만들어졌든 같은 방향으로 가기 때문입니다.
 * ::apply_brush가 그 방향으로 브러시를 밀고, ::door_travel이 그것을 보고하며, 문에 붙은 것은
 * 무엇이든 그것을 따라갑니다.
 *
 * @note ::DoorDef::axis로 색인하므로, 읽기 전에 반드시 그 필드의 범위를 검사해야 합니다. 그
 *       값은 파싱된 .map 파일에서 오며 신뢰하지 않습니다.
 */
static const v3 DOOR_DIR[DOOR_AXES] = {
    { 0.0f,  1.0f, 0.0f },   /* DOOR_UP   */
    { 0.0f, -1.0f, 0.0f },   /* DOOR_DOWN */
    { 1.0f,  0.0f, 0.0f },   /* DOOR_X    */
    { 0.0f,  0.0f, 1.0f }    /* DOOR_Z    */
};

/* --- static function prototypes / 정적 함수 원형 --- */

/* Geometry against the door's CLOSED outline / 문의 *닫힌* 외곽선에 대한 기하 연산 */
static v3    door_centre(const DoorState *st);
static float dist_to_outline(const DoorState *st, float x, float z);
static int   inside_outline(const DoorState *st, float x, float z);

/* Writing travel back into the level / 이동량을 레벨에 되쓰기 */
static void  apply_brush(Level *l, const DoorDef *d, DoorState *st);
static void  apply(Level *l, const DoorDef *d, DoorState *st);

/* One door's whole frame / 문 하나의 한 프레임 전체 */
static int   door_step_one(Level *l, int i, v3 player_pos, int keys,
                           int tagged, float dt);

/* --- Public function definitions: setup / 공개 함수 정의: 준비 --- */

void door_reset(Level *l) {
    DoorSet *ds = &l->door_run;

    ds->count   = l->n_doors > LVL_MAX_DOORS ? LVL_MAX_DOORS : l->n_doors;
    ds->refused = KEY_NONE;

    /* Cleared with everything else: a message about the last level's locked
       door has no business surviving into the next one.
       나머지와 함께 초기화합니다. 이전 레벨의 잠긴 문에 대한 메시지가 다음 레벨까지
       살아남을 이유는 없습니다. */
    ds->notice_key = KEY_NONE;
    ds->notice_t   = 0.0f;

    /* Every key any door on this level asks for, folded into one mask. The HUD
       reads it to decide which card slots to show at all.
       이 레벨의 어떤 문이든 요구하는 모든 열쇠를 하나의 마스크로 접어 넣습니다. HUD는 어느
       카드 칸을 아예 표시할지 정하기 위해 이것을 읽습니다. */
    ds->keys = KEY_NONE;
    for (int i = 0; i < ds->count; i++) ds->keys |= l->doors[i].key;

    for (int i = 0; i < ds->count; i++) {
        DoorState *st = &ds->d[i];
        st->t = 0.0f;
        st->wait = 0.0f;
        st->opening = 0;
        st->applied = 0.0f;

        int si = l->doors[i].sector;

        /* A BRUSH DOOR HAS NO OUTLINE TO SNAPSHOT. Its closed position is where
           the .map drew it, which is where it is: level_load has just parsed
           the text. So `applied` at zero is the whole of the record, and `n0`
           is set to one only so the stale-state guard below and in
           ::door_step_one treats the slot as live rather than as a door that
           was never captured.
           브러시 문에는 스냅숏할 외곽선이 없습니다. 닫힌 위치는 .map이 그린 자리이고 그곳이
           바로 지금 있는 자리입니다. level_load가 방금 텍스트를 파싱했기 때문입니다. 따라서
           `applied`가 0인 것이 기록의 전부이며, `n0`를 1로 두는 이유는 오직 아래와
           ::door_step_one의 낡은 상태 검사가 이 슬롯을 포착된 적 없는 문이 아니라 살아 있는
           것으로 취급하게 하기 위함입니다. */
        if (si < 0) {
            st->sector = -1;
            st->n0 = 0;
            if (l->doors[i].n_brushes <= 0 || !l->brushes) continue;

            /* The union of the leaf's brush boxes, which is the plan footprint
               the proximity tests below measure against. Degenerate brushes
               (min past max) are skipped rather than folded in, because one
               would blow the box out to the whole level.
               문짝을 이루는 브러시 박스들의 합집합이며, 아래의 근접 판정이 기준으로 삼는 평면
               발자국입니다. 축퇴된 브러시(min이 max를 넘는 것)는 접어 넣지 않고 건너뜁니다.
               하나만 있어도 박스가 레벨 전체로 부풀기 때문입니다. */
            v3 lo = v3f(1e30f, 1e30f, 1e30f), hi = v3f(-1e30f, -1e30f, -1e30f);
            for (int k = 0; k < l->doors[i].n_brushes; k++) {
                const Brush *b = &l->brushes->brushes[l->doors[i].first_brush + k];
                if (b->min.x > b->max.x) continue;
                if (b->min.x < lo.x) lo.x = b->min.x;
                if (b->min.y < lo.y) lo.y = b->min.y;
                if (b->min.z < lo.z) lo.z = b->min.z;
                if (b->max.x > hi.x) hi.x = b->max.x;
                if (b->max.y > hi.y) hi.y = b->max.y;
                if (b->max.z > hi.z) hi.z = b->max.z;
                st->n0 = 1;      /* live: something was captured */
            }
            st->lo0 = lo;
            st->hi0 = hi;
            continue;
        }

        if (si >= l->n_sectors) { st->n0 = 0; st->sector = -1; continue; }

        /* Recorded with the shape, not before it: the two are one fact, and a
           slot that carried a sector index but no outline would claim a
           provenance for nothing.
           형상과 함께 기록하며 그 전에 하지 않습니다. 둘은 하나의 사실이며, 섹터 인덱스는
           있고 외곽선은 없는 슬롯은 아무것도 아닌 것에 대한 출처를 주장하게 됩니다. */
        st->sector = (short)si;

        const Sector *s = &l->sectors[si];
        st->floor0 = s->floor;
        st->ceil0  = s->ceil;
        st->n0     = s->n;
        /* Two shorts per point, x and z interleaved -- the same packing
           ::Sector::pts uses, copied rather than reinterpreted.
           점마다 short 둘이며 x와 z가 번갈아 놓입니다. ::Sector::pts가 쓰는 것과 같은 배치를
           재해석하지 않고 그대로 복사합니다. */
        for (int k = 0; k < s->n * 2; k++) st->pts0[k] = s->pts[k];
    }
}

/* --- Public function definitions: stepping / 공개 함수 정의: 진행 --- */

int door_update(Level *l, v3 player_pos, int keys, float dt) {
    int moved = 0;
    l->door_run.refused = KEY_NONE;

    /* Counted down before the touch tests below, which may re-arm it. Order
       matters only in that a refusal this frame must not be shortened by this
       frame's own dt.
       아래의 접촉 검사보다 먼저 감소시킵니다. 검사가 다시 채울 수 있기 때문입니다. 순서가
       중요한 이유는, 이번 프레임의 거절이 이번 프레임의 dt만큼 깎여서는 안 되기
       때문입니다. */
    if (l->door_run.notice_t > 0.0f) l->door_run.notice_t -= dt;

    /* --- how many doors are there, really? --------------------------------
       Two answers, and they can still disagree: `door_run.count` is how many
       states ::door_reset captured, and `n_doors` is how many definitions the
       level holds. They now live in the same struct, which removes the way they
       used to disagree -- a state array left over from ANOTHER level -- but not
       this one: ::level_load clears the count and a caller that never called
       door_reset arrives here with 0 against a level full of doors.
       Walking to the larger would read a slot nobody filled in, so the loops
       below walk to the smaller and the disagreement is counted rather than
       absorbed.
       답이 둘이며 여전히 어긋날 수 있습니다. `door_run.count`는 ::door_reset이 포착한 상태의
       수이고 `n_doors`는 레벨이 담은 정의의 수입니다. 이제 둘은 같은 구조체 안에 있으며, 그것이
       이전에 둘이 어긋나던 경로(*다른* 레벨에서 남은 상태 배열)를 없앱니다. 그러나 이 경로는
       남습니다. ::level_load가 개수를 비우므로, door_reset을 부른 적 없는 호출자는 문이 가득 찬
       레벨에 대해 0을 들고 이곳에 도달합니다. 큰 쪽까지 순회하면 아무도 채우지 않은 슬롯을 읽게
       되므로, 아래의 루프들은 작은 쪽까지만 돌고 그 불일치는 흡수되지 않고 계수됩니다. */
    int n_level = l->n_doors > LVL_MAX_DOORS ? LVL_MAX_DOORS : l->n_doors;
    int n_state = l->door_run.count;
    if (n_level != n_state) DIAG(DIAG_DOOR_STALE);
    int n = n_level < n_state ? n_level : n_state;

    /* Switch entities, gathered once: a tagged door asks whether anything is
       standing on a switch that names it. Touch-activated, so there is no key
       to press and no aim to get right -- see the request this was built for.
       스위치 엔티티를 한 번에 모읍니다. 태그가 있는 문은 자신을 지목하는 스위치 위에
       무언가 서 있는지 묻습니다. 접촉식이므로 누를 키도, 맞춰야 할 조준도 없습니다. */
    int touched_tag[LVL_MAX_DOORS];
    for (int i = 0; i < LVL_MAX_DOORS; i++) touched_tag[i] = 0;

    for (int e = 0; e < l->n_ents; e++) {
        const Entity *en = &l->ents[e];
        /* "switch<n>": the trailing digits are the tag it fires. Parsed from
           the name for the reason every other kind is -- a drawing, an entity
           and a tag in one word means no second table to keep in step.
           "switch<n>" 형식이며 끝의 숫자가 발동시키는 태그입니다. 다른 모든 종류와 같은
           이유로 이름에서 해석합니다. */
        if (!(en->kind[0]=='s'&&en->kind[1]=='w'&&en->kind[2]=='i'&&
              en->kind[3]=='t'&&en->kind[4]=='c'&&en->kind[5]=='h')) continue;

        int tag = 0;
        for (int c = 6; en->kind[c] >= '0' && en->kind[c] <= '9'; c++)
            tag = tag * 10 + (en->kind[c] - '0');

        /* Squared distance against a squared radius, so the loop costs no
           square root per entity per frame.
           반지름의 제곱과 거리의 제곱을 비교합니다. 그래야 이 루프가 프레임마다 엔티티마다
           제곱근을 치르지 않습니다. */
        float dx = player_pos.x - en->x * 0.01f;
        float dz = player_pos.z - en->z * 0.01f;
        if (dx*dx + dz*dz > DOOR_SWITCH_DIST * DOOR_SWITCH_DIST) continue;

        for (int i = 0; i < n; i++)
            if (l->doors[i].tag == tag) touched_tag[i] = 1;
    }

    /* Trigger VOLUMES, which is the brush model's shape for the same idea. A
       switch is a point with a radius around it and a trigger is the space
       somebody drew, so the test is "inside" rather than "within". Their
       brushes are not solid -- that is what lets the player be inside one --
       which is exactly why this asks ::brush_point_in and not the sweep.
       트리거 *부피*이며, 같은 개념에 대한 브러시 모델의 형태입니다. 스위치는 점과 그 둘레의
       반경이고 트리거는 누군가 그린 공간이므로, 판정은 "이내"가 아니라 "안"입니다. 그
       브러시는 고체가 아니고 그것이 플레이어가 안에 있을 수 있게 하는 것이며, 그래서 이곳은
       스윕이 아니라 ::brush_point_in에 묻습니다. */
    if (l->brushes) {
        for (int t = 0; t < l->n_triggers; t++) {
            const TriggerDef *tr = &l->triggers[t];
            if (!brush_point_in(l->brushes, tr->first_brush, tr->n_brushes,
                                player_pos)) continue;
            for (int i = 0; i < n; i++)
                if (l->doors[i].tag == tr->tag) touched_tag[i] = 1;
        }
    }

    for (int i = 0; i < n; i++)
        if (door_step_one(l, i, player_pos, keys, touched_tag[i], dt))
            moved = 1;

    /* A slid door changes which grid cells it occupies, and the grid is what
       sector_at consults first. Rebuilt once for the whole frame rather than
       per door, because it is a whole-level structure and building it four
       times would be building it three times for nothing.
       미끄러진 문은 자신이 차지하는 격자 셀을 바꾸며, 격자는 sector_at이 먼저 참조하는
       것입니다. 문마다가 아니라 프레임당 한 번 다시 만듭니다. 레벨 전체의 구조이므로, 네
       번 만드는 것은 세 번을 헛되이 만드는 것입니다. */
    if (moved) level_grid_build(l);

    return moved;
}

/* --- Public function definitions: queries / 공개 함수 정의: 조회 --- */

v3 door_travel(const Level *l, int i, float t) {
    if (!l || i < 0 || i >= l->door_run.count) return v3f(0, 0, 0);

    const DoorDef *d = &l->doors[i];
    /* The axis comes from a parsed file, so it is range-checked before it
       indexes ::DOOR_DIR.
       축은 파싱된 파일에서 오므로, ::DOOR_DIR을 색인하기 전에 범위를 검사합니다. */
    if (d->axis < 0 || d->axis >= DOOR_AXES) return v3f(0, 0, 0);

    /* Centimetres to metres, scaled by how far open the door is asked about.
       `t` is a parameter rather than the door's current state so a caller can
       ask about a position the door held earlier -- which is exactly what
       decal.c does to move a mark by the door's change.
       센티미터를 미터로 바꾸고, 물어본 열림 정도만큼 배율을 적용합니다. `t`가 문의 현재
       상태가 아니라 매개변수인 이유는, 호출자가 문이 이전에 있던 위치를 물을 수 있게 하기
       위함입니다. decal.c가 문의 변화량만큼 자국을 옮기려고 하는 일이 정확히 그것입니다. */
    return v3scale(DOOR_DIR[d->axis], d->amount * 0.01f * t);
}

float door_openness(const Level *l, int i) {
    return (i >= 0 && i < l->door_run.count) ? l->door_run.d[i].t : 0.0f;
}

int door_refused(const Level *l) { return l->door_run.refused; }

int door_notice_key(const Level *l) {
    return l->door_run.notice_t > 0.0f ? l->door_run.notice_key : KEY_NONE;
}

float door_notice_left(const Level *l) {
    return l->door_run.notice_t > 0.0f ? l->door_run.notice_t : 0.0f;
}

const char *door_key_name(int key) {
    /* The FIRST bit set wins. A door asks for one card, and a mask that
       somehow carries two names the lower-numbered one rather than refusing to
       name anything.
       *처음* 설정된 비트가 이깁니다. 문은 카드 하나를 요구하며, 어쩌다 둘을 담은 마스크는
       아무 이름도 대지 않는 대신 번호가 낮은 쪽의 이름을 댑니다. */
    for (int i = 0; i < KEY_KINDS; i++)
        if (key & (1 << i)) return KEY_NAME[i];
    return "";
}

int door_keys_used(const Level *l) {
    /* Asked of the DOORS rather than of the level's pickups: the question the
       HUD is answering is "which cards can this map demand", and a level that
       scatters a key no door wants would light a row the player never needs.
       아이템이 아니라 *문*에게 묻습니다. HUD가 답하는 질문은 "이 맵이 요구할 수 있는
       카드는 무엇인가"이며, 어떤 문도 원하지 않는 열쇠를 뿌린 레벨은 플레이어에게 결코
       필요 없는 행을 켜게 됩니다. */
    return l->door_run.keys;
}

/* --- static helpers: geometry against the closed outline / 정적 보조 함수: 닫힌 외곽선에 대한 기하 --- */

/**
 * @brief The middle of the door's CLOSED outline, in metres -- where the sound of it moving comes from.
 *
 * ENGLISH
 * -------
 * @param[in] st Motion state holding the snapshot ::door_reset captured.
 * @return The centre in world metres, or the origin when nothing was captured.
 *
 * @note The closed shape rather than the current one, for the same reason the
 *       touch test uses it: a sliding door that measured from where it has got
 *       to would walk away from the player who opened it.
 * @note A brush door's centre is the middle of its bounding box; a sector
 *       door's is the mean of its outline points, whose stored units are
 *       centimetres and are converted here.
 *
 * 한국어
 * ------
 * @brief 문의 *닫힌* 외곽선의 한가운데를 미터로 반환합니다. 그것이 문이 움직이는 소리가 나는
 *        자리입니다.
 * @param[in] st ::door_reset이 포착한 스냅숏을 지닌 이동 상태.
 * @return 월드 미터 단위의 중심. 포착된 것이 없으면 원점입니다.
 *
 * @note 현재 모양이 아니라 닫힌 모양을 쓰는 이유는 접촉 판정과 같습니다. 도달한 위치를
 *       기준으로 재면, 미닫이문은 그것을 연 플레이어에게서 멀어져 갑니다.
 * @note 브러시 문의 중심은 바운딩 박스의 한가운데이고, 섹터 문의 중심은 외곽선 점들의
 *       평균입니다. 저장 단위는 센티미터이며 이곳에서 변환합니다.
 */
static v3 door_centre(const DoorState *st) {
    float sx = 0.0f, sz = 0.0f;
    if (st->n0 <= 0) return v3f(0.0f, 0.0f, 0.0f);
    if (st->sector < 0) return v3scale(v3add(st->lo0, st->hi0), 0.5f);
    for (int i = 0; i < st->n0; i++) {
        sx += st->pts0[i * 2 + 0];
        sz += st->pts0[i * 2 + 1];
    }
    float y = (st->floor0 + st->ceil0) * 0.5f;
    return v3f(sx / st->n0 * 0.01f, y * 0.01f, sz / st->n0 * 0.01f);
}

/**
 * @brief The closest a point gets to the door's CLOSED outline, in metres.
 *
 * ENGLISH
 * -------
 * @param[in] st Motion state holding the snapshot ::door_reset captured.
 * @param[in] x  World X of the point, in metres.
 * @param[in] z  World Z of the point, in metres.
 * @return The distance in metres, or 1e9f for a door with no usable outline.
 *
 * @note Measured against the CLOSED shape rather than the current one, so a
 *       door that has started opening does not walk away from the player who
 *       opened it and stall halfway.
 * @note Plan distance only. Y is not considered, so a door directly overhead
 *       reads as touching.
 *
 * 한국어
 * ------
 * @brief 점이 문의 *닫힌* 외곽선에 가장 가까워지는 거리(미터)입니다.
 * @param[in] st ::door_reset이 포착한 스냅숏을 지닌 이동 상태.
 * @param[in] x  점의 월드 X (미터).
 * @param[in] z  점의 월드 Z (미터).
 * @return 미터 단위 거리. 쓸 만한 외곽선이 없는 문이면 1e9f입니다.
 *
 * @note 현재 형상이 아니라 *닫힌* 형상을 기준으로 재므로, 열리기 시작한 문이 그것을 연
 *       플레이어에게서 멀어져 중간에 멈추지 않습니다.
 * @note 평면 거리만 잽니다. Y는 고려하지 않으므로 바로 머리 위의 문도 닿은 것으로 읽힙니다.
 */
static float dist_to_outline(const DoorState *st, float x, float z) {
    /* A brush door's footprint is its closed box in plan, so the nearest point
       on it is the point clamped into that rectangle. The polygon walk below
       is the same measurement for an outline that is not a rectangle.
       Tested BEFORE the `n0 < 2` guard below, which counts OUTLINE POINTS: a
       brush door has none and carries n0 as a liveness flag, so reaching that
       guard sent every one of them home with 1e9 and no door ever opened.
       브러시 문의 발자국은 평면상의 닫힌 박스이므로, 그 위의 가장 가까운 점은 그 사각형
       안으로 제한한 점입니다. 아래의 다각형 순회는 사각형이 아닌 외곽선에 대한 같은
       측정입니다. 아래의 `n0 < 2` 검사보다 *먼저* 판정합니다. 그 검사가 세는 것은 *외곽선
       점*인데 브러시 문에는 그것이 없고 n0를 생존 플래그로 쓰므로, 그곳에 도달하면 모든
       브러시 문이 1e9를 받고 돌아갔고 어떤 문도 열리지 않았습니다. */
    if (st->sector < 0 && st->n0 > 0) {
        float dx = x < st->lo0.x ? st->lo0.x - x : (x > st->hi0.x ? x - st->hi0.x : 0.0f);
        float dz = z < st->lo0.z ? st->lo0.z - z : (z > st->hi0.z ? z - st->hi0.z : 0.0f);
        return sqrtf(dx*dx + dz*dz);
    }

    if (st->n0 < 2) return 1e9f;
    float best = 1e9f;

    /* Every edge of the closed outline, `j` trailing `i` by one so the pair
       wraps from the last point back to the first.
       닫힌 외곽선의 모든 변을 훑습니다. `j`가 `i`보다 하나 뒤에 있어, 마지막 점에서 첫
       점으로 짝이 순환합니다. */
    for (int i = 0, j = st->n0 - 1; i < st->n0; j = i++) {
        float ax = st->pts0[j*2] * 0.01f, az = st->pts0[j*2+1] * 0.01f;
        float bx = st->pts0[i*2] * 0.01f, bz = st->pts0[i*2+1] * 0.01f;
        float ex = bx - ax, ez = bz - az;
        float len2 = ex*ex + ez*ez;
        /* The projection of the point onto the edge, clamped to the segment so
           a point beyond either end measures to that endpoint. A degenerate
           edge falls back to t=0, which measures to `a`.
           점을 변에 투영한 값이며, 선분 안으로 제한하여 양 끝을 넘어선 점은 그 끝점까지의
           거리를 재게 합니다. 축퇴된 변은 t=0으로 되돌아가 `a`까지를 잽니다. */
        float t = len2 > 1e-6f ? ((x-ax)*ex + (z-az)*ez) / len2 : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float qx = ax + ex*t - x, qz = az + ez*t - z;
        /* Compared squared and rooted once at the end, so the loop pays no
           square root per edge.
           제곱 상태로 비교하고 마지막에 한 번만 제곱근을 취합니다. 그래야 루프가 변마다
           제곱근을 치르지 않습니다. */
        float d = qx*qx + qz*qz;
        if (d < best) best = d;
    }
    return sqrtf(best);
}

/**
 * @brief Whether a point is inside the door's CLOSED footprint.
 *
 * ENGLISH
 * -------
 * @param[in] st Motion state holding the snapshot ::door_reset captured.
 * @param[in] x  World X of the point, in metres.
 * @param[in] z  World Z of the point, in metres.
 * @return Non-zero when the point is inside the footprint.
 *
 * @note A door will not close on somebody who is standing in it -- see door.h.
 *       This is the test that decides that.
 * @note A brush door tests against its box; a sector door uses a crossing
 *       count, which needs three points to bound anything and returns zero
 *       below that.
 *
 * 한국어
 * ------
 * @brief 점이 문의 *닫힌* 발자국 안에 있는지 여부입니다.
 * @param[in] st ::door_reset이 포착한 스냅숏을 지닌 이동 상태.
 * @param[in] x  점의 월드 X (미터).
 * @param[in] z  점의 월드 Z (미터).
 * @return 점이 발자국 안에 있으면 0이 아닙니다.
 *
 * @note 문은 그 안에 서 있는 대상 위로 닫히지 않습니다. door.h를 참조하십시오. 그것을
 *       결정하는 판정이 이 함수입니다.
 * @note 브러시 문은 자신의 박스로 판정하고, 섹터 문은 교차 횟수를 씁니다. 교차 횟수는 무언가를
 *       둘러싸려면 점이 셋은 있어야 하므로 그보다 적으면 0을 반환합니다.
 */
static int inside_outline(const DoorState *st, float x, float z) {
    if (st->sector < 0)
        return st->n0 > 0 &&
               x >= st->lo0.x && x <= st->hi0.x &&
               z >= st->lo0.z && z <= st->hi0.z;
    if (st->n0 < 3) return 0;
    /* Ray crossing: count the edges that straddle this Z and lie to the right,
       and an odd total means inside. The `(zi > z) == (zj > z)` test skips
       edges entirely above or below, which is also what keeps the division
       below from dividing by zero.
       광선 교차입니다. 이 Z를 가로지르면서 오른쪽에 놓인 변의 수를 세고, 총합이 홀수면
       안쪽입니다. `(zi > z) == (zj > z)` 판정은 완전히 위나 아래에 있는 변을 건너뛰며, 그것이
       아래의 나눗셈이 0으로 나누지 않게 하는 것이기도 합니다. */
    int in = 0;
    for (int i = 0, j = st->n0 - 1; i < st->n0; j = i++) {
        float xi = st->pts0[i*2] * 0.01f, zi = st->pts0[i*2+1] * 0.01f;
        float xj = st->pts0[j*2] * 0.01f, zj = st->pts0[j*2+1] * 0.01f;
        if ((zi > z) == (zj > z)) continue;
        if (x < (xj - xi) * (z - zi) / (zj - zi) + xi) in = !in;
    }
    return in;
}

/* --- static helpers: writing travel back into the level / 정적 보조 함수: 이동량을 레벨에 되쓰기 --- */

/**
 * @brief The brush half of ::apply: the leaf is somewhere else, so it moves there.
 *
 * ENGLISH
 * -------
 * @param[in,out] l  Level holding the brush store this door's leaf lives in.
 * @param[in]     d  Definition naming the leaf, its axis and its travel.
 * @param[in,out] st Motion state; ::DoorState::applied is read and rewritten.
 *
 * RELATIVE, because ::brush_translate is. The planes carry no memory of where
 * they started, so the state carries how far it has already been moved and this
 * asks for the difference. Translating by the absolute amount every frame would
 * walk the door out of the level at `speed` per frame.
 *
 * All four DOOR_* directions are plain translations here, which is not quite
 * what they mean for a sector: DOOR_UP raises a sector's CEILING and leaves its
 * floor, because a sector is a floor plan and cannot move as a body. A brush
 * can, and a Quake `func_door` does -- the whole leaf slides. That is the
 * behaviour a mapper placing a func_door expects, and it is the one the
 * geometry can express.
 *
 * @warning ::DoorState::applied is the ONLY record of where the leaf has got
 *          to. Anything that moves these brushes without going through here
 *          leaves it lying, and the next call translates by the difference
 *          against a position that is no longer true.
 *
 * 한국어
 * ------
 * @brief ::apply의 브러시 쪽 절반입니다. 문짝은 다른 곳에 있으므로 그곳으로 옮깁니다.
 * @param[in,out] l  이 문의 문짝이 사는 브러시 저장소를 지닌 레벨.
 * @param[in]     d  문짝과 그 축, 이동량을 지목하는 정의.
 * @param[in,out] st 이동 상태. ::DoorState::applied를 읽고 다시 씁니다.
 *
 * ::brush_translate가 상대적이므로 이것도 상대적입니다. 평면은 자기가 어디서 시작했는지
 * 기억하지 않으므로, 상태가 이미 옮겨진 거리를 지니고 이 함수는 그 차이를 요청합니다. 매
 * 프레임 절대량만큼 옮기면 문이 프레임당 `speed`씩 레벨 밖으로 걸어 나갑니다.
 *
 * 이곳에서 네 DOOR_* 방향은 모두 단순한 평행이동이며, 섹터에서의 의미와는 조금 다릅니다.
 * DOOR_UP은 섹터의 *천장*을 올리고 바닥은 그대로 둡니다. 섹터는 평면도이고 하나의 몸으로
 * 움직일 수 없기 때문입니다. 브러시는 할 수 있고 Quake의 `func_door`가 그렇게 합니다. 문짝
 * 전체가 미끄러집니다. func_door를 놓는 제작자가 기대하는 동작이며, 지오메트리가 표현할 수
 * 있는 동작입니다.
 *
 * @warning ::DoorState::applied는 문짝이 어디까지 갔는지에 대한 *유일한* 기록입니다. 이곳을
 *          거치지 않고 이 브러시들을 옮기는 것은 그 값을 거짓으로 만들며, 다음 호출은 더 이상
 *          참이 아닌 위치를 기준으로 차이만큼 이동시킵니다.
 */
static void apply_brush(Level *l, const DoorDef *d, DoorState *st) {
    if (!l->brushes) return;
    if (d->axis < 0 || d->axis >= DOOR_AXES) return;

    float want  = d->amount * 0.01f * st->t;    /* file units -> metres */
    float delta = want - st->applied;
    /* Nothing to do rather than a zero translation: ::brush_translate would
       still walk every plane of the leaf to add zero to it.
       0만큼 평행이동하는 대신 아무것도 하지 않습니다. ::brush_translate는 0을 더하려고
       문짝의 모든 평면을 여전히 순회할 것이기 때문입니다. */
    if (delta == 0.0f) return;

    brush_translate(l->brushes, d->first_brush, d->n_brushes,
                    v3scale(DOOR_DIR[d->axis], delta));
    st->applied = want;
}

/**
 * @brief Writes a door's current travel back into the level. This is the whole mechanism.
 *
 * ENGLISH
 * -------
 * @param[in,out] l  Level holding the sector or the brush leaf this door drives.
 * @param[in]     d  Definition naming what moves, along which axis, and how far.
 * @param[in]     st Motion state; ::DoorState::t is how far open the door is now.
 *
 * Everything that collides reads the fields this writes, which is why there is
 * no door object for anything to know about. A negative ::DoorDef::sector means
 * the door is a brush leaf and ::apply_brush takes it; otherwise the sector
 * itself is rewritten in place.
 *
 * @note A sector cannot move as a body, so the axes mean different things
 *       here than in ::apply_brush: ::DOOR_UP raises the ceiling, ::DOOR_DOWN
 *       lowers the floor, and the sliding axes move the outline points.
 * @warning Rewrites ::Sector fields directly. The lookup grid the level keeps
 *          describes where sectors WERE, so ::door_update rebuilds it after
 *          any frame in which this moved something.
 *
 * 한국어
 * ------
 * @brief 문의 현재 이동량을 레벨에 되씁니다. 이것이 기구의 전부입니다.
 * @param[in,out] l  이 문이 구동하는 섹터 또는 브러시 문짝을 지닌 레벨.
 * @param[in]     d  무엇이 어느 축을 따라 얼마나 움직이는지를 지목하는 정의.
 * @param[in]     st 이동 상태. ::DoorState::t가 지금 문이 얼마나 열렸는지입니다.
 *
 * 충돌하는 모든 것이 이 함수가 기록하는 필드를 읽으며, 그래서 무언가가 알아야 할 문 객체가
 * 존재하지 않습니다. ::DoorDef::sector가 음수이면 그 문은 브러시 문짝이며 ::apply_brush가
 * 맡습니다. 그렇지 않으면 섹터 자체를 제자리에서 다시 씁니다.
 *
 * @note 섹터는 하나의 몸으로 움직일 수 없으므로, 이곳에서 축의 의미는 ::apply_brush에서와
 *       다릅니다. ::DOOR_UP은 천장을 올리고, ::DOOR_DOWN은 바닥을 내리며, 미끄러지는 축은
 *       외곽선 점들을 옮깁니다.
 * @warning ::Sector 필드를 직접 다시 씁니다. 레벨이 유지하는 조회 격자는 섹터가 *있던* 자리를
 *          서술하므로, 이 함수가 무언가를 움직인 프레임 뒤에 ::door_update가 격자를 다시
 *          만듭니다.
 */
static void apply(Level *l, const DoorDef *d, DoorState *st) {
    if (d->sector < 0) { apply_brush(l, d, st); return; }
    if (d->sector >= l->n_sectors) return;
    Sector *s = &l->sectors[d->sector];

    /* Absolute, not incremental: every branch below adds this to the snapshot
       ::door_reset took, so a frame that recomputes it from the same `t`
       produces the same sector and nothing accumulates drift.
       증분이 아니라 절대량입니다. 아래의 모든 분기는 이 값을 ::door_reset이 찍은 스냅숏에
       더하므로, 같은 `t`로 다시 계산하는 프레임은 같은 섹터를 만들어 내고 오차가 누적되지
       않습니다. */
    float moved = d->amount * st->t;

    switch (d->axis) {
    case DOOR_UP:
        s->ceil = (short)(st->ceil0 + moved);
        /* What the wall builder needs and the height above cannot say: not
           where the surface IS, but how far it came. See ::Sector::uv_y.
           벽 생성기가 필요로 하는 것이자 위의 높이가 말할 수 없는 것입니다. 면이 어디에
           *있는지*가 아니라 얼마나 왔는지입니다. ::Sector::uv_y를 참조하십시오. */
        s->uv_y = (short)moved;
        break;
    case DOOR_DOWN:
        s->floor = (short)(st->floor0 - moved);
        s->uv_y  = (short)(-moved);
        break;
    case DOOR_X:
    case DOOR_Z: {
        /* The two sliding axes differ only in which of the interleaved shorts
           they touch, so one branch handles both with an offset.
           미끄러지는 두 축은 번갈아 놓인 short 중 어느 쪽을 건드리느냐만 다르므로, 하나의
           분기가 오프셋으로 둘 다 처리합니다. */
        int off = (d->axis == DOOR_X) ? 0 : 1;
        for (int k = 0; k < st->n0; k++)
            s->pts[k*2 + off] = (short)(st->pts0[k*2 + off] + moved);
        /* The bounding box is what point_in_sector rejects against, so a slid
           outline whose box stayed put would be solid where it no longer is
           and passable where it now stands.
           바운딩 박스는 point_in_sector가 기각에 쓰는 값이므로, 박스가 제자리에 남은
           외곽선은 더 이상 있지 않은 곳에서 막고 지금 서 있는 곳에서 통과시킵니다. */
        level_bounds(s);
        break;
    }
    default: break;
    }
}

/* --- static helpers: one door's frame / 정적 보조 함수: 문 하나의 프레임 --- */

/**
 * @brief Advances one door: its trigger, its travel, and the sector it moves.
 *
 * ENGLISH
 * -------
 * @param[in,out] l          Level whose sector this door drives.
 * @param[in]     i          Door index, into both `l->doors` and `l->door_run.d`.
 * @param[in]     player_pos Where the player is, for the proximity trigger.
 * @param[in]     keys       Keycards held, for a locked door.
 * @param[in]     tagged     Non-zero when a switch fired this door's tag.
 * @param[in]     dt         Frame time.
 * @return Non-zero when the sector actually moved this frame.
 *
 * @note THE SECTOR IS WHAT MOVES, not a door object: everything that collides
 *       sees the change without knowing what a door is. The caller rebuilds
 *       the lookup grid once for the frame rather than once per door.
 * @note Split out of ::door_update, which was a hundred-line loop body wrapped
 *       in fourteen lines of bookkeeping. Whether a door opens is a question
 *       about ONE door; how many doors there are, and which switches were
 *       pressed, are questions about the level.
 *
 * 한국어
 * ------
 * @brief 문 하나를 진행시킵니다. 작동 조건, 이동, 그리고 그것이 움직이는 섹터입니다.
 * @param[in,out] l          이 문이 구동하는 섹터를 지닌 레벨.
 * @param[in]     i          문 인덱스. `l->doors`와 `l->door_run.d` 양쪽에 대한 것입니다.
 * @param[in]     player_pos 근접 작동을 위한 플레이어 위치.
 * @param[in]     keys       잠긴 문을 위해 보유한 키카드.
 * @param[in]     tagged     스위치가 이 문의 태그를 작동시켰으면 0이 아닙니다.
 * @param[in]     dt         프레임 시간.
 * @return 이번 프레임에 섹터가 실제로 움직였으면 0이 아닙니다.
 *
 * @note *움직이는 것은 섹터*이지 문 객체가 아닙니다. 충돌하는 모든 것이 문의 정체를 모른 채
 *       그 변화를 봅니다. 조회 격자는 호출자가 문마다가 아니라 프레임당 한 번 다시
 *       만듭니다.
 * @note ::door_update에서 분리했습니다. 그 함수는 열네 줄의 기록 처리로 감싼 백 줄짜리 루프
 *       본문이었습니다. 문이 열리는지는 *문 하나*에 대한 질문이고, 문이 몇 개인지와 어떤
 *       스위치가 눌렸는지는 레벨에 대한 질문입니다.
 */
static int door_step_one(Level *l, int i, v3 player_pos, int keys,
                         int tagged, float dt) {
    int moved = 0;
    const DoorDef *d = &l->doors[i];
    DoorState *st = &l->door_run.d[i];
    if (st->n0 <= 0) return 0;          /* was `continue`: skip this door */

    /* --- is this state still about this door? --------------------------
       The two arrays are matched by index and by nothing else. door_reset
       is what agrees them, and a level stepped without one -- a second Level
       in play, an editor that rewrote the doors, a load path that forgot --
       lands here holding somebody else's closed shape. apply() would write
       it into d->sector: a wall in the wrong place, moving, and looking for
       all the world like a door that works.

       Skipped rather than guessed at, and counted so it is not silent. A
       stale door that does nothing is a bug you can find; a stale door that
       moves the wrong geometry is one you chase through the renderer.

       두 배열은 인덱스로만 대응하며 다른 무엇으로도 대응하지 않습니다. 둘을 일치시키는
       것은 door_reset이고, 그것 없이 진행된 레벨은(진행 중인 두 번째 Level, 문을 다시
       쓴 에디터, 잊어버린 로드 경로) 남의 닫힌 형상을 든 채로 이곳에 도달합니다.
       apply()는 그것을 d->sector에 씁니다. 엉뚱한 자리에서 움직이는 벽이며, 어느 모로
       보나 정상 동작하는 문처럼 보입니다.

       추측하지 않고 건너뛰며, 조용하지 않도록 셉니다. 아무것도 하지 않는 낡은 문은
       찾을 수 있는 버그이고, 엉뚱한 지오메트리를 움직이는 낡은 문은 렌더러를 헤매며
       쫓아야 하는 버그입니다. */
    if (st->sector != d->sector) { DIAG(DIAG_DOOR_STALE); return 0; }

    /* --- what wants this door open right now --------------------------
       An untagged door opens to a touch on itself; a tagged one opens only
       to its switch. Both are "somebody asked", and the difference is only
       where they had to stand to ask.
       태그가 없는 문은 자신을 건드리면 열리고, 태그가 있는 문은 자신의 스위치에만
       반응합니다. 둘 다 "누군가 요청했다"이며, 차이는 요청하려면 어디에 서야 하는가
       뿐입니다. */
    int asked;
    if (d->tag > 0) asked = tagged;
    else            asked = dist_to_outline(st, player_pos.x, player_pos.z)
                            <= DOOR_TOUCH_DIST;

    if (asked && d->key != KEY_NONE && !(keys & d->key)) {
        /* Refused. Reported once per frame so the HUD can say which key,
           and the door does not budge.
           거절되었습니다. HUD가 어떤 열쇠인지 말할 수 있도록 프레임당 한 번
           보고하며, 문은 움직이지 않습니다. */
        l->door_run.refused = d->key;

        /* Re-armed to the full time on every touch rather than only when
           it has run out, so leaning on a locked door keeps the message up
           instead of letting it blink.
           이미 떠 있든 아니든 닿을 때마다 시간을 가득 채웁니다. 잠긴 문에 계속 붙어
           있으면 메시지가 깜빡이지 않고 유지됩니다. */
        l->door_run.notice_key = d->key;
        l->door_run.notice_t   = DOOR_NOTICE_TIME;
        /* Cleared, so the request does not survive the refusal and open the
           door further down.
           요청이 거절을 넘어 살아남아 아래에서 문을 열지 않도록 지웁니다. */
        asked = 0;
    }

    if (asked && !st->opening && st->t < 1.0f) {
        st->opening = 1;
        /* Its own sound now. This was the shotgun's rack, because that was
           the nearest thing the synthesised library had and a door has to
           make SOME noise -- a stand-in that stopped being one the moment
           there was a door sound to play.
           이제 자기 사운드를 갖습니다. 이전에는 샷건의 장전음이었는데, 합성
           라이브러리에 있던 것 중 가장 가까웠고 문은 *어떤* 소리든 내야 했기
           때문입니다. 문 사운드가 생긴 순간 그것은 대역이기를 멈췄습니다. */
        /* A tagged door was opened by a switch, so the clack comes
           with it. Played on the door's opening EDGE rather than at the
           switch, because the switch handler runs every frame the player
           stands on it and would machine-gun the sound; this fires once
           per activation and already knows which case it is.
           태그가 있는 문은 스위치가 연 것이므로 그 소리가 함께 납니다. 스위치가
           아니라 문이 *열리기 시작하는 경계*에서 재생하는 이유는, 스위치 처리기가
           플레이어가 밟고 있는 매 프레임 실행되어 소리를 연발하기 때문입니다. */
        if (d->tag > 0) audio_play_at("switch", 80, door_centre(st));
        audio_play_at("door", 75, door_centre(st));
    }
    if (asked) st->wait = DOOR_OPEN_TIME;

    /* Travel expressed as a FRACTION of the door's span per frame, because
       ::DoorState::t runs 0..1 whatever the door's size. A door with no span
       jumps straight to fully open rather than dividing by zero.
       프레임당 이동을 문의 전체 행정에 대한 *비율*로 표현합니다. ::DoorState::t는 문의
       크기와 무관하게 0..1을 오가기 때문입니다. 행정이 없는 문은 0으로 나누는 대신 곧바로
       완전히 열린 상태가 됩니다. */
    float step = (d->speed * 0.01f) * dt;
    float span = fabsf(d->amount * 0.01f);
    float rate = span > 1e-4f ? step / span : 1.0f;

    if (st->opening) {
        if (st->t < 1.0f) {
            st->t += rate;
            /* The hold only starts once the door is fully open, so a door
               still moving never begins counting down.
               문이 완전히 열린 뒤에야 유지 시간이 시작되므로, 아직 움직이는 문은 결코
               카운트다운을 시작하지 않습니다. */
            if (st->t >= 1.0f) { st->t = 1.0f; st->wait = DOOR_OPEN_TIME; }
            moved = 1;
        } else {
            st->wait -= dt;
            /* Held open by anything standing in the doorway. A door that
               closed on the player would be a death with no lesson in it.
               문간에 서 있는 것이 문을 열린 채로 붙잡습니다. 플레이어 위에서 닫히는
               문은 아무 교훈도 없는 죽음입니다. */
            if (inside_outline(st, player_pos.x, player_pos.z))
                st->wait = DOOR_OPEN_TIME;
            if (st->wait <= 0.0f) st->opening = 0;
        }
    } else if (st->t > 0.0f) {
        /* Closing, and checked again on the way: somebody who steps into a
           door that has already started to shut sends it back open.
           닫히는 중이며 가는 도중에 다시 검사합니다. 이미 닫히기 시작한 문에 들어선
           사람은 문을 도로 열리게 합니다. */
        if (inside_outline(st, player_pos.x, player_pos.z)) {
            st->wait = DOOR_OPEN_TIME;
            st->opening = 1;
        } else {
            st->t -= rate;
            if (st->t <= 0.0f) st->t = 0.0f;
            moved = 1;
        }
    }

    /* Unconditionally, even on a frame where nothing moved: `applied` and the
       sector must agree with `t` whatever happened above.
       위에서 무슨 일이 있었든 `applied`와 섹터는 `t`와 일치해야 하므로, 아무것도 움직이지
       않은 프레임에도 무조건 호출합니다. */
    apply(l, d, st);
    return moved;
}
