/**
 * @file audio_web.c
 * @brief Web Audio under audio_dev.h. Four buffers ahead, exactly as waveOut,
 *        and no thread at all.
 *
 * ENGLISH
 * -------
 * audio_dev.h describes a device that calls ::audio_mix when a buffer comes
 * back empty, and a lock the policy takes because the voice table it writes is
 * the one the mixer reads. This host keeps the first half and deletes the
 * second, and the header had already written down the case where that is
 * allowed.
 *
 * THERE IS NO MIXER THREAD, AND THAT IS THE WHOLE DESIGN. Emscripten can give
 * one, and it would cost the entire distribution: a thread means
 * SharedArrayBuffer, which means COOP/COEP headers, which means a host that
 * lets you set headers -- and GitHub Pages cannot, while itch.io needs a
 * per-project switch. An AudioWorklet has the same price for the same reason,
 * because reaching wasm memory from the audio thread is what the shared buffer
 * is for. So the samples are made on the main thread, ahead of time, and
 * handed to the browser already scheduled.
 *
 * WHY THAT IS NOT A COMPROMISE. A browser's event loop runs one task at a
 * time: the pump below and the frame loop are both tasks, and neither can
 * begin in the middle of the other. The voice table can therefore never be
 * observed half-written, which is precisely what the lock on Windows exists to
 * prevent -- so there is nothing here for a lock to do. ::audio_dev_lock says
 * so by answering 0.
 *
 * THE SHAPE IS waveOut'S, deliberately. ::FRAMES samples a chunk, ::NBUF
 * chunks kept scheduled ahead, ::RATE asked of the AudioContext directly so
 * nothing in audio.c has to know the hardware runs at 48000. The numbers that
 * describe the latency are the same numbers on both hosts, and they are in
 * audio_dev.h where they were.
 *
 * 한국어
 * ------
 * @brief audio_dev.h 아래의 Web Audio. waveOut과 똑같이 버퍼 넷을 앞세우며, 스레드는 아예
 *        없습니다.
 *
 * audio_dev.h는 버퍼가 비어 돌아올 때 ::audio_mix를 부르는 장치와, 정책이 쓰는 보이스 표가
 * 곧 믹서가 읽는 것이기에 정책이 획득하는 락을 서술합니다. 이 호스트는 앞의 절반을 지키고
 * 뒤의 절반을 지웁니다. 그리고 그것이 허용되는 경우를 그 헤더가 이미 적어 두었습니다.
 *
 * *믹서 스레드가 없으며, 그것이 설계의 전부입니다.* Emscripten은 스레드를 줄 수 있고, 그
 * 대가는 배포 전체입니다. 스레드는 SharedArrayBuffer를 뜻하고, 그것은 COOP/COEP 헤더를 뜻하며,
 * 그것은 헤더를 설정할 수 있는 호스트를 뜻합니다. GitHub Pages는 그럴 수 없고 itch.io는
 * 프로젝트별 스위치를 켜야 합니다. AudioWorklet도 같은 이유로 같은 값을 치릅니다. 오디오
 * 스레드에서 wasm 메모리에 닿는 것이 바로 공유 버퍼가 하는 일이기 때문입니다. 그래서 샘플은
 * 메인 스레드에서, 미리, 만들어지고 이미 예약된 채로 브라우저에 건네집니다.
 *
 * *그것이 타협이 아닌 이유.* 브라우저의 이벤트 루프는 한 번에 하나의 작업을 돌립니다. 아래의
 * 펌프와 프레임 루프는 둘 다 작업이며, 어느 쪽도 다른 쪽 한가운데서 시작할 수 없습니다. 따라서
 * 보이스 표가 절반만 쓰인 채로 관측되는 일이 결코 없으며, 그것이 바로 Windows의 락이 막으려는
 * 것입니다. 그러니 이곳에는 락이 할 일이 없습니다. ::audio_dev_lock이 0으로 답하여 그렇게
 * 말합니다.
 *
 * *형태는 의도적으로 waveOut의 것입니다.* 덩어리당 ::FRAMES 샘플, 앞세워 예약해 두는 ::NBUF
 * 개의 덩어리, 그리고 AudioContext에 직접 요구하는 ::RATE. 그래서 audio.c의 어떤 것도 하드웨어가
 * 48000으로 돈다는 것을 알 필요가 없습니다. 지연 시간을 서술하는 수들이 두 호스트에서 같은
 * 수이며, 있던 자리인 audio_dev.h에 그대로 있습니다.
 */
#include "audio.h"
#include "audio_dev.h"

#include <emscripten.h>

