/**
 * @file story.h
 * @brief The three things the game says outside the fight, authored in a file.
 *
 * ENGLISH
 * -------
 * WHAT THIS IS NOT: a dialogue system, a script, or anything with branches. A
 * cutscene here is a short stack of pages, a page is a few lines of text, and
 * the only thing that happens is that the world stops while they are read. The
 * boss already speaks during the fight -- ::BossLine, one slot and a clock --
 * and this is deliberately the same idea with the world held still, because the
 * three moments it covers are the ones with nothing left to play.
 *
 * WHY A FILE. The lines are the part most likely to be rewritten and the part
 * least likely to be worth a rebuild, which is exactly the argument loot.txt is
 * built on: "a rate that needs a rebuild to change is a rate nobody changes."
 * A build that has to be waited for is a line that stays as first drafted.
 *
 * WHY THE MOMENTS ARE AN ENUM AND THE FILE NAMES THEM. ::StoryMoment is what
 * world.c has to say -- "the intro", "it is over" -- and a name is what a
 * person editing the file has to write. Resolving one to the other here means
 * world.c never holds a string and the file never holds an index, and a cut
 * named something this build does not know is discarded rather than refusing
 * the file. That last rule is loot.c's, stated for an unknown monster, and it
 * is the same situation: a story.txt written for a build with a fourth moment
 * has to still play the three this one has.
 *
 * A MOMENT WITH NO CUTSCENE SIMPLY DOES NOT PLAY. ::story_for answers NULL and
 * world.c carries straight on to what the cutscene was covering -- the win
 * screen, the death screen, the first frame of play. That is what makes this
 * module removable: delete assets/story.txt and the game is the game it was
 * before, with no branch anywhere saying so.
 *
 * @note NO `#` IN A LINE, and this is a property of the format rather than of
 *       the parser. Every text asset here is comment-stripped twice -- by
 *       bake.ps1 on the way into the binary and by ::txt_skip on the way out of
 *       a file -- so a `#` inside a quoted line survives in a hot-reload build
 *       and is eaten in the shipped one. Two builds that read the same file
 *       differently is the exact failure ::data_baked exists to prevent, so the
 *       character is simply not available.
 * @note No GL, no window, no ::World. The pages are handed to
 *       ::scene_draw_story one at a time by ::scene_frame, which is where the
 *       fade is worked out; this module only parses.
 *
 * 한국어
 * ------
 * @brief 전투 바깥에서 게임이 하는 세 가지 말. 파일로 제작합니다.
 *
 * *이것이 아닌 것:* 대화 시스템도, 스크립트도, 분기를 가진 무엇도 아닙니다. 이곳의 컷신은
 * 페이지 몇 장의 더미이고, 페이지는 텍스트 몇 줄이며, 일어나는 일은 그것을 읽는 동안 월드가
 * 멈춘다는 것뿐입니다. 보스는 전투 중에 이미 말합니다(::BossLine, 슬롯 하나와 시계 하나).
 * 이것은 의도적으로 같은 발상을 월드를 멈춘 채로 옮긴 것입니다. 이것이 다루는 세 순간은
 * 플레이할 것이 남지 않은 순간들이기 때문입니다.
 *
 * *왜 파일인가.* 대사는 가장 다시 쓰이기 쉬운 부분이자 재빌드를 치를 가치가 가장 적은
 * 부분이며, 그것이 정확히 loot.txt가 기반하는 논거입니다. *"바꾸는 데 재빌드가 필요한 확률은
 * 아무도 바꾸지 않는 확률입니다."* 기다려야 하는 빌드는 초고 그대로 남는 대사입니다.
 *
 * *왜 순간은 열거형이고 파일은 그것을 이름으로 부르는가.* ::StoryMoment는 world.c가 말해야 하는
 * 것("인트로", "끝났다")이고, 이름은 파일을 편집하는 사람이 써야 하는 것입니다. 둘을 이곳에서
 * 변환하면 world.c는 결코 문자열을 들지 않고 파일은 결코 인덱스를 담지 않으며, 이 빌드가 모르는
 * 이름의 컷은 파일을 거부하는 대신 버려집니다. 마지막 규칙은 loot.c의 것이며 알 수 없는
 * 몬스터에 대해 진술되었습니다. 같은 상황입니다. 네 번째 순간을 가진 빌드용으로 쓰인
 * story.txt도 이 빌드가 가진 셋은 그대로 재생해야 합니다.
 *
 * *컷신이 없는 순간은 그냥 재생되지 않습니다.* ::story_for가 NULL로 답하고 world.c는 컷신이
 * 덮고 있던 것(승리 화면, 사망 화면, 플레이의 첫 프레임)으로 곧장 넘어갑니다. 그것이 이 모듈을
 * 제거 가능하게 만듭니다. assets/story.txt를 지우면 게임은 이전의 그 게임이며, 어디에도 그렇게
 * 말하는 분기가 없습니다.
 *
 * @note *대사에 `#`를 쓸 수 없으며*, 이는 파서가 아니라 형식의 성질입니다. 이곳의 모든 텍스트
 *       에셋은 주석이 두 번 제거됩니다. 바이너리로 들어가는 길에 bake.ps1이, 파일에서 나오는
 *       길에 ::txt_skip이 제거합니다. 그래서 따옴표 안의 `#`는 핫 리로드 빌드에서는 살아남고
 *       배포 빌드에서는 먹힙니다. 같은 파일을 다르게 읽는 두 빌드는 ::data_baked가 막으려고
 *       존재하는 바로 그 실패이므로, 그 문자는 그냥 쓸 수 없습니다.
 * @note GL도, 창도, ::World도 없습니다. 페이지는 ::scene_frame이 한 장씩 ::scene_draw_story에
 *       건네며 페이드가 계산되는 곳도 그곳입니다. 이 모듈은 파싱만 합니다.
 */
