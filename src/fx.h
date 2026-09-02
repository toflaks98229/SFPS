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
 *     stretch 30         draw the quad over 30ms of its own travel, along the
 *                        velocity -- a streak instead of a square, and one
 *                        that shortens as `drag` takes the speed away
 *     trail emberwake    the effect it leaves BEHIND it as it flies
 *     trailms 60         and how often, in milliseconds
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
   own, and from 40 when the blast became NINE. A definition past the cap is
   DROPPED -- reported through DIAG_FX_CAP, but dropped -- so the symptom is an
   effect that silently does nothing, and the ones that go missing are the ones
   added last, which are the ones nobody has looked at yet.

   The file holds 44. Five of headroom was the margin before the flash, the
   ground wave and the embers were written, and the layer that keeps eating it
   is always the same one: an explosion has grown from four definitions to
   ELEVEN over the life of this file, because each new question it has to
   answer -- how big, how hot, how far, how long ago, and now what it looks
   like while it is still burning -- costs a layer. The array is .bss, so the
   eight added here are eight that the floppy never sees.

   THE NINE ADDED LAST WERE NOT ALL THE BLAST'S, and only two of them were.
   The fireball and the lit half of the ground rim belong to the pattern above.
   Four more are what a projectile leaves BEHIND it -- `fusespark`,
   `fusetrail`, `boltwake` and `emberwake` -- and three are what one leaves
   WHERE IT LANDS: `scorch`, `zapflash` and `zapburst`. Those are two further
   kinds of growth this cap has to absorb, and neither is bounded by how many
   explosions the game has. A trail is a definition per emitter and a landing
   is a definition per weapon per surface; there are more of both than there
   are ways for a grenade to go off.

   그리고 *마지막에 더해진 아홉 중 폭발의 것은 둘뿐이었습니다.* 화구와 지면 테두리의 빛나는
   절반이 위의 패턴에 속합니다. 넷은 발사체가 *뒤에* 남기는 것이고(`fusespark`, `fusetrail`,
   `boltwake`, `emberwake`), 셋은 발사체가 *착탄한 자리에* 남기는 것입니다(`scorch`,
   `zapflash`, `zapburst`). 이 캡이 흡수해야 하는 성장의 종류가 둘 더 생긴 것이며, 어느 쪽도
   이 게임에 폭발이 몇 가지인가로 한계 지어지지 않습니다. 자취는 방출기당 정의 하나이고
   착탄은 무기당 표면당 정의 하나입니다. 둘 다 유탄이 터지는 방식의 가짓수보다 많습니다.

   폭발이 네 겹이 되고 톱이 자기 것 둘을 얻으면서 16에서 올렸고, 폭발이 *아홉* 겹이 되면서
   40에서 올렸습니다. 상한을 넘는 정의는 *버려집니다*. DIAG_FX_CAP으로 보고되기는 하지만
   버려지므로, 증상은 아무 일도 하지 않는 이펙트이며, 사라지는 것은 가장 나중에 추가된 것,
   즉 아직 아무도 보지 않은 것입니다.

   파일에는 35개가 있습니다. 섬광과 지면 파동과 불티가 작성되기 전의 여유가 5였고, 그
   여유를 계속 먹는 것은 언제나 같은 겹입니다. 폭발은 이 파일의 생애 동안 정의 넷에서 아홉으로
   자랐습니다. 그것이 답해야 하는 새 질문(얼마나 큰가, 얼마나 뜨거운가, 얼마나 먼가, 얼마나
   지났는가)마다 한 겹이 들기 때문입니다. 이 배열은 .bss이므로, 이곳에서 더한 여덟은 플로피가
   결코 보지 않는 여덟입니다. */
#define FX_MAX_DEFS      48   ///< @brief Effect definitions the text may hold. / 텍스트가 담을 수 있는 이펙트 정의 수.

