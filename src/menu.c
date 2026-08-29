/**
 * @file menu.c
 * @brief The ESC menu's state and navigation. No GL, no window, no drawing.
 *
 * ENGLISH
 * -------
 * The rows of both screens are described by one table each, and every routine
 * here walks those tables rather than switching on a row index. That is what
 * makes adding a setting a table row instead of an edit in four places -- the
 * same argument enemy.c's MonType table is built on.
 *
 * 한국어
 * ------
 * 두 화면의 행은 각각 하나의 테이블로 기술되며, 이곳의 모든 루틴은 행 인덱스로 분기하지
 * 않고 그 테이블을 순회합니다. 그것이 설정 추가를 네 곳의 수정이 아니라 테이블 한 행으로
 * 만듭니다. enemy.c의 MonType 테이블이 기반하는 것과 동일한 논거입니다.
 */

#include "menu.h"

/* --- Row description / 행 기술 --- */

/**
 * @brief What activating or adjusting a row does.
 *
 * ENGLISH
 * -------
 * A row either runs an action or cycles a value; nothing does both. Keeping
 * that as a kind rather than as two nullable fields means an inconsistent row
 * -- one that both quits and toggles a setting -- cannot be written.
 *
 * 한국어
 * ------
 * 행은 동작을 실행하거나 값을 순환시키며, 둘 다 하는 것은 없습니다. 이를 널 허용 필드
 * 두 개가 아니라 종류로 두면, 종료도 하고 설정도 바꾸는 모순된 행을 아예 작성할 수 없게
 * 됩니다.
 */
typedef enum {
    ROW_ACTION = 0,  /**< Runs a ::MenuAction. / ::MenuAction을 실행합니다. */
    ROW_SCREEN,      /**< Switches to another ::MenuScreen. / 다른 화면으로 전환합니다. */
    ROW_VALUE,       /**< Cycles an int in ::MenuSettings. / 설정의 정수 값을 순환시킵니다. */

    /**
     * @brief A ROW_VALUE drawn as a bar rather than as a word.
     *
     * ENGLISH
     * -------
     * THE DIFFERENCE IS ENTIRELY PRESENTATIONAL. Input treats this exactly as
     * ROW_VALUE -- the same cycling, the same wrap, the same right-click
     * reverse -- because a volume IS an int in MenuSettings and nothing about
     * moving it changes. What changes is that "70" is a number the player has
     * to read and compare against the two rows above it, where a bar is a
     * quantity they can see at a glance.
     *
     * Kept a KIND rather than a flag on ::Row so the drawing side has one
     * question to ask and menu.c has one place to answer it. ::menu_row_slider
     * is that answer; ::RowKind itself stays private, because scene.c has no
     * business knowing how this module files its rows.
     *
     * 한국어
     * ------
     * @brief 단어가 아니라 막대로 그려지는 ROW_VALUE입니다.
     *
     * *차이는 전적으로 표현에 있습니다.* 입력은 이것을 ROW_VALUE와 정확히 같이 다룹니다. 같은
     * 순환, 같은 되돌기, 같은 우클릭 역방향입니다. 음량은 MenuSettings의 정수이고 그것을 움직이는
     * 일에 달라지는 것이 없기 때문입니다. 달라지는 것은, "70"은 플레이어가 읽어서 위의 두 행과
     * 비교해야 하는 숫자인 반면 막대는 한눈에 보이는 양이라는 점입니다.
     *
     * ::Row의 플래그가 아니라 *종류*로 둔 것은 그리기 쪽이 물어볼 질문을 하나로, menu.c가 답할
     * 곳을 하나로 하기 위함입니다. ::menu_row_slider가 그 답이며, ::RowKind 자체는 사설로
     * 남습니다. scene.c가 이 모듈이 행을 어떻게 분류하는지 알아야 할 이유가 없습니다.
     */
    ROW_SLIDER,

    /**
     * @brief Returns to whichever screen is home.
     *
     * ENGLISH
     * -------
     * A KIND RATHER THAN A ROW_SCREEN WITH A SENTINEL, because "go back" is not
     * "go to ::MENU_ROOT" any more: before a run, home is ::MENU_TITLE. Writing
     * it as a screen id would mean a value in `arg` that is not a screen, and
     * the next reader would have to know that MENU_ROOT there sometimes means
     * something else -- the exact shape of a table that lies about itself.
     *
     * 한국어
     * ------
     * @brief 집인 화면으로 돌아갑니다.
     *
     * *특별한 값을 가진 ROW_SCREEN이 아니라 종류인 이유*는, "뒤로"가 더 이상 "::MENU_ROOT으로"가
     * 아니기 때문입니다. 플레이 이전에 집은 ::MENU_TITLE입니다. 화면 id로 적으면 `arg`에 화면이
     * 아닌 값이 들어가고, 다음에 읽는 사람은 그곳의 MENU_ROOT이 때때로 다른 것을 뜻한다는 사실을
     * 알아야 합니다. 자기 자신에 대해 거짓말하는 표의 정확한 형태입니다.
     */
    ROW_BACK
} RowKind;

/**
 * @struct Row
 * @brief One line of a menu screen.
 *
 * ENGLISH
 * -------
 * @note `field` is a BYTE OFFSET into ::MenuSettings rather than a pointer,
 *       so the table can stay `const` and land in .rdata. A pointer into
 *       module storage would need a runtime initialiser, which puts the whole
 *       table in .data and costs its full size on disk -- the trap README.md's
 *       first size rule describes.
 *
 * 한국어
 * ------
 * 메뉴 화면의 한 줄입니다.
 * @note `field`는 포인터가 아니라 ::MenuSettings 안의 *바이트 오프셋*입니다. 덕분에
 *       테이블이 `const`로 남아 .rdata에 놓입니다. 모듈 저장 공간을 가리키는 포인터는
 *       런타임 초기화가 필요하므로 테이블 전체가 .data로 가고 디스크에서 제 크기를 전부
 *       차지하게 됩니다. README.md의 첫 번째 크기 규칙이 설명하는 함정입니다.
 */
