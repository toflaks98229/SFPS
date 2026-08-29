/**
 * @file demo.c
 * @brief Records a run as intent, replays it back, and moves it through text.
 *
 * ENGLISH
 * -------
 * A RECORDING IS INTENT, NOT OUTCOME. What is stored each frame is what the
 * player asked for -- the buttons, the mouse deltas, the elapsed time -- and
 * never where anybody ended up. Replaying feeds that intent back into
 * ::world_step and the simulation reaches the same place on its own. That is
 * what makes a demo about twelve bytes a frame instead of a snapshot of the
 * world, and it is also what makes a demo a TEST: a replay that diverges is a
 * simulation that changed.
 *
 * NO GL, NO WINDOW, NO FILE. This module never opens anything. ::demo_write
 * and ::demo_read move a recording through a caller-owned character buffer,
 * and whoever wants that buffer on disk is demo_file.c. The split is what lets
 * tools/demotest.c round-trip a recording with no platform underneath it.
 *
 * THE FRAME IS PACKED BY HAND. ::DemoFrame is four narrow fields, and the
 * buttons share one of them with the weapon request. The layout is not an
 * optimisation for its own sake -- ::DEMO_MAX_FRAMES frames at the obvious
 * struct size would not fit the budget this project is named after.
 *
 * @note Every entry point writes through caller-owned storage and allocates
 *       nothing. A ::Demo is large and demo.h says plainly it is never a stack
 *       local; this file assumes the caller obeyed that and does not check.
 * @warning ::DEMO_VERSION is checked on read and refused on mismatch. Any
 *          change to ::DemoFrame's layout, to the bit assignments below, or to
 *          what a field means MUST bump it -- a recording from a different
 *          layout parses cleanly and then diverges, which presents as a
 *          physics bug rather than as a bad file.
 *
 * 한국어
 * ------
 * *기록은 결과가 아니라 의도입니다.* 매 프레임 저장되는 것은 플레이어가 요청한 것(버튼, 마우스
 * 변위, 경과 시간)이며 누가 어디에 도달했는지는 결코 저장하지 않습니다. 재생은 그 의도를
 * ::world_step에 다시 흘려보내고 시뮬레이션이 스스로 같은 자리에 도달합니다. 그래서 데모가
 * 월드의 스냅숏이 아니라 프레임당 12바이트 남짓이 되며, 또한 데모가 *테스트*가 됩니다. 갈라지는
 * 재생은 곧 달라진 시뮬레이션입니다.
 *
 * *GL도, 창도, 파일도 없습니다.* 이 모듈은 아무것도 열지 않습니다. ::demo_write와 ::demo_read는
 * 호출자가 소유한 문자 버퍼를 통해 기록을 옮기며, 그 버퍼를 디스크에 두고자 하는 쪽은
 * demo_file.c입니다. 이 분리 덕분에 tools/demotest.c가 아래에 플랫폼 없이도 기록을 왕복시킬 수
 * 있습니다.
 *
 * *프레임은 손으로 채웁니다.* ::DemoFrame은 좁은 필드 네 개이고, 버튼들은 그중 하나를 무기
 * 요청과 나눠 씁니다. 이 배치는 그 자체를 위한 최적화가 아닙니다. 뻔한 구조체 크기로는
 * ::DEMO_MAX_FRAMES 프레임이 이 프로젝트의 이름이 가리키는 예산에 들어가지 않습니다.
 *
 * @note 모든 진입점은 호출자가 소유한 저장 공간에 기록하며 아무것도 할당하지 않습니다.
 *       ::Demo는 크고 demo.h는 그것이 결코 스택 지역 변수가 아니라고 분명히 말합니다. 이
 *       파일은 호출자가 그것을 지켰다고 가정하며 확인하지 않습니다.
 * @warning ::DEMO_VERSION은 읽기 시 검사하며 불일치하면 거절합니다. ::DemoFrame의 배치, 아래의
 *          비트 배정, 또는 어떤 필드의 의미를 바꾸면 *반드시* 이 값을 올려야 합니다. 다른
 *          배치의 기록은 깔끔하게 파싱된 다음 갈라지며, 그것은 잘못된 파일이 아니라 물리
 *          버그처럼 보입니다.
 */

#include "demo.h"

#include "txt.h"
#include "diag.h"

/* --- File-local constants: the bit layout of DemoFrame::bits / 파일 지역 상수: DemoFrame::bits의 비트 배치 --- */

