/**
 * @file proj.c
 * @brief Player projectile flight, bouncing, fuses and blasts. No GL.
 *
 * ENGLISH
 * -------
 * See proj.h for why this is separate from enemy.c's Shot.
 *
 * 한국어
 * ------
 * enemy.c의 Shot과 분리한 이유는 proj.h를 참조하십시오.
 */

#include "proj.h"
#include "pools.h"   /* the bundle the calls below are handed */
#include "enemy.h"
#include "audio.h"
#include "fx.h"
#include "diag.h"
#include <math.h>

void proj_reset(Pools *pl) {
    for (int i = 0; i < PROJ_MAX; i++) pl->proj.p[i].active = 0;

    /* The flashes with them. Life to zero rather than the array cleared, the
       way ::decal_reset does it: life is the only field that decides whether a
       slot is real, so zeroing it retires every flash without touching
       positions nothing will read.
       섬광도 함께입니다. ::decal_reset처럼 배열을 지우지 않고 수명을 0으로 둡니다. 슬롯이
       실재하는지를 결정하는 필드는 수명뿐이므로, 그것을 0으로 만들면 아무도 읽지 않을 위치를
       건드리지 않고 모든 섬광을 물러나게 합니다. */
    for (int i = 0; i < PROJ_MAX_FLASHES; i++) pl->flash.f[i].life = 0.0f;
    pl->flash.next = 0;
}

/* --- the flash / 섬광 ------------------------------------------------------
   Four functions and no branch worth the name. The whole of the design is in
   proj.h; this is the arithmetic under it.
   함수 넷이며 분기라 할 만한 것이 없습니다. 설계 전체는 proj.h에 있고, 이곳은 그 아래의
   산술입니다. */

void proj_flash(Pools *pl, v3 at, float radius, float power, int kind, int type) {
    /* Nothing to light and nothing to shake. Refused here rather than left for
       every reader to test, which is where the muzzle flash's own rule already
       lives -- ::light_offer ignores a power of zero so a caller may hand over
       a source that has faded without checking it first.
       밝힐 것도 흔들 것도 없습니다. 모든 독자가 검사하게 두지 않고 이곳에서 거절합니다.
       총구 섬광의 같은 규칙이 이미 그렇게 되어 있습니다. ::light_offer는 세기가 0이면
       무시하므로, 호출자는 사그라든 광원을 먼저 검사하지 않고 그대로 건네도 됩니다. */
    if (radius <= 0.0f || power <= 0.0f) return;

    Flash *f = &pl->flash.f[pl->flash.next];
    pl->flash.next = (pl->flash.next + 1) % PROJ_MAX_FLASHES;

    f->pos    = at;
    f->radius = radius;
    f->power  = power > 1.0f ? 1.0f : power;
    f->life   = PROJ_FLASH_TIME;
    f->kind   = (short)kind;
    f->type   = (short)type;
}

void proj_flash_update(Pools *pl, float dt) {
    for (int i = 0; i < PROJ_MAX_FLASHES; i++) {
        Flash *f = &pl->flash.f[i];
        if (f->life <= 0.0f) continue;
        f->life -= dt;
        if (f->life < 0.0f) f->life = 0.0f;
    }
}

int proj_flash_count(const Pools *pl) { (void)pl; return PROJ_MAX_FLASHES; }

const Flash *proj_flash_at(const Pools *pl, int i) {
    return (i >= 0 && i < PROJ_MAX_FLASHES) ? &pl->flash.f[i] : 0;
}

int proj_flash_live(const Pools *pl) {
    int n = 0;
    for (int i = 0; i < PROJ_MAX_FLASHES; i++) if (pl->flash.f[i].life > 0.0f) n++;
    return n;
}

float proj_flash_fade(const Flash *f) {
    if (!f || f->life <= 0.0f) return 0.0f;
    float t = f->life / PROJ_FLASH_TIME;
    if (t > 1.0f) t = 1.0f;
    return t * t;
}

