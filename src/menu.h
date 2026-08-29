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
    MENU_SETTINGS,    /**< The settings page. / 설정 화면. */

    /**
     * @brief The credits, and the licence notices the game is obliged to show.
     *
     * ENGLISH
     * -------
     * Not decoration. SFPS ships as a single executable with no files beside
     * it, and the BSD licence on the artwork it uses requires the notice to
     * accompany a BINARY distribution -- so with nothing else in the folder,
     * the only place it can accompany anything is inside the game. A licence
     * that is present in the .rdata but unreachable by the player is a weaker
     * claim than one you can read from the menu.
     *
     * 한국어
     * ------
     * @brief 크레딧, 그리고 게임이 표시할 의무가 있는 라이선스 고지입니다.
     *
     * 장식이 아닙니다. SFPS는 옆에 아무 파일도 없는 단일 실행 파일로 배포되며, 사용하는
     * 아트의 BSD 라이선스는 *바이너리* 배포에도 고지가 동반될 것을 요구합니다. 폴더에 다른
     * 것이 없다면 무언가에 동반될 수 있는 유일한 장소는 게임 안입니다. .rdata에는 있지만
     * 플레이어가 닿을 수 없는 라이선스는 메뉴에서 읽을 수 있는 것보다 약한 주장입니다.
     */
    MENU_CREDITS,

    /**
     * @brief The front screen: which game this is going to be.
     *
     * ENGLISH
     * -------
     * THE ONE SCREEN THAT CANNOT BE CLOSED, and the reason is what is behind
     * it. Every other screen here is drawn over a run in progress, so closing
     * it hands the player back what they were doing. There is no run yet when
     * this is up -- ::RunState::title is still set and the level behind it is a
     * frozen backdrop -- so a close would leave a player looking at a room they
     * cannot move in with nothing to press. ESC therefore steps BACK to this
     * screen from settings and credits, and does nothing on it.
     *
     * IT IS ALSO WHERE THE MODE IS CHOSEN, which is why it exists at all.
     * ::RunState::endless is a property of how a run was ENTERED rather than of
     * the map -- story and endless are the same room -- so something has to ask
     * before the room is loaded, and a title screen is the only place in this
     * game that runs before a room is loaded.
     *
     * @note Opened with ::menu_open_title, not with ::menu_escape. Which screen
     *       is "home" is a fact about whether a run exists, and the menu is told
     *       rather than guessing from state it does not own.
     *
     * 한국어
     * ------
     * @brief 앞 화면. 이것이 어떤 게임이 될 것인가입니다.
     *
     * *닫을 수 없는 유일한 화면이며*, 이유는 그 뒤에 있는 것입니다. 이곳의 다른 모든 화면은
     * 진행 중인 플레이 위에 그려지므로, 닫으면 플레이어가 하던 것을 돌려받습니다. 이것이 떠
     * 있을 때는 아직 플레이가 없습니다. ::RunState::title이 여전히 서 있고 뒤의 레벨은 정지된
     * 배경입니다. 그래서 닫으면 플레이어는 움직일 수 없는 방을 누를 것도 없이 바라보게 됩니다.
     * 따라서 ESC는 설정과 크레딧에서 이 화면으로 *물러나고*, 이 화면에서는 아무 일도 하지
     * 않습니다.
     *
     * *모드를 고르는 곳이기도 하며*, 애초에 이것이 존재하는 이유가 그것입니다.
     * ::RunState::endless는 맵이 아니라 플레이에 *들어온 방식*의 성질입니다. 스토리와 무한은
     * 같은 방이기 때문입니다. 그러므로 방이 로드되기 전에 무언가가 물어야 하고, 방이 로드되기
     * 전에 도는 곳은 이 게임에서 타이틀 화면뿐입니다.
     *
     * @note ::menu_escape이 아니라 ::menu_open_title로 엽니다. 어느 화면이 "집"인가는 플레이가
     *       존재하는지에 대한 사실이며, 메뉴는 자신이 소유하지 않은 상태로부터 추측하는 대신
     *       그것을 전달받습니다.
     */
    MENU_TITLE
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
    MENU_ACT_DISPLAY,    /**< Apply the display mode; see ::menu_settings. / 표시 모드 적용. */

    /**
     * @brief Begin a story run. / 스토리 플레이를 시작합니다.
     *
     * ENGLISH: TWO ACTIONS RATHER THAN ONE WITH A FLAG, because the caller has
     * to do two different things with them anyway -- and an action carrying a
     * payload would be the first one here that does, for a distinction that is
     * one row either way.
     *
     * 한국어: 플래그 하나를 가진 동작 하나가 아니라 *동작 둘*입니다. 호출자가 어차피 둘로 서로
     * 다른 일을 해야 하며, 값을 실어 나르는 동작은 이곳에서 그렇게 하는 첫 번째가 될 텐데 그
     * 구분은 어느 쪽이든 행 하나입니다.
     */
    MENU_ACT_STORY,

    /** @brief Begin an endless run. / 무한 플레이를 시작합니다. */
    MENU_ACT_ENDLESS
} MenuAction;

