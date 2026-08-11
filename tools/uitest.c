/* uitest -- drive the widget layer with synthetic input and no window.
 *
 * "It is a GUI" sounded like a reason not to test it, and it is not one. Every
 * widget in ui.c is arithmetic over a struct: only ui_end touches GL, and this
 * never calls it. So the parts that actually go wrong -- a button that fires on
 * press instead of release, a drag that drifts away from the cursor, a field
 * that writes garbage when you type a letter into a number -- are all reachable
 * from here.
 *
 * The rules checked are the ones whose failure is invisible until it is in
 * front of somebody editing a map:
 *
 *   - a click that slides off its button does nothing
 *   - a drag tracks total cursor travel rather than accumulating per frame
 *   - a value never leaves its range, by either drag or keyboard
 *   - typing a non-number leaves the old value alone instead of zeroing it
 *   - a widget with the mouse keeps it until release, even off its own rect
 *
 * ui.c의 모든 위젯은 구조체에 대한 산술 연산입니다. GL을 건드리는 것은 ui_end뿐이며 이
 * 테스트는 그것을 호출하지 않습니다. 따라서 실제로 잘못되는 부분들을 전부 이곳에서 확인할
 * 수 있습니다.
 */

#include <stdio.h>
#include "ui.h"

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void okd(int cond, const char *what, int got, int want) {
    printf("  %-58s %6d / %6d  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Clear any grab left over from the previous block.
 *
 * Each block below reuses small ids like 1 and 2, so without this a widget
 * still held at the end of one test is "the same widget" as the first one in
 * the next and inherits its drag. That is not a quirk of the test -- it is
 * exactly the id collision ui.h warns about, seen from the inside: two controls
 * sharing an id fight over the mouse. Real code gets its ids from UI_ID, which
 * is why it does not happen there.
 *
 * 각 블록이 1, 2 같은 작은 id를 재사용하므로, 이것이 없으면 한 테스트 끝에 여전히 붙잡혀
 * 있던 위젯이 다음 테스트의 첫 위젯과 "같은 위젯"이 되어 드래그를 물려받습니다. 이는
 * 테스트의 특이사항이 아니라 ui.h가 경고하는 id 충돌을 안쪽에서 본 것입니다. */
static void reset(Ui *u) {
    u->active = 0;
    u->edit   = 0;
    u->hot    = 0;
}

/* A frame of input with nothing happening; the tests set what they need. */
static UiInput idle(float x, float y) {
    UiInput in = {0};
    in.mx = x; in.my = y;
    return in;
}

/* Widgets are laid out from ui_panel's origin, so every test uses the same
   panel and hits the first row's centre. Hardcoding a y that happens to land
   on a row would go stale the moment UI_ROW_H changed.
   위젯은 ui_panel의 원점에서부터 배치되므로 모든 테스트가 같은 패널을 쓰고 첫 행의
   가운데를 클릭합니다. 우연히 행에 맞는 y를 하드코딩하면 UI_ROW_H가 바뀌는 순간
   낡은 값이 됩니다. */
#define PANEL_X   0.0f
#define PANEL_Y   0.0f
#define PANEL_W 200.0f
#define ROW1_X   40.0f
#define ROW1_Y  (PANEL_Y + UI_PAD + UI_ROW_H * 0.5f)

int main(void) {
    static Ui u;
    printf("uitest\n\n");

    /* --- a button fires on release, over itself --------------------------- */
    {
        reset(&u);
        UiInput in = idle(ROW1_X, ROW1_Y);
        in.click = 1; in.down = 1;
        ui_begin(&u, 800, 600, &in);
        ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ok(!ui_button(&u, 1, "OK"), "pressing a button does not fire it yet");

        in = idle(ROW1_X, ROW1_Y);
        in.release = 1;
        ui_begin(&u, 800, 600, &in);
        ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ok(ui_button(&u, 1, "OK") == 1, "and releasing over it fires exactly once");
    }

    /* --- a click that slides off is cancelled -----------------------------
       The property that makes a misclick recoverable, and the reason the
       handshake fires on release rather than press. */
    {
        reset(&u);
        UiInput in = idle(ROW1_X, ROW1_Y);
        in.click = 1; in.down = 1;
        ui_begin(&u, 800, 600, &in);
        ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_button(&u, 1, "OK");

        in = idle(ROW1_X, ROW1_Y + 500.0f);   /* slid far off the button */
        in.release = 1;
        ui_begin(&u, 800, 600, &in);
        ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ok(!ui_button(&u, 1, "OK"), "sliding off a pressed button cancels it");
    }

    /* --- the mouse stays with the widget that grabbed it -------------------
       Without this, dragging a value and straying onto the row below would
       hand the drag to that row -- which reads as two fields fighting. */
    {
        reset(&u);
        UiInput in = idle(ROW1_X, ROW1_Y);
        in.click = 1; in.down = 1;
        ui_begin(&u, 800, 600, &in);
        ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        int a = 10, b = 10;
        ui_drag_int(&u, 1, "A", &a, 0, 100, 1.0f);
        ui_drag_int(&u, 2, "B", &b, 0, 100, 1.0f);

        /* Move down onto B's row while still holding A. */
        in = idle(ROW1_X + 20.0f, ROW1_Y + UI_ROW_H + 2.0f);
        in.down = 1;
        ui_begin(&u, 800, 600, &in);
        ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 1, "A", &a, 0, 100, 1.0f);
        ui_drag_int(&u, 2, "B", &b, 0, 100, 1.0f);
        okd(b == 10, "straying onto another field does not drag it", b, 10);
        ok(a != 10, "and the field that was grabbed still follows the mouse");
    }

    /* --- a drag tracks TOTAL travel, not a per-frame sum -------------------
       Accumulating each frame's delta rounds to an int every frame, and the
       error compounds: over a long drag the number stops tracking the cursor.
       Fifty one-pixel steps must land in the same place as one fifty-pixel
       step, and this asserts the equality directly. */
    {
        reset(&u);
        int slow = 0, fast = 0;

        UiInput in = idle(ROW1_X, ROW1_Y);
        in.click = 1; in.down = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 1, "V", &slow, -1000, 1000, 0.5f);
        for (int i = 1; i <= 50; i++) {
            in = idle(ROW1_X + i, ROW1_Y);
            in.down = 1;
            ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
            ui_drag_int(&u, 1, "V", &slow, -1000, 1000, 0.5f);
        }

        reset(&u);
        in = idle(ROW1_X, ROW1_Y);
        in.click = 1; in.down = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 2, "V", &fast, -1000, 1000, 0.5f);
        in = idle(ROW1_X + 50.0f, ROW1_Y);
        in.down = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 2, "V", &fast, -1000, 1000, 0.5f);

        okd(slow == fast, "50 small drag steps equal one big one", slow, fast);
        okd(slow == 25, "and 50px at 0.5/px moves the value 25", slow, 25);
    }

    /* --- the range is a real bound, on both paths -------------------------- */
    {
        reset(&u);
        int v = 0;
        UiInput in = idle(ROW1_X, ROW1_Y);
        in.click = 1; in.down = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 1, "V", &v, -5, 5, 1.0f);

        in = idle(ROW1_X + 9999.0f, ROW1_Y);
        in.down = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 1, "V", &v, -5, 5, 1.0f);
        okd(v == 5, "dragging far past the top clamps to it", v, 5);

        in = idle(ROW1_X - 9999.0f, ROW1_Y);
        in.down = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 1, "V", &v, -5, 5, 1.0f);
        okd(v == -5, "and far past the bottom clamps there too", v, -5);
    }

    /* --- click without travel opens the keyboard --------------------------
       Drag is for finding a value by eye; typing is for matching one exactly.
       A field that only dragged would make the second job guesswork. */
    {
        int v = 7;
        reset(&u);

        UiInput in = idle(ROW1_X, ROW1_Y);
        in.click = 1; in.down = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 1, "V", &v, 0, 999, 1.0f);

        in = idle(ROW1_X, ROW1_Y);        /* released without moving */
        in.release = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 1, "V", &v, 0, 999, 1.0f);
        ok(ui_wants_keys(&u), "a click with no travel opens keyboard entry");

        /* Type 250 and commit. */
        const char *digits = "250";
        for (int i = 0; digits[i]; i++) {
            in = idle(ROW1_X, ROW1_Y);
            in.ch = digits[i];
            ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
            ui_drag_int(&u, 1, "V", &v, 0, 999, 1.0f);
        }
        in = idle(ROW1_X, ROW1_Y);
        in.enter = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 1, "V", &v, 0, 999, 1.0f);
        okd(v == 250, "and typing a number commits it on Enter", v, 250);
        ok(!ui_wants_keys(&u), "which also closes the editor");
    }

    /* --- a typed non-number must not become zero --------------------------
       The failure that matters: fumbling a keystroke while setting a ceiling
       height would otherwise flatten the room to 0 and look like a save bug. */
    {
        int v = 450;
        reset(&u);

        UiInput in = idle(ROW1_X, ROW1_Y);
        in.click = 1; in.down = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 1, "V", &v, 0, 999, 1.0f);
        in = idle(ROW1_X, ROW1_Y); in.release = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 1, "V", &v, 0, 999, 1.0f);

        const char *junk = "12x";
        for (int i = 0; junk[i]; i++) {
            in = idle(ROW1_X, ROW1_Y);
            in.ch = junk[i];
            ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
            ui_drag_int(&u, 1, "V", &v, 0, 999, 1.0f);
        }
        in = idle(ROW1_X, ROW1_Y); in.enter = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_int(&u, 1, "V", &v, 0, 999, 1.0f);
        okd(v == 450, "committing a non-number leaves the value alone", v, 450);
    }

    /* --- escape abandons an edit ------------------------------------------ */
    {
        char name[32] = "arena";
        reset(&u);

        UiInput in = idle(ROW1_X, ROW1_Y);
        in.click = 1; in.down = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_text_field(&u, 1, "name", name, sizeof(name));
        in = idle(ROW1_X, ROW1_Y); in.release = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_text_field(&u, 1, "name", name, sizeof(name));

        in = idle(ROW1_X, ROW1_Y); in.ch = 'Z';
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_text_field(&u, 1, "name", name, sizeof(name));

        in = idle(ROW1_X, ROW1_Y); in.escape = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_text_field(&u, 1, "name", name, sizeof(name));

        ok(name[0] == 'a' && name[5] == 0, "escape abandons a text edit unchanged");
        ok(!ui_wants_keys(&u), "and closes the editor");
    }

    /* --- a short field cannot be pushed outside a short --------------------
       Every authored number in the level format is a short, and the inspector
       edits them through ui_drag_short. A range wider than the type would wrap
       silently, which in a coordinate reads as geometry teleporting. */
    {
        reset(&u);
        short s = 0;
        reset(&u);
        UiInput in = idle(ROW1_X, ROW1_Y);
        in.click = 1; in.down = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_short(&u, 1, "S", &s, -32000, 32000, 10.0f);

        in = idle(ROW1_X + 100000.0f, ROW1_Y);
        in.down = 1;
        ui_begin(&u, 800, 600, &in); ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_drag_short(&u, 1, "S", &s, -32000, 32000, 10.0f);
        okd(s == 32000, "a short field clamps inside the type's range", s, 32000);
    }

    /* --- the caller can tell whether the UI owns the cursor ---------------
       mapedit asks this before treating a click as a map edit. Without it,
       pressing a button in the panel would also drag the sector behind it. */
    {
        reset(&u);
        UiInput in = idle(ROW1_X, ROW1_Y);
        ui_begin(&u, 800, 600, &in);
        ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_button(&u, 1, "OK");
        ok(ui_wants_mouse(&u), "the UI reports owning a cursor over a widget");

        in = idle(PANEL_X + PANEL_W + 300.0f, ROW1_Y);
        ui_begin(&u, 800, 600, &in);
        ui_panel(&u, PANEL_X, PANEL_Y, PANEL_W, 400, 1.0f);
        ui_button(&u, 1, "OK");
        ok(!ui_wants_mouse(&u), "and releasing it when the cursor is elsewhere");
    }

    /* --- nothing overflowed its queue -------------------------------------
       A silent truncation in the draw list would drop widgets off the bottom of
       a panel, which reads as a layout bug. ui.c reports it instead. */
    ok(!u.overflow, "no draw queue overflowed during the run");

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall ui checks passed\n", fails);
    return fails != 0;
}