typedef struct {
    const char *label;
    RowKind     kind;
    int         arg;        /**< A MenuAction, a MenuScreen, or unused. / MenuAction, MenuScreen, 또는 미사용. */
    int         field;      /**< Byte offset into MenuSettings, for ROW_VALUE. / ROW_VALUE용 MenuSettings 내 바이트 오프셋. */
    int         values;     /**< How many values the field cycles through. / 필드가 순환하는 값의 개수. */
    const char *const *names; /**< One label per value. / 값마다 하나의 레이블. */

    /**
     * @brief ::MenuUnlock bits this row needs before it may be chosen. 0 = none.
     *
     * ENGLISH: A COLUMN RATHER THAN A PREDICATE, so the second locked row is an
     * edit to one line of one table -- the argument the whole of this file is
     * built on. Zero is "always available", which every existing row gets from
     * the initialiser without being edited.
     *
     * 한국어: *술어가 아니라 열*이므로, 두 번째로 잠기는 행은 한 표의 한 줄에 대한 수정입니다.
     * 이 파일 전체가 기반하는 논거입니다. 0은 "언제나 가능"이며, 기존의 모든 행이 수정 없이
     * 초기화자로부터 그 값을 받습니다.
     */
    unsigned    needs;
} Row;

/* offsetof, without pulling in <stddef.h> for one macro. The cast goes through
   char* and a pointer difference rather than through an integer type, because
   `long` is 32-bit on this target while a pointer is 64 -- casting a pointer to
   long truncates it, which the compiler rightly warns about even though these
   offsets are all tiny.
   <stddef.h>를 매크로 하나 때문에 끌어오지 않는 offsetof입니다. 정수 타입이 아니라
   char* 포인터 차이를 거치는데, 이 타깃에서 `long`은 32비트인 반면 포인터는
   64비트이기 때문입니다. 포인터를 long으로 캐스트하면 잘리며, 이 오프셋들이 전부 작은
   값이더라도 컴파일러가 경고하는 것이 옳습니다. */
#define FIELD(f) ((int)((char *)&(((MenuSettings *)0)->f) - (char *)0))

/* --- Value labels / 값 레이블 --- */

static const char *const OFF_ON[]  = { "OFF", "ON" };
static const char *const DISPLAYS[] = { "WINDOWED", "BORDERLESS" };
static const char *const PIXELS[]   = { "CHUNKY", "NORMAL", "FINE" };
static const char *const DITHERS[]  = { "HEAVY", "NORMAL", "LIGHT", "OFF" };
static const char *const PATTERNS[] = { "BAYER", "NOISE" };
/* One label per notch. Spelled out rather than formatted at draw time: this
   project has no snprintf and menu_row_text returns a `const char *` that has to
   outlive the call, so a table is both the cheaper and the only easy answer.
   눈금마다 라벨 하나입니다. 그릴 때 포매팅하지 않고 적어 둡니다. 이 프로젝트에는 snprintf가
   없고 menu_row_text는 호출보다 오래 살아야 하는 `const char *`를 반환하므로, 표가 더 싼
   답이자 유일하게 쉬운 답입니다. */
static const char *const VOLUMES[] = { "OFF", "10", "20", "30", "40", "50",
                                       "60", "70", "80", "90", "100" };

/* Each name table must have exactly one entry per value the enum can take. A
   short table would index out of bounds the first time the player cycled onto
   the missing value -- and the value that has no name is precisely the one
   nobody tested.
   각 이름 테이블은 열거형이 가질 수 있는 값마다 정확히 하나의 항목을 가져야 합니다.
   테이블이 짧으면 플레이어가 누락된 값으로 순환하는 순간 범위를 벗어나 인덱싱하게 되며,
   이름이 없는 그 값이야말로 아무도 테스트하지 않은 값입니다. */
_Static_assert(sizeof(DISPLAYS) / sizeof(DISPLAYS[0]) == DISPLAY_MODE_COUNT,
               "one name per DisplayMode");
_Static_assert(sizeof(PIXELS) / sizeof(PIXELS[0]) == GFX_PIXEL_COUNT,
               "one name per GfxPixelPreset");
_Static_assert(sizeof(DITHERS) / sizeof(DITHERS[0]) == GFX_DITHER_COUNT,
               "one name per GfxDither");
_Static_assert(sizeof(PATTERNS) / sizeof(PATTERNS[0]) == GFX_PATTERN_COUNT,
               "one name per GfxDitherPattern");

/* --- Screens / 화면 --- */

static const Row ROOT_ROWS[] = {
    { "RESUME",    ROW_SCREEN, MENU_CLOSED,      0, 0, 0, 0 },
    { "SETTINGS",  ROW_SCREEN, MENU_SETTINGS,    0, 0, 0, 0 },
    { "RESTART",   ROW_ACTION, MENU_ACT_RESTART, 0, 0, 0, 0 },
    { "CREDITS",   ROW_SCREEN, MENU_CREDITS,     0, 0, 0, 0 },
    { "QUIT",      ROW_ACTION, MENU_ACT_QUIT,    0, 0, 0, 0 },
};

