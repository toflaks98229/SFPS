/**
 * @file menu.h
 * @brief The ESC menu: pause, settings, restart and quit.
 *
 * ENGLISH
 * -------
 * ESC used to quit the game outright, from anywhere, with no confirmation --
 * one keypress between a run in progress and the desktop. This module makes it
 * open a menu instead, which is what ESC means in every other first-person
 * game.
 *
 * The module owns the menu's STATE and its INPUT rules; it owns no drawing.
 * scene.c draws it, the same way it draws the HUD and the win screen, because
 * the menu is a UI pass like those two and every reason they live there
 * applies here as well. Splitting it that way also keeps this file free of GL,
 * so the navigation rules can be driven headlessly by tools/menutest.c.
 *
 * @note The settings this exposes are applied by the CALLER, not here. A
 *       display mode change destroys and recreates the GL context's window,
 *       and a module that owns no window cannot do that. ::menu_take_action
 *       reports what the player asked for and main.c carries it out -- the
 *       same shape ::enemy_update uses to report damage rather than reaching
 *       into the player's health.
 *
 * 한국어
 * ------
 * ESC는 원래 어디서든 확인 없이 게임을 즉시 종료했습니다. 진행 중인 플레이와 바탕화면
 * 사이에 키 한 번뿐이었습니다. 이 모듈은 대신 메뉴를 열게 하며, 이는 다른 모든 1인칭
 * 게임에서 ESC가 의미하는 바입니다.
 *
 * 이 모듈은 메뉴의 *상태*와 *입력 규칙*을 소유하며, 그리기는 소유하지 않습니다.
 * scene.c가 HUD와 승리 화면을 그리는 것과 같은 방식으로 메뉴를 그립니다. 메뉴도 그 둘과
 * 같은 UI 패스이며, 그 둘이 그곳에 있는 모든 이유가 여기에도 똑같이 적용되기
 * 때문입니다. 이렇게 나누면 이 파일이 GL로부터 자유로워지므로, 탐색 규칙을
 * tools/menutest.c가 헤드리스로 구동할 수 있습니다.
 *
 * @note 이곳이 노출하는 설정은 *호출자*가 적용하며 이 모듈이 하지 않습니다. 표시 모드
 *       변경은 GL 컨텍스트의 창을 파괴하고 다시 만드는 일인데, 창을 소유하지 않은
 *       모듈은 그것을 할 수 없습니다. ::menu_take_action은 플레이어가 무엇을
 *       요청했는지 보고하고 main.c가 그것을 수행합니다. ::enemy_update가 플레이어의
 *       체력에 직접 손대지 않고 피해량을 보고하는 것과 동일한 형태입니다.
 */
#ifndef MENU_H
#define MENU_H

/* --- Enumerations / 열거형 --- */

/**
 * @brief Which screen of the menu is showing.
 *
 * ENGLISH
 * -------
 * A menu is in exactly one of these, which is why this is an enum rather than
 * a pair of "open" and "in settings" flags -- "closed but in settings" is not
 * a state that can be represented, so it is not one that has to be handled.
 * The same argument ::HookState is built on.
 *
 * 한국어
 * ------
 * 메뉴는 항상 이 중 정확히 하나의 상태에 있습니다. "열림"과 "설정 중"이라는 플래그
 * 쌍이 아니라 열거형인 이유가 그것입니다. "닫혀 있으면서 설정 화면"은 표현할 수 없는
 * 상태이므로 처리할 필요도 없는 상태가 됩니다. ::HookState가 기반하는 것과 동일한
 * 논거입니다.
 */
typedef enum {
    MENU_CLOSED = 0,  /**< Playing; the menu is not up. / 플레이 중이며 메뉴가 없습니다. */
    MENU_ROOT,        /**< The root menu: resume, settings, restart, quit. / 최상위 메뉴. */
    MENU_SETTINGS     /**< The settings page. / 설정 화면. */
} MenuScreen;

