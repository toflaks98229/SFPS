/**
 * @file run.c
 * @brief Implements the single reset that puts a playthrough back to its start.
 *
 * ENGLISH
 * -------
 * ::RunState is a plain aggregate with no invariant beyond "a restart clears
 * it", so ::run_reset is that sentence written as code: assign a zeroed struct,
 * then put back by hand only the fields for which zero is the wrong answer.
 *
 * Keeping it in a translation unit of its own rather than beside WinMain is
 * what lets a headless tool link the rule and check it. See run.h for that
 * argument in full.
 *
 * ::run_summary joined it later and belongs to the same sentence: what a run
 * amounted to is a fact about the run, and the screen that draws it should not
 * be the only thing that knows how it is worded. See its note in run.h.
 *
 * @note Touches no GL, no window and no level, and allocates nothing. The
 *       caller owns the ::RunState; this only writes through the pointer it is
 *       handed.
 *
 * 한국어
 * ------
 * ::RunState는 "재시작이 지운다"는 것 외에 어떤 불변식도 없는 단순 집합체이므로,
 * ::run_reset은 그 문장을 코드로 옮긴 것입니다. 0으로 채운 구조체를 대입한 다음, 0이 정답이
 * 아닌 필드만 손으로 되돌립니다.
 *
 * WinMain 옆이 아니라 별도의 번역 단위에 두었기에 헤드리스 도구가 이 규칙을 링크하여
 * 검사할 수 있습니다. 그 논거의 전문은 run.h를 참조하십시오.
 *
 * ::run_summary는 나중에 합류했고 같은 문장에 속합니다. 한 플레이가 무엇이었는지는 그
 * 플레이에 대한 사실이며, 그것을 그리는 화면이 문구를 아는 유일한 곳이어서는 안 됩니다. 그
 * 논거는 run.h의 참고 사항에 있습니다.
 *
 * @note GL도, 창도, 레벨도 건드리지 않으며 아무것도 할당하지 않습니다. ::RunState의
 *       소유권은 호출자에게 있고, 이곳은 건네받은 포인터를 통해 쓰기만 합니다.
 */

#include "run.h"
/* txt_append_*, for the summary line. Header-only and allocation-free, so this
   translation unit still names nothing a headless tool cannot link.
   요약 줄을 위한 txt_append_* 입니다. 헤더 전용이며 할당하지 않으므로, 이 번역 단위는 여전히
   헤드리스 도구가 링크할 수 없는 것을 하나도 지목하지 않습니다. */
#include "txt.h"

/* --- Internal helpers / 내부 도우미 --- */

/* Appends `seconds` as m:ss, with the seconds ALWAYS two digits.
 *
 * The zero pad is the whole reason this is a function rather than two
 * txt_append_int calls at the call site: 187 seconds is "3:07", and the obvious
 * version writes "3:7" for every run that ends in the first ten seconds of a
 * minute. That is a sixth of all runs, and it reads as a corrupt number rather
 * than as a wrong one -- so nobody reports it, they just distrust the screen.
 *
 * Minutes are not carried into hours; see the note on ::run_summary.
 *
 * `seconds`를 m:ss로 덧붙이며, 초는 *언제나 두 자리*입니다.
 *
 * 0 채우기야말로 이것이 호출부의 txt_append_int 두 번이 아니라 함수인 이유 전부입니다. 187초는
 * "3:07"이며, 자명해 보이는 판은 분의 첫 10초 안에 끝난 모든 플레이에 대해 "3:7"을 씁니다. 그것은
 * 전체 플레이의 6분의 1이고, 틀린 숫자가 아니라 깨진 숫자로 읽힙니다. 그래서 아무도 신고하지
 * 않고, 그냥 화면을 믿지 않게 됩니다. */
static int append_clock(char *out, int cap, int pos, float seconds) {
    /* Clamped at zero before the cast. A negative clock cannot be produced by
       ::world_step, but a truncating cast of a small negative float is 0 while
       the modulus below would be negative -- so the guard costs one compare and
       removes a "-0:-5" that would otherwise need a clock to go backwards to be
       noticed.
       캐스팅 전에 0에서 자릅니다. ::world_step은 음수 시계를 만들 수 없지만, 작은 음수 float의
       버림 캐스팅은 0인 반면 아래의 나머지 연산은 음수가 됩니다. 이 검사는 비교 하나를 치르고,
       그러지 않으면 시계가 거꾸로 가야만 발견될 "-0:-5"를 없앱니다. */
    if (!(seconds > 0.0f)) seconds = 0.0f;

    int total = (int)seconds;
    int mins  = total / 60;
    int secs  = total % 60;

    pos = txt_append_int(out, cap, pos, mins);
    pos = txt_append_str(out, cap, pos, ":");
    if (secs < 10) pos = txt_append_str(out, cap, pos, "0");
    return txt_append_int(out, cap, pos, secs);
}

