/* weapontest -- the roster's invariants, and what its projectiles actually do.
 *
 * Two kinds of check, and the first matters more than it looks:
 *
 *   1. TABLE INVARIANTS. Exactly one of pellets/proj_speed/melee_range is set
 *      per row, every weapon has a distinct name, every belt is orderable.
 *      These are the rules weapon.c's dispatch relies on, and a row that
 *      breaks one does not fail to compile -- it produces a weapon that fires
 *      twice, or does nothing at all when you pull the trigger.
 *
 *   2. PROJECTILE PHYSICS. A grenade arcs, bounces off a wall, keeps its fuse
 *      burning while it rests, and takes a group down when it goes off. A bolt
 *      flies flat and stops at the first thing it meets. All of it is
 *      arithmetic over a struct, so none of it needs a window.
 *
 * 두 종류의 검사이며, 첫 번째가 보이는 것보다 중요합니다. 표의 불변식은 weapon.c의 분배가
 * 의존하는 규칙인데, 이를 깨는 행은 컴파일에 실패하지 않습니다. 두 번 발사되거나, 방아쇠를
 * 당겨도 아무 일도 일어나지 않는 무기가 될 뿐입니다.
 */

#include <stdio.h>
#include <math.h>
#include "weapon.h"
#include "weaponview.h"
#include "pools.h"
#include "sprite.h"   /* WPN_* -- the poses the viewmodel cycles through */
#include "proj.h"
#include "enemy.h"
#include "player.h"

/* The pools a run spawns into. A test owns one the same way a ::World does --
   these used to be file-scope arrays inside their own modules, so a fixture
   inherited whatever the previous case left in them. See pools.h.
   플레이가 생성해 넣는 풀들입니다. ::World가 그러하듯 테스트도 자기 것을 소유합니다.
   이것들은 각자의 모듈 안 파일 스코프 배열이었으므로, 픽스처는 이전 사례가 남긴 것을
   그대로 물려받았습니다. pools.h를 참조하십시오. */
