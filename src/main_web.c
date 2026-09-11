/**
 * @file main_web.c
 * @brief The browser's window, input and frame loop. The platform layer for
 *        the game, the way main.c is on Windows.
 *
 * ENGLISH
 * -------
 * plat.h says what does NOT belong in it: "not the window, not the input, not
 * the GL context, not the frame loop -- main.c owns all of that and is itself
 * the platform layer for the game." This is that file for the second host, and
 * it is the one place the two hosts genuinely duplicate each other. That cost
 * was named in the proposal before it was paid and it is paid here.
 *
 * THREE THINGS ARE DIFFERENT, AND ONLY THREE.
 *
 * 1. THE LOOP IS INVERTED. A page may not be held by a `while`; the browser
 *    calls us. ::frame is WinMain's loop body with the `while` taken off, and
 *    the body split cleanly because it already was -- menu_take_action,
 *    world_step, world_take_geometry_scope, scene_frame, and the only thing
 *    the window supplies is a dt and an ::Input.
 *
 * 2. THE MOUSE IS SIMPLER HERE. main.c warps the cursor to the centre every
 *    frame and reads the distance it moved back; a browser has pointer lock
 *    and hands over the delta directly. There is no warp, no centre, and no
 *    ::g_warp_mouse -- the flag that exists on Windows to stop the huge jump
 *    when the cursor is put back after wandering off.
 *
 * 3. STARTING IS ASYNCHRONOUS, and that is the one real awkwardness. The save
 *    lives in IndexedDB and arrives through a callback, and ::save_init reads
 *    it during start-up -- so start-up cannot be a straight line. ::main asks
 *    for the load and ::boot is what happens when it lands. Everything else
 *    happens in the order app_start does it, which is the order it has to be.
 *
 * 한국어
 * ------
 * @brief 브라우저의 창과 입력과 프레임 루프. Windows에서 main.c가 그러하듯, 게임에 대한 플랫폼
 *        계층입니다.
 *
 * plat.h는 자신에게 속하지 *않는* 것을 말합니다. "창도, 입력도, GL 컨텍스트도, 프레임 루프도
 * 아닙니다. main.c가 그 전부를 소유하며 게임에 대한 플랫폼 계층 자체입니다." 이것이 두 번째
 * 호스트를 위한 그 파일이며, 두 호스트가 진짜로 서로를 중복하는 유일한 자리입니다. 그 대가는
 * 치르기 전에 기획서에 이름 붙여 두었고 이곳에서 치러집니다.
 *
 * *다른 것은 셋이며, 셋뿐입니다.*
 *
 * 1. *루프가 뒤집힙니다.* 페이지는 `while`로 붙잡을 수 없으며 브라우저가 우리를 부릅니다.
 *    ::frame은 WinMain의 루프 본문에서 `while`을 떼어 낸 것이고, 본문이 깔끔하게 갈라진 것은
 *    이미 그랬기 때문입니다. menu_take_action, world_step, world_take_geometry_scope,
 *    scene_frame이며, 창이 공급하는 것은 dt와 ::Input 하나뿐입니다.
 *
 * 2. *마우스는 이쪽이 더 단순합니다.* main.c는 매 프레임 커서를 중앙으로 워프시키고 움직인
 *    거리를 되읽습니다. 브라우저에는 포인터 락이 있고 변화량을 직접 건넵니다. 워프도, 중앙도,
 *    ::g_warp_mouse도 없습니다. 그 플래그는 커서가 벗어났다 되돌아올 때의 거대한 도약을 막으려고
 *    Windows에 존재하는 것입니다.
 *
 * 3. *시작이 비동기이며*, 그것이 유일한 진짜 껄끄러움입니다. 저장은 IndexedDB에 있고 콜백으로
 *    도착하는데 ::save_init이 시작 중에 그것을 읽습니다. 그래서 시작이 직선일 수 없습니다.
 *    ::main이 로드를 요청하고 ::boot이 그것이 도착했을 때 일어나는 일입니다. 나머지 전부는
 *    app_start가 하는 순서대로 일어나며, 그 순서여야만 합니다.
 */