/**
 * @brief What a row can require before it may be chosen.
 *
 * ENGLISH
 * -------
 * A MASK RATHER THAN A BOOL PER ROW, so the next locked row is a bit and a
 * column in the table rather than a second predicate. The value is handed in by
 * the caller through ::menu_set_unlocked; this module compares and never
 * interprets, which is what lets save.c store the same number without either of
 * them owning what it means.
 *
 * THE VOCABULARY IS HERE and not in save.h, because the question a bit answers
 * is "may this row be chosen" -- a menu question. save.h's own note says it
 * stores the mask verbatim for exactly that reason: two lists of bit names
 * agree until somebody adds the second unlock to one of them.
 *
 * 한국어
 * ------
 * @brief 어떤 행이 선택되기 전에 요구할 수 있는 것.
 *
 * *행마다의 불리언이 아니라 마스크*이므로, 다음에 잠기는 행은 두 번째 술어가 아니라 비트
 * 하나와 표의 열 하나입니다. 값은 ::menu_set_unlocked를 통해 호출자가 건네줍니다. 이 모듈은
 * 비교할 뿐 결코 해석하지 않으며, 그것이 save.c가 같은 숫자를 저장하면서도 둘 중 어느 쪽도 그
 * 뜻을 소유하지 않게 합니다.
 *
 * *어휘가 save.h가 아니라 이곳에 있는 이유*는, 비트가 답하는 질문이 "이 행을 고를 수 있는가"
 * 이기 때문입니다. 메뉴의 질문입니다. save.h 자신의 참고 사항이 바로 그 이유로 마스크를 그대로
 * 저장한다고 말합니다. 비트 이름 목록이 둘이면 누군가 그중 하나에 두 번째 해금을 추가하기
 * 전까지만 일치합니다.
 */
typedef enum {
    /**
     * @brief The endless row, earned by finishing the story once.
     *
     * ENGLISH: Locked rather than hidden. A row nobody can see is a mode
     * nobody knows to want, and the whole reason endless is worth unlocking is
     * that the player has been looking at it since the first launch.
     *
     * 한국어: 숨기지 않고 잠급니다. 아무도 볼 수 없는 행은 아무도 원할 줄 모르는 모드이며,
     * 무한 모드가 해금할 가치가 있는 이유 전부는 플레이어가 첫 실행 이후로 줄곧 그것을 보고
     * 있었다는 데 있습니다.
     */
    MENU_UNLOCK_ENDLESS = 1u << 0
} MenuUnlock;

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

