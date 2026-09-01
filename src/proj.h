/**
 * @file proj.h
 * @brief The player's projectiles: grenades that arc and bounce, bolts that fly.
 *
 * ENGLISH
 * -------
 * The shotgun is hitscan -- it hits the instant you pull the trigger, and the
 * only question is where you were aiming. Everything here travels, and that
 * travel time is the whole design: a grenade can be banked around a corner and
 * a bolt has to be led onto a moving target, which are decisions the shotgun
 * never asks for.
 *
 * @note Touches no GL, deliberately. The flight, the bouncing, the fuse and the
 *       blast radius are all arithmetic over a struct, so tools/weapontest.c
 *       steps a grenade down a corridor and asserts where it lands without a
 *       window -- the same split player.c, enemy.c and hook.c are built on.
 * @note Separate from enemy.c's `Shot`, which looks the same from a distance
 *       and is not: that one sweeps against the PLAYER and these sweep against
 *       MONSTERS. Merging them would mean a projectile carrying a flag for
 *       whose side it is on, and every collision test asking.
 *
 * 한국어
 * ------
 * 샷건은 히트스캔입니다. 방아쇠를 당기는 즉시 명중하며, 유일한 질문은 어디를 겨눴는가입니다.
 * 이곳의 모든 것은 *날아가며*, 그 비행 시간이 설계의 전부입니다. 유탄은 모퉁이 너머로
 * 튕겨 넣을 수 있고 탄은 움직이는 표적에 예측 사격을 해야 하는데, 둘 다 샷건은 결코 묻지
 * 않는 판단입니다.
 *
 * @note 의도적으로 GL을 전혀 건드리지 않습니다. 비행·도탄·도화선·폭발 반경이 모두 구조체에
 *       대한 산술이므로, tools/weapontest.c가 창 없이 유탄을 복도로 굴려 어디에 떨어지는지
 *       단언합니다. player.c, enemy.c, hook.c가 기반한 것과 같은 분리입니다.
 * @note enemy.c의 `Shot`과 별개입니다. 멀리서 보면 같아 보이지만 아닙니다. 그쪽은
 *       *플레이어*를, 이쪽은 *몬스터*를 향해 훑습니다. 합치면 발사체가 어느 편인지를
 *       나타내는 플래그를 지니게 되고, 모든 충돌 판정이 그것을 묻게 됩니다.
 */
#ifndef PROJ_H
#define PROJ_H

#include "level.h"
/* FxTint -- a blast carries the colour it goes off in, and ::proj_boom_fx is
   where it is spent. fx.h names no GL and includes only m.h, so this costs the
   headless side nothing.
   FxTint입니다. 폭발은 자신이 터질 색을 지니고 다니며 ::proj_boom_fx가 그것을 씁니다.
   fx.h는 GL을 부르지 않고 m.h만 포함하므로, 헤드리스 쪽이 치르는 비용은 없습니다. */
#include "fx.h"

/* --- Capacity / 용량 --- */

/**
 * @brief Projectiles in flight at once.
 *
 * Sized for the rapid weapon, which is the only one that can fill it: at one
 * shot every 0.085s over a 70 m/s flight across a 35m arena, roughly six are
 * airborne at any moment. The rest is headroom for a grenade volley thrown
 * into the same room.
 *
 * 연사 무기를 기준으로 정했습니다. 이 풀을 채울 수 있는 유일한 무기입니다.
 */
#define PROJ_MAX 48



/** @brief Metres a grenade's blast reaches. / 유탄 폭발이 도달하는 거리 (미터). */
#define PROJ_BLAST_RADIUS 4.2f

/**
 * @brief Seconds a grenade burns before going off on its own.
 *
 * Long enough to bank a shot off a wall and around a corner, short enough that
 * one landing at your feet is your problem rather than something you stroll
 * away from. Contact with a monster ends it early.
 *
 * 벽에 튕겨 모퉁이를 돌 만큼 길고, 발밑에 떨어진 것이 태연히 걸어 나갈 일이 아니라
 * 스스로의 문제가 될 만큼 짧습니다. 몬스터와 접촉하면 즉시 끝납니다.
 */
#define PROJ_FUSE 1.6f

/* --- what colour an explosion is -------------------------------------------
 *
 * ENGLISH
 * -------
 * A LEGEND, NOT DECORATION, and it is the legend scene.c's LIGHT_COL_* table is
 * written to: what the player causes is warm, what a monster casts is cold, and
 * the shrine's gold belongs to the one thing in a room that is a REWARD -- see
 * the note above LIGHT_COL_ALTAR, which is where that last rule is argued. An
 * explosion is the loudest thing that happens in this game, so it is the worst
 * place to file something under the wrong side of those lines -- which is
 * exactly what had happened twice, both times by borrowing the monster bolt's
 * `boltburst` for a player's blast.
 *
 * The table lives here rather than beside the weapons that throw them because
 * a legend has to be readable in one place. Two entries is where a table is
 * cheapest to start and hardest to start later.
 *
 * 한국어
 * ------
 * *장식이 아니라 범례이며*, scene.c의 LIGHT_COL_* 표가 따라 쓰인 그 범례입니다. 플레이어가
 * 일으키는 것은 따뜻하고, 몬스터가 시전하는 것은 차가우며, 제단의 금색은 방에서 유일하게
 * *보상*인 것의 몫입니다. 마지막 규칙이 논해지는 자리는 LIGHT_COL_ALTAR 위의 설명입니다.
 * 폭발은 이 게임에서 가장 요란한 사건이므로 그 선들의 잘못된 쪽에 무언가를 분류해 넣기에 가장
 * 나쁜 자리입니다. 그리고 정확히 그 일이 두 번 일어났습니다. 두 번 모두 플레이어의 폭발에
 * 몬스터 볼트의 `boltburst`를 빌려 쓰면서였습니다.
 *
 * 이 표가 그것을 던지는 무기 곁이 아니라 이곳에 있는 이유는, 범례는 한자리에서 읽혀야 하기
 * 때문입니다. 항목이 둘일 때가 표를 시작하기 가장 싸고, 나중에 시작하기 가장 어려운 때입니다. */

/**
 * @brief The launcher's blast: the orange effects.txt already cools into.
 *
 * ENGLISH: What the text already authors rather than a new colour, so the
 * default and the legend agree: every layer converges on the hue `blastburst`
 * cools into, which is the deep end of the same orange LIGHT_COL_PROJ throws on
 * the wall while the grenade is still in the air. The grenade is the blast this
 * project's effects were written for, so its entry is the one that had to
 * change nothing -- what the tint buys here is that the colour is now SAID
 * somewhere, and the next explosion has a line to be different from.
 *
 * 한국어: 새 색이 아니라 텍스트가 이미 작성해 둔 색입니다. 기본값과 범례가 일치하도록 하기
 * 위해서입니다. 모든 겹이 `blastburst`가 식어 가는 색상으로 수렴하며, 그것은 유탄이 아직
 * 공중에 있는 동안 LIGHT_COL_PROJ가 벽에 던지는 것과 같은 주황의 짙은 끝입니다. 이 프로젝트의
 * 이펙트들이 그것을 위해 쓰인 폭발이 유탄이므로, 아무것도 바꾸지 않아야 하는 항목이 이것입니다.
 * 색조가 이곳에서 사 오는 것은 그 색이 이제 *어딘가에 적혀 있다*는 것과, 다음 폭발이 그것과
 * 달라질 기준선을 갖는다는 것입니다.
 */