#include "world.h"
#include "scene.h"
#include "render.h"
#include "post.h"
#include "gfx.h"
#include "menu.h"
#include "save.h"
#include "font.h"
#include "decal.h"
#include "audio.h"
#include "music.h"
#include "data.h"
#include "story.h"   /* STORY_VICTORY: which cutscene the boss music covers */
#include "plat.h"
#include "webgl.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <string.h>

#define CANVAS "#canvas"

static World g_world;
static Scene g_scene;

static int   g_vw = 960, g_vh = 540;
static double g_prev_ms;
static int   g_started;

/* --- input / 입력 --- */

/* Held keys, and the list is short because the game's is. Indexed by an enum
   of its own rather than by a key code: a browser reports `code` strings and
   there is no table to index with one.
   유지되는 키들이며, 게임의 목록이 짧아서 짧습니다. 키 코드가 아니라 자체 열거형으로
   인덱싱합니다. 브라우저는 `code` 문자열을 보고하며 그것으로 인덱싱할 표가 없습니다. */
enum { K_FWD, K_BACK, K_LEFT, K_RIGHT, K_JUMP, K_COUNT };
static int g_keys[K_COUNT];

static int   g_mouse_down, g_hook_down;
static float g_look_dx, g_look_dy;
static float g_mouse_x, g_mouse_y;
static int   g_locked;

/* The edges a window sees and a frame consumes, cleared by construction the
   way main.c's are -- see ::run_reset for the argument. A fourth edge added
   later is cleared without anybody extending a list.
   창이 보고 프레임이 소비하는 엣지들이며, main.c의 것과 같이 구조체 대입으로 지워집니다.
   논거는 ::run_reset을 보십시오. 나중에 더해지는 네 번째 엣지도 누가 목록을 늘리지 않아도
   지워집니다. */
typedef struct { int confirm, weapon, let_go; } EdgeLatch;
static EdgeLatch g_edge;

static int screen_takes_press(void) {
    return (g_world.run.title || g_world.run.dead || g_world.run.cut) &&
           !menu_is_open();
}

static void keys_release_all(void) {
    for (int i = 0; i < K_COUNT; i++) g_keys[i] = 0;
    g_mouse_down = g_hook_down = 0;
    g_edge.let_go = 1;
}

/* --- the canvas / 캔버스 --- */

static void size_to_window(void) {
    int w = EM_ASM_INT({ return Math.max(1, window.innerWidth | 0); });
    int h = EM_ASM_INT({ return Math.max(1, window.innerHeight | 0); });

    /* ONE DRAWING-BUFFER PIXEL PER CSS PIXEL, and devicePixelRatio is left out
       on purpose. The world is rendered into an art-sized buffer and magnified
       with GL_NEAREST, so a retina-sized canvas would only make the integer
       magnification land differently -- post.h's note on POST_HEIGHT says what
       that costs: edges crawl and the stipple creeps. The look wants whole
       pixels, not more of them.
       *CSS 픽셀 하나에 드로잉 버퍼 픽셀 하나*이며 devicePixelRatio는 의도적으로 뺐습니다.
       월드는 아트 크기 버퍼에 그려져 GL_NEAREST로 확대되므로, 레티나 크기의 캔버스는 정수 배율이
       다르게 떨어지게 만들 뿐입니다. 그 비용은 POST_HEIGHT에 대한 post.h의 주석이 말합니다.
       가장자리가 기어 다니고 스티플이 흐릅니다. 이 룩이 원하는 것은 온전한 픽셀이지 더 많은
       픽셀이 아닙니다. */
    if (w == g_vw && h == g_vh) return;
    g_vw = w;
    g_vh = h;
    emscripten_set_canvas_element_size(CANVAS, w, h);
    gfx_apply_pixel_preset(menu_settings()->pixel, w, h);
}

static EM_BOOL on_resize(int t, const EmscriptenUiEvent *e, void *u) {
    (void)t; (void)e; (void)u;
    if (g_started) size_to_window();
    return EM_TRUE;
}

/* --- keyboard / 키보드 --- */