/* The front screen. No RESUME and no RESTART: there is nothing to resume, and
   "start again" is what both of the top two rows already are.
   ENDLESS SITS SECOND rather than last, in the place it will occupy once it is
   unlocked. A row that moved when it became available would make the unlock
   read as the menu being rebuilt, and the player who has been looking at it for
   an hour would have to find it again.
   앞 화면입니다. RESUME도 RESTART도 없습니다. 재개할 것이 없고, "다시 시작"은 위의 두 행이
   이미 그것이기 때문입니다.
   *ENDLESS는 마지막이 아니라 둘째에 놓입니다.* 해금된 뒤에 차지할 바로 그 자리입니다. 사용
   가능해질 때 자리를 옮기는 행은 해금을 메뉴가 다시 만들어진 것으로 읽히게 하며, 한 시간 동안
   그것을 바라보고 있던 플레이어는 다시 찾아야 합니다. */
static const Row TITLE_ROWS[] = {
    { "STORY",     ROW_ACTION, MENU_ACT_STORY,   0, 0, 0, 0 },
    { "ENDLESS",   ROW_ACTION, MENU_ACT_ENDLESS, 0, 0, 0, MENU_UNLOCK_ENDLESS },
    { "SETTINGS",  ROW_SCREEN, MENU_SETTINGS,    0, 0, 0, 0 },
    { "CREDITS",   ROW_SCREEN, MENU_CREDITS,     0, 0, 0, 0 },
    { "QUIT",      ROW_ACTION, MENU_ACT_QUIT,    0, 0, 0, 0 },
};

/* The credits screen has one row, and it is the way back. The notices
   themselves are text scene.c draws, not rows: they are paragraphs to read
   rather than things to choose between, and a row per line would make the
   highlight walk down a licence.
   크레딧 화면의 행은 하나이며 그것은 돌아가는 길입니다. 고지 자체는 행이 아니라 scene.c가
   그리는 텍스트입니다. 고르는 대상이 아니라 읽는 문단이며, 줄마다 행을 두면 강조 표시가
   라이선스를 따라 내려가게 됩니다. */
static const Row CREDITS_ROWS[] = {
    { "BACK",      ROW_BACK,   0,                0, 0, 0, 0 },
};

static const Row SETTINGS_ROWS[] = {
    { "DISPLAY",     ROW_VALUE, 0, FIELD(display),   DISPLAY_MODE_COUNT, DISPLAYS, 0 },
    { "PIXEL SIZE",  ROW_VALUE, 0, FIELD(pixel),     GFX_PIXEL_COUNT,    PIXELS  , 0 },
    { "POST FX",     ROW_VALUE, 0, FIELD(post_on),   2,                  OFF_ON  , 0 },
    { "SCANLINES",   ROW_VALUE, 0, FIELD(scanlines), 2,                  OFF_ON  , 0 },
    { "DITHER",      ROW_VALUE, 0, FIELD(dither),    GFX_DITHER_COUNT,   DITHERS , 0 },
    { "PATTERN",     ROW_VALUE, 0, FIELD(pattern),   GFX_PATTERN_COUNT,  PATTERNS, 0 },
    { "MASTER VOL",  ROW_SLIDER,0, FIELD(master),    MENU_VOL_STEPS,     VOLUMES , 0 },
    { "SFX VOL",     ROW_SLIDER,0, FIELD(sfx),       MENU_VOL_STEPS,     VOLUMES , 0 },
    { "BGM VOL",     ROW_SLIDER,0, FIELD(music),     MENU_VOL_STEPS,     VOLUMES , 0 },
    { "BACK",        ROW_BACK,  0, 0,               0,                  0,        0 },
};

/* --- Module state / 모듈 상태 --- */

static MenuScreen   g_screen;
static int          g_cursor;
static MenuAction   g_pending;

/* The screen ESC and every BACK row return to. ::MENU_ROOT during a run and
   ::MENU_TITLE before one; see menu.h's note on ::menu_open_title for why this
   is told rather than derived.
   ZERO WOULD BE ::MENU_CLOSED, which is why ::menu_init assigns it explicitly
   rather than relying on the .bss the rest of this module does: "home is
   closed" is a state where ESC opens nothing and the menu cannot be reached.
   ESC와 모든 BACK 행이 돌아가는 화면입니다. 플레이 중에는 ::MENU_ROOT, 플레이 이전에는
   ::MENU_TITLE입니다. 왜 유도하지 않고 전달받는지는 menu.h의 ::menu_open_title 참고 사항을
   보십시오.
   *0은 ::MENU_CLOSED가 될 것이므로*, ::menu_init이 이 모듈의 나머지처럼 .bss에 기대지 않고
   명시적으로 대입합니다. "집이 닫힘"은 ESC가 아무것도 열지 않고 메뉴에 도달할 수 없는
   상태입니다. */
static MenuScreen   g_home = MENU_ROOT;

/** @brief What ::menu_set_unlocked was last told. / ::menu_set_unlocked가 마지막으로 들은 것. */
static unsigned     g_unlocked;
static MenuSettings g_set = {
    /* Named rather than positional past the first few: a struct that gains a
       field silently gives it 0, and 0 here is GFX_DITHER_HEAVY -- the exact
       setting this list exists to stop being the default.
       처음 몇 개를 넘어서는 위치가 아니라 이름으로 씁니다. 필드가 늘어난 구조체는 그것에
       조용히 0을 주는데, 여기서 0은 GFX_DITHER_HEAVY이며 이 목록이 기본값이 되지 않게
       하려는 바로 그 설정입니다. */
    .display   = DISPLAY_WINDOWED,
    .pixel     = GFX_PIXEL_NORMAL,
    .post_on   = 1,
    .scanlines = 1,
    .dither    = GFX_DITHER_NORMAL,
    .pattern   = GFX_PATTERN_BAYER,
    /* Full, both of them, so a player who never opens this menu hears exactly
       what the game sounded like before the rows existed.
       둘 다 최대입니다. 이 메뉴를 한 번도 열지 않는 플레이어는 이 행들이 생기기 전의 게임과
       정확히 같은 소리를 듣습니다. */
    .master    = MENU_VOL_STEPS - 1,
    .sfx       = MENU_VOL_STEPS - 1,
    .music     = MENU_VOL_STEPS - 3,   /* under the effects: it is a background */
};

