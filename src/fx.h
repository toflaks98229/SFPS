/**
 * @file fx.h
 * @brief Data-driven particle effects: spawned by name, defined in assets\effects.txt.
 *
 * ENGLISH
 * -------
 * Every world effect in this project used to be hand-written C: a struct, a
 * ring buffer, an ageing loop, a build loop and a draw loop, repeated per
 * effect. Three of them existed -- bullet holes, impact sparks and tracers --
 * and adding a fourth meant writing all five pieces again. This module is
 * those five pieces written once, with the parameters moved into text.
 *
 * The shape they all shared is what the format encodes: spawn N particles at a
 * point, give each a velocity, age them, and draw each as a camera-facing quad
 * whose colour and size follow its remaining life.
 *
 *   e spark              name it
 *     count 6            particles per spawn
 *     life 220           milliseconds each lives
 *     size 12 2          size at birth and at death, in cm
 *     rgb 255 216 128    colour at birth
 *     rgb2 180 60 20     colour at death; omit to hold the birth colour
 *     alpha 90 0         opacity at birth and at death, percent
 *     speed 180 40       initial speed along the spawn normal, cm/s, +/- spread
 *     spawn 8            radius the burst starts from, cm -- see below
 *     drag 40            per-second speed loss, percent
 *     spin 300           degrees per second each particle rolls
 *     gravity 0          cm/s^2 downward
 *     blend add          `add` for glows, `alpha` for decals and smoke
 *     face camera        `camera` billboards, `normal` lies flat on the surface
 *
 * `spawn` is the one that most changes how a burst reads. Every particle
 * starting from a single point makes them overlap for the first frames, and
 * overlapping additive quads saturate almost immediately -- two at 90% alpha
 * already composite to 99.7%, so a twelve-particle burst is one white blob
 * rather than twelve sparks. Starting them spread over a small radius is what
 * separates them into something countable.
 *
 * `spawn`이 폭발의 인상을 가장 크게 좌우합니다. 모든 입자가 한 점에서 출발하면 처음 몇
 * 프레임 동안 서로 겹치는데, 겹친 가산 블렌드 사각형은 거의 즉시 포화됩니다. 알파 90%
 * 두 장이면 이미 99.7%가 되므로, 입자 12개짜리 폭발이 12개의 불꽃이 아니라 하나의 흰
 * 얼룩이 됩니다. 작은 반경에 흩어져 시작하게 하는 것이 이들을 셀 수 있는 무언가로
 * 분리해 줍니다.
 *
 * Integers throughout, the same as every other asset language here: the parser
 * needs no float handling, and the text compresses well.
 *
 * @note Hot reload works: edit the file and the running game re-parses it.
 *       Live particles are dropped on a reload rather than migrated, because a
 *       particle holds an index into the definition table and the table is
 *       rewritten in place.
 * @note This owns WORLD effects only. Screen effects live in post.c, because
 *       they are a property of the resolve pass rather than of anything in the
 *       scene -- ::post_set_scanline and ::post_set_dither are that side of the
 *       line, and neither takes a position because neither has one.
 *
 * 한국어
 * ------
 * 이 프로젝트의 모든 월드 이펙트는 손으로 쓴 C였습니다. 구조체, 링 버퍼, 노화 루프,
 * 생성 루프, 드로우 루프를 이펙트마다 반복했습니다. 그런 것이 셋(탄흔, 피격 스파크,
 * 예광탄) 있었고, 네 번째를 추가하려면 다섯 조각을 전부 다시 써야 했습니다. 이 모듈은
 * 그 다섯 조각을 한 번만 작성하고 매개변수를 텍스트로 옮긴 것입니다.
 *
 * 셋이 공유하던 형태가 곧 이 형식이 표현하는 것입니다. 한 지점에 N개의 입자를 생성하고,
 * 각각에 속도를 주고, 나이를 먹이고, 남은 수명에 따라 색과 크기가 변하는 카메라 지향
 * 사각형으로 그립니다.
 *
 * 다른 모든 애셋 언어와 마찬가지로 전부 정수입니다. 파서가 부동소수점을 다룰 필요가
 * 없고 텍스트도 잘 압축됩니다.
 *
 * @note 핫 리로드가 동작합니다. 파일을 수정하면 실행 중인 게임이 다시 파싱합니다.
 *       리로드 시 살아 있는 입자는 이전되지 않고 버려집니다. 입자가 정의 테이블의
 *       인덱스를 들고 있는데 그 테이블이 제자리에서 재작성되기 때문입니다.
 * @note 이 모듈은 *월드* 이펙트만 소유합니다. 화면 이펙트는 post.c에 있습니다. 그것은
 *       장면 속 무언가의 속성이 아니라 해상 패스의 속성이기 때문입니다. ::post_set_scanline과
 *       ::post_set_dither가 경계의 저쪽이며, 둘 다 위치를 받지 않습니다. 가진 적이 없기
 *       때문입니다.
 */
