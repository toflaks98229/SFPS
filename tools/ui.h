/**
 * @file ui.h
 * @brief An immediate-mode widget layer for the editor tools.
 *
 * ENGLISH
 * -------
 * mapedit already had a *drawing* layer -- ::draw_text plus a quad -- and no
 * *widget* layer, so every piece of state it could not express as a keystroke
 * simply had no UI. That is why `hurt` and the point lights were uneditable,
 * and it is why the level's `next` had to be preserved blind through a save.
 *
 * This is the missing half: buttons, draggable numbers, text fields, scrolling
 * lists and collapsible sections, in the style every 3D editor's inspector
 * uses.
 *
 * ### Why immediate mode
 *
 * A retained-mode toolkit needs a widget tree, and a widget tree needs to be
 * kept in step with the thing it is editing. That synchronisation is the entire
 * bug surface of editor UI: a panel showing a sector that was just deleted, or
 * a field holding a value the level no longer has. Immediate mode has no tree
 * to fall out of step -- `ui_drag_short(&s->floor, ...)` reads and writes the
 * live struct, so the widget cannot disagree with the data because it *is* the
 * data. The same argument the rest of this project keeps making about second
 * copies of a rule.
 *
 * ### Why hand-written rather than a library
 *
 * Nuklear would have worked and is one header. It is also 25,000 lines, which
 * would make it by far the largest file here, and it brings its own font,
 * styling and buffer abstractions that would sit alongside the ones this
 * project already has rather than using them. The expensive parts of a GUI
 * toolkit are variable-width text layout and arbitrary docking; this editor has
 * a fixed-width bitmap font and vertical stacks, so what is left is small.
 *
 * ### The GL split, and why it matters
 *
 * **Every widget function is pure state and arithmetic. Only ::ui_end touches
 * GL.** Widgets append to a command list and mutate the caller's data; nothing
 * before ::ui_end binds, uploads or draws. That is what makes tools/uitest.c
 * possible: it feeds synthetic input, calls the widgets, asserts what they
 * returned, and never opens a window -- the same split player.c, enemy.c and
 * hook.c are built on, applied to the one part of this project that had no
 * headless test at all because "it is a GUI" sounded like a reason.
 *
 * @note Not shipped. This lives in tools/ and is compiled only into the
 *       editors, so it costs the 1.44MB budget nothing.
 *
 * 한국어
 * ------
 * mapedit에는 *그리기* 레이어(::draw_text와 사각형 하나)는 있었지만 *위젯* 레이어가
 * 없었습니다. 그래서 키 입력으로 표현할 수 없는 상태는 UI가 아예 없었습니다. `hurt`와
 * 점광원을 편집할 수 없었던 이유이고, 레벨의 `next`를 저장 과정에서 보이지 않게 보존해야
 * 했던 이유입니다.
 *
 * 이 파일이 빠져 있던 나머지 절반입니다. 버튼, 드래그 가능한 숫자, 텍스트 필드, 스크롤
 * 목록, 접이식 섹션이며, 모든 3D 에디터의 인스펙터가 쓰는 방식입니다.
 *
 * ### 왜 즉시 모드인가
 *
 * 유지 모드 툴킷은 위젯 트리가 필요하고, 위젯 트리는 편집 대상과 동기화되어야 합니다. 그
 * 동기화가 에디터 UI 버그의 전부입니다. 방금 삭제된 섹터를 보여 주는 패널이나, 레벨에
 * 더 이상 없는 값을 들고 있는 필드 같은 것들입니다. 즉시 모드에는 어긋날 트리가 없습니다.
 * `ui_drag_short(&s->floor, ...)`는 살아 있는 구조체를 읽고 쓰므로, 위젯이 데이터와
 * 어긋날 수 없습니다. 위젯이 곧 데이터이기 때문입니다. 이 프로젝트가 규칙의 두 번째
 * 사본에 대해 반복해 온 것과 같은 논거입니다.
 *
 * ### 왜 라이브러리가 아니라 직접 작성했는가
 *
 * Nuklear도 동작했을 것이고 헤더 하나입니다. 동시에 25,000줄이며, 그러면 이곳에서 압도적
 * 최대 파일이 되고, 이 프로젝트가 이미 가진 것들 옆에 자체 폰트·스타일·버퍼 추상화를
 * 나란히 놓게 됩니다. GUI 툴킷에서 비싼 부분은 가변폭 텍스트 레이아웃과 임의 도킹인데, 이
 * 에디터는 고정폭 비트맵 폰트와 세로 스택을 쓰므로 남는 것이 적습니다.
 *
 * ### GL 분리, 그리고 그것이 중요한 이유
 *
 * **모든 위젯 함수는 순수한 상태와 산술이며, GL을 건드리는 것은 ::ui_end뿐입니다.**
 * 위젯은 명령 목록에 추가하고 호출자의 데이터를 수정할 뿐, ::ui_end 이전에는 무엇도
 * 바인딩·업로드·그리기를 하지 않습니다. 그것이 tools/uitest.c를 가능하게 합니다. 합성
 * 입력을 넣고, 위젯을 호출하고, 반환값을 단언하며, 창을 열지 않습니다. player.c, enemy.c,
 * hook.c가 기반한 것과 같은 분리를, "GUI니까"라는 말이 이유처럼 들려서 헤드리스 테스트가
 * 전혀 없었던 유일한 부분에 적용한 것입니다.
 *
 * @note 배포되지 않습니다. tools/에 있으며 에디터에만 컴파일되므로 1.44MB 예산에 비용이
 *       없습니다.
 */