#define BLAST_TINT_GRENADE ((FxTint){ 255, 104,  26 })

/**
 * @brief The axe's landing slam: an impact, so almost no colour at all.
 *
 * ENGLISH
 * -------
 * NOTHING IS BURNING HERE. A grenade is fuel going off and it has the deep
 * orange of one; a slam is a floor being broken by something heavy landing on
 * it, and what that throws is white. So this is nearly white by design, and the
 * saturation is the difference the player reads: from behind and in the dark, a
 * white flash is the axe and an orange one is a grenade, which is the whole
 * reason a blast carries a colour at all.
 *
 * WHAT IT LEANS AWAY FROM IS GOLD. The obvious pale warm colour for struck
 * metal sits within a few percent of LIGHT_COL_ALTAR, and the shrine's rule is
 * that nothing which hurts may wear its hue -- an explosion that read as a
 * reward for a second would be the most expensive second in the room. The blue
 * channel is lifted well past the shrine's to keep the two apart.
 *
 * 한국어
 * ------
 * @brief 도끼의 착지 내려찍기. 충격이므로 색이라 할 것이 거의 없습니다.
 *
 * *이곳에서는 아무것도 타지 않습니다.* 유탄은 연료가 터지는 것이고 그에 맞는 짙은 주황을
 * 가지지만, 내려찍기는 무거운 것이 떨어져 바닥이 깨지는 것이며 그것이 던지는 것은 흰색입니다.
 * 그래서 이 색은 의도적으로 거의 흰색이고, 플레이어가 읽는 차이는 채도입니다. 등 뒤에서
 * 어둠 속에서도 흰 섬광은 도끼이고 주황 섬광은 유탄입니다. 폭발이 색을 지니는 이유 전부가
 * 그것입니다.
 *
 * *이 색이 피해 가는 것은 금색입니다.* 두들겨 맞은 금속에 어울리는 뻔한 밝고 따뜻한 색은
 * LIGHT_COL_ALTAR와 몇 퍼센트 이내로 붙어 있는데, 제단의 규칙은 아프게 하는 무엇도 그 색조를
 * 입을 수 없다는 것입니다. 잠깐이라도 보상으로 읽히는 폭발은 방에서 가장 비싼 1초가 됩니다.
 * 둘을 갈라 두기 위해 파랑 채널을 제단의 것보다 충분히 높였습니다.
 */
#define BLAST_TINT_SLAM    ((FxTint){ 255, 196, 172 })

/** @brief Speed kept after bouncing off a surface. / 표면에 튕긴 뒤 유지되는 속도의 비율. */
#define PROJ_BOUNCE 0.42f

/** @brief Radius used against walls and monsters, metres. / 벽과 몬스터에 대한 판정 반경 (미터). */
#define PROJ_RADIUS 0.18f

/* --- the flash a detonation leaves ----------------------------------------
 *
 * ENGLISH
 * -------
 * A GRENADE LIT THE ROOM RIGHT UP TO THE MOMENT IT WENT OFF, AND NOT AFTER.
 * scene.c offers every round in flight to the shader as a point light --
 * ::LIGHT_PROJ_POWER, steady for the whole flight -- and ::detonate clears
 * `active`. So the brightest event in this game was the one frame in which the
 * room got DARKER: the fireball is six layers of additive particle drawn in
 * front of geometry that fell back to ambient, because the only thing that had
 * been lighting it was the round that had just stopped existing.
 *
 * A ::Flash is that missing half. It is neither a particle nor a projectile:
 * it is a record that something went off HERE, THIS BIG, THIS RECENTLY, kept
 * because three separate readers each ask it a different question.
 *
 *   how bright is the room   ::scene_lights, which hands it to the shader
 *   how hard is the camera   ::world_step, which shakes by distance from it
 *   how long ago was it      both, through ::proj_flash_fade -- ONE curve, so
 *                            the light and the jolt cannot come to different
 *                            conclusions about when the explosion was over
 *
 * WHY IT IS NOT A PARTICLE. fx.c would hold it happily and could answer none
 * of the three. A particle is written to be drawn and nothing may ask where it
 * is; the pool evicts the oldest without asking whether something is still
 * reading it; and a light has to last exactly as long as it is bright, where a
 * burst is authored to last as long as it is still expanding. The blast smoke
 * lives 900ms and the light that made it is over in a third of that.
 *
 * WHY IT IS IN THIS FILE. ::detonate is what makes one, and a detonation is
 * this module's event -- the same reason ::proj_blast is here rather than in
 * enemy.c even though every monster it damages belongs to that one. The light
 * an explosion throws is the same fact seen from the renderer's side.
 *
 * 한국어
 * ------
 * *유탄은 터지기 직전까지 방을 밝혔고, 터진 뒤에는 밝히지 않았습니다.* scene.c는 비행 중인
 * 모든 탄을 점광원으로 셰이더에 제안하며(::LIGHT_PROJ_POWER, 비행 내내 일정) ::detonate는
 * `active`를 지웁니다. 그래서 이 게임에서 가장 밝은 사건이, 방이 오히려 *어두워지는* 한
 * 프레임이었습니다. 화구는 가산 입자 여섯 겹으로 그려지는데 그 뒤의 지오메트리는 주변광으로
 * 되돌아갑니다. 그것을 밝히던 유일한 것이 방금 존재하기를 그만둔 그 탄이었기 때문입니다.
 *
 * ::Flash가 그 빠진 절반입니다. 입자도 발사체도 아닙니다. *여기서, 이만큼 크게, 이만큼
 * 최근에* 무언가가 터졌다는 기록이며, 세 명의 서로 다른 독자가 각각 다른 질문을 던지기
 * 때문에 존재합니다.
 *
 *   방이 얼마나 밝은가   ::scene_lights. 이것을 셰이더에 건넵니다
 *   카메라가 얼마나 흔들리는가   ::world_step. 이것으로부터의 거리로 흔듭니다
 *   얼마나 지났는가   양쪽 모두. ::proj_flash_fade를 통해 *하나의* 곡선을 쓰므로, 빛과
 *                     충격이 폭발이 언제 끝났는지에 대해 서로 다른 결론에 이를 수 없습니다
 *
 * *왜 입자가 아닌가.* fx.c는 이것을 기꺼이 담겠지만 셋 중 어느 것에도 답하지 못합니다.
 * 입자는 그려지기 위해 쓰이며 그 위치를 물을 수 있는 것이 없고, 풀은 누군가 아직 읽고
 * 있는지 묻지 않고 가장 오래된 것을 밀어내며, 빛은 밝은 동안만 지속되어야 하는데 폭발은
 * 아직 퍼지는 동안 지속되도록 작성됩니다. 폭발 연기는 900ms를 살고 그것을 만든 빛은 그
 * 3분의 1 만에 끝납니다.
 *
 * *왜 이 파일인가.* ::detonate가 이것을 만들고, 폭발은 이 모듈의 사건입니다. ::proj_blast가
 * 피해를 주는 몬스터가 전부 enemy.c의 것인데도 이 파일에 있는 것과 같은 이유입니다. 폭발이
 * 던지는 빛은 같은 사실을 렌더러 쪽에서 본 것입니다. */

