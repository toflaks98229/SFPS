/**
 * @file plat_web.c
 * @brief The browser side of plat.h. The second host, and deliberately the
 *        same four functions and nothing else.
 *
 * ENGLISH
 * -------
 * plat_win32.c's header says what to do to add a host: "write plat_posix.c
 * beside this, implementing the same four functions, and have the build pick
 * one. Nothing else in src/ changes." This is that file, for a host nobody had
 * in mind when the sentence was written, and the sentence held -- not one line
 * of the other 35 translation units moved to make room for it.
 *
 * WHAT A BROWSER MAKES AWKWARD, and it is not what one might guess. The GL is
 * fine, the filesystem is fine, the clock is fine. The awkward one is that
 * persistence is ASYNCHRONOUS: IndexedDB is the only place a page may keep
 * something across reloads, and reading it back is a callback, not a return
 * value. ::plat_save_dir cannot wait for it without stopping the whole page,
 * so it does not try -- see the note there for what main_web.c owes it.
 *
 * 한국어
 * ------
 * @brief plat.h의 브라우저 쪽. 두 번째 호스트이며, 의도적으로 같은 네 함수이고 그 외에는
 *        아무것도 아닙니다.
 *
 * plat_win32.c의 머리말은 호스트를 추가하려면 무엇을 하라고 적어 두었습니다. "이 옆에
 * plat_posix.c를 쓰고 같은 네 함수를 구현한 뒤, 빌드가 하나를 고르게 하십시오. src/의 다른
 * 어떤 것도 바뀌지 않습니다." 이것이 그 파일이며, 그 문장을 쓸 때 아무도 염두에 두지 않았던
 * 호스트를 위한 것이고, 그 문장은 지켜졌습니다. 나머지 35개 번역 단위 중 단 한 줄도 이것에
 * 자리를 내주려고 움직이지 않았습니다.
 *
 * *브라우저가 까다롭게 만드는 것*이며, 짐작할 만한 것이 아닙니다. GL은 괜찮고, 파일 시스템도
 * 괜찮고, 시계도 괜찮습니다. 까다로운 것은 *영속성이 비동기*라는 점입니다. 페이지가 새로고침을
 * 넘어 무언가를 간직할 수 있는 유일한 곳이 IndexedDB이고, 그것을 다시 읽는 것은 반환값이
 * 아니라 콜백입니다. ::plat_save_dir은 페이지 전체를 멈추지 않고는 그것을 기다릴 수 없으므로
 * 시도하지 않습니다. main_web.c가 무엇을 빚지는지는 그곳의 주석을 보십시오.
 */
#include "plat.h"

#include <emscripten.h>
#include <stdlib.h>      /* abort: the only way out of a page that cannot draw */
#include <sys/stat.h>    /* mkdir, stat */

void plat_fatal(const char *title, const char *detail) {
    /* TWO PLACES, BECAUSE THEY HAVE DIFFERENT READERS. The console gets the
       driver's own words, which is what somebody debugging will paste into a
       search. The page gets a sentence, because a visitor who came for a game
       does not have a console open and would otherwise see a canvas that never
       lit up -- the browser's version of "the window disappeared", which is the
       exact failure plat_win32.c says this function exists to prevent.
       NOT alert(). It blocks the event loop, it is suppressed in some contexts,
       and a page that has already failed should not also be a page that has
       seized. Replacing the body is louder and cannot be refused.
       *두 곳이며, 읽는 사람이 다르기 때문입니다.* 콘솔은 드라이버 자신의 말을 받습니다.
       디버깅하는 사람이 검색창에 붙여 넣을 것이 그것입니다. 페이지는 문장을 받습니다. 게임을
       하러 온 방문자는 콘솔을 열어 두지 않았고, 그러지 않으면 끝내 켜지지 않은 캔버스를 보게
       되기 때문입니다. 그것이 "창이 사라졌다"의 브라우저판이며, plat_win32.c가 이 함수의 존재
       이유라고 말하는 바로 그 실패입니다.
       *alert()이 아닙니다.* 이벤트 루프를 막고, 어떤 맥락에서는 억제되며, 이미 실패한 페이지가
       멈추기까지 해서는 안 됩니다. 본문을 갈아 끼우는 쪽이 더 크게 말하며 거부당하지 않습니다. */
    EM_ASM({
        var title  = UTF8ToString($0);
        var detail = UTF8ToString($1);
        console.error(title + ': ' + detail);
        if (typeof document !== 'undefined' && document.body) {
            var box = document.createElement('div');
            box.setAttribute('style',
                'position:fixed;inset:0;z-index:9999;background:#101014;color:#e8e8ec;' +
                'font:14px/1.5 monospace;padding:2rem;white-space:pre-wrap;overflow:auto');
            box.textContent = title + '\n\n' + detail;
            document.body.appendChild(box);
        }
    }, title, detail);

    abort();
}

