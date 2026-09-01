/* steptest -- run whole frames of the game with no window.
 *
 * main.c used to say, in its own header comment, that the order the per-frame
 * update ran in was "load-bearing rather than incidental" -- and then nothing
 * checked any of it, because the order lived in the body of WinMain and the
 * only way to run it was to open a window and play. Every other test in this
 * folder drops one module onto a fixture; none of them could reach the frame
 * that puts the modules together.
 *
 * world.c exists so that this file can. It names no GL function, no Win32 call
 * and no menu, so a World can be stepped here exactly as the game steps one.
 * What is asserted below is therefore not "does the player walk" -- movetest
 * owns that -- but "does a frame do its parts in the right order, and does it
 * stop the right things when the world is frozen".
 *
 * The fixtures are built here rather than loaded, for the reason movetest
 * learned: `arena` is a map somebody edits, and a test that names its
 * coordinates goes red on every edit. The one test that does load a level
 * asserts nothing about what is inside it.
 */

#include <stdio.h>
#include <string.h>   /* strcmp -- level names are compared by value */
#include <math.h>

#include "world.h"
#include "hook.h"
#include "enemy.h"    /* enemy_reset -- the monster pool is global, and shared */
#include "pickup.h"   /* pickup_spawn_level -- also takes the World's pool now */
#include "proj.h"     /* proj_reset -- now takes the World's own pool */
#include "door.h"     /* door_reset, and the DOOR_* axes */
#include "brush.h"    /* Brush::min/max and brush_point_in -- standing on a hazard */
#include "brush.h"    /* Brush::min/max and brush_point_in -- standing inside a teleport volume */
#include "diag.h"     /* diag_count -- a stale door is counted, not printed */
#include "story.h"    /* STORY_DEFEAT -- the screen that is now in front of the death one */
#include "txt.h"      /* txt_copy -- walking the level chain by name */
/* level_geometry, to fill the light cache the checks below watch being
   dropped. CPU side only: mb_init/mb_free need no GL context, and only
   mesh_upload would.
   아래 검사들이 버려지는 것을 지켜보는 라이트 캐시를 채우기 위한 level_geometry입니다.
   CPU 측뿐이며 mb_init/mb_free는 GL 컨텍스트가 필요 없습니다. */
#include "render.h"

#define DT     (1.0f / 60.0f)
#define ASPECT 1.7777f          /* 16:9. Only the muzzle solve reads it. */

static int fails;

/* Builds a level's geometry with a sun on it, and reports what that left in
 * the light cache.
 *
 * ENGLISH
 * -------
 * THE SUN IS PUT THERE BECAUSE NOTHING DECLARES ONE. ::bake_light is a sun
 * bake -- the point lamps left it, and then `lqdm1`'s worldspawn keys left too
 * -- so it returns on its first line for every level the game loads and the
 * cache it feeds stays empty. The checks below are about a cache being DROPPED
 * on the paths that make a level into a different level, and "the cache is
 * empty after a reload" is worth nothing if it was empty before.
 *
 * Overhead and bright, because the subject is the reset rather than the
 * lighting. Re-applied at every fill rather than once: each of the paths under
 * test reloads the level, and a reload is exactly what puts ::Level::sun back
 * to the zero the file declares.
 *
 * 한국어
 * ------
 * *태양을 이곳에서 놓는 이유는 그것을 선언하는 것이 없기 때문입니다.* ::bake_light는 태양
 * 베이크입니다. 점광원이 그것을 떠났고 그다음 `lqdm1`의 worldspawn 키도 떠났으므로, 게임이
 * 로드하는 모든 레벨에서 첫 줄에 반환하며 그것이 채우는 캐시는 비어 있습니다. 아래의 검사는
 * 레벨을 *다른* 레벨로 만드는 경로에서 캐시가 *버려지는지*에 관한 것이고, "다시 로드한 뒤
 * 캐시가 비어 있다"는 그 전에도 비어 있었다면 아무 가치가 없습니다.
 *
 * 주제가 조명이 아니라 리셋이므로 머리 위이고 밝은 태양입니다. 한 번이 아니라 채울 때마다 다시
 * 적용합니다. 검사 대상인 각 경로가 레벨을 다시 로드하고, 다시 로드하는 것이야말로
 * ::Level::sun을 파일이 선언하는 0으로 되돌리는 일이기 때문입니다.
 */