static int key_index(const char *code) {
    if (!strcmp(code, "KeyW") || !strcmp(code, "ArrowUp"))    return K_FWD;
    if (!strcmp(code, "KeyS") || !strcmp(code, "ArrowDown"))  return K_BACK;
    if (!strcmp(code, "KeyA"))                                return K_LEFT;
    if (!strcmp(code, "KeyD"))                                return K_RIGHT;
    if (!strcmp(code, "Space"))                               return K_JUMP;
    return -1;
}

/* The menu's half. menu.h is a complete interface for this -- move, adjust,
   activate, escape -- so unlike main.c, which had a VK table to translate,
   there is nothing here but the mapping.
   메뉴의 절반입니다. menu.h가 이것에 대한 완전한 인터페이스입니다. move, adjust, activate,
   escape. 그래서 번역할 VK 표가 있던 main.c와 달리 이곳에는 대응 관계 말고는 없습니다. */
static void menu_take_code(const char *code) {
    if (!strcmp(code, "ArrowUp")    || !strcmp(code, "KeyW")) menu_move(-1);
    else if (!strcmp(code, "ArrowDown")  || !strcmp(code, "KeyS")) menu_move(1);
    else if (!strcmp(code, "ArrowLeft")  || !strcmp(code, "KeyA")) menu_adjust(-1);
    else if (!strcmp(code, "ArrowRight") || !strcmp(code, "KeyD")) menu_adjust(1);
    else if (!strcmp(code, "Enter") || !strcmp(code, "Space"))     menu_activate();
    else if (!strcmp(code, "Escape"))                              menu_escape();
}

static EM_BOOL on_key_down(int t, const EmscriptenKeyboardEvent *e, void *u) {
    (void)t; (void)u;
    if (e->repeat) return EM_TRUE;

    if (!strcmp(e->code, "F1")) { post_set_enabled(!post_enabled()); return EM_TRUE; }

    if (menu_is_open()) { menu_take_code(e->code); return EM_TRUE; }

    if (!strcmp(e->code, "Escape")) {
        menu_open_title();
        keys_release_all();
        return EM_TRUE;
    }

    /* A screen in front of the player takes any key, and nothing else does
       while it is up -- the same list ::screen_takes_press keeps on Windows.
       플레이어 앞의 화면은 아무 키나 받아들이며, 그것이 떠 있는 동안에는 다른 무엇도 받지
       않습니다. Windows에서 ::screen_takes_press가 지키는 것과 같은 목록입니다. */
    if (screen_takes_press()) { g_edge.confirm = 1; return EM_TRUE; }

    if (e->code[0] == 'D' && !strncmp(e->code, "Digit", 5) &&
        e->code[5] >= '1' && e->code[5] <= '9')
        g_edge.weapon = e->code[5] - '0';

    int k = key_index(e->code);
    if (k >= 0) g_keys[k] = 1;
    return EM_TRUE;
}

static EM_BOOL on_key_up(int t, const EmscriptenKeyboardEvent *e, void *u) {
    (void)t; (void)u;
    int k = key_index(e->code);
    if (k >= 0) g_keys[k] = 0;
    return EM_TRUE;
}

/* --- mouse / 마우스 --- */

static EM_BOOL on_mouse_move(int t, const EmscriptenMouseEvent *e, void *u) {
    (void)t; (void)u;
    if (g_locked) {
        /* ACCUMULATED, not assigned. Several move events can arrive between
           two frames and each carries its own delta; taking only the last one
           would throw the rest of the movement away and make a fast flick
           shorter than a slow one.
           *대입이 아니라 누적입니다.* 두 프레임 사이에 여러 이동 이벤트가 도착할 수 있고 각각이
           자기 변화량을 나릅니다. 마지막 것만 취하면 나머지 움직임을 버리게 되고, 빠른 튕김이
           느린 것보다 짧아집니다. */
        g_look_dx += (float)e->movementX;
        g_look_dy += (float)e->movementY;
    } else {
        g_mouse_x = (float)e->targetX;
        g_mouse_y = (float)e->targetY;
        if (menu_is_open()) menu_hover(g_mouse_x, g_mouse_y, g_vw, g_vh);
    }
    return EM_TRUE;
}

