/**
 * @file decal.h
 * @brief Bullet holes, blood, the spark that announces a hit, and tracers.
 *
 * ENGLISH
 * -------
 * The marks a shot leaves behind, and the only thing in the game that
 * remembers a hit after the frame it happened in.
 *
 * This lived inside weapon.c, which is where it was born: the shotgun was the
 * only thing that could mark a wall, so its decals were part of it. They are
 * not. A decal is spawned by whatever landed the hit and drawn a frame or six
 * later by whoever is drawing the world, and weapon.c was holding both ends of
 * that plus the pools, the lifetimes, the ring cursors and two hundred lines of
 * GL in the middle -- which is how a 1,900 line file gets to be one.
 *
 * The seam it came out on is the one this module states: the simulation PUSHES
 * (::decal_hit, ::decal_tracer) and never asks what became of it, and the frame
 * DRAWS (::decal_draw) and never asks who put it there. That is the same shape
 * fx.h and audio.h already have, and for the same reason -- it is the only
 * arrangement where a headless test can drive the first half.
 *
 * @note Every ::decal_hit and ::decal_tracer is safe with no GL context and no
 *       ::decal_init. The pools are plain memory; only ::decal_draw needs the
 *       buffers, and it does nothing until it has them.
 *
 * 한국어
 * ------
 * 사격이 남기는 자국이며, 그것이 일어난 프레임 이후까지 명중을 기억하는 게임 내의 유일한
 * 것입니다.
 *
 * 이것은 weapon.c 안에 있었고, 그곳이 태어난 자리입니다. 샷건이 벽에 자국을 낼 수 있는
 * 유일한 것이었으므로 그 자국은 샷건의 일부였습니다. 그렇지 않습니다. 데칼은 명중시킨 무엇이든
 * 생성하고 한 프레임에서 여섯 프레임 뒤에 월드를 그리는 누군가가 그립니다. weapon.c는 그
 * 양쪽 끝에 더해 풀과 수명과 링 커서와 그 사이의 GL 200줄을 함께 들고 있었습니다. 1,900줄짜리
 * 파일은 그렇게 만들어집니다.
 *
 * 떼어 낸 이음매는 이 모듈이 명시하는 바로 그것입니다. 시뮬레이션은 *밀어 넣고*
 * (::decal_hit, ::decal_tracer) 그것이 어떻게 되었는지 결코 묻지 않으며, 프레임은 *그리고*
 * (::decal_draw) 누가 놓았는지 결코 묻지 않습니다. fx.h와 audio.h가 이미 가진 것과 같은
 * 형태이며 이유도 같습니다. 헤드리스 테스트가 앞쪽 절반을 구동할 수 있는 유일한 배치이기
 * 때문입니다.
 *
 * @note 모든 ::decal_hit과 ::decal_tracer는 GL 컨텍스트 없이도, ::decal_init 없이도
 *       안전합니다. 풀은 평범한 메모리이며, 버퍼가 필요한 것은 ::decal_draw뿐이고 그것은
 *       버퍼를 갖기 전까지 아무 일도 하지 않습니다.
 */
#ifndef DECAL_H
#define DECAL_H

#include "m.h"

/* --- Capacities / 용량 --- */

/**
 * @brief Wall and body marks held at once, oldest overwritten first.
 *
 * ENGLISH: Every trigger pull spawns one per pellet, so the ring holds a few
 * full blasts rather than a few shots.
 *
 * 한국어: 방아쇠를 당길 때마다 산탄 하나당 하나가 생성되므로, 링은 몇 발이 아니라 몇 번의
 * 일제사격을 담습니다.
 */
#define DECAL_MAX_MARKS   48

/** @brief Tracers held at once, oldest overwritten first. / 동시에 유지되는 예광탄 수. 오래된 것부터 덮어씁니다. */
#define DECAL_MAX_TRACERS 24

/* --- Lifetimes / 수명 --- */

/**
 * @brief Seconds a mark on a WALL lasts.
 *
 * ENGLISH: A wall does not move, and the accumulated evidence of a firefight
 * along a corridor is most of what makes it feel like there was one.
 *
 * 한국어: 벽은 움직이지 않으며, 복도에 쌓인 교전의 흔적이 곧 그 교전이 있었다는 느낌의
 * 대부분입니다.
 */
#define DECAL_WALL_LIFE  6.0f