/* One chunk's worth of mono samples, filled by ::audio_mix and read straight
   out of the heap by the pump. Static rather than malloc'd so its address
   never moves: the JS side caches nothing, but a pointer that changed between
   calls would be a bug nobody could see.
   ::audio_mix가 채우고 펌프가 힙에서 곧장 읽어 가는, 덩어리 하나 분량의 모노 샘플입니다.
   주소가 결코 움직이지 않도록 malloc이 아니라 static입니다. JS 쪽은 아무것도 캐시하지 않지만,
   호출 사이에 달라지는 포인터는 아무도 볼 수 없는 버그일 것입니다. */
static short g_chunk[FRAMES];
static int   g_ready;

/* --- what the pump calls / 펌프가 부르는 것 --- */

EMSCRIPTEN_KEEPALIVE short *audio_web_chunk(void) { return g_chunk; }

EMSCRIPTEN_KEEPALIVE void audio_web_fill(void) { audio_mix(g_chunk, FRAMES); }

int audio_init(void) {
    if (g_ready) return 1;

    int ok = EM_ASM_INT({
        var rate   = $0;
        var frames = $1;
        var nbuf   = $2;

        var Ctor = window.AudioContext || window.webkitAudioContext;
        if (!Ctor) return 0;

        var ctx;
        try {
            /* THE RATE IS ASKED FOR, not accepted. Hardware commonly runs at
               48000 and a context that came back at 48000 would play every
               sound a tenth of a semitone sharp and slightly fast -- audible on
               music, and nothing in audio.c is in a position to notice. The
               browser resamples for us.
               *레이트를 요구하며* 받아들이지 않습니다. 하드웨어는 흔히 48000으로 돌고, 48000으로
               돌아온 컨텍스트는 모든 소리를 반음의 십분의 일만큼 높고 조금 빠르게 재생합니다.
               음악에서 들리며, audio.c의 어떤 것도 그것을 알아챌 위치에 있지 않습니다.
               브라우저가 대신 리샘플링합니다. */
            ctx = new Ctor({ sampleRate: rate });
        } catch (e) {
            ctx = new Ctor();
        }

        /* FIELD BY FIELD, AND NOT A LITERAL. An EM_ASM body is macro
           arguments before it is JavaScript, and the C preprocessor balances
           parentheses but not braces or brackets -- so every comma inside an
           object or array literal here splits the call and the error arrives as
           "use of undeclared identifier 'next'", which reads like a JavaScript
           mistake and is not one. Tidying these four lines back into one is the
           obvious edit and it does not compile.
           *리터럴이 아니라 필드 하나씩입니다.* EM_ASM의 본문은 자바스크립트이기 이전에 매크로
           인자이고, C 전처리기는 괄호는 균형 맞추지만 중괄호와 대괄호는 그러지 않습니다. 그래서
           이곳의 객체나 배열 리터럴 안의 쉼표 하나하나가 호출을 갈라 놓으며, 오류는 "use of
           undeclared identifier 'next'"로 도착합니다. 자바스크립트 실수처럼 읽히지만 아닙니다.
           이 네 줄을 한 줄로 정돈하는 것이 떠오르는 편집이고, 그것은 컴파일되지 않습니다. */
        var A = {};
        A.ctx   = ctx;
        A.next  = 0;
        A.timer = 0;
        Module.__sfps_audio = A;

        /* THE GESTURE GATE. A page may not make noise until somebody has
           touched it, so the context starts suspended and these bring it back.
           Registered here rather than left to main_web.c because the rule
           belongs to the device: every host has some condition on opening an
           output, and on Windows it is that a sound card exists.
           Not `once`, because the first gesture can arrive while the tab is
           still hidden and resume() will not take. Cheap enough to keep asking.
           *제스처 게이트입니다.* 페이지는 누군가 건드리기 전에는 소리를 낼 수 없으므로
           컨텍스트는 정지된 채 시작하고 이것들이 그것을 되살립니다. main_web.c에 맡기지 않고
           이곳에 등록하는 이유는 그 규칙이 장치의 것이기 때문입니다. 모든 호스트는 출력을 여는
           데 어떤 조건을 가지며, Windows에서 그것은 사운드 카드가 있다는 것입니다.
           `once`가 아닌 이유는 첫 제스처가 탭이 아직 숨겨진 동안 도착할 수 있고 그때
           resume()이 먹지 않기 때문입니다. 계속 물어볼 만큼 값이 쌉니다. */
        var wake = function () { if (ctx.state !== 'running') ctx.resume(); };
        window.addEventListener('pointerdown', wake, true);
        window.addEventListener('keydown', wake, true);
        window.addEventListener('touchstart', wake, true);

        /* THE PUMP. Tops the schedule up to nbuf chunks ahead every time it
           runs, so a late tick costs nothing as long as it is less than that
           far late -- which is the same bargain waveOut's four buffers make.
           setInterval rather than the frame loop, on purpose: audio that
           stopped whenever the game stopped drawing would cut out on every
           hitch, and a background tab throttles rAF to nothing while it still
           runs timers.
           *펌프입니다.* 돌 때마다 예약을 nbuf 덩어리 앞까지 채우므로, 늦은 틱은 그만큼보다 덜
           늦은 한 아무 비용이 없습니다. waveOut의 버퍼 넷이 맺는 것과 같은 거래입니다.
           프레임 루프가 아니라 setInterval인 것은 의도적입니다. 게임이 그리기를 멈출 때마다
           멈추는 오디오는 모든 끊김에서 소리가 끊기고, 백그라운드 탭은 rAF를 0으로 조이면서도
           타이머는 계속 돌립니다. */
        var ahead = (nbuf * frames) / rate;
        A.timer = setInterval(function () {
            if (ctx.state !== 'running') return;
            var now = ctx.currentTime;
            if (A.next < now) A.next = now;
            while (A.next < now + ahead) {
                _audio_web_fill();
                var ptr = _audio_web_chunk() >> 1;
                var buf = ctx.createBuffer(1, frames, rate);
                var out = buf.getChannelData(0);
                for (var i = 0; i < frames; i++) out[i] = HEAP16[ptr + i] / 32768;
                var src = ctx.createBufferSource();
                src.buffer = buf;
                src.connect(ctx.destination);
                src.start(A.next);
                A.next += frames / rate;
            }
        }, 1000 * frames / rate);

        return 1;
    }, RATE, FRAMES, NBUF);

    /* 0 IS NOT FATAL, and audio.h says so: "failure is not fatal and needs no
       handling -- every other call in this header becomes a harmless no-op, so
       the game runs silently rather than refusing to start." A browser with no
       Web Audio is exactly the machine with no sound card.
       *0은 치명적이지 않으며* audio.h가 그렇게 말합니다. "실패는 치명적이지 않으며 따로 처리할
       필요가 없습니다. 이 헤더의 다른 모든 호출이 무해한 no-op이 되므로, 게임은 시작을 거부하는
       대신 조용히 실행됩니다." Web Audio가 없는 브라우저가 곧 사운드 카드가 없는 기계입니다. */
    g_ready = ok;
    return ok;
}