/* --- Static helpers / 정적 헬퍼 --- */

/* The row table for a screen, and its length. Both come from one place so a
   count cannot disagree with the table it counts.
   화면의 행 테이블과 그 길이입니다. 둘 다 한 곳에서 나오므로 개수가 그것이 세는 테이블과
   어긋날 수 없습니다. */
static const Row *rows_of(MenuScreen s, int *count) {
    if (s == MENU_ROOT) {
        *count = (int)(sizeof(ROOT_ROWS) / sizeof(ROOT_ROWS[0]));
        return ROOT_ROWS;
    }
    if (s == MENU_SETTINGS) {
        *count = (int)(sizeof(SETTINGS_ROWS) / sizeof(SETTINGS_ROWS[0]));
        return SETTINGS_ROWS;
    }
    if (s == MENU_CREDITS) {
        *count = (int)(sizeof(CREDITS_ROWS) / sizeof(CREDITS_ROWS[0]));
        return CREDITS_ROWS;
    }
    if (s == MENU_TITLE) {
        *count = (int)(sizeof(TITLE_ROWS) / sizeof(TITLE_ROWS[0]));
        return TITLE_ROWS;
    }
    *count = 0;
    return 0;
}

/* The row at `row` on the current screen, or NULL. Four functions below were
   each opening with the same three lines and one of them tested the bounds the
   other way round; this is that opening, written once.
   현재 화면의 `row` 행이며, 없으면 NULL입니다. 아래의 네 함수가 각각 같은 세 줄로 시작하고
   있었고 그중 하나는 경계를 반대로 검사했습니다. 이것이 그 시작 부분을 한 번 적은 것입니다. */
static const Row *row_of(int row) {
    int n;
    const Row *rs = rows_of(g_screen, &n);
    if (!rs || row < 0 || row >= n) return 0;
    return &rs[row];
}

/* Whether a row's requirement is met. The one place that compares, so a locked
   row cannot be drawn dim by one rule and activated by another.
   행의 요구 조건이 충족되었는지입니다. 비교하는 곳이 하나이므로, 잠긴 행이 한 규칙으로 흐리게
   그려지고 다른 규칙으로 실행되는 일이 없습니다. */
static int locked(const Row *r) {
    return r && (r->needs & ~g_unlocked) != 0;
}

/* A slider is a value row that draws differently, so everything about INPUT
   has to treat the two alike. One predicate rather than three copies of the
   same `||`, because three copies is how the next kind gets added to two of
   them.
   슬라이더는 다르게 그려지는 값 행이므로 *입력*에 관한 모든 것이 둘을 똑같이 다뤄야 합니다.
   같은 `||`의 사본 셋이 아니라 술어 하나인 이유는, 사본이 셋이면 다음 종류가 그중 둘에만
   추가되기 때문입니다. */
static int is_value(const Row *r) {
    return r->kind == ROW_VALUE || r->kind == ROW_SLIDER;
}

static int *field_of(const Row *r) {
    return (int *)((char *)&g_set + r->field);
}

/* --- Lifecycle / 수명 주기 --- */

void menu_init(const MenuSettings *s) {
    if (s) g_set = *s;
    g_screen  = MENU_CLOSED;
    g_cursor  = 0;
    g_pending = MENU_ACT_NONE;
    /* Home is the pause root unless somebody says otherwise. That is what a
       restart wants and what every existing caller meant before there was
       anything else to mean. The unlocks are NOT cleared here: they describe
       the player rather than this menu, and re-initialising the menu is not an
       event that could take one away.
       달리 말하는 사람이 없으면 집은 일시정지 최상위입니다. 재시작이 원하는 것이고, 다른 것을
       뜻할 여지가 생기기 전까지 기존의 모든 호출자가 뜻하던 것입니다. 해금은 이곳에서 지우지
       *않습니다*. 그것은 이 메뉴가 아니라 플레이어를 기술하며, 메뉴를 다시 초기화하는 것은
       해금을 빼앗을 수 있는 사건이 아닙니다. */
    g_home    = MENU_ROOT;
}

void menu_open_title(void) {
    g_home   = MENU_TITLE;
    g_screen = MENU_TITLE;
    g_cursor = 0;
}

/* --- State / 상태 --- */

MenuScreen menu_screen(void)          { return g_screen; }
int        menu_is_open(void)         { return g_screen != MENU_CLOSED; }
const MenuSettings *menu_settings(void) { return &g_set; }
int        menu_cursor(void)          { return g_cursor; }

int menu_row_count(void) {
    int n;
    rows_of(g_screen, &n);
    return n;
}

int menu_row_locked(int row) { return locked(row_of(row)); }

void menu_set_unlocked(unsigned bits) { g_unlocked = bits; }
unsigned menu_unlocked(void)          { return g_unlocked; }

int menu_row_slider(int row, float *fill) {
    int n;
    const Row *rs = rows_of(g_screen, &n);
    if (fill) *fill = 0.0f;
    if (!rs || row < 0 || row >= n) return 0;

    const Row *r = &rs[row];
    if (r->kind != ROW_SLIDER || r->values < 2) return 0;

    /* Clamped on read, the same way menu_row_text clamps: a settings struct
       handed in by the caller is not this module's to trust.
       menu_row_text가 제한하는 것과 같은 방식으로 읽기 시에 제한합니다. 호출자가 넘긴 설정
       구조체는 이 모듈이 신뢰할 수 있는 것이 아닙니다. */
    int v = *field_of(r);
    if (v < 0) v = 0;
    if (v >= r->values) v = r->values - 1;

    /* Over values-1 rather than values, so the top notch fills the bar
       completely. Dividing by the count would leave a sliver unfilled at
       maximum, which reads as "not quite all the way" on a control whose whole
       job is to say how far along it is.
       values가 아니라 values-1로 나눕니다. 그래야 최상단 눈금이 막대를 완전히 채웁니다.
       개수로 나누면 최대에서 한 조각이 비어 남는데, 얼마나 진행되었는지를 말하는 것이 일의
       전부인 컨트롤에서 그것은 "아직 끝까지는 아니다"로 읽힙니다. */
    if (fill) *fill = (float)v / (float)(r->values - 1);
    return 1;
}