/**
 * @brief What the menu is asking the caller to do.
 *
 * ENGLISH
 * -------
 * Returned by ::menu_take_action and cleared by the same call, so an action is
 * delivered exactly once however many times the caller asks. That matters
 * because two of these -- restarting and changing the display mode -- are not
 * idempotent: performing one twice because the frame loop polled twice would
 * restart a level the player had already begun.
 *
 * 한국어
 * ------
 * ::menu_take_action이 반환하며 같은 호출이 이를 지웁니다. 따라서 호출자가 몇 번을
 * 묻든 동작은 정확히 한 번만 전달됩니다. 이것이 중요한 이유는 이 중 둘(재시작과 표시
 * 모드 변경)이 멱등하지 않기 때문입니다. 프레임 루프가 두 번 조회했다는 이유로 두 번
 * 수행하면, 플레이어가 이미 시작한 레벨을 다시 시작시키게 됩니다.
 */
typedef enum {
    MENU_ACT_NONE = 0,   /**< Nothing to do. / 할 일 없음. */
    MENU_ACT_RESTART,    /**< Reload the current level from the start. / 현재 레벨을 처음부터 다시 로드. */
    MENU_ACT_QUIT,       /**< Leave the game. / 게임 종료. */
    MENU_ACT_DISPLAY     /**< Apply the display mode; see ::menu_settings. / 표시 모드 적용. */
} MenuAction;

/**
 * @brief Window presentation modes offered by the settings page.
 *
 * ENGLISH
 * -------
 * @note Borderless rather than exclusive fullscreen. Exclusive mode changes
 *       the desktop resolution, which has to be put back on exit and on a
 *       crash -- and a crash that leaves the desktop at 640x360 is a worse
 *       failure than this game can justify. A borderless window sized to the
 *       monitor looks the same and alt-tabs instantly.
 *
 * 한국어
 * ------
 * @note 전체 화면 독점 모드가 아니라 테두리 없는 창입니다. 독점 모드는 바탕화면 해상도를
 *       바꾸므로 종료 시와 비정상 종료 시 되돌려야 하는데, 바탕화면을 640x360으로
 *       남기고 죽는 것은 이 게임이 정당화할 수 있는 것보다 나쁜 실패입니다. 모니터
 *       크기에 맞춘 테두리 없는 창은 보기에 같으면서 alt-tab이 즉시 됩니다.
 */
typedef enum {
    DISPLAY_WINDOWED = 0,  /**< A normal resizable-less window. / 일반 창. */
    DISPLAY_BORDERLESS,    /**< Borderless, filling the monitor. / 테두리 없이 모니터를 채움. */
    DISPLAY_MODE_COUNT     /**< How many modes there are. / 모드의 개수. */
} DisplayMode;

/**
 * @brief Art resolution presets, as divisors of the window height.
 *
 * ENGLISH
 * -------
 * Expressed as a target height the same way ::POST_HEIGHT is, because the real
 * constraint is the integer magnification: main.c divides the window height by
 * this to get a whole-number scale. A preset that does not divide evenly still
 * works -- it simply lands on the next scale down.
 *
 * @note Lower is chunkier. ::GFX_PIXEL_FINE is close to no pixelisation at
 *       1080p, and ::GFX_PIXEL_CHUNKY is roughly the PlayStation's own 320x240.
 *
 * 한국어
 * ------
 * ::POST_HEIGHT와 같은 방식으로 목표 높이로 표현합니다. 실제 제약이 정수 확대
 * 배율이기 때문입니다. main.c가 창 높이를 이 값으로 나누어 정수 배율을 구합니다. 균등하게
 * 나누어떨어지지 않는 프리셋도 동작하며, 단지 한 단계 낮은 배율에 놓일 뿐입니다.
 *
 * @note 낮을수록 픽셀이 큽니다. ::GFX_PIXEL_FINE은 1080p에서 픽셀화가 거의 없는 수준이고,
 *       ::GFX_PIXEL_CHUNKY는 대략 플레이스테이션의 320x240입니다.
 */
typedef enum {
    GFX_PIXEL_CHUNKY = 0,  /**< ~240p art. / 약 240p 아트. */
    GFX_PIXEL_NORMAL,      /**< ~360p art -- the shipped default. / 약 360p 아트. 기본값. */
    GFX_PIXEL_FINE,        /**< ~540p art. / 약 540p 아트. */
    GFX_PIXEL_COUNT        /**< How many presets there are. / 프리셋의 개수. */
} GfxPixelPreset;

/* --- Type definitions / 타입 정의 --- */