static EM_BOOL on_mouse_down(int t, const EmscriptenMouseEvent *e, void *u) {
    (void)t; (void)u;

    /* THE CLICK CARRIES ITS OWN POSITION, and reading the one a previous move
       left behind was wrong. Found by clicking STORY in a browser and watching
       nothing happen: the pointer had never MOVED over the canvas, so
       ::g_mouse_x was still 0 and the menu was asked what is at the top-left
       corner, which is nothing. A person nudges the mouse before clicking and
       would never have seen it; a synthetic click does not, and neither does
       anybody who tabs to the page and clicks without moving.
       *클릭은 자기 위치를 지니고 있으며*, 이전 이동이 남긴 것을 읽는 것은 틀렸습니다.
       브라우저에서 STORY를 클릭했는데 아무 일도 일어나지 않는 것을 보고 찾았습니다. 포인터가
       캔버스 위에서 *움직인* 적이 없어 ::g_mouse_x가 여전히 0이었고, 메뉴는 좌상단 모서리에
       무엇이 있는지 질문받았으며 그곳에는 아무것도 없습니다. 사람은 클릭 전에 마우스를 조금
       움직이므로 결코 보지 못했을 것입니다. 합성된 클릭은 그러지 않으며, 페이지에 탭으로 들어와
       움직이지 않고 클릭하는 사람도 마찬가지입니다. */
    g_mouse_x = (float)e->targetX;
    g_mouse_y = (float)e->targetY;

    if (menu_is_open()) {
        menu_click(g_mouse_x, g_mouse_y, g_vw, g_vh, e->button == 2);
        return EM_TRUE;
    }
    if (screen_takes_press()) { g_edge.confirm = 1; return EM_TRUE; }

    /* POINTER LOCK IS ASKED FOR HERE AND NOWHERE ELSE, because a browser only
       grants it inside a gesture. This is also the click that wakes the audio
       context -- audio_web.c registers its own listener for that, so the two
       do not have to know about each other.
       *포인터 락은 이곳에서만 요청합니다.* 브라우저가 제스처 안에서만 허락하기 때문입니다.
       이것은 오디오 컨텍스트를 깨우는 클릭이기도 합니다. audio_web.c가 그것을 위해 자기
       리스너를 등록하므로 둘이 서로를 알 필요가 없습니다. */
    if (!g_locked) emscripten_request_pointerlock(CANVAS, EM_TRUE);

    if (e->button == 0) g_mouse_down = 1;
    if (e->button == 2) g_hook_down = 1;
    return EM_TRUE;
}

static EM_BOOL on_mouse_up(int t, const EmscriptenMouseEvent *e, void *u) {
    (void)t; (void)u;
    if (menu_is_open()) { menu_mouse_up(); return EM_TRUE; }
    if (e->button == 0) g_mouse_down = 0;
    if (e->button == 2) g_hook_down = 0;
    return EM_TRUE;
}

static EM_BOOL on_lock_change(int t, const EmscriptenPointerlockChangeEvent *e,
                              void *u) {
    (void)t; (void)u;
    int was = g_locked;
    g_locked = e->isActive;

    /* LOSING THE LOCK IS LOSING FOCUS, and it is the same event: Escape takes
       the pointer back, and the player is no longer steering. Everything held
       is let go for the reason main.c gives -- "the player stopped touching
       the keyboard and does not know it" -- and the accumulated look delta goes
       with it, or the first frame after coming back would turn by however far
       the cursor travelled meanwhile. That is the jump ::g_warp_mouse exists to
       prevent on the other host.
       *락을 잃는 것이 포커스를 잃는 것이며*, 같은 사건입니다. Escape가 포인터를 되찾아 가고
       플레이어는 더 이상 조종하고 있지 않습니다. 붙잡고 있던 것을 전부 놓는 이유는 main.c가
       말하는 것과 같습니다. "플레이어가 키보드에서 손을 뗐는데 본인은 모른다." 그리고 쌓인 시선
       변화량도 함께 버립니다. 그러지 않으면 돌아온 첫 프레임이 그동안 커서가 이동한 만큼
       돌아갑니다. 다른 호스트에서 ::g_warp_mouse가 막으려는 그 도약입니다. */
    if (was && !g_locked) {
        keys_release_all();
        g_look_dx = g_look_dy = 0.0f;
    }
    return EM_TRUE;
}

/* --- the frame / 프레임 --- */