static int fill_light_cache(World *w, MeshBuf *b) {
    w->level.sun[0] = 0.0f; w->level.sun[1] = 1.0f; w->level.sun[2] = 0.0f;
    w->level.sun_power = 200;
    mb_reset(b);
    level_geometry(b, &w->level, 0, 0);
    return level_light_cache_count();
}

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void okf(int cond, const char *what, float got, float want) {
    printf("  %-58s %8.3f / %8.3f  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* ------------------------------------------------------------- fixtures */

/* Sector units are centimetres. `hurt` is damage per second standing on it. */
static void box(Level *l, short x0, short z0, short x1, short z1,
                short floor, short ceil, short hurt) {
    Sector *s = &l->sectors[l->n_sectors++];
    short p[8] = { x0,z0,  x1,z0,  x1,z1,  x0,z1 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4;
    s->floor = floor;
    s->ceil  = ceil;
    s->hurt  = hurt;
    /* level_load does this after parsing; a hand-built sector has to as well,
       or min_x..max_z are zero and the smoke sampler never finds it. */
    level_bounds(s);
}

/* A world mid-run, standing in the middle of one flat 40m room.
 *
 * world_init leaves the run on the title screen, which freezes everything --
 * correct for the game and useless for a test, so the fixture clears it.
 *
 * The pools are reset here too, and the list is getting shorter: the
 * projectiles now live in World::pools and are cleared by owning them, while
 * the monsters, the items and the doors are still file-scope arrays inside
 * their own modules and have to be emptied by hand or the previous case's
 * contents are still standing in this one. See pools.h.
 *
 * world_init은 플레이를 타이틀 화면 상태로 두며 그것은 모든 것을 정지시킵니다. 게임에는
 * 옳고 테스트에는 쓸모없으므로 픽스처가 해제합니다.
 *
 * 풀도 이곳에서 초기화하며, 그 목록은 이제 비어 있습니다. 발사체·아이템·몬스터는
 * World::pools에, 문의 움직임은 Level::door_run에 있고, 전부 소유하는 것만으로 비워집니다.
 * 남은 두 호출은 비우는 것이 아닙니다. pickup_spawn_level은 레벨의 아이템을 *배치*하고,
 * door_reset은 섹터가 존재한 뒤에야 가능한 "닫힘"의 스냅숏을 *포착*합니다. pools.h와
 * DoorSet을 참조하십시오. */
static void fixture(World *w, short hurt) {
    world_init(w);
    w->run.title = 0;

    box(&w->level, -2000, -2000, 2000, 2000, 0, 3000, hurt);

    w->player.pos      = v3f(0.0f, PLAYER_EYE, 0.0f);
    w->player.vel      = v3f(0.0f, 0.0f, 0.0f);
    w->player.grounded = 1;
    w->player.health   = PLAYER_MAX_HP;

    /* No enemy_reset, no proj_reset, no decal_reset, no fx cleanup. world_init
       cleared the whole World a few lines up and the pools are inside it now,
       so owning them IS emptying them. Those four calls were here because the
       pools were file-scope arrays and a fixture inherited whatever the
       previous case left in them; that is what World::pools removed.

       The two that remain are not pool resets. pickup_spawn_level LAYS OUT the
       level's items rather than clearing them, and door_reset CAPTURES each
       door's closed shape -- the state itself is Level::door_run now and was
       emptied with the rest of the World, but the snapshot of what "closed"
       means still has to be taken after the sectors exist.

       enemy_reset도 proj_reset도 decal_reset도 fx 정리도 없습니다. 몇 줄 위에서 world_init이
       World 전체를 비웠고 이제 풀이 그 안에 있으므로, 소유하는 것이 곧 비우는 것입니다. 그 네
       호출이 이곳에 있던 이유는 풀이 파일 스코프 배열이었고 픽스처가 이전 사례가 남긴 것을
       물려받았기 때문입니다. World::pools가 없앤 것이 바로 그것입니다.

       남은 둘은 풀 초기화가 아닙니다. pickup_spawn_level은 레벨의 아이템을 지우는 것이 아니라
       *배치*하며, 문은 여전히 레벨에 매인 모듈 상태입니다. door.c가 자기 DoorState 배열을
       보유하고 있고, 이 목록에서 아직 옮겨지지 않은 유일한 것입니다. */
    pickup_spawn_level(&w->pools, &w->level);
    door_reset(&w->level);
}

static Input idle(void) {
    Input in = {0};
    return in;
}

/* Two whole level names, compared. The parsers use txt_is, which wants a
   counted token on one side; both of these are already strings. */
static int same_name(const char *a, const char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

/* Marks every weapon `l` hands out. Written independently of the one in
   world.c so that comparing the two is a check rather than a tautology. */
static void weapons_in(const Level *l, int *owned) {
    for (int i = 0; i < l->n_ents; i++) {
        const char *k = l->ents[i].kind;
        int n = 0;
        while (n < LVL_KIND && k[n]) n++;
        int wp = PK_WEAPON_WEAPON(pickup_kind_for_n(k, n));
        if (wp >= 0 && wp < WP_TYPES) owned[wp] = 1;
    }
}

/* ------------------------------------------------------------------ main */

/* --- the view shake ---------------------------------------------------------
 *
 * Three facts, and the third is the one worth a test.
 *
 * A shake has to START on something the player did, it has to END on its own,
 * and it must NOT MOVE THE AIM. The first two would be noticed the first time
 * anybody played; the third would not, because a camera that drags the aim with
 * it feels like bad mouse handling rather than like a bug in a shake -- and the
 * obvious way to implement one, adding the offset to World::yaw, does exactly
 * that. So the aim is checked to the bit, before and after.
 *
 * 흔들림에 대한 세 가지 사실이며, 세 번째가 검사할 가치가 있는 것입니다.
 *
 * 흔들림은 플레이어가 한 무언가에서 *시작*해야 하고, 스스로 *끝*나야 하며, *조준을 움직여서는
 * 안 됩니다.* 앞의 둘은 누구든 처음 플레이할 때 알아챕니다. 세 번째는 아닙니다. 조준을 끌고
 * 다니는 카메라는 흔들림의 결함이 아니라 마우스 처리가 나쁜 것처럼 느껴지기 때문이며, 그것을
 * 구현하는 자명한 방법(World::yaw에 변위를 더하는 것)이 정확히 그렇게 만듭니다. 그래서 조준을
 * 전후로 비트 단위까지 검사합니다. */
static void check_shake(void) {
    printf("\nview shake\n");

    World w;
    fixture(&w, 0);

    ok(w.run.shake == 0.0f, "a fresh world is not shaking");

    /* Firing. The trigger is held for one step, which is all a shot needs. */
    float yaw0 = w.yaw, pitch0 = w.pitch;
    Input in = {0};
    in.fire = 1;
    world_step(&w, &in, 1.777f, 0.016f);

    ok(w.run.shake > 0.0f, "firing shakes the view");
    ok(w.yaw == yaw0 && w.pitch == pitch0,
       "and does not move the aim by so much as a bit");

    /* It ends on its own. Long enough that WORLD_SHAKE_DECAY has to have run;
       short enough that this is not just "eventually". */
    Input idle = {0};
    for (int i = 0; i < 40; i++) world_step(&w, &idle, 1.777f, 0.016f);
    ok(w.run.shake == 0.0f, "and it settles back to still");

    /* Taking a hit shakes harder than a shot, because it is scaled by what the
       hit was worth -- and a hit worth half the bar is not a shotgun. */
    fixture(&w, 0);
    world_step(&w, &in, 1.777f, 0.016f);
    float from_fire = w.run.shake;

    fixture(&w, 0);
    world_shake(&w, WORLD_SHAKE_HURT * 0.5f);
    ok(w.run.shake > from_fire, "a serious hit shakes harder than a shot");

    /* The loudest wins rather than the sum, so a busy frame does not launch
       the camera. Two quiet events must not add up to a loud one. */
    fixture(&w, 0);
    world_shake(&w, 0.4f);
    world_shake(&w, 0.2f);
    ok(w.run.shake == 0.4f, "a quieter source does not add to a louder one");
    world_shake(&w, 0.7f);
    ok(w.run.shake == 0.7f, "and a louder one replaces it");

    /* Clamped, so no pile-up leaves the camera in the next room. */
    world_shake(&w, 99.0f);
    ok(w.run.shake == WORLD_SHAKE_MAX, "and the magnitude is capped");

    /* --- and now one that happened SOMEWHERE ------------------------------
       The three above are things that happened TO the player -- their gun,
       their health, their landing -- and none of them has a position, because
       the player was always at it. A blast is the first source that has one,
       so the first that can be wrong about it: the whole of ::step_blast is
       the arithmetic between where it went off and where the camera is.

       Driven through ::world_step rather than by reaching into ::step_blast,
       because the half that is easy to get wrong is not the falloff -- it is
       whether the frame ever asks. ::proj_flash records into the pool and
       world.c walks it, and a walk that was never wired up leaves a falloff
       that is perfectly correct and never runs.

       RECORDED WITH ::proj_flash AND NOT ::proj_blast, which is the one thing
       to know when reading this beside the damage tests. They are two calls
       because they are two questions -- who got hurt, and who saw it -- and a
       blast that lit the room without hurting anybody is a decoration this
       engine is allowed to have.

       위의 셋은 플레이어*에게* 일어난 일(자기 총, 자기 체력, 자기 착지)이며 어느 것도 위치를
       갖지 않습니다. 플레이어가 언제나 그 자리에 있었기 때문입니다. 폭발은 위치를 가진 첫
       원천이므로, 그 위치에 대해 틀릴 수 있는 첫 원천이기도 합니다. ::step_blast의 전부가
       터진 자리와 카메라가 있는 자리 사이의 산술입니다.

       ::step_blast에 직접 손을 뻗지 않고 ::world_step을 통해 구동하는 이유는, 틀리기 쉬운
       절반이 감쇠가 아니기 때문입니다. 그것은 프레임이 *묻기는 하는가*입니다. ::proj_flash가
       풀에 기록하고 world.c가 그것을 훑는데, 연결되지 않은 순회는 완벽하게 올바르면서 결코
       실행되지 않는 감쇠를 남깁니다.

       *::proj_blast가 아니라 ::proj_flash로 기록합니다.* 피해 검사들 곁에서 이것을 읽을 때
       알아야 할 한 가지입니다. 둘이 별개의 호출인 이유는 별개의 질문이기 때문입니다. 누가
       다쳤는가와 누가 그것을 보았는가이며, 아무도 다치게 하지 않고 방만 밝힌 폭발은 이 엔진이
       가져도 되는 연출입니다. */
    fixture(&w, 0);
    proj_flash(&w.pools, v3f(0.0f, 0.0f, -2.0f),
               PROJ_BLAST_RADIUS, 1.0f, FLASH_BLAST, -1);
    world_step(&w, &idle, 1.777f, 0.016f);
    float from_near = w.run.shake;
    ok(from_near > 0.0f, "a blast beside the player shakes the view");

    fixture(&w, 0);
    proj_flash(&w.pools, v3f(0.0f, 0.0f, -8.0f),
               PROJ_BLAST_RADIUS, 1.0f, FLASH_BLAST, -1);
    world_step(&w, &idle, 1.777f, 0.016f);
    ok(w.run.shake > 0.0f && w.run.shake < from_near,
       "one twice as far away shakes, and shakes less");

    /* Past ::WORLD_SHAKE_BLAST_REACH radii there is nothing, which is the
       claim the reach makes: a grenade across the level is somebody else's
       problem.
       반경의 ::WORLD_SHAKE_BLAST_REACH배를 넘으면 아무것도 없습니다. 도달 거리가 하는 주장이
       그것입니다. 레벨 건너편의 유탄은 남의 일입니다. */
    fixture(&w, 0);
    proj_flash(&w.pools,
               v3f(0.0f, 0.0f,
                   -(PROJ_BLAST_RADIUS * WORLD_SHAKE_BLAST_REACH + 1.0f)),
               PROJ_BLAST_RADIUS, 1.0f, FLASH_BLAST, -1);
    world_step(&w, &idle, 1.777f, 0.016f);
    ok(w.run.shake == 0.0f, "and one past the reach does not shake at all");

    /* THE MIDDLE SATURATES, and that is the point of ::WORLD_SHAKE_BLAST being
       set past ::WORLD_SHAKE_MAX rather than just under it. What the player
       reads off a blast is near against far, and spending the top of the range
       distinguishing the strongest jolt from the second strongest would cost
       exactly that.
       *한가운데는 포화합니다.* ::WORLD_SHAKE_BLAST를 ::WORLD_SHAKE_MAX 바로 아래가 아니라 그
       위로 잡은 이유가 그것입니다. 플레이어가 폭발에서 읽는 것은 가까움과 멂이며, 가장 센
       충격과 두 번째로 센 충격을 구별하는 데 범위의 꼭대기를 쓰는 것은 정확히 그것을
       희생합니다. */
    fixture(&w, 0);
    proj_flash(&w.pools, w.player.pos, PROJ_BLAST_RADIUS, 1.0f, FLASH_BLAST, -1);
    world_step(&w, &idle, 1.777f, 0.016f);
    ok(w.run.shake == WORLD_SHAKE_MAX,
       "and one at the player's feet reaches the cap");

    /* SPENT, not remembered. A flash lives ::PROJ_FLASH_TIME and fades over it,
       and ::WORLD_SHAKE_DECAY outruns that curve within two frames -- so one
       explosion is one jolt. If the flash were re-raising at full strength on
       every frame it lived, the shake would sit at the ceiling for a third of a
       second and then drop, which is a different bug with the same first frame.
       기억이 아니라 *소비*입니다. 섬광은 ::PROJ_FLASH_TIME 동안 살며 그동안 사그라들고,
       ::WORLD_SHAKE_DECAY가 두 프레임 안에 그 곡선을 앞지릅니다. 그래서 폭발 하나는 충격
       하나입니다. 섬광이 사는 매 프레임마다 최대 세기로 다시 올린다면 흔들림은 0.3초 동안
       천장에 머물다 떨어질 텐데, 그것은 첫 프레임이 같은 다른 버그입니다. */
    fixture(&w, 0);
    proj_flash(&w.pools, v3f(0.0f, 0.0f, -2.0f),
               PROJ_BLAST_RADIUS, 1.0f, FLASH_BLAST, -1);
    for (int i = 0; i < 40; i++) world_step(&w, &idle, 1.777f, 0.016f);
    ok(w.run.shake == 0.0f, "and one blast is one jolt, not one per frame");

    /* A restart puts it back, by construction rather than by being listed --
       which is the whole reason it lives in RunState. */
    run_reset(&w.run, 0);
    ok(w.run.shake == 0.0f, "a restart clears it");
}

/* --- the score a run carries to its own end screen ---------------------------
 *
 * Two numbers, and neither of them is visible from inside the run: how many the
 * player took down, and how long they stayed up. The game is an arena survived
 * rather than a chain of levels finished, so these ARE the result -- and both
 * are accumulated by ::world_step, which means both are exactly the kind of
 * bookkeeping that goes quietly wrong and is noticed months later on a screen
 * nobody reaches on purpose.
 *
 * WHAT IS ACTUALLY WORTH PINNING. Not "does a kill count" -- that fails on the
 * first playtest. The two that do not are: a corpse must be paid out ONCE,
 * because it lies in its slot for ::CORPSE_FADE seconds after it dies and a
 * counter that re-reads the pool would count it every frame it lies there; and
 * the survival clock must NOT be ::RunState::world_time, which wraps at
 * ::WORLD_TIME_WRAP because it drives animated materials. Sharing that clock
 * would tell a player who lasted five minutes that they lasted twenty seconds,
 * and it would only ever happen to the players who did best.
 *
 * 한국어
 * ------
 * 두 개의 숫자이며, 어느 쪽도 플레이 안에서는 보이지 않습니다. 몇을 쓰러뜨렸는가, 그리고
 * 얼마나 오래 서 있었는가입니다. 이 게임은 끝내는 레벨의 사슬이 아니라 살아남는 아레나이므로
 * 이것이 곧 결과입니다. 그리고 둘 다 ::world_step이 누적하는데, 그것은 조용히 잘못되었다가 몇
 * 달 뒤 아무도 일부러 도달하지 않는 화면에서 발견되는 바로 그런 종류의 기록입니다.
 *
 * 무엇을 고정할 가치가 있는가. "처치가 세어지는가"는 아닙니다. 그것은 첫 플레이테스트에서
 * 실패합니다. 그렇지 않은 둘은 이것입니다. 시체는 *한 번* 지급되어야 합니다. 죽은 뒤
 * ::CORPSE_FADE초 동안 자기 슬롯에 누워 있으므로, 풀을 다시 읽는 계수기는 누워 있는 모든
 * 프레임마다 그것을 셉니다. 그리고 생존 시계는 ::RunState::world_time이어서는 안 됩니다.
 * 그것은 애니메이션 재질을 구동하기에 ::WORLD_TIME_WRAP에서 순환합니다. 그 시계를 함께 쓰면
 * 5분을 버틴 플레이어에게 20초를 버텼다고 말하게 되며, 그런 일은 가장 잘한 플레이어에게만
 * 일어납니다. */
/* --- a monster the level put there, four metres in front of the player --- */
static void put_monster(World *w, short x, short z) {
    Entity *e = &w->level.ents[w->level.n_ents++];
    e->kind[0]='s'; e->kind[1]='p'; e->kind[2]='a'; e->kind[3]='w';
    e->kind[4]='n'; e->kind[5]=0;
    e->x = x; e->z = z;
    enemy_spawn_level(&w->pools, &w->level);
}

/* --- the three artifacts actually do their three things -------------------
 *
 * ENGLISH
 * -------
 * EACH IS A CLOCK AND A READER, and both halves fail silently. A clock that
 * runs with nothing reading it is a HUD number attached to nothing; a reader
 * wired to a knob ::world_step never sets is a powerup that is always on, or
 * never. Neither shows up in a compile, and neither showed up in the other 38
 * checks -- the suite went green the first time with all three effects
 * untested, which is the whole reason this function exists.
 *
 * So each is driven END TO END, from `Player::power` through the knob to
 * something a player would see: damage landing on a monster, damage landing on
 * the player, and a monster's own decision about whether to walk over.
 *
 * 한국어
 * ------
 * *각각은 시계와 그것을 읽는 무언가*이고, 두 쪽 다 조용히 실패합니다. 아무도 읽지 않는 시계는
 * 아무것에도 붙어 있지 않은 HUD 숫자이고, ::world_step이 설정하지 않는 손잡이에 연결된 독자는
 * 항상 켜져 있거나 절대 켜지지 않는 파워업입니다. 어느 쪽도 컴파일에 나타나지 않으며 나머지 38개
 * 검사에도 나타나지 않았습니다. 이 suite는 세 효과가 전혀 검사되지 않은 채로 처음에 초록이었고,
 * 그것이 이 함수가 존재하는 이유 전부입니다.
 *
 * 그래서 각각은 `Player::power`에서 손잡이를 거쳐 플레이어가 볼 무언가까지 *끝에서 끝까지*
 * 구동됩니다. 몬스터에게 꽂히는 피해, 플레이어에게 꽂히는 피해, 그리고 걸어올지에 대한 몬스터
 * 자신의 결정입니다.
 */
static void check_power(void) {
    printf("\nthe three artifacts\n");

    World w;
    Input in;

    /* --- the clocks, and the knobs that follow them ----------------------
       The knobs are set every frame from the clock rather than once on pickup,
       so the half worth checking is not that a fresh pickup turns one ON. It is
       that a clock reaching zero turns it OFF with nobody having been told.
       손잡이는 획득 시점에 한 번이 아니라 매 프레임 시계로부터 설정되므로, 검사할 값어치가 있는
       쪽은 갓 주운 것이 하나를 *켠다*는 것이 아닙니다. 0에 닿은 시계가 아무도 듣지 않은 채로
       그것을 *끈다*는 것입니다. */
    fixture(&w, 0);
    in = idle();
    world_step(&w, &in, ASPECT, DT);
    ok(w.weapon.damage_mul == 1 && w.pools.enemy.blinded == 0,
       "nothing runs on a fresh player, and both knobs say so");

    w.player.power[PW_QUAD]   = 3.0f * DT;
    w.player.power[PW_SHADOW] = 3.0f * DT;
    world_step(&w, &in, ASPECT, DT);
    ok(w.weapon.damage_mul == PLAYER_QUAD_MUL && w.pools.enemy.blinded == 1,
       "a running clock reaches the weapon and the monsters");

    for (int i = 0; i < 4; i++) world_step(&w, &in, ASPECT, DT);
    okf(w.player.power[PW_QUAD] == 0.0f,
        "the clock runs out and does not go negative",
        w.player.power[PW_QUAD], 0.0f);
    ok(w.weapon.damage_mul == 1 && w.pools.enemy.blinded == 0,
       "and both knobs let go by themselves");

    /* --- PW_QUAD: the same shot, four times ------------------------------
       Two fresh worlds fired identically. They are comparable because a fresh
       World seeds the same way, so the shotgun throws its pellets along the
       same spread in both -- the only difference between the runs is the clock.
       동일하게 발사된 새 월드 둘입니다. 새 World는 같은 방식으로 시드되므로 샷건이 두 경우 모두
       같은 산포로 펠릿을 던집니다. 두 실행의 유일한 차이는 시계입니다. */
    int hp_plain, hp_quad;
    for (int quad = 0; quad < 2; quad++) {
        fixture(&w, 0);
        put_monster(&w, 0, -400);
        if (quad) w.player.power[PW_QUAD] = PLAYER_POWER_TIME;

        in = idle();
        in.fire = 1;
        for (int i = 0; i < 12; i++) world_step(&w, &in, ASPECT, DT);

        int hp = enemy_alive(&w.pools) ? enemy_at(&w.pools, 0)->health : 0;
        if (quad) hp_quad = hp; else hp_plain = hp;
    }
    ok(hp_plain > 0, "one blast leaves the monster standing");
    okf(hp_quad < hp_plain, "and the same blast under the quad does not",
        (float)hp_quad, (float)hp_plain);

    /* --- PW_AEGIS: the same hazard, cut ----------------------------------
       A hazard floor rather than a monster, because a hazard charges the same
       number every frame -- a monster's damage depends on whether it chose to
       swing, and this needs the two runs to differ in one thing only.
       몬스터가 아니라 유해 바닥인 이유는, 유해 지형이 매 프레임 같은 수를 물리기 때문입니다.
       몬스터의 피해는 그것이 휘두르기로 했는지에 달려 있고, 이 검사는 두 실행이 한 가지에서만
       달라야 합니다. */
    int lost_plain, lost_aegis;
    for (int aegis = 0; aegis < 2; aegis++) {
        fixture(&w, 40);
        if (aegis) w.player.power[PW_AEGIS] = PLAYER_POWER_TIME;

        int before = w.player.health;
        in = idle();
        for (int i = 0; i < 30; i++) world_step(&w, &in, ASPECT, DT);

        int lost = before - w.player.health;
        if (aegis) lost_aegis = lost; else lost_plain = lost;
    }
    ok(lost_plain > 0, "standing in a hazard costs health");
    okf(lost_aegis < lost_plain, "and standing in it under the aegis costs less",
        (float)lost_aegis, (float)lost_plain);
    ok(lost_aegis > 0,
       "but still costs some -- a cut is not an immunity");

    /* --- PW_SHADOW: the monster does not shoot ---------------------------
       DAMAGE TAKEN rather than distance closed, because the fixture's monster
       is a water spirit and a water spirit is a CASTER: seeing the player, it
       holds its range and opens fire, and the ground it gives up is a metre of
       repositioning that a threshold could not tell from idling. What it does
       instead is unmistakable at the player's end -- the health bar moves.

       Nor `Enemy::seen`, which is the CACHE: a blinded monster is answered
       before the cache is ever consulted, so the field it leaves behind is the
       same 0 that a monster which has simply not looked yet leaves.
       거리가 아니라 *받은 피해*인 이유는, 픽스처의 몬스터가 물 정령이고 물 정령은 *캐스터*이기
       때문입니다. 플레이어를 보면 사거리를 지키며 사격을 시작하고, 내주는 땅은 1미터의 자리
       고쳐잡기라서 어떤 문턱값으로도 가만히 있는 것과 구별할 수 없습니다. 대신 그것이 하는 일은
       플레이어 쪽 끝에서 명백합니다. 체력 막대가 움직입니다.

       `Enemy::seen`도 아닙니다. 그것은 *캐시*이고, 눈먼 몬스터는 캐시를 보기도 전에 답을
       받으므로 남기는 필드는 아직 보지 않았을 뿐인 몬스터가 남기는 것과 같은 0입니다. */
    int shot_plain, shot_shadow;
    for (int shadow = 0; shadow < 2; shadow++) {
        fixture(&w, 0);
        put_monster(&w, 0, -800);
        if (shadow) w.player.power[PW_SHADOW] = PLAYER_POWER_TIME;

        int before = w.player.health;
        in = idle();
        for (int i = 0; i < 300; i++) world_step(&w, &in, ASPECT, DT);

        int lost = before - w.player.health;
        if (shadow) shot_shadow = lost; else shot_plain = lost;
    }
    okf(shot_plain > 0, "a monster that can see the player shoots it",
        (float)shot_plain, 1.0f);
    okf(shot_shadow == 0, "and one that cannot does not",
        (float)shot_shadow, 0.0f);
}

static void check_score(void) {
    printf("\nthe run's own score\n");

    /* One monster, put in by the level rather than by hand, so what is counted
       is a monster the game made.
       손이 아니라 레벨이 넣은 몬스터 하나입니다. 그래야 세어지는 것이 게임이 만든 몬스터입니다. */
    World w;
    fixture(&w, 0);
    {
        Entity *e = &w.level.ents[w.level.n_ents++];
        e->kind[0]='s'; e->kind[1]='p'; e->kind[2]='a'; e->kind[3]='w';
        e->kind[4]='n'; e->kind[5]=0;
        e->x = -1500; e->z = -1500;
    }
    enemy_spawn_level(&w.pools, &w.level);
    ok(enemy_count(&w.pools) == 1, "one monster is standing in the room");

    ok(w.run.kills == 0 && w.run.alive_time == 0.0f,
       "a fresh run has killed nothing and survived nothing");

    /* --- the clock ------------------------------------------------------- */
    Input in = idle();
    world_step(&w, &in, ASPECT, DT);
    okf(fabsf(w.run.alive_time - DT) < 1e-6f,
        "a played frame is a frame survived", w.run.alive_time, DT);

    float held = w.run.alive_time;
    in = idle();
    in.paused = 1;
    for (int i = 0; i < 30; i++) world_step(&w, &in, ASPECT, DT);
    okf(w.run.alive_time == held,
        "and half a second behind the pause menu is not",
        w.run.alive_time, held);

    /* --- the kill -------------------------------------------------------- */
    enemy_hurt(&w.pools, 0, 10000, v3f(0.0f, 0.0f, 1.0f));
    ok(enemy_alive(&w.pools) == 0, "the monster is down");
    ok(w.run.kills == 0,
       "which the run does not know until a frame is stepped");

    in = idle();
    world_step(&w, &in, ASPECT, DT);
    ok(w.run.kills == 1, "and then it does");

    /* THE ONE THAT WOULD NOT BE NOTICED. The corpse is still in its slot and
       still dead, so anything that answers "how many dead monsters are there"
       instead of "how many died since you last asked" counts it again every
       frame -- and reports several hundred kills for one monster.
       알아채지 못할 바로 그것입니다. 시체는 여전히 자기 슬롯에 죽은 채로 있으므로, "죽은
       몬스터가 몇인가"에 답하는 것은 "마지막으로 물은 뒤 몇이 죽었는가" 대신 매 프레임 그것을
       다시 세고, 몬스터 하나로 수백 처치를 보고합니다. */
    for (int i = 0; i < 60; i++) world_step(&w, &in, ASPECT, DT);
    ok(w.run.kills == 1, "a corpse is paid out once, not once per frame");

    /* --- the clock that must not wrap -------------------------------------
       Both are pushed to the edge and stepped over it, which is the only way to
       see the difference: they advance together for the whole of a normal run
       and part company exactly once, at 4m39s.
       둘 다 경계까지 밀어 넣고 그 너머로 진행시킵니다. 차이를 볼 수 있는 방법은 그것뿐입니다.
       평범한 플레이 내내 함께 나아가다가 정확히 한 번, 4분 39초에 갈라섭니다. */
    w.run.world_time = WORLD_TIME_WRAP - 0.01f;
    w.run.alive_time = WORLD_TIME_WRAP - 0.01f;
    for (int i = 0; i < 5; i++) world_step(&w, &in, ASPECT, DT);

    ok(w.run.world_time < 1.0f,
       "the material clock wraps, because only its phase means anything");
    okf(w.run.alive_time > WORLD_TIME_WRAP,
        "the survival clock does not, because its value is the answer",
        w.run.alive_time, WORLD_TIME_WRAP);

    /* And a restart puts both back, by construction rather than by being
       listed -- the whole reason they live in RunState.
       그리고 재시작이 둘 다 되돌립니다. 나열되어서가 아니라 구조적으로이며, 그것이 이들이
       RunState에 사는 이유 전부입니다. */
    run_reset(&w.run, 0);
    ok(w.run.kills == 0 && w.run.alive_time == 0.0f, "a restart clears both");
}

/* --- a teleporter actually moves the player ------------------------------
 *
 * A DIFFERENT CLAIM FROM THE ONE leveltest MAKES. That file asserts the two
 * volumes parsed and found the points they name, which is about level.c. This
 * asserts that walking into one goes somewhere, which is about world.c -- and
 * the half that is easy to get wrong is not the arithmetic, it is whether
 * ::world_step ever asks. A ::step_teleport that was never called leaves a
 * perfectly correct teleporter that never fires, exactly as the blast falloff
 * would have.
 *
 * Driven on the SHIPPED arena rather than a fixture, because a teleporter needs
 * a brush level and the fixture here is a sector box -- and because the thing
 * worth knowing is that the arena's own two work, not that a synthetic one does.
 *
 * leveltest가 하는 주장과 다른 주장입니다. 그쪽은 부피 둘이 파싱되어 자기가 지목한 점을
 * 찾았다고 단언하며 그것은 level.c에 대한 것입니다. 이곳은 그 안으로 걸어 들어가면 어딘가로
 * 간다고 단언하며 그것은 world.c에 대한 것입니다. 그리고 틀리기 쉬운 절반은 산술이 아니라
 * ::world_step이 *묻기는 하는가*입니다. 호출되지 않는 ::step_teleport는 완벽하게 올바르면서
 * 결코 발동하지 않는 텔레포터를 남기며, 폭발 감쇠가 그러했을 것과 똑같습니다.
 *
 * 픽스처가 아니라 *출하 아레나*에서 구동하는 이유는, 텔레포터에 브러시 레벨이 필요한데 이곳의
 * 픽스처는 섹터 상자이기 때문이고, 알아야 할 것이 합성된 것 하나가 아니라 아레나 자신의 둘이
 * 작동한다는 사실이기 때문입니다. */
static void check_teleport(void) {
    printf("\nteleporters\n");

    static World w;
    world_init(&w);
    w.run.title = 0;

    if (!world_load_level(&w, "lqdm4", WORLD_ENTER_NEW)) {
        ok(0, "the shipped arena loads into a world");
        return;
    }
    ok(1, "the shipped arena loads into a world");

    if (w.level.n_teleports < 1 || !w.level.brushes) {
        ok(0, "and it has a teleporter to step into");
        return;
    }

    const TeleportDef *t = &w.level.teleports[0];
    v3 dest = t->dest;

    /* The middle of the volume's first brush. ::Brush carries its own bounding
       box, so this is two adds and a halve -- and `brush_point_in` is asked
       whether that point really is inside before anything is concluded from
       standing there. A trigger volume shaped like an L would put its AABB
       centre outside itself, and the test would then be measuring nothing.
       부피의 첫 브러시 한가운데입니다. ::Brush가 자기 바운딩 박스를 지니므로 덧셈 둘과 반으로
       나누기 하나입니다. 그리고 그 점이 정말 안에 있는지를 `brush_point_in`에게 먼저 묻습니다.
       L자 모양의 트리거 부피는 자기 AABB 중심이 자기 바깥에 놓이고, 그러면 이 검사는 아무것도
       재지 않게 됩니다. */
    const Brush *b = &w.level.brushes->brushes[t->first_brush];
    v3 mid = v3scale(v3add(b->min, b->max), 0.5f);
    ok(brush_point_in(w.level.brushes, t->first_brush, t->n_brushes, mid),
       "the middle of the volume is inside the volume");

    float away = v3len(v3sub(mid, dest));
    ok(away > 1.0f, "and the destination is somewhere else");

    /* Stand in it and let one frame happen. */
    Input in; memset(&in, 0, sizeof in);
    w.player.pos      = mid;
    w.player.vel      = v3f(0.0f, 0.0f, 0.0f);
    w.player.grounded = 1;

    world_step(&w, &in, 1.777f, 0.016f);

    /* The marker is the feet; ::Player::pos is the eye. Compared where the
       engine puts it rather than where the .map wrote it -- see ::step_teleport.
       표식은 발이고 ::Player::pos는 눈입니다. .map이 적은 자리가 아니라 엔진이 놓는 자리에서
       비교합니다. ::step_teleport를 참조하십시오. */
    v3 eye_dest = v3f(dest.x, dest.y + PLAYER_EYE, dest.z);
    float landed = v3len(v3sub(w.player.pos, eye_dest));
    okf(landed < 0.001f, "one frame in it puts the player at the destination",
        landed, 0.0f);
    okf(fabsf(w.yaw - t->yaw) < 0.001f, "facing the way it says",
        w.yaw, t->yaw);

    /* AND IT DOES NOT FIRE AGAIN. The destination is outside the volume, so the
       next frame should be an ordinary one -- if the player is still being
       moved every frame, this is a teleporter that has caught them rather than
       one they used.
       *그리고 다시 발동하지 않습니다.* 목적지는 부피 바깥이므로 다음 프레임은 평범한
       프레임이어야 합니다. 매 프레임 계속 옮겨지고 있다면 그것은 플레이어가 쓴 텔레포터가
       아니라 그를 붙잡은 텔레포터입니다. */
    v3 after = w.player.pos;
    world_step(&w, &in, 1.777f, 0.016f);
    okf(v3len(v3sub(w.player.pos, after)) < 1.0f,
        "and the frame after is an ordinary one",
        v3len(v3sub(w.player.pos, after)), 0.0f);

    /* THE SLOT BACK. LVL_BRUSH_SLOTS is small and a brush level holds one
       for as long as it is loaded; leaking it here made the NEXT test that
       wanted the arena fail to load it, and load_brush_level reports that by
       falling through to the text loader -- so the symptom was "the shipped
       arena loads" going red in a function that had not touched it.
       *칸을 돌려줍니다.* LVL_BRUSH_SLOTS는 작고 브러시 레벨은 로드되어 있는 동안
       하나를 붙듭니다. 이곳에서 흘리면 아레나를 원하는 *다음* 검사가 그것을 로드하지
       못했고, load_brush_level은 그것을 텍스트 로더로 떨어뜨려 보고하므로, 증상은
       그것을 건드리지도 않은 함수에서 "출하 아레나가 로드된다"가 빨개지는 것이었습니다. */
    level_release(&w.level);
}


/* --- the lava sea burns what stands on it --------------------------------
 *
 * THREE CLAIMS, AND THE FIRST IS A DEPENDENCY RATHER THAN A FEATURE.
 * ::brush_lava_of leaves the lava brush SOLID -- you stand on this lava, you do
 * not sink into it -- so a player on its surface is not inside the volume in
 * any ordinary sense. It works because ::brush_point_in rejects on
 * `dot(n,p) - dist > 0` and therefore counts a point exactly ON a face as in.
 * That is a real thing to lean on and an easy thing to change by accident, so
 * it is asserted directly.
 *
 * The second is that the sea is REACHABLE. A hazard nothing can walk onto is a
 * texture with a rule attached, and `lqdm4`'s lava spans the whole footprint --
 * most of it under the building. The scan below is not test scaffolding, it is
 * the claim: of the columns that are over lava, most of them are places a body
 * could actually stand.
 *
 * The third is that ::world_step charges it. A hazard that reads correctly and
 * is never applied is the shape ::step_blast and ::step_teleport could both
 * have had, and the only way to tell is to run frames and look at the health.
 *
 * THE SCAN IS ALSO WHY THIS TEST EXISTS IN THIS FORM. Its first draft stood the
 * player at the centre of the lava brush's bounding box, which for a slab that
 * spans the map is inside the central structure -- `level_ground` reported no
 * floor, the player fell through the world, and the failure looked exactly like
 * "solid lava does not collide". The bug was in the test. A map-wide AABB has
 * no meaningful centre, and anything that wants a point ON such a brush has to
 * go and find one.
 *
 * 세 가지 주장이며, 첫 번째는 기능이 아니라 *의존*입니다. ::brush_lava_of는 용암 브러시를
 * *고체*로 둡니다. 이 용암은 밟고 서는 것이지 잠기는 것이 아니므로, 표면 위의 플레이어는
 * 평범한 의미에서 부피 안에 있지 않습니다. 이것이 통하는 이유는 ::brush_point_in이
 * `dot(n,p) - dist > 0`으로 거절하여 면 위에 정확히 놓인 점을 안쪽으로 세기 때문입니다.
 * 기대는 실제 성질이면서 실수로 바꾸기 쉬운 것이므로 직접 단언합니다.
 *
 * 두 번째는 그 바다에 *닿을 수 있다*는 것입니다. 아무도 걸어 들어갈 수 없는 위험은 규칙이
 * 붙은 텍스처이며, `lqdm4`의 용암은 전체 footprint에 걸쳐 있고 그 대부분은 건물 아래입니다.
 * 아래의 훑기는 검사용 발판이 아니라 주장 그 자체입니다. 용암 위에 있는 기둥들 중 대부분이
 * 실제로 몸이 설 수 있는 자리라는 것입니다.
 *
 * 세 번째는 ::world_step이 그것을 물린다는 것입니다. 올바르게 읽히면서 결코 적용되지 않는
 * 위험은 ::step_blast와 ::step_teleport가 둘 다 가질 수 있었던 모양이며, 알아내는 유일한
 * 방법은 프레임을 돌리고 체력을 보는 것입니다.
 *
 * *그리고 이 훑기가 이 검사가 이런 형태인 이유이기도 합니다.* 초안은 플레이어를 용암 브러시
 * 바운딩 박스의 *중심*에 세웠는데, 맵 전체에 걸친 슬래브에게 그곳은 중앙 구조물 *안*입니다.
 * `level_ground`가 바닥 없음을 보고했고, 플레이어는 세계를 뚫고 떨어졌으며, 실패는 정확히
 * "고체 용암이 충돌하지 않는다"처럼 보였습니다. 버그는 검사에 있었습니다. 맵 크기의 AABB에는
 * 의미 있는 중심이 없고, 그런 브러시 *위의* 한 점을 원하는 것은 그것을 찾아 나서야 합니다. */
static void check_lava(void) {
    printf("\nthe lava sea\n");

    static World w;
    world_init(&w);
    w.run.title = 0;

    if (!world_load_level(&w, "lqdm4", WORLD_ENTER_NEW)) {
        ok(0, "the shipped arena loads into a world");
        return;
    }
    if (w.level.n_hazards < 1 || !w.level.brushes) {
        ok(0, "and its lava was found by texture");
        level_release(&w.level);
        return;
    }
    ok(1, "the arena's lava was found by texture, with no trigger_hurt in the map");

    const HazardDef *h = &w.level.hazards[0];
    const Brush *b = &w.level.brushes->brushes[h->first_brush];
    float top = b->max.y;

    /* A column that is over the lava AND has it as its floor. */
    int cols = 0, standable = 0;
    float sx = 0.0f, sz = 0.0f;
    for (int ix = -30; ix <= 30; ix++)
        for (int iz = -30; iz <= 30; iz++) {
            float px = (float)ix * 2.0f, pz = (float)iz * 2.0f;
            float gf, gc;
            if (level_hazard_at(&w.level, px, top, pz) <= 0) continue;
            cols++;
            if (level_ground(&w.level, px, pz, top + 0.5f, 2.0f, &gf, &gc) &&
                gf > top - 0.2f && gf < top + 0.2f) {
                if (!standable) { sx = px; sz = pz; }
                standable++;
            }
        }
    printf("      %d columns over lava, %d of them standable\n", cols, standable);
    ok(cols > 0, "the sea covers ground the level actually has");
    ok(standable * 2 > cols,
       "and most of it is a floor a body could be standing on");

    okf(level_hazard_at(&w.level, sx, top, sz) == LVL_HURT_LAVA,
        "its surface reports lava damage, felt through LVL_HAZARD_UNDERFOOT",
        (float)level_hazard_at(&w.level, sx, top, sz), (float)LVL_HURT_LAVA);
    ok(level_hazard_at(&w.level, sx, top + 4.0f, sz) == 0,
       "and four metres above it is clear air");

    /* Stand in it for a second. */
    Input in; memset(&in, 0, sizeof in);
    w.player.pos      = v3f(sx, top + PLAYER_EYE, sz);
    w.player.vel      = v3f(0.0f, 0.0f, 0.0f);
    w.player.grounded = 1;
    w.player.health   = PLAYER_MAX_HP;
    w.run.hazard_accum = 0.0f;

    for (int i = 0; i < 62; i++) world_step(&w, &in, 1.777f, 0.016f);
    int lost = PLAYER_MAX_HP - w.player.health;
    printf("      a second of standing in it cost %d health\n", lost);
    ok(lost >= 30 && lost <= 50, "a second in it is most of half the bar");

    /* AND CLIPPING A CORNER IS SURVIVABLE, which is the other half of the rate.
       ::step_damage's own note says a lava channel has to stay crossable by
       jumping or by the hook, "otherwise the room is a wall rather than an
       obstacle, and the momentum systems this game is built on have nothing to
       do there".
       *그리고 모서리를 스치는 것은 살아남을 수 있으며*, 그것이 이 비율의 나머지 절반입니다.
       ::step_damage 자신의 주석이 용암 수로는 뛰어넘거나 갈고리로 건널 수 있어야 한다고
       말합니다. "그러지 않으면 방은 장애물이 아니라 벽이고, 이 게임이 그 위에 세워진 운동량
       체계는 그곳에서 할 일이 없습니다." */
    w.player.pos       = v3f(sx, top + PLAYER_EYE, sz);
    w.player.grounded  = 1;
    w.player.health    = PLAYER_MAX_HP;
    w.run.hazard_accum = 0.0f;
    for (int i = 0; i < 12; i++) world_step(&w, &in, 1.777f, 0.016f);
    int clipped = PLAYER_MAX_HP - w.player.health;
    printf("      a fifth of a second cost %d\n", clipped);
    ok(clipped > 0 && clipped < PLAYER_MAX_HP / 2,
       "and clipping a corner of it is survivable");

    /* EVERY PLACE THE ARENA PUTS A GROUND MONSTER IS GROUND A GROUND MONSTER
       MAY USE. ::make_monster refuses a hazard floor now, so a spawner or a
       ground ward slot standing over the sea is furniture that silently stops
       producing -- a wave that never arrives, which reads as the wave clock
       being broken rather than as a marker being in the wrong place.
       Psychofuge puts its brute spawner 1.5m above the lava and its lowest
       ground ward 0.75m above it, so this is close enough to be worth pinning:
       what makes those safe is that their column's FLOOR is the ledge they sit
       on and not the sea underneath it.
       The air slots are deliberately not checked. A flyer never asks
       ::floor_safe -- that asymmetry is the whole gameplay consequence of a
       lava floor -- so a ward slot hanging over the sea is exactly what an air
       slot is for.
       *아레나가 지상 몬스터를 놓는 모든 자리는 지상 몬스터가 쓸 수 있는 땅입니다.*
       ::make_monster는 이제 위험한 바닥을 거절하므로, 바다 위에 선 스포너나 지상 결계핵
       자리는 조용히 생산을 멈추는 가구입니다. 오지 않는 웨이브이며, 그것은 표식이 잘못된
       자리에 있는 것이 아니라 웨이브 시계가 고장 난 것처럼 읽힙니다.
       Psychofuge는 브루트 스포너를 용암 위 1.5m에, 가장 낮은 지상 결계핵을 0.75m에 둡니다.
       못 박아 둘 값어치가 있을 만큼 가깝습니다. 그것들을 안전하게 만드는 것은 그 기둥의
       *바닥*이 그 아래의 바다가 아니라 그것들이 앉은 턱이라는 사실입니다.
       공중 자리는 의도적으로 검사하지 않습니다. 비행체는 ::floor_safe를 묻지 않으며 그
       비대칭이 용암 바닥이 낳는 게임플레이 결과의 전부이므로, 바다 위에 걸린 결계핵 자리는
       정확히 공중 자리가 존재하는 이유입니다. */
    {
        int checked = 0, over_lava = 0;
        for (int i = 0; i < w.level.n_ents; i++) {
            const Entity *e = &w.level.ents[i];
            int ground = txt_eq(e->kind, "wardground") ||
                         txt_eq(e->kind, "altar") ||
                         (e->kind[0] == 's' && e->kind[1] == 'p'); /* spawner* */
            if (!ground) continue;

            float ex = e->x * 0.01f, ey = e->y * 0.01f, ez = e->z * 0.01f;
            float gf, gc;
            if (!level_ground(&w.level, ex, ez, ey, 1e9f, &gf, &gc)) continue;
            checked++;
            if (level_hazard_at(&w.level, ex, gf, ez) > 0) over_lava++;
        }
        printf("      %d ground markers, %d of them standing on lava\n",
               checked, over_lava);
        ok(checked > 0, "the arena has ground markers to check");
        okf(over_lava == 0,
            "and none of them stands a monster on the sea",
            (float)over_lava, 0.0f);
    }

    /* THE SLOT BACK. LVL_BRUSH_SLOTS is small and a brush level holds one for
       as long as it is loaded.
       *칸을 돌려줍니다.* LVL_BRUSH_SLOTS는 작고 브러시 레벨은 로드되어 있는 동안 하나를
       붙듭니다. */
    level_release(&w.level);
}

int main(void) {
    printf("steptest\n\n");

    /* --- what a freeze actually stops --------------------------------------
       Four different states mean "the world does not advance", and they have to
       mean it identically. Checking each one separately is the point: a freeze
       that half works -- the player stops but the monsters do not, or the menu
       stops the world but the win screen does not -- is exactly the failure
       world_frozen exists to make impossible. */
    printf("freezing\n");
    {
        struct { const char *name; int title, won, dead, paused; } CASE[] = {
            { "the title screen",   1, 0, 0, 0 },
            { "the win screen",     0, 1, 0, 0 },
            { "the death screen",   0, 0, 1, 0 },
            { "an open menu",       0, 0, 0, 1 },
        };

        for (int c = 0; c < 4; c++) {
            World w;
            fixture(&w, 0);
            Input in = idle();
            in.forward = 1;

            /* Live first, so the fixture is known to be one a player can walk
               out of -- otherwise "did not move" proves nothing. */
            v3 from = w.player.pos;
            int frozen = world_step(&w, &in, ASPECT, DT);
            float walked = v3len(v3sub(w.player.pos, from));
            ok(!frozen && walked > 0.05f, "a live frame walks the player forward");

            w.run.title = CASE[c].title;
            w.run.won   = CASE[c].won;
            w.run.dead  = CASE[c].dead;
            in.paused   = CASE[c].paused;

            from = w.player.pos;
            frozen = world_step(&w, &in, ASPECT, DT);
            float after = v3len(v3sub(w.player.pos, from));

            char what[96];
            snprintf(what, sizeof(what), "%s stops the player dead", CASE[c].name);
            okf(after < 1e-5f, what, after, 0.0f);

            snprintf(what, sizeof(what), "...and the step reports it as frozen");
            ok(frozen != 0, what);
        }
    }

    /* --- the frozen state a step RETURNS is the one it used -----------------
       Not the one that is true afterwards. A frame that kills the player ran
       live -- the damage that killed them was dealt by it -- and the renderer
       has to draw it that way. Re-deriving the flag after the step instead
       hides the crosshair one frame before the death screen it belongs to
       appears, which reads as a dropped frame rather than as a death. */
    printf("\nthe frame a death happens on\n");
    {
        World w;
        fixture(&w, 30000);           /* lava that empties the bar in one frame */
        Input in = idle();

        int frozen = world_step(&w, &in, ASPECT, DT);

        ok(w.player.health == 0,   "a hazard that empties the bar takes it to zero");
        ok(w.run.dead == 1,        "and one place notices, wherever the damage came from");
        ok(frozen == 0,            "the step still reports the frame it ran as LIVE");
        ok(world_frozen(&w, 0) == 1, "even though asking again afterwards says frozen");

        /* The hook is let go on death, or it keeps reeling a corpse across the
           room with the claw still out there. */
        ok(w.weapon.hook_state == HOOK_IDLE, "and death lets go of the grapple");

        /* death_time is zeroed when the death is noticed and then advanced by
           the clocks below it in the same step. If it comes out of the killing
           frame at exactly zero, the clocks are running BEFORE the detection
           and the death screen is a frame late. */
        okf(w.run.death_time > DT * 0.5f && w.run.death_time < DT * 1.5f,
            "the death clock starts on the killing frame, not after it",
            w.run.death_time, DT);
    }

    /* --- hazard floors ------------------------------------------------------ */
    printf("\nhazard floors\n");
    {
        /* A hazard is the FLOOR. Jumping a lava channel, or being pulled across
           it by the hook, has to be a way through -- otherwise the room is a
           wall rather than an obstacle and the momentum systems this game is
           built on have nothing to do there. */
        World w;
        fixture(&w, 100);
        w.player.pos.y = PLAYER_EYE + 6.0f;   /* mid-air over the same lava */
        Input in = idle();

        world_step(&w, &in, ASPECT, DT);
        ok(!w.player.grounded,                 "the fixture really is airborne");
        ok(w.player.health == PLAYER_MAX_HP,   "lava does not burn a player in the air");
    }
    {
        /* One point a second, stepped at 60Hz, is 1/60th of a point per frame.
           Truncated per frame that is zero for ever, and a slow hazard is
           entirely harmless -- which is why the fraction is carried in the run
           rather than discarded. */
        World w;
        fixture(&w, 1);
        Input in = idle();

        for (int i = 0; i < 30; i++) world_step(&w, &in, ASPECT, DT);
        okf(w.player.health == PLAYER_MAX_HP,
            "half a second on 1dps lava has not yet cost a point",
            (float)w.player.health, (float)PLAYER_MAX_HP);

        for (int i = 0; i < 60; i++) world_step(&w, &in, ASPECT, DT);
        okf(w.player.health == PLAYER_MAX_HP - 1,
            "and a second and a half has cost exactly one",
            (float)w.player.health, (float)(PLAYER_MAX_HP - 1));

        /* Walking off it stops the charge rather than banking it. */
        w.player.pos = v3f(0.0f, PLAYER_EYE, 0.0f);
        w.level.sectors[0].hurt = 0;
        for (int i = 0; i < 120; i++) world_step(&w, &in, ASPECT, DT);
        okf(w.player.health == PLAYER_MAX_HP - 1,
            "dry ground clears the accumulator instead of holding it",
            (float)w.player.health, (float)(PLAYER_MAX_HP - 1));
    }

    /* --- clocks that run whether or not the world does ---------------------
       The death and title screens animate while frozen; they are what the
       freeze is FOR. The hurt flash has to fade even if the player opened the
       menu on the frame they were hit, or it stays painted on the screen for as
       long as the menu is up. */
    printf("\nclocks under a freeze\n");
    {
        World w;
        fixture(&w, 0);
        Input in = idle();
        in.paused = 1;

        w.run.dead       = 1;
        w.run.death_time = 0.0f;
        w.run.title      = 1;
        w.run.title_time = 0.0f;
        w.player.hurt    = 1.0f;

        for (int i = 0; i < 6; i++) world_step(&w, &in, ASPECT, DT);

        okf(w.run.death_time > DT * 5.5f, "the death screen animates while stopped",
            w.run.death_time, DT * 6.0f);
        okf(w.run.title_time > DT * 5.5f, "so does the title screen",
            w.run.title_time, DT * 6.0f);
        okf(w.player.hurt < 1.0f, "and the hurt flash still fades out",
            w.player.hurt, 1.0f - DT * 12.0f);
    }

    /* --- the clock the lava flows against ---------------------------------- */
    printf("\nthe world clock\n");
    {
        World w;
        fixture(&w, 0);
        Input in = idle();

        world_step(&w, &in, ASPECT, DT);
        okf(w.run.world_time > 0.0f, "a live frame advances the material clock",
            w.run.world_time, DT);

        in.paused = 1;
        float held = w.run.world_time;
        for (int i = 0; i < 10; i++) world_step(&w, &in, ASPECT, DT);
        okf(w.run.world_time == held,
            "a pause menu stops the lava churning behind it",
            w.run.world_time, held);

        /* Wrapped rather than left to grow: a float clock that runs for an hour
           loses the precision the shader's own pulse needs. */
        in.paused = 0;
        w.run.world_time = WORLD_TIME_WRAP - 0.001f;
        world_step(&w, &in, ASPECT, 0.01f);
        okf(w.run.world_time < 1.0f, "and it wraps instead of growing without bound",
            w.run.world_time, 0.009f);
    }

    /* --- the exit is tested AFTER the player has moved ----------------------
       An ordering assertion, and the cheapest one to state: the exit marker is
       placed just outside reach and one frame of walking arrives inside it. If
       the exit were tested before the move, that frame would not win -- the
       player would have to spend a second frame standing on it, which is how a
       "walk over the exit at speed and nothing happens" bug feels. */
    printf("\nreaching the exit\n");
    {
        World w;
        fixture(&w, 0);

        /* Forward is -z at yaw 0, and PLAYER_WALK * DT is 18cm a frame. The
           marker sits 102cm away: outside LVL_EXIT_RADIUS now, inside it after
           one step. */
        Entity *e = &w.level.ents[w.level.n_ents++];
        e->kind[0] = 'e'; e->kind[1] = 'x'; e->kind[2] = 'i'; e->kind[3] = 't';
        e->kind[4] = 0;
        e->x = 0;
        e->z = -102;

        Input in = idle();
        world_step(&w, &in, ASPECT, DT);
        ok(!w.run.won, "standing still, an exit 1.02m away is out of reach");

        in.forward = 1;
        world_step(&w, &in, ASPECT, DT);
        ok(w.run.won, "one frame of walking reaches it -- the move runs first");
    }

    /* --- a door that moved makes the drawn geometry stale ------------------- */
    printf("\ndoors and the geometry they invalidate\n");
    {
        World w;
        fixture(&w, 0);

        /* A doorway 1m to the player's right, inside DOOR_TOUCH_DIST of them.
           Untagged, so it opens on touch and needs no switch and no key. */
        box(&w.level, 100, -100, 200, 100, 0, 200, 0);
        DoorDef *d = &w.level.doors[w.level.n_doors++];
        d->sector = 1;
        d->axis   = DOOR_UP;
        d->amount = 200;
        d->speed  = 100;
        d->tag    = 0;
        d->key    = KEY_NONE;
        door_reset(&w.level);

        w.geometry_dirty = 0;

        Input in = idle();
        world_step(&w, &in, ASPECT, DT);
        ok(w.geometry_dirty,
           "a door the player touched raises the rebuild flag");

        /* Frozen, nothing moves the sectors, so nothing needs rebuilding.
           A door that kept opening behind a pause menu would also keep asking
           for a rebuild of a frame nobody is stepping. */
        w.geometry_dirty = 0;
        in.paused = 1;
        world_step(&w, &in, ASPECT, DT);
        ok(!w.geometry_dirty, "and a paused door asks for nothing");
    }

    /* --- claiming the rebuild ---------------------------------------------- */
    printf("\nthe rebuild handshake\n");
    {
        World w;
        fixture(&w, 0);
        w.geometry_dirty = 0;

        /* Against the SCOPE, which is what main.c switches on. These used to
           call a wrapper that flattened the answer to yes/no, and it survived
           only because these four lines were its last callers -- a second
           entry point kept alive by the test for it. Comparing to
           ::WORLD_GEOM_NONE says the same thing and says it about the value
           the game actually reads.
           main.c가 분기하는 대상인 *범위*에 대해 단언합니다. 이전에는 답을 예/아니오로
           뭉개는 래퍼를 호출했고, 그 래퍼는 이 네 줄이 마지막 호출자였기 때문에만 살아
           있었습니다. 자신을 위한 테스트가 살려 두는 두 번째 진입점이었습니다.
           ::WORLD_GEOM_NONE과 비교하면 같은 것을 말하되, 게임이 실제로 읽는 값에 대해
           말하게 됩니다. */
        int dynamic = -1;
        ok(world_take_geometry_scope(&w, &dynamic) == WORLD_GEOM_NONE,
           "a settled world asks for no rebuild");

        w.geometry_dirty = 1;
        ok(world_take_geometry_scope(&w, &dynamic) != WORLD_GEOM_NONE && dynamic == 0,
           "the first rebuild CREATES the mesh");
        ok(world_take_geometry_scope(&w, &dynamic) == WORLD_GEOM_NONE,
           "and taking it once is taking it -- twice does not rebuild twice");

        w.geometry_dirty = 1;
        ok(world_take_geometry_scope(&w, &dynamic) != WORLD_GEOM_NONE && dynamic == 1,
           "every rebuild after that replaces an existing allocation");

        /* The scope the flattened answer could not carry, asserted now that
           there is somewhere to put it: a raised flag comes back as the scope
           it was raised to, not merely as "something".
           뭉개진 답이 나를 수 없던 범위이며, 이제 둘 곳이 생겼으므로 단언합니다. 세워진
           플래그는 그저 "무언가"가 아니라 세워진 그 범위로 돌아옵니다. */
        w.geometry_dirty = WORLD_GEOM_MOVING;
        ok(world_take_geometry_scope(&w, &dynamic) == WORLD_GEOM_MOVING,
           "a door's rebuild comes back as MOVING, not as ALL");

        w.geometry_dirty = WORLD_GEOM_ALL;
        ok(world_take_geometry_scope(&w, &dynamic) == WORLD_GEOM_ALL,
           "and a load's comes back as ALL");
    }

    /* --- restart ------------------------------------------------------------
       The one case that loads a level, because a restart is a reload. It
       asserts nothing about what is IN the level -- only that a run which had
       been won, lost and half torn down comes back as a run that has not
       started yet. */
    printf("\nrestart\n");
    {
        World w;
        world_init(&w);

        w.run.title          = 0;
        w.run.won            = 1;
        w.run.dead           = 1;
        w.run.death_time     = 5.0f;
        w.run.hazard_accum   = 0.7f;
        w.run.restart_wanted = 1;
        w.player.health      = 3;

        world_restart(&w);

        ok(!w.run.won,            "a restart clears the win");
        ok(!w.run.dead,           "and the death");
        ok(!w.run.title,          "and does NOT go back to the title -- play was asked for");
        ok(!w.run.restart_wanted, "and clears the request, so nothing does it by hand");
        okf(w.run.death_time == 0.0f, "and every clock the run owns",
            w.run.death_time, 0.0f);
        okf(w.run.hazard_accum == 0.0f, "including the ones that were function statics",
            w.run.hazard_accum, 0.0f);
        okf(w.player.health == PLAYER_MAX_HP, "a fresh run starts at full health",
            (float)w.player.health, (float)PLAYER_MAX_HP);
        ok(w.geometry_dirty, "and the level it reloaded needs its geometry rebuilt");

        /* THIS BLOCK LOADS A LEVEL WITHOUT LOOKING LIKE IT. world_restart
           reloads World::cur_level, which world_init seeded with
           WORLD_START_LEVEL -- so a block that never names a level still takes
           a brush slot, and there are two. Invisible while the start level was
           sectors and the reason the three fixtures after this one began
           failing the moment it was a .map.
           이 블록은 그렇게 보이지 않으면서 레벨을 로드합니다. world_restart는
           World::cur_level을 다시 로드하는데 world_init이 그것을 WORLD_START_LEVEL로 심어
           두었습니다. 따라서 레벨을 전혀 언급하지 않는 블록도 브러시 슬롯을 가져가며, 슬롯은
           둘뿐입니다. 시작 레벨이 섹터인 동안에는 보이지 않았고, 그것이 .map이 되는 순간 이
           블록 뒤의 픽스처 셋이 실패하기 시작한 이유입니다. */
        level_release(&w.level);
    }

    /* --- what the player carries, and what they do not ----------------------
       Loads WORLD_START_LEVEL twice and asserts nothing about what is inside it.
       The question is only which fields survive a load and which are handed
       back to what a fresh run starts with. */
    printf("\nwhat crosses a level boundary\n");
    {
        World w;
        world_init(&w);
        w.run.title = 0;
        ok(world_load_level(&w, WORLD_START_LEVEL, WORLD_ENTER_NEW),
           "the start level loads");

        /* Something distinguishable in every field PlayerProgress claims. */
        const int LAST = WP_TYPES - 1;
        w.player.health     = 42;
        w.player.keys       = KEY_RED | KEY_BLUE;
        w.weapon.owned[LAST] = 1;
        w.weapon.ammo[LAST]  = 17;
        w.weapon.cur         = LAST;

        /* read and write are inverses, across a World that shares nothing with
           the one the progress came from. */
        {
            PlayerProgress p;
            world_progress_read(&w, &p);

            World blank;
            world_init(&blank);
            world_progress_write(&blank, &p);

            ok(blank.player.health == 42
               && blank.player.keys == (KEY_RED | KEY_BLUE)
               && blank.weapon.cur == LAST
               && blank.weapon.ammo[LAST] == 17
               && blank.weapon.owned[LAST] == 1,
               "read and write are inverses over every field");
        }

        /* carry_state=1: the exit is a reward you arrive at, not a reset. */
        ok(world_load_level(&w, WORLD_START_LEVEL, WORLD_ENTER_CARRY),
           "a transition loads");
        ok(w.player.health == 42,           "a transition carries health");
        ok(w.player.keys == (KEY_RED | KEY_BLUE), "and the keycards");
        ok(w.weapon.cur == LAST && w.weapon.ammo[LAST] == 17
           && w.weapon.owned[LAST] == 1,   "and the whole belt, weapon in hand included");

        /* carry_state=0: a fresh run. player_spawn resets health; the belt and
           the keycards used to have nobody at all, so a restart handed back
           every weapon and key the player had found on a map whose doors had
           just been re-locked. */
        ok(world_load_level(&w, WORLD_START_LEVEL, WORLD_ENTER_NEW),
           "a fresh start loads");
        okf(w.player.health == PLAYER_MAX_HP, "a fresh start restores health",
            (float)w.player.health, (float)PLAYER_MAX_HP);
        ok(w.player.keys == KEY_NONE,        "and takes the keycards back");
        ok(w.weapon.owned[LAST] == 0 && w.weapon.ammo[LAST] == 0,
           "and the weapons that were earned");
        ok(w.weapon.cur == WP_SHOTGUN && w.weapon.owned[WP_SHOTGUN]
           && w.weapon.ammo[WP_SHOTGUN] == WEAPON_START_AMMO,
           "leaving exactly the belt the game boots with");
        /* GIVEN BACK, because this World is abandoned here and the brush pool
           has room for exactly two. This cost nothing while the start level was
           sectors -- arena claimed no slot -- and became mandatory the moment it
           was a .map: the third fixture to load one is refused, and it is
           refused by a load returning 0 that nobody was checking.
           See ::Level::brush_key and ::DIAG_LEVEL_SLOTS.
           이 World가 이곳에서 버려지고 브러시 풀은 정확히 둘을 담기 때문에 반납합니다. 시작
           레벨이 섹터인 동안에는 비용이 없었지만(arena는 슬롯을 쓰지 않았습니다) .map이 되는
           순간 필수가 되었습니다. 그것을 로드하는 세 번째 픽스처가 거절되며, 아무도 검사하지
           않던 로드가 0을 반환하는 방식으로 거절됩니다. */
        level_release(&w.level);
    }

    /* --- door state that outlived the level it described --------------------
       The runtime array and the level's door definitions are matched by index
       and by nothing else. Stepping a level door_reset never saw used to write
       one sector's geometry out of another sector's closed shape, silently. */
    printf("\nstale door state\n");
    {
        World a;
        fixture(&a, 0);
        box(&a.level, 100, -100, 200, 100, 0, 200, 0);
        DoorDef *d = &a.level.doors[a.level.n_doors++];
        d->sector = 1; d->axis = DOOR_UP; d->amount = 200; d->speed = 100;
        d->tag = 0; d->key = KEY_NONE;
        door_reset(&a.level);

        /* A SECOND world whose door names a different sector, stepped without a
           reset of its own -- exactly what a level load that forgot, or a second
           Level in play, produces. */
        World b;
        fixture(&b, 0);
        box(&b.level, 100, -100, 200, 100, 0, 200, 0);
        box(&b.level, 400, -100, 500, 100, 0, 200, 0);
        DoorDef *d2 = &b.level.doors[b.level.n_doors++];
        d2->sector = 2; d2->axis = DOOR_UP; d2->amount = 200; d2->speed = 100;
        d2->tag = 0; d2->key = KEY_NONE;
        /* deliberately NO door_reset(&b.level) */

        int before = diag_count(DIAG_DOOR_STALE);
        short ceil_before = b.level.sectors[2].ceil;

        Input in = idle();
        world_step(&b, &in, ASPECT, DT);

        ok(diag_count(DIAG_DOOR_STALE) > before,
           "a door whose snapshot is another sector's is reported");
        okf(b.level.sectors[2].ceil == ceil_before,
            "and is left alone rather than moved from the wrong shape",
            (float)b.level.sectors[2].ceil, (float)ceil_before);

        /* Put the shared module back the way the next case expects it. */
        door_reset(&b.level);
    }

    /* --- a restart is a retry of THIS stage --------------------------------
       The reported bug. Reaching stage two with the axe and dying handed back
       the boot belt: not a retry of the stage, a demotion out of it. A restart
       has to put the player back where they started the stage they are in --
       which for the first stage is still the boot belt, and for every stage
       after it is whatever they walked in with. */
    printf("\nrestarting a stage you did not start the game in\n");
    {
        const int LAST = WP_TYPES - 1;

        World w;
        world_init(&w);
        w.run.title = 0;
        world_load_level(&w, WORLD_START_LEVEL, WORLD_ENTER_NEW);

        /* Earn something on the way through the first stage. */
        w.weapon.owned[LAST] = 1;
        w.weapon.ammo[LAST]  = 30;
        w.weapon.cur         = LAST;
        w.player.health      = 70;

        /* Cross into the next one. The name is the same map on purpose: what is
           under test is the entry snapshot, not which level follows which. */
        ok(world_load_level(&w, WORLD_START_LEVEL, WORLD_ENTER_CARRY),
           "an exit carries the earned weapon into the next stage");
        ok(w.weapon.owned[LAST] && w.weapon.ammo[LAST] == 30, "...it did");

        /* Spend it all and die. */
        w.weapon.ammo[LAST] = 0;
        w.player.health     = 0;
        w.run.dead          = 1;

        world_restart(&w);

        ok(w.weapon.owned[LAST],
           "a restart keeps the weapon the stage was ENTERED with");
        okf(w.weapon.ammo[LAST] == 30,
            "and its ammo as it was on arrival, not as it was on death",
            (float)w.weapon.ammo[LAST], 30.0f);
        okf(w.player.health == 70, "and the health it was entered with",
            (float)w.player.health, 70.0f);
        ok(w.weapon.cur == LAST, "with the same weapon in hand");
        ok(!w.run.dead, "and the death cleared");

        /* The first stage is the case the old behaviour got right, and it still
           is: entering it NEW makes the boot belt its checkpoint. */
        World f;
        world_init(&f);
        f.run.title = 0;
        world_load_level(&f, WORLD_START_LEVEL, WORLD_ENTER_NEW);
        f.weapon.owned[LAST] = 1;
        f.weapon.ammo[LAST]  = 30;
        world_restart(&f);
        ok(!f.weapon.owned[LAST] && f.weapon.ammo[WP_SHOTGUN] == WEAPON_START_AMMO,
           "restarting the FIRST stage still hands back the boot belt");

        /* Both of this block's Worlds, for the reason above. */
        level_release(&w.level);
        level_release(&f.level);
    }

    /* --- starting part way into the episode --------------------------------
       Asserts the SHAPE of the answer rather than its contents: which weapons a
       given map contains is a thing somebody edits, and a test that named them
       would go red on every edit. What must hold whatever the maps say is that
       the grant accumulates over the whole chain rather than reading only the
       stage before, that a granted weapon carries half its belt, and that the
       first stage grants nothing at all. */
    printf("\nstarting from a cleared stage\n");
    {
        /* ONE scratch level for all three chain walks below, and released after
           each of them. It used to be a `Level scan = {0}` declared inside each
           loop, which is the natural way to write it and now leaks: a brush
           level takes one of ::LVL_BRUSH_SLOTS, there are two, and a fresh
           Level per iteration claims a new one every hop. Before the pool was
           keyed by serial it got away with that -- each iteration's Level
           landed on the same stack address, so the pool handed back the slot it
           had given the last one, which is precisely the mistaken identity
           Level::brush_key exists to stop.
           It reads better hoisted anyway. world.c's own walk keeps one scratch
           level for the same reason, and says so.

           아래 세 번의 체인 순회 전부에 임시 레벨 *하나*를 쓰고 각각 뒤에 반납합니다.
           이전에는 루프마다 `Level scan = {0}`을 선언했고, 그것이 자연스러운 작성 방식이지만
           이제는 슬롯을 흘립니다. 브러시 레벨은 ::LVL_BRUSH_SLOTS 중 하나를 차지하는데 슬롯은
           둘이고, 반복마다 새 Level은 구간마다 새 슬롯을 요구합니다. 풀이 일련번호로 키잉되기
           전에는 그냥 넘어갔습니다. 각 반복의 Level이 같은 스택 주소에 놓였으므로 풀이 직전
           것에게 준 슬롯을 그대로 돌려주었기 때문이며, 그것이 바로 Level::brush_key가 막으려는
           신원 오인입니다.
           어차피 밖으로 빼는 편이 읽기에도 낫습니다. world.c 자신의 순회도 같은 이유로 임시
           레벨 하나를 두며 그렇게 적어 두었습니다. */
        static Level walk;

        PlayerProgress first;
        ok(world_progress_for_stage(WORLD_CHAIN_ROOT, WORLD_CHAIN_ROOT, &first),
           "the first stage is reachable from itself");
        {
            /* Nothing precedes it, so there is nothing to have been given. */
            World boot;
            world_init(&boot);
            PlayerProgress b;
            world_progress_read(&boot, &b);
            ok(first.owned[WP_SHOTGUN] && first.health == PLAYER_MAX_HP
               && first.keys == KEY_NONE && first.cur == b.cur,
               "and grants exactly the belt the game boots with");
        }

        /* Walk the shipped chain, checking the invariants at every stage. */
        char at[WORLD_LEVEL_MAX];
        txt_copy(at, sizeof(at), WORLD_CHAIN_ROOT, -1);

        PlayerProgress prev = first;
        int stages = 0, shrank = 0, wrong_ammo = 0, grew = 0;

        for (int hop = 0; hop < 8; hop++) {
            PlayerProgress p;
            if (!world_progress_for_stage(WORLD_CHAIN_ROOT, at, &p)) break;
            stages++;

            for (int i = 0; i < WP_TYPES; i++) {
                int want = p.owned[i] ? wp_stats(i)->max_ammo / 2 : 0;
                if (p.ammo[i] != want) wrong_ammo++;
                /* A weapon granted at one stage may never be missing from a
                   later one. Reading only the immediately previous stage would
                   drop stage one's axe the moment stage two did not contain it,
                   and this is what catches that. */
                if (prev.owned[i] && !p.owned[i]) shrank++;
                if (!prev.owned[i] && p.owned[i]) grew++;
            }
            prev = p;

            if (!level_load(at, &walk)) break;
            if (!walk.next[0]) break;
            txt_copy(at, sizeof(at), walk.next, -1);
        }
        level_release(&walk);

        okf(stages >= 2, "the shipped chain has stages to walk",
            (float)stages, 2.0f);
        okf(shrank == 0, "the grant never loses a weapon an earlier stage gave",
            (float)shrank, 0.0f);
        okf(wrong_ammo == 0, "and every granted weapon carries half its belt",
            (float)wrong_ammo, 0.0f);
        printf("  %-58s %d stage(s), %d weapon grant(s)\n",
               "(walked)", stages, grew);

        /* --- the accumulation, checked differentially ----------------------
           The grant for the deepest stage must equal the boot belt plus the
           UNION of the weapons in every stage before it -- built here by a loop
           written independently of the one in world.c, so agreeing is evidence
           rather than a tautology.

           With the two stages shipped today this reduces to "stage one's
           weapons", which an implementation reading only the immediately
           previous stage would also satisfy. It starts to bite the moment a
           third stage exists, which is exactly when the difference between
           "the previous stage" and "every previous stage" begins to matter --
           and it is cheaper to write the check now than to notice its absence
           after somebody has lost an axe. */
        {
            /* Walk to the end of the chain. */
            char deep[WORLD_LEVEL_MAX];
            txt_copy(deep, sizeof(deep), WORLD_CHAIN_ROOT, -1);
            for (int hop = 0; hop < 8; hop++) {
                if (!level_load(deep, &walk) || !walk.next[0]) break;
                txt_copy(deep, sizeof(deep), walk.next, -1);
            }
            level_release(&walk);

            /* The union of everything strictly before it. */
            int want[WP_TYPES];
            for (int i = 0; i < WP_TYPES; i++) want[i] = 0;

            char cur[WORLD_LEVEL_MAX];
            txt_copy(cur, sizeof(cur), WORLD_CHAIN_ROOT, -1);
            int before = 0;
            for (int hop = 0; hop < 8 && !same_name(cur, deep); hop++) {
                if (!level_load(cur, &walk)) break;
                weapons_in(&walk, want);
                before++;
                if (!walk.next[0]) break;
                txt_copy(cur, sizeof(cur), walk.next, -1);
            }
            level_release(&walk);

            PlayerProgress d;
            ok(world_progress_for_stage(WORLD_CHAIN_ROOT, deep, &d), "the deepest stage is reachable");

            int mismatch = 0;
            for (int i = 0; i < WP_TYPES; i++) {
                int expect = want[i] || first.owned[i];   /* union, plus the boot belt */
                if (!d.owned[i] != !expect) mismatch++;
            }
            okf(mismatch == 0,
                "the deepest stage grants the union of every stage before it",
                (float)mismatch, 0.0f);
            printf("  %-58s %d stage(s) folded in\n", "(union built from)", before);
        }

        /* Unreachable names change nothing. */
        PlayerProgress untouched;
        untouched.health = 1234;
        ok(!world_progress_for_stage(WORLD_CHAIN_ROOT, "no-such-stage-exists", &untouched),
           "a stage not on the chain is refused");
        okf(untouched.health == 1234, "and the caller's buffer is left alone",
            (float)untouched.health, 1234.0f);

        /* And entering one leaves a checkpoint, so a restart of a stage started
           this way replays the granted belt rather than the boot one. */
        World s;
        world_init(&s);
        s.run.title = 0;
        ok(world_start_stage(&s, WORLD_CHAIN_ROOT, WORLD_CHAIN_ROOT), "a stage can be started directly");
        s.weapon.ammo[WP_SHOTGUN] = 0;
        world_restart(&s);
        okf(s.weapon.ammo[WP_SHOTGUN] == first.ammo[WP_SHOTGUN],
            "and restarting it replays what it was started with",
            (float)s.weapon.ammo[WP_SHOTGUN], (float)first.ammo[WP_SHOTGUN]);
    }


    /* --- the exit shows the names before it loads ------------------------
     *
     * Reaching an exit used to load the next level on the same frame: the
     * player crossed a line and the world was simply a different world, with
     * no moment in which anything was said about what they had just done.
     *
     * THE ORDER IS THE WHOLE FEATURE. If the load happened first and the names
     * were shown afterwards, the screen would be sitting over the level it is
     * announcing -- the player reading "ENTERING VAULT" while standing in the
     * vault. So what is checked is not that a screen appears but that the
     * level has NOT changed while it is up.
     *
     * 출구에 닿으면 같은 프레임에 다음 레벨을 불러왔습니다. 순서가 기능의 전부입니다. 로드가
     * 먼저 일어나고 이름이 나중에 표시되면, 화면이 자신이 알리는 그 레벨 위에 뜨게 됩니다.
     * 금고 안에 서서 "ENTERING VAULT"를 읽는 것입니다. 그래서 검사하는 것은 화면이
     * 나타난다는 것이 아니라, 화면이 떠 있는 동안 레벨이 *바뀌지 않았다*는 것입니다.
     */
    {
        World w;
        fixture(&w, 0);
        Input in = idle();

        char started[32];
        snprintf(started, sizeof(started), "%s", w.level.name);

        /* An exit placed under the player, the same way the reach test above
           places one -- the arena authors none, so a test that went looking
           for one would be asserting about the map rather than about the code.
           위의 도달 테스트와 같은 방식으로 플레이어 발밑에 출구를 놓습니다. 아레나는 출구를
           작성하지 않으므로, 찾아 나서는 테스트는 코드가 아니라 맵에 대해 단언하게 됩니다. */
        Entity *e = &w.level.ents[w.level.n_ents++];
        e->kind[0] = 'e'; e->kind[1] = 'x'; e->kind[2] = 'i'; e->kind[3] = 't';
        e->kind[4] = 0;
        e->x = (short)(w.player.pos.x * 100.0f);
        e->z = (short)(w.player.pos.z * 100.0f);

        /* THE CHAIN IS SET HERE rather than relied on from the fixture. The
           fixture's level has no `next`, so its exit is terminal and sets
           `won` -- which is a different feature and would have made this test
           silently assert nothing about the one it is for.
           사슬을 fixture에 의존하지 않고 여기서 세웁니다. fixture의 레벨에는 `next`가
           없어 그 출구는 종착이며 `won`을 세웁니다. 그것은 다른 기능이고, 그대로 두었다면
           이 테스트가 정작 대상 기능에 대해 아무것도 단언하지 않았을 것입니다. */
        snprintf(w.level.next, sizeof(w.level.next), "%s", "arena");
        ok(w.level.next[0] != 0, "the level leads somewhere to go");

        {
            world_step(&w, &in, ASPECT, DT);
            ok(w.run.between, "reaching the exit raises the between screen");
            ok(!strcmp(w.level.name, started),
               "and the finished level is still the one loaded");
            ok(!strcmp(w.run.cleared, started),
               "which is the name it reports as cleared");
            ok(w.run.entering[0] != 0, "and it names where it is going");

            /* Frozen: a player who dies during the screen announcing that they
               cleared the level has been told two contradictory things.
               정지 상태입니다. 레벨을 클리어했다고 알리는 화면 도중에 죽는 플레이어는 서로
               모순되는 두 가지를 들은 것입니다. */
            in.forward = 1;
            v3 from = w.player.pos;
            int frozen = world_step(&w, &in, ASPECT, DT);
            float moved = v3len(v3sub(w.player.pos, from));
            ok(frozen != 0, "the world is frozen while it is up");
            okf(moved < 1e-5f, "so the player cannot walk out of it", moved, 0.0f);
            in.forward = 0;

            /* Halfway: still up, still the old level. The screen having a
               DURATION is what this checks -- one that cleared itself on the
               next frame would pass every assertion above.
               중간 지점입니다. 여전히 떠 있고 여전히 이전 레벨입니다. 화면에 *지속 시간*이
               있다는 것을 검사합니다. 다음 프레임에 사라지는 화면도 위의 모든 단언은
               통과합니다. */
            for (float t = 0; t < WORLD_BETWEEN_TIME * 0.5f; t += DT)
                world_step(&w, &in, ASPECT, DT);
            ok(w.run.between, "it is still up halfway through");
            ok(!strcmp(w.level.name, started),
               "and has still not loaded the next level");

            /* Past its time: the level changes and the screen goes. */
            for (float t = 0; t < WORLD_BETWEEN_TIME; t += DT)
                world_step(&w, &in, ASPECT, DT);
            ok(!w.run.between, "and comes down once its time is up");
            ok(strcmp(w.level.name, started) != 0,
               "having loaded the level it named");
        }
    }


    /* --- the jump pad throws you the same distance every time --------------
     *
     * A pad SETS the velocity rather than adding to it, and that is the whole
     * mechanic. Adding would make the height depend on how fast the player
     * happened to be falling when they landed on it, so the same pad would
     * throw them somewhere different every time and stop being a piece of
     * level they can learn.
     *
     * WHICH IS WHY THE TEST HITS IT TWICE AT DIFFERENT SPEEDS. Checking that a
     * pad launches at all passes just as well for an adding pad -- the fault
     * is not that it fails to fire, it is that it fires by a different amount.
     * Dropped from two different heights, the apex must be the same.
     *
     * 점프대는 속도를 더하지 않고 *설정*하며, 그것이 기능의 전부입니다. 더하면 착지 순간
     * 마침 얼마나 빨리 떨어지고 있었는지에 따라 높이가 달라져, 같은 점프대가 매번 다른
     * 곳으로 던지고 배울 수 있는 레벨의 일부이기를 그만둡니다.
     *
     * 그래서 검사가 서로 다른 속도로 두 번 밟습니다. "발사되는가"만 보면 더하는 점프대도
     * 똑같이 통과합니다. 결함은 발동하지 않는 것이 아니라 *다른 양*으로 발동하는 것입니다.
     * 서로 다른 높이에서 떨어뜨렸을 때 정점이 같아야 합니다.
     */
    {
        /* Apex reached after landing on a pad from `drop` metres up. */
        float apex[2];
        const float DROP[2] = { 0.2f, 6.0f };

        for (int k = 0; k < 2; k++) {
            World w;
            fixture(&w, 0);
            Input in = idle();

            /* A pad under the player. Placed rather than looked for, so the
               test asserts about the code and not about whichever map happens
               to have one today.
               찾지 않고 직접 놓습니다. 오늘 어느 맵에 하나 있는지가 아니라 코드에 대해
               단언하기 위해서입니다. */
            Entity *e = &w.level.ents[w.level.n_ents++];
            e->kind[0]='p'; e->kind[1]='u'; e->kind[2]='s'; e->kind[3]='h';
            e->kind[4]=0;
            e->x = (short)(w.player.pos.x * 100.0f);
            e->z = (short)(w.player.pos.z * 100.0f);
            e->p[0] = 1300;

            /* Lifted, then allowed to fall onto it. The two drops arrive at
               very different downward speeds, which is the whole point. */
            w.player.pos.y   += DROP[k];
            w.player.vel.y    = 0.0f;
            w.player.grounded = 0;

            float top = w.player.pos.y, launch_y = 0.0f;
            int   launched = 0;
            for (int i = 0; i < 60 * 6; i++) {
                float before = w.player.vel.y;
                world_step(&w, &in, ASPECT, DT);
                /* The launch is the frame velocity turns sharply upward. */
                if (!launched && w.player.vel.y > 1.0f && before <= 0.0f) {
                    launched = 1;
                    launch_y = w.player.pos.y;
                    top = w.player.pos.y;
                }
                if (launched && w.player.pos.y > top) top = w.player.pos.y;
                if (launched && w.player.grounded) break;
            }
            ok(launched, k ? "a pad fires under a fast landing"
                           : "a pad fires under a slow landing");
            /* The HEIGHT GAINED, not the world height. Comparing two apexes to
               each other proved nothing: it was satisfied by a pad that adds
               (both drops rise further, but by the same amount once the ground
               contact has zeroed the fall) and by a pad with no ground
               requirement (both rise forever at the same rate). Two broken
               versions and the correct one all scored a difference of 0.000.
               A number with a right answer is what discriminates.
               월드 높이가 아니라 *상승량*입니다. 두 정점을 서로 비교하는 것은 아무것도
               증명하지 못했습니다. 더하는 점프대(접지가 낙하를 0으로 만든 뒤라 둘 다 같은
               양만큼 더 오릅니다)도, 접지 조건이 없는 점프대(둘 다 같은 속도로 영원히
               오릅니다)도 만족시켰습니다. 망가진 둘과 올바른 하나가 모두 차이 0.000을
               기록했습니다. 정답이 있는 수치라야 구분됩니다. */
            apex[k] = top - launch_y;
        }

        /* The heights themselves are the table's business; that they AGREE is
           this test's. 5cm is far tighter than the difference an adding pad
           would show -- falling 6m arrives at about 16 m/s, which added to a
           13 m/s launch is more than double the height.
           높이 자체는 표의 몫이고, 둘이 *일치한다*는 것이 이 검사의 몫입니다. 5cm는 더하는
           점프대가 보일 차이보다 훨씬 빡빡합니다. 6m 낙하는 약 16 m/s로 도착하며, 13 m/s
           발사에 더해지면 높이가 두 배를 넘습니다. */
        /* 1300 file units is 13 m/s, and v^2/2g against PLAYER_GRAVITY 22 is
           3.84m. The window is wide because the apex is sampled once a frame;
           it is narrow enough that a pad which added a 6m fall's 16 m/s would
           reach 19m, and one that never let go would leave the map.
           1300 파일 단위는 13 m/s이고, PLAYER_GRAVITY 22에 대한 v^2/2g는 3.84m입니다.
           정점을 프레임마다 표본화하므로 창이 넓지만, 6m 낙하의 16 m/s를 더하는 점프대가
           19m에 이르고 놓아주지 않는 점프대가 맵을 벗어날 만큼은 좁습니다. */
        for (int k = 0; k < 2; k++)
            okf(apex[k] > 3.0f && apex[k] < 5.0f,
                k ? "a fast landing gains the pad's own height"
                  : "a slow landing gains the pad's own height",
                apex[k], 3.84f);

        float gap = apex[0] - apex[1];
        if (gap < 0) gap = -gap;
        okf(gap < 0.05f, "and the two agree with each other", gap, 0.05f);
    }

    /* --- the baked light belongs to the level it was traced against -------
       ::level_geometry keeps each vertex's static light under that vertex's
       position and normal, so a door moving does not re-trace what did not
       move. The cost of that is a rule: a reading is only valid for the level
       it was taken in, and a level that becomes a DIFFERENT level has to drop
       every one of them. A missed path does not crash and does not look wrong
       in the code -- it lights the new map with the old map's shadows, which
       is plausible enough to walk past.

       The reset lives inside ::level_load, which is the one place a Level can
       become another Level, so every path below should already be covered.
       That is a reason to believe it, not a reason not to check it: the same
       shape of fault has been in this project before, when a `!s->n` guard was
       copied to two places and fixed in one.

       Checked from here rather than from leveltest because these are WORLD
       paths -- a restart, a stage transition, a fresh run -- and world.c is
       what owns them.

       ::level_geometry는 각 정점의 정적 조명을 그 정점의 위치와 법선 아래 보관하여, 문이
       움직여도 움직이지 않은 것을 다시 판정하지 않게 합니다. 그 대가는 하나의 규칙입니다.
       판정 결과는 그것을 얻은 레벨에서만 유효하며, *다른* 레벨이 된 레벨은 그 전부를 버려야
       합니다. 놓친 경로는 죽지도 않고 코드상 틀려 보이지도 않습니다. 새 맵을 옛 맵의
       그림자로 밝히며, 그것은 그냥 지나칠 만큼 그럴듯합니다.

       리셋은 ::level_load 안에 있고 그곳이 Level이 다른 Level이 될 수 있는 유일한
       지점이므로, 아래의 모든 경로는 이미 덮여 있어야 합니다. 그것은 믿을 이유이지 검사하지
       않을 이유가 아닙니다. 같은 형태의 결함이 이 프로젝트에 이미 있었습니다. `!s->n` 가드가
       두 곳에 복사되어 한 곳만 고쳐졌을 때입니다.

       leveltest가 아니라 이곳에서 검사하는 이유는 이것들이 *월드* 경로이기 때문입니다.
       재시작, 스테이지 전환, 새 플레이이며, 그것들을 소유하는 것은 world.c입니다. */
    printf("\n  --- the light cache belongs to one level ---\n");
    /* The three fills below go through ::fill_light_cache, which gives the
       level a sun first. See it for why. */
    {
        static World w;
        MeshBuf b;
        mb_init(&b, 32768);

        /* Fills the cache, so that a path which forgot to drop it would be
           carrying something to notice. Asserting emptiness after a reset that
           was already empty proves nothing.
           캐시를 채웁니다. 그래야 버리기를 잊은 경로가 눈에 띌 무언가를 들고 있게 됩니다.
           이미 비어 있던 것을 리셋한 뒤 비었다고 단언하는 것은 아무것도 증명하지
           않습니다. */
        world_init(&w);
        world_load_level(&w, w.cur_level, WORLD_ENTER_NEW);
        int filled = fill_light_cache(&w, &b);
        ok(filled > 0, "a build fills the cache, so an empty one means something");

        /* 1. A fresh load. */
        world_load_level(&w, w.cur_level, WORLD_ENTER_NEW);
        ok(level_light_cache_count() == 0, "loading a level drops the cache");

        /* 2. A restart, which replays the stage the player is in. */
        fill_light_cache(&w, &b);
        world_restart(&w);
        ok(level_light_cache_count() == 0, "and so does a restart");

        /* 3. A stage transition. step_between loads the next level once the
              intermission clock runs out, so this drives it the way a frame
              does rather than calling the loader directly.
              step_between은 인터미션 시계가 끝나면 다음 레벨을 로드하므로, 로더를 직접
              호출하지 않고 프레임이 하는 방식으로 구동합니다. */
        int before_stage = fill_light_cache(&w, &b);

        w.run.between      = 1;
        w.run.between_time = 0.0f;
        txt_copy(w.run.entering, sizeof(w.run.entering),
                 w.level.next, (int)strlen(w.level.next));

        Input in = {0};
        in.paused = 0;
        for (int i = 0; i < 200 && w.run.between; i++)
            world_step(&w, &in, 1.6f, 1.0f / 60.0f);

        ok(before_stage > 0 && !w.run.between,
           "the intermission ran out and loaded the next stage");
        ok(level_light_cache_count() == 0,
           "and a stage transition drops the cache too");

        mb_free(&b);
    }

    /* --- the edges the window used to answer for itself ---------------------
       Every check below drives a rule that, until Input grew these three
       fields, lived inside wnd_proc: which number key puts what in your hand,
       what a keypress means on the death screen, and what happens to a rope
       when the player alt-tabs. None of it was reachable from here, because
       reaching it meant opening a window and pressing a key.

       That is the entire argument for the change, so this is where it is
       cashed in.
       아래의 모든 검사는, Input이 이 세 필드를 갖기 전까지 wnd_proc 안에 살던 규칙을
       구동합니다. 어느 숫자 키가 무엇을 손에 쥐여 주는지, 사망 화면에서의 키 입력이 무엇을
       뜻하는지, 플레이어가 alt-tab 할 때 로프가 어떻게 되는지입니다. 그중 무엇도 이곳에서
       도달할 수 없었습니다. 도달하려면 창을 열고 키를 눌러야 했기 때문입니다.

       그것이 이번 변경의 논거 전부이며, 그래서 이곳이 그것을 회수하는 자리입니다. */
    printf("\nedges\n");
    {
        /* --- weapon select ------------------------------------------------ */
        {
            World w;
            fixture(&w, 0);

            /* The boot belt is the shotgun and nothing else, so the axe is the
               weapon the player does not have yet.
               부팅 구성은 샷건뿐이므로, 도끼가 플레이어가 아직 갖지 못한 무기입니다. */
            Input in = idle();
            in.want_weapon = WP_AXE + 1;
            world_step(&w, &in, 1.6f, DT);
            ok(w.weapon.cur == WP_SHOTGUN,
               "a weapon the player does not own is silently refused");

            w.weapon.owned[WP_AXE] = 1;
            w.weapon.cooldown      = 0.4f;   /* mid-swing */
            in = idle();
            in.want_weapon = WP_AXE + 1;
            world_step(&w, &in, 1.6f, DT);
            ok(w.weapon.cur == WP_AXE, "one they do own is put in their hand");
            okf(w.weapon.cooldown == 0.0f,
                "and the swing in progress is cancelled rather than inherited",
                w.weapon.cooldown, 0.0f);

            /* Zero is "no request", which is what a zeroed Input has to mean --
               otherwise every fixture in this folder selects weapon 0 on every
               frame without saying so.
               0은 "요청 없음"이며, 0으로 초기화된 Input이 뜻해야 하는 바가 그것입니다.
               그렇지 않으면 이 폴더의 모든 픽스처가 매 프레임 말없이 0번 무기를 고릅니다. */
            w.weapon.cur = WP_AXE;
            in = idle();
            world_step(&w, &in, 1.6f, DT);
            ok(w.weapon.cur == WP_AXE,
               "a zeroed Input selects nothing rather than weapon zero");

            /* Frozen: there is no hand to put anything in yet. */
            in = idle();
            in.paused      = 1;
            in.want_weapon = WP_SHOTGUN + 1;
            world_step(&w, &in, 1.6f, DT);
            ok(w.weapon.cur == WP_AXE,
               "and a frozen world does not change weapons at all");
        }

        /* --- confirm, on the title screen ---------------------------------- */
        {
            World w;
            fixture(&w, 0);
            w.run.title = 1;

            Input in = idle();
            world_step(&w, &in, 1.6f, DT);
            ok(w.run.title, "the title screen stays up on a frame with no press");

            in = idle();
            in.confirm = 1;
            world_step(&w, &in, 1.6f, DT);
            ok(!w.run.title, "and a press starts the run");
        }

        /* --- confirm, on the death screen ---------------------------------- */
        {
            World w;
            fixture(&w, 0);
            w.run.dead       = 1;
            w.run.death_time = 0.0f;

            /* THE RULE THIS FILE EXISTS TO REACH. The shot that killed the
               player is very often still held, so a restart on it reads as the
               game skipping the death screen entirely -- and until now the only
               way to check the delay was to die while holding fire.
               이 파일이 도달하려고 존재하는 규칙입니다. 플레이어를 죽인 그 사격은 대개 아직
               눌린 상태이므로, 그것으로 재시작되면 게임이 사망 화면을 통째로 건너뛴 것처럼
               보입니다. 그리고 지금까지 이 지연을 검사하는 유일한 방법은 사격 버튼을 누른 채
               죽는 것이었습니다. */
            Input in = idle();
            in.confirm = 1;
            world_step(&w, &in, 1.6f, DT);
            ok(!w.run.restart_wanted,
               "a press during the death screen's grace period is ignored");

            /* Waited out with idle frames, so the clock is advanced by the step
               rather than by assigning to it: death_time is what the rule reads
               and a test that sets it directly would not be checking that
               anything advances it.
               시계를 대입이 아니라 스텝으로 진행시키기 위해 빈 프레임으로 기다립니다.
               death_time이 규칙이 읽는 값이며, 그것을 직접 대입하는 테스트는 무언가가 그것을
               진행시킨다는 사실을 검사하지 않게 됩니다. */
            for (int i = 0; i < 240 && w.run.death_time <= DEATH_INPUT_DELAY; i++) {
                Input z = idle();
                world_step(&w, &z, 1.6f, DT);
            }
            ok(!w.run.restart_wanted,
               "and waiting alone does not restart anything");

            /* THE DEFEAT CUTSCENE IS IN FRONT OF THE DEATH SCREEN by now, and
               it takes the press. ::step_confirm answers `cut` before `dead`
               deliberately -- the cutscene is what is drawn, and a press
               belongs to the thing on top -- so the sequence a player performs
               is one press to clear the words and one to restart. Waiting
               the grace period out above also waits out ::DEATH_ANIM_TIME,
               which is what started it.
               The cut is asserted rather than assumed: if story.txt ever stops
               authoring a `defeat`, this line fails and says so, instead of the
               two presses below quietly becoming one press and a no-op.
               *이 시점에는 패배 컷신이 사망 화면 앞에 있고* 그것이 누름을 가져갑니다.
               ::step_confirm은 의도적으로 `dead`보다 `cut`을 먼저 답합니다. 그려지는 것이 컷신이고
               누름은 맨 위의 것에 속하기 때문입니다. 그래서 플레이어가 하는 순서는 말을 지우는
               누름 하나와 재시작하는 누름 하나입니다. 위에서 유예 시간을 기다린 것이
               ::DEATH_ANIM_TIME도 함께 기다린 것이며, 그것이 컷신을 시작시켰습니다.
               가정하지 않고 컷을 단언합니다. story.txt가 언젠가 `defeat`를 제작하지 않게 되면 이
               줄이 실패하며 그렇다고 말합니다. 아래의 두 누름이 조용히 한 번의 누름과 한 번의
               무동작이 되는 대신입니다. */
            ok(w.run.cut == STORY_DEFEAT + 1,
               "the defeat cutscene is up once the collapse has finished");

            in = idle();
            in.confirm = 1;
            world_step(&w, &in, 1.6f, DT);
            ok(!w.run.cut, "a press clears it");
            ok(!w.run.restart_wanted,
               "and that press was spent on it rather than on the restart");

            in = idle();
            in.confirm = 1;
            world_step(&w, &in, 1.6f, DT);
            ok(w.run.restart_wanted, "the next press asks for a restart");
        }

        /* --- let_go, which is the one edge that undoes something ------------ */
        {
            World w;
            fixture(&w, 0);

            /* Thrown through the real call rather than by assigning a state, so
               what is released is a hook that was genuinely in the air.
               상태를 대입하지 않고 실제 호출로 던지므로, 해제되는 것은 정말로 공중에 있던
               훅입니다. */
            ok(wp_hook_fire(&w.weapon, w.player.pos, 0.0f, 0.0f),
               "the grapple is away");
            ok(wp_hook_locks_aim(&w.weapon), "and has the aim locked");

            Input in = idle();
            in.let_go = 1;
            world_step(&w, &in, 1.6f, DT);
            ok(!wp_hook_locks_aim(&w.weapon),
               "losing focus drops it rather than leaving it attached off-screen");
            ok(!w.weapon.hook_latched,
               "and rearms it, because the button is already up");
        }

        /* --- an edge fires once, whatever the frame does with it ------------
           The latch lives in main.c and is cleared there, so what is checked
           here is the other half of the contract: world_step must not hold on
           to an edge and act on it again on a later frame.
           래치는 main.c에 있고 그곳에서 지워지므로, 이곳에서 검사하는 것은 계약의 나머지
           절반입니다. world_step은 엣지를 붙들고 있다가 나중 프레임에 다시 처리해서는 안
           됩니다. */
        {
            World w;
            fixture(&w, 0);
            w.run.title = 1;

            Input in = idle();
            in.confirm = 1;
            world_step(&w, &in, 1.6f, DT);
            ok(!w.run.title, "one press dismissed the title");

            w.run.dead       = 1;
            w.run.death_time = DEATH_INPUT_DELAY + 1.0f;

            Input z = idle();   /* the SAME frame's edge is gone from this one */
            world_step(&w, &z, 1.6f, DT);
            ok(!w.run.restart_wanted,
               "and did not carry over into the next frame");
        }
    }

    check_shake();
    check_teleport();
    check_lava();
    check_score();
    check_power();

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall frame-order checks passed\n", fails);
    return fails != 0;
}