/**
 * @brief Detonations whose light is still up at once.
 *
 * ENGLISH: The ring overwrites the oldest. Six was right while a flash meant a
 * DETONATION and nothing else: one lasts ::PROJ_FLASH_TIME and the launcher's
 * cooldown is 0.85s, nearly three times that, so only a volley thrown ahead
 * and arriving together could fill it.
 *
 * RAISED TO SIXTEEN BECAUSE A HIT IS NOW A FLASH TOO. The rapid fires every
 * 85ms and every bolt that lands leaves one, so four of the player's own are
 * alive at any moment before a single monster has fired back -- and at six
 * slots the fifth plasma ping would evict the grenade that went off half a
 * second ago, which is the one light in the room worth having. The cost of
 * being wrong here is silent and specific: the biggest event on screen is the
 * one that stops lighting anything.
 *
 * 한국어: @brief 빛이 아직 남아 있는 사건의 동시 개수.
 * 링이 가장 오래된 것을 덮어씁니다. 섬광이 *폭발*만을 뜻하던 동안에는 여섯이 옳았습니다.
 * 하나가 ::PROJ_FLASH_TIME 동안 지속되고 발사기 쿨다운은 그 세 배에 가까운 0.85초이므로,
 * 미리 던져 두어 한꺼번에 도착하는 일제 사격만이 그것을 채울 수 있었습니다.
 *
 * *피탄도 이제 섬광이므로 열여섯으로 올렸습니다.* 연사는 85ms마다 쏘고 명중한 탄마다 하나를
 * 남기므로, 몬스터가 한 발도 되쏘기 전에 플레이어 자신의 것 넷이 언제나 살아 있습니다. 슬롯이
 * 여섯이면 다섯 번째 플라즈마 피탄이 0.5초 전에 터진 유탄을 밀어내는데, 그것이야말로 방에서
 * 가질 값어치가 있는 유일한 빛입니다. 이곳에서 틀렸을 때의 대가는 조용하고 구체적입니다.
 * 화면에서 가장 큰 사건이 아무것도 밝히지 않게 됩니다.
 */
#define PROJ_MAX_FLASHES 16

/**
 * @brief What made a ::Flash, so the renderer can pick its colour.
 *
 * ENGLISH
 * -------
 * A KIND AND NOT A COLOUR, because colour is scene.c's and always has been --
 * every hue in this game is in one table there, under a note that says warm is
 * yours and cold is theirs. A `float col[3]` on this record would be the
 * second place a light's colour can be decided, and the two would drift the
 * first time somebody retuned one of them.
 *
 * It stopped being optional when a hit became a flash. A detonation could get
 * away with no kind at all -- there was only one blast colour, and scene.c
 * simply named it -- but a plasma bolt striking a wall has to light it in the
 * bolt's own green, and a monster's has to light it in that creature's row of
 * ::LIGHT_COL_SHOT, or the wall goes one colour while the burst on it goes
 * another. That is the failure ::LIGHT_COL_SHOT's own note warns about, and
 * this enum is the half of the fix that lives on this side of the line.
 *
 * 한국어
 * ------
 * @brief ::Flash를 만든 것이 무엇인지. 렌더러가 색을 고를 수 있게 합니다.
 *
 * *색이 아니라 종류입니다.* 색은 언제나 scene.c의 것이었기 때문입니다. 이 게임의 모든 색조는
 * 그곳의 한 표에 있고, 따뜻한 것은 당신 것이고 차가운 것은 그들 것이라는 설명이 붙어
 * 있습니다. 이 기록에 `float col[3]`을 두면 광원의 색을 정할 수 있는 곳이 둘이 되고, 둘 중
 * 하나를 다시 조정하는 순간 어긋나기 시작합니다.
 *
 * 피탄이 섬광이 되면서 이것은 선택 사항이기를 그만두었습니다. 폭발은 종류가 아예 없어도
 * 괜찮았습니다. 폭발 색은 하나뿐이었고 scene.c가 그냥 이름을 댔습니다. 그러나 벽을 때린
 * 플라즈마 볼트는 그 볼트 자신의 녹색으로 벽을 밝혀야 하고, 몬스터의 것은 그 생물의
 * ::LIGHT_COL_SHOT 행으로 밝혀야 합니다. 그러지 않으면 벽은 한 색이고 그 위의 폭발은 다른
 * 색입니다. ::LIGHT_COL_SHOT 자신의 설명이 경고하는 실패가 그것이며, 이 열거형은 그 수정의
 * 이쪽 절반입니다.
 */
enum {
    FLASH_BLAST = 0, /**< A charge going off. White at the instant, its own orange leaving. / 장약이 터짐. 순간에는 흰색, 사그라들며 자기 주황. */
    FLASH_BOLT,      /**< The player's plasma bolt landing. / 플레이어의 플라즈마 볼트가 닿음. */
    FLASH_SHOT       /**< A monster's bolt landing; ::Flash::type says whose. / 몬스터의 볼트가 닿음. 누구의 것인지는 ::Flash::type. */
};

/**
 * @brief Seconds a detonation's light lasts.
 *
 * ENGLISH: Shorter than any layer of the burst it belongs to -- the core is
 * 170ms and the smoke 900 -- because this is the FIREBALL and not the fire.
 * A light held for as long as the smoke would say the room is still burning
 * while what is left of the explosion is a grey cloud drifting upward.
 *
 * 한국어: @brief 폭발의 빛이 지속되는 시간(초).
 * 자신이 속한 폭발의 어느 겹보다도 짧습니다. 코어가 170ms, 연기가 900ms입니다. 이것은
 * *화구*이지 불이 아니기 때문입니다. 연기만큼 오래 유지되는 빛은, 폭발에서 남은 것이
 * 떠오르는 회색 구름뿐인데도 방이 아직 타고 있다고 말하게 됩니다.
 */
#define PROJ_FLASH_TIME 0.30f