#ifndef UI_H
#define UI_H

/* --- Capacity limits / 용량 제한 --- */

#define UI_MAX_RECTS  512   ///< @brief Rectangles queued per frame. / 프레임당 대기하는 사각형 수.
#define UI_MAX_TEXTS  256   ///< @brief Strings queued per frame. / 프레임당 대기하는 문자열 수.
#define UI_MAX_CLIPS   16   ///< @brief Nested/sequential scroll regions per frame. / 프레임당 스크롤 영역 수.
#define UI_TEXT_LEN    64   ///< @brief Longest string a widget may draw. / 위젯이 그릴 수 있는 최대 문자열 길이.
#define UI_EDIT_LEN    64   ///< @brief Longest value the keyboard editor holds. / 키보드 편집기가 담는 최대 길이.

/* --- Metrics / 치수 --- */

#define UI_ROW_H      18.0f  ///< @brief Height of one widget row, pixels. / 위젯 한 행의 높이 (픽셀).
#define UI_PAD         6.0f  ///< @brief Inner padding. / 내부 여백.
#define UI_TEXT_SIZE   1.0f  ///< @brief Glyph scale for widget labels. / 위젯 라벨의 글리프 배율.
#define UI_LABEL_W    76.0f  ///< @brief Width reserved for a field's label. / 필드 라벨에 할당된 너비.

/**
 * @brief A widget identity.
 *
 * ENGLISH
 * -------
 * Immediate mode has no widget objects, so "which control is the mouse holding"
 * has to be answered by a value the caller reproduces every frame. The line
 * number does that for a widget written once; a widget inside a loop adds its
 * index.
 *
 * @warning Two widgets sharing an id will fight over the mouse -- dragging one
 *          drags the other. ::UI_IDX exists so a loop cannot cause that by
 *          accident.
 *
 * 한국어
 * ------
 * 즉시 모드에는 위젯 객체가 없으므로, "마우스가 어떤 컨트롤을 잡고 있는가"는 호출자가 매
 * 프레임 재현하는 값으로 답해야 합니다. 한 번만 작성된 위젯은 줄 번호로 충분하고, 루프
 * 안의 위젯은 인덱스를 더합니다.
 *
 * @warning 같은 id를 공유하는 두 위젯은 마우스를 두고 다툽니다. 하나를 끌면 다른 하나도
 *          끌립니다. ::UI_IDX는 루프가 실수로 그런 상황을 만들지 않게 합니다.
 */
#define UI_ID       (__LINE__ * 64)
#define UI_IDX(i)   (__LINE__ * 64 + (int)(i) + 1)

