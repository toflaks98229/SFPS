/**
 * @file door.c
 * @brief The door state machine. Writes moved sectors back into the level. No GL.
 */

#include "door.h"
#include "audio.h"
#include "brush.h"  /* brush_translate -- what a door moves when it is brushes */
#include "diag.h"   /* DIAG_DOOR_STALE -- state that outlived the level it described */
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

    /**
     * @brief How far a BRUSH door has already been translated, in metres.
     *
     * ENGLISH
     * -------
     * The sector door needs nothing like this: `pts0` and `floor0` are the
     * closed shape, and ::apply writes the target absolutely from them every
     * frame, so where it currently is never has to be remembered.
     *
     * Brush planes carry no such snapshot -- ::brush_translate is relative and
     * there are up to ::BR_MAX_FACES of them per brush -- so what is stored is
     * the one number that makes the move reversible: how much has been applied.
     * The next frame asks for the difference.
     *
     * @warning Zero means the brushes are where the .map drew them, which is
     *          true immediately after ::level_load and is why ::door_reset must
     *          follow one. A reset against a level whose doors are part way
     *          open would call that position closed and the leaf would never
     *          return to the doorway.
     *
     * 한국어
     * ------
     * @brief *브러시* 문이 이미 평행이동된 거리이며 미터 단위입니다.
     *
     * 섹터 문에는 이런 것이 필요 없습니다. `pts0`와 `floor0`가 닫힌 형상이고 ::apply가 매
     * 프레임 그것으로부터 목표를 절대적으로 기록하므로, 지금 어디에 있는지는 기억할 필요가
     * 없습니다.
     *
     * 브러시 평면에는 그런 스냅숏이 없습니다. ::brush_translate는 상대적이고 브러시마다
     * 최대 ::BR_MAX_FACES개가 있습니다. 그래서 저장하는 것은 이동을 되돌릴 수 있게 만드는 단
     * 하나의 숫자, 즉 얼마나 적용했는가입니다. 다음 프레임은 그 차이를 요청합니다.
     *
     * @warning 0은 브러시가 .map이 그린 자리에 있다는 뜻이며, ::level_load 직후에 참입니다.
     *          ::door_reset이 그 뒤를 따라야 하는 이유입니다. 문이 반쯤 열린 레벨에 대해
     *          리셋하면 그 위치를 닫힘이라 부르게 되고 문짝은 결코 출입구로 돌아오지
     *          않습니다.
     */
    float applied;

    /**
     * @brief A brush door's CLOSED extent, in world units.
     *
     * The counterpart of `pts0` for the other model, and it exists for the same
     * reason: the touch test and the sound both measure against where the door
     * is when shut, so that a door already sliding does not walk away from the
     * player who opened it and stall halfway across.
     *
     * 브러시 문의 *닫힌* 크기이며 월드 단위입니다.
     * @note 다른 모델에서의 `pts0`에 해당하며 이유도 같습니다. 접촉 판정과 소리는 모두 문이
     *       닫혀 있을 때의 자리를 기준으로 재며, 그래야 이미 미끄러지고 있는 문이 그것을 연
     *       플레이어에게서 멀어져 중간에 멈추지 않습니다.
     */
    v3 lo0, hi0;
    /**
     * @brief Which sector the shape above was copied from, or -1.
     *
     * ENGLISH
     * -------
     * PROVENANCE, NOT A SECOND COPY OF THE TRUTH. The note on ::g_keys_used
     * below argues against copying a door's `key` into this struct, and the
     * argument is right: nothing writes `key`, so a copy of it would be a
     * second source kept in step by nothing. This field is the opposite case
     * and has to be read as such. It is not the sector the door acts on --
     * ::apply still asks the definition for that, and always will. It is a
     * record of where the snapshot beside it came from, which is a fact about
     * `pts0` and belongs with `pts0`.
     *
     * A snapshot that does not know what it is a snapshot OF is the actual
     * defect here. This array and the level's `doors[]` are matched by index
     * and by nothing else; ::door_reset is what agrees them, and until this
     * field existed, a ::door_update run against a level that reset never saw
     * would write one sector's geometry out of another's closed shape and look
     * exactly like a door working. Now it is a ::DIAG_DOOR_STALE and a door
     * that does not move.
     *
     * 한국어
     * ------
     * @brief 위의 형상을 복사해 온 섹터, 또는 -1.
     *
     * *출처*이지 진실의 두 번째 사본이 아닙니다. 아래 ::g_keys_used의 주석은 문의 `key`를 이
     * 구조체로 복사하는 것에 반대하며, 그 주장은 옳습니다. `key`는 아무도 쓰지 않으므로 그
     * 사본은 무엇으로도 일치가 유지되지 않는 두 번째 진실이 됩니다. 이 필드는 정반대의
     * 경우이며 그렇게 읽어야 합니다. 이것은 문이 작용하는 섹터가 아닙니다. ::apply는 여전히
     * 그것을 정의에게 묻고 앞으로도 그럴 것입니다. 이것은 옆에 있는 스냅숏이 어디에서 왔는지에
     * 대한 기록이며, `pts0`에 관한 사실이므로 `pts0` 옆에 있어야 합니다.
     *
     * 자신이 무엇의 스냅숏인지 모르는 스냅숏이 이곳의 실제 결함입니다. 이 배열과 레벨의
     * `doors[]`는 인덱스로만 대응하며 다른 무엇으로도 대응하지 않습니다. 둘을 일치시키는 것은
     * ::door_reset이고, 이 필드가 생기기 전에는 reset이 본 적 없는 레벨에 대해 실행된
     * ::door_update가 한 섹터의 지오메트리를 다른 섹터의 닫힌 형상으로부터 쓰면서도 정확히
     * 정상 동작하는 문처럼 보였습니다. 이제 그것은 ::DIAG_DOOR_STALE이며 움직이지 않는
     * 문입니다.
     */
    short sector;
} DoorState;