/**
 * @brief Metres the ground wave is lifted off the surface it runs across.
 *
 * ENGLISH
 * -------
 * `blastwave` is `face normal`, which means its quads lie IN the plane of the
 * surface -- and a quad in the same plane as the floor it is drawn on has no
 * answer to the depth test. It flickers, per fragment, differently every frame
 * as the camera moves, which reads as the floor being broken rather than as
 * anything having gone off on it.
 *
 * The same nudge ::decal_hit makes for the same reason, and larger than its
 * 0.012 because these quads are up to half a metre across where a bullet hole
 * is 8.5cm: a big flat quad at a glancing angle needs more clearance than a
 * small one before the near edge dips back under.
 *
 * @note Applied by the CALLERS rather than inside the effect, because the
 *       effect file has no way to express it -- `spawn` is a radius in every
 *       direction, not an offset along one -- and because both callers already
 *       hold the normal they would have to be handed.
 *
 * 한국어
 * ------
 * @brief 지면 파동이 자신이 가로지르는 표면에서 들어 올려지는 거리 (미터).
 *
 * `blastwave`는 `face normal`이며, 그 사각형들이 표면과 *같은 평면에* 눕는다는 뜻입니다.
 * 그리고 자신이 그려지는 바닥과 같은 평면에 있는 사각형은 깊이 테스트에 답할 것이 없습니다.
 * 프래그먼트 단위로, 카메라가 움직일 때마다 다르게 깜박이며, 그것은 바닥에서 무슨 일이
 * 일어났다는 것이 아니라 바닥이 고장 났다는 뜻으로 읽힙니다.
 *
 * ::decal_hit이 같은 이유로 하는 것과 같은 밀어내기이며, 그쪽의 0.012보다 큽니다. 탄흔은
 * 8.5cm인데 이 사각형들은 최대 0.5미터에 이르기 때문입니다. 비스듬히 놓인 큰 평면 사각형은
 * 가까운 쪽 모서리가 다시 아래로 잠기기 전까지 작은 것보다 더 많은 여유를 필요로 합니다.
 *
 * @note 이펙트 안이 아니라 *호출자*가 적용합니다. 이펙트 파일에는 이것을 표현할 방법이 없고
 *       (`spawn`은 한 방향의 오프셋이 아니라 모든 방향의 반경입니다), 두 호출자 모두 자기에게
 *       건네져야 할 법선을 이미 들고 있기 때문입니다.
 */
#define PROJ_WAVE_LIFT 0.05f

/**
 * @brief Metres a landing bolt's light claims, and how big an event it is.
 *
 * ENGLISH
 * -------
 * A BOLT HAS NO DAMAGE RADIUS, so unlike every other ::proj_flash caller this
 * one cannot read its number off the thing that happened -- there is nothing
 * to read. It is a look, and the reason it is a number here rather than in
 * scene.c is that scene.c multiplies whatever it is given by a reach of its
 * own: what this says is how big the event was in the world, and what that
 * says is how far a light of that size carries. The two are different
 * questions and a hit that lit a room like a grenade would be answering the
 * wrong one.
 *
 * Small on purpose, and dim. Quake II gives its blaster impact a light of 150
 * against a rocket's 350, and Xonotic's `electro_impact` a lightradius of 250
 * that fades over the same distance; both are pings rather than events. The
 * rapid puts one of these on a wall every 85ms, and a ping that lit the room
 * properly would strobe it.
 *
 * 한국어
 * ------
 * @brief 착탄한 볼트의 빛이 주장하는 거리(미터)와, 그것이 얼마나 큰 사건인가.
 *
 * *볼트에는 피해 반경이 없으므로*, 다른 모든 ::proj_flash 호출자와 달리 이쪽은 일어난 일에서
 * 수를 읽어 낼 수 없습니다. 읽을 것이 없습니다. 이것은 겉모습이며, scene.c가 아니라 이곳의
 * 수인 이유는 scene.c가 건네받은 것에 자기 몫의 도달 거리를 곱하기 때문입니다. 이것이 말하는
 * 것은 사건이 세계에서 얼마나 컸는가이고, 그쪽이 말하는 것은 그 크기의 빛이 얼마나 멀리
 * 가는가입니다. 둘은 다른 질문이며, 유탄처럼 방을 밝히는 피탄은 틀린 쪽에 답하는 것입니다.
 *
 * 의도적으로 작고 어둡습니다. Quake II는 블래스터 피탄에 로켓의 350에 대해 150의 빛을 주고,
 * Xonotic의 `electro_impact`는 같은 거리에 걸쳐 사그라드는 250의 lightradius를 줍니다. 둘 다
 * 사건이 아니라 신호입니다. 연사는 85ms마다 이것을 벽에 하나씩 놓으며, 방을 제대로 밝히는
 * 신호는 방을 점멸시킵니다.
 */
#define PROJ_HIT_RADIUS 1.20f   ///< @brief Metres. / 미터.
#define PROJ_HIT_POWER  0.55f   ///< @brief 0..1, against a charge's 1. / 0..1. 장약의 1에 대하여.

/**
 * @brief Seconds between the puffs a projectile lays down behind it.
 *
 * ENGLISH
 * -------
 * A FIXED INTERVAL AND NOT ONCE PER FRAME, which is the same rule and the same
 * reasoning as ::SHOT_TRAIL_INTERVAL on the monsters' side. Emitting per frame
 * makes the trail's density a property of the machine -- visibly thicker at
 * 144fps than at 60 -- and lets one grenade in the air fill the shared
 * particle pool by itself, at which point every other effect in the level
 * starts being dropped to make room for it.
 *
 * A little slower than the bolt's 30ms, because a grenade is a little slower:
 * spacing along the path is speed x interval, and matching the intervals would
 * have made the arc that travels less far the one drawn with more puffs.
 *
 * 한국어
 * ------
 * @brief 발사체가 뒤에 남기는 퍼프 사이의 간격(초).
 *
 * *프레임마다가 아니라 고정 간격입니다.* 몬스터 쪽의 ::SHOT_TRAIL_INTERVAL과 같은 규칙이며
 * 같은 논거입니다. 프레임 단위 방출은 궤적의 밀도를 기기의 성질로 만들고(144fps에서 60fps보다
 * 눈에 띄게 두꺼워집니다), 공중의 유탄 하나가 공유 입자 풀을 혼자 채우게 하며, 그 시점부터
 * 레벨의 다른 모든 이펙트가 자리를 내주느라 버려지기 시작합니다.
 *
 * 볼트의 30ms보다 조금 느린 이유는 유탄이 조금 느리기 때문입니다. 경로상의 간격은 속력 x
 * 간격이므로, 간격을 맞추면 덜 멀리 가는 포물선이 더 많은 퍼프로 그려졌을 것입니다.
 */
#define PROJ_TRAIL_INTERVAL 0.035f

/* --- Types / 타입 --- */

/**
 * @struct Proj
 * @brief One projectile in flight.
 *
 * @note `gravity` is what separates a grenade from a bolt, and it is the only
 *       thing that does -- a bolt is a grenade that does not fall and does not
 *       bounce. One field, the same way enemy.c's `shot_speed` decides melee
 *       from ranged, rather than a `kind` enum that every branch has to read.
 *
 * @brief 비행 중인 발사체 하나입니다.
 * @note `gravity`가 유탄과 탄을 가르며, 그것이 유일한 구분입니다. 탄은 떨어지지도 튕기지도
 *       않는 유탄입니다. enemy.c의 `shot_speed`가 근접과 원거리를 가르는 것과 같이 필드
 *       하나이며, 모든 분기가 읽어야 하는 `kind` 열거형이 아닙니다.
 */