static void input_gather(Input *in) {
    Input z = {0};
    *in = z;

    in->paused = menu_is_open();

    if (g_locked && !world_frozen(&g_world, in->paused)) {
        in->look_dx = g_look_dx;
        in->look_dy = g_look_dy;
    }
    /* Cleared whether or not it was used, so a delta that arrived while the
       world was frozen does not land on the frame after it thaws.
       쓰였든 아니든 지웁니다. 그래야 월드가 정지한 동안 도착한 변화량이 풀린 다음 프레임에
       떨어지지 않습니다. */
    g_look_dx = g_look_dy = 0.0f;

    in->forward = g_keys[K_FWD];
    in->back    = g_keys[K_BACK];
    in->left    = g_keys[K_LEFT];
    in->right   = g_keys[K_RIGHT];
    in->jump    = g_keys[K_JUMP];
    /* NOT GATED ON POINTER LOCK, and it was. The lock is how the camera is
       steered, not how the trigger is pulled, and hanging the trigger off it
       means an embedding that refuses the lock gets a game that cannot shoot at
       all -- unplayable rather than awkward. Measured: this project's own
       preview browser answers requestPointerLock with "WrongDocumentError: the
       root document of this element is not valid for pointer lock", which is
       not something the page did wrong and not something it can fix.
       WHAT IS LOST WITHOUT THE LOCK is aiming, and only aiming: look_dx stays
       zero above because there are no deltas to have. The game degrades to one
       that shoots where it is already pointing, which is a bad game and a long
       way better than a dead one. The menu and the screens are already guarded
       further up, so a click that lands here is a click during play.
       *포인터 락에 걸지 않으며*, 걸려 있었습니다. 락은 카메라를 조종하는 방법이지 방아쇠를
       당기는 방법이 아닙니다. 방아쇠를 그것에 매달면, 락을 거부하는 임베딩은 아예 쏠 수 없는
       게임을 받습니다. 어색한 것이 아니라 플레이 불가입니다. 실제로 재어 보니 이 프로젝트의
       미리 보기 브라우저가 requestPointerLock에 "WrongDocumentError: 이 요소의 루트 문서는
       포인터 락에 유효하지 않습니다"로 답합니다. 페이지가 잘못한 것도 아니고 페이지가 고칠 수
       있는 것도 아닙니다.
       *락 없이 잃는 것은 조준이며 조준뿐입니다.* 위의 look_dx는 가질 변화량이 없으므로 0으로
       남습니다. 게임은 이미 향하고 있는 곳을 쏘는 게임으로 내려앉으며, 그것은 나쁜 게임이고
       죽은 게임보다는 한참 낫습니다. 메뉴와 화면들은 위쪽에서 이미 걸러졌으므로, 이곳에 닿는
       클릭은 플레이 중의 클릭입니다. */
    in->fire    = g_mouse_down;
    in->hook    = g_hook_down;

    in->confirm     = g_edge.confirm;
    in->want_weapon = g_edge.weapon;
    in->let_go      = g_edge.let_go;

    EdgeLatch clear = {0};
    g_edge = clear;
}