static DoorState g_door[LVL_MAX_DOORS];
static int       g_n;
static int       g_refused;

/* THE REFUSAL HAS TO OUTLIVE THE FRAME IT HAPPENED IN. `g_refused` is cleared
   at the top of every door_update, which is right for asking "was the player
   turned away just now" and useless for telling them so: a message that is
   true for one frame at 60Hz is a message nobody reads.
   Kept here rather than in the HUD because the door is what knows, and a timer
   on the drawing side would be a second copy of an event that already has an
   owner. door_update is also the only place with a dt to count down.
   거절은 그것이 일어난 프레임보다 오래 살아남아야 합니다. `g_refused`는 door_update의
   맨 위에서 초기화되며, "방금 거절당했는가"를 묻기에는 맞지만 그것을 *알리기에는*
   쓸모없습니다. 60Hz에서 한 프레임 동안만 참인 메시지는 아무도 읽지 못합니다. HUD가
   아니라 여기에 두는 이유는 아는 쪽이 문이기 때문이며, 그리는 쪽의 타이머는 이미 주인이
   있는 사건의 사본이 됩니다. door_update는 셀 dt를 가진 유일한 곳이기도 합니다. */
static int       g_notice_key;
static float     g_notice_t;

/* The union of every door's key, folded once at ::door_reset. Derived rather
   than stored per door, because the key is the LEVEL's and does not change:
   copying it into DoorState beside floor0 and pts0 would look like the same
   pattern and is not. Those are snapshots of fields door_update overwrites,
   and this one nothing ever writes -- a copy of it would be a second source of
   truth kept in step by nothing.
   모든 문의 열쇠를 합집합으로 ::door_reset에서 한 번 접어 둡니다. 문마다 저장하지 않고
   유도하는 이유는 열쇠가 *레벨*의 것이고 변하지 않기 때문입니다. floor0나 pts0 옆에
   DoorState로 복사하면 같은 패턴처럼 보이지만 아닙니다. 그것들은 door_update가 덮어쓰는
   필드의 스냅숏이고, 이것은 아무도 쓰지 않습니다. 사본을 두면 무엇으로도 일치가 유지되지
   않는 두 번째 진실이 됩니다. */
static int       g_keys_used;