typedef struct {
    v3    pos;        /**< Current position. / 현재 위치. */
    v3    vel;        /**< Velocity, m/s. / 속도 (m/s). */
    float gravity;    /**< m/s^2 downward; 0 flies straight and never bounces. / 하강 가속도. 0이면 직진하며 튕기지 않습니다. */
    float fuse;       /**< Seconds until it goes off; <=0 means on contact only. / 폭발까지의 시간(초). 0 이하이면 접촉 시에만 폭발합니다. */
    float life;       /**< Seconds before it gives up, for the non-exploding kind. / 폭발하지 않는 종류가 소멸하기까지의 시간. */
    float blast;      /**< Blast radius in metres; 0 means it damages one target. / 폭발 반경(미터). 0이면 하나의 대상에만 피해를 줍니다. */
    int   damage;     /**< Damage at the centre. / 중심에서의 피해량. */
    float spin;       /**< Free-running clock, so a grenade tumbles. / 자유 진행 시계. 유탄이 구르게 합니다. */
    /**
     * @brief Seconds until the next puff of trail. See ::PROJ_TRAIL_INTERVAL.
     *
     * Beside ::spin rather than derived from it, even though both are clocks
     * on the same object: ::spin free-runs and is only ever read modulo a turn,
     * where this one has to be reset at a rate the tumble knows nothing about.
     * A trail paced off the spin would change with the spin, and the spin is a
     * look.
     * / 둘 다 같은 대상의 시계이지만 ::spin에서 유도하지 않고 그 곁에 둡니다. ::spin은 자유
     * 진행하며 한 바퀴에 대한 나머지로만 읽히는 데 반해, 이쪽은 회전이 알지 못하는 비율로
     * 초기화되어야 합니다. 회전에 맞춘 궤적은 회전과 함께 변하는데, 회전은 겉모습입니다.
     */
    float trail_t;
    int   active;     /**< 0 when the slot is free. / 0이면 빈 슬롯입니다. */
} Proj;

/**
 * @struct ProjPool
 * @brief Every projectile in flight, owned by the caller rather than by proj.c.
 *
 * A plain array in a struct, so that a ::World holds its own and two of them do
 * not share one. It was a file-scope `static Proj g_proj[PROJ_MAX]`, which is
 * why tools\steptest.c had to call ::proj_reset by hand between fixtures: the
 * previous case's grenades were still in the air.
 *
 * 호출자가 소유하는, 비행 중인 모든 발사체입니다. 구조체 안의 평범한 배열이므로 ::World가
 * 자기 것을 가지며 두 개가 하나를 공유하지 않습니다. 이것은 파일 스코프
 * `static Proj g_proj[PROJ_MAX]`였고, 그래서 tools\steptest.c가 픽스처 사이에 ::proj_reset을
 * 손으로 불러야 했습니다. 이전 사례의 유탄이 아직 공중에 있었기 때문입니다.
 */
typedef struct {
    Proj p[PROJ_MAX];   /**< Slots. `active` says which are in use. / 슬롯. `active`가 사용 중인 것을 말합니다. */
} ProjPool;

/**
 * @struct Flash
 * @brief One detonation, for as long as anything is still lighting or shaking from it.
 *
 * ENGLISH
 * -------
 * @note `radius` is the DAMAGE radius, unmultiplied. Every reader scales it by
 *       its own factor, and they are deliberately different numbers: the light
 *       reaches past where the damage stops because light does, and the shake
 *       reaches further still because a blast you cannot be hurt by is still a
 *       blast you heard. Storing a pre-scaled radius would pick one of those
 *       for all three and make the other two wrong.
 * @note `power` is HOW BIG AN EVENT rather than how bright, which is why it is
 *       separate from `radius` when the radius already says something about
 *       size. The axe's slam has the larger radius of the two -- 5.5m against
 *       the grenade's 4.2 -- and is a mass of metal hitting a floor rather
 *       than a charge going off, so a reader that took brightness from radius
 *       would light the room harder for the one with no fire in it.
 *
 * 한국어
 * ------
 * @brief 폭발 하나. 그것으로부터 아직 밝히거나 흔들리는 것이 있는 동안 존재합니다.
 * @note `radius`는 배율이 적용되지 않은 *피해* 반경입니다. 모든 독자가 자기 배율을 곱하며,
 *       그 배율들은 의도적으로 서로 다릅니다. 빛은 피해가 멈추는 곳 너머까지 닿습니다. 빛이
 *       원래 그렇기 때문입니다. 흔들림은 그보다 더 멀리 닿습니다. 다칠 수 없는 거리의 폭발도
 *       들리는 폭발이기 때문입니다. 미리 배율을 곱한 반경을 저장하면 셋 중 하나를 골라 셋
 *       모두에 적용하는 것이고 나머지 둘이 틀리게 됩니다.
 * @note `power`는 얼마나 밝은가가 아니라 *얼마나 큰 사건인가*이며, 반경이 이미 크기에 대해
 *       무언가를 말하고 있는데도 따로 두는 이유가 그것입니다. 도끼의 내려찍기는 둘 중 반경이
 *       더 큽니다(유탄의 4.2m에 대해 5.5m). 그러나 그것은 장약이 터지는 것이 아니라 금속
 *       덩어리가 바닥을 치는 것입니다. 밝기를 반경에서 가져오는 독자는 불이 없는 쪽을 위해
 *       방을 더 세게 밝히게 됩니다.
 */