int plat_exe_dir(char *out, int cap) {
    /* THERE IS NO EXECUTABLE DIRECTORY, and saying "/" is not a dodge around
       that. The wasm arrives over a URL and never lands in the filesystem this
       returns a path into; what the one caller actually wants -- data.c, and
       only in a HOT_RELOAD build -- is the prefix a relative asset path should
       be resolved against, and for this host that is the root of the virtual
       filesystem. "/" is the true answer to the question being asked, even
       though it is not the answer to the question the NAME asks.
       A WEB BUILD DOES NOT HOT RELOAD, so in practice nothing reaches here
       through data.c at all; the live caller is save.c, and only as a fallback
       it will not need. This is written to be correct rather than to be
       reached.
       *실행 파일 디렉토리는 존재하지 않으며*, "/"라고 답하는 것은 그것을 피해 가는 것이
       아닙니다. wasm은 URL로 도착하며 이 함수가 경로를 돌려주는 그 파일 시스템에 놓이는 일이
       없습니다. 유일한 호출자가 실제로 원하는 것은(data.c이고 그것도 HOT_RELOAD 빌드에서만)
       상대 에셋 경로를 기준으로 삼을 접두사이며, 이 호스트에서 그것은 가상 파일 시스템의
       루트입니다. "/"는 던져진 질문에 대한 참된 답입니다. *이름*이 던지는 질문에 대한 답이
       아닐 뿐입니다.
       *웹 빌드는 핫 리로드를 하지 않으므로* 실제로는 data.c를 통해 이곳에 닿는 것이 없습니다.
       살아 있는 호출자는 save.c이며, 그것도 필요로 하지 않을 폴백으로서입니다. 이것은 닿기
       위해서가 아니라 옳기 위해 쓰였습니다. */
    if (cap < 2) { if (cap > 0) out[0] = 0; return 0; }
    out[0] = '/';
    out[1] = 0;
    return 1;
}