static void frame(void) {
    double now = emscripten_get_now();
    float  dt  = (float)((now - g_prev_ms) / 1000.0);
    g_prev_ms = now;

    /* The same clamp WinMain uses. A tab that was in the background for a
       minute comes back with a minute of dt, and one step of that size is a
       player who has fallen through the floor.
       WinMain이 쓰는 것과 같은 제한입니다. 1분 동안 백그라운드에 있던 탭은 1분짜리 dt를 들고
       돌아오며, 그만한 크기의 한 걸음은 바닥을 뚫고 떨어진 플레이어입니다. */
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0f) dt = 0.0f;

    /* TAKEN INTO A LOCAL rather than switched on directly, for the reason
       WinMain gives: two of the cases share a body and have to tell themselves
       apart inside it, and menu_take_action CLEARS as it reports -- so asking
       twice would answer MENU_ACT_NONE the second time and every run would
       start in story mode.
       직접 switch하지 않고 지역 변수로 받습니다. WinMain이 말하는 이유입니다. 두 case가 본문을
       공유하면서 그 안에서 서로를 구별해야 하고, menu_take_action은 *보고하면서 지웁니다*.
       두 번 물으면 두 번째는 MENU_ACT_NONE으로 답하고 모든 플레이가 스토리 모드로 시작합니다. */
    MenuAction act = menu_take_action();
    switch (act) {
    case MENU_ACT_QUIT:
        /* There is no process to end. A page cannot close itself unless it
           opened itself, so QUIT is the one action this host cannot honour --
           it puts the title back instead, which is where the row lives.
           끝낼 프로세스가 없습니다. 페이지는 스스로 연 것이 아니면 스스로 닫을 수 없으므로,
           QUIT은 이 호스트가 이행할 수 없는 유일한 동작입니다. 대신 타이틀을 되돌려 놓으며,
           그 행이 사는 곳이 그곳입니다. */
        menu_open_title();
        break;
    case MENU_ACT_RESTART: g_world.run.restart_wanted = 1; break;
    case MENU_ACT_STORY:
    case MENU_ACT_ENDLESS:
        if (world_begin(&g_world, act == MENU_ACT_ENDLESS))
            menu_close();
        break;
    case MENU_ACT_DISPLAY:
        gfx_apply_pixel_preset(menu_settings()->pixel, g_vw, g_vh);
        break;
    case MENU_ACT_NONE: break;
    }

    gfx_apply_live_settings();
    audio_set_volume(menu_settings()->master * MENU_VOL_PER_STEP,
                     menu_settings()->sfx    * MENU_VOL_PER_STEP,
                     menu_settings()->music  * MENU_VOL_PER_STEP);

    menu_set_unlocked(save_unlocks());
    save_note_wave(g_world.run.wave_best);
    if (g_world.run.won && !g_world.run.endless)
        save_unlock(MENU_UNLOCK_ENDLESS);

    int boss_music = world_boss_present(&g_world) ||
                     g_world.run.boss_line ||
                     g_world.run.cut == STORY_VICTORY + 1;
    music_play(g_world.run.title ? MUSIC_TITLE :
               boss_music ? MUSIC_BOSS : MUSIC_LEVEL);
    music_update(dt);

    Input in;
    input_gather(&in);

    int frozen = world_step(&g_world, &in, (float)g_vw / (float)g_vh, dt);

    if (g_world.run.restart_wanted) {
        world_restart(&g_world);
        menu_close();
        frozen = world_frozen(&g_world, menu_is_open());
    }

    int dynamic = 0;
    switch (world_take_geometry_scope(&g_world, &dynamic)) {
    case WORLD_GEOM_ALL:    scene_build_level(&g_scene, &g_world.level, dynamic); break;
    case WORLD_GEOM_MOVING: scene_rebuild_moving(&g_scene, &g_world.level); break;
    default: break;
    }

    scene_frame(&g_world, &g_scene, g_vw, g_vh, frozen);

    /* NO SwapBuffers. The browser presents whatever is in the drawing buffer
       when the callback returns, which is the one line of WinMain's loop that
       has no counterpart here rather than a different spelling of one.
       *SwapBuffers가 없습니다.* 콜백이 반환할 때 드로잉 버퍼에 있는 것을 브라우저가 제시합니다.
       WinMain 루프에서 이곳에 다른 철자가 아니라 아예 대응물이 없는 유일한 줄입니다. */
}

/* --- start-up / 시작 --- */