/**
 * @brief Seconds a mark on a MONSTER lasts.
 *
 * ENGLISH: It cannot last, because monsters move. A decal sits at the world
 * position of the impact and does not follow what it hit, so a monster that
 * walks away leaves the stain hanging in the air behind it. Short enough to
 * read as part of the same hit, gone while the body is still under it.
 *
 * 한국어: 오래갈 수 없습니다. 몬스터가 움직이기 때문입니다. 데칼은 충돌 지점의 월드 좌표에
 * 놓이고 맞은 대상을 따라가지 않으므로, 걸어가 버린 몬스터는 얼룩을 허공에 남깁니다. 같은
 * 피격의 일부로 읽힐 만큼 짧고, 몸이 아직 그 아래에 있는 동안 사라집니다.
 */
#define DECAL_BLOOD_LIFE 0.55f

/** @brief Seconds a tracer line lasts. / 예광탄 선의 수명 (초). */
#define DECAL_TRACER_LIFE 0.055f

/**
 * @brief Seconds the additive flare on a fresh mark lasts.
 *
 * ENGLISH: A dark bullet hole 30m down a fogged corridor is invisible on its
 * own -- the spark is what tells the player they connected.
 *
 * 한국어: 안개 낀 복도 30m 앞의 어두운 탄흔은 그 자체로는 보이지 않습니다. 명중했다는 것을
 * 플레이어에게 알리는 것은 스파크입니다.
 */
#define DECAL_SPARK_TIME 0.06f

/* --- Types / 타입 --- */

/**
 * @struct DecalPlace
 * @brief Where a mark was actually put, returned so effects can agree with it.
 *
 * ENGLISH
 * -------
 * A hit is drawn twice: once as this module's own mark, and once as whatever
 * `assets\effects.txt` says that surface throws off. Both have to happen at the
 * same point, and the point is not the raw impact -- blood is pulled back
 * toward the shooter and a wall mark is nudged off the surface so it wins the
 * depth test. That offset is a decal rule, so ::decal_hit owns it and reports
 * where it landed rather than making the caller reproduce the arithmetic.
 *
 * 한국어
 * ------
 * @brief 자국이 실제로 놓인 자리이며, 이펙트가 그것과 일치할 수 있도록 반환됩니다.
 *
 * 한 번의 명중은 두 번 그려집니다. 한 번은 이 모듈 자신의 자국으로, 한 번은
 * `assets\effects.txt`가 그 표면이 무엇을 튀긴다고 말하든 그것으로. 둘은 같은 지점에서
 * 일어나야 하며, 그 지점은 원래의 충돌 지점이 아닙니다. 피는 사수 쪽으로 당겨지고 벽의
 * 자국은 깊이 테스트에서 이기도록 표면에서 살짝 띄워집니다. 그 오프셋은 데칼의 규칙이므로
 * ::decal_hit이 그것을 소유하고, 호출자가 같은 산술을 재현하게 하는 대신 어디에 놓였는지
 * 알려 줍니다.
 */
typedef struct {
    v3 p;   /**< Where the mark sits. / 자국이 놓인 자리. */
    v3 n;   /**< Which way it faces. / 자국이 향하는 방향. */
} DecalPlace;

/* --- What a run leaves behind / 플레이가 남기는 것 --- */

/**
 * @struct Mark
 * @brief One bullet hole or blood splat left on a surface.
 *
 * ENGLISH
 * -------
 * @note `blood` selects the material: a hit on a monster splatters rather than
 *       chipping the wall. It also selects the LIFETIME, which is the reason
 *       the field is stored rather than re-derived at draw time -- see the
 *       fades below.
 *
 * 한국어
 * ------
 * 표면에 남은 탄흔 또는 혈흔 하나입니다.
 * @note `blood`가 재질을 결정합니다. 몬스터에 명중하면 벽이 파이는 대신 피가 튑니다. 또한
 *       *수명*도 결정하며, 이 필드를 그리는 시점에 다시 유도하지 않고 저장해 두는 이유가
 *       그것입니다. 아래의 페이드를 참조하십시오.
 */
typedef struct { v3 p, n; float life; int blood; } Mark;

/**
 * @struct Tracer
 * @brief One short-lived line from the muzzle to a pellet's impact point.
 * / 총구에서 산탄이 명중한 지점까지 이어지는 짧은 수명의 선 하나입니다.
 */
typedef struct { v3 a, b; float life; } Tracer;