/**
 * @brief Bit position of the weapon request inside ::DemoFrame::bits.
 *
 * ENGLISH
 * -------
 * The DEMO_* button flags occupy the low bits and the weapon request sits
 * above them. Ten flags are defined in demo.h, so twelve leaves room for two
 * more without moving this field and therefore without bumping
 * ::DEMO_VERSION.
 *
 * 한국어
 * ------
 * DEMO_* 버튼 플래그가 하위 비트를 차지하고 무기 요청이 그 위에 놓입니다. demo.h에 플래그가
 * 열 개 정의되어 있으므로, 12로 두면 이 필드를 옮기지 않고 따라서 ::DEMO_VERSION을 올리지
 * 않고도 두 개를 더 넣을 여지가 남습니다.
 */
#define DEMO_WEAPON_SHIFT 12

/**
 * @brief Mask of the weapon request once shifted down. Four bits, so fifteen weapons.
 *
 * ENGLISH
 * -------
 * Doubles as the range check in ::demo_record: a request that will not survive
 * the round trip is recorded as weapon 0 rather than truncated into a
 * different weapon.
 *
 * 한국어
 * ------
 * ::demo_record에서 범위 검사 역할도 겸합니다. 왕복을 견디지 못할 요청은 다른 무기로 잘려
 * 들어가지 않고 무기 0으로 기록됩니다.
 */
#define DEMO_WEAPON_MASK  0xf

/* The field has to be able to hold every weapon there is. Caught here rather
   than in a replay, where the symptom is a recording that reaches for the
   wrong gun in the last few frames.
   이 필드는 존재하는 모든 무기를 담을 수 있어야 합니다. 재생 중이 아니라 이곳에서 잡습니다.
   재생에서의 증상은 마지막 몇 프레임에 엉뚱한 총을 집는 기록입니다. */
_Static_assert(WP_TYPES < DEMO_WEAPON_MASK,
               "a weapon request must fit in DemoFrame::bits");

/* --- Public function definitions: recording and replay / 공개 함수 정의: 기록과 재생 --- */

void demo_begin(Demo *d, const char *level) {
    txt_copy(d->level, sizeof(d->level), level, -1);
    d->n  = 0;
    /* Left at zero rather than given a default shape, because ::demo_record
       treats zero as "not yet taken" and fills both from the first frame.
       기본 형태를 주지 않고 0으로 둡니다. ::demo_record가 0을 "아직 받지 않음"으로 취급하고
       첫 프레임에서 둘 다 채우기 때문입니다. */
    d->vw = 0;
    d->vh = 0;
}

void demo_record(Demo *d, const Input *in, int vw, int vh, float dt) {
    if (d->n >= DEMO_MAX_FRAMES) { DIAG(DIAG_DEMO_FULL); return; }

    /* Taken from the first frame and not written again. It is the window's
       shape, the player does not change it mid-run, and a per-frame copy would
       be 18,000 identical numbers to make a resize -- which reloads nothing and
       changes no rule -- expressible.
       첫 프레임에서 가져오고 다시 쓰지 않습니다. 창의 형태이고 플레이어가 플레이 도중 바꾸지
       않으며, 프레임마다 복사하면 크기 변경 하나를 표현하자고 똑같은 숫자 18,000개를 두는
       셈입니다. 크기 변경은 아무것도 다시 불러오지 않고 어떤 규칙도 바꾸지 않습니다. */
    if (d->n == 0) {
        d->vw = vw > 0 ? vw : 1;
        d->vh = vh > 0 ? vh : 1;
    }

    DemoFrame *f = &d->f[d->n++];

    /* Rounded, not truncated: dt is a measured duration and the nearest
       microsecond is the honest one. Clamped because the frame loop already
       clamps dt to 100ms, and 65535us is what the field holds.
       버림이 아니라 반올림입니다. dt는 측정된 시간이고 가장 가까운 마이크로초가 정직한
       값입니다. 프레임 루프가 이미 dt를 100ms로 제한하며 이 필드가 담는 값이 65535us이므로
       상한을 둡니다. */
    int us = (int)(dt * 1000000.0f + 0.5f);
    if (us < 0)     us = 0;
    if (us > 65535) us = 65535;
    f->dt_us = (unsigned short)us;

    f->look_dx = (short)in->look_dx;
    f->look_dy = (short)in->look_dy;

    /* Every button folded into one word. Written as ten independent tests
       rather than a loop over a table, because ::Input names its fields and
       there is no array to walk.
       모든 버튼을 한 워드에 접어 넣습니다. 표를 도는 루프가 아니라 열 번의 독립적인 판정으로
       적습니다. ::Input은 필드에 이름을 붙이며 순회할 배열이 없기 때문입니다. */
    unsigned b = 0;
    if (in->forward) b |= DEMO_FORWARD;
    if (in->back)    b |= DEMO_BACK;
    if (in->left)    b |= DEMO_LEFT;
    if (in->right)   b |= DEMO_RIGHT;
    if (in->jump)    b |= DEMO_JUMP;
    if (in->fire)    b |= DEMO_FIRE;
    if (in->hook)    b |= DEMO_HOOK;
    if (in->paused)  b |= DEMO_PAUSED;
    if (in->confirm) b |= DEMO_CONFIRM;
    if (in->let_go)  b |= DEMO_LET_GO;

    /* An out-of-range request becomes "no request" rather than being masked
       into a different weapon. Masking would record a switch the player never
       asked for, and a replay that draws the wrong gun looks like a weapon bug.
       범위를 벗어난 요청은 다른 무기로 마스크되지 않고 "요청 없음"이 됩니다. 마스크하면
       플레이어가 요청한 적 없는 전환을 기록하게 되며, 엉뚱한 총을 꺼내는 재생은 무기 결함처럼
       보입니다. */
    int wp = in->want_weapon;
    if (wp < 0 || wp > DEMO_WEAPON_MASK) wp = 0;
    b |= (unsigned)wp << DEMO_WEAPON_SHIFT;

    f->bits = (unsigned short)b;
}