static void boot(void) {
    if (!glweb_make_context(CANVAS)) {
        plat_fatal("WebGL 2",
                   "This browser would not give a WebGL 2 context. "
                   "The game needs one and has no fallback below it.");
        return;
    }

    rd_init();
    decal_init();
    menu_init(0);
    save_init();
    size_to_window();
    audio_init();
    font_init();
    world_init(&g_world);
    scene_init(&g_scene, &g_world.weapon);

    if (!world_load_level(&g_world, g_world.cur_level, WORLD_ENTER_NEW)) {
        plat_fatal("level", "The first level would not load.");
        return;
    }
    menu_open_title();

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, EM_TRUE, on_key_down);
    emscripten_set_keyup_callback  (EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, EM_TRUE, on_key_up);
    emscripten_set_mousemove_callback(CANVAS, 0, EM_TRUE, on_mouse_move);
    emscripten_set_mousedown_callback(CANVAS, 0, EM_TRUE, on_mouse_down);
    emscripten_set_mouseup_callback  (CANVAS, 0, EM_TRUE, on_mouse_up);
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, EM_TRUE, on_resize);
    emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, 0,
                                              EM_TRUE, on_lock_change);

    /* The right mouse button is the meat hook, so the context menu has to go
       or every throw opens one.
       오른쪽 버튼은 고기 갈고리이므로 컨텍스트 메뉴는 사라져야 합니다. 그러지 않으면 던질
       때마다 하나씩 열립니다. */
    EM_ASM({
        var c = document.querySelector(UTF8ToString($0));
        if (c) c.addEventListener('contextmenu', function (e) { e.preventDefault(); });
    }, CANVAS);

    g_started = 1;
    g_prev_ms = emscripten_get_now();

    /* fps 0 means "whenever the browser is ready to paint", which is rAF and
       therefore the display's own rate. simulate_infinite_loop is 0 because
       ::main has already returned by now; passing 1 here would unwind through
       a stack that is not there.
       fps 0은 "브라우저가 그릴 준비가 될 때마다"이며 곧 rAF이고 따라서 디스플레이 자신의
       주기입니다. simulate_infinite_loop이 0인 것은 이 시점에 ::main이 이미 반환했기
       때문입니다. 이곳에 1을 넘기면 있지도 않은 스택을 풀게 됩니다. */
    emscripten_set_main_loop(frame, 0, 0);
}

EMSCRIPTEN_KEEPALIVE void sfps_boot(void) { boot(); }

int main(void) {
    /* THE MOUNT FIRST, THEN THE LOAD, THEN EVERYTHING. plat_save_dir creates
       the directory and mounts IDBFS over it; what it cannot do is wait for
       the contents, because they arrive through a callback and it returns a
       path. That wait is this file's, exactly as plat_web.c says.
       WHY IT GATES THE WHOLE START-UP rather than just save_init: the order
       app_start uses is the order it has to be, and save_init sits in the
       middle of it. Splitting the start-up around one async read would be two
       orders to keep in step instead of one.
       *마운트가 먼저, 그다음 로드, 그다음 전부입니다.* plat_save_dir이 디렉토리를 만들고 그
       위에 IDBFS를 마운트합니다. 그것이 할 수 없는 일은 내용을 기다리는 것입니다. 내용은
       콜백으로 도착하는데 그 함수는 경로를 반환하기 때문입니다. 그 기다림은 이 파일의 것이며,
       plat_web.c가 말하는 그대로입니다.
       *save_init만이 아니라 시작 전체를 막는 이유*는, app_start가 쓰는 순서가 그래야만 하는
       순서이고 save_init이 그 한가운데 있기 때문입니다. 비동기 읽기 하나를 두고 시작을 쪼개면
       보조를 맞춰야 할 순서가 하나가 아니라 둘이 됩니다. */
    char dir[256];
    plat_save_dir(dir, (int)sizeof(dir));

    EM_ASM({
        if (typeof FS === 'undefined' || typeof IDBFS === 'undefined') {
            _sfps_boot();
            return;
        }
        FS.syncfs(true, function (err) {
            /* A FAILED LOAD IS NOT A FAILED START. Nothing is corrupt; the
               save simply is not there, which is what a first launch looks
               like anyway. Refusing to boot over it would turn a missing
               unlock into a blank page.
               *실패한 로드가 실패한 시작은 아닙니다.* 무엇도 깨지지 않았고 저장이 그저 없을
               뿐이며, 그것은 어차피 첫 실행이 보이는 모습입니다. 그것 때문에 부팅을 거부하는
               것은 사라진 해금 하나를 빈 페이지로 바꾸는 일입니다. */
            if (err) console.warn('save not loaded: ' + err);
            _sfps_boot();
        });
    });

    /* The runtime has to outlive main, because everything above is a callback
       that has not happened yet.
       위의 모든 것이 아직 일어나지 않은 콜백이므로 런타임은 main보다 오래 살아야 합니다. */
    emscripten_exit_with_live_runtime();
    return 0;
}
