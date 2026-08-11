/**
 * @file ui.c
 * @brief The widget layer's implementation. Only ::ui_end touches GL.
 *
 * ENGLISH
 * -------
 * The hot/active protocol every immediate-mode GUI is built on, a vertical
 * layout cursor, and a command list that ::ui_end turns into draw calls. See
 * ui.h for why this is hand-written rather than a library.
 *
 * 한국어
 * ------
 * 모든 즉시 모드 GUI가 기반하는 hot/active 규약, 세로 배치 커서, 그리고 ::ui_end가 그리기
 * 명령으로 바꾸는 명령 목록입니다. 라이브러리 대신 직접 작성한 이유는 ui.h를 참조하십시오.
 */

#include "ui.h"

/* The renderer's CPU-side half and the font atlas. Needed only by ui_end; every
   widget above it is arithmetic.
   렌더러의 CPU 측 절반과 폰트 아틀라스입니다. ui_end에만 필요하며, 그 위의 모든 위젯은
   산술 연산입니다. */
#include "../src/render.h"
#include "../src/font.h"
#include "../src/txt.h"

/* ------------------------------------------------------------------ theme */

/* One place decides what the editor looks like. These were going to be
   scattered through the widgets as literals, which is how a "disabled" grey and
   a "dim label" grey drift half a percent apart and the panel starts looking
   accidental.
   에디터의 외형을 결정하는 곳은 한 군데입니다. 이 값들은 위젯 곳곳에 리터럴로 흩어질
   뻔했는데, 그렇게 하면 "비활성" 회색과 "흐린 라벨" 회색이 0.5% 어긋나기 시작하고 패널이
   의도하지 않은 모양으로 보이게 됩니다. */
#define C_PANEL_R 0.09f
#define C_PANEL_G 0.10f
#define C_PANEL_B 0.13f

#define C_FIELD_R 0.16f
#define C_FIELD_G 0.17f
#define C_FIELD_B 0.21f

#define C_HOT_R   0.24f
#define C_HOT_G   0.26f
#define C_HOT_B   0.32f

#define C_ACT_R   0.35f
#define C_ACT_G   0.55f
#define C_ACT_B   0.85f

#define C_TEXT_R  0.86f
#define C_TEXT_G  0.88f
#define C_TEXT_B  0.92f

#define C_DIM_R   0.55f
#define C_DIM_G   0.58f
#define C_DIM_B   0.64f

#define C_ACCENT_R 1.00f
#define C_ACCENT_G 0.84f
#define C_ACCENT_B 0.35f

/* --------------------------------------------------------------- plumbing */

static void push_rect(Ui *u, float x, float y, float w, float h,
                      float r, float g, float b, float a) {
    if (u->n_rects >= UI_MAX_RECTS) { u->overflow = 1; return; }
    int i = u->n_rects++;
    u->rect[i].x = x; u->rect[i].y = y;
    u->rect[i].w = w; u->rect[i].h = h;
    u->rect[i].r = r; u->rect[i].g = g; u->rect[i].b = b; u->rect[i].a = a;
    u->rect[i].clip = u->clip;
}

static void push_text(Ui *u, float x, float y, float size,
                      float r, float g, float b, const char *s) {
    if (u->n_texts >= UI_MAX_TEXTS) { u->overflow = 1; return; }
    int i = u->n_texts++;
    u->text[i].x = x; u->text[i].y = y; u->text[i].size = size;
    u->text[i].r = r; u->text[i].g = g; u->text[i].b = b;
    u->text[i].clip = u->clip;
    txt_copy(u->text[i].s, UI_TEXT_LEN, s, -1);
}

/* Whether the cursor is inside a rect AND inside whatever clip is in force.
   A row scrolled half out of a list must not respond on the half you cannot
   see, or clicking near a list's edge selects something off-screen.
   커서가 사각형 안에 있고 *동시에* 현재 적용 중인 클립 안에도 있는지 여부입니다. 목록에서
   절반쯤 잘려 나간 행은 보이지 않는 절반에서 반응해서는 안 됩니다. 그렇지 않으면 목록
   가장자리를 클릭했을 때 화면 밖의 것이 선택됩니다. */