int demo_replay(const Demo *d, int i, Input *in, float *aspect, float *dt) {
    if (i < 0 || i >= d->n) return 0;

    const DemoFrame *f = &d->f[i];

    /* Zeroed first, so every field is written whatever this format grows to
       carry. An Input the caller reused would otherwise contribute the fields
       a recording does not name, which is a replay differing from its
       recording for a reason the recording cannot show.
       먼저 0으로 만들어, 이 형식이 무엇을 나르게 되든 모든 필드가 기록되도록 합니다. 그러지
       않으면 호출자가 재사용한 Input이 기록이 이름 붙이지 않은 필드를 보태게 되며, 그것은
       기록이 보여 줄 수 없는 이유로 재생이 기록과 달라지는 일입니다. */
    Input z = {0};
    *in = z;

    /* The exact inverse of the packing in ::demo_record, in the same order, so
       the two can be read against each other.
       ::demo_record의 채우기를 같은 순서로 정확히 뒤집습니다. 그래야 둘을 나란히 놓고 읽을 수
       있습니다. */
    unsigned b = f->bits;
    in->forward = (b & DEMO_FORWARD) != 0;
    in->back    = (b & DEMO_BACK)    != 0;
    in->left    = (b & DEMO_LEFT)    != 0;
    in->right   = (b & DEMO_RIGHT)   != 0;
    in->jump    = (b & DEMO_JUMP)    != 0;
    in->fire    = (b & DEMO_FIRE)    != 0;
    in->hook    = (b & DEMO_HOOK)    != 0;
    in->paused  = (b & DEMO_PAUSED)  != 0;
    in->confirm = (b & DEMO_CONFIRM) != 0;
    in->let_go  = (b & DEMO_LET_GO)  != 0;
    in->want_weapon = (int)((b >> DEMO_WEAPON_SHIFT) & DEMO_WEAPON_MASK);

    in->look_dx = (float)f->look_dx;
    in->look_dy = (float)f->look_dy;

    /* Divided, not scaled by a stored reciprocal: float division is correctly
       rounded, so this is the nearest float to vw/vh and therefore the same
       value the game computed from the same two integers.
       저장된 역수를 곱하지 않고 나눕니다. 부동소수점 나눗셈은 올바르게 반올림되므로 이 값은
       vw/vh에 가장 가까운 float이며, 따라서 게임이 같은 두 정수로부터 계산한 값과 같습니다. */
    *aspect = (float)(d->vw > 0 ? d->vw : 1) / (float)(d->vh > 0 ? d->vh : 1);
    *dt     = f->dt_us * 0.000001f;
    return 1;
}

/* --- Public function definitions: text serialisation / 공개 함수 정의: 텍스트 직렬화 --- */