void audio_shutdown(void) {
    if (!g_ready) return;
    g_ready = 0;

    EM_ASM({
        var A = Module.__sfps_audio;
        if (!A) return;
        if (A.timer) clearInterval(A.timer);
        /* Chunks already scheduled keep their start times and would play on
           after this returns, which on Windows is what waveOutReset prevents.
           close() is this host's waveOutReset.
           이미 예약된 덩어리들은 시작 시각을 지닌 채 이 함수가 반환한 뒤에도 재생될 것이며,
           Windows에서 waveOutReset이 막는 것이 그것입니다. close()가 이 호스트의
           waveOutReset입니다. */
        if (A.ctx && A.ctx.state !== 'closed') A.ctx.close();
        Module.__sfps_audio = null;
    });
}

int audio_dev_lock(void) {
    /* 0, AND THE HEADER'S OWN WORDS COVER IT: "0 if there is no device and the
       caller may proceed unlocked... Do NOT call audio_dev_unlock after a
       return of 0." The second half is the contract the caller depends on and
       it holds exactly. The first half describes WHY the only host that existed
       when it was written would answer 0; this host answers 0 for the other
       reason in the same sentence -- there is nothing to race with. See this
       file's header, and the note added to audio_dev.h.
       0이며, 헤더 자신의 말이 그것을 덮습니다. "장치가 없어 호출자가 락 없이 진행해도 되면 0...
       0이 반환된 뒤에 ::audio_dev_unlock을 호출하지 *마십시오*." 뒤의 절반이 호출자가 의존하는
       계약이고 정확히 성립합니다. 앞의 절반은 그것이 쓰일 때 존재하던 유일한 호스트가 왜 0으로
       답하는지를 서술합니다. 이 호스트는 같은 문장 안의 다른 이유로 0을 답합니다. 경쟁할 상대가
       없다는 것입니다. 이 파일의 머리말과 audio_dev.h에 추가된 주석을 보십시오. */
    return 0;
}

void audio_dev_unlock(void) {
    /* Unreachable by contract: nothing here ever returns 1 from the lock. Left
       defined because audio.c links against it and a host is not free to
       delete half an interface.
       계약상 도달할 수 없습니다. 이곳의 무엇도 락에서 1을 반환하지 않습니다. audio.c가 이것에
       링크하며 호스트가 인터페이스의 절반을 지울 자유는 없으므로 정의는 남겨 둡니다. */
}