/**
 * @struct UiInput
 * @brief One frame of input, gathered by the caller's window procedure.
 *
 * ENGLISH
 * -------
 * Filled once per frame and handed to ::ui_begin. Edge flags (`click`,
 * `release`) are consumed by ::ui_begin, so the caller clears them after the
 * frame rather than trying to guess when a widget used one.
 *
 * 한국어
 * ------
 * 프레임마다 한 번 채워 ::ui_begin에 전달합니다. 엣지 플래그(`click`, `release`)는
 * ::ui_begin이 소비하므로, 호출자는 위젯이 언제 그것을 썼는지 추측하지 않고 프레임이 끝난
 * 뒤에 지웁니다.
 */
typedef struct {
    float mx, my;        /**< Cursor, client pixels. / 커서 (클라이언트 픽셀). */
    int   down;          /**< Left button held. / 좌클릭 유지 상태. */
    int   click;         /**< Left button went down this frame. / 이번 프레임에 좌클릭이 눌렸습니다. */
    int   release;       /**< Left button came up this frame. / 이번 프레임에 좌클릭이 떼어졌습니다. */
    float wheel;         /**< Notches this frame, positive away from the user. / 이번 프레임의 휠 눈금. */
    int   ch;            /**< Printable character typed, or 0. / 입력된 출력 가능 문자. 없으면 0. */
    int   backspace;     /**< Backspace was pressed. / 백스페이스가 눌렸습니다. */
    int   enter;         /**< Enter was pressed: commit an edit. / 엔터가 눌렸습니다. 편집을 확정합니다. */
    int   escape;        /**< Escape was pressed: abandon an edit. / ESC가 눌렸습니다. 편집을 취소합니다. */
} UiInput;

/**
 * @struct Ui
 * @brief The whole state of the widget layer. One instance, owned by the tool.
 *
 * ENGLISH
 * -------
 * @note `hot` and `active` are the two-value core every immediate-mode GUI is
 *       built on. `hot` is what the cursor is over and changes freely; `active`
 *       is what the mouse has grabbed and does NOT change until the button is
 *       released. Without the second, dragging a slider and sliding off it
 *       would hand the drag to whatever the cursor crossed next.
 *
 * 한국어
 * ------
 * @note `hot`과 `active`는 모든 즉시 모드 GUI가 기반하는 두 값입니다. `hot`은 커서가
 *       올라가 있는 대상이며 자유롭게 바뀝니다. `active`는 마우스가 붙잡은 대상이며 버튼을
 *       놓기 전까지 바뀌지 *않습니다*. 두 번째가 없으면 슬라이더를 끌다가 벗어났을 때 커서가
 *       지나친 다른 것에 드래그가 넘어갑니다.
 */