void door_reset(const Level *l) {
    g_n = l->n_doors > LVL_MAX_DOORS ? LVL_MAX_DOORS : l->n_doors;
    g_refused = KEY_NONE;

    /* Cleared with everything else: a message about the last level's locked
       door has no business surviving into the next one.
       나머지와 함께 초기화합니다. 이전 레벨의 잠긴 문에 대한 메시지가 다음 레벨까지
       살아남을 이유는 없습니다. */
    g_notice_key = KEY_NONE;
    g_notice_t   = 0.0f;

    g_keys_used = KEY_NONE;
    for (int i = 0; i < g_n; i++) g_keys_used |= l->doors[i].key;

    for (int i = 0; i < g_n; i++) {
        DoorState *st = &g_door[i];
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
        for (int k = 0; k < s->n * 2; k++) st->pts0[k] = s->pts[k];
    }
}

float door_openness(int i) {
    return (i >= 0 && i < g_n) ? g_door[i].t : 0.0f;
}

int door_refused(void) { return g_refused; }

/* Indexed by BIT POSITION, so KEY_RED (1<<0) is [0]. Ordered to match the
   KEY_* enum, which is also the order pickup.h's PK_KEY0..PK_KEY_LAST run in,
   so a keycard's colour, its pickup sprite and this name are all the same
   index rather than three lists agreeing by habit.
   *비트 위치*로 색인하므로 KEY_RED(1<<0)는 [0]입니다. KEY_* enum의 순서와 같고, 그것은
   pickup.h의 PK_KEY0..PK_KEY_LAST 순서이기도 하므로, 키카드의 색·아이템 스프라이트·이
   이름이 습관으로 일치하는 세 목록이 아니라 같은 색인이 됩니다. */
static const char *const KEY_NAME[] = { "RED", "BLUE", "YELLOW" };

/* The table and the enum have to end together. Adding a key without a name
   here would print an empty string on a door nobody can open, which looks like
   a bug in the door rather than a missing line in a table.
   표와 enum은 함께 끝나야 합니다. 여기에 이름 없이 열쇠를 추가하면 아무도 열 수 없는 문에
   빈 문자열이 표시되며, 표에 빠진 한 줄이 아니라 문의 결함처럼 보입니다. */
_Static_assert(sizeof(KEY_NAME) / sizeof(KEY_NAME[0]) == KEY_KINDS,
               "KEY_NAME must name every KEY_* bit");

const char *door_key_name(int key) {
    for (int i = 0; i < KEY_KINDS; i++)
        if (key & (1 << i)) return KEY_NAME[i];
    return "";
}

int door_keys_used(void) {
    /* Asked of the DOORS rather than of the level's pickups: the question the
       HUD is answering is "which cards can this map demand", and a level that
       scatters a key no door wants would light a row the player never needs.
       아이템이 아니라 *문*에게 묻습니다. HUD가 답하는 질문은 "이 맵이 요구할 수 있는
       카드는 무엇인가"이며, 어떤 문도 원하지 않는 열쇠를 뿌린 레벨은 플레이어에게 결코
       필요 없는 행을 켜게 됩니다. */
    return g_keys_used;
}

int door_notice_key(void) { return g_notice_t > 0.0f ? g_notice_key : KEY_NONE; }

float door_notice_left(void) { return g_notice_t > 0.0f ? g_notice_t : 0.0f; }

/* The closest a point gets to the door's closed outline, in metres. Measured
   against the CLOSED shape rather than the current one, so a door that has
   started opening does not walk away from the player who opened it and stall
   halfway.
   점이 문의 *닫힌* 외곽선에 가장 가까워지는 거리(미터)입니다. 현재 형상이 아니라 닫힌
   형상을 기준으로 재므로, 열리기 시작한 문이 그것을 연 플레이어에게서 멀어져 중간에
   멈추지 않습니다. */
/* The middle of the door's CLOSED outline, in metres -- where the sound of it
   moving comes from. The closed shape rather than the current one for the same
   reason the touch test uses it: a sliding door that measured from where it
   has got to would walk away from the player who opened it.
   문의 *닫힌* 외곽선의 한가운데를 미터로 반환합니다. 그것이 문이 움직이는 소리가 나는
   자리입니다. 현재 모양이 아니라 닫힌 모양을 쓰는 이유는 접촉 판정과 같습니다. 도달한
   위치를 기준으로 재면, 미닫이문은 그것을 연 플레이어에게서 멀어져 갑니다. */
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
    if (st->sector < 0)
        return st->n0 > 0 &&
               x >= st->lo0.x && x <= st->hi0.x &&
               z >= st->lo0.z && z <= st->hi0.z;
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
/**
 * The brush half of ::apply: the leaf is somewhere else, so it moves there.
 *
 * ENGLISH
 * -------
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
 * 한국어
 * ------
 * ::apply의 브러시 쪽 절반입니다. 문짝은 다른 곳에 있으므로 그곳으로 옮깁니다.
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
 */