/**
 * @brief How hard the resolve pass quantises colour.
 *
 * ENGLISH
 * -------
 * A setting rather than a constant because it is the single knob that decides
 * whether the picture can be read, and where to put it is taste. The dither's
 * loudness is not set by the dither: an ordered pattern swings half a
 * quantisation step to fake the shades between two levels, so fewer levels
 * means a louder pattern, in direct proportion.
 *
 * @note ::GFX_DITHER_HEAVY is what the game shipped with. Measured on a real
 *       frame, a third of the screen had neighbouring pixels differing by more
 *       than 16% of full scale, which is what was fighting the silhouettes.
 * @note ::GFX_DITHER_OFF is the PlayStation's own 15-bit colour -- five bits a
 *       channel, thirty-two levels -- which is what the PSX shader collections
 *       quantise to, and where the pattern stops reading as interference.
 *
 * 한국어
 * ------
 * @brief 해상 패스가 색을 얼마나 강하게 양자화하는지입니다.
 *
 * 상수가 아니라 설정인 이유는, 이것이 그림을 읽을 수 있는지를 결정하는 유일한 손잡이이고
 * 어디에 둘지는 취향이기 때문입니다. 디더의 시끄러움은 디더가 정하지 않습니다. 정렬
 * 패턴은 두 단계 사이의 음영을 흉내 내려고 양자화 한 단계의 절반을 흔들므로, 단계가
 * 적을수록 패턴이 정비례로 시끄러워집니다.
 */
typedef enum {
    GFX_DITHER_HEAVY = 0,  /**< 4 steps -- what this shipped with. / 4단계. 기존 배포값. */
    GFX_DITHER_NORMAL,     /**< 12 steps -- the default. / 12단계. 기본값. */
    GFX_DITHER_LIGHT,      /**< 20 steps. / 20단계. */
    GFX_DITHER_OFF,        /**< 32 steps: the PlayStation's own. / 32단계. 플레이스테이션의 값. */
    GFX_DITHER_COUNT       /**< How many presets there are. / 프리셋의 개수. */
} GfxDither;

/**
 * @brief Which pattern the dither uses.
 *
 * ENGLISH
 * -------
 * A separate setting from ::GfxDither because they are separate questions: how
 * MUCH the picture is quantised, and what shape the pattern that hides it
 * takes. Folding them into one list would have meant eight presets describing
 * two independent axes, and no way to keep a strength while trying a pattern.
 *
 * @note ::GFX_PATTERN_BAYER is an 8x8 grid, and a grid is what the eye is best
 *       at seeing: its threshold repeats every eight pixels, so a flat surface
 *       gets a weave that reads as dirt rather than as shading, and it survives
 *       neither screen scaling nor video compression.
 * @note ::GFX_PATTERN_NOISE is Jimenez's interleaved gradient noise -- named
 *       for what it is rather than called blue noise, which it is not. It is a
 *       hash chosen so neighbouring values land far apart, and it is used
 *       instead of a blue noise texture because it is one line of arithmetic
 *       and this project ships no texture it did not generate.
 *
 * 한국어
 * ------
 * @brief 디더가 어느 패턴을 쓰는지입니다.
 *
 * ::GfxDither와 별개의 설정인 이유는 서로 다른 질문이기 때문입니다. 그림을 *얼마나*
 * 양자화하는가와, 그것을 감추는 패턴이 어떤 모양인가입니다. 하나의 목록으로 합치면 독립적인
 * 두 축을 기술하는 프리셋 여덟 개가 되고, 세기를 유지한 채 패턴만 시험해 볼 방법이
 * 없어집니다.
 */
typedef enum {
    GFX_PATTERN_BAYER = 0, /**< 8x8 ordered matrix. / 8x8 정렬 행렬. */
    GFX_PATTERN_NOISE,     /**< Interleaved gradient noise. / 인터리브드 그래디언트 잡음. */
    GFX_PATTERN_COUNT      /**< How many there are. / 개수. */
} GfxDitherPattern;

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
/**
 * @brief How many notches a volume row has, and what one notch is worth.
 *
 * ENGLISH: Eleven, so the ends are exactly OFF and exactly full and the nine
 * between them are round numbers a player can name. A finer slider would be
 * more precise about a quantity nobody measures.
 *
 * 한국어: 열하나입니다. 양끝이 정확히 무음과 정확히 최대가 되고 그 사이 아홉이 플레이어가
 * 이름 붙일 수 있는 반올림된 숫자가 됩니다. 더 촘촘한 슬라이더는 아무도 재지 않는 양에 대해
 * 더 정밀할 뿐입니다.
 */