#ifndef FX_H
#define FX_H

#include "m.h"

/* Forward-declared, pointer-only: this header must not drag the GL stack into
   whatever includes it, for the same reason level.h refuses render.h.
   포인터로만 쓰이므로 전방 선언합니다. level.h가 render.h를 거부하는 것과 같은 이유로,
   이 헤더는 자신을 포함하는 쪽에 GL 스택을 끌어들여서는 안 됩니다. */
typedef struct MeshBuf MeshBuf;

/* --- Capacity limits / 용량 제한 --- */

/* Raised from 16 when the blast became four layers and the saw got two of its
   own. A definition past the cap is DROPPED -- reported through DIAG_FX_CAP,
   but dropped -- so the symptom is an effect that silently does nothing, and
   the ones that go missing are the ones added last, which are the ones nobody
   has looked at yet.
   폭발이 네 겹이 되고 톱이 자기 것 둘을 얻으면서 16에서 올렸습니다. 상한을 넘는 정의는
   *버려집니다*. DIAG_FX_CAP으로 보고되기는 하지만 버려지므로, 증상은 아무 일도 하지 않는
   이펙트이며, 사라지는 것은 가장 나중에 추가된 것, 즉 아직 아무도 보지 않은 것입니다. */
#define FX_MAX_DEFS      32   ///< @brief Effect definitions the text may hold. / 텍스트가 담을 수 있는 이펙트 정의 수.

/**
 * @brief Particles alive at once, across every effect.
 *
 * ENGLISH
 * -------
 * Sized against the worst case that can actually occur rather than against a
 * round number. The continuous emitters are what decide it: a caster bolt lays
 * a trail particle every SHOT_TRAIL_INTERVAL and each lives ~260ms, so one
 * bolt in flight holds about nine, and ENEMY_MAX_SHOTS of them is ~416 -- past
 * 256, at which point the trails alone would evict every impact, spark and
 * pickup burst in the level.
 *
 * 640 covers that with room for the one-shot effects firing on top of it. The
 * array is .bss, so it is zero-filled at load and costs nothing on disk -- the
 * same reasoning LVL_MAX_RANGES is sized by.
 *
 * 한국어
 * ------
 * @brief 모든 이펙트를 통틀어 동시에 살아 있는 입자 수입니다.
 *
 * 어림수가 아니라 실제로 발생 가능한 최악의 경우를 기준으로 정했습니다. 이를 결정하는
 * 것은 지속적으로 방출하는 쪽입니다. 캐스터의 볼트는 SHOT_TRAIL_INTERVAL마다 궤적
 * 입자를 남기고 각각 약 260ms를 살므로, 비행 중인 볼트 하나가 약 9개를 유지하며
 * ENEMY_MAX_SHOTS개면 약 416개가 됩니다. 256을 넘으며, 그 시점에는 궤적만으로 레벨의
 * 모든 피격·스파크·획득 효과가 밀려납니다.
 *
 * 640은 그 위에 일회성 이펙트가 겹쳐 발생하는 것까지 감당합니다. 이 배열은 .bss이므로
 * 로드 시 0으로 채워지며 디스크 용량을 소모하지 않습니다. LVL_MAX_RANGES의 크기를 정한
 * 것과 동일한 근거입니다.
 */
/* Raised from 640: one layered blast is 84 particles, and the pool evicts the
   OLDEST, so two explosions close together used to eat the first one's dome
   while it was still expanding -- which removes exactly the thing the dome is
   there to show. .bss, so it is zeroed at load and costs the floppy nothing.
   640에서 올렸습니다. 여러 겹의 폭발 하나가 84개이고 풀은 *가장 오래된* 것을 밀어내므로,
   가까이서 두 번 터지면 첫 번째의 돔이 아직 팽창하는 중에 잡아먹혔습니다. 돔이 보여
   주려던 바로 그것이 사라집니다. .bss이므로 로드 시 0으로 채워지고 플로피 용량은 들지
   않습니다. */