/**
 * @struct DecalPool
 * @brief The marks and tracers a run has left, owned by the caller.
 *
 * ONLY HALF OF THIS MODULE'S STATE IS HERE, and the split is the point. decal.c
 * also holds two ::Mesh objects and the ::MeshBuf pair they are built from --
 * GL names and heap allocations made by ::decal_init and destroyed by
 * ::decal_free. Those stay where they are. They are one set of buffers per
 * PROCESS, like the texture cache or the offscreen target, and a second World
 * wants its own bullet holes but has no use for a second vertex buffer to draw
 * them with.
 *
 * The rule that sorts them: would a second ::World want a second one? Two runs
 * mean two sets of marks. They do not mean two GPUs.
 *
 * ::Mark and ::Tracer moved up here from decal.c to make this possible, which
 * is the price of owning a pool by value rather than by handle. proj.h and
 * pickup.h publish their element types for the same reason.
 *
 * 이 모듈 상태의 *절반만* 이곳에 있으며, 그 분리가 요점입니다. decal.c는 ::Mesh 두 개와
 * 그것을 만드는 ::MeshBuf 쌍도 보유합니다. ::decal_init이 만들고 ::decal_free가 파괴하는 GL
 * 이름과 힙 할당입니다. 그것들은 있던 자리에 남습니다. 텍스처 캐시나 오프스크린 타깃처럼
 * *프로세스*당 한 벌이며, 두 번째 World는 자기 탄흔을 원하지 그것을 그릴 두 번째 정점 버퍼를
 * 원하지 않습니다.
 *
 * 가르는 기준: 두 번째 ::World가 두 번째 것을 원하는가? 두 판의 플레이는 두 벌의 자국을
 * 뜻하지만 두 개의 GPU를 뜻하지는 않습니다.
 *
 * 이것을 가능하게 하려고 ::Mark과 ::Tracer가 decal.c에서 이곳으로 올라왔습니다. 풀을 핸들이
 * 아니라 값으로 소유하는 대가입니다. proj.h와 pickup.h도 같은 이유로 원소 타입을
 * 공개합니다.
 */
typedef struct {
    Mark   marks[DECAL_MAX_MARKS];      /**< Bullet holes and blood. / 탄흔과 혈흔. */
    Tracer tracers[DECAL_MAX_TRACERS];  /**< The streaks a shot leaves. / 사격이 남기는 궤적. */
    int    mark_next;                   /**< Ring cursor: the oldest is overwritten. / 링 커서. 가장 오래된 것을 덮어씁니다. */
    int    tracer_next;                 /**< The same, for tracers. / 예광탄에 대해서도 같습니다. */
} DecalPool;

/* The bundle that holds this pool. See proj.h for why the calls take it. */
typedef struct Pools Pools;


/* --- API --- */

/**
 * @brief Sets up the drawing buffers. Pairs with ::decal_free.
 *
 * ENGLISH
 * -------
 * @note Needed only for ::decal_draw. Spawning and ageing work without it, so
 *       a headless test drives this module by simply not calling it.
 *
 * 한국어
 * ------
 * @brief 그리기 버퍼를 준비합니다. ::decal_free와 짝을 이룹니다.
 * @note ::decal_draw에만 필요합니다. 생성과 노화는 이것 없이 동작하므로, 헤드리스 테스트는
 *       그냥 호출하지 않는 것으로 이 모듈을 구동합니다.
 */
void decal_init(void);

/** @brief Releases what ::decal_init took. / ::decal_init이 취한 것을 해제합니다. */
void decal_free(void);

/**
 * @brief Clears every mark and tracer.
 *
 * ENGLISH
 * -------
 * @note For a level change: a hole belongs to the wall it was shot into, and
 *       that wall no longer exists.
 *
 * 한국어
 * ------
 * @brief 모든 자국과 예광탄을 지웁니다.
 * @note 레벨 전환용입니다. 구멍은 그것이 박힌 벽에 속하며, 그 벽은 더 이상 존재하지 않습니다.
 */
void decal_reset(Pools *pl);

/**
 * @brief Leaves a mark where something was hit.
 *
 * ENGLISH
 * -------
 * @param[in] end    The impact point, unoffset.
 * @param[in] dir    The direction the shot was travelling.
 * @param[in] surf_n The surface normal at the impact. Ignored when `blood`.
 * @param[in] blood  Non-zero if the thing hit was a monster.
 * @return Where the mark was placed, for effects that must agree with it.
 *
 * 한국어
 * ------
 * @brief 무언가가 맞은 자리에 자국을 남깁니다.
 * @param[in] end    오프셋이 적용되지 않은 충돌 지점.
 * @param[in] dir    사격이 진행하던 방향.
 * @param[in] surf_n 충돌 지점의 표면 법선. `blood`일 때는 무시됩니다.
 * @param[in] blood  맞은 대상이 몬스터이면 0이 아닙니다.
 * @return 자국이 놓인 자리. 그것과 일치해야 하는 이펙트를 위한 값입니다.
 */