static Pools g_pools;

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void okf(int cond, const char *what, float got, float want) {
    printf("  %-58s %8.2f / %8.2f  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void okd(int cond, const char *what, int got, int want) {
    printf("  %-58s %8d / %8d  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* A room 40m across with a floor at 0 and a wall the grenade can be thrown at.
   The fixture is a hand-built Level, which leaves the sector grid unbuilt --
   sector_at falls back to the full scan, which is correct and merely slower.
   손으로 조립한 Level이므로 섹터 격자가 생성되지 않습니다. sector_at이 전체 순회로
   폴백하며, 이는 올바르고 다만 느릴 뿐입니다. */
static Level L;

static void build_room(void) {
    Level zero = {0};
    L = zero;
    Sector *s = &L.sectors[L.n_sectors++];
    short p[8] = { -2000, -2000,  2000, -2000,  2000, 2000,  -2000, 2000 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4;
    s->floor = 0;
    s->ceil  = 800;
    level_bounds(s);
}

static int name_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return !*a && !*b;
}

int main(void) {
    printf("weapontest\n\n");
    build_room();

    /* --- 1. exactly one attack kind per row ------------------------------
       weapon.c's attack() tests pellets, then proj_speed, then melee_range and
       takes the first that is set. If a row set two, the second would be dead
       and the weapon would quietly be something other than what the table
       says. If a row set none, the trigger would do nothing at all.
       weapon.c의 attack()은 pellets, proj_speed, melee_range를 차례로 검사하여 처음
       설정된 것을 취합니다. 한 행이 둘을 설정하면 두 번째는 죽은 코드가 되고 무기는 표가
       말하는 것과 다른 무언가가 됩니다. 아무것도 설정하지 않으면 방아쇠가 아무 일도 하지
       않습니다. */
    {
        int bad = 0;
        for (int i = 0; i < WP_TYPES; i++) {
            const WeaponType *S = wp_stats(i);
            int kinds = (S->pellets > 0) + (S->proj_speed > 0.0f) + (S->melee_range > 0.0f);
            if (kinds != 1) {
                bad++;
                printf("      '%s' declares %d attack kinds\n", S->name, kinds);
            }
        }
        okd(bad == 0, "every weapon declares exactly one attack kind", bad, 0);
    }

    /* --- names are distinct, and resolvable ------------------------------
       The name is the sprite prefix, the pickup entity and the HUD label at
       once, so two weapons sharing one would collide in three places.
       이름은 스프라이트 접두사이자 아이템 엔티티이자 HUD 표시명입니다. 두 무기가 하나를
       공유하면 세 곳에서 충돌합니다. */
    {
        int dup = 0, unresolved = 0;
        for (int i = 0; i < WP_TYPES; i++) {
            if (wp_type_for(wp_stats(i)->name) != i) unresolved++;
            for (int j = i + 1; j < WP_TYPES; j++)
                if (name_eq(wp_stats(i)->name, wp_stats(j)->name)) dup++;
        }
        okd(dup == 0, "no two weapons share a name", dup, 0);
        okd(unresolved == 0, "and each name resolves back to its own index",
            unresolved, 0);
        ok(wp_type_for("nosuchweapon") < 0, "an unknown name resolves to -1");
    }

    /* --- belts are orderable --------------------------------------------- */
    {
        int bad = 0;
        for (int i = 0; i < WP_TYPES; i++) {
            const WeaponType *S = wp_stats(i);
            if (S->start_ammo > S->max_ammo)  bad++;
            if (S->pickup_ammo > S->max_ammo) bad++;
            if (S->max_ammo < 0)              bad++;
            /* No belt at all is allowed, and then nothing may fill it.
               탄띠가 아예 없는 것은 허용되며, 그러면 아무것도 채워서는 안 됩니다. */
            if (S->max_ammo == 0 && (S->start_ammo || S->pickup_ammo)) bad++;
            if (S->cooldown <= 0.0f)          bad++;
            if (S->damage <= 0)               bad++;
        }
        okd(bad == 0, "every belt and rate is orderable", bad, 0);
    }

    /* --- the axe is the one weapon without the grapple -------------------- */
    {
        int hooks = 0;
        for (int i = 0; i < WP_TYPES; i++) hooks += wp_stats(i)->hook ? 1 : 0;
        okd(hooks == WP_TYPES - 1, "every weapon but one throws the grapple",
            hooks, WP_TYPES - 1);
        ok(!wp_stats(WP_AXE)->hook, "and the one that does not is the axe");
        okd(wp_stats(WP_AXE)->max_ammo == 0, "which is also the one weapon that takes no ammo",
            wp_stats(WP_AXE)->max_ammo, 0);
    }

    /* --- 2. a bolt flies flat -------------------------------------------
       No gravity means the height it was fired at is the height it arrives at.
       A bolt that sagged would make the crosshair a lie at range. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);
        const WeaponType *S = wp_stats(WP_RAPID);
        v3 from = v3f(0, 5.0f, 0);
        proj_fire(&g_pools, from, v3f(0, 0, -1), S->proj_speed, S->proj_gravity,
                  S->damage, 0.0f, 0.0f);

        for (int i = 0; i < 10; i++) proj_update(&g_pools, &L, 1.0f / 60.0f);

        const Proj *p = 0;
        for (int i = 0; i < proj_count(&g_pools); i++)
            if (proj_at(&g_pools, i)->active) { p = proj_at(&g_pools, i); break; }
        ok(p != 0, "a bolt is still in flight after ten frames");
        if (p) {
            okf(fabsf(p->pos.y - 5.0f) < 0.001f, "and has not dropped at all",
                p->pos.y, 5.0f);
            ok(p->pos.z < -5.0f, "having travelled down the aim");
        }
    }

    /* --- a grenade arcs --------------------------------------------------
       Gravity means a grenade thrown level ends up lower than it started, and
       that fall is what lets it be lobbed over things. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);
        const WeaponType *S = wp_stats(WP_GRENADE);
        proj_fire(&g_pools, v3f(0, 5.0f, 0), v3f(0, 0, -1), S->proj_speed, S->proj_gravity,
                  S->damage, PROJ_BLAST_RADIUS, PROJ_FUSE);

        for (int i = 0; i < 12; i++) proj_update(&g_pools, &L, 1.0f / 60.0f);

        const Proj *p = 0;
        for (int i = 0; i < proj_count(&g_pools); i++)
            if (proj_at(&g_pools, i)->active) { p = proj_at(&g_pools, i); break; }
        ok(p != 0, "a grenade is still in flight");
        if (p) ok(p->pos.y < 5.0f, "and has fallen below where it was thrown");
    }

    /* --- a grenade goes off on its fuse, even at rest --------------------
       The property that makes one at your feet a threat rather than scenery:
       the fuse burns whether or not it is moving. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);
        const WeaponType *S = wp_stats(WP_GRENADE);
        proj_fire(&g_pools, v3f(0, 1.0f, 0), v3f(0, 0, -1), S->proj_speed, S->proj_gravity,
                  S->damage, PROJ_BLAST_RADIUS, PROJ_FUSE);
        ok(proj_live(&g_pools) == 1, "the grenade launched");

        /* Well past the fuse. */
        for (int i = 0; i < (int)((PROJ_FUSE + 0.5f) * 60.0f); i++)
            proj_update(&g_pools, &L, 1.0f / 60.0f);
        okd(proj_live(&g_pools) == 0, "and is gone once its fuse has burned",
            proj_live(&g_pools), 0);
    }

    /* --- a detonation outlives the round that made it ---------------------
       WHAT THIS IS GUARDING IS A LIFETIME, not a picture. The light an
       explosion casts and the jolt it gives the camera both read one record --
       see ::Flash -- and that record exists because the projectile does not:
       ::detonate clears `active` on the frame it goes off, and everything a
       player experiences of an explosion happens afterwards. If the flash were
       ever folded back onto the ::Proj it came from, or aged on the
       projectile's clock, the symptom would be the room going dark on the
       brightest frame in the game -- which is exactly the state this engine was
       in before the pool existed.

       THE AGEING SPLIT IS CHECKED HERE AND NOWHERE ELSE. ::proj_update must not
       touch a flash: it is frozen with the world and a light is not, because
       the particles it is lighting are not either. That is one line's worth of
       decision in world.c and nothing about the code makes it visible -- a
       flash aged inside ::proj_update would pass every other check in this
       file, and the fault would surface as a paused game whose explosion had
       frozen half-lit under a smoke cloud that was still growing.

       이것이 지키는 것은 그림이 아니라 수명입니다. 폭발이 던지는 빛과 카메라에 주는 충격은
       모두 하나의 기록을 읽으며(::Flash 참조), 그 기록이 존재하는 이유는 발사체가 존재하지
       않기 때문입니다. ::detonate는 터지는 프레임에 `active`를 지우고, 플레이어가 폭발에서
       겪는 모든 것은 그 뒤에 일어납니다. 섬광을 그것이 나온 ::Proj에 다시 접어 넣거나
       발사체의 시계로 나이 먹인다면, 증상은 게임에서 가장 밝은 프레임에 방이 어두워지는
       것이며, 그것이 이 풀이 생기기 전 엔진이 놓여 있던 상태 그대로입니다.

       나이 먹이기의 분리는 이곳에서만 검사됩니다. ::proj_update는 섬광을 건드려서는 안
       됩니다. 그것은 월드와 함께 멈추지만 빛은 멈추지 않습니다. 빛이 밝히고 있는 입자들도
       멈추지 않기 때문입니다. 그것은 world.c의 한 줄짜리 결정이고 코드의 무엇도 그것을 눈에
       보이게 하지 않습니다. ::proj_update 안에서 나이 먹는 섬광은 이 파일의 다른 모든 검사를
       통과하며, 결함은 아직 커지고 있는 연기 구름 아래에 반쯤 밝혀진 채 얼어붙은 폭발로
       드러납니다. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);
        okd(proj_flash_live(&g_pools) == 0,
            "nothing is lit before anything has gone off",
            proj_flash_live(&g_pools), 0);

        const WeaponType *S = wp_stats(WP_GRENADE);
        proj_fire(&g_pools, v3f(0, 1.0f, 0), v3f(0, 0, -1), S->proj_speed,
                  S->proj_gravity, S->damage, PROJ_BLAST_RADIUS, PROJ_FUSE);

        /* Past the fuse, and then a full second further. The extra second is
           the point: ::proj_update is the only thing running here, so a flash
           that has faded by the end of it is one being aged on the wrong clock.
           도화선을 지나고 다시 1초를 더 돌립니다. 그 1초가 요점입니다. 이곳에서 돌고 있는
           것은 ::proj_update뿐이므로, 그것이 끝날 때 사그라든 섬광은 틀린 시계로 나이를 먹고
           있는 섬광입니다. */
        for (int i = 0; i < (int)((PROJ_FUSE + 1.0f) * 60.0f); i++)
            proj_update(&g_pools, &L, 1.0f / 60.0f);

        okd(proj_live(&g_pools) == 0, "the grenade is gone",
            proj_live(&g_pools), 0);
        okd(proj_flash_live(&g_pools) == 1,
            "and the light it threw is not", proj_flash_live(&g_pools), 1);

        const Flash *f = 0;
        for (int i = 0; i < proj_flash_count(&g_pools); i++) {
            const Flash *c = proj_flash_at(&g_pools, i);
            if (c && c->life > 0.0f) { f = c; break; }
        }
        ok(f != 0, "and it can be read back out of the pool");

        /* THE RADIUS IS THE DAMAGE RADIUS, unmultiplied. Both readers scale it
           by their own factor and the factors differ -- scene.c's light reaches
           1.7 radii, world.c's shake 2.5 -- so a pre-scaled number stored here
           would make two of the three wrong. Compared exactly because it is
           copied rather than computed.
           반경은 배율이 적용되지 않은 피해 반경입니다. 두 독자가 각자의 배율을 곱하며 그
           배율은 서로 다릅니다(scene.c의 빛은 1.7배, world.c의 흔들림은 2.5배). 그러므로 미리
           배율을 곱한 수를 저장하면 셋 중 둘이 틀리게 됩니다. 계산이 아니라 복사이므로 정확히
           비교합니다. */
        okf(f && f->radius == PROJ_BLAST_RADIUS,
            "carrying the damage radius, not a scaled one",
            f ? f->radius : 0.0f, PROJ_BLAST_RADIUS);
        okf(f && proj_flash_fade(f) > 0.999f,
            "still at full strength, because proj_update does not age it",
            f ? proj_flash_fade(f) : 0.0f, 1.0f);

        /* NOT A LINEAR RAMP. Halfway through the life the curve must be near a
           quarter, not near a half: an explosion arrives at full and is most of
           the way gone before the eye finishes registering it, and a light that
           walks evenly down to nothing reads as a lamp being dimmed. Every
           other check here passes with the square taken out.
           선형 램프가 아닙니다. 수명의 절반 지점에서 곡선은 1/2이 아니라 1/4 근처여야
           합니다. 폭발은 최대치로 도착해서 눈이 그것을 인지하기를 마치기도 전에 거의
           사라지며, 고르게 0까지 걸어 내려가는 빛은 조명이 어두워지는 것으로 읽힙니다. 이곳의
           다른 모든 검사는 제곱을 빼도 통과합니다. */
        proj_flash_update(&g_pools, PROJ_FLASH_TIME * 0.5f);
        okf(f && proj_flash_fade(f) < 0.30f && proj_flash_fade(f) > 0.20f,
            "and it collapses on a curve rather than a ramp",
            f ? proj_flash_fade(f) : 0.0f, 0.25f);

        /* WHAT MADE IT, which is the field the renderer picks a colour from.
           A blast is white going orange; a bolt is its own hue the whole way.
           The kinds are indistinguishable in every other measure on this
           record -- position, radius, power and life are all plausible for
           either -- so a flash tagged wrong is a room lit the wrong colour
           with nothing else out of place to notice.
           *무엇이 만들었는가*이며, 렌더러가 색을 고르는 필드입니다. 폭발은 흰색에서
           주황으로 가고 볼트는 내내 자기 색조입니다. 이 기록의 다른 모든 척도에서 두 종류는
           구분되지 않습니다. 위치도 반경도 세기도 수명도 어느 쪽으로든 그럴듯하므로, 잘못
           표시된 섬광은 알아챌 다른 이상이 하나도 없는 채로 틀린 색으로 밝혀진 방입니다. */
        okd(f && f->kind == FLASH_BLAST,
            "and says a charge is what made it",
            f ? f->kind : -1, FLASH_BLAST);

        proj_flash_update(&g_pools, PROJ_FLASH_TIME);
        okd(proj_flash_live(&g_pools) == 0,
            "and is over once its own time is up",
            proj_flash_live(&g_pools), 0);
    }

    /* --- a bolt landing is a flash too, and a different one ----------------
       The rapid's impact carried no light at all until this pass. The bolt
       lights the wall green for the whole of its flight -- scene.c offers one
       per live projectile -- and then stopped existing, so the brightest thing
       about a hit was the frame the wall went dark again. That is the fault
       ::proj_flash was written for on the grenade's side, unaddressed on this
       one for as long as both have existed.

       WHAT IS ACTUALLY BEING GUARDED IS THE KIND. That a flash appears at all
       is one line; that it appears tagged FLASH_BOLT is what makes it green
       rather than the blast's orange, and orange is the colour this weapon
       spent its whole impact wearing by mistake before this. The radius is
       checked too because it is the other number a bolt cannot inherit from a
       charge: a hit that claimed a grenade's 4.2m would light the room like
       one, eleven times a second.

       연사의 피탄은 이 작업 전까지 빛을 전혀 지니지 않았습니다. 볼트는 비행 내내 벽을 녹색으로
       밝히다가(scene.c가 살아 있는 발사체마다 하나씩 제안합니다) 존재하기를 그만두므로, 피탄에서
       가장 밝은 것은 벽이 다시 어두워지는 프레임이었습니다. 유탄 쪽에서 ::proj_flash가 쓰인
       이유인 그 결함이, 둘이 함께 존재해 온 내내 이쪽에서는 다뤄지지 않았습니다.

       *실제로 지키는 것은 종류입니다.* 섬광이 나타난다는 것 자체는 한 줄입니다. 그것이
       FLASH_BOLT로 표시되어 나타난다는 것이 그것을 폭발의 주황이 아니라 녹색으로 만들며,
       주황은 이 무기가 이전까지 피탄 내내 실수로 걸치고 있던 색입니다. 반경도 검사하는 이유는
       그것이 볼트가 장약에서 물려받을 수 없는 나머지 한 수이기 때문입니다. 유탄의 4.2m를
       주장하는 피탄은 초당 열한 번 방을 그만큼 밝히게 됩니다. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);

        const WeaponType *R = wp_stats(WP_RAPID);
        proj_fire(&g_pools, v3f(0, 1.0f, 0), v3f(0, 0, -1), R->proj_speed,
                  R->proj_gravity, R->damage, 0.0f, 0.0f);

        /* The room is 40m across, so the wall is 20m out and 70 m/s reaches it
           in under a third of a second. Half a second of frames is comfortably
           past that and comfortably short of ::PROJ_FLASH_TIME running out.
           방은 가로 40m이므로 벽은 20m 밖이고 70m/s이면 3분의 1초가 안 되어 닿습니다. 0.5초분의
           프레임이면 그것을 넉넉히 지나면서 ::PROJ_FLASH_TIME이 끝나기에는 넉넉히 짧습니다. */
        for (int i = 0; i < 30; i++) proj_update(&g_pools, &L, 1.0f / 60.0f);

        okd(proj_live(&g_pools) == 0, "the bolt has landed",
            proj_live(&g_pools), 0);
        okd(proj_flash_live(&g_pools) == 1,
            "and it lit the wall it landed on", proj_flash_live(&g_pools), 1);

        const Flash *b = 0;
        for (int i = 0; i < proj_flash_count(&g_pools); i++) {
            const Flash *c = proj_flash_at(&g_pools, i);
            if (c && c->life > 0.0f) { b = c; break; }
        }
        okd(b && b->kind == FLASH_BOLT,
            "in the bolt's own colour rather than a charge's",
            b ? b->kind : -1, FLASH_BOLT);
        okf(b && b->radius == PROJ_HIT_RADIUS,
            "and at a hit's reach, not a blast's",
            b ? b->radius : 0.0f, PROJ_HIT_RADIUS);

        proj_flash_update(&g_pools, PROJ_FLASH_TIME);
    }

    /* --- the axe's slam is the same event without the fire -----------------
       ::wp_axe_land calls ::proj_blast, so it makes a crater by every measure
       the code has. What it did not do was say so: it threw `boltburst` -- the
       MONSTER bolt's flash, which cools into that bolt's blue -- and left the
       geometry unlit, so the player's own axe coming down was drawn in the
       colour scene.c's palette reserves for something shooting at them.

       The two numbers checked here are the two that cannot be derived from each
       other. The radius is the slam's, which is LARGER than a grenade's; the
       power is a third, because a mass of metal hitting stone is not a charge
       going off. Anything taking brightness from reach would light the room
       harder for the one with no fire in it, which is the whole reason
       ::AXE_SLAM_FLASH exists as a number of its own.

       ::wp_axe_land는 ::proj_blast를 호출하므로, 코드가 가진 모든 척도에서 구덩이를 만듭니다.
       하지 않던 것은 그렇다고 말하는 것이었습니다. `boltburst`, 즉 몬스터 볼트의 섬광을
       던졌고 그것은 그 볼트의 파랑으로 식으며, 지오메트리는 밝혀지지 않은 채였습니다. 그래서
       플레이어 자신의 도끼가 내려찍는 장면이, scene.c의 팔레트가 자신을 쏘는 무언가를 위해
       예약해 둔 색으로 그려졌습니다.

       이곳에서 검사하는 두 수는 서로에게서 유도할 수 없는 두 수입니다. 반경은 내려찍기의
       것이며 유탄의 것보다 넓습니다. 세기는 3분의 1인데, 금속 덩어리가 돌을 때리는 것은 장약이
       터지는 것이 아니기 때문입니다. 밝기를 도달 거리에서 가져오는 것은 무엇이든 불이 없는
       쪽을 위해 방을 더 세게 밝히게 되며, ::AXE_SLAM_FLASH가 자기 수로 존재하는 이유가
       그것입니다. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);

        Weapon aw;
        wp_init(&aw);
        aw.leaping    = 1;
        aw.leap_timer = 0.0f;

        int landed = wp_axe_land(&aw, &g_pools, v3f(0, 0, 0), 1, 1.0f / 60.0f);
        ok(landed, "the slam lands");
        okd(proj_flash_live(&g_pools) == 1, "and lights the room it landed in",
            proj_flash_live(&g_pools), 1);

        const Flash *f = 0;
        for (int i = 0; i < proj_flash_count(&g_pools); i++) {
            const Flash *c = proj_flash_at(&g_pools, i);
            if (c && c->life > 0.0f) { f = c; break; }
        }
        okf(f && f->radius == AXE_SLAM_RADIUS,
            "at the slam's reach, which is wider than a grenade's",
            f ? f->radius : 0.0f, AXE_SLAM_RADIUS);
        okf(f && f->power < 1.0f && f->power == AXE_SLAM_FLASH,
            "and dimmer than one, because nothing here is on fire",
            f ? f->power : 0.0f, AXE_SLAM_FLASH);

        /* And a level load takes them with it. A light left over from a
           grenade thrown in the previous room would arrive in the new one at
           full strength, with nothing there to have made it.
           그리고 레벨 로드가 그것들을 함께 가져갑니다. 이전 방에서 던진 유탄이 남긴 빛은 새
           방에 최대 세기로 도착하며, 그 방에는 그것을 만든 무엇도 없습니다. */
        proj_reset(&g_pools);
        okd(proj_flash_live(&g_pools) == 0, "and a level load clears them",
            proj_flash_live(&g_pools), 0);
    }

    /* --- the blast reaches a group, and falls off with distance ----------
       A grenade that hurt exactly one monster would be a slow shotgun. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);

        /* Three water spirits: one at the centre, one near the rim, one
           outside. The BASELINE kind on purpose -- the falloff below is read
           against one health total, and the retired `imp` this fixture used to
           name now resolves to the caster, which has different health and does
           not stand on the floor. */
        Level E = L;
        E.n_ents = 0;
        for (int k = 0; k < 3; k++) {
            Entity *e = &E.ents[E.n_ents++];
            const char *kind = "water_spirit";
            int ki = 0;
            while (kind[ki]) { e->kind[ki] = kind[ki]; ki++; }
            e->kind[ki] = 0;
            e->x = (short)(k * 300);   /* 0m, 3m, 6m */
            e->z = 0;
        }
        enemy_spawn_level(&g_pools, &E);
        ok(enemy_alive(&g_pools) == 3, "three monsters to blast");

        int before[3];
        for (int i = 0; i < 3; i++) before[i] = enemy_at(&g_pools, i)->health;

        int hit = proj_blast(&g_pools, v3f(0, 0.85f, 0), PROJ_BLAST_RADIUS, 55);
        okd(hit == 2, "the blast reaches the two inside its radius", hit, 2);

        int d0 = before[0] - enemy_at(&g_pools, 0)->health;
        int d1 = before[1] - enemy_at(&g_pools, 1)->health;
        int d2 = before[2] - enemy_at(&g_pools, 2)->health;
        ok(d0 > d1, "the near one takes more than the far one");
        okd(d2 == 0, "and the one outside takes nothing", d2, 0);
    }

    /* --- a bolt stops at the first monster it meets -----------------------
       Swept, not teleported: at 70 m/s a bolt crosses more than a metre a
       frame, and a monster between this frame and the next must still be hit. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);
        Level E = L;
        E.n_ents = 0;
        Entity *e = &E.ents[E.n_ents++];
        e->kind[0]='i'; e->kind[1]='m'; e->kind[2]='p'; e->kind[3]=0;
        e->x = 0; e->z = -800;                 /* 8 m down the aim */
        enemy_spawn_level(&g_pools, &E);

        int before = enemy_at(&g_pools, 0)->health;
        const WeaponType *S = wp_stats(WP_RAPID);
        proj_fire(&g_pools, v3f(0, 0.9f, 0), v3f(0, 0, -1), S->proj_speed, 0.0f,
                  S->damage, 0.0f, 0.0f);

        for (int i = 0; i < 30 && proj_live(&g_pools); i++) proj_update(&g_pools, &L, 1.0f / 60.0f);

        ok(enemy_at(&g_pools, 0)->health < before, "a bolt damages the monster it reaches");
        okd(proj_live(&g_pools) == 0, "and is consumed by the hit", proj_live(&g_pools), 0);
    }

    /* --- the pool refuses rather than overruns ---------------------------- */
    {
        proj_reset(&g_pools);
        int made = 0;
        for (int i = 0; i < PROJ_MAX + 12; i++)
            made += proj_fire(&g_pools, v3f(0, 1, 0), v3f(0, 0, -1), 30.0f, 0.0f, 5, 0.0f, 0.0f);
        okd(made == PROJ_MAX, "the pool fills to its cap and then refuses",
            made, PROJ_MAX);
        okd(proj_live(&g_pools) == PROJ_MAX, "and holds exactly that many",
            proj_live(&g_pools), PROJ_MAX);
    }

    /* --- every weapon's cycle, against Doom's own state table -------------
       These tables are transcribed from info.c, and the reason to assert them
       is that reading the ART instead got two of them wrong and both shipped.
       The shotgun idled on its first PUMP frame, because its real idle
       (SHTGA0) is little more than the end of a barrel and had been dropped as
       unusable; and the chainsaw had its idle and its cut swapped, because
       SAWG C and D -- the frames A_WeaponReady alternates between -- are the
       wider drawings and read as a lunge.

       Neither failed to compile, neither crashed, and neither is visible in a
       screenshot unless you already know what to look for.

       이 표들은 info.c에서 옮긴 것이며, 이를 단언하는 이유는 대신 *아트*를 읽고
       판단했다가 둘을 틀렸고 둘 다 배포되었기 때문입니다. 어느 쪽도 컴파일에 실패하지
       않았고, 크래시도 나지 않았으며, 무엇을 찾아야 하는지 이미 알지 않는 한 스크린샷
       으로도 보이지 않습니다. */
    {
        /* Walk the whole recovery and read the poses off in order. A single
           sample cannot see a row inserted, dropped or reordered. Sampled
           rather than compared against the table's own numbers, because a test
           that reads the table proves only that the table equals itself. */
        struct { int type; const char *name; int want[8]; int n; } W[] = {
            /* A B C D C B A -- out and back, all four drawings */
            { WP_SHOTGUN, "shotgun",
              { SG_IDLE, SG_PUMP0, SG_PUMP1, SG_PUMP2, SG_PUMP1, SG_PUMP0, SG_IDLE }, 7 },
            /* B held for the whole shot, then back to A */
            { WP_GRENADE, "grenade", { LN_FIRE, LN_IDLE }, 2 },
            /* A(4) B(4): both states fire, so the alternation is the fire rate */
            { WP_RAPID,   "rapid",   { RP_IDLE, RP_SPIN }, 2 },
            /* A_Saw alternates A and B -- the bite, never the rev */
            { WP_AXE,     "axe",     { AX_CUT0, AX_CUT1 }, 2 },
        };

        for (int k = 0; k < 4; k++) {
            const float T = weapon_pump_time(W[k].type);
            int seq[12], n = 0;
            /* i < 400, not <= : at exactly 400 the timer is zero, which is
               not "the end of the animation" but "not animating", and the
               idle frame it returns then is a fifth pose that is not part of
               the cycle. Two weapons hid that because their idle happens to
               equal their cycle's last pose.
               i <= 400이 아니라 i < 400입니다. 정확히 400에서 타이머는 0이 되는데 그것은
               "애니메이션의 끝"이 아니라 "애니메이션 중이 아님"이며, 그때 반환되는 대기
               프레임은 주기에 속하지 않는 다섯 번째 자세입니다. */
            for (int i = 0; i < 400; i++) {
                int f = weapon_sprite_frame_at(W[k].type, 0.0f,
                                               T * (1.0f - i / 400.0f), 0.0f);
                if (n == 0 || seq[n - 1] != f) { if (n < 12) seq[n++] = f; }
            }
            int match = (n == W[k].n);
            for (int i = 0; match && i < n; i++) match = (seq[i] == W[k].want[i]);
            ok(match, W[k].name);
            if (!match) {
                printf("      got %d poses:", n);
                for (int i = 0; i < n; i++) printf(" %d", seq[i]);
                printf("   wanted %d:", W[k].n);
                for (int i = 0; i < W[k].n; i++) printf(" %d", W[k].want[i]);
                printf("\n");
            }
        }

        /* THE IDLE IS A CYCLE TOO, and for the chainsaw it is the whole point.
           A_WeaponReady shows one frame for three of these weapons and
           alternates two for the saw, so "at rest" cannot be a single drawing.
           Driven by a free-running clock rather than by bob_phase, because a
           saw revs while you stand still and bob_phase does not. */
        ok(weapon_sprite_frame_at(WP_SHOTGUN, 0.0f, 0.0f, 0.0f) == SG_IDLE,
           "a resting shotgun shows its IDLE frame, not a pump frame");

        int saw_seen0 = 0, saw_seen1 = 0, gun_moved = 0;
        for (int i = 0; i < 60; i++) {
            float clock = i / 60.0f;      /* a second of standing still */
            int a = weapon_sprite_frame_at(WP_AXE, 0.0f, 0.0f, clock);
            if (a == AX_REV0) saw_seen0 = 1;
            if (a == AX_REV1) saw_seen1 = 1;
            if (weapon_sprite_frame_at(WP_SHOTGUN, 0.0f, 0.0f, clock) != SG_IDLE)
                gun_moved = 1;
        }
        ok(saw_seen0 && saw_seen1, "a resting chainsaw revs between two frames");
        ok(!gun_moved, "and a weapon with a one-frame idle stays still");
    }

    /* --- wp_init needs no GL context -------------------------------------
       This is the property the weapon/weaponview split exists to produce, and
       the only one of its claims a test can hold. There is no context in this
       process -- no window, no gl_bootstrap, nothing -- so before the split
       this call uploaded a texture through a null function pointer.
       tools\hooktest.c worked around that by never calling wp_init and
       building its Weapon with `= {0}`, which meant every hook fixture ran
       against a weapon the game never produces: no belt, no rng seed, and
       hook_enemy 0 rather than -1.

       If this ever needs a context again, something drawable has moved back
       into weapon.c, and the crash lands here rather than in a fixture that
       looked unrelated.

       wp_init에 GL 컨텍스트가 필요 없다는 것. weapon/weaponview 분리가 만들어 내려는 성질
       자체이며, 그 주장 중 테스트가 붙잡을 수 있는 유일한 것입니다. 이 프로세스에는
       컨텍스트가 없습니다. 창도, gl_bootstrap도 없습니다. 따라서 분리 이전에 이 호출은 널
       함수 포인터를 통해 텍스처를 업로드했습니다. tools\hooktest.c는 wp_init을 아예
       호출하지 않고 `= {0}`으로 Weapon을 만들어 우회했는데, 그것은 모든 훅 픽스처가 게임이
       결코 만들지 않는 무기(탄약대 없음, 난수 시드 없음, hook_enemy가 -1이 아니라 0)를
       대상으로 돌았다는 뜻입니다. */
    {
        Weapon w;
        wp_init(&w);

        /* `wp_init records the level` used to be the first check here, and it
           is gone with the field it checked: a weapon does not hold a level any
           more, it is handed one with the shot. What is left is what wp_init is
           actually for.
           이곳의 첫 검사는 `wp_init이 레벨을 기록한다`였고, 그것이 검사하던 필드와 함께
           사라졌습니다. 무기는 더 이상 레벨을 쥐지 않으며 사격과 함께 건네받습니다. 남은 것은
           wp_init이 실제로 하는 일입니다. */
        ok(w.owned[WP_SHOTGUN] && w.ammo[WP_SHOTGUN] == WEAPON_START_AMMO,
           "and hands over the boot belt");
        ok(w.hook_enemy == -1, "and marks the hook as attached to nothing");
        ok(w.rng != 0u, "and seeds the rng, which `= {0}` never did");

        /* The muzzle a headless weapon fires from: no model has been loaded,
           so this is the default rather than whatever a previous one left.
           헤드리스 무기가 발사하는 총구입니다. 모델이 로드된 적 없으므로 이전 모델이 남긴
           값이 아니라 기본값입니다. */
        v3 d = WP_MUZZLE_DEFAULT;
        ok(w.muzzle.x == d.x && w.muzzle.y == d.y && w.muzzle.z == d.z,
           "and starts at the default muzzle, not at the camera origin");
    }

    /* --- the ring's spin is the fire rate ---------------------------------
     *
     * WHAT THIS PINS IS A RATIO, not a rate. The whole point of driving the
     * wand's ring from ::WeaponType::cooldown is that the picture cannot
     * disagree with the gun: retune a cooldown and the ring follows, and this
     * stays green. Assert 6.30 rad/s on the rapid and the check would be
     * ::WPN_SPIN_RATE written twice, red the first time anybody tunes a weapon
     * for reasons that have nothing to do with the ring.
     *
     * THE FASTER WEAPON MUST TURN FASTER, and by the ratio of the two fire
     * rates -- which is what makes the ring readable as "what am I holding"
     * rather than as decoration that happens to move.
     *
     * AND A SHOT MUST BE VISIBLE IN IT. A ring that only ever turned at its
     * resting rate would say which weapon is held and nothing about firing it,
     * so the kick is checked as: faster immediately after a shot, and settled
     * back afterwards. The settling is what stops a held trigger stacking into
     * a blur -- ::wp_fire SETS the kick rather than adding to it.
     *
     * *못 박는 것은 비율이지 속도가 아닙니다.* 지팡이의 고리를 ::WeaponType::cooldown에서
     * 구동하는 요점 전체는 그림이 총과 어긋날 수 없다는 것입니다. 대기 시간을 조율하면 고리가
     * 따라오고 이 검사는 초록으로 남습니다. 래피드에 6.30 rad/s를 단언하면 그 검사는
     * ::WPN_SPIN_RATE를 두 번 쓴 것이 되고, 고리와 아무 상관 없는 이유로 누가 무기를 조율하는
     * 첫 순간에 빨개집니다.
     * *더 빠른 무기가 더 빨리 돌아야 하며*, 두 연사 속도의 비만큼 그래야 합니다. 그것이 고리를
     * 장식이 아니라 "내가 무엇을 쥐고 있는가"로 읽히게 만듭니다.
     * *그리고 발사가 그 안에 보여야 합니다.* 정지 속도로만 도는 고리는 어느 무기를 쥐었는지만
     * 말하고 발사에 대해서는 아무 말도 하지 않으므로, 충격은 이렇게 검사합니다. 발사 직후 더
     * 빠르고, 그 뒤 가라앉을 것. 가라앉음이 방아쇠를 누르고 있을 때 뭉개짐으로 쌓이지 않게
     * 막는 것입니다. ::wp_fire는 충격을 더하지 않고 *설정*합니다. */
    printf("\nthe ring turns at the fire rate\n");
    {
        Weapon w = (Weapon){0};
        float rate[WP_TYPES];
        for (int i = 0; i < WP_TYPES; i++) {
            w.cur = i;
            w.spin_kick = 0.0f;
            rate[i] = wp_spin_rate(&w);
            printf("      %-8s cooldown %.3fs -> %.2f rad/s\n",
                   wp_stats(i)->name, (double)wp_stats(i)->cooldown, (double)rate[i]);
            ok(rate[i] > 0.0f, wp_stats(i)->name);
        }

        /* Every pair, so this cannot pass by one weapon happening to be right. */
        int wrong = 0;
        for (int a = 0; a < WP_TYPES; a++)
            for (int b = 0; b < WP_TYPES; b++) {
                if (a == b) continue;
                float want = wp_stats(b)->cooldown / wp_stats(a)->cooldown;
                float got  = rate[a] / rate[b];
                if (got < want * 0.99f || got > want * 1.01f) wrong++;
            }
        okd(wrong == 0, "and every pair is in the ratio of their fire rates",
            wrong, 0);

        /* THE KICK IS FIRED, NOT ASSIGNED. Setting ::Weapon::spin_kick here and
           reading the rate back would check the arithmetic and nothing else --
           it passed with ::wp_fire's kick deleted and with the decay deleted,
           because neither line was on the path the test walked. So the shot
           goes through ::wp_update with `firing` set, exactly as a trigger
           pull does, and the settling is real frames of it.
           *충격은 대입하는 것이 아니라 발사하는 것입니다.* 이곳에서 ::Weapon::spin_kick을
           설정하고 속도를 되읽는 것은 산술만 검사할 뿐입니다. ::wp_fire의 충격을 지워도,
           감쇠를 지워도 통과했습니다. 두 줄 다 검사가 걷는 경로에 없었기 때문입니다. 그래서
           발사는 방아쇠를 당길 때와 똑같이 `firing`을 세운 ::wp_update를 지나가고, 가라앉음도
           실제 프레임입니다. */
        const float SDT = 1.0f / 60.0f;
        v3 seye = v3f(0, 0, 0), svel = v3f(0, 0, 0);
        Weapon f = (Weapon){0};
        wp_init(&f);
        f.cur = WP_SHOTGUN;
        f.ammo[WP_SHOTGUN] = 10;

        wp_update(&f, &g_pools, 0, SDT, 0, seye, 0, 0, 0, 0, 0, 1.4f, 1.6f, &svel, 1);
        float rest = wp_spin_rate(&f);

        wp_update(&f, &g_pools, 0, SDT, 1, seye, 0, 0, 0, 0, 0, 1.4f, 1.6f, &svel, 1);
        float shot = wp_spin_rate(&f);
        ok(shot > rest * 1.5f, "a shot shoves the ring well past its resting rate");

        for (int i = 0; i < 60; i++)
            wp_update(&f, &g_pools, 0, SDT, 0, seye, 0, 0, 0, 0, 0, 1.4f, 1.6f, &svel, 1);
        float after = wp_spin_rate(&f);
        printf("      shotgun rest %.2f, shot %.2f, one second later %.2f\n",
               (double)rest, (double)shot, (double)after);
        ok(after < rest * 1.05f, "and it settles back within a second");

        /* And the angle actually moved, so the rate is not a number nobody
           multiplies dt by. */
        ok(f.spin > 0.0f, "and the ring's angle advanced while it turned");
    }

    /* --- the ring turns without leaning ------------------------------------
     *
     * THE QUAD LIVES IN A 1x1 BOX OVER A VIEWPORT THAT IS NOT SQUARE, so one
     * unit of x and one of y are different numbers of pixels. That is fine for
     * a still drawing and fatal for a turning one: scale the corner offsets
     * before rotating and the two scales meet inside the rotation, which is a
     * shear. It shipped that way and the ring visibly leaned.
     *
     * MEASURED IN PIXELS, because ortho units are exactly the thing that lies
     * here -- a quad that is square in ortho units is not square on screen. The
     * corners come from ::wpview_emblem_quad, which is the arithmetic the frame
     * draws with rather than a copy of it: a check against its own second
     * implementation would stay green while the drawn ring sheared.
     *
     * TWO PROPERTIES, THROUGH A FULL TURN. Adjacent sides keep their length
     * ratio, and the corners stay square. Both are what "rigid" means and
     * neither can be satisfied by a shear. Against the broken order these read
     * 3.15 and 31 degrees; the tolerances below are far tighter than that and
     * far looser than the 1.003 and 0.18 the fix actually achieves, so this
     * fails on the bug and not on the last digit of a float.
     *
     * *사각형은 정사각형이 아닌 뷰포트 위 1x1 상자 안에 있으므로*, x 한 단위와 y 한 단위는
     * 서로 다른 픽셀 수입니다. 가만히 있는 그림에는 괜찮고 도는 그림에는 치명적입니다. 회전
     * 전에 모서리 오프셋에 배율을 주면 두 배율이 회전 안에서 만나며, 그것이 전단입니다. 그렇게
     * 출하되었고 고리가 눈에 띄게 기울었습니다.
     * *픽셀로 잽니다.* 이곳에서 거짓말하는 것이 바로 직교 단위이기 때문입니다. 직교 단위로
     * 정사각형인 사각형은 화면에서 정사각형이 아닙니다. 모서리는 ::wpview_emblem_quad에서
     * 오며, 그것은 사본이 아니라 프레임이 그리는 산술입니다. 자기 자신의 두 번째 구현에 대한
     * 검사는 그려지는 고리가 전단되는 동안에도 초록으로 남습니다.
     * *한 바퀴 동안 두 성질입니다.* 이웃한 변이 길이 비를 지키고, 모서리가 직각으로 남습니다.
     * 둘 다 "강체"의 뜻이며 전단으로는 어느 쪽도 만족되지 않습니다. 깨진 순서에서는 3.15와
     * 31도가 나옵니다. 아래 허용치는 그보다 훨씬 빡빡하고, 수정이 실제로 내는 1.003과
     * 0.18보다는 훨씬 헐거우므로, 이 검사는 결함에서 실패하고 실수의 마지막 자리에서는
     * 실패하지 않습니다. */
    printf("\nthe ring turns without leaning\n");
    {
        /* A 16:9 view, which is the shape the box is stretched over. */
        const float VW = 1600.0f, VH = 900.0f, ASPECT = VW / VH;
        float worst_ratio = 1.0f, worst_skew = 0.0f;

        for (int deg = 0; deg < 360; deg += 5) {
            float q[4][2];
            wpview_emblem_quad(ASPECT, (float)deg * 3.14159265f / 180.0f, q);

            /* Ortho units to pixels: x spans VW, y spans VH. */
            float px[4], py[4];
            for (int k = 0; k < 4; k++) { px[k] = q[k][0] * VW; py[k] = q[k][1] * VH; }

            float ax = px[1] - px[0], ay = py[1] - py[0];   /* one side */
            float bx = px[2] - px[1], by = py[2] - py[1];   /* the next */
            float la = sqrtf(ax * ax + ay * ay);
            float lb = sqrtf(bx * bx + by * by);
            if (la < 1e-6f || lb < 1e-6f) { ok(0, "the quad has area"); break; }

            float r = la > lb ? la / lb : lb / la;
            if (r > worst_ratio) worst_ratio = r;

            /* The corner between them, in degrees away from square. */
            float cosang = (ax * bx + ay * by) / (la * lb);
            if (cosang >  1.0f) cosang =  1.0f;
            if (cosang < -1.0f) cosang = -1.0f;
            float skew = fabsf(acosf(cosang) * 180.0f / 3.14159265f - 90.0f);
            if (skew > worst_skew) worst_skew = skew;
        }

        printf("      over a full turn: sides up to %.3f:1, corners off square by %.2f deg\n",
               (double)worst_ratio, (double)worst_skew);
        ok(worst_ratio < 1.05f, "adjacent sides keep their length through the turn");
        ok(worst_skew  < 2.0f,  "and the corners stay square");
    }

    /* --- and it stays set into the staff -----------------------------------
     *
     * THE EMBLEM IS PART OF THE WAND, not a badge floating beside it. That is a
     * composition, and a composition is exactly what a resize breaks: the wand
     * was shrunk to WPN_ART_SCALE of Doom's cell, and had the emblem kept the
     * old extent it would now be a magic circle wider than the staff it is set
     * into.
     *
     * A RATIO, NOT A RECTANGLE. The check does not know 40%, and must not --
     * that number is a taste decision the author is free to change. What it
     * knows is that the emblem's share of the wand is EMB_CW/WPN_CW across,
     * which is the atlas geometry the art was drawn against, and that the point
     * it turns about is on the wand. Both survive any scale; a scale applied to
     * one layer and not the other fails them.
     *
     * THE CENTRE AND NOT THE BOX, because the box is not inside the cell and
     * should not be. The emblem canvas keeps two transparent rows under its
     * ink, the ring reaches the cell's last row, and so the canvas hangs two
     * rows below the wand -- an earlier draft of this check asked whether the
     * canvas fitted and went red against art that was placed correctly. See
     * ::EMB_ON_WAND_Y.
     *
     * *문양은 지팡이의 일부이지*, 그 옆에 떠 있는 배지가 아닙니다. 그것은 구성이며, 크기 변경이
     * 정확히 깨뜨리는 것이 구성입니다. 지팡이는 Doom의 셀 대비 WPN_ART_SCALE로 줄었고,
     * 문양이 예전 크기를 유지했다면 지금쯤 자기가 박힌 지팡이보다 넓은 마법진이 되었을 것입니다.
     * *사각형이 아니라 비율입니다.* 이 검사는 40%를 모르며 알아서도 안 됩니다. 그 수는 저자가
     * 자유로이 바꿀 수 있는 취향의 결정입니다. 검사가 아는 것은, 문양이 지팡이에서 차지하는
     * 너비의 몫이 EMB_CW/WPN_CW라는 것, 그것이 아트가 저작된 기준인 아틀라스 기하라는 것,
     * 그리고 그것이 도는 중심점이 지팡이 위에 있다는 것입니다. 둘 다 어떤 배율에서도
     * 살아남습니다. 한 레이어에만 적용된 배율은 둘을 실패시킵니다.
     * *상자가 아니라 가운데입니다.* 상자는 셀 안에 있지 않으며 있어서도 안 되기 때문입니다.
     * 문양 캔버스는 잉크 아래에 투명한 두 행을 두고, 고리는 셀의 마지막 행에 닿으므로,
     * 캔버스는 지팡이보다 두 행 아래로 내려갑니다. 이 검사의 이전 초안은 캔버스가 들어맞는지
     * 물었고 올바르게 배치된 아트에 대해 빨간불이 되었습니다. ::EMB_ON_WAND_Y를 보십시오. */
    printf("\nthe emblem is set into the staff\n");
    {
        const float ASPECT = 1600.0f / 900.0f;
        float rect[4], q[4][2];
        wpview_art_rect(ASPECT, rect);
        wpview_emblem_quad(ASPECT, 0.0f, q);   /* unturned: the authored pose */

        float ex0 = q[0][0], ex1 = q[1][0];
        float ey0 = q[0][1], ey1 = q[2][1];
        float share = (ex1 - ex0) / (rect[2] - rect[0]);
        float want  = (float)EMB_CW / (float)WPN_CW;

        printf("      the emblem spans %.3f of the wand, the atlas says %.3f\n",
               (double)share, (double)want);
        ok(fabsf(share - want) < 0.01f,
           "the emblem keeps the atlas's share of the wand's width");
        float mx = (ex0 + ex1) * 0.5f, my = (ey0 + ey1) * 0.5f;
        ok(mx > rect[0] && mx < rect[2] && my > rect[1] && my < rect[3],
           "and turns about a point on the wand rather than beside it");
    }

    /* --- a swap flares white and comes back another colour -----------------
     *
     * THE FLOURISH IS THE ONLY THING THAT SAYS THE SWITCH TOOK. Nothing else
     * about a swap is visible: the belt is instant by design, the art in hand
     * is the same wand for every weapon, and the only part that differs is the
     * ring turning on it. A change the player cannot see is a change they will
     * make twice.
     *
     * WALKED THROUGH ::wp_update, not assigned. The spin check below learned
     * this the hard way -- a fixture that sets the field and reads it back
     * passes with the feature deleted, because the deleted line was never on
     * the path. So the swap is asked for the way a keypress asks for it and the
     * flourish is real frames of the real update.
     *
     * WHAT IS PINNED IS THE SHAPE, not the colours. The four hues come off the
     * art (::emblem_hue) and the artist may repaint them tomorrow. What must
     * not change is that the walk starts on the old weapon's colour, passes
     * through white, and ends leaning the way the new weapon's does -- white in
     * the middle is the whole reason the smear frame is colourless, and a
     * straight fade between two hues passes through the mud between them
     * instead of flaring.
     *
     * *연출만이 전환이 먹혔다고 말합니다.* 전환의 다른 무엇도 보이지 않습니다. 벨트는 설계상
     * 즉시이고, 손에 든 그림은 모든 무기에 대해 같은 지팡이이며, 다른 것은 그 위에서 도는
     * 고리뿐입니다. 플레이어가 볼 수 없는 변화는 그들이 두 번 하게 되는 변화입니다.
     * *대입이 아니라 ::wp_update를 통해 걷습니다.* 아래의 회전 검사가 이것을 힘들게
     * 배웠습니다. 필드를 설정하고 되읽는 픽스처는 기능을 지워도 통과합니다. 지워진 줄이 애초에
     * 경로에 없었기 때문입니다. 그래서 전환은 키 입력이 요청하는 방식으로 요청되고, 연출은
     * 실제 갱신의 실제 프레임입니다.
     * *고정하는 것은 색이 아니라 모양입니다.* 네 색상은 아트에서 옵니다(::emblem_hue). 작가는
     * 내일 다시 칠할 수 있습니다. 바뀌어서는 안 되는 것은, 걸음이 옛 무기의 색에서 시작해
     * 흰색을 지나 새 무기의 색이 기우는 쪽으로 끝난다는 것입니다. 가운데의 흰색이 스미어
     * 프레임이 무색인 이유의 전부이며, 두 색상 사이의 곧은 페이드는 그 사이의 진창을
     * 지나갑니다. */
    printf("\na weapon swap flares white and comes back another colour\n");
    {
        const float SDT = 1.0f / 60.0f;
        v3 seye = v3f(0, 0, 0), svel = v3f(0, 0, 0);
        Weapon f = (Weapon){0};
        wp_init(&f);
        f.owned[WP_RAPID]  = 1;
        f.ammo[WP_RAPID]   = 50;
        f.ammo[WP_SHOTGUN] = 10;

        float rest[3];
        wpview_emblem_tint(&f, rest);
        ok(rest[0] == 1.0f && rest[1] == 1.0f && rest[2] == 1.0f,
           "a weapon at rest draws its ring in the colours it was painted");
        ok(wpview_emblem_cell(&f) == WP_SHOTGUN,
           "and shows its own emblem");

        /* THE SMEAR'S OWN DRAWING HAS TO BE COLOURLESS. Everything below rests
           on it: a tinted canvas cannot be tinted to something else.
           *스미어 자신의 그림은 무색이어야 합니다.* 아래의 모든 것이 그것에 기댑니다. 이미
           물든 캔버스는 다른 것으로 물들일 수 없습니다. */
        float canvas[3];
        emblem_hue(EMB_SMEAR, canvas);
        printf("      the smear frame's own colour is %.2f %.2f %.2f\n",
               (double)canvas[0], (double)canvas[1], (double)canvas[2]);
        ok(fabsf(canvas[0] - canvas[1]) < 0.03f &&
           fabsf(canvas[1] - canvas[2]) < 0.03f,
           "the smear frame is drawn colourless, so a tint can give it a colour");

        wp_swap_to(&f, WP_RAPID);
        ok(f.cur == WP_RAPID, "a swap puts the new weapon in hand at once");
        ok(f.cooldown <= 0.0f, "and leaves it able to fire on the same frame");

        float first[3] = { 0, 0, 0 }, last[3] = { 1, 1, 1 };
        float ends = 0.0f, middle_lit = 0.0f;
        int frames = 0, smeared = 0;

        while (f.swap > 0.0f && frames < 240) {
            float c[3];
            wpview_emblem_tint(&f, c);
            if (frames == 0) for (int k = 0; k < 3; k++) first[k] = c[k];
            for (int k = 0; k < 3; k++) last[k] = c[k];

            float lo = c[0], hi = c[0];
            for (int k = 1; k < 3; k++) {
                if (c[k] < lo) lo = c[k];
                if (c[k] > hi) hi = c[k];
            }
            float spread = hi - lo, t = wp_swap_t(&f);
            if (t < 0.15f || t > 0.85f) { if (spread > ends) ends = spread; }
            /* THE DIMMEST CHANNEL, not the spread between them. A spread near
               zero only says "grey", and grey is where a straight fade between
               two roughly opposite hues passes anyway -- this fixture's own
               pair does, at 0.03, so the first cut of this check passed with
               the white midpoint deleted. What only a walk THROUGH WHITE
               produces is all three channels up at once, so that is what is
               asked for.
               *가장 어두운 채널이지 채널 사이의 폭이 아닙니다.* 폭이 0에 가깝다는 것은
               "회색"이라는 뜻일 뿐이고, 대략 반대인 두 색상 사이의 곧은 페이드는 어차피
               회색을 지나갑니다. 이 픽스처의 짝도 0.03으로 그러하며, 그래서 이 검사의 첫
               판은 흰색 중간점을 지워도 통과했습니다. 흰색을 *지나는* 걸음만이 내는 것은 세
               채널이 동시에 올라가는 것이며, 그래서 그것을 묻습니다. */
            if (t > 0.40f && t < 0.60f) { if (lo > middle_lit) middle_lit = lo; }
            if (wpview_emblem_cell(&f) == EMB_SMEAR) smeared++;

            wp_update(&f, &g_pools, 0, SDT, 0, seye, 0, 0, 0, 0, 0,
                      1.4f, 1.6f, &svel, 1);
            frames++;
        }

        float from[3], to[3];
        emblem_hue(WP_SHOTGUN, from);
        emblem_hue(WP_RAPID,   to);
        int lead_last = 0, lead_to = 0;
        for (int k = 1; k < 3; k++) {
            if (last[k] > last[lead_last]) lead_last = k;
            if (to[k]   > to[lead_to])     lead_to   = k;
        }

        printf("      %d frame(s), %d of them smeared; spread %.2f at the ends, "
               "dimmest channel %.2f in the middle\n", frames, smeared,
               (double)ends, (double)middle_lit);
        printf("      it ends leaning on channel %d and the new weapon's is %d\n",
               lead_last, lead_to);

        ok(frames > 0 && frames <= (int)(WPN_SWAP_TIME * 60.0f) + 2,
           "the flourish runs itself out in about WPN_SWAP_TIME");
        ok(smeared == frames,
           "and the ring is the smear frame for every frame of it");
        ok(fabsf(first[0] - from[0]) < 0.01f &&
           fabsf(first[1] - from[1]) < 0.01f &&
           fabsf(first[2] - from[2]) < 0.01f,
           "it starts on the colour the weapon being put away was painted");
        ok(middle_lit > 0.90f, "flares all the way to white halfway through");
        ok(ends > 0.15f,   "and is a colour and not a wash at both ends");
        ok(lead_last == lead_to,
           "it ends leaning the way the weapon being drawn does");
        ok(wpview_emblem_cell(&f) == WP_RAPID,
           "then the new weapon's own emblem takes the ring back");
    }

    /* --- the saw runs while it cuts ---------------------------------------
     *
     * EVERY OTHER WEAPON SHOVES THE RING AND LETS IT DECAY, which reads as
     * recoil. The saw is the one whose ring is a blade, so it holds a rate for
     * ::WPN_SAW_SPIN_TIME instead: at speed while cutting, stopped after.
     *
     * A RATIO AND A SHAPE, not the constants. What is pinned is that a swing
     * makes the ring turn several times faster than at rest, that it is still
     * at that speed a swing-length later (so holding the trigger is a steady
     * scream and not a pulse), and that it is back to rest once the window
     * ends. Any of those broken is a saw that reads wrong; none of them cares
     * what the numbers are.
     *
     * DRIVEN THROUGH ::wp_update AND ::wp_fire, never by writing the field:
     * a check that sets `saw_spin` by hand passes with the feature deleted.
     *
     * *다른 모든 무기는 고리를 떠밀고 감쇠시키며*, 그것은 반동으로 읽힙니다. 톱은 고리가 곧
     * 날인 유일한 무기이므로 대신 ::WPN_SAW_SPIN_TIME 동안 속도를 유지합니다. 자르는 동안은
     * 최고 속도이고 그 뒤에는 멎습니다.
     * *상수가 아니라 비율과 형태입니다.* 고정하는 것은, 휘두르면 고리가 휴지 상태보다 몇 배
     * 빨리 돈다는 것, 휘두르기 하나만큼 지난 뒤에도 여전히 그 속도라는 것(그래서 방아쇠를 누르고
     * 있으면 맥동이 아니라 한결같은 비명), 그리고 창이 끝나면 휴지 속도로 돌아온다는 것입니다.
     * 셋 중 무엇이 깨져도 잘못 읽히는 톱이며, 어느 것도 숫자가 얼마인지는 상관하지 않습니다.
     * *필드를 쓰지 않고 ::wp_update와 ::wp_fire를 통해 구동합니다.* `saw_spin`을 손으로
     * 설정하는 검사는 기능이 삭제되어도 통과합니다. */
    printf("\nthe saw runs while it cuts\n");
    {
        const float SDT = 1.0f / 60.0f;
        v3 seye = v3f(0, 0, 0), svel = v3f(0, 0, 0);
        Weapon f = (Weapon){0};
        wp_init(&f);
        f.owned[WP_AXE] = 1;
        f.cur = WP_AXE;

        wp_update(&f, &g_pools, 0, SDT, 0, seye, 0, 0, 0, 0, 0, 1.4f, 1.6f, &svel, 1);
        float rest = wp_spin_rate(&f);

        /* One swing, through the same path a held trigger takes. */
        wp_update(&f, &g_pools, 0, SDT, 1, seye, 0, 0, 0, 0, 0, 1.4f, 1.6f, &svel, 1);
        float cutting = wp_spin_rate(&f);

        /* A swing-length later with the trigger released: still at speed,
           because the window outlasts the cooldown. */
        for (int i = 0; i < (int)(wp_stats(WP_AXE)->cooldown / SDT); i++)
            wp_update(&f, &g_pools, 0, SDT, 0, seye, 0, 0, 0, 0, 0, 1.4f, 1.6f, &svel, 1);
        float still = wp_spin_rate(&f);

        /* Past the window: back to rest. */
        for (int i = 0; i < (int)(WPN_SAW_SPIN_TIME / SDT) + 8; i++)
            wp_update(&f, &g_pools, 0, SDT, 0, seye, 0, 0, 0, 0, 0, 1.4f, 1.6f, &svel, 1);
        float after = wp_spin_rate(&f);

        printf("      saw rest %.2f rad/s, cutting %.2f, one swing later %.2f, after %.2f\n",
               (double)rest, (double)cutting, (double)still, (double)after);
        ok(cutting > rest * 5.0f, "a swing spins the blade several times faster than rest");
        ok(still   > rest * 5.0f, "and it is still at speed a swing later: a steady cut, not a pulse");
        ok(after   < rest * 1.5f, "and it stops once the cut is over");

        /* No other weapon holds a rate: the shotgun's shove decays away over
           the same span. 다른 무기는 속도를 유지하지 않습니다. 샷건의 충격은 같은 시간 동안
           감쇠해 사라집니다. */
        Weapon g = (Weapon){0};
        wp_init(&g);
        g.cur = WP_SHOTGUN;
        g.ammo[WP_SHOTGUN] = 10;
        wp_update(&g, &g_pools, 0, SDT, 0, seye, 0, 0, 0, 0, 0, 1.4f, 1.6f, &svel, 1);
        float grest = wp_spin_rate(&g);
        wp_update(&g, &g_pools, 0, SDT, 1, seye, 0, 0, 0, 0, 0, 1.4f, 1.6f, &svel, 1);
        float gshot = wp_spin_rate(&g);
        for (int i = 0; i < (int)(WPN_SAW_SPIN_TIME / SDT) + 8; i++)
            wp_update(&g, &g_pools, 0, SDT, 0, seye, 0, 0, 0, 0, 0, 1.4f, 1.6f, &svel, 1);
        float gafter = wp_spin_rate(&g);
        printf("      shotgun rest %.2f, shot %.2f, after %.2f\n",
               (double)grest, (double)gshot, (double)gafter);
        /* MEASURED AT THE SHOT, not only after it. Checking the end state alone
           passes for a shotgun that briefly span like a blade and then stopped
           -- verified: giving every weapon `saw_spin` broke nothing here until
           this line was added.
           *끝 상태만이 아니라 발사 시점에 잽니다.* 끝 상태만 보는 검사는 잠깐 날처럼 돌았다가
           멎은 샷건에 대해서도 통과합니다. 확인했습니다. 모든 무기에 `saw_spin`을 줘도 이 줄이
           추가되기 전까지는 이곳에서 아무것도 깨지지 않았습니다. */
        ok(gshot < cutting * 0.5f,
           "a shot is a shove and not a blade: nowhere near the saw's cutting rate");
        ok(gafter < grest * 1.5f,
           "and the shove has decayed away over the same span");
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall weapon checks passed\n", fails);
    return fails != 0;
}