#ifndef STORY_H
#define STORY_H

/* --- Capacities / 용량 --- */

/**
 * @brief How many lines one page may hold.
 *
 * ENGLISH: Four, because a page is a held moment rather than a paragraph -- the
 * player is looking at a stopped room and waiting to be given it back, and the
 * fifth line is where that stops being a beat and starts being reading. A
 * cutscene that wants more says it in more pages, which is also what gives the
 * reader somewhere to press.
 *
 * 한국어: 넷입니다. 페이지는 문단이 아니라 붙잡아 둔 한 박자이기 때문입니다. 플레이어는 멈춘
 * 방을 바라보며 그것을 돌려받기를 기다리고 있고, 다섯 번째 줄이 그것이 박자이기를 그만두고
 * 독서가 되기 시작하는 지점입니다. 더 말하고 싶은 컷신은 페이지를 더 씁니다. 그것이 읽는
 * 사람에게 누를 곳을 주기도 합니다.
 */
#define STORY_LINES 4

/** @brief How many pages one cutscene may hold. / 컷신 하나가 담을 수 있는 페이지 수. */
#define STORY_PAGES 6

/**
 * @brief Characters in one line, terminator included.
 *
 * ENGLISH: Sized against the narrowest window this UI is drawn in rather than
 * against taste. At ::STORY_TEXT_SIZE a glyph advances ::FONT_CW * size pixels,
 * so 47 characters is 564 of them -- inside a 640-wide viewport with a margin,
 * and centred, so a shorter line simply sits in the middle.
 *
 * 한국어: 취향이 아니라 이 UI가 그려지는 가장 좁은 창을 기준으로 정한 크기입니다.
 * ::STORY_TEXT_SIZE에서 글리프는 ::FONT_CW * size 픽셀만큼 전진하므로, 47자는 564픽셀입니다.
 * 너비 640 뷰포트 안에 여백을 두고 들어가며, 가운데 정렬이므로 짧은 줄은 그냥 가운데 놓입니다.
 */
#define STORY_LINE_MAX 48

/* STORY_NAME_MAX WAS HERE and sized nothing. Cutscene names are not copied:
   ::story_moment_name hands back a pointer into a static table, so there is no
   buffer for sixteen characters to be the length of. A sizing constant with no
   array is a promise about storage that does not exist, and the day somebody
   writes `char name[STORY_NAME_MAX]` against a longer name it becomes a
   truncation nobody chose.
   *STORY_NAME_MAX가 이곳에 있었고* 아무것도 재지 않았습니다. 컷신 이름은 복사되지 않습니다.
   ::story_moment_name은 정적 표 안을 가리키는 포인터를 돌려주므로, 열여섯 글자가 길이가 될
   버퍼가 없습니다. 배열 없는 크기 상수는 존재하지 않는 저장 공간에 대한 약속이며, 누군가 더 긴
   이름에 대해 `char name[STORY_NAME_MAX]`를 쓰는 날 아무도 고르지 않은 잘림이 됩니다. */

