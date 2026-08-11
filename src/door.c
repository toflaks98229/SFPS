/**
 * @file door.c
 * @brief The door state machine. Writes moved sectors back into the level. No GL.
 */

#include "door.h"
#include "audio.h"
#include <math.h>

/**
 * @struct DoorState
 * @brief Where one door is in its travel, and what it started as.
 *
 * ENGLISH
 * -------
 * The closed shape is copied here at ::door_reset because ::door_update
 * overwrites the sector: after one frame of motion the level no longer holds
 * the door's starting position, so anything that derived "closed" from the
 * level would drift a little further open every frame.
 *
 * 한국어
 * ------
 * 닫힌 형상을 ::door_reset에서 이곳으로 복사합니다. ::door_update가 섹터를 덮어쓰므로,
 * 한 프레임만 움직여도 레벨은 문의 출발 위치를 더 이상 담고 있지 않습니다. "닫힘"을
 * 레벨에서 유도하는 것은 무엇이든 매 프레임 조금씩 더 열리게 됩니다.
 */
typedef struct {
    float t;                        /**< 0 closed .. 1 open. / 0이면 닫힘, 1이면 열림. */
    float wait;                     /**< Seconds left of the open pause. / 열린 채 대기하는 남은 시간. */
    int   opening;                  /**< Non-zero while travelling open. / 열리는 중이면 0이 아닙니다. */
    short floor0, ceil0;            /**< Closed heights. / 닫힌 상태의 높이. */
    short pts0[LVL_MAX_PTS * 2];    /**< Closed outline. / 닫힌 상태의 외곽선. */
    int   n0;                       /**< Points in `pts0`. / `pts0`의 점 개수. */
} DoorState;

static DoorState g_door[LVL_MAX_DOORS];
static int       g_n;
static int       g_refused;

void door_reset(const Level *l) {
    g_n = l->n_doors > LVL_MAX_DOORS ? LVL_MAX_DOORS : l->n_doors;
    g_refused = KEY_NONE;

    for (int i = 0; i < g_n; i++) {
        DoorState *st = &g_door[i];
        st->t = 0.0f;
        st->wait = 0.0f;
        st->opening = 0;

        int si = l->doors[i].sector;
        if (si < 0 || si >= l->n_sectors) { st->n0 = 0; continue; }

        const Sector *s = &l->sectors[si];
        st->floor0 = s->floor;
        st->ceil0  = s->ceil;
        st->n0     = s->n;
        for (int k = 0; k < s->n * 2; k++) st->pts0[k] = s->pts[k];
    }
}

float door_openness(int i) {
    return (i >= 0 && i < g_n) ? g_door[i].t : 0.0f;
}

int door_refused(void) { return g_refused; }

/* The closest a point gets to the door's closed outline, in metres. Measured
   against the CLOSED shape rather than the current one, so a door that has
   started opening does not walk away from the player who opened it and stall
   halfway.
   점이 문의 *닫힌* 외곽선에 가장 가까워지는 거리(미터)입니다. 현재 형상이 아니라 닫힌
   형상을 기준으로 재므로, 열리기 시작한 문이 그것을 연 플레이어에게서 멀어져 중간에
   멈추지 않습니다. */
static float dist_to_outline(const DoorState *st, float x, float z) {
    if (st->n0 < 2) return 1e9f;
    float best = 1e9f;

    for (int i = 0, j = st->n0 - 1; i < st->n0; j = i++) {
        float ax = st->pts0[j*2] * 0.01f, az = st->pts0[j*2+1] * 0.01f;
        float bx = st->pts0[i*2] * 0.01f, bz = st->pts0[i*2+1] * 0.01f;
        float ex = bx - ax, ez = bz - az;
        float len2 = ex*ex + ez*ez;
        float t = len2 > 1e-6f ? ((x-ax)*ex + (z-az)*ez) / len2 : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float qx = ax + ex*t - x, qz = az + ez*t - z;
        float d = qx*qx + qz*qz;
        if (d < best) best = d;
    }
    return sqrtf(best);
}