static int hit(const Ui *u, float x, float y, float w, float h) {
    float mx = u->in.mx, my = u->in.my;
    if (mx < x || mx >= x + w || my < y || my >= y + h) return 0;
    if (u->clip >= 0) {
        const float cx = u->clips[u->clip].x, cy = u->clips[u->clip].y;
        const float cw = u->clips[u->clip].w, ch = u->clips[u->clip].h;
        if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) return 0;
    }
    return 1;
}

/**
 * @brief The hot/active handshake, shared by every clickable widget.
 *
 * ENGLISH
 * -------
 * @return 1 on the frame the widget is released while still under the cursor.
 *
 * Firing on RELEASE rather than press is what makes a misclick recoverable:
 * press, notice it is the wrong button, slide off, let go, nothing happens.
 * Every desktop toolkit behaves this way and users rely on it without knowing
 * they do.
 *
 * 한국어
 * ------
 * @brief 모든 클릭 가능한 위젯이 공유하는 hot/active 절차입니다.
 *
 * 누를 때가 아니라 *뗄 때* 발동하는 것이 잘못된 클릭을 되돌릴 수 있게 합니다. 누르고,
 * 잘못된 버튼임을 알아채고, 밖으로 미끄러뜨리고, 놓으면 아무 일도 일어나지 않습니다.
 */
static int clickable(Ui *u, int id, float x, float y, float w, float h) {
    int inside = hit(u, x, y, w, h);
    int fired = 0;

    if (u->active == id) {
        if (u->in.release) {
            if (inside) fired = 1;
            u->active = 0;
        }
    } else if (u->active == 0 && inside && u->in.click) {
        u->active = id;
    }
    if (inside && u->active == 0) u->hot = id;
    else if (inside && u->active == id) u->hot = id;
    return fired;
}

/* Integer to text, without stdio. Returns the length written.
   stdio 없이 정수를 텍스트로 변환합니다. */
static int int_str(int v, char *out, int cap) {
    char tmp[16];
    int n = 0, neg = v < 0;
    unsigned a = neg ? (unsigned)(-(long long)v) : (unsigned)v;
    if (!a) tmp[n++] = '0';
    while (a && n < 15) { tmp[n++] = (char)('0' + a % 10); a /= 10; }
    int i = 0;
    if (neg && i < cap - 1) out[i++] = '-';
    while (n > 0 && i < cap - 1) out[i++] = tmp[--n];
    out[i] = 0;
    return i;
}

/* Appends "label" then the value, into a caller buffer. Used by the fields so
   the label and the value are one string and therefore one draw.
   라벨과 값을 호출자 버퍼에 이어 붙입니다. */
static void field_text(char *out, int cap, const char *label, int v) {
    int i = txt_copy(out, cap, label, -1);
    if (i < cap - 1) out[i++] = ' ';
    int_str(v, out + i, cap - i);
}

/* --------------------------------------------------------- frame lifecycle */