const char *menu_row_text(int row, const char **value) {
    int n;
    const Row *rs = rows_of(g_screen, &n);

    /* An out-of-range row yields empty strings rather than reading past the
       table. A drawing loop that runs one row long is a cosmetic bug; the same
       loop reading a stray pointer is a crash.
       범위를 벗어난 행은 테이블 밖을 읽는 대신 빈 문자열을 내놓습니다. 한 행을 더 도는
       그리기 루프는 외관상의 버그이지만, 같은 루프가 엉뚱한 포인터를 읽으면 충돌입니다. */
    if (!rs || row < 0 || row >= n) {
        if (value) *value = "";
        return "";
    }

    const Row *r = &rs[row];
    if (value) {
        if (is_value(r)) {
            int v = *field_of(r);
            /* Clamped on READ as well as on write. A settings struct handed in
               by the caller is not this module's to trust, and a value out of
               range would index the name table out of bounds.
               쓰기뿐 아니라 *읽기* 시에도 제한합니다. 호출자가 넘긴 설정 구조체는 이
               모듈이 신뢰할 수 있는 것이 아니며, 범위를 벗어난 값은 이름 테이블을
               범위 밖으로 인덱싱하게 됩니다. */
            if (v < 0 || v >= r->values) v = 0;
            *value = r->names[v];
        } else {
            *value = "";
        }
    }
    return r->label;
}

/* --- Layout / 배치 --- */

/* The rows are laid out as one centred block. These are the only numbers that
   decide where a row is, and both the drawing and the hit test read them from
   here -- see menu_row_bounds in menu.h for why a second copy would be worse
   than merely untidy.
   행은 중앙에 놓인 하나의 묶음으로 배치됩니다. 행의 위치를 결정하는 숫자는 이것뿐이며,
   그리기와 히트 판정 양쪽이 이곳에서 읽어 갑니다. 사본이 두 개면 왜 단순히 지저분한
   것보다 나쁜지는 menu.h의 menu_row_bounds를 참조하십시오. */
#define ROW_STEP    38.0f   /* pixels between row baselines */
#define ROW_HEIGHT  30.0f   /* the clickable height of one row */
#define ROW_LEFT  (-176.0f) /* from the centre: the marker column */
#define ROW_RIGHT  (176.0f) /* from the centre: past the widest value */
#define TITLE_GAP   70.0f   /* above the first row */
#define HINT_GAP    34.0f   /* below the last row */

/* The front screen's header is the game's name at title size with a line under
   it, not one word at menu size, so it needs room the pause menu does not.
   HERE RATHER THAN IN scene.c for menu_row_bounds' own reason: this is where a
   row IS, and the drawing reads it. A title drawn from its own constant would
   be a second layout, and the first thing two layouts disagree about is
   whether the header overlaps the first row.
   앞 화면의 머리글은 메뉴 크기의 단어 하나가 아니라 타이틀 크기의 게임 이름과 그 아래 한
   줄이므로, 일시정지 메뉴가 필요로 하지 않는 공간을 필요로 합니다.
   scene.c가 아니라 이곳인 이유는 menu_row_bounds 자신의 이유와 같습니다. 행이 *어디에 있는지*를
   정하는 곳이 이곳이고 그리기가 그것을 읽습니다. 자기 상수로 그려지는 제목은 두 번째 배치가
   되며, 두 배치가 가장 먼저 어긋나는 것은 머리글이 첫 행과 겹치는가입니다. */
#define TITLE_GAP_FRONT 150.0f

/* The slider bar, in the same block as the rows above and for the same reason:
   it is a HIT TARGET, and a hit target whose extent is known only to the
   drawing side cannot be dragged. That was the bug -- the bar was drawn from
   constants in scene.c, so menu.c had nothing to test a click against and a
   slider could only be cycled like the value row it used to be.
   BAR_X names the same column scene.c puts value TEXT in. They are deliberately
   equal and deliberately separate: the words are a layout question and belong
   to the drawing, the bar is a target and belongs here.
   슬라이더 막대이며, 위의 행들과 같은 블록에 같은 이유로 놓입니다. 이것은 *히트 대상*이고,
   그리기 쪽만 범위를 아는 히트 대상은 드래그할 수 없습니다. 그것이 결함이었습니다. 막대가
   scene.c의 상수로 그려졌으므로 menu.c에는 클릭을 판정할 대상이 없었고, 슬라이더는 예전의 값
   행처럼 순환시키는 것밖에 할 수 없었습니다.
   BAR_X는 scene.c가 값 *텍스트*를 놓는 것과 같은 열을 가리킵니다. 의도적으로 같은 값이고
   의도적으로 별개입니다. 단어는 배치의 문제라 그리기의 것이고, 막대는 대상이라 이곳의
   것입니다. */
#define BAR_X       40.0f   /* from the centre: where the bar starts */
#define BAR_W       92.0f   /* the track's full width */
#define BAR_H        6.0f   /* the track's height */

static float block_top(int vh) {
    int rows = menu_row_count();
    return vh * 0.5f - (rows * ROW_STEP) * 0.5f;
}

