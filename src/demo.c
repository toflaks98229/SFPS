/**
 * @file demo.c
 * @brief Recording and replaying a run. No GL, no window, no file.
 */

#include "demo.h"
#include "txt.h"
#include "diag.h"

/* Where the weapon request sits in `bits`. Four bits is fifteen weapons and
   ::WP_TYPES is well under that; the held state below it uses ten.
   `bits` 안에서 무기 요청이 놓이는 자리입니다. 4비트면 무기 15종이고 ::WP_TYPES는 그보다 훨씬
   적습니다. 그 아래의 유지 상태가 10비트를 씁니다. */
#define DEMO_WEAPON_SHIFT 12
#define DEMO_WEAPON_MASK  0xf

_Static_assert(WP_TYPES < DEMO_WEAPON_MASK,
               "a weapon request must fit in DemoFrame::bits");

void demo_begin(Demo *d, const char *level) {
    txt_copy(d->level, sizeof(d->level), level, -1);
    d->n  = 0;
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

/* ------------------------------------------------------------------- bytes */

int demo_write(const Demo *d, char *buf, int cap) {
    int p = 0;

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

/* ------------------------------------------------------------------ driving */

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

int demo_read(Demo *d, const char *text, int len) {
    const char *p   = text;
    const char *end = text + len;

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
            if (ok && w > 0 && h > 0) { d->vw = w; d->vh = h; }

        } else if (txt_is(tok, tlen, "f")) {
            if (!have_tag) return 0;      /* frames before the tag: not ours */

            int ok = 1, dt = 0, dx = 0, dy = 0, bits = 0;
            p = txt_read_int(p, &dt,   &ok);
            p = txt_read_int(p, &dx,   &ok);
            p = txt_read_int(p, &dy,   &ok);
            p = txt_read_int(p, &bits, &ok);
            if (!ok) return 0;

            if (d->n >= DEMO_MAX_FRAMES) { DIAG(DIAG_DEMO_FULL); continue; }

            DemoFrame *f = &d->f[d->n++];
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

    return have_tag && d->level[0];
}