void ui_begin(Ui *u, int vw, int vh, const UiInput *in) {
    u->in = *in;
    u->vw = vw;
    u->vh = vh;
    u->n_rects = 0;
    u->n_texts = 0;
    u->n_clips = 0;
    u->clip = -1;
    u->hot = 0;
    u->overflow = 0;

    /* Release the grab only on a frame with NO release edge left to deliver.
     *
     * ENGLISH
     * -------
     * This exists for one case: a widget that grabbed the mouse and then
     * stopped being drawn -- a collapsed section, or a sector deleted while its
     * field was held. Nothing would ever clear `active` for it, and the UI
     * would refuse every later click because it believed the mouse was still
     * down on something that no longer exists.
     *
     * The `!release` term is load-bearing and was missing. A release frame has
     * `down` and `click` both clear, so without it this cleared `active` before
     * any widget ran -- and a button fires by seeing `active == id` on exactly
     * that frame. Every button in the editor would have been dead: pressable,
     * highlightable, and incapable of firing. uitest caught it on the second
     * assertion.
     *
     * 한국어
     * ------
     * 전달할 뗌 엣지가 남아 있지 *않은* 프레임에서만 붙잡기를 해제합니다.
     *
     * 이 코드는 한 가지 경우를 위해 존재합니다. 마우스를 붙잡은 위젯이 더 이상 그려지지
     * 않게 된 경우입니다. 접힌 섹션이나, 필드를 누른 채 삭제된 섹터가 그렇습니다. 그러면
     * 아무도 `active`를 지워 주지 않고, UI는 이제 존재하지 않는 무언가를 마우스가 여전히
     * 누르고 있다고 믿어 이후의 모든 클릭을 거부합니다.
     *
     * `!release` 항은 구조적으로 중요하며 빠져 있었습니다. 뗌 프레임은 `down`과 `click`이
     * 모두 해제된 상태이므로, 이 항이 없으면 위젯이 실행되기 *전에* `active`가 지워집니다.
     * 그런데 버튼은 바로 그 프레임에 `active == id`를 확인하여 발동합니다. 에디터의 모든
     * 버튼이 죽어 있었을 것입니다. 누를 수 있고, 강조도 되지만, 발동은 못 하는 상태로
     * 말입니다. uitest가 두 번째 단언에서 이를 잡아냈습니다. */
    if (!u->in.down && !u->in.click && !u->in.release) u->active = 0;

    u->x = u->col_x = 0.0f;
    u->y = 0.0f;
    u->w = u->col_w = (float)vw;
}

int ui_wants_mouse(const Ui *u) { return u->hot != 0 || u->active != 0; }
int ui_wants_keys (const Ui *u) { return u->edit != 0; }

/* ------------------------------------------------------------- layout */

void ui_panel(Ui *u, float x, float y, float w, float h, float a) {
    if (a > 0.0f) push_rect(u, x, y, w, h, C_PANEL_R, C_PANEL_G, C_PANEL_B, a);
    u->col_x = x + UI_PAD;
    u->col_w = w - UI_PAD * 2.0f;
    u->x = u->col_x;
    u->w = u->col_w;
    u->y = y + UI_PAD;
}

void ui_space(Ui *u, float px) { u->y += px; }

void ui_separator(Ui *u) {
    u->y += 4.0f;
    push_rect(u, u->x, u->y, u->w, 1.0f, C_DIM_R, C_DIM_G, C_DIM_B, 0.35f);
    u->y += 5.0f;
}

float ui_cursor_y(const Ui *u) { return u->y; }

/* ------------------------------------------------------------- scrolling */

void ui_scroll_begin(Ui *u, float h, float *scroll) {
    /* The wheel scrolls only while the cursor is actually over the region --
       otherwise a wheel meant for the 3D view's height tool would also scroll
       whatever list happened to be on screen.
       커서가 실제로 영역 위에 있을 때만 휠이 스크롤합니다. 그렇지 않으면 3D 뷰의 높이
       조절을 위한 휠이 화면에 있던 목록까지 함께 스크롤하게 됩니다. */
    if (hit(u, u->x, u->y, u->w, h) && u->in.wheel != 0.0f) {
        *scroll -= u->in.wheel * UI_ROW_H * 3.0f;
        if (*scroll < 0.0f) *scroll = 0.0f;
    }

    if (u->n_clips < UI_MAX_CLIPS) {
        int c = u->n_clips++;
        u->clips[c].x = u->x;
        u->clips[c].y = u->y;
        u->clips[c].w = u->w;
        u->clips[c].h = h;
        u->clip = c;
    }
    /* Remember where the viewport started so ui_scroll_end can restore the
       cursor to below it regardless of how tall the content turned out.
       내용이 얼마나 길어졌든 ui_scroll_end가 커서를 그 아래로 되돌릴 수 있도록 뷰포트
       시작 위치를 기억합니다. */
    u->clips[u->clip].y = u->y;
    u->y -= *scroll;
}