/**
 * @brief Particles alive at once, across every effect.
 *
 * ENGLISH
 * -------
 * Sized against the worst case that can actually occur rather than against a
 * round number. The continuous emitters are what decide it, and a caster bolt
 * is now TWO of them: `bolttrail` every SHOT_TRAIL_INTERVAL at ~280ms is about
 * nine alive, `boltwake` on the same timer at 620ms is about twenty-one, so a
 * bolt in flight holds thirty and ENEMY_MAX_SHOTS of them is ~1440. Add the
 * 297 one grenade going off costs -- see the note under this comment -- and
 * about 110 for the rapid's landings, which is the third continuous
 * population: eleven hits a second, four layers each, and `scorch` lives 4.5
 * seconds so fifty of those alone are on the walls at any moment during
 * sustained fire. The worst frame this project can produce is a little under
 * 1850.
 *
 * 2048 covers that, with two hundred to spare rather than the comfortable
 * margin it started with -- and what the ring evicts first when it does run
 * out is the oldest scorch on a wall behind the player, which is exactly the
 * particle worth losing. The array is .bss, so it is zero-filled at load and
 * costs nothing on disk -- the same reasoning LVL_MAX_RANGES is sized by.
 *
 * 한국어
 * ------
 * @brief 모든 이펙트를 통틀어 동시에 살아 있는 입자 수입니다.
 *
 * 어림수가 아니라 실제로 발생 가능한 최악의 경우를 기준으로 정했습니다. 이를 결정하는
 * 것은 지속적으로 방출하는 쪽이며, 캐스터의 볼트는 이제 *둘*입니다. `bolttrail`은
 * SHOT_TRAIL_INTERVAL마다 남고 280ms를 살아 약 9개, `boltwake`는 같은 타이머로 620ms를
 * 살아 약 21개이므로, 비행 중인 볼트 하나가 30개를 유지하며 ENEMY_MAX_SHOTS개면 약
 * 1440개가 됩니다. 여기에 유탄 하나가 터질 때 드는 297(이 주석 아래의 설명을 참조하십시오)과,
 * 연사의 착탄에 드는 약 110을 더합니다. 그것이 세 번째 지속 인구입니다. 초당 열한 번 명중하고
 * 각각 네 겹이며, `scorch`는 4.5초를 살기에 지속 사격 중에는 그것만으로도 언제나 쉰 개가
 * 벽에 붙어 있습니다. 이 프로젝트가 만들어 낼 수 있는 최악의 프레임은 1850에 조금 못
 * 미칩니다.
 *
 * 2048이 그것을 감당합니다. 다만 출발할 때의 넉넉한 여유가 아니라 이백의 여유입니다. 그리고
 * 실제로 바닥났을 때 링이 가장 먼저 밀어내는 것은 플레이어 뒤쪽 벽의 가장 오래된 그을음이며,
 * 그것이야말로 잃어도 되는 입자입니다. 이 배열은 .bss이므로 로드 시 0으로 채워지며 디스크
 * 용량을 소모하지 않습니다. LVL_MAX_RANGES의 크기를 정한 것과 동일한 근거입니다.
 */
