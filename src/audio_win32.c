/**
 * @file audio_win32.c
 * @brief waveOut, four buffers and the thread that refills them. A platform file.
 *
 * ENGLISH
 * -------
 * Split out of audio.c, which is now free of windows.h and holds only what a
 * sound IS -- layers, envelopes, voice eviction, distance falloff. This file
 * holds what a machine PLAYS it on, and every line of it is Win32.
 *
 * The split was not a move. g_ready, the flag gating g_lock's own validity,
 * used to be read by the policy half at four separate call sites, each one
 * writing out the same "is there a device" test by hand. It is folded into
 * ::audio_dev_lock now and audio.c never sees it, which is one fewer piece of
 * cross-thread state in the file that has no threads of its own.
 *
 * TO ADD A HOST: write audio_alsa.c beside this. See audio_dev.h.
 *
 * 한국어
 * ------
 * audio.c에서 분리되었습니다. 그쪽은 이제 windows.h가 없고 소리가 *무엇인지*(레이어, 엔벨로프,
 * 보이스 밀어내기, 거리 감쇠)만을 담습니다. 이 파일은 기계가 그것을 *무엇으로 재생하는지*를
 * 담으며, 모든 줄이 Win32입니다.
 *
 * 이 분리는 단순한 이동이 아니었습니다. g_lock 자체의 유효성을 결정하는 플래그 g_ready는
 * 이전에 정책 절반의 네 지점에서 읽혔고, 각 지점이 "장치가 있는가"라는 같은 검사를 손으로
 * 적어 두고 있었습니다. 이제 ::audio_dev_lock 안에 접혀 있고 audio.c는 그것을 보지 않습니다.
 * 자기 스레드가 없는 파일에서 스레드 간 상태 하나가 사라진 것입니다.
 *
 * 호스트를 추가하려면: 이 옆에 audio_alsa.c를 쓰십시오. audio_dev.h를 참조하십시오.
 */
#include "audio.h"
#include "audio_dev.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

/* --- 장치 상태 / device state --- */

static CRITICAL_SECTION g_lock;

/* 장치가 열리고 스레드가 실행 중인지 여부.
 *
 * ENGLISH
 * -------
 * THE GATE ON g_lock's OWN VALIDITY. audio_shutdown clears it from the game
 * thread while the mixer may still be running, and once it is clear the
 * critical section has been deleted and must not be entered.
 *
 * READ AND WRITTEN THROUGH ::flag_get AND ::flag_set, not as a plain variable,
 * and it used to be `volatile` instead. That was borrowed from MSVC, where
 * volatile carries acquire/release semantics by default. It does not in GCC,
 * which is what builds this: there volatile means only "do not cache it in a
 * register", and orders nothing with respect to the surrounding
 * non-volatile writes. The claim this flag makes is an ORDERING claim -- a
 * thread that sees it set must also see an initialised g_lock and an open
 * device, and a thread that sees it clear must not be about to enter either.
 * That needs a release on the way out and an acquire on the way in, which is
 * what the two helpers below are.
 *
 * It worked, and would have gone on working: x86 does not reorder stores with
 * stores or loads with loads, so the hardware supplied for free what the source
 * never asked for. Nothing here is a bug report. It is a claim that was true
 * about the machine rather than about the program.
 *
 * 한국어
 * ------
 * g_lock 자체의 유효성을 결정하는 게이트입니다. audio_shutdown이 믹서가 아직 실행 중일 수
 * 있는 상태에서 게임 스레드로부터 이 값을 해제하며, 해제된 이후에는 임계 영역이 삭제된
 * 상태이므로 진입해서는 안 됩니다.
 *
 * 평범한 변수가 아니라 ::flag_get과 ::flag_set을 통해 읽고 씁니다. 이전에는 대신
 * `volatile`이었습니다. 그것은 MSVC에서 빌려 온 것으로, 그곳에서는 volatile이 기본적으로
 * 획득·해제 의미론을 가집니다. 이것을 빌드하는 GCC에서는 그렇지 않습니다. 그곳에서 volatile은
 * "레지스터에 캐시하지 말라"는 뜻일 뿐이며 주변의 비휘발성 쓰기에 대해 아무 순서도 정하지
 * 않습니다. 이 플래그가 하는 주장은 *순서*에 대한 주장입니다. 이것이 설정된 것을 본 스레드는
 * 초기화된 g_lock과 열린 장치도 보아야 하고, 해제된 것을 본 스레드는 그 어느 쪽에도 진입하려
 * 해서는 안 됩니다. 그러려면 나갈 때 해제, 들어올 때 획득이 필요하며, 아래 두 헬퍼가 그것입니다.
 *
 * 동작했고 앞으로도 동작했을 것입니다. x86은 저장을 저장과, 적재를 적재와 재배열하지 않으므로
 * 하드웨어가 소스 코드는 요청한 적 없는 것을 공짜로 공급했습니다. 이것은 버그 보고가 아닙니다.
 * 프로그램이 아니라 기계에 대해 참이던 주장입니다.
 */
