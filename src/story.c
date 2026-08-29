/**
 * @file story.c
 * @brief Parses assets\story.txt. No GL, no ::World -- see story.h.
 *
 * ENGLISH
 * -------
 * The section-header grammar every other authored file here uses: `c` opens a
 * cutscene, `p` opens a page, and the keywords after them belong to whatever is
 * open. Somebody who has read loot.txt or effects.txt can read this without
 * being told, which is the whole argument for not inventing a second shape.
 *
 * WHY THE TEXT IS QUOTED, and it is the one departure. Every other file in this
 * project is whitespace-separated tokens, and a line of prose is not one token
 * -- but it cannot be several either, because ::ConvertTo-Minified flattens
 * newlines into spaces on the way into the binary. So a reader that took
 * "tokens until the next keyword" would work on the file and break on the bake,
 * and a line-oriented reader would break on the bake too. Quotes state where
 * the line ends in a way that survives having its newlines taken away.
 *
 * PARSED LAZILY AND ONCE, ::loot_drop's arrangement and its reason.
 *
 * 한국어
 * ------
 * @brief assets\story.txt를 파싱합니다. GL도 ::World도 없습니다. story.h를 참조하십시오.
 *
 * 이곳의 다른 모든 제작 파일이 쓰는 구역 머리글 문법입니다. `c`가 컷신을 열고 `p`가 페이지를
 * 열며, 그 뒤의 키워드는 열려 있는 것에 속합니다. loot.txt나 effects.txt를 읽어 본 사람은
 * 설명 없이 이것을 읽을 수 있으며, 두 번째 형태를 발명하지 않는 논거가 그것입니다.
 *
 * *왜 텍스트에 따옴표를 두는가*, 그리고 이것이 유일한 이탈입니다. 이 프로젝트의 다른 모든
 * 파일은 공백으로 나뉜 토큰이고 산문 한 줄은 토큰 하나가 아닙니다. 그런데 여럿일 수도 없습니다.
 * ::ConvertTo-Minified가 바이너리로 들어가는 길에 개행을 공백으로 평탄화하기 때문입니다. 그래서
 * "다음 키워드까지의 토큰들"로 읽는 판독기는 파일에서는 동작하고 베이크에서는 깨지며, 줄
 * 단위 판독기도 베이크에서 깨집니다. 따옴표는 개행을 빼앗기고도 살아남는 방식으로 줄이 어디서
 * 끝나는지를 진술합니다.
 *
 * 게으르게, 한 번만 파싱합니다. ::loot_drop의 배치이자 그 이유입니다.
 */

#include "story.h"
#include "data.h"
#include "diag.h"
#include "txt.h"

/* --- Static variable definitions / 정적 변수 정의 --- */

/* Indexed by ::StoryMoment, so a cut is filed rather than appended and there is
   no "more cutscenes than fit" for this module to report. See the note on
   ::STORY_MOMENTS.
   ::StoryMoment로 인덱싱하므로 컷은 덧붙여지는 것이 아니라 편철되며, 이 모듈이 보고할 "들어가는
   것보다 많은 컷신"이 존재하지 않습니다. ::STORY_MOMENTS의 참고 사항을 보십시오. */
static StoryCut g_cut[STORY_MOMENTS];

/** @brief Whether ::parse has run since the last ::story_reload. / 마지막 ::story_reload 이후 ::parse가 돌았는지. */
static int      g_parsed;

/* The names the file writes, in ::StoryMoment order. A table rather than a
   chain of comparisons for the reason menu.c's rows are one: adding a moment is
   an enum row and a string, in two places that are next to each other.
   파일이 적는 이름들이며 ::StoryMoment 순서입니다. 비교의 연쇄가 아니라 표인 이유는 menu.c의
   행들이 표인 이유와 같습니다. 순간을 추가하는 일이 서로 붙어 있는 두 곳에서 열거형 한 행과
   문자열 하나가 됩니다. */
static const char *const MOMENTS[STORY_MOMENTS] = {
    "intro", "victory", "defeat"
};

_Static_assert(sizeof(MOMENTS) / sizeof(MOMENTS[0]) == STORY_MOMENTS,
               "one name per StoryMoment");

/* --- Static function prototypes / 정적 함수 프로토타입 --- */

static void        parse(void);
static int         moment_for(const char *name, int len);
static const char *read_quoted(const char *p, char *out, int cap, int *ok);