void ui_scroll_end(Ui *u) {
    if (u->clip >= 0) u->y = u->clips[u->clip].y + u->clips[u->clip].h + 2.0f;
    u->clip = -1;
}

/* --------------------------------------------------------------- widgets */

void ui_label(Ui *u, const char *s) {
    push_text(u, u->x, u->y + 4.0f, UI_TEXT_SIZE, C_TEXT_R, C_TEXT_G, C_TEXT_B, s);
    u->y += UI_ROW_H;
}

void ui_label_dim(Ui *u, const char *s) {
    push_text(u, u->x, u->y + 4.0f, UI_TEXT_SIZE, C_DIM_R, C_DIM_G, C_DIM_B, s);
    u->y += UI_ROW_H - 3.0f;
}

int ui_button(Ui *u, int id, const char *label) {
    float x = u->x, y = u->y, w = u->w, h = UI_ROW_H;
    int fired = clickable(u, id, x, y, w, h);

    int down = (u->active == id);
    int over = (u->hot == id);
    push_rect(u, x, y, w, h,
              down ? C_ACT_R   : over ? C_HOT_R   : C_FIELD_R,
              down ? C_ACT_G   : over ? C_HOT_G   : C_FIELD_G,
              down ? C_ACT_B   : over ? C_HOT_B   : C_FIELD_B, 1.0f);
    push_text(u, x + UI_PAD, y + 4.0f, UI_TEXT_SIZE,
              C_TEXT_R, C_TEXT_G, C_TEXT_B, label);
    u->y += h + 2.0f;
    return fired;
}

int ui_button_strip(Ui *u, int id, const char *label, int i, int n, int on) {
    if (n < 1) n = 1;
    float cw = (u->w - 2.0f * (n - 1)) / n;
    float x = u->x + i * (cw + 2.0f), y = u->y, h = UI_ROW_H;
    int fired = clickable(u, id, x, y, cw, h);

    int over = (u->hot == id);
    push_rect(u, x, y, cw, h,
              on ? C_ACT_R : over ? C_HOT_R : C_FIELD_R,
              on ? C_ACT_G : over ? C_HOT_G : C_FIELD_G,
              on ? C_ACT_B : over ? C_HOT_B : C_FIELD_B, 1.0f);
    push_text(u, x + 5.0f, y + 4.0f, UI_TEXT_SIZE,
              C_TEXT_R, C_TEXT_G, C_TEXT_B, label);

    /* Only the last cell advances the cursor: the whole strip is one row.
       마지막 칸만 커서를 진행시킵니다. 띠 전체가 한 행입니다. */
    if (i == n - 1) u->y += h + 2.0f;
    return fired;
}

int ui_checkbox(Ui *u, int id, const char *label, int *v) {
    float x = u->x, y = u->y, h = UI_ROW_H, bs = 11.0f;
    int fired = clickable(u, id, x, y, u->w, h);
    if (fired) *v = !*v;

    int over = (u->hot == id);
    push_rect(u, x, y + 3.0f, bs, bs,
              over ? C_HOT_R : C_FIELD_R,
              over ? C_HOT_G : C_FIELD_G,
              over ? C_HOT_B : C_FIELD_B, 1.0f);
    if (*v)
        push_rect(u, x + 3.0f, y + 6.0f, bs - 6.0f, bs - 6.0f,
                  C_ACCENT_R, C_ACCENT_G, C_ACCENT_B, 1.0f);
    push_text(u, x + bs + 6.0f, y + 4.0f, UI_TEXT_SIZE,
              C_TEXT_R, C_TEXT_G, C_TEXT_B, label);
    u->y += h + 2.0f;
    return fired;
}

/* Shared by ui_drag_int and the text-entry path it falls into.
   ui_drag_int과 그것이 진입하는 텍스트 입력 경로가 공유합니다. */
static void edit_begin(Ui *u, int id, int v) {
    u->edit = id;
    u->edit_len = int_str(v, u->edit_buf, UI_EDIT_LEN);
    u->edit_fresh = 1;
}