/* Raised from 640: a layered blast is a large fraction of the pool on its own,
   and the pool evicts the OLDEST, so two explosions close together used to eat
   the first one's dome while it was still expanding -- which removes exactly
   the thing the dome is there to show. .bss, so it is zeroed at load and costs
   the floppy nothing.

   THE NUMBER TO CHECK THIS AGAINST IS 297, which is what one grenade going off
   costs now: eleven layers spawning 213 particles at the instant, plus the 84
   that `blastember`'s fourteen embers keep in the air behind them for the next
   second and a half. The count is written down here because it is the one that
   moves -- it was 84 when the cap was 640, 126 before the flash, the wave and
   the embers, and 171 before the fireball, the lit rim and the ember wakes. A
   capacity argument that cites a stale number is a capacity argument nobody
   can check.

   RAISED FROM 1536 BECAUSE THAT LAST STEP WAS THE BIG ONE. At 171 a blast was
   a ninth of the pool and nine could overlap; at 297 it is a fifth and five
   can, which is inside the range a player reaches -- the launcher holds twenty
   and fires every 0.85 seconds, so three or four in the air at once is a
   volley rather than an edge case. The eviction is oldest-first and what it
   takes first is the dome of the blast before, which is the layer whose whole
   job is to say where the damage stopped.
   AND THE WAKES ARE THE OTHER HALF OF IT. `trail` made particles into emitters,
   so the pool now has a second continuous population on top of the bolts'
   trails: 84 per blast that did not exist, arriving over a second and a half
   rather than all at once, which is exactly the shape that quietly crowds out
   whatever was there first. .bss, so the 512 added here cost the floppy nothing
   and cost a running frame one more pass over slots that are mostly empty.

   *이 마지막 단계가 컸기에 1536에서 올렸습니다.* 171일 때 폭발 하나는 풀의 9분의 1이었고 아홉
   개가 겹칠 수 있었습니다. 297이면 5분의 1이고 다섯 개가 겹칠 수 있는데, 그것은 플레이어가
   실제로 도달하는 범위 안입니다. 발사기는 스무 발을 담고 0.85초마다 쏘므로, 서넛이 동시에
   공중에 있는 것은 예외적 상황이 아니라 일제 사격입니다. 축출은 오래된 것부터이며 가장 먼저
   가져가는 것은 직전 폭발의 돔인데, 그것은 피해가 어디서 멈췄는지 말하는 것이 임무의 전부인
   겹입니다.
   *그리고 자취가 나머지 절반입니다.* `trail`이 입자를 방출기로 만들었으므로, 이제 풀에는 볼트의
   궤적 위에 두 번째 지속 인구가 있습니다. 폭발당 84개이며 존재하지 않던 것이고, 한꺼번에가
   아니라 1.5초에 걸쳐 도착합니다. 먼저 있던 것을 조용히 밀어내는 것이 바로 그 형태입니다.
   .bss이므로 이곳에서 더한 512는 플로피에 아무 비용도 들지 않고, 실행 중인 프레임에는 대부분
   비어 있는 슬롯을 한 번 더 훑는 비용이 듭니다.

   640에서 올렸습니다. 여러 겹의 폭발 하나가 그 자체로 풀의 큰 몫을 차지하고 풀은 *가장
   오래된* 것을 밀어내므로, 가까이서 두 번 터지면 첫 번째의 돔이 아직 팽창하는 중에
   잡아먹혔습니다. 돔이 보여 주려던 바로 그것이 사라집니다. .bss이므로 로드 시 0으로 채워지고
   플로피 용량은 들지 않습니다.

   *이것을 검증할 수 있는 수는 171이며*, 이제 유탄 하나가 터질 때 드는 비용입니다. 아홉 겹이고,
   `blastember` 하나만으로도 다른 모든 것이 끝난 뒤 1.5초 동안 열네 슬롯을 붙들고 있습니다.
   이 수를 이곳에 적어 두는 이유는 그것이 *움직이는* 수이기 때문입니다. 이 상한을 정할 때는
   84였고 섬광과 파동과 불티가 추가되기 전에는 126이었는데, 늘어날 때마다 상한 옆의 설명은 옛
   숫자에 머물렀습니다. 낡은 수를 인용하는 용량 논거는 아무도 검증할 수 없는 용량 논거입니다. */
#define FX_MAX_PARTICLES 2048
#define FX_NAME_LEN      16   ///< @brief Longest effect name. / 이펙트 이름의 최대 길이.