typedef struct {
    v3    pos;      /**< Where it went off. / 터진 자리. */
    float radius;   /**< The damage radius it had, metres. / 그것이 가졌던 피해 반경 (미터). */
    float power;    /**< 0..1: how big an event it was. / 0..1. 얼마나 큰 사건이었는가. */
    float life;     /**< Seconds left. 0 means the slot is free. / 남은 시간(초). 0이면 빈 슬롯. */
    short kind;     /**< FLASH_*: what made it, so the renderer can colour it. / FLASH_*. 무엇이 만들었는지. 렌더러가 색을 고르는 데 씁니다. */
    /**
     * @brief For ::FLASH_SHOT, which monster cast it; -1 otherwise.
     *
     * A ::MonTypeID, held as a plain short rather than as the enum: enemy.h is
     * not included here and must not be. proj.h is included BY pools.h, which
     * enemy.h also travels through, and a projectile that needed to know what
     * a monster is would close that loop. The renderer already owns the table
     * this indexes and already bounds-checks it, which is where a stale type
     * would be caught anyway.
     * / ::FLASH_SHOT일 때 어느 몬스터가 시전했는지이며, 그 외에는 -1입니다. ::MonTypeID이지만
     * 열거형이 아니라 평범한 short로 들고 있습니다. 이곳에 enemy.h가 포함되어 있지 않고
     * 포함되어서도 안 되기 때문입니다. proj.h는 pools.h가 포함하는데 enemy.h도 그곳을
     * 지나가므로, 몬스터가 무엇인지 알아야 하는 발사체는 그 고리를 닫아 버립니다. 이것이
     * 색인하는 표는 이미 렌더러의 것이고 이미 범위를 검사하므로, 낡은 type은 어차피 그곳에서
     * 걸립니다.
     */
    short type;

    /**
     * @brief Damage this blast does to the PLAYER at its centre, 0 for none.
     *
     * ENGLISH
     * -------
     * THE ONE FIELD THAT IS NOT ABOUT LIGHT. A ::Flash is otherwise a record
     * of something bright happening somewhere, read by the renderer for the
     * light and by ::step_blast for the camera. This makes it the record of
     * something bright that can also HURT, which is the same event seen by a
     * third reader rather than a second list to keep in step with the first.
     *
     * ZERO FOR ALMOST EVERYTHING, and the exceptions are the argument. A
     * monster's bolt bursting is not the player's doing and the player has
     * already been charged for it by `enemy_update`; charging again here
     * would bill one hit twice. The axe's slam goes off at the player's own
     * feet by design -- a ground pound that hurt the grounder is a weapon
     * with a cost nobody chose. What is left is the grenade: a thing you
     * throw, which is the only blast in the game that can arrive somewhere
     * you did not intend and still be yours.
     *
     * 한국어
     * ------
     * @brief 이 폭발이 중심에서 *플레이어*에게 주는 피해. 없으면 0입니다.
     *
     * *빛에 대한 것이 아닌 유일한 필드입니다.* ::Flash는 그 밖에는 어딘가에서 밝은 일이
     * 일어났다는 기록이며, 렌더러가 빛을 위해 읽고 ::step_blast가 카메라를 위해 읽습니다.
     * 이것은 그것을 *다치게 할 수도 있는* 밝은 일의 기록으로 만듭니다. 첫 목록과 보조를
     * 맞춰야 하는 두 번째 목록이 아니라, 같은 사건을 보는 세 번째 독자입니다.
     *
     * *거의 모든 것에 0이며*, 예외가 곧 논거입니다. 몬스터의 탄환이 터지는 것은 플레이어가
     * 한 일이 아니고 `enemy_update`가 이미 값을 물렸습니다. 이곳에서 또 물리면 한 번의
     * 타격을 두 번 청구하는 것입니다. 도끼의 내려찍기는 설계상 플레이어 자신의 발밑에서
     * 터집니다. 내려찍는 자를 다치게 하는 지면 강타는 아무도 고르지 않은 비용을 지닌
     * 무기입니다. 남는 것은 유탄입니다. *던지는* 것이며, 의도하지 않은 곳에 도착하고도
     * 여전히 자기 것인 이 게임의 유일한 폭발입니다.
     */
    short hurt;
    char  taken;        /**< Whether ::hurt has already been charged. / ::hurt를 이미 물렸는지 여부. */
} Flash;

/**
 * @struct FlashPool
 * @brief The detonations a run still has light out from, owned by the caller.
 *
 * The same shape as ::ProjPool and for the same reason -- a ::World holds its
 * own, so a headless fixture does not inherit the previous one's explosions.
 *
 * 호출자가 소유하는, 아직 빛이 남아 있는 폭발들입니다. ::ProjPool과 같은 형태이며 이유도
 * 같습니다. ::World가 자기 것을 가지므로, 헤드리스 픽스처가 이전 사례의 폭발을 물려받지
 * 않습니다.
 */
typedef struct {
    Flash f[PROJ_MAX_FLASHES];  /**< Slots; `life` 0 means free. / 슬롯. `life`가 0이면 비어 있습니다. */
    int   next;                 /**< Ring cursor: the oldest is overwritten. / 링 커서. 가장 오래된 것을 덮어씁니다. */
} FlashPool;

/* The bundle that holds this pool and its neighbours, by name only: the calls
   below take it because a projectile's detonation reaches monsters and
   particles, not only other projectiles. pools.h defines it, and includes this
   file to do so -- so this end of the pair can only forward-declare.
   이 풀과 그 이웃들을 담는 묶음이며 이름으로만 참조합니다. 아래의 호출들이 그것을 받는
   이유는, 발사체의 폭발이 다른 발사체가 아니라 몬스터와 입자에 닿기 때문입니다. pools.h가
   그것을 정의하며 그러기 위해 이 파일을 포함하므로, 이 쪽 끝은 전방 선언만 할 수 있습니다. */
typedef struct Pools Pools;

/* --- Lifecycle / 수명 주기 --- */

/**
 * @brief Clears every projectile and every flash. Called on a level load.
 *
 * ENGLISH: The flashes as well, and a level load is the one moment the two
 * pools would otherwise disagree. A grenade in the air is gone with the level
 * it was thrown in; a light left over from it arrives in the new room with
 * nothing in that room to have made it, and it arrives at full strength,
 * because a flash that has not been aged has not faded.
 *
 * 한국어: @brief 모든 발사체와 모든 섬광을 제거합니다. 레벨 로드 시 호출됩니다.
 * 섬광도 함께이며, 레벨 로드가 두 풀이 어긋날 수 있는 유일한 순간입니다. 공중의 유탄은
 * 그것이 던져진 레벨과 함께 사라집니다. 거기서 남은 빛은 그것을 만든 무엇도 없는 새 방에
 * 도착하며, 게다가 최대 세기로 도착합니다. 나이를 먹지 않은 섬광은 사그라들지도 않았기
 * 때문입니다.
 */
void proj_reset(Pools *pl);

/**
 * @brief Launches one projectile.
 *
 * @param[in] from    Muzzle position.
 * @param[in] dir     Unit direction.
 * @param[in] speed   m/s along `dir`.
 * @param[in] gravity Downward acceleration; 0 for a straight bolt.
 * @param[in] damage  Damage at the centre of the hit.
 * @param[in] blast   Blast radius in metres, or 0 to damage a single target.
 * @param[in] fuse    Seconds before it goes off on its own, or 0 for never.
 * @return Non-zero when a slot was free.
 *
 * @brief 발사체 하나를 발사합니다.
 * @return 빈 슬롯이 있었으면 0이 아닌 값.
 */
int proj_fire(Pools *pl, v3 from, v3 dir, float speed, float gravity,
              int damage, float blast, float fuse);

/**
 * @brief Advances every projectile, resolving walls, monsters and fuses.
 *
 * @param[in] l  Level, for wall collision.
 * @param[in] dt Timestep in seconds.
 *
 * @note Sweeps rather than teleports: a bolt at 70 m/s covers over a metre in
 *       a frame, so testing only the arrival point would let it pass through a
 *       monster standing between the two.
 *
 * @brief 모든 발사체를 진행시키며 벽·몬스터·도화선을 처리합니다.
 * @note 순간이동이 아니라 훑습니다. 70m/s의 탄은 한 프레임에 1미터 이상 이동하므로,
 *       도착 지점만 검사하면 그 사이에 서 있는 몬스터를 통과해 버립니다.
 */