typedef struct {
    UiInput in;          /**< This frame's input. / 이번 프레임의 입력. */

    int   hot;           /**< Widget under the cursor, or 0. / 커서 아래의 위젯. 없으면 0. */
    int   active;        /**< Widget the mouse has grabbed, or 0. / 마우스가 붙잡은 위젯. 없으면 0. */
    float drag_x;        /**< Cursor x when the grab began. / 붙잡기 시작 시점의 커서 x. */
    float drag_acc;      /**< Sub-step drag remainder, so slow drags still move. / 스텝 미만의 드래그 잔량. 느린 드래그도 값을 움직이게 합니다. */

    /* --- keyboard editing --- */
    int   edit;          /**< Widget being typed into, or 0. / 타이핑 중인 위젯. 없으면 0. */
    char  edit_buf[UI_EDIT_LEN];  /**< Text being edited. / 편집 중인 텍스트. */
    int   edit_len;      /**< Characters in `edit_buf`. / `edit_buf`의 문자 수. */

    /**
     * @brief The edit was just opened and its whole contents count as selected.
     *
     * ENGLISH
     * -------
     * A field opens showing what it already holds, so you can see the value you
     * are replacing. Typing must then REPLACE it rather than append: clicking a
     * ceiling of 450 and typing 300 has to give 300, not 450300 -- which the
     * range then clamps to the maximum, so a mistyped height silently becomes
     * the tallest room the format allows.
     *
     * This is what every inspector does by selecting the text on focus. There
     * is no selection model here and none is needed for one flag: the first
     * printable character clears the buffer, and after that editing is normal.
     *
     * 한국어
     * ------
     * @brief 편집이 방금 열렸으며 전체 내용이 선택된 것으로 간주됩니다.
     *
     * 필드는 이미 담고 있는 값을 보여 주며 열립니다. 무엇을 대체하는지 볼 수 있어야 하기
     * 때문입니다. 그렇다면 입력은 덧붙이는 것이 아니라 *대체*해야 합니다. 천장 450을
     * 클릭하고 300을 입력하면 450300이 아니라 300이 되어야 하며, 전자는 범위 제한에 걸려
     * 최댓값이 되므로 잘못 입력한 높이가 조용히 포맷이 허용하는 가장 높은 방이 됩니다.
     *
     * 모든 인스펙터가 포커스 시 텍스트를 선택하여 하는 일입니다. 이곳에는 선택 모델이
     * 없고 플래그 하나에는 필요하지도 않습니다. 첫 출력 가능 문자가 버퍼를 비우고, 그
     * 이후로는 평범한 편집입니다.
     */
    int   edit_fresh;

    /* --- layout --- */
    float x, y;          /**< Layout cursor, top-left of the next row. / 배치 커서. 다음 행의 좌상단. */
    float w;             /**< Width available to a widget. / 위젯이 사용할 수 있는 너비. */
    float col_x, col_w;  /**< Current panel's origin and width. / 현재 패널의 원점과 너비. */

    /* --- clipping, for scroll regions ---
       Recorded per command rather than applied when the widget runs, because
       nothing is drawn until ::ui_end -- by then the scroll region that set the
       rect has long since ended. `clip` on each command is an index into this
       table, or -1 for none.
       위젯이 실행될 때 적용하지 않고 명령마다 기록합니다. ::ui_end 전에는 아무것도 그리지
       않는데, 그 시점이면 사각형을 설정한 스크롤 영역은 이미 오래전에 끝나 있기
       때문입니다. 각 명령의 `clip`은 이 표의 인덱스이며, 없으면 -1입니다. */
    struct { float x, y, w, h; } clips[UI_MAX_CLIPS];
    int   n_clips;
    int   clip;          /**< Clip index in force right now, or -1. / 현재 적용 중인 클립 인덱스. 없으면 -1. */

    /* --- queued drawing, flushed by ui_end --- */
    struct {
        float x, y, w, h, r, g, b, a;
        int   clip;
    } rect[UI_MAX_RECTS];
    int n_rects;

    struct {
        float x, y, size, r, g, b;
        int   clip;
        char  s[UI_TEXT_LEN];
    } text[UI_MAX_TEXTS];
    int n_texts;

    int   vw, vh;        /**< Viewport, for the ortho projection. / 뷰포트. 정사영 투영에 사용됩니다. */
    int   overflow;      /**< A queue filled up this frame. / 이번 프레임에 큐가 가득 찼습니다. */
} Ui;

/* --- Frame lifecycle / 프레임 수명 주기 --- */

/**
 * @brief Starts a frame: takes the input and clears the draw queues.
 *
 * @param[in,out] u  Widget state.
 * @param[in]     vw Viewport width in pixels.
 * @param[in]     vh Viewport height in pixels.
 * @param[in]     in This frame's input.
 * @note Touches no GL.
 *
 * @brief 프레임을 시작합니다. 입력을 받고 그리기 큐를 비웁니다.
 */
void ui_begin(Ui *u, int vw, int vh, const UiInput *in);

/**
 * @brief Ends a frame and draws everything the widgets queued.
 *
 * @param[in,out] u Widget state.
 * @warning The ONLY function here that touches GL. Requires a current context,
 *          the renderer's program bound (::rd_use), and ::font_init to have run.
 * @note Draws every rectangle first and every string second, so a label always
 *       lands on top of its own background regardless of the order the widgets
 *       queued them in. Nothing in an inspector wants text underneath a fill.
 *
 * @brief 프레임을 끝내고 위젯이 대기시킨 모든 것을 그립니다.
 * @warning 이곳에서 GL을 건드리는 *유일한* 함수입니다.
 */
void ui_end(Ui *u);