int proj_count(const Pools *pl) { (void)pl; return PROJ_MAX; }

const Proj *proj_at(const Pools *pl, int i) {
    return (i >= 0 && i < PROJ_MAX) ? &pl->proj.p[i] : 0;
}

int proj_live(const Pools *pl) {
    int n = 0;
    for (int i = 0; i < PROJ_MAX; i++) if (pl->proj.p[i].active) n++;
    return n;
}

int proj_fire(Pools *pl, v3 from, v3 dir, float speed, float gravity,
              int damage, float blast, float fuse) {
    Proj *p = 0;
    for (int i = 0; i < PROJ_MAX; i++)
        if (!pl->proj.p[i].active) { p = &pl->proj.p[i]; break; }

    /* Every other pool here reports when it turns something away, and a shot
       that produced no projectile is indistinguishable from one that missed --
       the animation and the sound play either way. Costs nothing in release.
       이 프로젝트의 다른 모든 풀은 무언가를 거절할 때 보고합니다. 발사체를 만들어 내지
       못한 사격은 빗나간 사격과 구분되지 않습니다. 어느 쪽이든 애니메이션과 소리는
       재생되기 때문입니다. 릴리스에서는 비용이 없습니다. */
    if (!p) { DIAG(DIAG_SHOT_CAP); return 0; }

    float len = v3len(dir);
    if (len < 1e-6f) return 0;
    dir = v3scale(dir, 1.0f / len);

    p->pos     = from;
    p->vel     = v3scale(dir, speed);
    p->gravity = gravity;
    p->damage  = damage;
    p->blast   = blast;
    p->fuse    = fuse;
    p->life    = 6.0f;
    p->spin    = 0.0f;
    /* Zero, so the first puff is laid on the frame the round leaves the
       barrel. A full interval here would start the trail 35ms downrange, and
       35ms of a 26 m/s grenade is most of a metre -- a gap between the muzzle
       and the trail, at the one moment the player is looking straight at both.
       0입니다. 그래야 첫 퍼프가 탄이 총구를 떠나는 프레임에 놓입니다. 이곳에 온전한 간격을
       두면 궤적이 35ms 앞에서 시작하는데, 26m/s 유탄의 35ms는 1미터에 가깝습니다. 플레이어가
       총구와 궤적을 동시에 정면으로 보고 있는 바로 그 순간에 둘 사이가 벌어집니다. */
    p->trail_t = 0.0f;
    p->active  = 1;
    return 1;
}

int proj_blast(Pools *pl, v3 at, float radius, int damage) {
    if (radius <= 0.0f) return 0;
    int hits = 0;

    for (int i = 0; i < enemy_count(pl); i++) {
        const Enemy *m = enemy_at(pl, i);
        if (!m || !m->active || m->state == E_DEAD) continue;

        /* Measured to the monster's MIDDLE, not its feet. A blast beside a
           brute's boots should not count as further away than one beside its
           head, and the feet are what `pos` holds.
           발이 아니라 몬스터의 *몸통 중심*까지 잽니다. 브루트의 발 옆에서 터진 폭발이
           머리 옆에서 터진 것보다 멀다고 계산되어서는 안 되며, `pos`가 담고 있는 것이
           발입니다. */
        const MonType *S = mon_stats(m->type);
        v3 mid = v3f(m->pos.x, m->pos.y + S->height * 0.5f, m->pos.z);
        v3 d   = v3sub(mid, at);
        float dist = v3len(d);
        if (dist > radius) continue;

        /* Linear falloff to zero at the rim, with a floor of one point so a
           monster inside the radius is never told it was hit for nothing.
           A quadratic curve was the alternative and it makes the rim useless:
           past about 60% of the radius it rounds to zero, so the blast reads
           as much smaller than it draws.
           가장자리에서 0이 되는 선형 감쇠이며, 반경 안의 몬스터가 피해 0을 통보받지
           않도록 최소 1을 보장합니다. 이차 곡선이 대안이었지만 가장자리를 쓸모없게
           만듭니다. 반경의 약 60%를 넘으면 0으로 반올림되어, 폭발이 그려지는 것보다 훨씬
           작게 느껴집니다. */
        float t = 1.0f - dist / radius;
        int dmg = (int)(damage * t + 0.5f);
        if (dmg < 1) dmg = 1;

        /* Pushed outward from the blast, so the spray leaves the body away
           from where the explosion was. */
        v3 away = dist > 1e-4f ? v3scale(d, 1.0f / dist) : v3f(0, 1, 0);
        enemy_hurt(pl, i, dmg, away);
        hits++;
    }
    return hits;
}