void proj_update(Pools *pl, const Level *l, float dt);

/* --- Read-back, for the renderer and for tests / 렌더러와 테스트를 위한 조회 --- */

/** @brief How many slots exist; walk them and skip the inactive. / 슬롯의 개수. 순회하며 비활성은 건너뛰십시오. */
int proj_count(const Pools *pl);

/** @brief Borrowed pointer to slot `i`, or NULL. / 슬롯 `i`에 대한 참조 포인터. 없으면 NULL. */
const Proj *proj_at(const Pools *pl, int i);

/** @brief How many are in flight right now. / 지금 비행 중인 개수. */
int proj_live(const Pools *pl);

/**
 * @brief Damages every monster within `radius` of `at`, falling off with distance.
 *
 * @param[in] at     Centre of the blast.
 * @param[in] radius Metres.
 * @param[in] damage Damage at the centre; scales to 0 at the rim.
 * @return How many monsters were hit.
 *
 * @note Public because the axe's landing slam is the same operation as a
 *       grenade going off, and two copies of a falloff curve is two curves to
 *       tune. See ::wp_axe_land.
 *
 * @brief `at`을 중심으로 `radius` 안의 모든 몬스터에 거리에 따라 감소하는 피해를 줍니다.
 * @note 공개된 이유는 도끼의 착지 내려찍기가 유탄 폭발과 동일한 연산이기 때문입니다.
 *       감쇠 곡선의 사본이 둘이면 조정할 곡선이 둘이 됩니다.
 */
int proj_blast(Pools *pl, v3 at, float radius, int damage);

/**
 * @brief Draws an explosion: the six layers, in the colour of what went off.
 *
 * ENGLISH
 * -------
 * @param[in] at     Centre of the blast.
 * @param[in] normal Surface it went off against, or any unit vector in the air.
 * @param[in] radius The DAMAGE radius, which is what the dome is scaled to.
 * @param[in] tint   BLAST_TINT_*, or {0,0,0} for the colours effects.txt wrote.
 *
 * @note SEPARATE FROM ::proj_blast, which is the damage. They are called
 *       together every time so far, and they are still two calls: the axe's
 *       slam wants both and a future decoration -- a barrel with nothing near
 *       it, a scripted detonation -- wants only this one, and a damage
 *       parameter it had to pass 0 to would be a lie in the call.
 * @note Public for the reason ::proj_blast is: the slam is the same explosion
 *       as a grenade, and a second copy of six layers is six chances for the
 *       two to drift apart. wp_axe_land had that copy -- two effects, one of
 *       them the monsters' -- which is how the slam ended up with no visible
 *       radius and the wrong colour.
 *
 * 한국어
 * ------
 * @brief 폭발을 그립니다. 여섯 겹을, 터진 것의 색으로.
 * @param[in] radius *피해* 반경. 돔의 크기가 여기에 맞춰집니다.
 * @param[in] tint   BLAST_TINT_* 또는 {0,0,0}(effects.txt가 쓴 색).
 * @note 피해인 ::proj_blast와 *분리되어* 있습니다. 지금까지는 항상 함께 호출되지만 여전히 두
 *       개의 호출입니다. 도끼의 내려찍기는 둘 다 원하고, 이후의 연출(주변에 아무것도 없는 드럼통,
 *       각본된 폭발)은 이것만 원하며, 0을 넘겨야 하는 피해 인자는 호출문에 적힌 거짓말입니다.
 * @note ::proj_blast와 같은 이유로 공개입니다. 내려찍기는 유탄과 같은 폭발이며, 여섯 겹의 사본이
 *       하나 더 있다는 것은 둘이 어긋날 기회가 여섯이라는 뜻입니다. wp_axe_land가 그 사본을
 *       가지고 있었고(이펙트 둘, 그중 하나는 몬스터의 것), 그래서 내려찍기는 보이는 반경이 없고
 *       색도 틀린 채로 남아 있었습니다.
 */
void proj_boom_fx(Pools *pl, v3 at, v3 normal, float radius, FxTint tint);

/* --- the flash / 섬광 --- */

/**
 * @brief Records that something went off, so the room can be lit and shaken by it.
 *
 * ENGLISH
 * -------
 * @param[in] at     Where it happened, world units.
 * @param[in] radius The DAMAGE radius, metres. Readers scale it themselves --
 *                   see ::Flash.
 * @param[in] power  0..1, how big an event this was. A charge going off is 1;
 *                   anything that merely hits hard is less.
 *
 * @note Public rather than private to ::detonate because the axe's landing
 *       slam is the same event without the fire, and it is already reaching in
 *       here for ::proj_blast. Two places that make a crater should not be two
 *       places that disagree about whether a crater lights anything.
 * @note Takes the ring's next slot unconditionally, the way ::decal_hit does.
 *       There is no "is there room" question: six is more than a player can
 *       fill and the oldest is the one that gives way.
 * @note Touches no GL and reads no ::Level. Safe from simulation code and from
 *       headless tools, which is what lets tools\weapontest.c assert that a
 *       grenade going off leaves one.
 *
 * 한국어
 * ------
 * @brief 무언가가 터졌음을 기록하여, 방이 그것으로 밝아지고 흔들릴 수 있게 합니다.
 * @param[in] radius *피해* 반경 (미터). 배율은 독자들이 각자 곱합니다. ::Flash를 참조하십시오.
 * @param[in] power  0..1. 얼마나 큰 사건이었는가. 장약이 터지면 1이고, 단지 세게 부딪히는
 *                   것은 그보다 작습니다.
 * @note ::detonate의 내부 함수가 아니라 공개인 이유는, 도끼의 착지 내려찍기가 불이 빠진 같은
 *       사건이고 이미 ::proj_blast를 위해 이곳에 손을 뻗고 있기 때문입니다. 구덩이를 만드는
 *       두 곳이, 구덩이가 무언가를 밝히는지에 대해 서로 다른 말을 하는 두 곳이 되어서는 안
 *       됩니다.
 * @note ::decal_hit처럼 링의 다음 슬롯을 무조건 가져옵니다. "자리가 있는가"라는 질문은
 *       없습니다. 여섯은 플레이어가 채울 수 있는 것보다 많고, 가장 오래된 것이 자리를
 *       내어줍니다.
 * @note GL을 건드리지 않고 ::Level도 읽지 않습니다.
 */
void proj_flash(Pools *pl, v3 at, float radius, float power,
                int kind, int type, int hurt);