#define MENU_VOL_STEPS    11
#define MENU_VOL_PER_STEP 10

typedef struct {
    int display;    /**< A ::DisplayMode. / ::DisplayMode 값. */
    int pixel;      /**< A ::GfxPixelPreset. / ::GfxPixelPreset 값. */
    int post_on;    /**< Non-zero to run the pixelise/dither pass. / 픽셀화·디더 패스를 켜려면 0이 아닌 값. */
    int scanlines;  /**< Non-zero to draw CRT scanlines. / CRT 주사선을 그리려면 0이 아닌 값. */
    int dither;     /**< A ::GfxDither. / ::GfxDither 값. */
    int pattern;    /**< A ::GfxDitherPattern. / ::GfxDitherPattern 값. */

    /**
     * @brief Overall loudness, as a STEP rather than a percentage. 0 is silent.
     *
     * ENGLISH
     * -------
     * An index into ::MENU_VOL_STEPS, exactly like every other ROW_VALUE field
     * here -- the menu moves an int between 0 and a count and knows nothing
     * about what the int means, which is what lets one row implementation serve
     * a display mode and a volume alike. Multiply by ::MENU_VOL_PER_STEP for the
     * 0-100 figure ::audio_set_volume wants.
     *
     * 한국어
     * ------
     * @brief 전체 음량. 백분율이 아니라 *단계*입니다. 0이면 무음입니다.
     *
     * ::MENU_VOL_STEPS에 대한 인덱스이며, 이곳의 다른 모든 ROW_VALUE 필드와 정확히 같습니다.
     * 메뉴는 0과 개수 사이의 int를 움직일 뿐 그 int가 무엇을 뜻하는지 모르며, 그것이 하나의 행
     * 구현이 디스플레이 모드와 음량을 똑같이 처리할 수 있게 하는 것입니다. ::audio_set_volume이
     * 원하는 0-100 값은 ::MENU_VOL_PER_STEP을 곱해 얻습니다.
     */
    int master;

    /** @brief Sound-effect loudness, under ::master. Same units. / 효과음 음량. ::master 아래에 놓입니다. 단위 동일. */
    int sfx;

    /** @brief Music loudness, under ::master. Same units. / 음악 음량. ::master 아래에 놓입니다. 단위 동일. */
    int music;
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

/**
 * @brief Puts the menu on ::MENU_TITLE and makes that screen its home.
 *
 * ENGLISH
 * -------
 * WHAT "HOME" MEANS: the screen ESC steps back to, and the screen a BACK row
 * returns to. During a run that is ::MENU_ROOT and ESC from it closes the menu;
 * before one it is ::MENU_TITLE and ESC on it does nothing, because there is no
 * run underneath to be handed back to.
 *
 * @note Told rather than derived. This module owns no ::RunState and could only
 *       guess at whether a run exists; a menu that guessed would be a second
 *       opinion about it, and the two would disagree on exactly the frame a run
 *       begins.
 * @note ::menu_init leaves the menu closed with ::MENU_ROOT as home, which is
 *       what a restart wants -- the player has already chosen a mode and is not
 *       being asked again.
 *
 * 한국어
 * ------
 * @brief 메뉴를 ::MENU_TITLE에 놓고 그 화면을 집으로 삼습니다.
 *
 * *"집"이 뜻하는 것*은 ESC가 물러나는 화면이자 BACK 행이 돌아가는 화면입니다. 플레이 중에는
 * ::MENU_ROOT이며 그곳에서의 ESC는 메뉴를 닫습니다. 플레이 이전에는 ::MENU_TITLE이며 그곳에서의
 * ESC는 아무 일도 하지 않습니다. 돌려줄 플레이가 아래에 없기 때문입니다.
 *
 * @note 유도하지 않고 전달받습니다. 이 모듈은 ::RunState를 소유하지 않으므로 플레이가
 *       존재하는지 추측할 수밖에 없습니다. 추측하는 메뉴는 그에 대한 두 번째 의견이 되고, 그
 *       둘은 정확히 플레이가 시작되는 프레임에 어긋납니다.
 * @note ::menu_init은 메뉴를 닫힌 채로 두고 ::MENU_ROOT을 집으로 삼습니다. 재시작이 원하는 것이
 *       그것입니다. 플레이어는 이미 모드를 골랐고 다시 질문받지 않습니다.
 */
void menu_open_title(void);

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
 * @brief Is this row visible but not choosable?
 *
 * ENGLISH
 * -------
 * @param[in] row Row index on the current screen.
 * @return Non-zero when the row requires an unlock the caller has not reported.
 *
 * @note THE CURSOR STILL PASSES OVER IT. Skipping a locked row would make the
 *       highlight jump two places for a reason nothing on screen explains,
 *       which reads as the menu losing keypresses -- and it would leave a row
 *       drawn that the keyboard can never point at. Landing on it and refusing
 *       to activate says what is true: that is a real row and it is not yours
 *       yet.
 * @note An out-of-range row is not locked. A drawing loop that runs one row
 *       long already gets "" from ::menu_row_text; this answers in the same
 *       spirit rather than making the caller test the range twice.
 *
 * 한국어
 * ------
 * @brief 이 행은 보이지만 고를 수 없는가?
 * @param[in] row 현재 화면에서의 행 인덱스.
 * @return 호출자가 보고하지 않은 해금을 요구하는 행이면 0이 아닌 값.
 *
 * @note *커서는 여전히 그 위를 지나갑니다.* 잠긴 행을 건너뛰면 화면의 어느 것도 설명하지 않는
 *       이유로 강조가 두 칸씩 뛰게 되며, 그것은 메뉴가 키 입력을 잃는 것으로 읽힙니다. 또한
 *       키보드가 결코 가리킬 수 없는 행이 그려진 채 남습니다. 그 위에 서되 실행을 거절하는
 *       것이 참인 바를 말합니다. 그것은 실제 행이고 아직 당신의 것이 아닙니다.
 * @note 범위를 벗어난 행은 잠긴 것이 아닙니다. 한 행을 더 도는 그리기 루프는 이미
 *       ::menu_row_text에서 ""를 받습니다. 이 함수도 같은 정신으로 답하며, 호출자가 범위를 두
 *       번 검사하게 만들지 않습니다.
 */
int menu_row_locked(int row);

/**
 * @brief Tells the menu which unlocks the player has.
 *
 * ENGLISH
 * -------
 * @param[in] bits A mask of ::MenuUnlock values. 0 locks everything that has a
 *                 requirement, which is what a first launch looks like.
 * @note Read every time a row is drawn or activated rather than cached into the
 *       rows, so an unlock earned mid-session takes effect the moment the
 *       caller reports it -- the menu is not open at that moment and there
 *       would be nothing to re-open it for.
 *
 * 한국어
 * ------
 * @brief 플레이어가 어떤 해금을 가지고 있는지 메뉴에 알립니다.
 * @param[in] bits ::MenuUnlock 값들의 마스크. 0이면 요구 조건이 있는 모든 것이 잠기며, 첫
 *                 실행이 그렇게 보입니다.
 * @note 행에 캐시하지 않고 행이 그려지거나 실행될 때마다 읽으므로, 세션 도중에 얻은 해금은
 *       호출자가 그것을 보고하는 순간 효력을 갖습니다. 그 순간 메뉴는 열려 있지 않으며, 그것을
 *       다시 열게 할 것도 없습니다.
 */
void menu_set_unlocked(unsigned bits);

/** @brief The mask last given to ::menu_set_unlocked. / ::menu_set_unlocked에 마지막으로 준 마스크. */
unsigned menu_unlocked(void);

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

/**
 * @brief Is this row drawn as a bar, and how full is it?
 *
 * ENGLISH
 * -------
 * @param[in]  row  Row index on the current screen.
 * @param[out] fill 0 at the bottom notch, 1 at the top. Set to 0 and ignored
 *                  when the row is not a slider, so a caller may read it
 *                  without testing the return first.
 * @return Non-zero if the row wants a bar.
 *
 * @note PRESENTATION ONLY. A slider row is an ordinary value row in every other
 *       respect -- ::menu_adjust moves it, ::menu_row_text still names its
 *       value, ENTER still cycles it. This exists so the drawing side can show
 *       a quantity as a quantity without menu.c having to expose how it files
 *       its rows.
 * @note The value text is still worth drawing beside the bar. A bar says
 *       roughly how loud; a number says which notch, which is what somebody
 *       matching two rows to each other needs.
 *
 * 한국어
 * ------
 * @brief 이 행은 막대로 그려지는가, 그리고 얼마나 찼는가?
 * @param[in]  row  현재 화면에서의 행 인덱스.
 * @param[out] fill 최하단 눈금에서 0, 최상단에서 1. 행이 슬라이더가 아니면 0으로 설정되고
 *                  무시되므로, 호출자는 반환값을 먼저 검사하지 않고 읽어도 됩니다.
 * @return 막대를 원하는 행이면 0이 아닌 값.
 *
 * @note *표현 전용입니다.* 슬라이더 행은 그 밖의 모든 면에서 평범한 값 행입니다.
 *       ::menu_adjust가 움직이고, ::menu_row_text가 여전히 그 값을 이름 짓고, ENTER가 여전히
 *       순환시킵니다. 이것이 존재하는 이유는, menu.c가 행을 어떻게 분류하는지 드러내지 않고도
 *       그리기 쪽이 양을 양으로 보여 줄 수 있게 하기 위함입니다.
 * @note 막대 곁에 값 텍스트도 그릴 가치가 있습니다. 막대는 대략 얼마나 큰지를 말하고, 숫자는
 *       어느 눈금인지를 말합니다. 두 행을 서로 맞추려는 사람에게 필요한 것이 후자입니다.
 */
int menu_row_slider(int row, float *fill);

/**
 * @brief Where a slider row's bar is, in the same coordinates as ::menu_row_bounds.
 *
 * ENGLISH
 * -------
 * @param[in]  row            Row index on the current screen.
 * @param[in]  vw,vh          Viewport, as ::menu_row_bounds takes it.
 * @param[out] x0,y0,x1,y1    The track's rectangle. Untouched when the row is
 *                            not a slider.
 * @return Non-zero if the row has a bar.
 *
 * @note HERE RATHER THAN IN THE DRAWING, because the bar is a hit target and a
 *       target whose extent only the drawing knows cannot be clicked. The first
 *       version of the sliders put these numbers in scene.c and the result was
 *       a control that looked draggable and could only be cycled -- menu.c had
 *       nothing to test a click against. Same argument as ::menu_row_bounds.
 *
 * 한국어
 * ------
 * @brief 슬라이더 행의 막대 위치이며, ::menu_row_bounds와 같은 좌표계입니다.
 * @param[in]  row            현재 화면에서의 행 인덱스.
 * @param[in]  vw,vh          ::menu_row_bounds가 받는 것과 같은 뷰포트.
 * @param[out] x0,y0,x1,y1    트랙의 사각형. 행이 슬라이더가 아니면 건드리지 않습니다.
 * @return 막대가 있는 행이면 0이 아닌 값.
 *
 * @note *그리기가 아니라 이곳에 있는 이유*는 막대가 히트 대상이고, 그리기만 범위를 아는 대상은
 *       클릭할 수 없기 때문입니다. 슬라이더의 첫 판은 이 숫자들을 scene.c에 두었고, 그 결과는
 *       드래그할 수 있어 보이는데 순환밖에 되지 않는 컨트롤이었습니다. menu.c에 클릭을 판정할
 *       대상이 없었습니다. ::menu_row_bounds와 같은 논거입니다.
 */
int menu_row_bar_bounds(int row, int vw, int vh,
                        float *x0, float *y0, float *x1, float *y1);

/**
 * @brief Tells the menu the mouse button came up, ending any drag.
 *
 * ENGLISH: Safe to call at any time, whether the menu is open or not and
 * whether anything was being dragged or not, so the caller can hand every
 * button-up over without deciding first.
 *
 * 한국어: 메뉴가 열려 있든 아니든, 무언가를 끌고 있었든 아니든 언제든 호출해도 안전하므로,
 * 호출자는 먼저 판단하지 않고 모든 버튼 놓임을 그대로 넘기면 됩니다.
 */
void menu_mouse_up(void);

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
 * This is what ESC does. From ::MENU_CLOSED it opens home; from a sub-screen it
 * returns home; from home it closes -- unless home is ::MENU_TITLE, where there
 * is nothing underneath to close onto and it does nothing at all.
 *
 * @note ESC therefore never quits the game. Leaving is a menu row the player
 *       has to choose, which is the whole point of this module -- one
 *       mistaken keypress used to end a run outright.
 * @note Which screen is home is ::menu_open_title's, not this function's. Every
 *       step here is expressed against it rather than against ::MENU_ROOT by
 *       name, so the title screen needed no branch of its own.
 *
 * 한국어
 * ------
 * @brief 메뉴를 열거나, 이미 열려 있으면 한 화면 뒤로 물러납니다.
 *
 * ESC가 하는 일입니다. ::MENU_CLOSED에서는 집을 열고, 하위 화면에서는 집으로 돌아가며,
 * 집에서는 닫습니다. 다만 집이 ::MENU_TITLE이면 닫고 갈 아래가 없으므로 아무 일도 하지
 * 않습니다.
 *
 * @note 어느 화면이 집인가는 이 함수가 아니라 ::menu_open_title의 것입니다. 이곳의 모든 단계가
 *       ::MENU_ROOT을 이름으로 지목하지 않고 집을 기준으로 표현되므로, 타이틀 화면은 자기
 *       분기를 필요로 하지 않았습니다.
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
 * @note A LOCKED ROW IS THE ONE EXCEPTION, and it is inert on purpose: the row
 *       is drawn dim precisely so that pressing it and getting nothing is the
 *       answer the player was already expecting. See ::menu_row_locked.
 *
 * 한국어
 * ------
 * @brief ENTER처럼 강조된 행을 실행합니다.
 * @note 값을 가진 행에서는 +1을 준 ::menu_adjust와 동일하므로, ENTER가 아무 일도 하지
 *       않는 대신 설정을 순환시킵니다. 주 실행 키가 대부분의 행에서 반응하지 않는
 *       메뉴는 플레이어에게 고장 났다고 가르칩니다.
 * @note *잠긴 행이 유일한 예외이며* 의도적으로 반응하지 않습니다. 행이 흐리게 그려지는 것은
 *       바로, 눌렀는데 아무 일도 없는 것이 플레이어가 이미 예상하고 있던 답이 되게 하기
 *       위함입니다. ::menu_row_locked를 참조하십시오.
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
 * @note For the caller to use after it has carried out a restart, or after it
 *       has begun the run a ::MENU_TITLE row asked for, so the player lands
 *       back in the game rather than in the menu they just used.
 * @note ALSO PUTS HOME BACK TO ::MENU_ROOT, because "closed" and "home is the
 *       title" cannot both be true: the title is up precisely when there is no
 *       run behind the menu, and a closed menu is a run being played.
 *
 * 한국어
 * ------
 * @brief 아무 동작도 하지 않고 메뉴를 닫습니다.
 * @note 호출자가 재시작을 수행한 뒤, 또는 ::MENU_TITLE의 행이 요청한 플레이를 시작한 뒤
 *       사용하기 위한 것입니다. 그래야 플레이어가 방금 사용한 메뉴가 아니라 게임으로
 *       돌아갑니다.
 * @note *집도 ::MENU_ROOT으로 되돌립니다.* "닫힘"과 "집이 타이틀"은 동시에 참일 수 없습니다.
 *       타이틀이 떠 있는 것은 정확히 메뉴 뒤에 플레이가 없을 때이고, 닫힌 메뉴는 플레이
 *       중이라는 뜻입니다.
 */
void menu_close(void);

#endif