int menu_row_bounds(int row, int vw, int vh,
                    float *x0, float *y0, float *x1, float *y1) {
    int n = menu_row_count();
    if (row < 0 || row >= n) return 0;

    float cx = vw * 0.5f;
    float y  = block_top(vh) + row * ROW_STEP;

    /* The box is centred on the text's own baseline band rather than starting
       at it, so the gap between two rows belongs to neither and a click
       between them selects nothing instead of the wrong one.
       상자는 텍스트의 기준선 대역에서 시작하지 않고 그것을 중심에 둡니다. 그래야 두 행
       사이의 여백이 어느 쪽에도 속하지 않아, 그 사이를 클릭하면 엉뚱한 행이 아니라
       아무것도 선택되지 않습니다. */
    if (x0) *x0 = cx + ROW_LEFT;
    if (x1) *x1 = cx + ROW_RIGHT;
    if (y0) *y0 = y - 4.0f;
    if (y1) *y1 = y - 4.0f + ROW_HEIGHT;
    return 1;
}

float menu_title_y(int vw, int vh) {
    (void)vw;
    return block_top(vh)
         - (g_screen == MENU_TITLE ? TITLE_GAP_FRONT : TITLE_GAP);
}

float menu_hint_y(int vw, int vh) {
    (void)vw;
    return block_top(vh) + menu_row_count() * ROW_STEP + HINT_GAP;
}

int menu_row_bar_bounds(int row, int vw, int vh,
                        float *x0, float *y0, float *x1, float *y1) {
    if (!menu_row_slider(row, 0)) return 0;

    float rx0, ry0, rx1, ry1;
    if (!menu_row_bounds(row, vw, vh, &rx0, &ry0, &rx1, &ry1)) return 0;

    /* Centred in the row's own box, so the bar moves with the row it belongs
       to and nothing here repeats ROW_STEP.
       행 자신의 상자 안에 세로 중앙 정렬하므로, 막대가 자기 행을 따라 움직이고 이곳의
       무엇도 ROW_STEP을 되풀이하지 않습니다. */
    float cx = vw * 0.5f;
    float cy = (ry0 + ry1) * 0.5f;
    if (x0) *x0 = cx + BAR_X;
    if (x1) *x1 = cx + BAR_X + BAR_W;
    if (y0) *y0 = cy - BAR_H * 0.5f;
    if (y1) *y1 = cy + BAR_H * 0.5f;
    return 1;
}

/* Sets a slider from a mouse x, and this is what a bar is FOR: the notch you
   point at is the notch you get. Rounding rather than truncating so the two
   ends are reachable and each notch owns the half-step either side of it --
   truncation would make the top notch need a click past the end of the bar.
   Clamped rather than ignored outside, so a drag that runs off the end pins to
   the end instead of stopping wherever it left.
   마우스 x로 슬라이더를 설정하며, 막대가 존재하는 *이유*가 이것입니다. 가리킨 눈금이 얻는
   눈금입니다. 잘라내지 않고 반올림하므로 양 끝에 도달할 수 있고 각 눈금이 양옆 반 칸을
   가집니다. 잘라내면 최상단 눈금은 막대 끝 너머를 클릭해야 합니다. 바깥에서 무시하지 않고
   제한하므로, 끝을 지나친 드래그는 벗어난 자리에 멈추지 않고 끝에 붙습니다. */