DecalPlace decal_hit(Pools *pl, v3 end, v3 dir, v3 surf_n, int blood);

/**
 * @brief Leaves a tracer line from a muzzle to where the shot ended.
 *
 * ENGLISH
 * -------
 * @param[in] from The muzzle.
 * @param[in] to   Where the shot stopped, hit or not.
 *
 * 한국어
 * ------
 * @brief 총구에서 사격이 끝난 지점까지 예광탄 선을 남깁니다.
 * @param[in] from 총구.
 * @param[in] to   명중 여부와 무관하게 사격이 멈춘 지점.
 */
void decal_tracer(Pools *pl, v3 from, v3 to);

/**
 * @brief Ages every mark and tracer, retiring the ones that run out.
 *
 * ENGLISH
 * -------
 * @param[in] dt Timestep in seconds.
 * @note Called with the world's dt, so marks freeze when the world does -- a
 *       tracer hanging in the air behind a pause menu says the game is still
 *       running when it is not.
 *
 * 한국어
 * ------
 * @brief 모든 자국과 예광탄을 노화시키고, 수명이 다한 것을 회수합니다.
 * @param[in] dt 시간 간격 (초).
 * @note 월드의 dt로 호출되므로 월드가 멈추면 자국도 멈춥니다. 일시정지 메뉴 뒤에 공중에 멈춘
 *       예광탄은 게임이 멈춰 있는데도 돌아가고 있다고 말하는 셈입니다.
 */
void decal_update(Pools *pl, float dt);

/**
 * @brief Draws the marks, their sparks and the tracers.
 *
 * ENGLISH
 * -------
 * @param[in] view_proj The world camera.
 * @param[in] cam_pos   Camera position; the sparks scale with distance from it.
 * @param[in] cam_right Screen right, from ::cam_basis.
 * @param[in] cam_up    Screen up, from ::cam_basis.
 *
 * @note Belongs on the WORLD side of the pass boundary -- these are part of the
 *       scene's lighting and must be pixelised and dithered with it. Expects
 *       face culling to be off; the marks are billboards with one winding.
 * @note A no-op before ::decal_init.
 *
 * 한국어
 * ------
 * @brief 자국과 그 스파크, 그리고 예광탄을 그립니다.
 * @param[in] view_proj 월드 카메라.
 * @param[in] cam_pos   카메라 위치. 스파크가 이 지점으로부터의 거리에 따라 크기를 조정합니다.
 * @param[in] cam_right ::cam_basis가 만든 화면 우측 축.
 * @param[in] cam_up    ::cam_basis가 만든 화면 상향 축.
 *
 * @note 패스 경계의 *월드* 쪽에 속합니다. 이것들은 장면 조명의 일부이며 함께 픽셀화되고
 *       디더링되어야 합니다. 면 컬링이 꺼져 있어야 합니다. 자국은 감김 방향이 하나인
 *       빌보드입니다.
 * @note ::decal_init 이전에는 아무 동작도 하지 않습니다.
 */
void decal_draw(const Pools *pl, mat4 view_proj, v3 cam_pos, v3 cam_right, v3 cam_up);

/**
 * @brief How many marks are still alive.
 *
 * ENGLISH
 * -------
 * @return 0 to ::DECAL_MAX_MARKS.
 * @note The same shape as ::enemy_alive, and here for the same reason: a pool
 *       with no way to ask what is in it can only be checked by looking at the
 *       screen. Ageing, expiry and the ring wrapping round are all observable
 *       through this and nothing else -- ::decal_hit reporting where it put a
 *       mark covers placement, and between them a headless test can drive the
 *       whole module.
 *
 * 한국어
 * ------
 * @brief 아직 살아 있는 자국의 수입니다.
 * @return 0에서 ::DECAL_MAX_MARKS 사이.
 * @note ::enemy_alive와 같은 형태이며 이유도 같습니다. 안에 무엇이 있는지 물을 방법이 없는
 *       풀은 화면을 보는 것으로만 확인할 수 있습니다. 노화, 소멸, 링이 한 바퀴 도는 것은
 *       모두 오직 이것을 통해서만 관측됩니다. 자국이 놓인 자리는 ::decal_hit이 보고하므로,
 *       둘을 합치면 헤드리스 테스트가 이 모듈 전체를 구동할 수 있습니다.
 */
int decal_live_marks(const Pools *pl);

/** @brief How many tracers are still alive. / 아직 살아 있는 예광탄의 수. */
int decal_live_tracers(const Pools *pl);

#endif