/**
 * @def PROJ_SELF_DAMAGE
 * @brief What fraction of its own blast the thrower takes.
 *
 * ENGLISH
 * -------
 * QUAKE'S HALF, and taken from it for the reason ::check_attack takes fight.qc's
 * odds: `T_RadiusDamage` in combat.qc computes the points for everyone inside
 * the radius and then writes `if (head == attacker) points = points * 0.5`. It
 * is the number that makes a rocket jump a decision rather than a suicide.
 *
 * WHY THERE IS SELF-DAMAGE AT ALL, since the game ran without it: a splash
 * weapon with no cost to the thrower is not a splash weapon, it is a better
 * hitscan. The grenade's whole shape -- an arc, a fuse, a radius -- is a set of
 * questions about WHERE, and none of them is a question if the answer never
 * costs anything. Firing into a corridor you are standing in should be a thing
 * you decide to do.
 *
 * @note Cut further by ::PW_AEGIS like any other damage, because it arrives
 *       through the same ::player_take as a monster's blow. An artifact that
 *       protected against monsters and not against your own grenade would be a
 *       rule nobody could state.
 *
 * 한국어
 * ------
 * @brief 던진 자가 자기 폭발에서 받는 비율.
 *
 * *퀘이크의 절반*이며, ::check_attack이 fight.qc의 확률을 가져오는 이유로 가져왔습니다.
 * combat.qc의 `T_RadiusDamage`가 반경 안의 모두에 대해 점수를 계산한 뒤
 * `if (head == attacker) points = points * 0.5`라고 적습니다. 로켓 점프를 자살이 아니라
 * *결정*으로 만드는 수입니다.
 *
 * *애초에 왜 자폭 피해가 있는가.* 게임은 그것 없이도 돌아갔습니다. 던진 자에게 아무 비용이
 * 없는 폭발 무기는 폭발 무기가 아니라 더 좋은 히트스캔입니다. 유탄의 형태 전부(포물선, 도화선,
 * 반경)가 *어디에* 대한 질문들이며, 답이 아무 값도 치르지 않는다면 그중 어느 것도 질문이
 * 아닙니다. 자기가 서 있는 복도로 쏘는 것은 *하기로 정하는* 일이어야 합니다.
 *
 * @note 다른 모든 피해와 마찬가지로 ::PW_AEGIS가 더 깎습니다. 몬스터의 일격과 같은
 *       ::player_take를 거쳐 도착하기 때문입니다. 몬스터에게는 지켜 주고 자기 유탄에는
 *       지켜 주지 않는 아티팩트는 아무도 말로 옮길 수 없는 규칙입니다.
 */
#define PROJ_SELF_DAMAGE 0.5f

/**
 * @brief Mark flash `i` as having charged its ::Flash::hurt, so it cannot again.
 *
 * ENGLISH: A flash outlives the instant it describes -- ::PROJ_FLASH_TIME, so
 * the light can fade -- and everything that reads it therefore sees it many
 * times. That is right for a light and for a shake and wrong for a debt. The
 * latch lives on the flash rather than in the reader because the flash is what
 * is shared: a second reader that also charged would need its own memory of
 * which ones it had seen, and the two would drift.
 *
 * 한국어: 섬광은 자신이 서술하는 순간보다 오래 삽니다. 빛이 잦아들 수 있도록
 * ::PROJ_FLASH_TIME 동안이며, 따라서 그것을 읽는 모든 것이 여러 번 봅니다. 빛과 흔들림에는
 * 옳고 *빚*에는 그릅니다. 걸쇠가 독자가 아니라 섬광에 사는 이유는 공유되는 것이 섬광이기
 * 때문입니다. 값을 물리는 두 번째 독자가 생기면 자기가 본 것들에 대한 기억을 따로 가져야
 * 하고, 둘은 어긋나게 됩니다.
 */
void proj_flash_taken(Pools *pl, int i);

/**
 * @brief Ages every flash and retires the dead ones.
 *
 * ENGLISH
 * -------
 * @param[in] dt Timestep in seconds.
 * @note SEPARATE FROM ::proj_update, and called beside ::fx_update rather than
 *       beside it. ::proj_update is frozen with the world -- a grenade hanging
 *       in mid-air behind a pause menu says the game is still running -- and a
 *       light must not be, because the particles it lit are not either. Pause
 *       on the frame a grenade goes off and a frozen flash leaves the room lit
 *       white behind the menu, under a smoke cloud that is still expanding.
 *
 * 한국어
 * ------
 * @brief 모든 섬광을 나이 먹이고 죽은 것을 회수합니다.
 * @note *::proj_update와 분리되어 있으며*, 그 옆이 아니라 ::fx_update 옆에서 호출됩니다.
 *       ::proj_update는 월드와 함께 멈춥니다. 일시정지 메뉴 뒤에 공중에 떠 있는 유탄은 게임이
 *       아직 돌아간다고 말하기 때문입니다. 그러나 빛은 멈추어서는 안 됩니다. 그것이 밝힌
 *       입자들도 멈추지 않기 때문입니다. 유탄이 터지는 프레임에 일시정지하면, 멈춘 섬광이
 *       메뉴 뒤의 방을 하얗게 밝힌 채로 두고 그 아래에서는 연기가 계속 퍼집니다.
 */
void proj_flash_update(Pools *pl, float dt);

/** @brief How many slots exist; walk them and skip the dead. / 슬롯의 개수. 순회하며 죽은 것은 건너뛰십시오. */
int proj_flash_count(const Pools *pl);

/** @brief Borrowed pointer to slot `i`, or NULL. / 슬롯 `i`에 대한 참조 포인터. 없으면 NULL. */
const Flash *proj_flash_at(const Pools *pl, int i);

/** @brief How many are still lighting anything. For tests. / 아직 무언가를 밝히고 있는 개수. 테스트용입니다. */
int proj_flash_live(const Pools *pl);

/**
 * @brief How much of a flash is left, 0..1, on the curve both readers use.
 *
 * ENGLISH
 * -------
 * @param[in] f A slot from ::proj_flash_at. A dead or null one returns 0.
 * @note SQUARED, not linear. An explosion is a spike: it arrives at full and
 *       is most of the way gone before the eye has finished registering it,
 *       and a light that walks evenly down to nothing over three tenths of a
 *       second reads as a lamp being dimmed rather than as a detonation.
 * @note ONE function, called by both readers, for the reason ::WORLD_SHAKE_DECAY
 *       is public: a second copy of this curve on the drawing side is a number
 *       that can disagree with the one that ends the shake, and the failure it
 *       produces is a room that is still white after the camera has settled.
 *
 * 한국어
 * ------
 * @brief 섬광이 얼마나 남았는지, 0..1. 두 독자가 함께 쓰는 곡선입니다.
 * @note 선형이 아니라 *제곱*입니다. 폭발은 스파이크입니다. 최대치로 도착해서, 눈이 그것을
 *       인지하기를 마치기도 전에 거의 사라집니다. 0.3초에 걸쳐 고르게 0까지 걸어 내려가는
 *       빛은 폭발이 아니라 조명이 어두워지는 것으로 읽힙니다.
 * @note ::WORLD_SHAKE_DECAY가 공개인 것과 같은 이유로 *하나의* 함수를 두 독자가 부릅니다.
 *       그리는 쪽에 있는 이 곡선의 두 번째 사본은 흔들림을 끝내는 값과 어긋날 수 있는
 *       숫자이며, 그것이 만드는 실패는 카메라가 가라앉은 뒤에도 여전히 하얀 방입니다.
 */
float proj_flash_fade(const Flash *f);

#endif