static void slider_set_from_x(int row, float mx, int vw, int vh) {
    float x0, x1;
    if (!menu_row_bar_bounds(row, vw, vh, &x0, 0, &x1, 0)) return;

    int n;
    const Row *rs = rows_of(g_screen, &n);
    if (!rs || row < 0 || row >= n) return;
    const Row *r = &rs[row];
    if (r->values < 2 || x1 <= x0) return;

    float t = (mx - x0) / (x1 - x0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    int v = (int)(t * (float)(r->values - 1) + 0.5f);
    if (v < 0) v = 0;
    if (v >= r->values) v = r->values - 1;
    *field_of(r) = v;
}

/* Which row contains a point, or -1. The one place that answers this. */
static int row_at(float mx, float my, int vw, int vh) {
    int n = menu_row_count();
    for (int i = 0; i < n; i++) {
        float x0, y0, x1, y1;
        if (!menu_row_bounds(i, vw, vh, &x0, &y0, &x1, &y1)) continue;
        if (mx >= x0 && mx <= x1 && my >= y0 && my <= y1) return i;
    }
    return -1;
}

/* --- Input / 입력 --- */

/* The row a drag is holding, or -1. A latch rather than a per-move hit test,
   because a drag that only worked while the pointer stayed inside a six-pixel
   tall bar is not a drag -- the hand moves vertically as it moves sideways, and
   the control has to keep following.
   드래그가 붙잡고 있는 행이며, 없으면 -1입니다. 이동마다 히트 판정을 하지 않고 래치를 두는
   이유는, 포인터가 높이 6픽셀짜리 막대 안에 머무는 동안에만 동작하는 드래그는 드래그가 아니기
   때문입니다. 손은 옆으로 움직이면서 위아래로도 움직이며, 컨트롤은 계속 따라가야 합니다. */
static int g_drag = -1;

void menu_mouse_up(void) { g_drag = -1; }

int menu_hover(float mx, float my, int vw, int vh) {
    if (!menu_is_open()) { g_drag = -1; return -1; }

    /* A DRAG IN PROGRESS OWNS THE POINTER. Answered before the hit test, and
       the row is not re-derived from `my` -- the held row keeps the pointer
       even when it wanders off its own band, which is what makes this a drag
       rather than a sequence of clicks that stop the moment the hand drifts.
       진행 중인 드래그가 포인터를 소유합니다. 히트 판정보다 먼저 답하며, 행을 `my`로 다시
       유도하지 않습니다. 붙잡힌 행이 자기 대역을 벗어나도 포인터를 계속 쥐며, 그것이 이것을
       손이 흔들리는 순간 멈추는 클릭의 연속이 아니라 드래그로 만듭니다. */
    if (g_drag >= 0) {
        slider_set_from_x(g_drag, mx, vw, vh);
        return g_drag;
    }

    int r = row_at(mx, my, vw, vh);
    /* Only moved when the cursor is actually over a row, so drifting off the
       list leaves the keyboard's choice highlighted.
       커서가 실제로 행 위에 있을 때만 옮기므로, 목록 밖으로 벗어나도 키보드로 고른 것이
       강조된 채 남습니다. */
    if (r >= 0) g_cursor = r;
    return r;
}

int menu_click(float mx, float my, int vw, int vh, int right) {
    if (!menu_is_open()) return 0;

    int r = row_at(mx, my, vw, vh);
    if (r < 0) return 0;      /* a stray click does nothing -- see menu.h */

    g_cursor = r;

    /* A LOCKED ROW TAKES THE CLICK AND DOES NOTHING WITH IT. It returns 1
       because the click DID land on a row -- 0 here would be "you clicked
       nothing", and the caller reads that to decide whether the world gets the
       button instead. Firing a shot into the level from the title screen
       because a row refused is the wrong kind of refusal.
       Answered before the bar test below, because ::menu_row_bar_bounds knows
       nothing about locks and would happily start a drag on one.
       *잠긴 행은 클릭을 받아들이고 아무것도 하지 않습니다.* 1을 반환하는 이유는 그 클릭이
       실제로 행에 *닿았기* 때문입니다. 이곳의 0은 "아무것도 클릭하지 않았다"이며, 호출자는
       그것을 읽고 버튼을 월드에 넘길지 결정합니다. 행이 거절했다는 이유로 타이틀 화면에서
       레벨에 총알이 나가는 것은 잘못된 종류의 거절입니다.
       아래의 막대 판정보다 먼저 답합니다. ::menu_row_bar_bounds는 잠금에 대해 아무것도 모르며
       기꺼이 그 위에서 드래그를 시작할 것이기 때문입니다. */
    if (menu_row_locked(r)) return 1;

    /* The right button is only meaningful on a value row, where it reverses
       the cycle. On a button row there is no "backward", so it does what the
       left button does rather than nothing -- a right-click on QUIT that
       silently ignored the player would read as a broken menu.
       오른쪽 버튼은 값을 가진 행에서만 의미가 있으며 순환을 역방향으로 돌립니다. 버튼
       행에는 "뒤로"가 없으므로 아무 일도 하지 않는 대신 왼쪽 버튼과 같은 일을 합니다.
       QUIT에서 오른쪽 클릭이 조용히 무시되면 고장 난 메뉴로 읽힙니다. */
    int n;
    const Row *rs = rows_of(g_screen, &n);

    /* ON THE BAR, the notch you point at is the notch you get, and the button
       stays held so the pointer can drag it. This is the whole difference
       between a slider and the value row it is drawn instead of: cycling still
       happens, but only where cycling is the only thing available.

       Off the bar -- on the label, or in the space either side -- a slider
       falls back to cycling, so the keyboard's behaviour and the mouse's agree
       on every part of the row that is not the control itself.

       Right-click never drags. It reverses one notch, which is what it does on
       every other value row; a drag with the wrong button would be a second way
       to do the same thing and a first way to do it by accident.

       *막대 위에서는* 가리킨 눈금이 얻는 눈금이며, 버튼을 누른 채로 두면 포인터가 그것을 끌 수
       있습니다. 이것이 슬라이더와, 그것 대신 그려지던 값 행 사이의 차이 전부입니다. 순환은
       여전히 일어나지만 순환만이 유일한 선택지인 곳에서만 일어납니다.

       막대 *바깥*(레이블 위나 양옆 여백)에서 슬라이더는 순환으로 물러나므로, 컨트롤 자체가
       아닌 행의 모든 부분에서 키보드의 동작과 마우스의 동작이 일치합니다.

       오른쪽 클릭은 결코 드래그하지 않습니다. 한 눈금 되돌리며, 그것은 다른 모든 값 행에서
       하는 일과 같습니다. 엉뚱한 버튼으로 하는 드래그는 같은 일을 하는 두 번째 방법이자
       실수로 하게 되는 첫 번째 방법입니다. */
    float bx0, by0, bx1, by1;
    if (!right && menu_row_bar_bounds(r, vw, vh, &bx0, &by0, &bx1, &by1) &&
        mx >= bx0 && mx <= bx1) {
        slider_set_from_x(r, mx, vw, vh);
        g_drag = r;
        return 1;
    }

    if (right && rs && is_value(&rs[r])) menu_adjust(-1);
    else                                        menu_activate();
    return 1;
}

void menu_escape(void) {
    /* One key, three meanings, and each is the obvious one for where it is
       pressed: open, step back, close. Notably none of them is "quit".
       Expressed against `g_home` rather than against MENU_ROOT by name, which
       is what let the title screen arrive without a branch here: before a run
       "step back" lands on the title, and "close" is refused below because
       there is no run underneath to close onto.
       키 하나에 의미가 셋이며, 각각은 눌린 위치에서 가장 자연스러운 것입니다. 열기,
       뒤로, 닫기입니다. 그중 어느 것도 "종료"가 아니라는 점이 핵심입니다.
       MENU_ROOT을 이름으로 지목하지 않고 `g_home`을 기준으로 표현하며, 그것이 타이틀 화면이
       이곳에 분기를 추가하지 않고 도착할 수 있게 한 것입니다. 플레이 이전에 "뒤로"는 타이틀에
       놓이고, "닫기"는 닫고 갈 플레이가 아래에 없으므로 아래에서 거절됩니다. */
    if (g_screen == MENU_CLOSED)   g_screen = g_home;
    else if (g_screen != g_home)   g_screen = g_home;
    else if (g_home != MENU_TITLE) g_screen = MENU_CLOSED;
    /* else: home IS the title, and the title cannot be closed. Deliberately not
       a no-op wrapped in an early return -- the cursor still goes back to the
       top, which is what a player pressing ESC on a screen with nothing to
       leave is asking for.
       그 밖의 경우: 집이 곧 타이틀이며 타이틀은 닫을 수 없습니다. 의도적으로 조기 반환으로
       감싼 무동작이 아닙니다. 커서는 여전히 맨 위로 돌아가며, 떠날 곳이 없는 화면에서 ESC를
       누르는 플레이어가 요청하는 것이 그것입니다. */
    g_cursor = 0;
}

void menu_move(int delta) {
    int n;
    if (!rows_of(g_screen, &n) || n < 1) return;

    g_cursor += delta;
    /* Wrap both ways. The modulo alone leaves a negative cursor negative in C,
       so the second line is what makes UP from the top row land on the last.
       양방향으로 순환합니다. C에서는 나머지 연산만으로는 음수 커서가 음수로 남으므로,
       맨 위 행에서 위로 갔을 때 마지막 행에 놓이게 하는 것은 두 번째 줄입니다. */
    g_cursor %= n;
    if (g_cursor < 0) g_cursor += n;
}

void menu_adjust(int delta) {
    const Row *r = row_of(g_cursor);
    if (!r || locked(r)) return;
    if (!is_value(r) || r->values < 1) return;

    int *f = field_of(r);
    int  v = *f + delta;
    v %= r->values;
    if (v < 0) v += r->values;
    *f = v;

    /* Display mode needs the window rebuilt, and pixel size needs the
       offscreen target resized -- both are the caller's work, so both raise
       the same action. The other two rows are read straight from the settings
       every frame and need no signal at all.
       표시 모드는 창을 다시 만들어야 하고 픽셀 크기는 오프스크린 타깃 크기를 바꿔야
       하며, 둘 다 호출자의 몫이므로 같은 동작을 올립니다. 나머지 두 행은 매 프레임
       설정에서 직접 읽으므로 신호가 전혀 필요 없습니다. */
    if (f == &g_set.display || f == &g_set.pixel)
        g_pending = MENU_ACT_DISPLAY;
}

void menu_activate(void) {
    const Row *r = row_of(g_cursor);
    if (!r) return;

    /* A LOCKED ROW DOES NOTHING. Three functions ask -- this one,
       ::menu_adjust and ::menu_click -- and all three ask ::locked rather than
       inspecting `needs` themselves, which is what keeps the row that is drawn
       dim and the row that refuses to fire the same row.
       Today's only locked row is an action, which ::menu_adjust would ignore
       anyway. It is guarded there regardless: "the locked ones happen to be
       actions" is a fact about one table, and the day it stops being true the
       symptom would be a setting that can be changed but not chosen.
       *잠긴 행은 아무 일도 하지 않습니다.* 세 함수가 묻습니다. 이 함수와 ::menu_adjust와
       ::menu_click이며, 셋 다 `needs`를 직접 들여다보지 않고 ::locked에 묻습니다. 그것이 흐리게
       그려지는 행과 발동을 거절하는 행을 같은 행으로 유지합니다.
       오늘 잠기는 유일한 행은 동작 행이고 ::menu_adjust는 어차피 그것을 무시합니다. 그럼에도
       그곳에도 검사를 둡니다. "잠기는 것들이 마침 동작 행이다"는 표 하나에 대한 사실이며, 그것이
       참이기를 그만두는 날의 증상은 고를 수는 없는데 바꿀 수는 있는 설정입니다. */
    if (locked(r)) return;

    switch (r->kind) {
    case ROW_ACTION:
        g_pending = (MenuAction)r->arg;
        break;
    case ROW_BACK:
        g_screen = g_home;
        g_cursor = 0;
        g_drag   = -1;
        break;
    case ROW_SCREEN:
        g_screen = (MenuScreen)r->arg;
        g_cursor = 0;
        /* The row under the pointer is not the row that was held.
           포인터 아래의 행은 붙잡고 있던 행이 아닙니다. */
        g_drag   = -1;
        break;
    case ROW_VALUE:
    case ROW_SLIDER:
        /* ENTER cycles forward, so the main action key does something on
           every row. See the note on menu_activate in menu.h.
           ENTER는 앞으로 순환시키므로 주 실행 키가 모든 행에서 무언가를 합니다.
           menu.h의 menu_activate 참고 사항을 확인하십시오. */
        menu_adjust(+1);
        break;
    }
}

/* --- Actions / 동작 --- */

MenuAction menu_take_action(void) {
    MenuAction a = g_pending;
    g_pending = MENU_ACT_NONE;
    return a;
}

void menu_close(void) {
    g_drag = -1;
    g_screen = MENU_CLOSED;
    g_cursor = 0;

    /* AND THE TITLE STOPS BEING HOME. "Closed" and "home is the title" are
       contradictory: the title exists because there is no run behind it, and a
       closed menu is a run being played. Putting them back together here makes
       the invariant hold by construction rather than by every caller of this
       function remembering to say so -- and this function IS the moment a run
       starts being played, whichever of its callers said it.
       *그리고 타이틀은 집이기를 그만둡니다.* "닫힘"과 "집이 타이틀"은 서로 모순됩니다. 타이틀이
       존재하는 이유는 뒤에 플레이가 없기 때문이고, 닫힌 메뉴는 플레이 중이라는 뜻입니다. 이곳에서
       둘을 도로 맞추면, 이 함수의 모든 호출자가 그렇게 말해 주기를 기다리는 대신 불변식이
       구조적으로 성립합니다. 그리고 어느 호출자가 말했든, 플레이가 시작되는 순간이 곧 이
       함수입니다. */
    g_home = MENU_ROOT;
}