static void edit_begin_str(Ui *u, int id, const char *s) {
    u->edit = id;
    u->edit_len = txt_copy(u->edit_buf, UI_EDIT_LEN, s, -1);
    u->edit_fresh = 1;
}

/* Feeds one frame of keystrokes into the edit buffer.
   @return 1 committed (Enter), -1 abandoned (Escape), 0 still editing.
   한 프레임의 키 입력을 편집 버퍼에 넣습니다. */
static int edit_step(Ui *u) {
    if (u->in.escape) { u->edit = 0; u->edit_fresh = 0; return -1; }
    if (u->in.enter)  { u->edit = 0; u->edit_fresh = 0; return  1; }

    /* The first keystroke replaces what the field opened with -- see
       ::Ui::edit_fresh. Backspace counts, and clears the whole value rather
       than one character, which is what deleting a selection does.
       첫 키 입력이 필드가 열릴 때의 내용을 대체합니다. ::Ui::edit_fresh를 참조하십시오.
       백스페이스도 해당하며, 한 글자가 아니라 값 전체를 지웁니다. 선택 영역을 삭제하는
       동작이 그렇기 때문입니다. */
    if (u->edit_fresh && (u->in.ch || u->in.backspace)) {
        u->edit_len = 0;
        u->edit_buf[0] = 0;
        u->edit_fresh = 0;
    }

    if (u->in.backspace && u->edit_len > 0) u->edit_buf[--u->edit_len] = 0;
    if (u->in.ch && u->edit_len < UI_EDIT_LEN - 1) {
        u->edit_buf[u->edit_len++] = (char)u->in.ch;
        u->edit_buf[u->edit_len] = 0;
    }
    return 0;
}