#define FX_MAX_PARTICLES 1536
#define FX_NAME_LEN      16   ///< @brief Longest effect name. / 이펙트 이름의 최대 길이.

/**
 * @struct FxParticle
 * @brief One live particle.
 *
 * ENGLISH
 * -------
 * @note `def` is an INDEX rather than a pointer. A pointer would dangle across
 *       a hot reload, which rewrites the definition table in place; an index
 *       at least stays in range, and ::fx_reload clears the particles anyway.
 *
 * 한국어
 * ------
 * @note `def`는 포인터가 아니라 *인덱스*입니다. 포인터는 정의 테이블을 제자리에서
 *       재작성하는 핫 리로드를 거치며 무효가 되지만, 인덱스는 최소한 범위 안에
 *       머무릅니다. 어차피 ::fx_reload가 입자를 정리합니다.
 */
typedef struct {
    v3    pos, vel;
    v3    axis;       /**< Surface normal, for FX_FACE_NORMAL. / FX_FACE_NORMAL용 표면 법선. */
    float life;       /**< Seconds remaining. 0 means the slot is free. / 남은 시간(초). 0이면 빈 슬롯. */
    float life_max;   /**< What it started with, for the 0..1 fade. / 시작값. 0..1 페이드에 사용됩니다. */
    /**
     * @brief Roll angle, radians. Randomised at birth, advanced by `spin`.
     *
     * Per particle rather than per effect: a burst whose quads all sit at the
     * same angle reads as one shape being scaled, because the square edges
     * line up. Giving each its own starting roll is most of what makes a dozen
     * billboards look like a dozen things.
     *
     * 이펙트 단위가 아니라 입자 단위입니다. 모든 사각형이 같은 각도로 놓인 폭발은 정사각형
     * 모서리가 서로 맞아떨어지므로 하나의 형태가 커지는 것처럼 보입니다. 각각에 고유한
     * 시작 회전각을 주는 것이, 빌보드 열두 개를 열두 개의 무언가로 보이게 만드는 핵심입니다.
     */
    float roll;
    short def;
} FxParticle;

/**
 * @struct FxPool
 * @brief The particles a run has spawned, owned by the caller.
 *
 * A THIRD of this module's state, not all of it. fx.c also holds the parsed
 * `effects.txt` -- the definitions every effect is spawned FROM -- and the
 * vertex buffer they are drawn with. Neither moved:
 *
 *   the definitions   authored data, parsed once, read-only afterwards. Like
 *                     the texture cache: one per process, and a second World
 *                     would want the same effects, not different ones.
 *   the buffer        one MeshBuf and one Mesh, the same as decal.c's. Two
 *                     runs mean two sets of sparks, not two GPUs.
 *
 * The RNG moved. It seeds every particle's direction and roll, so leaving it
 * shared would make one World's bursts depend on how many the other had
 * spawned -- which is exactly the kind of coupling this whole exercise is
 * removing. ::RunState already keeps its own `smoke_rng` for the same reason.
 *
 * 이 모듈 상태의 *3분의 1*이며 전부가 아닙니다. fx.c는 파싱된 `effects.txt`(모든 이펙트가
 * 그것으로부터 생성되는 정의들)와 그것을 그리는 정점 버퍼도 보유합니다. 둘 다 옮기지
 * 않았습니다.
 *
 *   정의   제작된 데이터이며 한 번 파싱되고 이후 읽기 전용입니다. 텍스처 캐시와 같습니다.
 *          프로세스당 하나이며, 두 번째 World는 다른 이펙트가 아니라 같은 이펙트를 원합니다.
 *   버퍼   MeshBuf 하나와 Mesh 하나로, decal.c의 것과 같습니다. 두 판의 플레이는 두 벌의
 *          불꽃을 뜻하지 두 개의 GPU를 뜻하지 않습니다.
 *
 * RNG는 옮겼습니다. 모든 입자의 방향과 회전각을 씨앗으로 삼으므로, 공유된 채로 두면 한
 * World의 폭발이 다른 World가 몇 개를 생성했는지에 의존하게 됩니다. 이 작업 전체가 제거하고
 * 있는 바로 그 종류의 결합입니다. ::RunState도 같은 이유로 자기 `smoke_rng`를 갖습니다.
 */