/**
 * @struct MenuSettings
 * @brief Everything the settings page can change.
 *
 * ENGLISH
 * -------
 * @note Plain values with no callbacks. The caller reads them after
 *       ::menu_take_action reports ::MENU_ACT_DISPLAY and applies whatever
 *       changed -- which keeps this struct describing WHAT the player wants
 *       rather than HOW it is achieved.
 *
 * 한국어
 * ------
 * 설정 화면이 바꿀 수 있는 모든 것입니다.
 * @note 콜백 없는 단순한 값들입니다. ::menu_take_action이 ::MENU_ACT_DISPLAY를 보고한
 *       뒤 호출자가 이 값을 읽어 변경된 것을 적용합니다. 덕분에 이 구조체는 그것이
 *       *어떻게* 달성되는지가 아니라 플레이어가 *무엇을* 원하는지를 기술하는 채로
 *       유지됩니다.
 */
typedef struct {
    int display;    /**< A ::DisplayMode. / ::DisplayMode 값. */
    int pixel;      /**< A ::GfxPixelPreset. / ::GfxPixelPreset 값. */
    int post_on;    /**< Non-zero to run the pixelise/dither pass. / 픽셀화·디더 패스를 켜려면 0이 아닌 값. */
    int scanlines;  /**< Non-zero to draw CRT scanlines. / CRT 주사선을 그리려면 0이 아닌 값. */
} MenuSettings;

/* --- Lifecycle / 수명 주기 --- */

/**
 * @brief Resets the menu to closed with the given starting settings.
 *
 * ENGLISH
 * -------
 * @param[in] s Initial settings, or NULL for the shipped defaults.
 *
 * 한국어
 * ------
 * @brief 메뉴를 닫힌 상태로 초기화하고 시작 설정을 지정합니다.
 * @param[in] s 초기 설정. NULL이면 기본값을 사용합니다.
 */
void menu_init(const MenuSettings *s);

/* --- State / 상태 --- */

/**
 * @brief Which screen is showing.
 * @return One of the ::MenuScreen values.
 *
 * 한국어
 * ------
 * @brief 어떤 화면이 표시 중인지 반환합니다.
 */
MenuScreen menu_screen(void);

/**
 * @brief Whether the menu is up at all.
 *
 * ENGLISH
 * -------
 * @return Non-zero unless the screen is ::MENU_CLOSED.
 * @note This is the one the frame loop asks. While it is true the world is
 *       frozen and the mouse is released, exactly the way a won run freezes it.
 *
 * 한국어
 * ------
 * @brief 메뉴가 열려 있는지 여부입니다.
 * @return 화면이 ::MENU_CLOSED가 아니면 0이 아닌 값.
 * @note 프레임 루프가 묻는 것이 이 함수입니다. 참인 동안 월드가 정지하고 마우스가
 *       해제되며, 이는 승리한 플레이가 정지시키는 방식과 정확히 같습니다.
 */
int menu_is_open(void);

/**
 * @brief The current settings.
 * @return A pointer to the live settings; never NULL.
 * @warning Points to module storage. Read it, do not keep it across a
 *          ::menu_init.
 *
 * 한국어
 * ------
 * @brief 현재 설정입니다.
 * @return 실시간 설정을 가리키는 포인터. 절대 NULL이 아닙니다.
 * @warning 모듈 저장 공간을 가리킵니다. 읽기만 하고 ::menu_init을 거쳐 보관하지
 *          마십시오.
 */
const MenuSettings *menu_settings(void);

/**
 * @brief How many rows the current screen has, and which is highlighted.
 *
 * ENGLISH
 * -------
 * Exposed so the drawing code does not need its own copy of the row list --
 * a second copy would drift from the navigation, and the symptom would be a
 * highlight on one row while a different one activates.
 *
 * 한국어
 * ------
 * @brief 현재 화면의 행 수와 강조된 행입니다.
 *
 * 그리기 코드가 행 목록의 사본을 따로 갖지 않도록 공개합니다. 사본이 두 개면 탐색과
 * 어긋나게 되며, 증상은 강조된 행과 실제로 실행되는 행이 다른 형태로 나타납니다.
 */
int menu_row_count(void);
int menu_cursor(void);

