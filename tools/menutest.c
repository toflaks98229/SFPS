/* menutest -- drive the ESC menu's navigation with no window.
 *
 * The rules this checks are the ones that are invisible from inside the
 * running game until they are wrong in front of a player: that ESC never
 * quits, that an action fires exactly once, and that the row the highlight
 * sits on is the row that activates.
 *
 * menu.c touches no GL and owns no window precisely so this can exist -- the
 * same split weapon.c's hook functions and enemy.c's AI are built on.
 *
 * ESC가 결코 게임을 종료하지 않는다는 것, 동작이 정확히 한 번만 발생한다는 것, 강조된
 * 행과 실제로 실행되는 행이 같다는 것. 이 규칙들은 플레이어 앞에서 잘못되기 전까지는
 * 게임 안에서 보이지 않습니다.
 */

#include <stdio.h>
#include "menu.h"

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Whether two strings match, so the test needs no <string.h> either. */
static int eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return !*a && !*b;
}

/* Find a row by its label on the current screen, or -1. Looking the row up by
   NAME rather than hardcoding its index is what keeps this test from going red
   the moment a row is inserted above it -- the same rule leveltest follows
   about not writing the map's numbers into its assertions.
   행을 인덱스가 아니라 *이름*으로 찾습니다. 그래야 위에 행이 하나 추가되는 순간 이
   테스트가 실패하지 않습니다. leveltest가 맵의 숫자를 단언에 적어 넣지 않는 것과 같은
   규칙입니다. */
static int row_named(const char *label) {
    int n = menu_row_count();
    for (int i = 0; i < n; i++) {
        const char *v;
        if (eq(menu_row_text(i, &v), label)) return i;
    }
    return -1;
}

/* Move the highlight onto a named row. Returns 0 if there is no such row. */
static int select_row(const char *label) {
    int want = row_named(label);
    if (want < 0) return 0;
    while (menu_cursor() != want) menu_move(+1);
    return 1;
}