typedef struct {
    FxParticle parts[FX_MAX_PARTICLES];  /**< Slots; `life` 0 means free. / 슬롯. `life`가 0이면 비어 있습니다. */
    int        next;                     /**< Ring cursor: the oldest is overwritten. / 링 커서. 가장 오래된 것을 덮어씁니다. */
    unsigned   rng;                      /**< Particle randomness. 0 means "seed me". / 입자 난수. 0이면 "씨앗을 채워라"입니다. */
} FxPool;

/* The bundle that holds this pool. See proj.h for why the calls take it. */
typedef struct Pools Pools;


/* --- Public function prototypes / 공개 함수 프로토타입 --- */

/**
 * @brief Spawns the named effect at a point.
 *
 * ENGLISH
 * -------
 * @param[in] name   Effect name as written in the effect text.
 * @param[in] pos    Where to spawn, world units.
 * @param[in] normal Direction the particles are thrown along. For a surface
 *                   hit this is the surface normal; pass any unit vector for
 *                   an omnidirectional burst.
 * @note An unknown name spawns nothing and is not an error -- an effect can be
 *       referenced before it is authored, and a typo costs a missing puff
 *       rather than a crash.
 * @note Silently drops particles once ::FX_MAX_PARTICLES is reached, and
 *       reports it through DIAG_FX_CAP so the truncation is visible rather
 *       than merely invisible.
 * @warning Touches no GL. Safe to call from simulation code and from headless
 *          tools, which is what lets the spawn logic be tested without a
 *          context.
 *
 * 한국어
 * ------
 * @brief 지정된 이펙트를 한 지점에 생성합니다.
 * @param[in] name   이펙트 텍스트에 기록된 이펙트 이름.
 * @param[in] pos    생성 위치 (월드 단위).
 * @param[in] normal 입자가 던져지는 방향. 표면 충돌이면 표면 법선이며, 전방향 분출을
 *                   원하면 아무 단위 벡터나 전달하십시오.
 * @note 알 수 없는 이름은 아무것도 생성하지 않으며 오류도 아닙니다. 이펙트를 제작하기
 *       전에 참조할 수 있고, 오타의 대가는 충돌이 아니라 연기 한 줌이 빠지는 것입니다.
 * @note ::FX_MAX_PARTICLES에 도달하면 입자를 조용히 버리며, DIAG_FX_CAP으로 보고하여
 *       절단이 보이지 않는 채로 남지 않게 합니다.
 * @warning GL을 전혀 사용하지 않습니다. 시뮬레이션 코드와 헤드리스 도구에서 호출해도
 *          안전하며, 이 덕분에 생성 로직을 컨텍스트 없이 테스트할 수 있습니다.
 */
void fx_spawn(Pools *pl, const char *name, v3 pos, v3 normal);

/**
 * @brief Spawns an effect with its speeds multiplied.
 *
 * ENGLISH
 * -------
 * @param[in] name   The effect's name.
 * @param[in] pos    Where it happens.
 * @param[in] normal The surface normal, or the direction it travels.
 * @param[in] scale  Multiplies every particle's speed. 1 is the authored size.
 * @note Exists so an effect that has to MEASURE something can be authored once
 *       for a unit radius and then sized by the caller. The blast dome is the
 *       case: a grenade and the axe's slam have different radii, and a dome
 *       drawn at the wrong one is worse than no dome, because it states a
 *       gameplay number that is not true.
 * @note Speeds only, not sizes or lifetimes. A dome reaches `speed * life`, so
 *       scaling the speed alone moves the edge while leaving the particles the
 *       size the artist drew them -- scaling the size too would make a big
 *       blast look like a near one seen close up.
 *
 * 한국어
 * ------
 * @brief 속력에 배율을 곱해 이펙트를 생성합니다.
 * @param[in] scale 모든 입자의 속력에 곱해집니다. 1이 작성된 크기입니다.
 * @note 무언가를 *측정해야* 하는 이펙트를 단위 반경으로 한 번만 작성하고 호출자가 크기를
 *       정할 수 있도록 존재합니다. 폭발 돔이 그 경우입니다. 유탄과 도끼의 내려찍기는
 *       반경이 다르며, 틀린 반경으로 그린 돔은 돔이 없는 것보다 나쁩니다. 사실이 아닌
 *       게임플레이 수치를 말하기 때문입니다.
 * @note 크기나 수명이 아니라 속력만 조절합니다. 돔은 `speed * life`까지 도달하므로,
 *       속력만 조절하면 가장자리가 움직이되 입자는 작성된 크기 그대로 남습니다.
 */