int demo_write(const Demo *d, char *buf, int cap) {
    int p = 0;

    /* The header, then one line per frame. Text rather than a struct dump
       because the fields are narrow and the format is the thing
       ::DEMO_VERSION guards -- a byte layout would guard nothing a compiler
       could not silently change.
       머리말을 쓰고 프레임마다 한 줄을 씁니다. 구조체 덤프가 아니라 텍스트인 이유는 필드가
       좁고 ::DEMO_VERSION이 지키는 대상이 곧 이 형식이기 때문입니다. 바이트 배치는 컴파일러가
       조용히 바꿀 수 있는 것을 전혀 지켜 주지 못합니다. */
    p = txt_append_str(buf, cap, p, "demo ");
    p = txt_append_int(buf, cap, p, DEMO_VERSION);
    p = txt_append_str(buf, cap, p, "\nlevel ");
    p = txt_append_str(buf, cap, p, d->level);
    p = txt_append_str(buf, cap, p, "\nview ");
    p = txt_append_int(buf, cap, p, d->vw);
    p = txt_append_str(buf, cap, p, " ");
    p = txt_append_int(buf, cap, p, d->vh);
    p = txt_append_str(buf, cap, p, "\n");

    for (int i = 0; i < d->n; i++) {
        const DemoFrame *f = &d->f[i];
        p = txt_append_str(buf, cap, p, "f ");
        p = txt_append_int(buf, cap, p, f->dt_us);
        p = txt_append_str(buf, cap, p, " ");
        p = txt_append_int(buf, cap, p, f->look_dx);
        p = txt_append_str(buf, cap, p, " ");
        p = txt_append_int(buf, cap, p, f->look_dy);
        p = txt_append_str(buf, cap, p, " ");
        p = txt_append_int(buf, cap, p, f->bits);
        p = txt_append_str(buf, cap, p, "\n");
    }

    /* txt_append_* clamp at `cap - 1` and never report past it, so hitting the
       ceiling is what a truncation looks like from here. A recording that
       exactly fills its buffer is refused along with the truncated ones,
       because from this side they are the same observation -- and a demo cut
       short replays into a different run, which is worse than a demo refused.
       txt_append_*는 `cap - 1`에서 멈추며 그보다 큰 값을 결코 보고하지 않으므로, 이곳에서
       절단은 천장에 닿은 것으로 보입니다. 버퍼를 정확히 채운 기록도 잘린 것들과 함께
       거절됩니다. 이쪽에서 보면 둘은 같은 관측이며, 잘린 데모는 다른 플레이로 재생되므로
       거절된 데모보다 나쁘기 때문입니다. */
    if (p >= cap - 1) return 0;
    return p;
}