/* A projectile's end: the blast if it has one, otherwise a single-target hit
   that the caller has already applied.
   발사체의 최후입니다. 폭발 반경이 있으면 폭발이고, 없으면 호출자가 이미 적용한 단일
   대상 피격입니다. */
static void detonate(Pools *pl, Proj *p, v3 at, v3 normal, int flesh) {
    if (p->blast > 0.0f) {
        proj_blast(pl, at, p->blast, p->damage);

        /* THE DOME IS SCALED BY THE RADIUS IT IS DRAWING. Its speed is
           authored so speed x life reaches one metre, so passing the blast
           radius makes the shell stop exactly where the damage does -- the
           point of drawing it at all is that the radius is a gameplay number
           and a player who cannot see it is guessing.
           Spawned along +Y rather than the surface normal: a blast is a
           hemisphere standing on the ground, and one leaning off a wall's
           normal would claim a shape the damage does not have.
           돔은 자신이 그리는 반경으로 배율이 정해집니다. speed x life가 1미터에 닿도록
           작성했으므로 폭발 반경을 넘기면 껍질이 데미지가 멈추는 바로 그 자리에서
           멈춥니다. 애초에 이것을 그리는 이유가, 반경이 게임플레이 수치이고 그것을 볼 수
           없는 플레이어는 짐작하게 되기 때문입니다. 표면 법선이 아니라 +Y로 생성하는
           이유는, 폭발이 지면에 선 반구이고 벽의 법선을 따라 기울어진 돔은 데미지가 갖지
           않은 모양을 주장하기 때문입니다. */
        fx_spawn_scaled(pl, "blastdome", at, v3f(0, 1, 0), p->blast);

        /* THE SAME RADIUS, ALONG THE GROUND. The dome is a shell in the air
           and an explosion in a room is read off the FLOOR -- that is where
           the player's own feet are and where the distance to it can be
           judged. `blastwave` is the dome's equator drawn on the surface the
           blast happened against, so it is scaled by the same number for the
           same reason, and spawned along the surface normal rather than +Y
           because a wave running out across a wall runs across the wall.
           같은 반경을, 지면을 따라. 돔은 공중의 껍질이고, 방 안의 폭발은 *바닥*에서
           읽힙니다. 플레이어 자신의 발이 있는 곳이며 거리를 가늠할 수 있는 곳입니다.
           `blastwave`는 폭발이 부딪힌 표면에 그려진 돔의 적도이므로 같은 이유로 같은 수로
           배율이 정해지며, +Y가 아니라 표면 법선으로 생성합니다. 벽을 가로질러 퍼지는
           파동은 벽을 가로질러 퍼지기 때문입니다. */
        fx_spawn_scaled(pl, "blastwave",
                        v3add(at, v3scale(normal, PROJ_WAVE_LIFT)),
                        normal, p->blast);

        /* AND THE SAME RIM, LIT. `blastwave` is dust and is alpha-blended so
           it can occlude the floor, which is the right answer for what a
           shockwave does to grit and the wrong one for what it does to the
           eye: in a dark room a pale smudge is not a radius anybody can read.
           Same position, same normal, same scale -- the two stop on one rim,
           and they have to be handed the same three arguments or they stop
           being that. NOT at the same moment, though: the dust runs its 100cm
           out in 300ms and the light takes 420, so the pale edge arrives first
           and the bright one follows it in and stays after it has gone.
           그리고 *같은 테두리를, 빛으로.* `blastwave`는 먼지이고 바닥을 가릴 수 있도록 알파
           블렌드입니다. 충격파가 모래에 하는 일에 대해서는 옳은 답이고 *눈에* 하는 일에
           대해서는 틀린 답입니다. 어두운 방에서 창백한 얼룩은 아무도 읽을 수 없는 반경입니다.
           같은 위치, 같은 법선, 같은 배율입니다. 둘은 두 번 그려진 하나의 고리이며, 같은 인자
           셋을 건네받지 못하면 그것이기를 그만둡니다. */
        fx_spawn_scaled(pl, "blastring",
                        v3add(at, v3scale(normal, PROJ_WAVE_LIFT)),
                        normal, p->blast);

        /* The first two frames, before anything has had time to become a
           shape. `blastcore` was already the "does not travel" layer and it
           still is; what it could not be is OVEREXPOSED, because a core that
           blows out for 170ms is a core that is still blowing out when the
           smoke arrives. So the white is its own layer with its own, much
           shorter life -- see blastflash in effects.txt.
           무엇도 형태가 될 시간을 갖기 전의 처음 두 프레임입니다. `blastcore`는 이미
           "이동하지 않는" 겹이었고 지금도 그렇습니다. 그것이 될 수 없었던 것은 *과다
           노출*입니다. 170ms 동안 흰색으로 날아가는 코어는 연기가 도착할 때까지도 계속
           날아가고 있는 코어이기 때문입니다. 그래서 흰색은 훨씬 짧은 자기 수명을 가진 자기
           겹입니다. effects.txt의 blastflash를 참조하십시오. */
        fx_spawn(pl, "blastflash",  at, normal);

        fx_spawn(pl, "blastcore",   at, normal);

        /* THE SECOND THE BLAST SPENDS AS A CLOUD. Everything above this line
           is additive -- the dome, the core and the flash are light, and light
           added to a frame can only brighten it -- so the explosion had two
           states and nothing in between: white-hot, then grey smoke climbing
           out of an empty floor. `blastfire` is alpha, which is what lets it
           go DARK, and its colour ramp carries it from flame to soot on its
           own. Along +Y with the smoke rather than along the surface normal,
           for the reason the dome is: a fireball stands up out of the floor,
           it does not lean off the wall it happened to touch.
           *폭발이 구름으로 보내는 그 1초.* 이 줄 위의 모든 것은 가산입니다. 돔과 코어와
           섬광은 빛이고, 프레임에 더해진 빛은 밝게 하는 일밖에 못 합니다. 그래서 폭발에는 두
           상태만 있고 그 사이에는 아무것도 없었습니다. 새하얗게 뜨겁거나, 텅 빈 바닥에서
           회색 연기가 오르거나. `blastfire`는 알파이며 그것이 *어두워질* 수 있게 하고, 색
           램프가 혼자서 불꽃에서 그을음까지 데려갑니다. 표면 법선이 아니라 연기와 함께
           +Y인 이유는 돔이 그러한 이유와 같습니다. 화구는 바닥에서 일어서지, 어쩌다 닿은
           벽을 따라 기울지 않습니다. */
        fx_spawn(pl, "blastfire",   at, v3f(0, 1, 0));

        fx_spawn(pl, "blastsmoke",  at, v3f(0, 1, 0));
        fx_spawn(pl, "blastdebris", at, normal);

        /* WHAT IS STILL BURNING A SECOND LATER. Every layer above this one is
           over inside 300ms except the smoke, and smoke alone says the fire
           went out. Embers are the layer that says it did not: additive, slow,
           and they arc and fall, so the eye keeps finding the blast site after
           the blast is finished with.
           1초 뒤에도 *아직 타고 있는 것*입니다. 이 위의 모든 겹은 연기를 빼면 300ms 안에
           끝나며, 연기만으로는 불이 꺼졌다고 말하게 됩니다. 불티가 그렇지 않다고 말하는
           겹입니다. 가산이고 느리며 포물선을 그리며 떨어지므로, 폭발이 끝난 뒤에도 눈이
           계속 폭발 지점을 찾아냅니다. */
        fx_spawn(pl, "blastember",  at, normal);
        /* blastburst, NOT boltburst. This borrowed the monster bolt's
           flash, which cools into that bolt's blue -- so a grenade going
           off threw blue sparks. The two events want the same SHAPE and
           opposite colours; sharing one effect gave them the reverse.
           boltburst가 아니라 blastburst입니다. 이 줄은 몬스터 볼트의 섬광을
           빌려 썼고 그것은 그 볼트의 파랑으로 식습니다. 그래서 유탄이 터지면
           파란 불꽃이 튀었습니다. 두 사건은 같은 *형태*와 반대되는 색을
           원하는데, 하나를 공유하면 그 반대를 얻습니다. */
        fx_spawn(pl, "blastburst", at, normal);
        fx_spawn(pl, "blastshard", at, normal);

        /* AND THE LIGHT, WHICH THE PARTICLES ABOVE CANNOT BE. Every layer here
           is additive geometry drawn in FRONT of the room; none of them
           brightens the wall behind them, the monster standing against it, or
           the floor under the player's feet. Until this line the loudest event
           in the game was the one frame in which all of that got darker,
           because the grenade had been lighting it right up until it stopped
           existing. Full power: a charge going off is the reference the axe's
           slam is measured against. See ::Flash.
           그리고 *빛*이며, 위의 입자들은 그것이 될 수 없습니다. 이곳의 모든 겹은 방 *앞에*
           그려지는 가산 지오메트리입니다. 그 어느 것도 뒤의 벽을, 그 벽에 붙어 선 몬스터를,
           플레이어 발밑의 바닥을 밝히지 않습니다. 이 줄이 생기기 전까지 이 게임에서 가장 큰
           사건은 그 모든 것이 오히려 어두워지는 한 프레임이었습니다. 유탄이 존재하기를
           그만두기 직전까지 그것을 밝히고 있었기 때문입니다. 세기는 최대입니다. 장약이
           터지는 것이 도끼의 내려찍기를 재는 기준입니다. ::Flash를 참조하십시오. */
        proj_flash(pl, at, p->blast, 1.0f, FLASH_BLAST, -1);

        /* ITS OWN SOUND, AND FROM WHERE IT HAPPENED. This was `impact`, which
           is DSPUNCH -- a punch -- at a flat gain of 100 wherever in the level
           it went off. Two faults in one line: the wrong sound, and a blast
           across the map as loud as one at your feet. A grenade you cannot
           place by ear is one you cannot learn to avoid.
           자기 소리이며, 일어난 자리에서 납니다. 이것은 `impact`, 즉 DSPUNCH(주먹질)였고,
           레벨 어디서 터지든 고정 게인 100이었습니다. 한 줄에 결함이 둘입니다. 틀린
           소리, 그리고 맵 건너편의 폭발이 발밑의 폭발과 같은 크기라는 것. 귀로 위치를
           짚을 수 없는 유탄은 피하는 법을 배울 수 없는 유탄입니다. */
        audio_play_at("blast", 100, at);
    } else {
        /* --- A BOLT LANDING, IN THE LAYERS EVERY ENGINE BUILDS ONE FROM -----
           This was one line and the line was `spark`, which is the SHOTGUN's
           effect: warm orange, authored for a lead pellet chipping stone. The
           rapid's bolt is plasma, it is green everywhere else it appears, and
           it was throwing the palette's other side on every hit. That is the
           fault this file already found once and wrote down -- `blastburst`
           exists because a grenade going off was borrowing the monsters' blue
           -- reflected: same shape, wrong colour, opposite direction.

           The four layers are not invented here. An energy impact is built the
           same way in every engine this project has borrowed from: Quake II's
           TE_BLASTER is particles plus a light; ioquake3's CG_MissileHitWall
           is a mark, an explosion sprite, a light and a sound, and it picks
           energyMarkShader over bulletMarkShader precisely because a bolt
           burns where a bullet chips; and Xonotic's `electro_impact` is three
           blocks -- a decal carrying the light, a smoke puff pushed along the
           surface, and a short bright core -- with the sparks split into
           `electro_ballexplode`, drawn with a `stretchfactor`, which is the
           same parameter as this file's `stretch`.

           FLESH TAKES THE FIRST TWO AND NOT THE LAST TWO, which is the rule
           weapon.c already states for the shotgun: a scorch mark and a puff of
           stone dust coming off a monster read as having MISSED it.

           AND IT IS GIVEN NO BLOOD HERE, which looks like the omission and is
           the opposite of one. ::enemy_hurt spawns `blood` on every non-fatal
           hit and `gib` on the one that kills, and it has already run by the
           time this branch is reached -- the caller damages the monster and
           then detonates. A second burst thrown from here would be the same
           event drawn twice, at eleven hits a second, out of a pool this
           weapon is already the heaviest user of.

           *볼트의 착탄이며, 모든 엔진이 그것을 쌓는 방식대로 쌓은 겹들입니다.* 이것은 한
           줄이었고 그 줄은 `spark`, 즉 *샷건의* 이펙트였습니다. 따뜻한 주황이고, 납 산탄이
           돌을 쪼는 것을 위해 작성된 것입니다. 연사의 탄은 플라즈마이고 다른 모든 곳에서
           녹색이며, 명중할 때마다 팔레트의 반대쪽을 던지고 있었습니다. 이 파일이 이미 한 번
           찾아서 적어 둔 결함(유탄이 몬스터의 파랑을 빌려 쓰고 있었기에 `blastburst`가
           존재합니다)을 거울에 비친 것입니다. 같은 형태, 틀린 색, 반대 방향.

           네 겹은 이곳에서 지어낸 것이 아닙니다. 이 프로젝트가 빌려 온 모든 엔진에서 에너지
           피탄은 같은 방식으로 만들어집니다. Quake II의 TE_BLASTER는 입자와 빛이고,
           ioquake3의 CG_MissileHitWall은 자국·폭발 스프라이트·빛·소리이며 bulletMarkShader가
           아니라 energyMarkShader를 고릅니다. 탄환은 쪼지만 볼트는 *태우기* 때문입니다.
           그리고 Xonotic의 `electro_impact`는 세 블록입니다. 빛을 지닌 데칼, 표면을 따라
           밀려 나가는 연기, 그리고 짧고 밝은 코어. 불꽃은 `electro_ballexplode`로 분리되어
           있고 `stretchfactor`로 그려지는데, 그것이 이 파일의 `stretch`와 같은 매개변수입니다.

           *살은 앞의 둘을 받고 뒤의 둘은 받지 않습니다.* weapon.c가 샷건에 대해 이미 말하고
           있는 규칙입니다. 몬스터에서 이는 그을음 자국과 돌먼지는 빗맞은 것으로 읽힙니다.

           *그리고 이곳에서 피를 주지 않는데*, 그것은 빠뜨린 것처럼 보이지만 정반대입니다.
           ::enemy_hurt는 치명적이지 않은 모든 명중에 `blood`를, 죽이는 한 방에 `gib`을
           생성하며, 이 갈래에 닿을 때는 이미 실행된 뒤입니다. 호출자가 몬스터에 피해를 준 다음
           터뜨리기 때문입니다. 이곳에서 던지는 두 번째 폭발은 같은 사건을 두 번 그리는 것이며,
           초당 열한 번, 이 무기가 이미 가장 무겁게 쓰고 있는 풀에서 그렇게 하는 것입니다. */
        fx_spawn(pl, "zapflash", at, normal);
        fx_spawn(pl, "zapburst", at, normal);
        if (!flesh) {
            fx_spawn(pl, "scorch",    at, normal);
            fx_spawn(pl, "smokepuff", at, normal);
        }

        /* AND THE LIGHT, for the reason the blast's own note gives above: the
           bolt has been lighting this wall green the whole way in, and without
           this the brightest thing about the hit is the frame the wall goes
           dark again. Small and dim -- see ::PROJ_HIT_RADIUS -- because the
           rapid puts one here every 85ms.
           그리고 *빛*이며, 위의 폭발이 남긴 설명과 같은 이유에서입니다. 볼트는 오는 내내 이
           벽을 녹색으로 밝히고 있었고, 이것이 없으면 피탄에서 가장 밝은 것은 벽이 다시
           어두워지는 프레임입니다. 작고 어둡습니다(::PROJ_HIT_RADIUS 참조). 연사가 85ms마다
           하나씩 이곳에 놓기 때문입니다. */
        proj_flash(pl, at, PROJ_HIT_RADIUS, PROJ_HIT_POWER, FLASH_BOLT, -1);
        audio_play_at("impact", 45, at);
    }
    p->active = 0;
}