void fx_spawn_scaled(Pools *pl, const char *name, v3 pos, v3 normal, float scale);

/**
 * @brief Advances every live particle one frame.
 *
 * ENGLISH
 * -------
 * @param[in] dt Timestep in seconds.
 * @note Pure simulation: integrates position, applies gravity, ages particles
 *       and retires the dead. Touches no GL.
 *
 * 한국어
 * ------
 * @brief 살아 있는 모든 입자를 한 프레임 진행시킵니다.
 * @param[in] dt 시간 간격 (초).
 * @note 순수 시뮬레이션입니다. 위치를 적분하고 중력을 적용하며 나이를 먹이고 죽은 것을
 *       회수합니다. GL을 사용하지 않습니다.
 */
void fx_update(Pools *pl, float dt);

/**
 * @brief Draws every live particle.
 *
 * ENGLISH
 * -------
 * @param[in] vp        Combined view-projection matrix.
 * @param[in] cam_right Camera right basis vector.
 * @param[in] cam_up    Camera up basis vector.
 * @note Belongs in the WORLD pass, before post_end -- particles are part of
 *       the scene and must be pixelised and dithered with it.
 * @note Draws the alpha-blended particles first and the additive ones second,
 *       so a glow lands on top of the smoke it came from rather than under it.
 * @warning Requires a current GL context.
 *
 * 한국어
 * ------
 * @brief 살아 있는 모든 입자를 그립니다.
 * @param[in] vp        뷰-투영 결합 행렬.
 * @param[in] cam_right 카메라의 우측 기저 벡터.
 * @param[in] cam_up    카메라의 상향 기저 벡터.
 * @note post_end 이전의 *월드* 패스에 속합니다. 입자는 장면의 일부이므로 장면과 함께
 *       픽셀화되고 디더링되어야 합니다.
 * @note 알파 블렌드 입자를 먼저, 가산 블렌드 입자를 나중에 그립니다. 그래야 발광이
 *       자신이 나온 연기 아래가 아니라 위에 놓입니다.
 * @warning 활성 GL 컨텍스트가 필요합니다.
 */
void fx_draw(const Pools *pl, mat4 vp, v3 cam_right, v3 cam_up);

/**
 * @brief Drops every live particle and forces the definitions to be re-read.
 *
 * ENGLISH
 * -------
 * @note Called by hot reload. Live particles hold an index into the definition
 *       table, and a reload rewrites that table in place, so they cannot be
 *       carried across -- one lost puff is a better trade than a particle
 *       reading a definition that has become something else.
 *
 * 한국어
 * ------
 * @brief 살아 있는 모든 입자를 버리고 정의를 다시 읽도록 강제합니다.
 * @note 핫 리로드가 호출합니다. 살아 있는 입자는 정의 테이블의 인덱스를 들고 있고
 *       리로드는 그 테이블을 제자리에서 재작성하므로, 입자를 그대로 넘길 수 없습니다.
 *       연기 한 줌을 잃는 편이, 입자가 다른 것으로 바뀐 정의를 읽는 것보다 낫습니다.
 */
void fx_reload(Pools *pl);

/**
 * @brief How many particles are currently alive. For tests and the debug HUD.
 *
 * 한국어
 * ------
 * @brief 현재 살아 있는 입자의 수입니다. 테스트와 디버그 HUD용입니다.
 */
int fx_live_count(const Pools *pl);

/**
 * @brief How many effect definitions the text supplied. For tests.
 *
 * 한국어
 * ------
 * @brief 텍스트가 제공한 이펙트 정의의 개수입니다. 테스트용입니다.
 */
int fx_def_count(void);