/**
 * @struct FxTint
 * @brief The colour a spawn paints an effect in, or {0,0,0} for the authored one.
 *
 * ENGLISH
 * -------
 * A TINT RATHER THAN A SECOND SET OF DEFINITIONS. A blast draws in six layers,
 * so a second kind of blast authored the obvious way is `blastcore2` beside
 * `blastcore` and five more like it: six rows against a table capped at
 * ::FX_MAX_DEFS, six copies of every number that has nothing to do with colour,
 * and six places to edit the next time the SHAPE changes. Colour is the only
 * thing that differs between one explosion and another, so colour is the only
 * thing that travels with the spawn.
 *
 * ZERO IS "AS AUTHORED". That is what lets ::fx_spawn stay a call with no
 * colour in it, and it is what keeps pools.h's promise that a zeroed ::Pools is
 * a valid one: a particle that never went through a tinted spawn carries
 * {0,0,0} and is drawn in exactly the colours effects.txt gave it.
 *
 * A HUE, NOT A BRIGHTNESS. ::fx_tint_colour divides by the brightest channel,
 * so {255,104,26} and {128,52,13} are the same instruction. The ramp from birth
 * to death -- white-hot fading into the colour of what burned -- belongs to the
 * text, and a tint that could flatten or lift it would be a second author for
 * something that already has one.
 *
 * 한국어
 * ------
 * @brief 생성 시 이펙트를 칠할 색. {0,0,0}이면 텍스트가 작성한 색 그대로입니다.
 *
 * *두 번째 정의 묶음이 아니라 색조입니다.* 폭발은 여섯 겹으로 그려지므로, 두 번째 종류의
 * 폭발을 뻔한 방식으로 작성하면 `blastcore` 옆의 `blastcore2`와 그런 것 다섯이 더 생깁니다.
 * ::FX_MAX_DEFS로 제한된 표에 여섯 행, 색과 무관한 모든 수치의 사본 여섯, 그리고 다음에
 * *형태*가 바뀔 때 고쳐야 할 자리 여섯입니다. 폭발과 폭발 사이에서 다른 것은 색뿐이므로,
 * 생성과 함께 다니는 것도 색뿐입니다.
 *
 * *0은 "작성된 그대로"입니다.* 그래서 ::fx_spawn은 색이 들어가지 않는 호출로 남을 수 있고,
 * 0으로 채운 ::Pools가 유효하다는 pools.h의 약속도 지켜집니다. 색조 있는 생성을 거치지 않은
 * 입자는 {0,0,0}을 지니며 effects.txt가 준 색 그대로 그려집니다.
 *
 * *밝기가 아니라 색상입니다.* ::fx_tint_colour가 가장 밝은 채널로 나누므로 {255,104,26}과
 * {128,52,13}은 같은 지시입니다. 생성에서 소멸까지의 변화(흰 열기가 타 버린 것의 색으로
 * 식어 가는 것)는 텍스트의 것이며, 그것을 눕히거나 들어 올릴 수 있는 색조는 이미 작성자가
 * 있는 것에 대한 두 번째 작성자가 됩니다.
 */
typedef struct { unsigned char r, g, b; } FxTint;

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
    /**
     * @brief Seconds until this one emits its wake. Negative means never.
     *
     * ENGLISH
     * -------
     * Per particle rather than per effect, for the reason ::FxParticle::roll is:
     * fourteen embers spawned on one frame share a definition, and a timer that
     * lived on the definition would fire all fourteen wakes together, forever,
     * on the same frame. Started at a random fraction of the interval instead,
     * so the trail behind each is laid down out of step with the others.
     *
     * THE NEGATIVE IS LOAD-BEARING. A particle that is itself somebody's wake
     * gets one, and that is the only thing stopping a wake from having a wake:
     * ::spawn_def decides it once, at birth, and nothing reads the definition
     * table again to ask. See the note above ::spawn_def.
     *
     * 한국어
     * ------
     * 이펙트 단위가 아니라 입자 단위이며, ::FxParticle::roll이 그러한 것과 같은 이유입니다.
     * 한 프레임에 생성된 불티 열넷은 정의를 공유하므로, 타이머가 정의 쪽에 있었다면 열넷의
     * 자취가 언제까지나 같은 프레임에 함께 발화했을 것입니다. 대신 간격의 무작위 일부에서
     * 시작하므로, 각자의 뒤에 놓이는 궤적이 서로 어긋납니다.
     *
     * *음수가 하중을 받습니다.* 스스로 누군가의 자취인 입자가 음수를 받으며, 자취가 자기
     * 자취를 갖지 못하게 막는 것은 그것 하나입니다. ::spawn_def가 태어날 때 한 번
     * 결정하고, 그 뒤로 아무것도 정의 표에 다시 묻지 않습니다. ::spawn_def 위의 설명을
     * 참조하십시오.
     */
    float trail_t;
    short def;

    /**
     * @brief The colour this spawn asked for, or {0,0,0} for the authored one.
     *
     * ENGLISH: Per PARTICLE and not per definition, because two blasts of
     * different colours are alive at the same time more often than not -- a
     * grenade going off while the slam that threw the player is still burning
     * -- and the definition they share is one table entry. Three bytes, and
     * they land in the padding after `def`, so a particle costs what it did.
     *
     * 한국어: 정의별이 아니라 *입자별*입니다. 색이 다른 두 폭발이 동시에 살아 있는 경우가
     * 드물지 않고(플레이어를 던진 내려찍기가 아직 타는 동안 터지는 유탄), 그 둘이 공유하는
     * 정의는 표의 한 행이기 때문입니다. 3바이트이며 `def` 뒤의 패딩에 들어가므로 입자
     * 하나의 비용은 예전 그대로입니다.
     */
    unsigned char tint[3];
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
 * @brief Spawns a recipe as a horizontal ring segment of a given size.
 *
 * ENGLISH: For drawing a measurement -- a reach, a blast edge -- where the
 * number belongs to the caller and not to effects.txt. The recipe supplies the
 * look and this supplies the geometry; a radius written into both files is one
 * number in two places.
 * 한국어: 치수를 그리기 위한 것입니다. 닿는 거리나 폭발의 가장자리처럼, 그 수가 effects.txt가
 * 아니라 호출자의 것일 때 씁니다. 레시피는 생김새를, 이것은 기하를 줍니다. 두 파일에 적힌
 * 반지름은 한 수가 두 곳에 있는 것입니다.
 *
 * @param[in]     radius_m Distance from  the particles land at, metres.
 * @param[in]     half_deg Half-angle either side of , degrees.
 */