/* --- Timing / 시간 --- */

/**
 * @brief Seconds a page holds when the file does not say.
 *
 * ENGLISH: Longer than ::BOSS_LINE_TIME, and it has to be: a banner is read
 * while the player is doing something else, and this is the only thing on the
 * screen. Long enough that a page is never gone before it was noticed, and
 * skippable at any moment for the player who reads faster than that -- which is
 * why erring long costs nothing here and erring short cannot be recovered from.
 *
 * 한국어: ::BOSS_LINE_TIME보다 길며, 그래야만 합니다. 배너는 플레이어가 다른 일을 하는 동안
 * 읽히지만 이것은 화면에 있는 유일한 것입니다. 페이지가 알아채기도 전에 사라지지 않을 만큼
 * 길고, 그보다 빨리 읽는 플레이어를 위해 언제든 넘길 수 있습니다. 그래서 이곳에서는 길게
 * 잡아도 비용이 없고 짧게 잡으면 되돌릴 수 없습니다.
 */
#define STORY_PAGE_TIME 4.5f

/**
 * @brief The shortest a page's `hold` may be, whatever the file says.
 *
 * ENGLISH: `hold 0` in the file would expire the page on the frame it starts,
 * so the whole cutscene would flash past in six frames and read as a rendering
 * fault. The same clamp loot.c puts on `rate`, for the same reason: a countdown
 * reset to zero is a countdown that has already finished.
 *
 * 한국어: 파일의 `hold 0`은 페이지가 시작하는 프레임에 만료된다는 뜻이므로, 컷신 전체가 여섯
 * 프레임 만에 지나가고 렌더링 결함으로 읽힙니다. loot.c가 `rate`에 거는 것과 같은 제한이며
 * 이유도 같습니다. 0으로 되돌려진 카운트다운은 이미 끝난 카운트다운입니다.
 */
#define STORY_PAGE_MIN 0.6f

/* --- Type definitions / 타입 정의 --- */

/**
 * @brief The three moments a cutscene can belong to.
 *
 * ENGLISH
 * -------
 * @note ::STORY_MOMENTS is also the size of the table, because a cut is FILED
 *       under its moment rather than appended to a list. That removes the
 *       capacity this module would otherwise have -- there is no "more
 *       cutscenes than fit", only a name this build does not know -- and it is
 *       what lets ::story_for be an array index instead of a search.
 * @note The values are used by ::RunState as `moment + 1`, so that zero can
 *       mean "no cutscene" and ::run_reset clears the banner by clearing the
 *       struct. The same bargain ::BOSS_LINE_NONE makes.
 *
 * 한국어
 * ------
 * @brief 컷신이 속할 수 있는 세 순간.
 *
 * @note ::STORY_MOMENTS는 표의 크기이기도 합니다. 컷은 목록에 덧붙여지는 것이 아니라 자기
 *       순간 아래에 *편철*되기 때문입니다. 그러면 이 모듈이 그러지 않았다면 가졌을 용량이
 *       사라집니다. "들어가는 것보다 많은 컷신"은 없고 이 빌드가 모르는 이름만 있습니다. 그리고
 *       그것이 ::story_for를 탐색이 아니라 배열 인덱스로 만듭니다.
 * @note 값은 ::RunState에서 `moment + 1`로 쓰입니다. 그래야 0이 "컷신 없음"을 뜻하고
 *       ::run_reset이 구조체를 지우는 것만으로 배너를 지웁니다. ::BOSS_LINE_NONE이 맺는 것과
 *       같은 거래입니다.
 */