/**
 * @brief Mean height of every live particle, or 0 when none are alive.
 *
 * ENGLISH
 * -------
 * @return The average world y of the live particles.
 * @note Exists so a headless test can assert which WAY an effect moves.
 *       ::fx_live_count proves particles exist and ::fx_update advances them,
 *       but neither can tell a plume that rises from one that falls -- and for
 *       the lava smoke that direction is the entire effect. A sign flip on its
 *       gravity would leave every other check in tools/fxtest.c passing while
 *       smoke poured into the floor.
 * @note A mean rather than a single particle's position, because a burst
 *       scatters: one sample could go either way on the spread alone, where
 *       the average of a dozen cannot.
 * @warning Not a general accessor. It answers one question cheaply; anything
 *          that needs real per-particle data should not go through here.
 *
 * 한국어
 * ------
 * @brief 살아 있는 모든 입자의 평균 높이. 살아 있는 입자가 없으면 0입니다.
 * @return 살아 있는 입자들의 평균 월드 y 좌표.
 * @note 헤드리스 테스트가 이펙트가 어느 *방향으로* 움직이는지 단언할 수 있도록
 *       존재합니다. ::fx_live_count는 입자의 존재를, ::fx_update는 진행을 증명하지만,
 *       둘 다 올라가는 연기와 내려가는 연기를 구분하지 못합니다. 그리고 용암 연기에서는
 *       그 방향이 효과의 전부입니다. 중력 부호가 뒤집히면 연기가 바닥으로 쏟아지는데도
 *       tools/fxtest.c의 다른 모든 검사는 통과합니다.
 * @note 입자 하나의 위치가 아니라 평균인 이유는 폭발이 흩어지기 때문입니다. 표본 하나는
 *       산포만으로도 어느 쪽으로든 갈 수 있지만 열두 개의 평균은 그럴 수 없습니다.
 * @warning 범용 접근자가 아닙니다. 하나의 질문에 저렴하게 답할 뿐이며, 실제 입자별
 *          데이터가 필요한 쪽은 이 함수를 거쳐서는 안 됩니다.
 */
float fx_mean_height(const Pools *pl);

/**
 * @brief Mean distance from a point, and the near-to-far spread.
 *
 * ENGLISH
 * -------
 * @param[in]  origin Where the effect was spawned.
 * @param[out] mean   Mean distance of every live particle from `origin`.
 * @param[out] width  Farthest any particle got from the vertical axis
     *                    through `origin`. 0 if nothing is alive.
 * @note Exists because the blast dome makes a claim with a NUMBER in it -- that
 *       it stops where the damage stops -- and ::fx_mean_height cannot see it.
 *       A dome drawn at the wrong radius is worse than no dome, because it
 *       states a gameplay fact that is not true, and it would look perfectly
 *       convincing while doing so.
 * @note `width` is the half that proves it is a HEMISPHERE. The mean alone
 *       passes for every particle flying straight up in a column, which has
 *       the right average distance and shows the player no radius at all.
 *       Measured as distance from the axis rather than as a spread of radii,
 *       because a spread of radii cannot distinguish them: with one speed for
 *       every particle they sit at the same distance whichever way they go, so
 *       that number reads ~0 for a dome and a column alike.
 *
 * 한국어
 * ------
 * @brief 한 점으로부터의 평균 거리와 원근 편차.
 * @param[out] width  `origin`을 지나는 수직축에서 입자가 도달한 최대 거리.
 * @note 폭발 돔이 *수치*를 담은 주장(데미지가 멈추는 곳에서 멈춘다)을 하는데
 *       ::fx_mean_height로는 그것을 볼 수 없기 때문에 존재합니다. 틀린 반경으로 그린
 *       돔은 돔이 없는 것보다 나쁩니다. 사실이 아닌 게임플레이 정보를 말하면서도 완벽히
 *       그럴듯해 보이기 때문입니다.
 * @note `width`는 그것이 *반구*임을 증명하는 쪽입니다. 평균만으로는 모든 입자가 곧장
 *       위로 솟는 기둥도 통과합니다. 평균 거리는 맞지만 플레이어에게 반경을 전혀 보여
 *       주지 못합니다. 반지름의 편차가 아니라 축으로부터의 거리로 재는 이유는, 편차로는
 *       둘을 구분할 수 없기 때문입니다. 모든 입자의 속력이 같으면 어느 방향으로 가든 같은
 *       거리에 있으므로, 그 값은 돔에서도 기둥에서도 ~0입니다.
 */
void fx_radius_spread(const Pools *pl, v3 origin, float *mean, float *width);

#endif
