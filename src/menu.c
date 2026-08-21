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
    ROW_SLIDER
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
    { "RESUME",    ROW_SCREEN, MENU_CLOSED,      0, 0, 0 },
    { "SETTINGS",  ROW_SCREEN, MENU_SETTINGS,    0, 0, 0 },
    { "RESTART",   ROW_ACTION, MENU_ACT_RESTART, 0, 0, 0 },
    { "CREDITS",   ROW_SCREEN, MENU_CREDITS,     0, 0, 0 },
    { "QUIT",      ROW_ACTION, MENU_ACT_QUIT,    0, 0, 0 },
};

/* The credits screen has one row, and it is the way back. The notices
   themselves are text scene.c draws, not rows: they are paragraphs to read
   rather than things to choose between, and a row per line would make the
   highlight walk down a licence.
   크레딧 화면의 행은 하나이며 그것은 돌아가는 길입니다. 고지 자체는 행이 아니라 scene.c가
   그리는 텍스트입니다. 고르는 대상이 아니라 읽는 문단이며, 줄마다 행을 두면 강조 표시가
   라이선스를 따라 내려가게 됩니다. */
static const Row CREDITS_ROWS[] = {
    { "BACK",      ROW_SCREEN, MENU_ROOT,        0, 0, 0 },
};

static const Row SETTINGS_ROWS[] = {
    { "DISPLAY",     ROW_VALUE, 0, FIELD(display),   DISPLAY_MODE_COUNT, DISPLAYS },
    { "PIXEL SIZE",  ROW_VALUE, 0, FIELD(pixel),     GFX_PIXEL_COUNT,    PIXELS   },
    { "POST FX",     ROW_VALUE, 0, FIELD(post_on),   2,                  OFF_ON   },
    { "SCANLINES",   ROW_VALUE, 0, FIELD(scanlines), 2,                  OFF_ON   },
    { "DITHER",      ROW_VALUE, 0, FIELD(dither),    GFX_DITHER_COUNT,   DITHERS  },
    { "PATTERN",     ROW_VALUE, 0, FIELD(pattern),   GFX_PATTERN_COUNT,  PATTERNS },
    { "MASTER VOL",  ROW_SLIDER,0, FIELD(master),    MENU_VOL_STEPS,     VOLUMES  },
    { "SFX VOL",     ROW_SLIDER,0, FIELD(sfx),       MENU_VOL_STEPS,     VOLUMES  },
    { "BGM VOL",     ROW_SLIDER,0, FIELD(music),     MENU_VOL_STEPS,     VOLUMES  },
    { "BACK",        ROW_SCREEN, MENU_ROOT, 0, 0, 0 },
};

/* --- Module state / 모듈 상태 --- */

static MenuScreen   g_screen;
static int          g_cursor;
static MenuAction   g_pending;
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
    *count = 0;
    return 0;
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

float menu_title_y(int vw, int vh) { (void)vw; return block_top(vh) - TITLE_GAP; }

float menu_hint_y(int vw, int vh) {
    (void)vw;
    return block_top(vh) + menu_row_count() * ROW_STEP + HINT_GAP;
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

int menu_hover(float mx, float my, int vw, int vh) {
    if (!menu_is_open()) return -1;

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

    /* The right button is only meaningful on a value row, where it reverses
       the cycle. On a button row there is no "backward", so it does what the
       left button does rather than nothing -- a right-click on QUIT that
       silently ignored the player would read as a broken menu.
       오른쪽 버튼은 값을 가진 행에서만 의미가 있으며 순환을 역방향으로 돌립니다. 버튼
       행에는 "뒤로"가 없으므로 아무 일도 하지 않는 대신 왼쪽 버튼과 같은 일을 합니다.
       QUIT에서 오른쪽 클릭이 조용히 무시되면 고장 난 메뉴로 읽힙니다. */
    int n;
    const Row *rs = rows_of(g_screen, &n);
    if (right && rs && is_value(&rs[r])) menu_adjust(-1);
    else                                        menu_activate();
    return 1;
}

void menu_escape(void) {
    /* One key, three meanings, and each is the obvious one for where it is
       pressed: open, step back, close. Notably none of them is "quit".
       키 하나에 의미가 셋이며, 각각은 눌린 위치에서 가장 자연스러운 것입니다. 열기,
       뒤로, 닫기입니다. 그중 어느 것도 "종료"가 아니라는 점이 핵심입니다. */
    if (g_screen == MENU_CLOSED)        g_screen = MENU_ROOT;
    else if (g_screen == MENU_SETTINGS) g_screen = MENU_ROOT;
    else if (g_screen == MENU_CREDITS)  g_screen = MENU_ROOT;
    else                                g_screen = MENU_CLOSED;
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
    int n;
    const Row *rs = rows_of(g_screen, &n);
    if (!rs || g_cursor < 0 || g_cursor >= n) return;

    const Row *r = &rs[g_cursor];
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
    int n;
    const Row *rs = rows_of(g_screen, &n);
    if (!rs || g_cursor < 0 || g_cursor >= n) return;

    const Row *r = &rs[g_cursor];
    switch (r->kind) {
    case ROW_ACTION:
        g_pending = (MenuAction)r->arg;
        break;
    case ROW_SCREEN:
        g_screen = (MenuScreen)r->arg;
        g_cursor = 0;
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
    g_screen = MENU_CLOSED;
    g_cursor = 0;
}