typedef enum {
    STORY_INTRO = 0,  /**< Before the first frame of a story run. / 스토리 플레이의 첫 프레임 이전. */
    STORY_VICTORY,    /**< The maw is down and the run is about to be won. / 아귀가 쓰러졌고 플레이가 곧 승리합니다. */
    STORY_DEFEAT,     /**< The player is down, after the collapse has played. / 플레이어가 쓰러졌고 넘어짐이 끝난 뒤입니다. */
    STORY_MOMENTS     /**< How many. / 개수. */
} StoryMoment;

/**
 * @struct StoryPage
 * @brief One screen of a cutscene.
 *
 * 한국어
 * ------
 * @brief 컷신의 한 화면.
 */
typedef struct {
    char  line[STORY_LINES][STORY_LINE_MAX]; /**< The text, one line per line. / 텍스트. 줄마다 하나입니다. */
    int   n_lines;                           /**< How many of them are used. / 그중 몇 개가 쓰였는지. */
    float hold;                              /**< Seconds before it advances by itself. / 스스로 넘어가기까지의 초. */
} StoryPage;

/**
 * @struct StoryCut
 * @brief One moment's pages.
 *
 * 한국어
 * ------
 * @brief 한 순간의 페이지들.
 */
typedef struct {
    StoryPage page[STORY_PAGES];
    int       n_pages;   /**< 0 means the file did not author this moment. / 0이면 파일이 이 순간을 제작하지 않았습니다. */
} StoryCut;

/* --- Public function prototypes / 공개 함수 프로토타입 --- */

/**
 * @brief The cutscene authored for a moment, or NULL.
 *
 * ENGLISH
 * -------
 * @param[in] moment A ::StoryMoment.
 * @return The cut, or NULL when the moment has no pages or `moment` is out of
 *         range. NULL is the ordinary answer for a file that does not mention
 *         a moment, not an error.
 * @note Parses on first use, the way ::loot_drop does and for its reason: a
 *       headless tool that only wants the drop tables should not pay for the
 *       cutscenes either.
 * @warning The pointer is module storage and is invalidated by ::story_reload.
 *          Read it within the frame; nothing needs to hold one longer, because
 *          ::scene_frame asks again every frame.
 *
 * 한국어
 * ------
 * @brief 한 순간에 대해 제작된 컷신. 없으면 NULL.
 * @param[in] moment ::StoryMoment 값.
 * @return 그 컷. 순간에 페이지가 없거나 `moment`가 범위를 벗어나면 NULL입니다. 어떤 순간을
 *         언급하지 않는 파일에 대해서는 NULL이 오류가 아니라 평범한 답입니다.
 * @note ::loot_drop이 그러하듯 첫 사용 시 파싱하며 이유도 같습니다. 드롭 표만 원하는 헤드리스
 *       도구가 컷신의 비용까지 낼 이유는 없습니다.
 * @warning 포인터는 모듈 저장 공간이며 ::story_reload가 무효화합니다. 프레임 안에서 읽으십시오.
 *          더 오래 쥐고 있어야 하는 것은 없습니다. ::scene_frame이 매 프레임 다시 묻습니다.
 */
const StoryCut *story_for(int moment);

/**
 * @brief The name a moment is written under in assets/story.txt.
 * @param[in] moment A ::StoryMoment.
 * @return The name, or "" for a moment out of range.
 *
 * 한국어
 * ------
 * @brief assets/story.txt에서 한 순간이 적히는 이름입니다.
 */
const char *story_moment_name(int moment);

/**
 * @brief Drops the parsed cutscenes so the next question re-reads the file.
 *
 * ENGLISH: ::loot_reload's shape exactly -- a flag rather than a re-parse, so a
 * hot reload costs nothing until something asks. A cutscene playing at the
 * moment of a reload keeps its page index and gets the new text under it, which
 * is what an author editing a line while looking at it wants.
 *
 * 한국어: ::loot_reload와 정확히 같은 형태입니다. 다시 파싱하지 않고 플래그만 두므로, 핫
 * 리로드는 무언가 물을 때까지 아무 비용도 들지 않습니다. 리로드 순간에 재생 중인 컷신은 페이지
 * 인덱스를 유지한 채 그 아래에서 새 텍스트를 받으며, 대사를 보면서 고치는 제작자가 원하는
 * 것이 그것입니다.
 */
void story_reload(void);

#endif /* STORY_H */