void fx_spawn_arc(Pools *pl, const char *name, v3 pos, v3 normal,
                  float radius_m, int half_deg);

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
 * @brief Spawns an effect scaled and painted in a colour of the caller's.
 *
 * ENGLISH
 * -------
 * @param[in] name   The effect's name.
 * @param[in] pos    Where it happens.
 * @param[in] normal The surface normal, or the direction it travels.
 * @param[in] scale  Multiplies every particle's speed. 1 is the authored size.
 * @param[in] tint   The colour, or {0,0,0} to keep the one effects.txt wrote.
 *
 * @note THE OTHER TWO SPAWNS ARE THIS ONE. ::fx_spawn is this with a scale of 1
 *       and no tint, ::fx_spawn_scaled is this with no tint; neither has a body
 *       of its own, so there is one spawn path and the eviction, the diagnostic
 *       and the dome cannot come apart between three copies of it.
 * @note What the tint may NOT do is say how bright or how long -- see ::FxTint.
 *       A source that wants a bigger blast passes a bigger `scale`, and one that
 *       wants a different shape authors a different effect.
 *
 * 한국어
 * ------
 * @brief 배율과 호출자가 정한 색으로 이펙트를 생성합니다.
 * @param[in] tint 색. {0,0,0}이면 effects.txt가 쓴 색을 유지합니다.
 * @note *나머지 두 생성 함수가 곧 이 함수입니다.* ::fx_spawn은 배율 1에 색조 없음이고
 *       ::fx_spawn_scaled는 색조 없음입니다. 둘 다 자기 본문을 갖지 않으므로 생성 경로는
 *       하나이며, 축출과 진단과 돔이 사본 셋 사이에서 어긋날 수 없습니다.
 * @note 색조가 말할 수 *없는* 것은 얼마나 밝은지와 얼마나 오래인지입니다(::FxTint 참조).
 *       더 큰 폭발을 원하는 쪽은 더 큰 `scale`을 넘기고, 다른 형태를 원하는 쪽은 다른
 *       이펙트를 작성합니다.
 */
void fx_spawn_tinted(Pools *pl, const char *name, v3 pos, v3 normal, float scale,
                     FxTint tint);