void proj_update(Pools *pl, const Level *l, float dt) {
    for (int i = 0; i < PROJ_MAX; i++) {
        Proj *p = &pl->proj.p[i];
        if (!p->active) continue;

        p->spin += dt;
        p->life -= dt;
        if (p->life <= 0.0f) { p->active = 0; continue; }

        /* The fuse burns whether or not the grenade is moving, which is what
           makes one resting at your feet a threat rather than scenery.
           도화선은 유탄이 움직이든 아니든 탑니다. 발밑에 멈춰 있는 것이 배경이 아니라
           위협이 되는 이유입니다. */
        if (p->fuse > 0.0f) {
            p->fuse -= dt;
            if (p->fuse <= 0.0f) { detonate(pl, p, p->pos, v3f(0, 1, 0), 0); continue; }
        }

        if (p->gravity > 0.0f) p->vel.y -= p->gravity * dt;

        v3 step = v3scale(p->vel, dt);
        float dist = v3len(step);
        if (dist < 1e-6f) continue;
        v3 dir = v3scale(step, 1.0f / dist);

        /* --- what it leaves behind it --------------------------------------
           ONLY THE ARCING KIND. `gravity` is already the field that tells a
           grenade from a bolt everywhere else in this file -- it decides
           bouncing, and scene.c reads the same field to decide which colour of
           light the round carries -- and a smoke ribbon is a grenade's fact.
           The rapid's bolts leave at 70 m/s on an 85ms cooldown, so a trail on
           them would be a dozen emitters in the air at once laying smoke the
           player is about to walk through, for a weapon whose whole read is
           that the stream is clean.
           AFTER the early-out above, which is what keeps a grenade that has
           come to rest from stacking its whole fuse into one puff: a round
           that has not moved this frame does not reach this line.
           *포물선을 그리는 것만입니다.* `gravity`는 이 파일의 다른 모든 곳에서 이미 유탄과
           탄을 가르는 필드이며(튕김을 결정하고, scene.c도 어느 색 빛을 지니는지 정하려고
           같은 필드를 읽습니다) 연기 리본은 유탄의 사실입니다. 연사의 탄은 85ms 간격으로
           70m/s로 떠나므로, 그것에 궤적을 달면 방출기 열둘이 동시에 공중에 떠서 플레이어가
           곧 지나갈 자리에 연기를 깔게 됩니다. 그 무기의 인상 전부가 흐름이 깨끗하다는
           것인데 말입니다.
           위의 조기 탈출 *뒤*이며, 그것이 멈춰 선 유탄이 도화선 전체를 한 퍼프에 쌓지 않게
           합니다. 이번 프레임에 움직이지 않은 탄은 이 줄에 닿지 않습니다. */
        if (p->gravity > 0.0f) {
            p->trail_t -= dt;
            if (p->trail_t <= 0.0f) {
                p->trail_t = PROJ_TRAIL_INTERVAL;
                v3 back = v3scale(dir, -1.0f);
                fx_spawn(pl, "fusespark", p->pos, back);
                fx_spawn(pl, "fusetrail", p->pos, back);
            }
        }

        /* --- monsters first, along the whole step ------------------------
           Swept, because at 70 m/s a bolt crosses more than a metre in a frame
           and a monster standing between this frame's position and the next
           would otherwise be passed straight through.
           한 스텝 전체에 걸쳐 훑습니다. 70m/s의 탄은 한 프레임에 1미터 이상 이동하므로,
           이번 프레임 위치와 다음 위치 사이에 선 몬스터를 그대로 통과하게 됩니다. */
        float et; int eidx;
        if (enemy_hitscan(pl, p->pos, dir, dist + PROJ_RADIUS, &et, &eidx)) {
            v3 at = v3add(p->pos, v3scale(dir, et));
            if (p->blast <= 0.0f) enemy_hurt(pl, eidx, p->damage, dir);
            /* FLESH, and the only caller that can say so. The trace that found
               the monster is here; ::detonate is handed a point and a normal
               and could not tell a brute from a brick wall.
               *살*이며, 그렇게 말할 수 있는 유일한 호출자입니다. 몬스터를 찾아낸 판정이 이곳에
               있습니다. ::detonate는 점과 법선만 건네받으므로 우락부락한 놈과 벽돌담을 구분할
               수 없습니다. */
            detonate(pl, p, at, v3scale(dir, -1.0f), 1);
            continue;
        }

        /* --- then the level ---------------------------------------------- */
        float t; v3 n;
        if (level_trace(l, p->pos, dir, dist + PROJ_RADIUS, &t, &n)) {
            v3 at = v3add(p->pos, v3scale(dir, t));

            /* A bolt stops at the wall; a grenade bounces off it. `gravity` is
               what tells them apart -- see Proj.
               탄은 벽에서 멈추고 유탄은 튕깁니다. 둘을 가르는 것은 `gravity`입니다. */
            if (p->gravity <= 0.0f) { detonate(pl, p, at, n, 0); continue; }

            /* Reflect, damped. Backed off along the normal so the grenade does
               not begin the next step inside the surface it just left, which
               reports an immediate hit at zero range and pins it to the wall.
               감쇠된 반사입니다. 법선 방향으로 약간 물러나게 하여, 유탄이 방금 떠난 표면
               안에서 다음 스텝을 시작하지 않게 합니다. 그 경우 거리 0에서 즉시 충돌이
               보고되어 벽에 붙어 버립니다. */
            float vn = v3dot(p->vel, n);
            p->vel = v3sub(p->vel, v3scale(n, 2.0f * vn));
            p->vel = v3scale(p->vel, PROJ_BOUNCE);
            p->pos = v3add(at, v3scale(n, PROJ_RADIUS));

            /* Below a crawl it has stopped, and a grenade that keeps micro-
               bouncing rattles for the rest of its fuse.
               기어가는 수준 이하이면 멈춘 것입니다. 계속 미세하게 튕기는 유탄은 남은
               도화선 내내 덜그럭거립니다. */
            if (v3len(p->vel) < 1.2f) p->vel = v3f(0, 0, 0);
            else audio_play("impact", 25);
            continue;
        }

        p->pos = v3add(p->pos, step);
    }
}