/* --- Public function definitions / 공개 함수 정의 --- */

void run_reset(RunState *r, int title) {
    /* Assign a zeroed aggregate rather than clearing field by field, so a
       field added to ::RunState is reset by construction and this function
       never has to be edited to keep up. That is the failure run.h describes:
       the previous version named its globals one at a time, and the ones it
       did not name were the ones nobody noticed surviving a restart.
       필드를 하나씩 지우지 않고 0으로 채운 집합체를 대입합니다. 그러면 ::RunState에
       추가된 필드가 구조적으로 초기화되며, 보조를 맞추기 위해 이 함수를 고칠 일이
       없습니다. run.h가 기술하는 실패가 바로 그것입니다. 이전 판은 전역 변수를 하나씩
       이름으로 나열했고, 나열되지 않은 것들이 곧 재시작을 견뎌 내는 줄 아무도 알아채지
       못한 것들이었습니다. */
    RunState zero = {0};
    *r = zero;

    /* A mode, not a counter, so the caller decides it rather than this
       function assuming one: startup wants the title screen, while a restart
       goes straight back into play because the player has already asked to.
       This is the one field the zeroing above is deliberately overridden for.
       카운터가 아니라 모드이므로, 이 함수가 임의로 가정하지 않고 호출자가 결정합니다.
       시작 시에는 타이틀 화면을 원하지만 재시작은 곧바로 플레이로 돌아갑니다. 플레이어가
       이미 플레이하겠다고 요청했기 때문입니다. 위의 0 초기화를 의도적으로 덮어쓰는 유일한
       필드입니다. */
    r->title     = title;

    /* Seeded rather than left at zero because the smoke placement state is
       multiplicative: zero's successor is zero, so a run left at the zeroed
       value would draw every lava plume from a degenerate sequence. Seeding it
       from a constant also makes a restart reproduce the same plume as a fresh
       start, which is what keeps a demo replay honest.
       연기 배치 상태가 곱셈 기반이므로 0으로 두지 않고 시드를 부여합니다. 0의 다음 값은
       0이어서, 0인 채로 둔 플레이는 모든 용암 연기를 축퇴된 수열에서 뽑게 됩니다. 상수로
       시드를 주면 재시작이 새로 시작할 때와 같은 연기를 재현하며, 그것이 데모 재생을
       정직하게 유지합니다. */
    r->smoke_rng = SMOKE_RNG_SEED;
}

int run_summary(const RunState *r, char *out, int cap) {
    /* Terminated first, so a caller handed a one-byte buffer still gets a
       string rather than whatever was on its stack. Every append below
       re-terminates as it goes, which is txt.h's own bargain.
       가장 먼저 종료 문자를 씁니다. 그래야 1바이트 버퍼를 건넨 호출자도 스택에 있던 무언가가
       아니라 문자열을 받습니다. 아래의 모든 덧붙이기가 진행하면서 다시 종료 문자를 쓰며, 그것이
       txt.h 자신의 약속입니다. */
    if (cap > 0) out[0] = 0;

    int n = txt_append_str(out, cap, 0, "kills ");
    n = txt_append_int(out, cap, n, r->kills);
    n = txt_append_str(out, cap, n, "   time ");
    n = append_clock(out, cap, n, r->alive_time);

    /* Only when there was an arena to reach a wave in. ::RunState::wave stays
       at 0 on every level without spawners, and `wave 0` on a death screen is
       the screen reporting a field rather than a fact.
       웨이브에 도달할 아레나가 있었을 때만입니다. ::RunState::wave는 스포너가 없는 모든
       레벨에서 0으로 남으며, 사망 화면의 `wave 0`은 사실이 아니라 필드를 보고하는 화면입니다. */
    if (r->wave_best > 0) {
        n = txt_append_str(out, cap, n, "   wave ");
        n = txt_append_int(out, cap, n, r->wave_best);
    }

    return n;
}