int ui_drag_int(Ui *u, int id, const char *label, int *v, int lo, int hi, float step) {
    float x = u->x, y = u->y, w = u->w, h = UI_ROW_H;
    int changed = 0;

    /* --- keyboard entry, once a click has opened it --- */
    if (u->edit == id) {
        int done = edit_step(u);
        if (done == 1) {
            /* Committed. A buffer that is not a number leaves the value alone
               rather than writing 0 -- typing "12x" by accident must not zero a
               ceiling height.
               확정되었습니다. 숫자가 아닌 버퍼는 0을 쓰지 않고 값을 그대로 둡니다. 실수로
               "12x"를 입력한 것이 천장 높이를 0으로 만들어서는 안 됩니다. */
            int n = u->edit_len;
            if (n > 0 && txt_is_number(u->edit_buf, n)) {
                int nv = txt_to_int(u->edit_buf, n);
                if (nv < lo) nv = lo;
                if (nv > hi) nv = hi;
                if (nv != *v) { *v = nv; changed = 1; }
            }
        }
        push_rect(u, x, y, w, h, C_ACT_R, C_ACT_G, C_ACT_B, 1.0f);
        char line[UI_TEXT_LEN];
        int i = txt_copy(line, sizeof(line), label, -1);
        if (i < (int)sizeof(line) - 1) line[i++] = ' ';
        txt_copy(line + i, (int)sizeof(line) - i, u->edit_buf, -1);
        push_text(u, x + UI_PAD, y + 4.0f, UI_TEXT_SIZE, 1.0f, 1.0f, 1.0f, line);
        u->y += h + 2.0f;
        return changed;
    }

    int inside = hit(u, x, y, w, h);

    if (u->active == id) {
        if (u->in.down) {
            /* drag_acc holds the value at the moment of the grab, so the result
               is computed from the TOTAL cursor travel rather than accumulated
               per frame. Accumulating would drift: each frame's delta is
               rounded to an int, and the rounding error compounds over a long
               drag until the number no longer tracks the mouse.
               drag_acc는 붙잡은 순간의 값을 보관하므로, 결과가 프레임마다 누적되지 않고
               커서의 *총* 이동량으로부터 계산됩니다. 누적하면 값이 밀립니다. 매 프레임의
               변화량이 정수로 반올림되고, 그 오차가 긴 드래그에 걸쳐 쌓여 숫자가 더 이상
               마우스를 따라가지 않게 됩니다. */
            float nvf = u->drag_acc + (u->in.mx - u->drag_x) * step;
            int nv = (int)(nvf < 0.0f ? nvf - 0.5f : nvf + 0.5f);
            if (nv < lo) nv = lo;
            if (nv > hi) nv = hi;
            if (nv != *v) { *v = nv; changed = 1; }
        }
        if (u->in.release) {
            /* Released without travelling: that was a click, not a drag, so
               open the keyboard. The threshold is what separates "I want to
               type an exact number" from "I nudged it one unit".
               움직이지 않고 뗐다면 드래그가 아니라 클릭이므로 키보드를 엽니다. 이
               임계값이 "정확한 숫자를 입력하고 싶다"와 "한 단위 밀었다"를 구분합니다. */
            float moved = u->in.mx - u->drag_x;
            if (moved < 0.0f) moved = -moved;
            if (moved < 3.0f) edit_begin(u, id, *v);
            u->active = 0;
        }
    } else if (u->active == 0 && inside && u->in.click) {
        u->active   = id;
        u->drag_x   = u->in.mx;
        u->drag_acc = (float)*v;
    }
    if (inside && (u->active == 0 || u->active == id)) u->hot = id;

    int over = (u->hot == id), grabbed = (u->active == id);
    push_rect(u, x, y, w, h,
              grabbed ? C_HOT_R : over ? C_HOT_R : C_FIELD_R,
              grabbed ? C_HOT_G : over ? C_HOT_G : C_FIELD_G,
              grabbed ? C_HOT_B : over ? C_HOT_B : C_FIELD_B, 1.0f);

    /* A fill showing where the value sits in its range. Free to draw and it
       turns a number into something you can read at a glance.
       값이 범위 어디쯤에 있는지 보여 주는 채움입니다. 그리는 비용이 없고, 숫자를 한눈에
       읽을 수 있는 것으로 바꿉니다. */
    if (hi > lo) {
        float t = (float)(*v - lo) / (float)(hi - lo);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        push_rect(u, x, y + h - 2.0f, w * t, 2.0f,
                  C_ACT_R, C_ACT_G, C_ACT_B, 0.9f);
    }

    char line[UI_TEXT_LEN];
    field_text(line, sizeof(line), label, *v);
    push_text(u, x + UI_PAD, y + 4.0f, UI_TEXT_SIZE,
              C_TEXT_R, C_TEXT_G, C_TEXT_B, line);
    u->y += h + 2.0f;
    return changed;
}

int ui_drag_short(Ui *u, int id, const char *label, short *v, int lo, int hi, float step) {
    int t = *v;
    int changed = ui_drag_int(u, id, label, &t, lo, hi, step);
    if (changed) *v = (short)t;
    return changed;
}

int ui_text_field(Ui *u, int id, const char *label, char *buf, int cap) {
    float x = u->x, y = u->y, w = u->w, h = UI_ROW_H;
    int committed = 0;

    if (u->edit == id) {
        int done = edit_step(u);
        if (done == 1) { txt_copy(buf, cap, u->edit_buf, -1); committed = 1; }
        push_rect(u, x, y, w, h, C_ACT_R, C_ACT_G, C_ACT_B, 1.0f);
        char line[UI_TEXT_LEN];
        int i = txt_copy(line, sizeof(line), label, -1);
        if (i < (int)sizeof(line) - 1) line[i++] = ' ';
        txt_copy(line + i, (int)sizeof(line) - i, u->edit_buf, -1);
        push_text(u, x + UI_PAD, y + 4.0f, UI_TEXT_SIZE, 1.0f, 1.0f, 1.0f, line);
        u->y += h + 2.0f;
        return committed;
    }

    if (clickable(u, id, x, y, w, h)) edit_begin_str(u, id, buf);

    int over = (u->hot == id);
    push_rect(u, x, y, w, h,
              over ? C_HOT_R : C_FIELD_R,
              over ? C_HOT_G : C_FIELD_G,
              over ? C_HOT_B : C_FIELD_B, 1.0f);
    char line[UI_TEXT_LEN];
    int i = txt_copy(line, sizeof(line), label, -1);
    if (i < (int)sizeof(line) - 1) line[i++] = ' ';
    txt_copy(line + i, (int)sizeof(line) - i, buf[0] ? buf : "-", -1);
    push_text(u, x + UI_PAD, y + 4.0f, UI_TEXT_SIZE,
              C_TEXT_R, C_TEXT_G, C_TEXT_B, line);
    u->y += h + 2.0f;
    return committed;
}