int plat_save_dir(char *out, int cap) {
    static const char DIR[] = "/save/";
    static int mounted = 0;

    /* MOUNTED ONCE, and the flag is not an optimisation. FS.mount over a point
       that already carries a mount throws, so a second call would raise where
       the first succeeded -- and this is called from save.c whenever it builds
       a path, not once at start-up.
       한 번만 마운트하며, 이 플래그는 최적화가 아닙니다. 이미 마운트가 걸린 지점에 다시
       FS.mount하면 예외가 나므로, 첫 호출이 성공한 곳에서 두 번째 호출이 던지게 됩니다.
       그리고 이 함수는 시작할 때 한 번이 아니라 save.c가 경로를 만들 때마다 호출됩니다. */
    if (!mounted) {
        /* autoPersist, which is what lets plat.h stay four functions long. A
           write to this directory is flushed to IndexedDB by the filesystem
           itself, so nothing in src/ needs a "and now persist it" call that
           only one host would ever implement -- and save.h's rule that a save
           flushed at exit is a save lost to every crash keeps holding, because
           there is no exit-time flush to lose.
           autoPersist이며, 이것이 plat.h를 네 함수로 남게 하는 것입니다. 이 디렉토리에 대한
           쓰기는 파일 시스템 자신이 IndexedDB로 내보내므로, src/의 어떤 것도 한 호스트만
           구현할 "그리고 이제 영속화하라" 호출을 필요로 하지 않습니다. 그리고 종료 시에
           내보내는 저장은 모든 비정상 종료에 잃는 저장이라는 save.h의 규칙이 계속 성립합니다.
           잃을 종료 시점 flush가 없기 때문입니다.
           THE READ BACK IS main_web.c'S. Mounting gives an EMPTY directory; the
           contents live in IndexedDB until FS.syncfs(true, cb) brings them
           over, and that is a callback. This function returns a path and cannot
           return a promise, so the load has to happen before the first read --
           which means before world_init, which means main_web.c. Skipping it
           does not corrupt anything; it makes every launch look like a first
           launch, which is the failure to look for if unlocks stop sticking.
           *다시 읽어 오는 것은 main_web.c의 몫입니다.* 마운트는 *빈* 디렉토리를 줍니다. 내용은
           FS.syncfs(true, cb)가 옮겨 올 때까지 IndexedDB에 있고, 그것은 콜백입니다. 이 함수는
           경로를 반환하며 프라미스를 반환할 수 없으므로, 그 로드는 첫 읽기 전에 일어나야 하고,
           그것은 world_init 전이며, 그것은 main_web.c입니다. 건너뛰어도 무엇이 깨지지는
           않습니다. 다만 모든 실행이 첫 실행처럼 보이게 되며, 해금이 남지 않는다면 찾아볼
           실패가 그것입니다. */
        int ok = EM_ASM_INT({
            var dir = UTF8ToString($0);
            try {
                FS.mkdir(dir);
            } catch (e) {
                if (!e || e.errno !== 20 /* EEXIST */) { /* keep going anyway */ }
            }
            try {
                FS.mount(IDBFS, { autoPersist: true }, dir);
                return 1;
            } catch (e) {
                console.warn('no persistent save: ' + e);
                return 0;
            }
        }, "/save");

        /* 0 RATHER THAN THE PATH, and plat.h settled this before this file
           existed: "Returns 0 rather than falling back to plat_exe_dir itself.
           The fallback is a policy about saves and belongs to save.c." A page
           in private browsing, or one whose storage is blocked, has no place to
           keep a save -- and handing back a directory that forgets on reload
           would be a save that silently fails to persist, which save.h calls
           worse than one that never claimed to.
           *경로가 아니라 0이며*, plat.h가 이 파일이 있기도 전에 정해 둔 것입니다.
           "::plat_exe_dir로 스스로 되돌아가지 않고 0을 반환합니다. 그 폴백은 저장에 대한
           *정책*이며 save.c의 것입니다." 사생활 보호 모드의 페이지나 저장소가 차단된 페이지는
           저장을 둘 곳이 없습니다. 새로고침하면 잊는 디렉토리를 돌려주는 것은 조용히 남지 않는
           저장이며, save.h는 그것을 애초에 남는다고 말한 적 없는 저장보다 나쁘다고 부릅니다. */
        if (!ok) { if (cap > 0) out[0] = 0; return 0; }
        mounted = 1;
    }

    int n = 0;
    while (DIR[n] && n < cap - 1) { out[n] = DIR[n]; n++; }
    out[n] = 0;

    /* Truncated names a different directory, so it is refused AND emptied --
       plat_win32.c's bargain, kept here so the two hosts fail the same way.
       잘린 것은 다른 디렉토리를 가리키므로 거절하고 *비웁니다*. plat_win32.c의 약속이며, 두
       호스트가 같은 방식으로 실패하도록 이곳에서도 지킵니다. */
    if (DIR[n]) { out[0] = 0; return 0; }
    return n;
}

unsigned long long plat_file_stamp(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;

    /* FOLDED, exactly as plat.h's note says a POSIX host must: "st_mtime alone
       counts in whole seconds and would miss a second save inside the same
       one". The size goes in beside the nanoseconds because a rewrite that
       lands in the same nanosecond and changes length is still a change.
       plat.h의 참고 사항이 POSIX 호스트가 그래야 한다고 말하는 그대로 *접어 넣습니다.*
       "st_mtime만으로는 초 단위로만 세므로 같은 1초 안의 두 번째 저장을 놓칩니다." 크기를
       나노초 옆에 함께 넣는 이유는, 같은 나노초에 떨어지면서 길이가 달라지는 덮어쓰기도 여전히
       변경이기 때문입니다. */
    unsigned long long t = (unsigned long long)st.st_mtime * 1000000000ull
                         + (unsigned long long)st.st_mtim.tv_nsec;
    t ^= (unsigned long long)st.st_size << 1;

    /* Zero is the caller's "no answer", so a file must never stamp as one.
       0은 호출자에게 "답 없음"이므로, 파일이 결코 0으로 찍혀서는 안 됩니다. */
    return t ? t : 1;
}