/**
 * @brief Whether the cursor is over any widget this frame.
 *
 * @param[in] u Widget state.
 * @return Non-zero when the UI owns the cursor.
 * @note The editor asks this before acting on a click in the plan or 3D view,
 *       so pressing a button does not also drag the geometry behind it.
 *
 * @brief 이번 프레임에 커서가 위젯 위에 있는지 여부입니다.
 * @note 에디터는 평면도나 3D 뷰에서 클릭을 처리하기 전에 이것을 확인하여, 버튼을 누르는
 *       동작이 그 뒤의 지오메트리까지 끌지 않게 합니다.
 */
int ui_wants_mouse(const Ui *u);

/**
 * @brief Whether a widget is currently taking keystrokes.
 *
 * @param[in] u Widget state.
 * @return Non-zero while a text or number field is being typed into.
 * @note The editor's single-key shortcuts (N, V, E, M...) must be suppressed
 *       while this is true, or typing a level name would delete a vertex.
 *
 * @brief 위젯이 현재 키 입력을 받고 있는지 여부입니다.
 * @note 이 값이 참인 동안에는 에디터의 단일 키 단축키(N, V, E, M 등)를 억제해야 합니다.
 *       그렇지 않으면 레벨 이름을 입력하는 것이 정점을 삭제하게 됩니다.
 */
int ui_wants_keys(const Ui *u);

/* --- Layout / 배치 --- */

/**
 * @brief Begins a panel: fills its background and puts the layout cursor inside.
 *
 * @param[in,out] u Widget state.
 * @param[in] x,y   Top-left in client pixels.
 * @param[in] w,h   Size in client pixels.
 * @param[in] a     Background alpha; 0 draws no background.
 *
 * @brief 패널을 시작합니다. 배경을 채우고 배치 커서를 그 안에 놓습니다.
 */
void ui_panel(Ui *u, float x, float y, float w, float h, float a);

/** @brief Moves the layout cursor down by `px`. / 배치 커서를 `px`만큼 아래로 옮깁니다. */
void ui_space(Ui *u, float px);

/** @brief Draws a horizontal rule and advances. / 가로 구분선을 그리고 커서를 진행시킵니다. */
void ui_separator(Ui *u);

/** @brief The layout cursor's current y, for a caller measuring its own region. / 배치 커서의 현재 y. */
float ui_cursor_y(const Ui *u);

/* --- Scrolling / 스크롤 --- */

/**
 * @brief Begins a clipped, scrollable region `h` pixels tall.
 *
 * @param[in,out] u      Widget state.
 * @param[in]     h      Visible height in pixels.
 * @param[in,out] scroll Caller-owned scroll offset, advanced by the wheel.
 * @note Pair with ::ui_scroll_end. Content taller than `h` is clipped by a
 *       scissor rect rather than by skipping widgets, so a partially visible
 *       row still hit-tests correctly against the part you can see.
 *
 * @brief `h` 픽셀 높이의 잘린 스크롤 영역을 시작합니다.
 * @note ::ui_scroll_end와 짝을 이룹니다.
 */
void ui_scroll_begin(Ui *u, float h, float *scroll);

/** @brief Ends the region ::ui_scroll_begin started. / ::ui_scroll_begin이 시작한 영역을 끝냅니다. */
void ui_scroll_end(Ui *u);

/* --- Widgets / 위젯 --- */

/** @brief Draws a line of text at the cursor. / 커서 위치에 텍스트 한 줄을 그립니다. */
void ui_label(Ui *u, const char *s);

/** @brief Draws a dim line of text: a heading or a hint. / 흐린 텍스트 한 줄을 그립니다. 제목이나 안내입니다. */
void ui_label_dim(Ui *u, const char *s);

/**
 * @brief A clickable button.
 * @return Non-zero on the frame it is released over.
 * @note Fires on RELEASE, not press, so sliding off a button cancels it -- the
 *       behaviour every desktop toolkit has and the one that makes a misclick
 *       recoverable.
 *
 * @brief 클릭 가능한 버튼입니다.
 * @note 누를 때가 아니라 *뗄 때* 발동하므로, 버튼에서 미끄러져 나가면 취소됩니다.
 */
int ui_button(Ui *u, int id, const char *label);