int ui_section(Ui *u, int id, const char *label, int *open) {
    float x = u->x, y = u->y, w = u->w, h = UI_ROW_H;
    if (clickable(u, id, x, y, w, h)) *open = !*open;

    int over = (u->hot == id);
    push_rect(u, x, y, w, h,
              over ? C_HOT_R : 0.13f,
              over ? C_HOT_G : 0.14f,
              over ? C_HOT_B : 0.18f, 1.0f);

    char line[UI_TEXT_LEN];
    int i = txt_copy(line, sizeof(line), *open ? "- " : "+ ", -1);
    txt_copy(line + i, (int)sizeof(line) - i, label, -1);
    push_text(u, x + 5.0f, y + 4.0f, UI_TEXT_SIZE,
              C_ACCENT_R, C_ACCENT_G, C_ACCENT_B, line);
    u->y += h + 2.0f;
    return *open;
}

int ui_list_item(Ui *u, int id, const char *label, int selected) {
    float x = u->x, y = u->y, w = u->w, h = UI_ROW_H - 2.0f;
    int fired = clickable(u, id, x, y, w, h);

    int over = (u->hot == id);
    if (selected || over)
        push_rect(u, x, y, w, h,
                  selected ? C_ACT_R : C_HOT_R,
                  selected ? C_ACT_G : C_HOT_G,
                  selected ? C_ACT_B : C_HOT_B, selected ? 1.0f : 0.7f);
    push_text(u, x + 5.0f, y + 3.0f, UI_TEXT_SIZE,
              C_TEXT_R, C_TEXT_G, C_TEXT_B, label);
    u->y += h + 1.0f;
    return fired;
}

int ui_color_rgb(Ui *u, int id, const char *label, short *r, short *g, short *b) {
    /* The swatch first, then the three channels. Reading three numbers does not
       tell you what colour they make, and a light's colour is the one field in
       this format whose value says least about its effect.
       견본을 먼저, 그 다음 세 채널입니다. 숫자 셋을 읽어도 그것이 어떤 색인지 알 수 없으며,
       조명의 색은 이 포맷에서 값이 효과를 가장 적게 설명하는 필드입니다. */
    float x = u->x, y = u->y;
    push_rect(u, x, y + 2.0f, 26.0f, UI_ROW_H - 4.0f,
              *r / 255.0f, *g / 255.0f, *b / 255.0f, 1.0f);
    push_text(u, x + 32.0f, y + 4.0f, UI_TEXT_SIZE,
              C_DIM_R, C_DIM_G, C_DIM_B, label);
    u->y += UI_ROW_H;

    int changed = 0;
    changed |= ui_drag_short(u, id + 1, "R", r, 0, 255, 0.7f);
    changed |= ui_drag_short(u, id + 2, "G", g, 0, 255, 0.7f);
    changed |= ui_drag_short(u, id + 3, "B", b, 0, 255, 0.7f);
    return changed;
}

/* ------------------------------------------------------------------- flush */

/* Scissor takes a BOTTOM-left origin while every coordinate above is top-left,
   so the y has to be flipped against the viewport height. Getting this wrong
   clips the mirror image of the region -- which looks like the list scrolling
   the wrong way rather than like a clipping bug.
   시저는 좌하단 원점을 쓰는 반면 위의 모든 좌표는 좌상단 기준이므로, y를 뷰포트 높이에
   대해 뒤집어야 합니다. 이를 틀리면 영역의 거울상이 잘리는데, 그것은 클리핑 버그가 아니라
   목록이 반대로 스크롤되는 것처럼 보입니다. */