int main(void) {
    printf("menutest\n\n");

    /* --- ESC opens rather than quits ------------------------------------ */
    menu_init(0);
    ok(!menu_is_open(), "the game starts with the menu closed");

    menu_escape();
    ok(menu_is_open(), "ESC opens the menu");
    ok(menu_screen() == MENU_ROOT, "and lands on the root screen");
    ok(menu_take_action() == MENU_ACT_NONE,
       "opening the menu asks for nothing -- ESC is not a quit");

    menu_escape();
    ok(!menu_is_open(), "ESC again closes it");
    ok(menu_take_action() == MENU_ACT_NONE, "and closing asks for nothing either");

    /* The whole reason this module exists: no sequence of ESC presses can end
       the game. Leaving has to be chosen from a row.
       이 모듈이 존재하는 이유 그 자체입니다. ESC를 어떻게 눌러도 게임이 끝나지 않으며,
       나가는 것은 행에서 골라야 합니다. */
    {
        int quit_seen = 0;
        for (int i = 0; i < 20; i++) {
            menu_escape();
            if (menu_take_action() == MENU_ACT_QUIT) quit_seen = 1;
        }
        ok(!quit_seen, "no amount of ESC ever asks to quit");
    }

    /* --- navigation ------------------------------------------------------ */
    menu_init(0);
    menu_escape();
    int rows = menu_row_count();
    ok(rows > 0, "the root screen has rows");
    ok(menu_cursor() == 0, "and opens with the first one highlighted");

    menu_move(+1);
    ok(menu_cursor() == 1, "down moves the highlight");
    menu_move(-1);
    ok(menu_cursor() == 0, "and up moves it back");

    /* Wrapping is what makes QUIT one press UP from the top rather than three
       presses down. */
    menu_move(-1);
    ok(menu_cursor() == rows - 1, "up from the top wraps to the last row");
    menu_move(+1);
    ok(menu_cursor() == 0, "and down from the last wraps to the first");

    /* --- the highlighted row is the row that runs ------------------------ */
    menu_init(0);
    menu_escape();
    ok(select_row("QUIT"), "the root screen offers a QUIT row");
    menu_activate();
    ok(menu_take_action() == MENU_ACT_QUIT, "activating it asks to quit");

    menu_init(0);
    menu_escape();
    ok(select_row("RESTART"), "and a RESTART row");
    menu_activate();
    ok(menu_take_action() == MENU_ACT_RESTART, "activating it asks to restart");

    /* Delivered exactly once. Two of these are not idempotent -- a restart
       taken twice because the frame loop polled twice would restart a level
       the player had already begun.
       정확히 한 번만 전달됩니다. 이 중 둘은 멱등하지 않으며, 프레임 루프가 두 번
       조회했다는 이유로 재시작이 두 번 일어나면 플레이어가 이미 시작한 레벨을 다시
       시작시키게 됩니다. */
    ok(menu_take_action() == MENU_ACT_NONE, "and it is not delivered a second time");

    menu_init(0);
    menu_escape();
    ok(select_row("RESUME"), "the root screen offers a RESUME row");
    menu_activate();
    ok(!menu_is_open(), "activating it closes the menu");
    ok(menu_take_action() == MENU_ACT_NONE, "without asking for anything");

    /* --- the settings screen --------------------------------------------- */
    menu_init(0);
    menu_escape();
    ok(select_row("SETTINGS"), "the root screen offers a SETTINGS row");
    menu_activate();
    ok(menu_screen() == MENU_SETTINGS, "activating it opens the settings");
    ok(menu_is_open(), "which is still an open menu");

    menu_escape();
    ok(menu_screen() == MENU_ROOT, "ESC from settings steps back to the root");
    ok(menu_is_open(), "rather than closing the menu outright");

    /* --- settings actually change ---------------------------------------- */
    menu_init(0);
    menu_escape();
    select_row("SETTINGS");
    menu_activate();

    ok(select_row("POST FX"), "the settings offer a POST FX row");
    {
        int before = menu_settings()->post_on;
        menu_adjust(+1);
        ok(menu_settings()->post_on != before, "adjusting it toggles the pass");
        menu_adjust(+1);
        ok(menu_settings()->post_on == before, "and toggles it back -- two values cycle");
        /* A pure toggle needs no signal to the caller: the frame loop reads it
           every frame. Raising one would make it look like a display change.
           순수한 토글은 호출자에게 신호가 필요 없습니다. 프레임 루프가 매 프레임 읽기
           때문입니다. 신호를 올리면 표시 모드 변경처럼 보이게 됩니다. */
        ok(menu_take_action() == MENU_ACT_NONE,
           "and toggling it asks for no window work");
    }

    ok(select_row("SCANLINES"), "the settings offer a SCANLINES row");
    {
        int before = menu_settings()->scanlines;
        menu_adjust(+1);
        ok(menu_settings()->scanlines != before, "adjusting it toggles them");
        menu_take_action();
    }

    /* Display and pixel size DO need the caller: one restyles the window, the
       other resizes the offscreen target.
       표시 모드와 픽셀 크기는 호출자가 필요합니다. 하나는 창 스타일을 바꾸고 다른 하나는
       오프스크린 타깃 크기를 바꿉니다. */
    ok(select_row("DISPLAY"), "the settings offer a DISPLAY row");
    {
        int before = menu_settings()->display;
        menu_adjust(+1);
        ok(menu_settings()->display != before, "adjusting it changes the mode");
        ok(menu_take_action() == MENU_ACT_DISPLAY,
           "and asks the caller to apply it -- the menu owns no window");
    }

    ok(select_row("PIXEL SIZE"), "the settings offer a PIXEL SIZE row");
    {
        int before = menu_settings()->pixel;
        menu_adjust(+1);
        ok(menu_settings()->pixel != before, "adjusting it changes the preset");
        ok(menu_take_action() == MENU_ACT_DISPLAY,
           "and asks the caller to resize the target");
    }

    /* Every value row must cycle through every one of its values and come back
       to where it started. A row whose name table is shorter than its value
       count would read past the table on the value nobody tested.
       모든 값 행은 자신의 모든 값을 거쳐 시작점으로 돌아와야 합니다. 이름 테이블이 값
       개수보다 짧은 행은, 아무도 테스트하지 않은 그 값에서 테이블 밖을 읽게 됩니다. */
    {
        int all_named = 1, all_cycle = 1;
        int n = menu_row_count();
        for (int i = 0; i < n; i++) {
            const char *v;
            menu_row_text(i, &v);
            if (!v[0]) continue;            /* a plain button, not a value row */

            while (menu_cursor() != i) menu_move(+1);

            const char *first = v;
            /* Step through at most a generous number of values; every row must
               return to its starting label well inside that.

               THE BOUND HAS TO EXCEED THE WIDEST VALUE ROW, and it is written
               against that row rather than guessed. It was a bare 8, which was
               generous while the widest row was DITHER's four -- and then the
               volume rows arrived with eleven notches and the check failed on
               content it was supposed to be checking. Naming the widest row
               here means the next one to grow takes this with it.
               상한은 *가장 넓은 값 행*보다 커야 하며, 추측이 아니라 그 행을 기준으로
               적습니다. 이전에는 맨 8이었고 가장 넓은 행이 DITHER의 넷이던 동안에는
               넉넉했습니다. 그러다 눈금 열한 개짜리 음량 행이 도착했고, 검사가 정작
               검사해야 할 콘텐츠에서 실패했습니다. 가장 넓은 행을 이곳에 이름으로 두면
               다음에 넓어지는 행이 이 값을 함께 데려갑니다. */
            int returned = 0;
            for (int k = 1; k <= MENU_VOL_STEPS + 4; k++) {
                menu_adjust(+1);
                menu_take_action();
                const char *now;
                menu_row_text(i, &now);
                if (!now[0]) { all_named = 0; break; }
                if (eq(now, first)) { returned = 1; break; }
            }
            if (!returned) all_cycle = 0;
        }
        ok(all_named, "every value row names every value it can hold");
        ok(all_cycle, "and cycles back round to where it started");
    }

    /* --- the mouse ------------------------------------------------------- */

    /* A fixed viewport, so the coordinates below mean something. */
#define VW 1280
#define VH 720

    /* Centre of a row, from the same function the drawing code uses. If these
       two ever disagreed, clicking a row would select a different one -- so
       the test asks for the centre rather than guessing at pixel positions.
       그리기 코드가 사용하는 것과 동일한 함수로 행의 중심을 구합니다. 둘이 어긋나면 행을
       클릭했을 때 다른 행이 선택되므로, 픽셀 위치를 추측하지 않고 중심을 물어봅니다. */
    menu_init(0);
    menu_escape();
    {
        float x0, y0, x1, y1;
        ok(menu_row_bounds(0, VW, VH, &x0, &y0, &x1, &y1),
           "a real row reports a box");
        ok(x1 > x0 && y1 > y0, "and the box has a positive area");

        ok(!menu_row_bounds(-1, VW, VH, &x0, &y0, &x1, &y1),
           "a negative row reports none");
        ok(!menu_row_bounds(9999, VW, VH, &x0, &y0, &x1, &y1),
           "and so does one past the end");
    }

    /* No two rows may overlap, or a click in the shared band is ambiguous and
       the answer depends on which one the search happens to reach first.
       두 행이 겹쳐서는 안 됩니다. 겹치면 그 대역의 클릭이 모호해지고, 결과가 탐색이 어느
       쪽에 먼저 도달하느냐에 좌우됩니다. */
    {
        int overlap = 0, n = menu_row_count();
        for (int i = 0; i < n; i++) {
            float ax0, ay0, ax1, ay1;
            menu_row_bounds(i, VW, VH, &ax0, &ay0, &ax1, &ay1);
            for (int j = i + 1; j < n; j++) {
                float bx0, by0, bx1, by1;
                menu_row_bounds(j, VW, VH, &bx0, &by0, &bx1, &by1);
                if (ay0 < by1 && by0 < ay1) overlap = 1;
            }
        }
        ok(!overlap, "no two rows overlap -- every click is unambiguous");
    }

    /* Hover moves the highlight, so the mouse and the keyboard agree about
       where a press would land. */
    menu_init(0);
    menu_escape();
    {
        int target = menu_row_count() - 1;
        float x0, y0, x1, y1;
        menu_row_bounds(target, VW, VH, &x0, &y0, &x1, &y1);
        float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;

        ok(menu_hover(cx, cy, VW, VH) == target, "hovering a row reports it");
        ok(menu_cursor() == target, "and moves the highlight there");

        /* Off the list: the highlight stays put rather than being cleared, so
           drifting away does not undo what the keyboard chose. */
        ok(menu_hover(cx, -500.0f, VW, VH) == -1, "hovering off the rows reports none");
        ok(menu_cursor() == target, "and leaves the highlight where it was");
    }

    /* A click activates the row under the cursor -- the same row the highlight
       is on, which is the property that makes the menu feel honest. */
    menu_init(0);
    menu_escape();
    {
        int quit = row_named("QUIT");
        float x0, y0, x1, y1;
        menu_row_bounds(quit, VW, VH, &x0, &y0, &x1, &y1);
        ok(menu_click((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, VW, VH, 0),
           "clicking the QUIT row hits it");
        ok(menu_take_action() == MENU_ACT_QUIT, "and asks to quit");
    }

    menu_init(0);
    menu_escape();
    {
        int resume = row_named("RESUME");
        float x0, y0, x1, y1;
        menu_row_bounds(resume, VW, VH, &x0, &y0, &x1, &y1);
        menu_click((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, VW, VH, 0);
        ok(!menu_is_open(), "clicking RESUME closes the menu");
    }

    /* A stray click does nothing. Closing the menu on any click off the rows
       would make a misclick indistinguishable from choosing Resume.
       빗나간 클릭은 아무 일도 하지 않습니다. 행 바깥의 아무 클릭에나 메뉴가 닫히면 잘못
       클릭한 것과 재개를 고른 것이 구분되지 않습니다. */
    menu_init(0);
    menu_escape();
    {
        ok(!menu_click(10.0f, 10.0f, VW, VH, 0), "a click off the rows hits nothing");
        ok(menu_is_open(), "and does not close the menu");
        ok(menu_take_action() == MENU_ACT_NONE, "nor ask for anything");
    }

    /* Right-click reverses a value, so a three-value row is one click away in
       either direction. */
    menu_init(0);
    menu_escape();
    select_row("SETTINGS");
    menu_activate();
    {
        int pixel = row_named("PIXEL SIZE");
        float x0, y0, x1, y1;
        menu_row_bounds(pixel, VW, VH, &x0, &y0, &x1, &y1);
        float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;

        menu_click(cx, cy, VW, VH, 0);          /* forward */
        menu_take_action();
        int after_left = menu_settings()->pixel;

        menu_click(cx, cy, VW, VH, 1);          /* back again */
        menu_take_action();
        ok(menu_settings()->pixel != after_left,
           "right-click steps a value the other way");
    }

    /* The mouse must not be able to reach a row that is not on screen. Clicking
       where a settings row WOULD be, while the root screen is showing, has to
       miss -- the bounds are asked of the current screen only.
       마우스가 화면에 없는 행에 닿을 수 있어서는 안 됩니다. 최상위 화면이 표시된 상태에서
       설정 행이 *있을 법한* 자리를 클릭하면 빗나가야 합니다. 경계는 현재 화면에 대해서만
       질의되기 때문입니다. */
    menu_init(0);
    menu_escape();
    {
        int n_root = menu_row_count();
        float x0, y0, x1, y1;
        int past = menu_row_bounds(n_root, VW, VH, &x0, &y0, &x1, &y1);
        ok(!past, "a row index past the current screen has no box");
    }

    /* --- input while closed is harmless ---------------------------------- */

    /* The mouse, too: a click while playing must reach the gun, not the menu.
       마우스도 마찬가지입니다. 플레이 중의 클릭은 메뉴가 아니라 총에 도달해야 합니다. */
    menu_init(0);
    ok(!menu_click(VW * 0.5f, VH * 0.5f, VW, VH, 0),
       "a click with the menu closed hits nothing");
    ok(menu_hover(VW * 0.5f, VH * 0.5f, VW, VH) == -1,
       "and hovering reports no row");
    ok(!menu_is_open(), "neither opens the menu");

    menu_init(0);
    menu_move(+1);
    menu_adjust(+1);
    menu_activate();
    ok(!menu_is_open(), "input with the menu closed does not open it");
    ok(menu_take_action() == MENU_ACT_NONE, "and asks for nothing");

    /* --- out-of-range rows are refused, not dereferenced ----------------- */
    menu_init(0);
    menu_escape();
    {
        const char *v;
        const char *l = menu_row_text(-1, &v);
        ok(l && !l[0] && v && !v[0], "a negative row yields empty text, not a crash");
        l = menu_row_text(9999, &v);
        ok(l && !l[0] && v && !v[0], "and so does one past the end");
    }

    printf("\n%s\n", fails ? "SOME MENU CHECKS FAILED" : "all menu checks passed");
    return fails != 0;
}