/* --- Static function definitions / 정적 함수 정의 --- */

/* The moment a `c <name>` names, or -1. An unknown name is DISCARDED rather
   than ending the file -- loot.c's rule for an unknown monster, and the same
   situation.
   `c <이름>`이 지목하는 순간이며, 없으면 -1입니다. 알 수 없는 이름은 파일을 끝내지 않고
   *버려집니다*. 알 수 없는 몬스터에 대한 loot.c의 규칙이며 같은 상황입니다. */
static int moment_for(const char *name, int len) {
    for (int i = 0; i < STORY_MOMENTS; i++)
        if (txt_is(name, len, MOMENTS[i])) return i;
    return -1;
}

/**
 * @brief Reads a `"..."` literal into `out`.
 *
 * ENGLISH
 * -------
 * @param[in]  p   Position to scan from.
 * @param[out] out Destination; always null-terminated.
 * @param[in]  cap Capacity of `out`, terminator included.
 * @param[out] ok  Set to 1 only when a complete literal was read.
 * @return The position after the closing quote, or where the scan stopped.
 *
 * @note ::txt_skip is used to reach the opening quote, so a comment before a
 *       line is skipped the way it is everywhere else -- and a `#` INSIDE the
 *       literal survives here and is eaten by the bake, which is why story.h
 *       says the character is not available rather than leaving it to be
 *       discovered from a build difference.
 * @note An unterminated literal leaves `ok` at 0 and the caller stops. The
 *       alternative -- taking the rest of the file as one line -- turns a
 *       missing quote into a cutscene that draws the whole asset.
 *
 * 한국어
 * ------
 * @brief `"..."` 리터럴을 `out`으로 읽습니다.
 * @param[in]  p   스캔을 시작할 위치.
 * @param[out] out 대상 버퍼. 항상 널로 종료됩니다.
 * @param[in]  cap `out`의 용량. 종료 문자 포함.
 * @param[out] ok  완전한 리터럴을 읽었을 때만 1이 됩니다.
 * @return 닫는 따옴표 다음 위치, 또는 스캔이 멈춘 자리.
 *
 * @note 여는 따옴표에 닿기 위해 ::txt_skip을 씁니다. 그래서 줄 앞의 주석은 다른 모든 곳과
 *       같이 건너뛰어지며, 리터럴 *안의* `#`는 이곳에서 살아남고 베이크에서 먹힙니다. story.h가
 *       그 문자를 쓸 수 없다고 말하는 이유이며, 빌드 차이로 발견되게 두지 않는 이유입니다.
 * @note 닫히지 않은 리터럴은 `ok`를 0으로 남기고 호출자는 멈춥니다. 대안(파일의 나머지를 한
 *       줄로 받아들이는 것)은 빠진 따옴표를 에셋 전체를 그리는 컷신으로 바꿉니다.
 */
static const char *read_quoted(const char *p, char *out, int cap, int *ok) {
    *ok = 0;
    /* Answered before anything is written, so the terminator below is in bounds
       by construction rather than by the one call site happening to pass a
       constant. txt_copy's own bargain.
       무엇을 쓰기 전에 답하므로, 아래의 종료 문자는 유일한 호출 지점이 마침 상수를 넘긴다는
       사실이 아니라 구조적으로 범위 안에 있습니다. txt_copy 자신의 약속입니다. */
    if (cap < 1) return p;
    out[0] = 0;

    p = txt_skip(p);
    if (*p != '"') return p;
    p++;

    int n = 0, over = 0;
    while (*p && *p != '"') {
        if (n < cap - 1) out[n++] = *p;
        else             over = 1;
        p++;
    }
    out[n] = 0;

    if (*p != '"') return p;          /* unterminated: the caller stops */
    p++;

    /* Counted rather than trusted to be noticed. A clipped line still draws,
       and it draws a sentence the author did not write.
       알아채 주기를 기대하지 않고 셉니다. 잘린 줄도 여전히 그려지며, 제작자가 쓰지 않은 문장을
       그립니다. */
    if (over) DIAG(DIAG_STORY_CAP);

    *ok = 1;
    return p;
}