/**
 * @brief The label and value text for one row of the current screen.
 *
 * ENGLISH
 * -------
 * @param[in]  row   Row index, 0 to ::menu_row_count - 1.
 * @param[out] value Receives the right-hand value text, or NULL for rows that
 *                   are plain buttons. Never NULL itself; set to "" instead.
 * @return The row's label, or "" for an out-of-range row.
 * @warning Both strings are static storage and must not be freed.
 *
 * 한국어
 * ------
 * @brief 현재 화면 한 행의 레이블과 값 텍스트입니다.
 * @param[in]  row   행 인덱스. 0부터 ::menu_row_count - 1까지.
 * @param[out] value 오른쪽 값 텍스트를 받습니다. 단순 버튼인 행은 ""가 됩니다. 이
 *                   포인터 자체는 NULL이 되지 않습니다.
 * @return 행의 레이블. 범위를 벗어난 행이면 "".
 * @warning 두 문자열 모두 정적 저장 공간이므로 해제해서는 안 됩니다.
 */
const char *menu_row_text(int row, const char **value);

/* --- Layout / 배치 --- */

/**
 * @brief The on-screen box one row occupies, in UI pixels.
 *
 * ENGLISH
 * -------
 * @param[in]  row Row index.
 * @param[in]  vw  Viewport width in pixels.
 * @param[in]  vh  Viewport height in pixels.
 * @param[out] x0  Receives the left edge. May be NULL.
 * @param[out] y0  Receives the top edge. May be NULL.
 * @param[out] x1  Receives the right edge. May be NULL.
 * @param[out] y1  Receives the bottom edge. May be NULL.
 * @return 1 for a real row, 0 for one out of range (outputs untouched).
 *
 * @note The geometry lives HERE rather than in the drawing code because the
 *       mouse has to hit exactly what the eye sees. Two copies of this
 *       arithmetic -- one to draw a row and one to decide what was clicked --
 *       would drift, and the symptom is the cruellest kind: clicking a row and
 *       getting the one above it, which reads as the game ignoring the player.
 * @note Rows are laid out as one centred block, so the menu does not shift
 *       when a screen with a different row count opens.
 *
 * 한국어
 * ------
 * @brief 한 행이 화면에서 차지하는 영역입니다 (UI 픽셀 단위).
 * @param[in]  row 행 인덱스.
 * @param[in]  vw  뷰포트 너비 (픽셀).
 * @param[in]  vh  뷰포트 높이 (픽셀).
 * @param[out] x0  왼쪽 경계를 받습니다. NULL이어도 됩니다.
 * @param[out] y0  위쪽 경계를 받습니다. NULL이어도 됩니다.
 * @param[out] x1  오른쪽 경계를 받습니다. NULL이어도 됩니다.
 * @param[out] y1  아래쪽 경계를 받습니다. NULL이어도 됩니다.
 * @return 실제 행이면 1, 범위를 벗어나면 0 (출력값은 변경되지 않음).
 *
 * @note 배치가 그리기 코드가 아니라 *이곳에* 있는 이유는, 마우스가 눈에 보이는 것을
 *       정확히 맞혀야 하기 때문입니다. 이 계산의 사본이 둘이면(행을 그리는 쪽과 무엇이
 *       클릭되었는지 판정하는 쪽) 서로 어긋나게 되며, 그 증상은 가장 고약한 종류입니다.
 *       행을 클릭했는데 그 위의 행이 선택되는 것은 게임이 플레이어를 무시하는 것으로
 *       읽힙니다.
 * @note 행은 중앙에 놓인 하나의 묶음으로 배치되므로, 행 수가 다른 화면이 열려도 메뉴가
 *       이동하지 않습니다.
 */
int menu_row_bounds(int row, int vw, int vh,
                    float *x0, float *y0, float *x1, float *y1);

/**
 * @brief Where the title baseline sits, for the drawing code.
 *
 * 한국어
 * ------
 * @brief 제목의 기준선 위치입니다. 그리기 코드를 위한 것입니다.
 */
float menu_title_y(int vw, int vh);

/**
 * @brief Where the hint line sits, for the drawing code.
 *
 * 한국어
 * ------
 * @brief 힌트 줄의 위치입니다. 그리기 코드를 위한 것입니다.
 */
float menu_hint_y(int vw, int vh);

/* --- Input / 입력 --- */