static void set_clip(const Ui *u, int c) {
    if (c < 0) { glDisable(GL_SCISSOR_TEST); return; }
    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)u->clips[c].x,
              (GLint)(u->vh - (u->clips[c].y + u->clips[c].h)),
              (GLsizei)u->clips[c].w, (GLsizei)u->clips[c].h);
}

void ui_end(Ui *u) {
    static MeshBuf buf;
    static Mesh    mesh;
    static int     ready;
    if (!ready) { mb_init(&buf, 4096); ready = 1; }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mat4 mvp = mat4_ortho(0.0f, (float)u->vw, (float)u->vh, 0.0f, -1.0f, 1.0f);
    rd_use();
    rd_mvp(mvp);

    /* --- rectangles, batched by colour ---
       Consecutive rects sharing a colour and a clip become one draw. The
       renderer's flat mode carries colour in a uniform rather than per vertex,
       so a colour change is necessarily a new draw call -- and widgets emit
       their fills in runs, so the batching is worth the few lines.
       색과 클립을 공유하는 연속된 사각형이 하나의 그리기가 됩니다. 렌더러의 flat 모드는
       색을 정점이 아닌 유니폼으로 전달하므로 색이 바뀌면 반드시 새 그리기 호출이 되며,
       위젯은 채움을 연속으로 방출하므로 이 몇 줄의 일괄 처리는 값어치를 합니다. */
    rd_mode(RD_FLAT);
    int i = 0;
    while (i < u->n_rects) {
        float r = u->rect[i].r, g = u->rect[i].g;
        float b = u->rect[i].b, a = u->rect[i].a;
        int   c = u->rect[i].clip;

        mb_reset(&buf);
        int j = i;
        while (j < u->n_rects &&
               u->rect[j].r == r && u->rect[j].g == g &&
               u->rect[j].b == b && u->rect[j].a == a &&
               u->rect[j].clip == c) {
            float x = u->rect[j].x, y = u->rect[j].y;
            float w = u->rect[j].w, h = u->rect[j].h;
            v3 n = v3f(0, 0, 1);
            mb_vtx(&buf, v3f(x,     y,     0), n, 0, 0);
            mb_vtx(&buf, v3f(x + w, y,     0), n, 1, 0);
            mb_vtx(&buf, v3f(x + w, y + h, 0), n, 1, 1);
            mb_vtx(&buf, v3f(x,     y,     0), n, 0, 0);
            mb_vtx(&buf, v3f(x + w, y + h, 0), n, 1, 1);
            mb_vtx(&buf, v3f(x,     y + h, 0), n, 0, 1);
            j++;
        }
        set_clip(u, c);
        rd_color(r, g, b, a);
        mesh_upload(&mesh, &buf, 1);
        mesh_draw(&mesh);
        i = j;
    }

    /* --- text, on top of every fill ---
       Drawn in a second pass rather than interleaved, so a label always lands
       over its own background whatever order the widget queued them in.
       끼워 넣지 않고 두 번째 패스로 그리므로, 위젯이 어떤 순서로 대기시켰든 라벨은 항상
       자신의 배경 위에 놓입니다. */
    rd_mode(RD_TEXT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_texture());
    i = 0;
    while (i < u->n_texts) {
        float r = u->text[i].r, g = u->text[i].g, b = u->text[i].b;
        int   c = u->text[i].clip;

        mb_reset(&buf);
        int j = i;
        while (j < u->n_texts &&
               u->text[j].r == r && u->text[j].g == g &&
               u->text[j].b == b && u->text[j].clip == c) {
            font_text(&buf, u->text[j].x, u->text[j].y, u->text[j].size, u->text[j].s);
            j++;
        }
        set_clip(u, c);
        rd_color(r, g, b, 1.0f);
        mesh_upload(&mesh, &buf, 1);
        mesh_draw(&mesh);
        i = j;
    }

    glDisable(GL_SCISSOR_TEST);
}