/**
 * @brief Recolours one authored colour with a tint, in place.
 *
 * ENGLISH
 * -------
 * @param[in]     tint The colour asked for; {0,0,0} leaves `rgb` untouched.
 * @param[in,out] rgb  A colour in 0..1, replaced by the tinted one.
 *
 * The rule, and why it is not a multiply: a multiply can only DARKEN, so an
 * authored orange multiplied toward blue is not a blue explosion, it is a
 * muddy brown one -- the red channel the text made large is the one the tint
 * needs to remove. So what is kept from the authored colour is its BRIGHTNESS
 * and how far it has travelled from white, and what the tint supplies is the
 * direction it travelled in. A layer that starts white-hot still starts
 * white-hot; what changes is the colour it cools into.
 *
 * @note Public, and not a static in fx.c, because a blast is going to have to
 *       say the same thing twice -- to the particles here and to the light it
 *       throws on the wall -- and two derivations of one colour are two
 *       colours the first time either is tuned. It is also the half a headless
 *       test can see: the particles' colours are chosen inside ::fx_draw,
 *       which needs a context, and this needs nothing.
 *
 * 한국어
 * ------
 * @brief 작성된 색 하나를 색조로 다시 칠합니다. 제자리에서 수정합니다.
 * @param[in]     tint 요청된 색. {0,0,0}이면 `rgb`를 건드리지 않습니다.
 * @param[in,out] rgb  0..1 범위의 색. 색조가 적용된 값으로 대체됩니다.
 *
 * 규칙과, 그것이 곱셈이 아닌 이유입니다. 곱셈은 *어둡게만* 할 수 있으므로 작성된 주황에
 * 파랑을 곱한 것은 파란 폭발이 아니라 탁한 갈색입니다. 텍스트가 크게 만든 빨강 채널이야말로
 * 색조가 걷어 내야 하는 것이기 때문입니다. 그래서 작성된 색에서 지키는 것은 *밝기*와 흰색에서
 * 얼마나 멀어졌는가이고, 색조가 공급하는 것은 어느 방향으로 멀어졌는가입니다. 희게 타오르며
 * 시작하는 겹은 여전히 희게 시작하고, 바뀌는 것은 그것이 식어 가는 색입니다.
 *
 * @note fx.c의 static이 아니라 공개인 이유는, 폭발이 같은 말을 두 번 해야 하기 때문입니다.
 *       이곳의 입자에게 한 번, 그것이 벽에 던지는 빛에게 한 번. 한 색에 대한 유도가 둘이면
 *       어느 한쪽을 조정하는 순간 색이 둘이 됩니다. 헤드리스 테스트가 볼 수 있는 절반이기도
 *       합니다. 입자의 색은 컨텍스트가 필요한 ::fx_draw 안에서 정해지지만, 이것은 아무것도
 *       필요로 하지 않습니다.
 */
void fx_tint_colour(FxTint tint, float rgb[3]);

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
 * @brief Releases the vertex buffer ::fx_draw allocates on its first call.
 *
 * ENGLISH
 * -------
 * The pair ::fx_draw's lazy mb_init never had. decal.c holds the identical
 * shape -- a file-scope MeshBuf, a ready flag, and a free that clears both --
 * and it is the one this follows.
 *
 * @note The process is about to exit and the OS reclaims the allocation
 *       either way. It is paired anyway for the reason main.c states beside
 *       ::scene_free: render.h declares that every ::mb_init has an ::mb_free,
 *       and a contract kept everywhere except here is one that stops being
 *       kept at all. A reader adding the next buffer copies what they find.
 * @note Safe to call when ::fx_draw never ran, and safe to call twice. The
 *       ready flag is cleared, so a later ::fx_draw allocates again rather
 *       than drawing through a freed pointer.
 * @note Needs no GL context. The ::Mesh beside the buffer is left to the
 *       context teardown, which is what decal.c does with its two.
 *
 * 한국어
 * ------
 * @brief ::fx_draw가 첫 호출에서 할당하는 정점 버퍼를 해제합니다.
 *
 * ::fx_draw의 지연 mb_init이 갖지 못했던 짝입니다. decal.c가 동일한 형태를 가지고 있으며
 * (파일 스코프 MeshBuf, 준비 플래그, 그리고 둘 다 지우는 해제 함수) 이것이 따르는 것이
 * 그 형태입니다.
 *
 * @note 프로세스가 곧 종료되며 어느 쪽이든 OS가 할당을 회수합니다. 그럼에도 짝을 맞추는
 *       이유는 main.c가 ::scene_free 곁에서 밝히는 것과 같습니다. render.h가 모든
 *       ::mb_init에 ::mb_free가 있다고 선언하며, 이곳만 빼고 지켜지는 계약은 곧 어디에서도
 *       지켜지지 않게 됩니다. 다음 버퍼를 추가하는 사람은 눈에 보이는 것을 베낍니다.
 * @note ::fx_draw가 한 번도 실행되지 않았을 때 호출해도, 두 번 호출해도 안전합니다. 준비
 *       플래그를 지우므로 이후의 ::fx_draw는 해제된 포인터로 그리는 대신 다시 할당합니다.
 * @note GL 컨텍스트가 필요 없습니다. 버퍼 곁의 ::Mesh는 컨텍스트 정리에 맡기며, decal.c가
 *       자신의 둘에 대해 하는 것이 그것입니다.
 */
void fx_free(void);

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