/* Whether the player is standing inside the door's closed footprint. A door
   will not close on somebody who is in it -- see door.h.
   플레이어가 문의 닫힌 발자국 안에 서 있는지 여부입니다. */
static int inside_outline(const DoorState *st, float x, float z) {
    if (st->n0 < 3) return 0;
    int in = 0;
    for (int i = 0, j = st->n0 - 1; i < st->n0; j = i++) {
        float xi = st->pts0[i*2] * 0.01f, zi = st->pts0[i*2+1] * 0.01f;
        float xj = st->pts0[j*2] * 0.01f, zj = st->pts0[j*2+1] * 0.01f;
        if ((zi > z) == (zj > z)) continue;
        if (x < (xj - xi) * (z - zi) / (zj - zi) + xi) in = !in;
    }
    return in;
}

/* Writes a door's current travel back into its sector. This is the whole
   mechanism: everything that collides reads these fields.
   문의 현재 이동량을 섹터에 되씁니다. 이것이 기구의 전부입니다. 충돌하는 모든 것이 이
   필드들을 읽습니다. */
static void apply(Level *l, const DoorDef *d, const DoorState *st) {
    if (d->sector < 0 || d->sector >= l->n_sectors) return;
    Sector *s = &l->sectors[d->sector];

    float moved = d->amount * st->t;

    switch (d->axis) {
    case DOOR_UP:
        s->ceil = (short)(st->ceil0 + moved);
        break;
    case DOOR_DOWN:
        s->floor = (short)(st->floor0 - moved);
        break;
    case DOOR_X:
    case DOOR_Z: {
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

int door_update(Level *l, v3 player_pos, int keys, float dt) {
    int moved = 0;
    g_refused = KEY_NONE;

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

        float dx = player_pos.x - en->x * 0.01f;
        float dz = player_pos.z - en->z * 0.01f;
        if (dx*dx + dz*dz > DOOR_SWITCH_DIST * DOOR_SWITCH_DIST) continue;

        for (int i = 0; i < g_n; i++)
            if (l->doors[i].tag == tag) touched_tag[i] = 1;
    }

    for (int i = 0; i < g_n; i++) {
        const DoorDef *d = &l->doors[i];
        DoorState *st = &g_door[i];
        if (st->n0 <= 0) continue;

        /* --- what wants this door open right now --------------------------
           An untagged door opens to a touch on itself; a tagged one opens only
           to its switch. Both are "somebody asked", and the difference is only
           where they had to stand to ask.
           태그가 없는 문은 자신을 건드리면 열리고, 태그가 있는 문은 자신의 스위치에만
           반응합니다. 둘 다 "누군가 요청했다"이며, 차이는 요청하려면 어디에 서야 하는가
           뿐입니다. */
        int asked;
        if (d->tag > 0) asked = touched_tag[i];
        else            asked = dist_to_outline(st, player_pos.x, player_pos.z)
                                <= DOOR_TOUCH_DIST;

        if (asked && d->key != KEY_NONE && !(keys & d->key)) {
            /* Refused. Reported once per frame so the HUD can say which key,
               and the door does not budge.
               거절되었습니다. HUD가 어떤 열쇠인지 말할 수 있도록 프레임당 한 번
               보고하며, 문은 움직이지 않습니다. */
            g_refused = d->key;
            asked = 0;
        }

        if (asked && !st->opening && st->t < 1.0f) {
            st->opening = 1;
            audio_play("pump", 70);
        }
        if (asked) st->wait = DOOR_OPEN_TIME;

        float step = (d->speed * 0.01f) * dt;
        float span = fabsf(d->amount * 0.01f);
        float rate = span > 1e-4f ? step / span : 1.0f;

        if (st->opening) {
            if (st->t < 1.0f) {
                st->t += rate;
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
            if (inside_outline(st, player_pos.x, player_pos.z)) {
                st->wait = DOOR_OPEN_TIME;
                st->opening = 1;
            } else {
                st->t -= rate;
                if (st->t <= 0.0f) st->t = 0.0f;
                moved = 1;
            }
        }

        apply(l, d, st);
    }

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