/**
 * @brief Points the highlight at whatever row is under the cursor.
 *
 * ENGLISH
 * -------
 * @param[in] mx Cursor x in UI pixels.
 * @param[in] my Cursor y in UI pixels.
 * @param[in] vw Viewport width in pixels.
 * @param[in] vh Viewport height in pixels.
 * @return The row under the cursor, or -1 when it is over none.
 * @note Moving the highlight on HOVER rather than only on click is what makes
 *       the mouse and the keyboard the same menu rather than two: whichever
 *       one the player last used, the highlight shows where a press would
 *       land.
 * @note Leaves the highlight where it is when the cursor is over no row, so
 *       drifting off the list does not deselect what the keyboard chose.
 *
 * 한국어
 * ------
 * @brief 커서 아래의 행으로 강조 표시를 옮깁니다.
 * @param[in] mx 커서 x 좌표 (UI 픽셀).
 * @param[in] my 커서 y 좌표 (UI 픽셀).
 * @param[in] vw 뷰포트 너비 (픽셀).
 * @param[in] vh 뷰포트 높이 (픽셀).
 * @return 커서 아래의 행. 어느 행에도 없으면 -1.
 * @note 클릭할 때가 아니라 *호버*할 때 강조를 옮기는 것이, 마우스와 키보드를 두 개의
 *       메뉴가 아닌 하나로 만듭니다. 플레이어가 마지막으로 무엇을 썼든, 강조 표시가
 *       지금 누르면 어디에 닿을지를 보여 줍니다.
 * @note 커서가 어느 행에도 없으면 강조를 그대로 두므로, 목록 밖으로 벗어나도 키보드로
 *       고른 것이 해제되지 않습니다.
 */
int menu_hover(float mx, float my, int vw, int vh);

/**
 * @brief Clicks the row under the cursor.
 *
 * ENGLISH
 * -------
 * @param[in] mx    Cursor x in UI pixels.
 * @param[in] my    Cursor y in UI pixels.
 * @param[in] vw    Viewport width in pixels.
 * @param[in] vh    Viewport height in pixels.
 * @param[in] right Non-zero for the right button, which steps a value
 *                  BACKWARD; the left button steps forward.
 * @return 1 when a row was hit, 0 when the click landed on nothing.
 * @note A click off the rows does nothing at all -- deliberately. Closing the
 *       menu on any stray click would make a misclick indistinguishable from
 *       choosing Resume, and one of those is recoverable.
 * @note On a value row the left button does what ::menu_activate does, so a
 *       setting cycles; the right button reverses it, which is what makes a
 *       three-value row like the pixel preset reachable in one click either
 *       way.
 *
 * 한국어
 * ------
 * @brief 커서 아래의 행을 클릭합니다.
 * @param[in] mx    커서 x 좌표 (UI 픽셀).
 * @param[in] my    커서 y 좌표 (UI 픽셀).
 * @param[in] vw    뷰포트 너비 (픽셀).
 * @param[in] vh    뷰포트 높이 (픽셀).
 * @param[in] right 오른쪽 버튼이면 0이 아닌 값이며 값을 *뒤로* 이동시킵니다. 왼쪽
 *                  버튼은 앞으로 이동시킵니다.
 * @return 행을 맞혔으면 1, 아무것도 없는 곳을 클릭했으면 0.
 * @note 행 바깥의 클릭은 의도적으로 아무 일도 하지 않습니다. 아무 데나 클릭했을 때 메뉴가
 *       닫히면 잘못 클릭한 것과 재개를 고른 것이 구분되지 않으며, 그중 하나만 되돌릴 수
 *       있습니다.
 * @note 값을 가진 행에서 왼쪽 버튼은 ::menu_activate와 같은 일을 하여 설정을
 *       순환시키고, 오른쪽 버튼은 그 반대로 이동시킵니다. 덕분에 픽셀 프리셋처럼 값이
 *       셋인 행에도 어느 방향으로든 한 번의 클릭으로 도달할 수 있습니다.
 */
int menu_click(float mx, float my, int vw, int vh, int right);

/**
 * @brief Opens the menu, or steps back one screen if it is already open.
 *
 * ENGLISH
 * -------
 * This is what ESC does. From ::MENU_CLOSED it opens the root; from
 * ::MENU_SETTINGS it returns to the root; from the root it closes.
 *
 * @note ESC therefore never quits the game. Leaving is a menu row the player
 *       has to choose, which is the whole point of this module -- one
 *       mistaken keypress used to end a run outright.
 *
 * 한국어
 * ------
 * @brief 메뉴를 열거나, 이미 열려 있으면 한 화면 뒤로 물러납니다.
 *
 * ESC가 하는 일입니다. ::MENU_CLOSED에서는 최상위 메뉴를 열고, ::MENU_SETTINGS에서는
 * 최상위로 돌아가며, 최상위에서는 닫습니다.
 *
 * @note 따라서 ESC는 결코 게임을 종료하지 않습니다. 나가는 것은 플레이어가 직접 골라야
 *       하는 메뉴 행이며, 그것이 이 모듈의 존재 이유입니다. 예전에는 잘못 누른 키 하나가
 *       진행 중인 플레이를 그대로 끝냈습니다.
 */