int demo_read(Demo *d, const char *text, int len) {
    const char *p   = text;
    const char *end = text + len;

    /* Emptied before the first token, so a refused parse leaves an empty
       recording rather than half of this file over whatever the caller had.
       첫 토큰보다 먼저 비웁니다. 그래야 거절된 파싱이, 호출자가 갖고 있던 것 위에 이 파일의
       절반을 얹은 상태가 아니라 빈 기록을 남깁니다. */
    d->n        = 0;
    d->vw       = 0;
    d->vh       = 0;
    d->level[0] = 0;

    int have_tag = 0;

    while (p < end) {
        p = txt_skip(p);
        if (p >= end || !*p) break;

        int tlen = 0;
        const char *tok = txt_token(p, &tlen);
        if (!tok || tlen <= 0) break;
        p = tok + tlen;

        if (txt_is(tok, tlen, "demo")) {
            int ok = 0, ver = 0;
            p = txt_read_int(p, &ver, &ok);
            /* A recording from a build that wrote a different layout is refused
               rather than read as this one. The fields would parse and the run
               would diverge, which is the failure that looks like a physics bug.
               다른 배치를 쓰던 빌드의 기록은 이 형식으로 읽는 대신 거절합니다. 필드는 파싱될
               것이고 플레이는 갈라질 것이며, 그것이 물리 버그처럼 보이는 고장입니다. */
            if (!ok || ver != DEMO_VERSION) return 0;
            have_tag = 1;

        } else if (txt_is(tok, tlen, "level")) {
            p = txt_skip(p);
            int nlen = 0;
            const char *nm = txt_token(p, &nlen);
            if (!nm || nlen <= 0) return 0;
            txt_copy(d->level, sizeof(d->level), nm, nlen);
            p = nm + nlen;

        } else if (txt_is(tok, tlen, "view")) {
            int ok = 1, w = 0, h = 0;
            p = txt_read_int(p, &w, &ok);
            p = txt_read_int(p, &h, &ok);
            /* Accepted only when both are positive, and otherwise left at
               zero for ::demo_replay to substitute. A stored viewport is a
               convenience, not the thing the file is for, so a bad one does
               not cost the frames.
               둘 다 양수일 때만 받아들이고, 그렇지 않으면 ::demo_replay가 대신 채우도록 0으로
               둡니다. 저장된 뷰포트는 편의이지 이 파일의 목적이 아니므로, 잘못된 값이
               프레임을 잃게 하지는 않습니다. */
            if (ok && w > 0 && h > 0) { d->vw = w; d->vh = h; }

        } else if (txt_is(tok, tlen, "f")) {
            if (!have_tag) return 0;      /* frames before the tag: not ours */

            int ok = 1, dt = 0, dx = 0, dy = 0, bits = 0;
            p = txt_read_int(p, &dt,   &ok);
            p = txt_read_int(p, &dx,   &ok);
            p = txt_read_int(p, &dy,   &ok);
            p = txt_read_int(p, &bits, &ok);
            if (!ok) return 0;

            /* `continue`, not `return`: the frames already read are a valid
               prefix of the run, and a file longer than this build can hold is
               reported rather than thrown away.
               `return`이 아니라 `continue`입니다. 이미 읽은 프레임은 그 플레이의 유효한
               앞부분이며, 이 빌드가 담을 수 있는 것보다 긴 파일은 버리지 않고 보고합니다. */
            if (d->n >= DEMO_MAX_FRAMES) { DIAG(DIAG_DEMO_FULL); continue; }

            DemoFrame *f = &d->f[d->n++];
            /* Masked back down to the field widths ::demo_write emitted from.
               The text carries no upper bound of its own, so a hand-edited or
               corrupt number is narrowed here rather than at every use.
               ::demo_write가 내보낸 필드 폭으로 다시 마스크합니다. 텍스트는 자체적인 상한을
               갖지 않으므로, 손으로 고쳤거나 손상된 숫자를 사용하는 자리마다가 아니라 이곳에서
               좁힙니다. */
            f->dt_us   = (unsigned short)(dt   & 0xffff);
            f->look_dx = (short)dx;
            f->look_dy = (short)dy;
            f->bits    = (unsigned short)(bits & 0xffff);
        }
        /* Anything else is skipped rather than refused, so a comment or a key a
           later build writes does not make this one reject the file.
           그 밖의 것은 거절하지 않고 건너뜁니다. 주석이나 이후 빌드가 쓰는 키 때문에 이 빌드가
           파일을 거부하지 않도록 하기 위함입니다. */
    }

    /* Both required: a file with a tag but no level names nothing to load, and
       frames without a tag were never this format's to begin with.
       둘 다 필요합니다. 태그는 있으나 레벨이 없는 파일은 불러올 대상을 지목하지 못하고, 태그
       없는 프레임은 애초에 이 형식의 것이 아니었습니다. */
    return have_tag && d->level[0];
}

/* --- Public function definitions: driving a run / 공개 함수 정의: 플레이 구동 --- */

int demo_take(DemoDrive *dr, Input *in, float *aspect, float *dt) {
    if (dr->mode != DEMO_PLAY) return 0;

    /* Into locals first, so a recording that has run out leaves the caller's
       own aspect and dt exactly as it found them. ::demo_replay writes both
       before it can know whether the frame exists, and the caller's values are
       the live ones it is about to fall back to.
       먼저 지역 변수로 받습니다. 그래야 다 떨어진 기록이 호출자 자신의 종횡비와 dt를 찾은
       그대로 남깁니다. ::demo_replay는 프레임의 존재 여부를 알기 전에 둘 다 쓰며, 호출자의
       값은 곧 되돌아갈 라이브 값입니다. */
    Input got;
    float got_aspect = 0.0f, got_dt = 0.0f;

    if (!demo_replay(&dr->d, dr->frame, &got, &got_aspect, &got_dt)) {
        /* Played out. Control goes back to the player rather than the process
           ending: a demo is a thing to watch, and what somebody does when one
           finishes is play. That is also what makes this an attract mode with
           nothing added.
           다 재생되었습니다. 프로세스가 끝나는 것이 아니라 조작권이 플레이어에게 돌아갑니다.
           데모는 보는 것이고, 그것이 끝났을 때 사람이 하는 일은 플레이입니다. 그 덕분에
           아무것도 더하지 않고 이것이 어트랙트 모드가 됩니다. */
        dr->mode = DEMO_OFF;
        return 0;
    }

    /* Published only once the frame is known to exist, which is what the
       locals above were for.
       프레임이 존재함이 확인된 뒤에만 내보냅니다. 위의 지역 변수들이 그것을 위한
       것이었습니다. */
    dr->frame++;
    *in     = got;
    *aspect = got_aspect;
    *dt     = got_dt;
    return 1;
}

void demo_put(DemoDrive *dr, const Input *in, int vw, int vh, float dt) {
    if (dr->mode != DEMO_RECORD) return;
    demo_record(&dr->d, in, vw, vh, dt);
}