static LONG g_ready;

// 오디오 백엔드
static HWAVEOUT g_dev;
static HANDLE   g_event, g_thread;
static WAVEHDR  g_hdr[NBUF];
static short    g_buf[NBUF][FRAMES];
/* Read by the mixer thread's loop and cleared by the game thread. Same
   treatment as g_ready and for the same reason; see the note above it.
   믹서 스레드의 루프가 읽고 게임 스레드가 해제합니다. g_ready와 같은 처리이며 이유도
   같습니다. 그 위의 설명을 참조하십시오. */
static LONG g_running;


/**
 * @brief Reads a cross-thread flag, and everything the writer wrote before it.
 *
 * ENGLISH
 * -------
 * Acquire, so a thread that sees the flag set also sees the critical section,
 * the device and the buffers that were prepared before it was set. On x86 this
 * compiles to the same plain load `volatile` did -- the barrier is against the
 * COMPILER reordering, which is the part that was never guaranteed.
 *
 * 한국어
 * ------
 * @brief 스레드 간 플래그를, 기록자가 그 이전에 쓴 모든 것과 함께 읽습니다.
 *
 * 획득 의미론입니다. 플래그가 설정된 것을 본 스레드는 그것이 설정되기 전에 준비된 임계 영역과
 * 장치와 버퍼도 봅니다. x86에서는 `volatile`이 만들던 것과 같은 평범한 적재로 컴파일됩니다.
 * 장벽이 막는 것은 *컴파일러*의 재배열이며, 그 부분이 보장된 적 없던 것입니다.
 */
static int flag_get(LONG *p) {
    return (int)__atomic_load_n(p, __ATOMIC_ACQUIRE);
}

/**
 * @brief Publishes a cross-thread flag, and everything written before it.
 *
 * ENGLISH: Release, so nothing the compiler could have moved past this write
 * becomes visible to a reader before the write itself.
 *
 * 한국어: 해제 의미론입니다. 컴파일러가 이 쓰기 너머로 옮길 수 있었을 무엇도, 그 쓰기 자체보다
 * 먼저 판독자에게 보이지 않게 됩니다.
 */
static void flag_set(LONG *p, int v) {
    __atomic_store_n(p, (LONG)v, __ATOMIC_RELEASE);
}

/* --- 락: 게이트를 접어 넣은 채로 / the lock, with the gate folded in --- */

int audio_dev_lock(void) {
    /* Acquire on the way in, which is the half of the ordering claim this
       side makes: a caller that sees the flag set must also see an
       initialised critical section and an open device behind it.
       들어올 때 획득이며, 이쪽이 하는 순서 주장의 절반입니다. 플래그가 설정된 것을 본
       호출자는 그 뒤의 초기화된 임계 영역과 열린 장치도 보아야 합니다. */
    if (!flag_get(&g_ready)) return 0;
    EnterCriticalSection(&g_lock);
    return 1;
}

void audio_dev_unlock(void) {
    LeaveCriticalSection(&g_lock);
}

/* --- 믹서 스레드 / the mixer thread --- */