void menu_escape(void);

/**
 * @brief Moves the highlight, wrapping at both ends.
 *
 * ENGLISH
 * -------
 * @param[in] delta -1 for up, +1 for down.
 * @note Wraps deliberately: with four rows, wrapping means the quit row is one
 *       press UP from the top rather than three presses down.
 * @note A no-op while the menu is closed, so the caller can hand it input
 *       without checking first.
 *
 * 한국어
 * ------
 * @brief 강조 표시를 이동하며 양 끝에서 순환합니다.
 * @param[in] delta 위로는 -1, 아래로는 +1.
 * @note 의도적으로 순환합니다. 행이 네 개일 때 순환하면 종료 행이 맨 위에서 아래로 세
 *       번이 아니라 *위로* 한 번입니다.
 * @note 메뉴가 닫혀 있으면 아무 동작도 하지 않으므로, 호출자가 먼저 확인하지 않고
 *       입력을 넘겨도 됩니다.
 */
void menu_move(int delta);

/**
 * @brief Changes the highlighted row's value, for rows that have one.
 *
 * ENGLISH
 * -------
 * @param[in] delta -1 for the previous value, +1 for the next.
 * @note Cycles rather than clamping, so every option is reachable from either
 *       direction on a two-value row like the post-process toggle.
 * @note A row with no value ignores this entirely.
 *
 * 한국어
 * ------
 * @brief 값을 가진 행에서 강조된 행의 값을 변경합니다.
 * @param[in] delta 이전 값은 -1, 다음 값은 +1.
 * @note 제한하지 않고 순환하므로, 후처리 토글처럼 값이 둘인 행에서는 어느 방향으로도
 *       모든 선택지에 도달할 수 있습니다.
 * @note 값이 없는 행은 이를 완전히 무시합니다.
 */
void menu_adjust(int delta);

/**
 * @brief Activates the highlighted row, as ENTER would.
 *
 * ENGLISH
 * -------
 * @note On a value row this is the same as ::menu_adjust with +1, so ENTER
 *       cycles a setting rather than doing nothing. A menu where the main
 *       action key is inert on most rows teaches the player it is broken.
 *
 * 한국어
 * ------
 * @brief ENTER처럼 강조된 행을 실행합니다.
 * @note 값을 가진 행에서는 +1을 준 ::menu_adjust와 동일하므로, ENTER가 아무 일도 하지
 *       않는 대신 설정을 순환시킵니다. 주 실행 키가 대부분의 행에서 반응하지 않는
 *       메뉴는 플레이어에게 고장 났다고 가르칩니다.
 */
void menu_activate(void);

/* --- Actions / 동작 --- */

/**
 * @brief Takes the pending action, clearing it.
 *
 * ENGLISH
 * -------
 * @return The pending ::MenuAction, or ::MENU_ACT_NONE.
 * @note Clears as it reports, so an action fires exactly once. See
 *       ::MenuAction for why that is load-bearing rather than tidy.
 *
 * 한국어
 * ------
 * @brief 대기 중인 동작을 가져오며 그것을 지웁니다.
 * @return 대기 중인 ::MenuAction, 없으면 ::MENU_ACT_NONE.
 * @note 보고하면서 지우므로 동작이 정확히 한 번만 발생합니다. 이것이 단순한 정돈이
 *       아니라 구조적으로 중요한 이유는 ::MenuAction을 참조하십시오.
 */
MenuAction menu_take_action(void);

/**
 * @brief Closes the menu without taking any action.
 *
 * ENGLISH
 * -------
 * @note For the caller to use after it has carried out a restart, so the
 *       player lands back in the game rather than in the menu they just used.
 *
 * 한국어
 * ------
 * @brief 아무 동작도 하지 않고 메뉴를 닫습니다.
 * @note 호출자가 재시작을 수행한 뒤 사용하기 위한 것입니다. 그래야 플레이어가 방금
 *       사용한 메뉴가 아니라 게임으로 돌아갑니다.
 */
void menu_close(void);

#endif