static void parse(void) {
    /* Cleared wholesale, so a reload cannot leave last file's pages standing
       beside this one's. loot.c's reason, and here it would be worse: the two
       would interleave inside a cutscene that is already playing.
       통째로 비웁니다. 리로드가 이전 파일의 페이지를 이번 것 옆에 남겨 둘 수 없게 하기
       위함입니다. loot.c의 이유이며 이곳에서는 더 나쁩니다. 둘이 이미 재생 중인 컷신 안에서
       뒤섞이게 됩니다. */
    StoryCut zero = {0};
    for (int i = 0; i < STORY_MOMENTS; i++) g_cut[i] = zero;

    const char *p = data_text(DATA_STORY);

    /* What the keywords now belong to. Two pointers rather than a tag, loot.c's
       arrangement: `t` writes through `page` and a `t` before any `p` simply
       has nothing to apply to.
       키워드가 지금 무엇에 속하는지입니다. 태그가 아니라 포인터 둘이며 loot.c의 배치입니다.
       `t`는 `page`를 통해 쓰고, 어떤 `p`보다 앞선 `t`는 그저 적용할 대상이 없습니다. */
    StoryCut  *cut  = 0;
    StoryPage *page = 0;

    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;

        if (txt_is(t, len, "c")) {
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;

            int m = moment_for(nm, len);
            cut  = (m >= 0) ? &g_cut[m] : 0;
            page = 0;
            continue;
        }

        if (txt_is(t, len, "p")) {
            page = 0;
            if (!cut) continue;                 /* a page before any `c` */

            if (cut->n_pages >= STORY_PAGES) {
                /* The page is refused and so is everything in it, which is why
                   `page` stays null rather than pointing at the last one: the
                   surplus text must not land on the page before it and make a
                   cut that is short by one read as a cut that is scrambled.
                   페이지가 거절되고 그 안의 모든 것도 거절되므로, `page`는 마지막 페이지를
                   가리키는 대신 널로 남습니다. 초과된 텍스트가 그 앞 페이지에 얹히면, 한 장
                   모자란 컷이 뒤죽박죽인 컷으로 읽힙니다. */
                DIAG(DIAG_STORY_CAP);
                continue;
            }

            page = &cut->page[cut->n_pages++];
            page->hold = STORY_PAGE_TIME;
            continue;
        }

        if (txt_is(t, len, "t")) {
            char text[STORY_LINE_MAX];
            int ok;
            p = read_quoted(p, text, (int)sizeof(text), &ok);
            if (!ok) break;                     /* unterminated: stop here */
            if (!page) continue;                /* a line before any `p` */

            if (page->n_lines >= STORY_LINES) { DIAG(DIAG_STORY_CAP); continue; }
            txt_copy(page->line[page->n_lines], STORY_LINE_MAX, text, -1);
            page->n_lines++;
            continue;
        }

        if (txt_is(t, len, "hold")) {
            int v = 0, ok = 1;
            p = txt_read_int(p, &v, &ok);
            /* Milliseconds in the file, seconds here. Converted at the one
               place the text becomes a number, loot.c's rule -- nothing
               downstream carries a unit it has to remember to divide by.
               파일에서는 밀리초, 이곳에서는 초입니다. 텍스트가 숫자가 되는 그 한 곳에서
               변환하는 loot.c의 규칙입니다. 그 아래의 무엇도 나눠야 한다고 기억해야 할 단위를
               들고 다니지 않습니다. */
            if (ok && page) {
                float h = v * 0.001f;
                page->hold = (h < STORY_PAGE_MIN) ? STORY_PAGE_MIN : h;
            }
            continue;
        }
        /* Anything else: skipped, not refused. loot.c's rule. */
    }

    g_parsed = 1;
}

/* --- Public function definitions / 공개 함수 정의 --- */

const StoryCut *story_for(int moment) {
    if (!g_parsed) parse();
    if (moment < 0 || moment >= STORY_MOMENTS) return 0;
    /* A moment the file never opened has no pages, and NULL is what says so.
       Returning the empty cut would make "nothing was authored" and "a cutscene
       with no pages" the same answer, and only one of those can be drawn.
       파일이 한 번도 열지 않은 순간에는 페이지가 없으며, NULL이 그것을 말합니다. 빈 컷을
       돌려주면 "제작된 것이 없다"와 "페이지가 없는 컷신"이 같은 답이 되는데, 그중 그려질 수
       있는 것은 하나뿐입니다. */
    return g_cut[moment].n_pages > 0 ? &g_cut[moment] : 0;
}

const char *story_moment_name(int moment) {
    if (moment < 0 || moment >= STORY_MOMENTS) return "";
    return MOMENTS[moment];
}

void story_reload(void) { g_parsed = 0; }