/**
 * @brief A button laid out in a horizontal strip of `n`, at index `i`.
 * @param[in] on Non-zero to draw it as the selected one of the group.
 * @return Non-zero when clicked.
 * @note For toolbars and mode selectors, where the row is one control.
 *
 * @brief `n`개의 가로 띠 중 `i`번째에 배치되는 버튼입니다. 툴바와 모드 선택기용입니다.
 */
int ui_button_strip(Ui *u, int id, const char *label, int i, int n, int on);

/** @brief A labelled on/off box. Returns non-zero when toggled. / 라벨이 붙은 켬/끔 상자입니다. */
int ui_checkbox(Ui *u, int id, const char *label, int *v);

/**
 * @brief A number you scrub by dragging, or type into by clicking.
 *
 * ENGLISH
 * -------
 * @param[in]     label Field name, drawn left of the value.
 * @param[in,out] v     The value, read and written in place.
 * @param[in]     lo    Minimum, inclusive.
 * @param[in]     hi    Maximum, inclusive.
 * @param[in]     step  Units per pixel of horizontal drag.
 * @return Non-zero on any frame the value changed.
 *
 * @note Drag AND type, because the two are for different jobs: dragging is how
 *       you find a height by watching the room, and typing is how you set one
 *       to the same number as the sector next door. An inspector with only one
 *       of them makes the other job clumsy.
 * @note Clamped to [lo,hi] on both paths, so a typed value cannot put the level
 *       somewhere a dragged one could not reach.
 *
 * 한국어
 * ------
 * @brief 끌어서 조절하거나 클릭해 입력하는 숫자입니다.
 * @note 드래그와 입력을 모두 지원하는 이유는 둘의 용도가 다르기 때문입니다. 드래그는 방을
 *       보면서 높이를 찾는 방법이고, 입력은 옆 섹터와 같은 값으로 맞추는 방법입니다.
 *       하나만 있는 인스펙터는 나머지 작업을 번거롭게 만듭니다.
 */
int ui_drag_int(Ui *u, int id, const char *label, int *v, int lo, int hi, float step);

/**
 * @brief ::ui_drag_int against a `short`, which is what the level format stores.
 * @note A wrapper rather than a separate implementation: the level's
 *       coordinates, heights, hazard rates and light fields are all `short`,
 *       and having the inspector cast at every call site is how one of them
 *       ends up truncating differently from the others.
 *
 * @brief `short`에 대한 ::ui_drag_int입니다. 레벨 포맷이 저장하는 타입입니다.
 */
int ui_drag_short(Ui *u, int id, const char *label, short *v, int lo, int hi, float step);

/**
 * @brief An editable text field.
 * @param[in,out] buf Caller's buffer, read and written in place.
 * @param[in]     cap Its capacity including the terminator.
 * @return Non-zero on the frame the edit is committed with Enter.
 * @note Commits on Enter and abandons on Escape, so a half-typed level name
 *       never reaches the data.
 *
 * @brief 편집 가능한 텍스트 필드입니다. 엔터로 확정하고 ESC로 취소합니다.
 */
int ui_text_field(Ui *u, int id, const char *label, char *buf, int cap);

/**
 * @brief A collapsible section header.
 * @param[in,out] open Caller-owned open/closed flag, toggled by a click.
 * @return The new value of `*open`, so the caller can `if (ui_section(...))`.
 *
 * @brief 접이식 섹션 헤더입니다.
 */
int ui_section(Ui *u, int id, const char *label, int *open);

/**
 * @brief A row in a list.
 * @param[in] selected Non-zero to draw it highlighted.
 * @return Non-zero when clicked.
 *
 * @brief 목록의 한 행입니다.
 */
int ui_list_item(Ui *u, int id, const char *label, int selected);

/**
 * @brief An RGB triple shown as a swatch with three draggable channels.
 * @return Non-zero on any frame a channel changed.
 * @note The swatch is the point: three numbers do not tell you what colour a
 *       light is, and a light is the one thing in this format you cannot judge
 *       from its values.
 *
 * @brief 견본과 함께 표시되는 RGB 3채널입니다. 조명은 값만으로는 판단할 수 없습니다.
 */
int ui_color_rgb(Ui *u, int id, const char *label, short *r, short *g, short *b);

#endif