static void apply_brush(Level *l, const DoorDef *d, DoorState *st) {
    if (!l->brushes) return;

    static const v3 DIR[DOOR_AXES] = {
        { 0.0f,  1.0f, 0.0f },   /* DOOR_UP   */
        { 0.0f, -1.0f, 0.0f },   /* DOOR_DOWN */
        { 1.0f,  0.0f, 0.0f },   /* DOOR_X    */
        { 0.0f,  0.0f, 1.0f }    /* DOOR_Z    */
    };
    if (d->axis < 0 || d->axis >= DOOR_AXES) return;

    float want  = d->amount * 0.01f * st->t;    /* file units -> metres */
    float delta = want - st->applied;
    if (delta == 0.0f) return;

    brush_translate(l->brushes, d->first_brush, d->n_brushes,
                    v3scale(DIR[d->axis], delta));
    st->applied = want;
}

static void apply(Level *l, const DoorDef *d, DoorState *st) {
    if (d->sector < 0) { apply_brush(l, d, st); return; }
    if (d->sector >= l->n_sectors) return;
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

/**
 * @brief Advances one door: its trigger, its travel, and the sector it moves.
 *
 * ENGLISH
 * -------
 * @param[in,out] l          Level whose sector this door drives.
 * @param[in]     i          Door index, into both `l->doors` and ::g_door.
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
 * @param[in]     i          문 인덱스. `l->doors`와 ::g_door 양쪽에 대한 것입니다.
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
    DoorState *st = &g_door[i];
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
        g_refused = d->key;

        /* Re-armed to the full time on every touch rather than only when
           it has run out, so leaning on a locked door keeps the message up
           instead of letting it blink.
           이미 떠 있든 아니든 닿을 때마다 시간을 가득 채웁니다. 잠긴 문에 계속 붙어
           있으면 메시지가 깜빡이지 않고 유지됩니다. */
        g_notice_key = d->key;
        g_notice_t   = DOOR_NOTICE_TIME;
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
    return moved;
}

int door_update(Level *l, v3 player_pos, int keys, float dt) {
    int moved = 0;
    g_refused = KEY_NONE;

    /* Counted down before the touch tests below, which may re-arm it. Order
       matters only in that a refusal this frame must not be shortened by this
       frame's own dt.
       아래의 접촉 검사보다 먼저 감소시킵니다. 검사가 다시 채울 수 있기 때문입니다. 순서가
       중요한 이유는, 이번 프레임의 거절이 이번 프레임의 dt만큼 깎여서는 안 되기
       때문입니다. */
    if (g_notice_t > 0.0f) g_notice_t -= dt;

    /* --- how many doors are there, really? --------------------------------
       Two answers, and they are allowed to disagree only because nothing made
       them agree: `g_n` is how many states door_reset captured, and the level in
       hand says how many definitions it has. Walking to the larger reads a slot
       nobody filled in -- a DoorDef past n_doors, or a DoorState past whatever
       the last reset saw -- so the loops below walk to the smaller and the
       disagreement is counted rather than absorbed.
       답이 둘이며, 둘을 일치시킨 것이 없기 때문에만 어긋날 수 있습니다. `g_n`은 door_reset이
       포착한 상태의 수이고, 손에 든 레벨은 자신의 정의가 몇 개인지 말합니다. 큰 쪽까지
       순회하면 아무도 채우지 않은 슬롯을 읽게 됩니다(n_doors를 넘은 DoorDef, 또는 마지막
       reset이 본 것을 넘은 DoorState). 그래서 아래의 루프들은 작은 쪽까지만 돌고, 그 불일치는
       흡수되지 않고 계수됩니다. */
    int n_level = l->n_doors > LVL_MAX_DOORS ? LVL_MAX_DOORS : l->n_doors;
    if (n_level != g_n) DIAG(DIAG_DOOR_STALE);
    int n = n_level < g_n ? n_level : g_n;

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

        for (int i = 0; i < n; i++)
            if (l->doors[i].tag == tag) touched_tag[i] = 1;
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