/**
 * @brief 오디오 믹싱을 처리하는 백그라운드 스레드 함수입니다.
 * @param param 스레드 파라미터 (사용되지 않음).
 * @return 스레드 종료 코드.
 */
static DWORD WINAPI mixer_thread(LPVOID param) {
    (void)param;
    while (flag_get(&g_running)) {
        WaitForSingleObject(g_event, 100);
        for (int i = 0; i < NBUF; i++) {
            if (!(g_hdr[i].dwFlags & WHDR_DONE)) continue;
            audio_mix(g_buf[i], FRAMES);
            waveOutWrite(g_dev, &g_hdr[i], sizeof(WAVEHDR));
        }
    }
    return 0;
}

/* --- 열고 닫기 / opening and closing --- */

int audio_init(void) {
    WAVEFORMATEX fmt = {0};
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = 1;
    fmt.nSamplesPerSec  = RATE;
    fmt.wBitsPerSample  = 16;
    fmt.nBlockAlign     = 2;
    fmt.nAvgBytesPerSec = RATE * 2;

    g_event = CreateEventA(0, FALSE, FALSE, 0);
    if (!g_event) return 0;

    if (waveOutOpen(&g_dev, WAVE_MAPPER, &fmt, (DWORD_PTR)g_event, 0,
                    CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(g_event);
        g_event = 0;
        return 0;
    }

    InitializeCriticalSection(&g_lock);

    for (int i = 0; i < NBUF; i++) {
        g_hdr[i].lpData         = (LPSTR)g_buf[i];
        g_hdr[i].dwBufferLength = FRAMES * 2;
        waveOutPrepareHeader(g_dev, &g_hdr[i], sizeof(WAVEHDR));
        waveOutWrite(g_dev, &g_hdr[i], sizeof(WAVEHDR));
    }

    flag_set(&g_running, 1);
    g_thread = CreateThread(0, 0, mixer_thread, 0, 0, 0);
    if (!g_thread) { audio_shutdown(); return 0; }

    /* LAST, and that is the ordering this flag exists to express: everything
       it gates -- the lock, the device, the prepared headers, the running mixer
       -- is in place above, and the release makes all of it visible to whoever
       sees this set.
       마지막이며, 그것이 이 플래그가 표현하려는 순서입니다. 이것이 여는 모든 것(락, 장치,
       준비된 헤더, 실행 중인 믹서)이 위에서 갖춰졌고, 해제 의미론이 그 전부를 이 값이 설정된
       것을 본 쪽에게 보이게 합니다. */
    flag_set(&g_ready, 1);
    return 1;
}

void audio_shutdown(void) {
    if (!g_event) return;

    /* Close the gate FIRST. g_ready is what audio_play tests before entering
       g_lock, so it has to be false before the critical section is deleted --
       otherwise a call already past the test enters a deleted lock. The
       mixer is stopped and joined below, so once this is clear no thread can
       reach the lock again.

       가장 먼저 게이트를 닫습니다. g_ready는 audio_play가 g_lock에 진입하기 전에
       검사하는 값이므로, 임계 영역이 삭제되기 전에 반드시 거짓이어야 합니다. 그렇지
       않으면 이미 검사를 통과한 호출이 삭제된 락에 진입하게 됩니다. 아래에서 믹서를
       정지시키고 합류시키므로, 이 값이 해제된 뒤에는 어떤 스레드도 락에 다시 도달할
       수 없습니다. */
    int was_ready = flag_get(&g_ready);
    flag_set(&g_ready, 0);

    flag_set(&g_running, 0);
    if (g_thread) {
        SetEvent(g_event);
        WaitForSingleObject(g_thread, 500);
        CloseHandle(g_thread);
        g_thread = 0;
    }
    if (g_dev) {
        waveOutReset(g_dev);
        for (int i = 0; i < NBUF; i++)
            waveOutUnprepareHeader(g_dev, &g_hdr[i], sizeof(WAVEHDR));
        waveOutClose(g_dev);
        g_dev = 0;
    }
    /* Safe now: the mixer has been joined and the gate is shut. */
    if (was_ready) DeleteCriticalSection(&g_lock);
    CloseHandle(g_event);
    g_event = 0;
}
