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
            if (S->max_ammo <= 0)             bad++;
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

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall weapon checks passed\n", fails);
    return fails != 0;
}
