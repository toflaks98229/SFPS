/**
 * @file world.h
 * @brief The simulated world, and the order one frame advances it in.
 *
 * ENGLISH
 * -------
 * Everything a frame does to the game that does not need a window. The level,
 * the player, the weapon and the run are one struct so that a caller cannot
 * hold half of them, and ::world_step is the whole per-frame update in one
 * function so that its ORDER is a thing a test can reach.
 *
 * That order used to live in the body of `WinMain`, and this file exists
 * because of what that cost. main.c's own header comment declared the order
 * "load-bearing rather than incidental" -- the rope constraint before the move,
 * the audio listener before anything that plays, death noticed in exactly one
 * place -- and then nothing checked any of it, because the only way to run it
 * was to open a window and play. tools\steptest.c now does, and it can only do
 * that because this module names no GL function, no Win32 call and no menu.
 *
 * What is deliberately NOT here:
 *
 *  - **Drawing.** The renderer pulls out of this struct (see scene.h); nothing
 *    here calls into it. ::World::geometry_dirty is how the one exception --
 *    a door that moved the sectors the level mesh was built from -- gets told
 *    to the platform without this file learning what a Scene is.
 *  - **The menu.** ::Input::paused arrives as a plain flag rather than a call
 *    to menu_is_open(), so a test can freeze the world without operating a UI.
 *  - **The cursor, the window, and the keyboard.** ::Input is already in
 *    metres-and-radians terms: whoever fills it in owns the hardware.
 *
 * 한국어
 * ------
 * @brief 시뮬레이션되는 월드, 그리고 한 프레임이 그것을 진행시키는 순서입니다.
 *
 * 창이 필요하지 않은, 한 프레임이 게임에 하는 모든 일입니다. 레벨·플레이어·무기·플레이가
 * 하나의 구조체인 이유는 호출자가 그중 절반만 들고 있을 수 없게 하기 위함이며,
 * ::world_step이 프레임별 갱신 전체를 담은 하나의 함수인 이유는 그 *순서*를 테스트가
 * 도달할 수 있는 것으로 만들기 위함입니다.
 *
 * 그 순서는 이전에 `WinMain`의 본문 안에 있었고, 이 파일은 그 대가 때문에 존재합니다.
 * main.c 자신의 헤더 주석은 그 순서를 "우연이 아니라 구조적으로 중요하다"고 선언했습니다.
 * 이동 전의 로프 구속, 무엇이든 재생되기 전의 오디오 리스너, 정확히 한 곳에서만 감지되는
 * 사망. 그런데 그중 어느 것도 검사되지 않았습니다. 실행하는 유일한 방법이 창을 열고
 * 플레이하는 것이었기 때문입니다. 이제 tools\steptest.c가 검사하며, 그것이 가능한 유일한
 * 이유는 이 모듈이 GL 함수도, Win32 호출도, 메뉴도 언급하지 않는다는 것입니다.
 *
 * 의도적으로 이곳에 *없는* 것:
 *
 *  - **그리기.** 렌더러가 이 구조체에서 끌어갑니다(scene.h 참조). 이곳의 어떤 것도 렌더러를
 *    호출하지 않습니다. 유일한 예외인 "레벨 메시가 만들어진 섹터를 움직인 문"은
 *    ::World::geometry_dirty를 통해, 이 파일이 Scene이 무엇인지 배우지 않은 채로 플랫폼에
 *    전달됩니다.
 *  - **메뉴.** ::Input::paused가 menu_is_open() 호출이 아니라 단순한 플래그로 도착하므로,
 *    테스트가 UI를 조작하지 않고 월드를 정지시킬 수 있습니다.
 *  - **커서, 창, 키보드.** ::Input은 이미 미터와 라디안의 용어로 되어 있습니다. 그것을
 *    채우는 쪽이 하드웨어를 소유합니다.
 */

#ifndef WORLD_H
#define WORLD_H

/* HOW LONG THE BETWEEN-LEVELS SCREEN STAYS UP, in seconds.
 *
 * A DURATION RATHER THAN A KEYPRESS. Doom's intermission waits for one, and
 * Doom's intermission has tallies to read; ours has two names, and a prompt to
 * dismiss two names is ceremony around nothing. Long enough to read them,
 * short enough that it does not become the thing between the player and the
 * next fight.
 *
 * Public because the screen fades against it: the drawer needs to know when it
 * ends to fade out before it does, and a second constant on the drawing side
 * would be a number that could disagree with the one that actually ends it --
 * a screen that fades to nothing and then sits there, or cuts out mid-fade.
 *
 * 레벨 사이 화면이 떠 있는 시간(초)입니다. 키 입력이 아니라 시간인 이유는, Doom의
 * 인터미션에는 읽을 집계가 있지만 우리 것에는 이름 둘뿐이고 이름 둘을 넘기기 위한 안내는
 * 아무것도 아닌 것을 둘러싼 의식이기 때문입니다.
 *
 * 공개하는 이유는 화면이 이 값에 맞춰 사라지기 때문입니다. 그리는 쪽은 언제 끝나는지 알아야
 * 그 전에 페이드 아웃할 수 있으며, 그리는 쪽의 두 번째 상수는 실제로 화면을 끝내는 값과
 * 어긋날 수 있는 숫자가 됩니다. 다 사라진 뒤 한참 남아 있거나, 페이드 도중에 잘리는
 * 화면이 그 결과입니다. */
#define WORLD_BETWEEN_TIME 2.6f

/**
 * @brief How long the breather between two waves lasts, seconds.
 *
 * ENGLISH
 * -------
 * Longer than ::WORLD_BETWEEN_TIME because it is not a screen to read, it is
 * time to USE: the wave's reward is on the floor and the player has to go and
 * get it. Short enough that it is a breath rather than an interlude -- an arena
 * that stops being tense between waves has waves that are separate fights
 * instead of one accelerating one.
 *
 * Public because the banner counts down against it, for the reason
 * ::WORLD_BETWEEN_TIME is public: a second constant on the drawing side is a
 * number that can disagree with the one that actually ends it.
 *
 * 한국어
 * ------
 * @brief 두 웨이브 사이의 휴식이 지속되는 시간 (초).
 *
 * ::WORLD_BETWEEN_TIME보다 긴 이유는, 그것이 읽을 화면이 아니라 *쓸* 시간이기 때문입니다.
 * 웨이브의 보상이 바닥에 있고 플레이어는 가서 주워야 합니다. 그러면서도 막간이 아니라 한
 * 숨이 되도록 짧습니다. 웨이브 사이에 팽팽함이 풀리는 아레나는, 하나의 가속하는 전투가 아니라
 * 서로 분리된 전투들을 가진 아레나입니다.
 *
 * 공개하는 이유는 배너가 이 값에 맞춰 카운트다운하기 때문이며, ::WORLD_BETWEEN_TIME을
 * 공개하는 이유와 같습니다. 그리는 쪽의 두 번째 상수는 실제로 그것을 끝내는 값과 어긋날 수
 * 있는 숫자입니다.
 */
#define WORLD_WAVE_BREAK 6.0f

/**
 * @brief How much slower the spawners run while a boss stands.
 *
 * ENGLISH
 * -------
 * A SLOWDOWN ADDED TO 1, so 2.0 is a third of the rate. See
 * ::EnemyPool::spawn_slow for why it is expressed that way and why it is read
 * every tick rather than saved and restored.
 *
 * THE PRESSURE DOES NOT DROP, IT MOVES. A boss fight suppresses the arena's own
 * spawners because during it the monsters come from somewhere else: shooting a
 * ward is what fills the room. That is what lets the player set the pace of
 * their own fight -- burst the wards and meet the crowd during the groggy
 * window, or clear as you go and spend longer at it -- and it is why leaving
 * the spawners at full rate would make the fight the sum of two unrelated
 * pressures rather than one the player is steering.
 *
 * 한국어
 * ------
 * @brief 보스가 서 있는 동안 스포너가 얼마나 느려지는가.
 *
 * *1에 더해지는 감속*이므로 2.0은 3분의 1 속도입니다. 왜 그렇게 표현하는지와 왜 저장했다
 * 되돌리지 않고 매 틱 읽는지는 ::EnemyPool::spawn_slow를 참조하십시오.
 *
 * *압박이 줄어드는 것이 아니라 옮겨 갑니다.* 보스전이 아레나 자신의 스포너를 억제하는 이유는,
 * 그동안 몬스터가 다른 곳에서 오기 때문입니다. 결계핵을 쏘는 행위가 방을 채웁니다. 그것이
 * 플레이어가 자기 전투의 박자를 정하게 하는 것이고(결계핵을 몰아쳐 터뜨리고 그로기 창에 무리를
 * 상대하거나, 정리해 가며 나아가고 더 오래 걸리거나), 스포너를 온전한 속도로 두면 이 전투가
 * 플레이어가 조종하는 하나가 아니라 무관한 두 압박의 합이 되는 이유입니다.
 */
#define WORLD_BOSS_SPAWN_SLOW 2.0f

/**
 * @brief Endless mode: waves after a maw dies before the next one is due.
 *
 * ENGLISH
 * -------
 * COUNTED FROM THE DEATH, not from a multiple of itself. A fight that outlasts
 * this many waves would, on a multiple, schedule the next maw into a wave that
 * has already passed -- so the moment the player finally won, the next one
 * would arrive. Counting from the death makes a long fight push its own next
 * appointment out, which is the behaviour a player would describe as "you get a
 * breather".
 *
 * 한국어
 * ------
 * @brief 무한 모드에서 아귀가 죽은 뒤 다음 아귀까지의 웨이브 수.
 *
 * *자기 자신의 배수가 아니라 사망 시점에서부터 셉니다.* 배수로 하면 이만큼의 웨이브보다 오래
 * 끄는 전투가 다음 아귀를 *이미 지나간* 웨이브에 예약하게 되고, 플레이어가 마침내 이긴 그
 * 순간 다음 아귀가 도착합니다. 사망 시점에서부터 세면 긴 전투가 자신의 다음 약속을 밀어내며,
 * 그것이 플레이어가 "한숨 돌릴 틈을 준다"고 말할 동작입니다.
 */
#define WORLD_BOSS_EVERY 5

/**
 * @brief Story mode: the wave the first maw is allowed to arrive on.
 *
 * ENGLISH
 * -------
 * ONE WAVE, AND THE REASON IT IS NOT ZERO IS THAT THE ARENA CHANGED.
 *
 * This gate did not exist. ::step_boss raised the story maw on the first
 * unfrozen frame, and the comment there argued for it: "the story arena IS the
 * boss fight, and a player who has to survive to wave five to meet it is
 * playing endless mode with a cutscene." That was exact reasoning about
 * `glasstower` -- seven brushes, a ring of ward slots around a tower, nothing
 * else in it. There was no room to be dropped into. The room WAS the fight.
 *
 * ::WORLD_BOSS_ARENA is `lqdm1` now: 807 brushes, three wave spawners,
 * twenty-six pickups and 2,614 x 2,016 units of deathmatch map. Measured, the
 * maw stood up one frame after the intro cutscene ended, at wave 1, with four
 * wards already placed and five monsters already walking -- so the player met
 * the boss before reaching a single weapon the map lays out, in a room they had
 * not seen. The old comment's premise died with the old arena, and this is what
 * replaces it.
 *
 * STILL NOT A WAVE GATE IN THE ENDLESS SENSE. Two is one wave, not five: long
 * enough to pick up a gun and learn where the walls are, short enough that the
 * story arena is still the boss fight rather than a survival mode with a
 * cutscene. The sentence that rejected five is still right; it just was not
 * about one.
 *
 * @note Compared against ::RunState::wave, which starts at 1 and becomes 2 when
 *       the first wave is cleared and its breather has run out. So this reads
 *       as "after the opening wave", not "two waves in".
 *
 * 한국어
 * ------
 * @brief 스토리 모드에서 첫 아귀가 도착할 수 있는 웨이브.
 *
 * *한 웨이브이며, 0이 아닌 이유는 아레나가 바뀌었기 때문입니다.*
 *
 * 이 관문은 없었습니다. ::step_boss는 정지가 풀린 첫 프레임에 스토리의 아귀를 세웠고, 그곳의
 * 주석이 그것을 옹호했습니다. "스토리 아레나가 곧 보스전이며, 그것을 만나기 위해 5웨이브를
 * 버텨야 하는 플레이어는 컷신이 붙은 무한 모드를 하고 있는 것이다." 그것은 `glasstower`에
 * 대한 정확한 추론이었습니다. 브러시 일곱 개, 탑을 둘러싼 결계핵 자리의 고리, 그 밖에는
 * 아무것도 없었습니다. 떨어질 방이라는 것이 없었습니다. 방이 *곧* 전투였습니다.
 *
 * ::WORLD_BOSS_ARENA는 이제 `lqdm1`입니다. 브러시 807개, 웨이브 스포너 셋, 획득물 스물여섯,
 * 2,614 x 2,016 크기의 데스매치 맵입니다. 재어 보니 아귀는 인트로 컷신이 끝난 *한 프레임 뒤*에
 * 일어섰습니다. 웨이브 1, 이미 배치된 결계핵 넷, 이미 걷고 있는 몬스터 다섯. 플레이어는 맵이
 * 깔아 둔 무기 하나에 닿기도 전에, 본 적 없는 방에서 보스를 만났습니다. 옛 주석의 전제는 옛
 * 아레나와 함께 죽었고, 이것이 그것을 대신합니다.
 *
 * *여전히 무한 모드적 의미의 웨이브 관문은 아닙니다.* 둘은 다섯이 아니라 한 웨이브입니다.
 * 총 하나를 줍고 벽이 어디 있는지 익히기에는 충분하고, 스토리 아레나가 컷신 붙은 생존 모드가
 * 아니라 여전히 보스전이기에는 짧습니다. 다섯을 거부한 그 문장은 여전히 옳습니다. 다만 그것은
 * 하나에 대한 말이 아니었습니다.
 */
#define WORLD_BOSS_STORY_WAVE 2

/* --- what a cleared wave pays -------------------------------------------
 *
 * ENGLISH
 * -------
 * WORLD_WAVE_MEDKITS and WORLD_WAVE_AMMO used to be here, two medkits and
 * three boxes, and they are now `give health 2` and `give held 3` in
 * assets\loot.txt. What moved is not the numbers -- they are the same numbers
 * -- but where they can be changed from. A reward economy is tuned by playing
 * it and moving it and playing it again, and a number that needs a rebuild
 * between those steps is a number that gets moved once and then left.
 *
 * The reasoning they carried moved with them and is stated where they now
 * live, which is also why it is not restated here: two copies of a rationale
 * is how one of them comes to describe a number that is no longer the number.
 * See ::LootReward, and the file's own header for the shape of the purse.
 *
 * 한국어
 * ------
 * WORLD_WAVE_MEDKITS와 WORLD_WAVE_AMMO가 이곳에 있었고, 구급상자 둘과 상자 셋이었으며,
 * 이제 assets\loot.txt의 `give health 2`와 `give held 3`입니다. 옮겨 간 것은 숫자가
 * 아니라(같은 숫자입니다) 그것을 어디에서 바꿀 수 있는가입니다. 보상 경제는 플레이해 보고
 * 옮기고 다시 플레이해 보며 조율하는 것이며, 그 사이에 재빌드를 요구하는 숫자는 한 번
 * 옮겨진 뒤 그대로 남는 숫자입니다.
 *
 * 그것들이 지니고 있던 근거도 함께 옮겨 가 지금 사는 곳에 적혀 있으며, 이곳에 다시 적지
 * 않는 이유도 그것입니다. 근거의 사본이 둘이라는 것은 그중 하나가 더 이상 그 숫자가 아닌
 * 숫자를 설명하게 되는 방식입니다. ::LootReward와 그 파일 자신의 머리글을 참조하십시오. */


#include "level.h"
#include "player.h"
#include "weapon.h"
#include "run.h"
#include "pools.h"   /* Pools: what a run spawns, owned here rather than by the modules */

/* ------------------------------------------------------------------ config */

/**
 * @brief Horizontal field of view for the world camera, radians. 90 degrees.
 *
 * ENGLISH
 * -------
 * Here rather than in main.c because both sides need the same number: the
 * renderer builds its projection from it, and ::world_step hands it to
 * ::wp_update, which solves the muzzle's world position by projecting the view
 * model's barrel through the same frustum. Two copies would put the tracer
 * somewhere the gun is not pointing.
 *
 * 한국어
 * ------
 * @brief 월드 카메라의 수평 시야각 (라디안). 90도입니다.
 *
 * main.c가 아니라 이곳에 있는 이유는 양쪽이 같은 값을 필요로 하기 때문입니다. 렌더러가
 * 이것으로 투영을 구성하고, ::world_step이 이것을 ::wp_update에 넘기면 그것이 뷰 모델
 * 총구를 같은 절두체로 투영해 총구의 월드 위치를 구합니다. 사본이 두 개면 예광탄이 총이
 * 가리키지 않는 곳으로 나갑니다.
 */
#define WORLD_FOV 1.5708f

/* --- view shake ------------------------------------------------------------
 *
 * ENGLISH
 * -------
 * WHAT SHAKES AND WHAT DOES NOT. The drawn camera shakes; the aim does not.
 * See ::RunState::shake for why that separation is not negotiable.
 *
 * The three sources are the three moments the player already knows something
 * violent happened, so the shake confirms an event rather than inventing one:
 * the gun going off, taking a hit, and landing hard. Each is detected in
 * ::world_step from state that already exists -- the muzzle flash coming up,
 * the damage total, the ground arriving under a falling player -- so none of
 * them needed a new signal plumbed out of the module that causes it.
 *
 * Public because ::scene_frame reads the magnitude to place the camera, and
 * because a second copy of the decay rate on the drawing side would be a
 * number that could disagree with the one that actually ends the shake.
 *
 * 한국어
 * ------
 * *무엇이 흔들리고 무엇이 흔들리지 않는가.* 그려지는 카메라는 흔들리고 조준은 흔들리지
 * 않습니다. 그 분리가 타협 불가능한 이유는 ::RunState::shake를 참조하십시오.
 *
 * 세 개의 원천은 플레이어가 이미 격렬한 일이 일어났음을 아는 세 순간이므로, 흔들림은 사건을
 * 만들어 내지 않고 확인해 줍니다. 총이 발사되는 것, 피격당하는 것, 세게 착지하는 것입니다.
 * 각각은 이미 존재하는 상태로부터 ::world_step에서 감지됩니다. 올라오는 총구 섬광, 피해
 * 총량, 떨어지는 플레이어 밑에 도착하는 지면입니다. 그래서 그중 어느 것도 그것을 일으키는
 * 모듈로부터 새 신호를 끌어낼 필요가 없었습니다.
 *
 * 공개하는 이유는 ::scene_frame이 카메라를 놓기 위해 크기를 읽기 때문이며, 그리는 쪽의 두
 * 번째 감쇠율 사본은 실제로 흔들림을 끝내는 값과 어긋날 수 있는 숫자이기 때문입니다. */

/** @brief How fast a shake dies, per second. / 흔들림이 잦아드는 속도 (초당). */
#define WORLD_SHAKE_DECAY 5.5f

/** @brief Ceiling, so no pile-up leaves the camera in the next room. / 상한. 누적이 카메라를 옆방에 두지 않도록 합니다. */
#define WORLD_SHAKE_MAX   1.0f

/** @brief A shot. Small: it happens constantly and must not become the game. / 사격. 작습니다. 끊임없이 일어나므로 게임 그 자체가 되어서는 안 됩니다. */
#define WORLD_SHAKE_FIRE  0.30f

/**
 * @brief Taking a hit, at the point where one hit is the whole bar.
 *
 * ENGLISH: Scaled by the fraction of maximum health the hit took, so a scratch
 * registers and a mauling is unmistakable. That is the one place a number the
 * player watches -- their health -- gets to drive the camera.
 *
 * 한국어: 그 피격이 가져간 최대 체력의 비율로 조정하므로, 스치는 상처도 등록되고 크게
 * 물어뜯긴 것은 착각할 수 없습니다. 플레이어가 지켜보는 숫자인 체력이 카메라를 구동하는
 * 유일한 지점입니다.
 */
#define WORLD_SHAKE_HURT  1.30f

/** @brief Landing, scaled by how fast the ground arrived. / 착지. 지면이 얼마나 빨리 도착했는지로 조정합니다. */
#define WORLD_SHAKE_LAND  0.85f

/**
 * @brief An explosion at its centre, before the distance is taken off.
 *
 * ENGLISH
 * -------
 * THE LOUDEST THING IN THE LIST, and it has to be: the other three are events
 * the player's own hands produced, and this one is a room going up. Below
 * ::WORLD_SHAKE_HURT on purpose, so that being torn into still outranks
 * standing near something going off -- the hurt shake is the one that must
 * never be mistaken for scenery.
 *
 * Just under ::WORLD_SHAKE_MAX rather than at it, so the ordering is one the
 * player can actually feel rather than one the clamp erases: a hit that takes
 * the whole bar is the only thing in the game that reaches the ceiling.
 *
 * At the rim of the damage this is already down to two thirds of what it is at
 * the centre, which is the number that matters: a grenade the player survived
 * by standing outside its radius still has to be felt, or the radius was never
 * taught.
 *
 * 한국어
 * ------
 * @brief 폭발의 중심에서의 세기. 거리에 따른 감쇠 이전 값입니다.
 *
 * *목록에서 가장 큰 값이며* 그래야 합니다. 나머지 셋은 플레이어 자신의 손이 만든 사건이고,
 * 이것은 방 하나가 통째로 터지는 것입니다. 의도적으로 ::WORLD_SHAKE_HURT보다는 아래인데,
 * 물어뜯기는 것이 무언가가 터지는 곁에 서 있는 것보다 여전히 위여야 하기 때문입니다. 피격의
 * 흔들림은 결코 배경으로 오인되어서는 안 되는 흔들림입니다.
 *
 * ::WORLD_SHAKE_MAX와 같지 않고 그 바로 아래인 이유는, 그 순서가 제한에 의해 지워지지 않고
 * 플레이어가 실제로 느낄 수 있는 것이 되도록 하기 위함입니다. 상한에 도달하는 것은 체력 바를
 * 통째로 가져가는 피격 하나뿐입니다.
 *
 * 피해 반경의 가장자리에서 이 값은 이미 중심에서의 3분의 2 수준으로 떨어지며, 중요한 것이
 * 그 숫자입니다. 반경 밖에 서서 살아남은 유탄도 몸으로 느껴져야 합니다. 그러지 않으면 그
 * 반경은 애초에 가르쳐진 적이 없는 것입니다.
 */
#define WORLD_SHAKE_BLAST 0.95f

/**
 * @brief How far past the damage radius a blast is still felt, as a multiple.
 *
 * ENGLISH
 * -------
 * THE SHAKE REACHES FURTHER THAN THE DAMAGE, and the gap between the two is
 * the point of the number rather than a rounding of it. Inside the radius the
 * player is told by their health bar; outside it there is nothing else to say
 * "that was close" -- and a blast whose felt edge and damaging edge were the
 * same edge would teach the opposite lesson, that anything you can feel has
 * already hurt you.
 *
 * Three is what a room is worth: a grenade's 4.2m becomes 12.6m of tremor,
 * which is most of an arena and none of the level. Linear falloff to nothing
 * at that edge, for the reason ::proj_blast gives about its own curve -- a
 * squared one spends its last two thirds below what anybody can feel, so the
 * reach would be a number that reads as much shorter than it is.
 *
 * 한국어
 * ------
 * @brief 폭발이 여전히 느껴지는, 피해 반경에 대한 배수.
 *
 * *흔들림은 피해보다 멀리 닿으며*, 둘 사이의 간격이 이 수치를 반올림한 것이 아니라 수치의
 * 존재 이유입니다. 반경 안에서는 체력 표시가 말해 주지만, 반경 밖에서 "아슬아슬했다"고 말해 줄
 * 것은 달리 없습니다. 그리고 느껴지는 가장자리와 상하게 하는 가장자리가 같은 폭발은 정반대의
 * 교훈을 가르칩니다. 느껴진다면 이미 맞은 것이라는.
 *
 * 3은 방 하나에 해당하는 값입니다. 유탄의 4.2미터는 12.6미터의 진동이 되며, 그것은 아레나의
 * 대부분이자 레벨의 일부에 지나지 않습니다. 그 가장자리에서 0이 되는 선형 감쇠이며, 이유는
 * ::proj_blast가 자기 곡선에 대해 말하는 것과 같습니다. 제곱 곡선은 마지막 3분의 2를 아무도
 * 느낄 수 없는 값으로 보내므로, 도달 거리가 실제보다 훨씬 짧게 읽히는 숫자가 됩니다.
 */
#define WORLD_SHAKE_BLAST_REACH 3.0f

/**
 * @brief Downward speed a landing needs before it shakes anything, m/s.
 *
 * ENGLISH: Walking off a step is not an impact. Without a floor here every
 * stair in the level would jolt the camera, which reads as the engine being
 * broken rather than as the player being heavy.
 *
 * 한국어: 계단 하나를 내려서는 것은 충격이 아닙니다. 이 하한이 없으면 레벨의 모든 계단이
 * 카메라를 흔들며, 그것은 플레이어가 무겁다는 뜻이 아니라 엔진이 고장 났다는 뜻으로 읽힙니다.
 */
#define WORLD_SHAKE_LAND_MIN 7.0f

/**
 * @brief Seconds after which ::RunState::world_time wraps.
 *
 * ENGLISH
 * -------
 * Twenty whole periods of the animated material shader's own pulse
 * (`20 * 2*pi / 0.45`), so the pulse is continuous across the reset. The
 * shader's DRIFT term is an unbounded scroll and cannot be made seamless this
 * way -- it jumps once every wrap. At four and a half minutes between jumps,
 * on a surface whose whole appearance is churning noise, that beats letting
 * float precision run out and the flow visibly step.
 *
 * 한국어
 * ------
 * @brief ::RunState::world_time이 순환하는 주기 (초).
 *
 * 애니메이션 재질 셰이더 자체 맥동의 정수 20주기(`20 * 2*pi / 0.45`)이므로, 맥동은
 * 초기화를 넘어 연속입니다. 셰이더의 DRIFT 항은 경계 없는 스크롤이라 이 방식으로 매끄럽게
 * 만들 수 없으며 순환마다 한 번 튑니다. 4분 30초에 한 번, 그것도 외형 전체가 끓는
 * 노이즈인 표면에서라면, float 정밀도가 바닥나며 흐름이 눈에 띄게 끊기는 것보다 낫습니다.
 */
#define WORLD_TIME_WRAP 279.253f

/** @brief Capacity of ::World::cur_level, bytes. / ::World::cur_level의 크기 (바이트). */
#define WORLD_LEVEL_MAX 32

/**
 * @brief The level a fresh ::World starts in.
 *
 * ENGLISH
 * -------
 * THE SAME ROOM THE GAME IS PLAYED IN, and it is one map because one map is
 * what this game ships.
 *
 * It was `spire`, a hand-authored arena kept solely to be the thing the title
 * menu is drawn over. ::WORLD_BOSS_ARENA carried a note arguing exactly against
 * pointing this at the arena -- "loading the arena to stand behind a menu would
 * mean the room the player is about to be dropped into has already been walked
 * through by nobody, and its spawners are one `title = 0` away from arming."
 *
 * BOTH HALVES OF THAT HAVE ANSWERS NOW.
 *
 * The spawners cannot arm from here. ::step_confirm is the only thing in the
 * tree that clears ::RunState::title, and main.c's `screen_takes_press` returns
 * 0 whenever a menu is open -- ::MENU_TITLE always is. The one path that
 * reaches it is demo playback, which skips ::menu_open_title and drives
 * recorded input, and a recording made in this room replays in this room.
 * Choosing STORY does not reuse the backdrop either: ::world_begin loads the
 * arena again with ::WORLD_ENTER_NEW, which is a fresh level and a cleared run.
 *
 * And "walked through by nobody" was an argument about `glasstower`, a
 * seven-brush greybox tower nobody would want behind a menu. The arena is
 * LibreQuake's Solstice now. A finished deathmatch map is a better backdrop
 * than a room kept alive to be one.
 *
 * @warning THIS IS NO LONGER THE HEAD OF THE CAMPAIGN, and has not been since
 *          before this edit. ::world_progress_for_stage walks from HERE, so
 *          with an arena at the front it finds a chain of one.
 *          ::WORLD_CHAIN_ROOT is the other half, and the tests that check the
 *          progression machinery use it. The old campaign levels still load by
 *          name; nothing reaches them by walking forward.
 *
 * 한국어
 * ------
 * @brief 새 ::World가 시작하는 레벨.
 *
 * *게임이 플레이되는 바로 그 방*이며, 맵 하나인 이유는 이 게임이 출하하는 것이 맵 하나이기
 * 때문입니다.
 *
 * 이것은 `spire`였습니다. 타이틀 메뉴가 그 위에 그려질 대상이 되기 위해서만 유지되던, 손으로
 * 만든 아레나입니다. ::WORLD_BOSS_ARENA에는 이곳을 아레나로 향하게 하는 것에 정확히 반대하는
 * 각주가 달려 있었습니다. "메뉴 뒤에 세우려고 아레나를 로드하면, 플레이어가 곧 떨어질 방을
 * 아무도 걸어 보지 않은 채 이미 지나간 것이 되고, 그 스포너들은 `title = 0` 하나 거리에서
 * 장전을 기다리게 된다."
 *
 * *그 두 절반 모두 이제 답을 가지고 있습니다.*
 *
 * 스포너는 이곳에서 장전될 수 없습니다. 트리에서 ::RunState::title을 지우는 것은 ::step_confirm
 * 하나뿐이고, main.c의 `screen_takes_press`는 메뉴가 열려 있으면 언제나 0을 반환하며
 * ::MENU_TITLE은 언제나 열려 있습니다. 그곳에 도달하는 유일한 경로는 데모 재생이고, 그것은
 * ::menu_open_title을 건너뛰며 기록된 입력을 구동합니다. 이 방에서 만든 기록은 이 방에서
 * 재생됩니다. STORY를 골라도 배경을 재사용하지 않습니다. ::world_begin이 ::WORLD_ENTER_NEW로
 * 아레나를 다시 로드하며, 그것은 새 레벨과 지워진 플레이입니다.
 *
 * 그리고 "아무도 걸어 보지 않은"은 `glasstower`에 대한 논거였습니다. 메뉴 뒤에 두고 싶지 않을
 * 브러시 일곱 개짜리 그레이박스 탑입니다. 아레나는 이제 LibreQuake의 Solstice입니다. 완성된
 * 데스매치 맵은, 배경이 되기 위해 살려 둔 방보다 나은 배경입니다.
 */
/**
 * @brief What ::WORLD_START_LEVEL is when nothing overrides it.
 *
 * ENGLISH
 * -------
 * SPLIT FROM THE NAME ABOVE so a build can boot somewhere else without editing
 * the constant every test reads. It had one caller: build.ps1's dev build sent
 * itself into the boss arena because there was no other way in, and a fight
 * nothing can reach is a fight nobody can playtest.
 *
 * THAT OVERRIDE IS GONE, and this paragraph is what is left of it. ::MENU_TITLE
 * landed, so the mode is chosen there and the arena is loaded by
 * ::world_begin -- which is where "which room is which mode" belongs, because
 * it is a fact about a run rather than about the binary. What this constant now
 * names is only the backdrop the title screen is drawn over: a real level,
 * loaded and frozen, for ::RunState::title's own reason.
 *
 * 한국어
 * ------
 * @brief 아무것도 덮어쓰지 않을 때 ::WORLD_START_LEVEL이 무엇인가.
 *
 * *위의 이름에서 갈라냈습니다.* 모든 테스트가 읽는 상수를 고치지 않고도 빌드가 다른 곳으로
 * 부팅할 수 있게 하기 위함입니다. 호출자가 하나 있었습니다. build.ps1의 개발 빌드가 스스로를
 * 보스 아레나로 보냈는데, 다른 입구가 없었기 때문입니다. 아무것도 도달할 수 없는 전투는 아무도
 * 플레이테스트할 수 없는 전투입니다.
 *
 * *그 덮어쓰기는 사라졌고* 이 문단이 그것의 남은 자취입니다. ::MENU_TITLE이 들어왔으므로 모드는
 * 그곳에서 정해지고 아레나는 ::world_begin이 로드합니다. "어느 방이 어느 모드인가"가 있어야 할
 * 곳이 그곳입니다. 바이너리가 아니라 *플레이*에 대한 사실이기 때문입니다. 이제 이 상수가
 * 이름 짓는 것은 타이틀 화면이 그 위에 그려지는 배경뿐입니다. ::RunState::title 자신의 이유에
 * 따라 실제로 로드되어 정지한 레벨입니다.
 */
#define WORLD_START_DEFAULT "lqdm1"

#ifndef WORLD_START_LEVEL
#define WORLD_START_LEVEL WORLD_START_DEFAULT
#endif

/**
 * @brief The room both modes are fought in.
 *
 * ENGLISH
 * -------
 * ONE ROOM, TWO MODES, and that is not an economy -- it is the constraint the
 * whole of ::RunState::endless exists to satisfy. Story and endless are the
 * same arena, so a level name cannot tell them apart, so the mode has to be a
 * property of how the run was entered. See ::RunState::endless, and note that
 * this constant is the reason it could not have been anything else.
 *
 * @note IT IS ::WORLD_START_DEFAULT NOW, and this note used to say the
 *       opposite. It argued that loading the arena to stand behind a menu
 *       "would mean the room the player is about to be dropped into has
 *       already been walked through by nobody, and its spawners are one
 *       `title = 0` away from arming." The spawners cannot arm from there --
 *       see ::WORLD_START_DEFAULT for the path-by-path reason -- and the
 *       aesthetic half was about `glasstower`. One map is what ships.
 * @note Named here rather than passed in by main.c, because a test that starts
 *       a run has to name the same room the game does. A tool spelling
 *       the arena's name into its fixture is a second copy of a fact that moves.
 *
 * 한국어
 * ------
 * @brief 두 모드가 함께 치러지는 방.
 *
 * *방 하나에 모드 둘*이며, 이것은 절약이 아닙니다. ::RunState::endless 전체가 만족시키려고
 * 존재하는 제약입니다. 스토리와 무한은 같은 아레나이므로 레벨 이름으로 둘을 구별할 수 없고,
 * 따라서 모드는 플레이에 들어온 방식의 성질이어야 합니다. ::RunState::endless를 참조하시고, 이
 * 상수가 그것이 다른 무엇일 수 없었던 이유임에 유의하십시오.
 *
 * @note *이제 ::WORLD_START_DEFAULT입니다.* 이 각주는 반대를 말하고 있었습니다. 스포너는
 *       그곳에서 장전될 수 없으며(경로별 이유는 ::WORLD_START_DEFAULT를 참조하십시오), 미학에
 *       대한 절반은 `glasstower`에 대한 것이었습니다. 출하되는 것은 맵 하나입니다.
 * @note main.c가 넘겨주지 않고 이곳에서 이름 짓는 이유는, 플레이를 시작하는 테스트가 게임과
 *       같은 방을 지목해야 하기 때문입니다. 픽스처에 아레나의 이름을 적어 넣는 도구는 움직이는
 *       사실의 두 번째 사본입니다.
 */
/* WHY `lqdm1` AND NOT `glasstower`, WHICH THIS WAS FOR MOST OF ITS LIFE.
 *
 * glasstower is seven brushes. It was authored to prove the boss fight works --
 * a ring of ward slots around a tower, at coordinates written by hand into a
 * .map -- and it did that job. It is not a room anybody would want to fight
 * fifteen waves in, and it was never claimed to be.
 *
 * `lqdm1` is LibreQuake's "Solstice" by ZungryWare, BSD-3, 807 brushes, and the
 * map LibreQuake's own docs/deathmatch-setup-guide.txt tells a first-time host
 * to start on. It won its slot inside that project by being finished: commit
 * 8ae29a30, Dec 2023, "Detailed lqdm1; switched lqdm1 with lqdm11".
 *
 * THE MAP IT DISPLACED THERE IS THE MAP THIS GAME HAD IMPORTED. `lqdm11` and
 * `lqdm13` are the pack's GREYBOXES and it is measurable rather than a matter
 * of taste: 99.1% and 91.1% of their faces carry lq_dev.wad development
 * textures, against 0.0-0.2% for every other map in the pack. They were chosen
 * here by "the smallest file that fitted the caps", which is what the caps were
 * for and is not a quality measure. Neither was ever entered by the game --
 * WORLD_BOSS_ARENA pointed at glasstower and WORLD_START_LEVEL at spire -- so
 * they cost 47KB of a 1.44MB budget to ship two rooms nobody could reach.
 *
 * WHY NOT A FAMOUS ONE. Because there is not one to have. Every famous arena in
 * this lineage -- aerowalk, ztndm3, id's own dm2/dm4/dm6 -- is either unlicensed
 * (which is all rights reserved) or GPL-2, and this game bakes its maps INTO the
 * executable, so a copyleft map is a copyleft binary. README.md carries the
 * survey. Within what a permissive licence actually allows, "most famous"
 * has no answer and "the one the project itself puts forward" does.
 *
 * *왜 `glasstower`가 아니라 `lqdm1`인가.* glasstower는 브러시 일곱 개입니다. 보스 전투가
 * 작동함을 증명하려고 만들어졌고 그 일을 해냈습니다. 웨이브 열다섯을 싸우고 싶은 방은 아니며,
 * 그렇다고 주장된 적도 없습니다.
 *
 * `lqdm1`은 ZungryWare의 LibreQuake "Solstice"이고 BSD-3이며 브러시 807개이고, LibreQuake 자신의
 * docs/deathmatch-setup-guide.txt가 처음 방을 여는 사람에게 시작하라고 말하는 맵입니다. 그
 * 프로젝트 안에서 *완성되었다는 이유로* 그 자리를 얻었습니다.
 *
 * *그곳에서 그것이 밀어낸 맵이 이 게임이 가져왔던 맵입니다.* `lqdm11`과 `lqdm13`은 팩의
 * *그레이박스*이며 이는 취향이 아니라 측정 가능한 사실입니다. 두 맵의 면 중 99.1%와 91.1%가
 * lq_dev.wad 개발용 텍스처를 지니는 반면 팩의 다른 모든 맵은 0.0~0.2%입니다. 이곳에서 그것들이
 * 선택된 기준은 "상한에 들어가는 가장 작은 파일"이었고, 그것은 상한이 하는 일이지 품질의
 * 척도가 아닙니다.
 *
 * *왜 유명한 것이 아닌가.* 가질 수 있는 것이 없기 때문입니다. 이 계보의 유명한 아레나는 전부
 * 라이선스가 없거나(모든 권리 유보와 같습니다) GPL-2이며, 이 게임은 맵을 실행 파일 *안에*
 * 굽습니다. 따라서 카피레프트 맵은 카피레프트 바이너리입니다. */
#define WORLD_BOSS_ARENA "lqdm1"

/**
 * @brief Where the level text's `next` chain begins.
 *
 * ENGLISH
 * -------
 * Split from ::WORLD_START_LEVEL when the game stopped booting into the
 * campaign. Nothing in src reads this: the chain-walking machinery takes a name
 * from its caller and has no opinion about which one. It is here rather than
 * spelled into each test because it is a fact about the shipped CONTENT, and a
 * fact stated in three test files is a fact that will be true in two of them.
 *
 * @note If the campaign is ever retired outright, this constant and the checks
 *       that use it go together -- which is the point of naming it. A chain
 *       nothing walks is easier to notice than a chain three tests walk from
 *       three separately spelled starting points.
 *
 * 한국어
 * ------
 * @brief 레벨 텍스트의 `next` 사슬이 시작되는 곳.
 *
 * 게임이 캠페인으로 부팅하기를 그만두었을 때 ::WORLD_START_LEVEL에서 갈라져 나왔습니다. src의
 * 어떤 것도 이것을 읽지 않습니다. 사슬을 걷는 기구는 호출자에게서 이름을 받으며 그것이 어느
 * 것인지에 대해 아무 견해도 갖지 않습니다. 각 검사에 적어 넣지 않고 이곳에 두는 이유는 이것이
 * *출하되는 콘텐츠*에 대한 사실이기 때문이며, 세 개의 검사 파일에 적힌 사실은 그중 둘에서만
 * 참이 될 사실이기 때문입니다.
 *
 * @note 캠페인을 아예 은퇴시킨다면 이 상수와 그것을 쓰는 검사들이 함께 갑니다. 이름을 붙이는
 *       요점이 그것입니다. 아무도 걷지 않는 사슬은, 세 검사가 각각 따로 적은 출발점에서 걷는
 *       사슬보다 알아채기 쉽습니다.
 */
#define WORLD_CHAIN_ROOT "arena"

/**
 * @brief How far ::world_progress_for_stage will walk the `next` chain.
 *
 * ENGLISH
 * -------
 * A bound rather than a trust. `next` is authored text and nothing stops one
 * stage naming an earlier one -- a hub that loops back is a reasonable thing to
 * build -- so the walk has to end whether or not the target is on the chain. A
 * number far past any episode this game will ship, because being too small is a
 * stage that cannot be selected and being too large costs a few loads that were
 * going to fail anyway.
 *
 * 한국어
 * ------
 * @brief ::world_progress_for_stage가 `next` 사슬을 따라갈 최대 횟수입니다.
 *
 * 신뢰가 아니라 상한입니다. `next`는 제작된 텍스트이고 한 스테이지가 앞선 스테이지를
 * 지목하는 것을 막는 것은 없습니다(되돌아오는 허브는 만들 만한 구조입니다). 따라서 대상이
 * 사슬 위에 있든 없든 순회는 끝나야 합니다. 이 게임이 출시할 어떤 에피소드보다도 훨씬 큰
 * 값인 이유는, 너무 작으면 고를 수 없는 스테이지가 생기고 너무 크면 어차피 실패할 로드를 몇
 * 번 더 하는 비용에 그치기 때문입니다.
 */
#define WORLD_STAGE_MAX_HOPS 64

/* ------------------------------------------------------------------- input */

/**
 * @struct Input
 * @brief One frame of intent, in the world's own terms.
 *
 * ENGLISH
 * -------
 * Deliberately not a key array. Everything here is already a decision -- "the
 * player wants to go forward", not "the W key is down" -- so the platform owns
 * the mapping and ::world_step owns what the intent means. It is also what
 * makes a test able to walk a player into a wall in three lines.
 *
 * @note MOST fields are a HELD state; the three at the end are EDGES, latched
 *       by the platform and consumed here. The distinction that matters is not
 *       held-versus-edge but POLLED-versus-LATCHED: a held key read once a
 *       frame would walk a whole menu in a frame, which is why an edge may not
 *       be polled -- but a latch set once by the message and cleared once by
 *       the step is still exactly one press, one step.
 *
 *       They used to be written straight into the ::World from the window
 *       procedure, which meant the rules they carry -- what a number key does,
 *       what dismisses the death screen -- lived somewhere no headless tool
 *       could reach. Everything else worth checking in this project is
 *       reachable from tools\steptest.c; these were the hole.
 * @note WHAT IS DELIBERATELY NOT HERE: the menu keys and the F1 pixelise
 *       toggle. Neither touches a ::World. The menu has its own state and its
 *       own module, and the post-process pass is a property of the window's
 *       render target in the same way the display mode is. An edge belongs
 *       here when the SIMULATION is what it changes, not merely because it is
 *       an edge.
 *
 * 한국어
 * ------
 * @brief 월드 자신의 용어로 표현된 한 프레임의 의도입니다.
 *
 * 의도적으로 키 배열이 아닙니다. 이곳의 모든 것은 이미 판단입니다. "W 키가 눌려 있다"가
 * 아니라 "플레이어가 전진하려 한다"입니다. 따라서 대응 관계는 플랫폼이 소유하고, 그 의도가
 * 무엇을 뜻하는지는 ::world_step이 소유합니다. 또한 이것이 테스트가 세 줄로 플레이어를
 * 벽에 걸어 들어가게 할 수 있는 이유입니다.
 *
 * @note *대부분의* 필드는 유지 상태이며, 끝의 셋은 *엣지*로서 플랫폼이 래치하고 이곳이
 *       소비합니다. 중요한 구분은 유지냐 엣지냐가 아니라 *폴링이냐 래치냐*입니다. 프레임마다
 *       읽히는 유지 키는 한 프레임에 메뉴 전체를 지나가며, 그것이 엣지를 폴링해서는 안 되는
 *       이유입니다. 그러나 메시지가 한 번 세우고 스텝이 한 번 지우는 래치는 여전히 정확히 한
 *       번 누름, 한 단계입니다.
 *
 *       이들은 이전에 창 프로시저에서 ::World로 곧장 기록되었고, 그 말은 이들이 나르는
 *       규칙(숫자 키가 무엇을 하는가, 무엇이 사망 화면을 해제하는가)이 어떤 헤드리스 도구도
 *       닿을 수 없는 곳에 있었다는 뜻입니다. 이 프로젝트에서 검사할 가치가 있는 다른 모든 것은
 *       tools\steptest.c에서 도달할 수 있었고, 이들이 그 구멍이었습니다.
 * @note 의도적으로 이곳에 *없는* 것: 메뉴 키와 F1 픽셀화 전환입니다. 둘 다 ::World를 건드리지
 *       않습니다. 메뉴는 자기 상태와 자기 모듈을 갖고, 포스트 프로세스 패스는 디스플레이 모드와
 *       같은 의미에서 창의 렌더 타깃에 속한 성질입니다. 엣지가 이곳에 속하는 것은 그것이 엣지여서가
 *       아니라 그것이 바꾸는 대상이 *시뮬레이션*일 때입니다.
 */
typedef struct {
    /**
     * @brief Mouse movement since the last frame, in pixels.
     *
     * ENGLISH: Raw pixels rather than radians, because two things want it and
     * they want it differently: yaw/pitch scale it by the sensitivity, and the
     * view model's sway is a spring driven by the raw flick. It also has to
     * arrive even on frames the look is locked -- see ::Input::hook.
     *
     * 한국어: 라디안이 아닌 원시 픽셀입니다. 두 곳이 이것을 원하지만 원하는 방식이 다르기
     * 때문입니다. yaw/pitch는 감도를 곱하고, 뷰 모델의 스웨이는 원시 이동량으로 구동되는
     * 스프링입니다. 또한 시점이 고정된 프레임에도 도착해야 합니다. ::Input::hook을
     * 참조하십시오.
     */
    float look_dx, look_dy;

    int forward;  /**< Held: walk along the view direction. / 유지: 시선 방향으로 전진. */
    int back;     /**< Held: walk against it. / 유지: 시선 반대 방향으로 후진. */
    int left;     /**< Held: strafe left. / 유지: 좌측 횡이동. */
    int right;    /**< Held: strafe right. / 유지: 우측 횡이동. */
    int jump;     /**< Held: jump when grounded. / 유지: 지면에 있으면 점프. */

    /** @brief Held: the trigger. Already gated on window focus by the caller. / 유지: 방아쇠. 호출자가 이미 창 포커스로 걸러 놓습니다. */
    int fire;

    /**
     * @brief Held: right mouse -- whatever the weapon in hand says it means.
     *
     * ENGLISH: Guns throw the grapple, the axe leaps. Routed on the weapon's
     * `hook` column and not on `cur == WP_AXE`, so a later weapon that also
     * leaps is a table row rather than an edit inside ::world_step.
     *
     * 한국어: 총기는 그래플을 던지고 도끼는 도약합니다. `cur == WP_AXE`가 아니라 무기의
     * `hook` 열로 분배하므로, 나중에 도약하는 무기가 생겨도 ::world_step 내부의 수정이
     * 아니라 표의 행 하나가 됩니다.
     */
    int hook;

    /**
     * @brief Non-zero while a UI has the world stopped.
     *
     * ENGLISH: A flag rather than a call to menu_is_open(), so this module has
     * no opinion about menus and a test can freeze the world by assigning to a
     * struct. Folded into ::world_frozen with the run's own end states,
     * because all of them stop exactly the same things.
     *
     * 한국어: menu_is_open() 호출이 아니라 플래그입니다. 그래서 이 모듈은 메뉴에 대해
     * 아무 견해도 갖지 않으며, 테스트는 구조체에 대입하는 것만으로 월드를 정지시킬 수
     * 있습니다. 플레이 자체의 종료 상태들과 함께 ::world_frozen으로 접히는 이유는, 그
     * 전부가 정확히 같은 것들을 정지시키기 때문입니다.
     */
    int paused;

    /* --- edges: latched once by the platform, consumed once by ::world_step --
       All three are read OUTSIDE the ::world_frozen gate, and they have to be:
       the states they act on -- the title screen, the death screen, an open
       menu -- are exactly the states that freeze the world. An edge gated on
       `!frozen` would be an edge that can never fire on the screen it is for.
       셋 모두 ::world_frozen 게이트 *바깥*에서 읽히며, 그래야만 합니다. 이들이 작용하는
       상태(타이틀 화면, 사망 화면, 열린 메뉴)가 바로 월드를 정지시키는 상태이기 때문입니다.
       `!frozen`으로 막힌 엣지는 정작 자신을 위한 화면에서 결코 발생할 수 없는 엣지입니다. */

    /**
     * @brief The player pressed something to acknowledge the screen in front of them.
     *
     * ENGLISH
     * -------
     * ONE FIELD FOR BOTH SCREENS, because from the platform's side they are one
     * event: a key or a click arrived and no menu wanted it. What it MEANS is a
     * question about the run -- the title screen starts the game, the death
     * screen retries it -- and that is a rule, so it lives in ::world_step
     * rather than in the window procedure that used to answer it.
     *
     * @note The death screen ignores this until ::DEATH_INPUT_DELAY has passed.
     *       The shot that killed the player is very often still held, and
     *       restarting on it reads as the game skipping the death screen
     *       entirely. That rule was unreachable from a test until this field
     *       existed; ::world_step is where it is now.
     * @note NOT the menu's RESTART row. That one is unconditional and arrives
     *       through ::RunState::restart_wanted, because a player who picked a
     *       row from a menu has already been asked whether they meant it.
     *
     * 한국어
     * ------
     * @brief 플레이어가 눈앞의 화면에 응답하려고 무언가를 눌렀습니다.
     *
     * 두 화면에 대해 필드가 하나인 이유는, 플랫폼 쪽에서 보면 그것이 하나의 사건이기
     * 때문입니다. 키나 클릭이 도착했고 어떤 메뉴도 그것을 원하지 않았습니다. 그것이 무엇을
     * *뜻하는지*는 플레이에 대한 질문이며(타이틀 화면은 게임을 시작하고 사망 화면은 다시
     * 시도합니다) 그것은 규칙이므로, 이전에 그 답을 내리던 창 프로시저가 아니라
     * ::world_step에 있습니다.
     *
     * @note 사망 화면은 ::DEATH_INPUT_DELAY가 지나기 전까지 이것을 무시합니다. 플레이어를 죽인
     *       그 사격은 대개 아직 눌린 상태이며, 그것으로 재시작되면 게임이 사망 화면을 통째로
     *       건너뛴 것처럼 보입니다. 그 규칙은 이 필드가 생기기 전까지 테스트에서 도달할 수
     *       없었습니다. 이제 그것이 있는 곳은 ::world_step입니다.
     * @note 메뉴의 RESTART 행은 아닙니다. 그쪽은 조건이 없으며
     *       ::RunState::restart_wanted를 통해 도착합니다. 메뉴에서 행을 고른 플레이어는 이미
     *       그럴 의도였는지 질문을 받은 셈이기 때문입니다.
     */
    int confirm;

    /**
     * @brief Which weapon the player asked for. ONE-BASED; 0 means they did not.
     *
     * ENGLISH
     * -------
     * One-based so that `Input in = {0}` is "no request" rather than "select
     * weapon 0", which is the convention ::Pools, ::RunState and ::DoorSet all
     * keep: a zeroed struct has to be a valid empty state, or every fixture
     * that builds one field by field has to know about this one. It also
     * happens to be the number the player actually pressed.
     *
     * @note Refused silently for a weapon that is not owned. An empty hand is
     *       worse than nothing happening, and it keeps the number keys harmless
     *       before the roster fills out.
     * @note The switch also cancels a swing in progress and plays the weapon's
     *       draw sound. Both were in the window procedure, and both are rules:
     *       the axe's dash must not be inherited by the shotgun, and the saw
     *       revving is what tells the player the change took.
     *
     * 한국어
     * ------
     * @brief 플레이어가 요청한 무기. *1부터 시작*하며 0은 요청하지 않았음을 뜻합니다.
     *
     * 1부터 시작하므로 `Input in = {0}`이 "0번 무기 선택"이 아니라 "요청 없음"이 됩니다.
     * ::Pools와 ::RunState와 ::DoorSet이 모두 지키는 관례입니다. 0으로 초기화된 구조체는
     * 유효한 빈 상태여야 하며, 그러지 않으면 필드를 하나씩 채워 구조체를 만드는 모든 픽스처가
     * 이 필드만은 따로 알아야 합니다. 마침 플레이어가 실제로 누른 숫자이기도 합니다.
     *
     * @note 보유하지 않은 무기는 조용히 거절됩니다. 빈손은 아무 일도 일어나지 않는 것보다
     *       나쁘며, 구성이 갖춰지기 전까지 숫자 키가 무해하게 유지됩니다.
     * @note 전환은 진행 중인 공격도 취소하고 무기의 뽑기 사운드를 재생합니다. 둘 다 창
     *       프로시저에 있었고 둘 다 규칙입니다. 도끼의 대쉬를 샷건이 물려받아서는 안 되며,
     *       톱이 돌기 시작하는 소리가 전환이 먹혔다는 것을 알려 줍니다.
     */
    int want_weapon;

    /**
     * @brief The player stopped holding things -- focus was lost, or a menu opened.
     *
     * ENGLISH
     * -------
     * The held fields above already go to zero on their own when this happens,
     * because the platform clears its keys. What does NOT clear itself is a
     * grapple that has already attached: it is state inside the ::Weapon, and
     * coming back from alt-tab still roped to a wall is the same class of
     * surprise as coming back still walking forward.
     *
     * @note Releasing AND rearming, in that order. The button is up by the time
     *       this arrives, so the hook has to be made ready for the next press
     *       rather than left waiting for a release that already happened.
     *
     * 한국어
     * ------
     * @brief 플레이어가 붙잡고 있던 것을 놓았습니다. 포커스를 잃었거나 메뉴가 열렸습니다.
     *
     * 위의 유지 필드들은 이 일이 일어나면 스스로 0이 됩니다. 플랫폼이 자기 키를 지우기
     * 때문입니다. 스스로 지워지지 *않는* 것은 이미 걸린 그래플입니다. 그것은 ::Weapon 내부의
     * 상태이며, alt-tab에서 벽에 로프가 걸린 채로 돌아오는 것은 계속 전진하는 채로 돌아오는
     * 것과 같은 종류의 놀라움입니다.
     *
     * @note 해제한 뒤 재장전하며, 순서가 그렇습니다. 이것이 도착할 무렵 버튼은 이미 올라와
     *       있으므로, 이미 일어난 해제를 기다리게 두지 않고 다음 누름에 대비시켜야 합니다.
     */
    int let_go;
} Input;

/* ---------------------------------------------------------------- progress */

/**
 * @struct PlayerProgress
 * @brief Everything the player keeps when they cross into the next level.
 *
 * ENGLISH
 * -------
 * Health, keycards, and the belt: which weapons are owned, how much each holds,
 * and which one is in hand. Not position, not facing, and not the hurt flash --
 * those are the new level's start to decide, or a frame's to forget.
 *
 * This exists because ::world_load_level used to spell the list out twice, in
 * two halves of a function with a ::player_spawn between them:
 *
 * @code
 *     int hp = w->player.health, held_keys = w->player.keys;
 *     int ammo[WP_TYPES], owned[WP_TYPES], cur = w->weapon.cur;
 *     ... 12 lines ...
 *     if (carry_state) { w->player.health = hp; ... w->player.keys = held_keys; }
 * @endcode
 *
 * That is the same shape ::RunState was extracted from, one layer up, and it
 * had the same problem: the list was a thing somebody had to keep current. Give
 * the player armour, or a powerup, or a count of secrets found, and the field
 * arrives in ::Player where it belongs and quietly does not survive a door.
 * "My armour vanished when I took the exit" is a bug that looks like a design
 * decision, which is exactly what the death check exists to avoid being.
 *
 * @note The list is now in three places rather than one -- these fields,
 *       ::world_progress_read and ::world_progress_write -- but all three are
 *       within twenty lines of each other, and a `_Static_assert` on this
 *       struct's size turns adding a field without teaching the other two into
 *       a compile error rather than a playtest surprise.
 *
 * 한국어
 * ------
 * @brief 플레이어가 다음 레벨로 넘어갈 때 가져가는 모든 것입니다.
 *
 * 체력, 키카드, 그리고 탄약대입니다. 어떤 무기를 보유했는지, 각각 얼마나 담고 있는지, 손에
 * 든 것이 무엇인지입니다. 위치도, 바라보는 방향도, 피격 섬광도 아닙니다. 그것들은 새 레벨의
 * 시작 지점이 결정하거나 한 프레임이 잊을 것들입니다.
 *
 * 이것이 존재하는 이유는 ::world_load_level이 그 목록을 두 번 적고 있었기 때문입니다. 한
 * 함수의 두 절반에, 그 사이에 ::player_spawn을 두고서 말입니다.
 *
 * 그것은 한 계층 위에서 ::RunState가 추출되어 나온 것과 같은 형태이며, 같은 문제를 갖고
 * 있었습니다. 그 목록은 누군가가 최신으로 유지해야 하는 것이었습니다. 플레이어에게 아머나
 * 파워업, 또는 발견한 비밀의 수를 준다고 해 보십시오. 그 필드는 마땅히 있어야 할 ::Player에
 * 도착하고, 조용히 문을 넘어 살아남지 못합니다. "출구를 지나니 아머가 사라졌다"는 설계
 * 판단처럼 보이는 버그이며, 사망 검사가 그렇게 되지 않기 위해 존재하는 바로 그것입니다.
 *
 * @note 이제 목록은 하나가 아니라 세 곳에 있습니다. 이 필드들, ::world_progress_read,
 *       ::world_progress_write입니다. 그러나 셋 모두 서로 20줄 안에 있으며, 이 구조체의
 *       크기에 대한 `_Static_assert`가 나머지 둘에게 알려 주지 않고 필드를 추가하는 것을
 *       플레이 중의 놀라움이 아니라 컴파일 오류로 만듭니다.
 */
typedef struct {
    int health;             /**< Hit points. / 체력. */
    int keys;               /**< KEY_* mask of the cards held. / 보유한 카드의 KEY_* 마스크. */
    int cur;                /**< Which weapon is in hand. / 손에 든 무기. */
    int ammo[WP_TYPES];     /**< Rounds in each belt. / 무기별 탄약. */
    int owned[WP_TYPES];    /**< Which weapons have been found. / 획득한 무기 여부. */
} PlayerProgress;

/* The two calls that move one are declared with the rest of the API below,
   because they take a ::World and it does not exist yet up here.
   이것을 옮기는 두 함수는 아래의 나머지 API와 함께 선언되어 있습니다. ::World를 인자로
   받는데 이 위쪽에는 그것이 아직 존재하지 않기 때문입니다. */

/* ------------------------------------------------------------------- world */

/**
 * @struct World
 * @brief The level, the player, the weapon and the run: everything a frame steps.
 *
 * ENGLISH
 * -------
 * One struct rather than four globals, for the reason ::RunState is one struct
 * rather than six fields: a caller that can only be handed all of it cannot be
 * handed a stale half of it, and there is exactly one place to look for what a
 * frame owns.
 *
 * @note A ::World IS copyable, and it was not always. ::wp_init used to record
 *       the address of ::World::level inside the weapon, so a field of this
 *       struct pointed at another field of the same struct and a copy left the
 *       gun firing into the geometry of the original. This carried a @warning
 *       saying not to do that, which is the weakest kind of invariant: one the
 *       compiler cannot check and the next reader may not find. The level is a
 *       parameter to ::wp_update now and nothing in here points into here.
 *
 *       It is 122KB, so copying one is still a thing to do on purpose rather
 *       than by accident -- but that is a size, not a trap.
 *
 * 한국어
 * ------
 * @brief 레벨·플레이어·무기·플레이. 한 프레임이 진행시키는 모든 것입니다.
 *
 * 전역 변수 넷이 아니라 하나의 구조체인 이유는 ::RunState가 필드 여섯 개가 아니라 하나의
 * 구조체인 이유와 같습니다. 전부를 함께만 건네받을 수 있는 호출자는 그중 낡은 절반을
 * 건네받을 수 없으며, 한 프레임이 무엇을 소유하는지 찾아볼 곳이 정확히 한 군데입니다.
 *
 * @note ::World는 복사할 수 *있습니다*. 언제나 그랬던 것은 아닙니다. ::wp_init이
 *       ::World::level의 주소를 무기 안에 기록했으므로 이 구조체의 한 필드가 같은 구조체의
 *       다른 필드를 가리켰고, 복사본은 총이 원본의 지오메트리를 향해 쏘게 만들었습니다.
 *       그래서 그러지 말라는 @warning이 붙어 있었는데, 그것은 가장 약한 종류의 불변식입니다.
 *       컴파일러가 검사할 수 없고 다음 독자가 찾지 못할 수도 있는 불변식입니다. 레벨은 이제
 *       ::wp_update의 인자이며, 이 안의 무엇도 이 안을 가리키지 않습니다.
 *
 *       122KB이므로 복사는 여전히 실수로가 아니라 의도적으로 할 일입니다. 그러나 그것은
 *       크기이지 함정이 아닙니다.
 */
typedef struct {
    Level    level;   /**< The level currently loaded. / 현재 로드된 레벨. */
    Player   player;  /**< Position, momentum, health, keycards. / 위치, 운동량, 체력, 열쇠. */
    Weapon   weapon;  /**< The belt, the gun in hand and the grapple. / 탄약, 손에 든 총기, 그래플. */
    RunState run;     /**< The current run. Reset through ::run_reset, never field by field. / 현재 플레이. 필드를 하나씩이 아니라 ::run_reset을 통해 초기화합니다. */

    /**
     * @brief The pools this run spawns into: projectiles, and in time the rest.
     *
     * Here for the reason the four above are: a caller cannot hold half a game.
     * These used to be file-scope arrays inside their own modules, which meant
     * a second World shared the first one's contents and this struct's whole
     * premise was untrue for everything it did not name. See pools.h.
     *
     * 위의 넷이 이곳에 있는 것과 같은 이유로 이곳에 있습니다. 호출자는 게임의 절반만 들고
     * 있을 수 없습니다. 이것들은 각자의 모듈 안 파일 스코프 배열이었고, 그 말은 두 번째
     * World가 첫 번째의 내용물을 공유한다는 뜻이며, 이 구조체가 이름 붙이지 않은 모든 것에
     * 대해 그 전제가 참이 아니었다는 뜻입니다. pools.h를 참조하십시오.
     */
    Pools    pools;

    /**
     * @brief Look angles in radians. Pitch is clamped short of vertical.
     *
     * ENGLISH: In the World rather than in ::Player because the simulation
     * needs them -- a hook is thrown along them and a hitscan is traced along
     * them -- and because ::player_spawn returns the yaw the level's start
     * faces, which has to land somewhere the next frame can read.
     *
     * 한국어: ::Player가 아니라 World에 있는 이유는 시뮬레이션이 이 값을 필요로 하기
     * 때문입니다. 훅이 이 방향으로 던져지고 히트스캔이 이 방향으로 탐색됩니다. 또한
     * ::player_spawn이 레벨 시작 지점이 바라보는 yaw를 반환하며, 그것이 다음 프레임이
     * 읽을 수 있는 곳에 놓여야 합니다.
     */
    float yaw, pitch;

    /**
     * @brief Name of the level currently loaded.
     *
     * ENGLISH: Its own buffer rather than a read of ::Level::name, and written
     * only by ::world_load_level -- which copies the requested name out before
     * it parses anything, precisely so that this field, ::Level::next and the
     * caller's argument may all alias each other safely.
     *
     * 한국어: ::Level::name을 다시 읽는 것이 아닌 자체 버퍼이며, ::world_load_level만이
     * 기록합니다. 그 함수는 파싱 전에 요청된 이름을 복사해 두는데, 바로 이 필드와
     * ::Level::next와 호출자의 인자가 서로 별칭이어도 안전하도록 하기 위함입니다.
     */
    char cur_level[WORLD_LEVEL_MAX];

    /**
     * @brief What the player was carrying when they ARRIVED in ::cur_level.
     *
     * ENGLISH
     * -------
     * The stage checkpoint, and what ::world_restart restores.
     *
     * A restart used to be handed the boot belt -- a shotgun and twenty shells
     * -- which is right for the first stage and wrong for every stage after it.
     * Reach stage two with the axe and the launcher, die, and the game took them
     * away: not a retry of the stage, a demotion out of it. The other obvious
     * answer, restoring what the player held at the moment they died, is worse
     * still -- it hands back the ammo they spent on the attempt that failed.
     *
     * What a retry means is "put me back where I started this stage", so this is
     * a snapshot taken on arrival and never touched again until the next
     * arrival. Written by ::world_load_level for whatever reason it loaded, so a
     * transition, a new game and a stage picked from a menu all leave a
     * checkpoint behind without any of them being asked to.
     *
     * 한국어
     * ------
     * @brief 플레이어가 ::cur_level에 *도착했을 때* 들고 있던 것.
     *
     * 스테이지 체크포인트이며, ::world_restart가 복원하는 값입니다.
     *
     * 재시작은 이전에 부팅 구성(샷건과 탄환 20발)을 받았습니다. 첫 스테이지에는 맞고 그
     * 이후의 모든 스테이지에는 틀립니다. 도끼와 유탄 발사기를 들고 2스테이지에 도달해서
     * 죽으면 게임이 그것들을 빼앗았습니다. 스테이지의 재시도가 아니라 스테이지에서의
     * 강등이었습니다. 또 하나의 자명한 답인 "죽은 순간에 들고 있던 것"은 더 나쁩니다. 실패한
     * 시도에서 소모한 탄약을 그대로 돌려주기 때문입니다.
     *
     * 재시도가 뜻하는 것은 "이 스테이지를 시작했던 자리로 되돌려 놓아라"이므로, 이것은
     * 도착 시점에 찍은 스냅숏이며 다음 도착까지 다시 건드리지 않습니다. ::world_load_level이
     * 무슨 이유로 로드했든 기록하므로, 전환·새 게임·메뉴에서 고른 스테이지 모두가 요청하지
     * 않아도 체크포인트를 남깁니다.
     */
    PlayerProgress entry;

    /* --- what the platform still has to do about this world -------------- */

    /**
     * @brief Set when the DRAWN level geometry no longer matches the sectors.
     *
     * ENGLISH
     * -------
     * Doors move the sectors themselves, so everything that collides sees them
     * without knowing what a door is -- but the render mesh was built once and
     * has to be rebuilt to follow. So does a level that has just been loaded.
     *
     * Those were two separate call sites, each remembering to rebuild for its
     * own reason, and a third mover of sectors would have been a third. This is
     * one flag, raised by whatever moved them and consumed in one place by
     * ::world_take_geometry_scope -- which is also what keeps this module from
     * having to know that a Scene exists.
     *
     * 한국어
     * ------
     * @brief *그려지는* 레벨 지오메트리가 더 이상 섹터와 일치하지 않을 때 설정됩니다.
     *
     * 문은 섹터 자체를 움직이므로 충돌하는 모든 것이 문의 정체를 모른 채 그것을 봅니다.
     * 그러나 렌더 메시는 한 번 만들어졌으므로 따라가려면 다시 만들어야 합니다. 방금 로드된
     * 레벨도 마찬가지입니다.
     *
     * 이전에는 두 개의 별개 호출 지점이 각자의 이유로 재생성을 기억하고 있었고, 섹터를
     * 움직이는 세 번째 시스템이 생기면 세 번째가 되었을 것입니다. 이제는 플래그 하나이며,
     * 움직인 쪽이 세우고 ::world_take_geometry_scope가 한 곳에서 소비합니다. 그것이 또한 이
     * 모듈이 Scene의 존재를 알지 않아도 되게 하는 장치입니다.
     */
    /* A ::WorldGeom rather than a flag, so the consumer can tell "a level was
       loaded" from "a door moved" and rebuild only what the second one
       invalidates. An `int` because the enum is declared below this struct,
       beside the call that returns it.
       플래그가 아니라 ::WorldGeom입니다. 소비하는 쪽이 "레벨이 로드되었다"와 "문이 움직였다"를
       구별하여 두 번째가 무효화하는 것만 다시 만들 수 있게 합니다. `int`인 이유는 그 열거형이
       이 구조체 아래, 그것을 반환하는 함수 곁에 선언되어 있기 때문입니다. */
    int geometry_dirty;

    /** @brief Non-zero once the level mesh has been uploaded at least once. / 레벨 메시가 최소 한 번 업로드되었으면 0이 아닙니다. */
    int geometry_uploaded;

    /**
     * @brief Where this world's brush levels are stored. NULL means the default.
     *
     * ENGLISH
     * -------
     * A POINTER, and it has to be. A ::BrushStore is 840KB against this
     * struct's 122KB, and a ::World is copyable and is a stack local in the
     * test suite -- embedding one would end both of those properties. This
     * points at storage somebody else owns, exactly as ::Level::brushes points
     * into that storage.
     *
     * WHAT IT IS FOR. ::LVL_BRUSH_SLOTS used to be a budget for the process, so
     * a second ::World beside the running one was a third live level against a
     * pool of two and was refused. pools.h describes that trap for the spawned
     * contents of a run -- "an editor that wanted to preview a level beside the
     * running game would be previewing INTO the running game" -- and fixed it
     * for the pools. The level's own geometry was the half left over. This is
     * that half.
     *
     * @note NULL is the ordinary case and costs nothing: ::world_init leaves it
     *       so, and every existing caller keeps the behaviour it had. Only
     *       something that genuinely wants a second independent world names a
     *       store, through ::world_init_in.
     * @note Copying a ::World copies this pointer, which is right -- a copy
     *       refers to the same storage, and ::Level::brushes in the copy already
     *       points into it.
     *
     * 한국어
     * ------
     * @brief 이 월드의 브러시 레벨이 저장되는 곳. NULL이면 기본 저장소입니다.
     *
     * *포인터이며 그럴 수밖에 없습니다.* ::BrushStore는 이 구조체의 122KB에 대해 840KB이고,
     * ::World는 복사 가능하며 테스트 묶음에서 스택 지역 변수입니다. 값으로 넣으면 그 두 성질이
     * 모두 끝납니다. 이것은 다른 누군가가 소유한 저장 공간을 가리키며, ::Level::brushes가 그
     * 저장 공간 안을 가리키는 것과 정확히 같습니다.
     *
     * *무엇을 위한 것인가.* ::LVL_BRUSH_SLOTS는 프로세스의 예산이었으므로, 실행 중인 것 곁의 두
     * 번째 ::World는 슬롯 둘짜리 풀에 대한 세 번째 살아 있는 레벨이었고 거절되었습니다. pools.h가
     * 한 판의 플레이가 생성하는 내용물에 대해 그 함정을 서술하며("실행 중인 게임 옆에서 레벨을
     * 미리 보려는 에디터는 실행 중인 게임 *안으로* 미리 보게 됩니다") 풀에 대해서는 그것을
     * 고쳤습니다. 레벨 자신의 지오메트리가 남은 절반이었습니다. 이것이 그 절반입니다.
     *
     * @note NULL이 평범한 경우이며 비용이 없습니다. ::world_init이 그대로 두고, 기존의 모든
     *       호출자는 가지고 있던 동작을 유지합니다. 진짜로 독립적인 두 번째 월드를 원하는
     *       것만이 ::world_init_in을 통해 저장소를 지목합니다.
     * @note ::World를 복사하면 이 포인터가 복사되며 그것이 옳습니다. 복사본은 같은 저장 공간을
     *       가리키고, 복사본의 ::Level::brushes는 이미 그 안을 가리키고 있습니다.
     */
    BrushStore *store;
} World;

/* ------------------------------------------------------------ entering one */

/**
 * @enum WorldEnter
 * @brief Where the belt comes from when a level is loaded.
 *
 * ENGLISH
 * -------
 * This was a `carry_state` flag, and a flag has two answers to a question that
 * turned out to have three. Carrying what the player holds is right for an exit
 * and wrong for a restart; the boot belt is right for a new game and wrong for a
 * restart of anything but the first stage. The third answer -- the stage's own
 * checkpoint -- had nowhere to be expressed, so a restart took the second one
 * and stripped the player of everything they had earned.
 *
 * A row rather than a flag, for the reason the weapon table is one: the next
 * reason to load a level is a case here, not a second boolean parameter that
 * has to be read together with the first.
 *
 * 한국어
 * ------
 * @brief 레벨을 로드할 때 탄약대가 어디에서 오는지를 나타냅니다.
 *
 * 이것은 `carry_state` 플래그였고, 플래그는 답이 셋인 질문에 두 개의 답만 갖습니다.
 * 플레이어가 든 것을 이어 가는 것은 출구에는 맞고 재시작에는 틀립니다. 부팅 구성은 새
 * 게임에는 맞고 첫 스테이지가 아닌 무엇의 재시작에도 틀립니다. 세 번째 답인 스테이지 자신의
 * 체크포인트는 표현될 곳이 없었으므로, 재시작이 두 번째 답을 가져가 플레이어가 얻어 낸 모든
 * 것을 벗겨 냈습니다.
 *
 * 플래그가 아니라 표의 행인 이유는 무기 표가 그런 것과 같습니다. 레벨을 로드할 다음 이유는
 * 이곳의 한 경우이며, 첫 번째와 함께 읽어야 하는 두 번째 불리언 매개변수가 아닙니다.
 */
typedef enum {
    /**
     * @brief A new game, or an authoring reload: the belt the game boots with.
     * / 새 게임 또는 제작용 리로드. 게임이 부팅하는 탄약대입니다.
     */
    WORLD_ENTER_NEW,
    /**
     * @brief An exit transition: whatever the player is holding right now.
     *
     * ENGLISH: The exit is a reward you arrive at, not a reset -- the way a Doom
     * episode runs.
     *
     * 한국어: 출구는 도달하는 보상이지 초기화가 아닙니다. Doom 에피소드가 진행되는
     * 방식입니다.
     */
    WORLD_ENTER_CARRY,
    /**
     * @brief A restart: what the player entered this stage with.
     *
     * ENGLISH: Reads ::World::entry, which is also how a caller enters a stage
     * with a progress of its own choosing -- seed `entry`, then replay it. That
     * is what ::world_start_stage does.
     *
     * 한국어: ::World::entry를 읽습니다. 호출자가 자신이 정한 진행 상태로 스테이지에
     * 진입하는 방법도 이것입니다. `entry`를 심고 그것을 재생하십시오. ::world_start_stage가
     * 하는 일이 그것입니다.
     */
    WORLD_ENTER_REPLAY,
    WORLD_ENTER_MODES   /**< How many. / 개수. */
} WorldEnter;

/* --------------------------------------------------------------------- api */

/**
 * @brief Brings a ::World up on the title screen, in ::WORLD_START_LEVEL.
 *
 * ENGLISH
 * -------
 * @param[out] w Zeroed and seeded. No level is loaded yet.
 *
 * @note CALLS ::wp_init, which it could not do while weapon.c needed a GL
 *       context to load the gun's model. The drawn gun is ::WeaponView now and
 *       ::wp_init touches nothing but the ::Weapon, so a ::World brings up the
 *       whole of itself -- and a headless test still drives one with no
 *       context, which is what the old arrangement was protecting and what
 *       this keeps for free.
 * @note Seeds the smoke rng and zeroes every clock, through ::run_reset, so a
 *       fresh start and a restart begin from a state one function produced.
 *
 * 한국어
 * ------
 * @brief ::WORLD_START_LEVEL을 대상으로, 타이틀 화면 상태의 ::World를 준비합니다.
 * @param[out] w 0으로 초기화되고 기본값이 설정됩니다. 레벨은 아직 로드되지 않습니다.
 *
 * @note ::wp_init을 *호출합니다*. weapon.c가 총기 모델을 로드하기 위해 GL 컨텍스트를
 *       필요로 하는 동안에는 그럴 수 없었습니다. 이제 그려지는 총은 ::WeaponView이고
 *       ::wp_init은 ::Weapon 외에는 아무것도 건드리지 않으므로, ::World가 자기 자신
 *       전부를 준비합니다. 그리고 헤드리스 테스트는 여전히 컨텍스트 없이 World를
 *       구동합니다. 기존 방식이 지키려던 것이 그것이며, 이 변경은 그것을 공짜로
 *       유지합니다.
 * @note ::run_reset을 통해 연기 난수의 시드를 설정하고 모든 시계를 0으로 만들므로, 새 시작과
 *       재시작이 하나의 함수가 만들어 낸 상태에서 출발합니다.
 */
void world_init(World *w);

/**
 * @brief ::world_init, with the brush storage this world's levels will use.
 *
 * ENGLISH
 * -------
 * @param[out] w  Zeroed and seeded, exactly as ::world_init leaves it.
 * @param[in]  bs Store for this world's brush levels. NULL is the default
 *                store, which makes this identical to ::world_init.
 *
 * FOR A SECOND WORLD, and that is the whole of it. One world needs nothing here
 * -- ::world_init is that call, and every existing caller is that case. A
 * program that wants two independent worlds gives each its own store, and
 * neither can take the last slot from the other or reload the other's geometry.
 *
 * @warning `bs` must outlive `w`, because ::Level::brushes points inside it.
 *          Give it static storage or make it a field of something that lives as
 *          long as the world does.
 *
 * 한국어
 * ------
 * @brief 이 월드의 레벨이 사용할 브러시 저장소와 함께 수행하는 ::world_init입니다.
 * @param[out] w  ::world_init이 남기는 것과 정확히 같이 0으로 초기화되고 설정됩니다.
 * @param[in]  bs 이 월드의 브러시 레벨을 위한 저장소. NULL이면 기본 저장소이며, 그 경우 이
 *                함수는 ::world_init과 동일합니다.
 *
 * *두 번째 월드를 위한 것이며* 그것이 전부입니다. 월드가 하나라면 이곳에서 필요한 것이 없습니다.
 * ::world_init이 그 호출이고 기존의 모든 호출자가 그 경우입니다. 독립적인 월드 둘을 원하는
 * 프로그램은 각각에 자기 저장소를 주며, 어느 쪽도 상대의 마지막 슬롯을 가져가거나 상대의
 * 지오메트리를 다시 로드할 수 없습니다.
 *
 * @warning `bs`는 `w`보다 오래 살아야 합니다. ::Level::brushes가 그 내부를 가리키기 때문입니다.
 *          정적 저장 기간을 주거나, 월드만큼 오래 사는 무언가의 필드로 만드십시오.
 */
void world_init_in(World *w, BrushStore *bs);

/**
 * @brief Loads a level and puts everything that belongs to one into place.
 *
 * ENGLISH
 * -------
 * @param[in,out] w    The world to load into.
 * @param[in]     name Level name. May alias ::World::cur_level or
 *                     ::Level::next; both are safe.
 * @param[in]     how  Where the belt comes from. See ::WorldEnter.
 * @return 1 on success. 0 if no level of that name exists, in which case
 *         NOTHING has changed and the caller stays where it is -- a typo must
 *         not drop the player into a void, and it must not win the game either.
 *
 * @note Whatever `how` decides, the result is recorded in ::World::entry: the
 *       progress the player is standing there with IS the checkpoint for this
 *       stage. Read back out of the world rather than copied from the branch
 *       that set it, so the checkpoint cannot disagree with what the player
 *       actually has.
 *
 * @note The steps below have to happen together and in this order. They used to
 *       be written out at three call sites -- startup, an exit transition and a
 *       hot reload -- and had already drifted: the reload did not respawn the
 *       player, and whether that was deliberate could not be answered from the
 *       code.
 * @note `name` is copied into a local before ::level_load runs. ::level_load
 *       blanks the destination's `next` field before parsing, so a caller
 *       handing over `w->level.next` directly would have its search string
 *       erased mid-call. Copying first makes that impossible rather than
 *       leaving it as a rule the caller has to know.
 * @note ::door_reset must run after the sectors exist and before the first
 *       ::door_update: it copies each door's CLOSED shape out of the level, and
 *       can only do that while the level still holds it.
 *
 * 한국어
 * ------
 * @brief 레벨을 로드하고, 레벨에 속한 모든 것을 제자리에 놓습니다.
 * @param[in,out] w    로드 대상 월드.
 * @param[in]     name 레벨 이름. ::World::cur_level이나 ::Level::next와 별칭이어도
 *                     안전합니다.
 * @param[in]     how  탄약대가 어디에서 오는지. ::WorldEnter를 참조하십시오.
 * @return 성공하면 1. 해당 이름의 레벨이 없으면 0이며, 이 경우 *아무것도* 바뀌지 않고
 *         호출자는 있던 자리에 머무릅니다. 오타가 플레이어를 빈 공간에 떨어뜨려서도, 게임을
 *         이기게 해서도 안 됩니다.
 *
 * @note `how`가 무엇을 결정했든 그 결과는 ::World::entry에 기록됩니다. 플레이어가 그곳에 서
 *       있는 상태 그 자체가 이 스테이지의 체크포인트입니다. 그것을 설정한 분기에서 복사하지
 *       않고 월드에서 다시 읽어 오므로, 체크포인트가 플레이어가 실제로 가진 것과 어긋날 수
 *       없습니다.
 *
 * @note 아래 단계들은 반드시 함께, 이 순서로 수행되어야 합니다. 이전에는 시작·출구 전환·핫
 *       리로드 세 곳에 각각 작성되어 있었고 이미 어긋나 있었습니다. 리로드는 플레이어를
 *       다시 스폰하지 않았는데, 그것이 의도인지 코드만 보고는 답할 수 없었습니다.
 * @note `name`은 ::level_load 실행 전에 지역 버퍼로 복사됩니다. ::level_load는 파싱 전에
 *       대상의 `next` 필드를 비우므로, `w->level.next`를 그대로 넘기는 호출자는 호출 도중에
 *       검색 문자열이 지워집니다. 먼저 복사하면 그것이 호출자가 알아야 하는 규칙이 아니라
 *       불가능한 일이 됩니다.
 * @note ::door_reset은 섹터가 존재한 뒤, 첫 ::door_update 이전에 실행되어야 합니다. 각 문의
 *       *닫힌* 형상을 레벨에서 복사하며, 레벨이 아직 그것을 담고 있는 동안에만 가능합니다.
 */
int world_load_level(World *w, const char *name, WorldEnter how);

/**
 * @brief What a player arriving at `name` for the first time would be carrying.
 *
 * ENGLISH
 * -------
 * @param[in]  name Stage to arrive at. Need not be reachable; see the return.
 * @param[out] out  The belt to start there with. Untouched on failure.
 * @return 1 if `name` was found on the chain from ::WORLD_START_LEVEL. 0 if the
 *         chain ends, breaks or loops before reaching it.
 *
 * For "start from a stage you have cleared". Every stage before the target is
 * walked -- ALL of them, not merely the one immediately before it -- and every
 * weapon any of them hands out is granted, at half that weapon's belt capacity.
 *
 * Accumulating across the whole run is the point. Weapons are found once and
 * kept for the rest of the episode, so a player who reaches stage four honestly
 * is holding what stages one, two and three gave them. Reading only the
 * previous stage would hand them whatever stage three happened to contain and
 * silently take back the axe they picked up in stage one.
 *
 * The chain is the level text's own: each stage names its successor in `next`,
 * and that is the only ordering this game has. No table of stages is kept
 * beside it -- a second list of what follows what is a second thing to get
 * wrong, and this one is already the thing the exit uses.
 *
 * Half of maximum rather than a full belt or the pickup amount: a full belt
 * makes the granted start easier than the earned one, and reproducing what the
 * player "would have had" exactly is not a thing the level text knows. Half is
 * the honest approximation -- enough to fight with, not enough to skip the
 * stage's own supply.
 *
 * @note Keycards are deliberately NOT granted. They are per-stage: ::door_reset
 *       re-locks every door on load, and the cards for those doors are in the
 *       stage the player is about to play.
 * @note Loads each stage on the chain into a scratch ::Level to read its
 *       entities. That is ~19KB of stack for the duration of the call, and the
 *       call happens once when a stage is picked, never in a frame.
 *
 * 한국어
 * ------
 * @brief `name`에 처음 도착하는 플레이어가 들고 있을 것.
 * @param[in]  name 도착할 스테이지. 도달 가능하지 않아도 됩니다. 반환값을 참조하십시오.
 * @param[out] out  그곳에서 시작할 탄약대. 실패 시에는 건드리지 않습니다.
 * @return `name`이 ::WORLD_START_LEVEL에서 이어지는 사슬 위에서 발견되면 1. 사슬이 그
 *         전에 끝나거나 끊기거나 순환하면 0.
 *
 * "클리어한 스테이지부터 시작하기"를 위한 것입니다. 대상 이전의 *모든* 스테이지를
 * 순회하며(직전 하나가 아니라 전부입니다), 그중 어느 것이든 내주는 무기를 그 무기의 최대
 * 탄약의 절반과 함께 부여합니다.
 *
 * 플레이 전체에 걸쳐 누적하는 것이 핵심입니다. 무기는 한 번 획득하면 에피소드의 나머지 동안
 * 유지되므로, 4스테이지에 정직하게 도달한 플레이어는 1·2·3스테이지가 준 것을 들고 있습니다.
 * 직전 스테이지만 읽으면 3스테이지가 우연히 담고 있던 것만 주게 되고, 1스테이지에서 주운
 * 도끼를 조용히 회수하게 됩니다.
 *
 * 사슬은 레벨 텍스트 자신의 것입니다. 각 스테이지가 `next`에 자신의 다음을 적으며, 이
 * 게임이 가진 순서는 그것뿐입니다. 그 옆에 스테이지 표를 따로 두지 않습니다. 무엇 다음에
 * 무엇이 오는지에 대한 두 번째 목록은 틀릴 수 있는 두 번째 대상이며, 이 사슬은 이미 출구가
 * 사용하는 바로 그것입니다.
 *
 * 가득 채우거나 아이템 획득량이 아니라 최대치의 절반인 이유는, 가득 채우면 부여된 시작이
 * 직접 얻어 낸 시작보다 쉬워지고, 플레이어가 "가지고 있었을" 양을 정확히 재현하는 것은 레벨
 * 텍스트가 아는 일이 아니기 때문입니다. 절반은 정직한 근사입니다. 싸우기에는 충분하고
 * 그 스테이지 자신의 보급을 건너뛰기에는 부족합니다.
 *
 * @note 키카드는 의도적으로 부여하지 *않습니다*. 키카드는 스테이지별입니다. ::door_reset이
 *       로드 시 모든 문을 다시 잠그며, 그 문들의 카드는 플레이어가 이제부터 플레이할 스테이지
 *       안에 있습니다.
 * @note 사슬 위의 각 스테이지를 임시 ::Level로 로드하여 엔티티를 읽습니다. 호출이 지속되는
 *       동안 약 19KB의 스택을 사용하며, 이 호출은 스테이지를 고를 때 한 번 일어나고 프레임
 *       안에서는 결코 일어나지 않습니다.
 *
 * @param[in]  root Where to start walking. This was ::WORLD_START_LEVEL, spelled
 *                  inside, and that was right while the game booted into the
 *                  campaign. It does not any more -- the start is an arena that
 *                  chains to nothing -- so a walk hardwired to it would find a
 *                  chain of one and refuse every level of the campaign that is
 *                  still shipped. Walking a chain has to know where the chain
 *                  begins, and that is now a different fact from where the game
 *                  begins. Pass ::WORLD_CHAIN_ROOT for the campaign.
 * @param[in]  name The stage being asked about.
 * @param[out] out  Left untouched when the walk does not reach `name`.
 *
 * @param[in]  root 어디서부터 걸을지. 이전에는 ::WORLD_START_LEVEL이었고 내부에 적혀 있었으며,
 *                  게임이 캠페인으로 부팅하는 동안에는 옳았습니다. 이제는 그렇지 않습니다.
 *                  시작은 아무것도 잇지 않는 아레나이므로, 그것에 고정된 순회는 길이 1의 사슬을
 *                  찾고 여전히 출하되는 캠페인의 모든 레벨을 거절하게 됩니다. 사슬을 걷는 일은
 *                  사슬이 어디서 시작하는지 알아야 하며, 그것은 이제 게임이 어디서 시작하는지와
 *                  다른 사실입니다. 캠페인에 대해서는 ::WORLD_CHAIN_ROOT를 넘기십시오.
 * @param[in]  name 질의 대상 스테이지.
 * @param[out] out  순회가 `name`에 닿지 못하면 손대지 않습니다.
 */
int world_progress_for_stage(const char *root, const char *name, PlayerProgress *out);

/**
 * @brief Begins a run at `name`, as though the player had played their way there.
 *
 * ENGLISH
 * -------
 * @param[in,out] w    The world.
 * @param[in]     name Stage to start in.
 * @return 1 on success. 0 if the stage is unreachable or does not load, in
 *         which case nothing has changed.
 *
 * @note Seeds ::World::entry and then enters as a ::WORLD_ENTER_REPLAY of it,
 *       which is what makes a restart of a stage started this way put the player
 *       back with the same belt rather than the boot one.
 *
 * 한국어
 * ------
 * @brief 플레이어가 직접 플레이해 도달한 것처럼 `name`에서 플레이를 시작합니다.
 * @param[in,out] w    월드.
 * @param[in]     name 시작할 스테이지.
 * @return 성공하면 1. 스테이지에 도달할 수 없거나 로드되지 않으면 0이며, 이 경우 아무것도
 *         바뀌지 않습니다.
 *
 * @note ::World::entry를 심은 뒤 그것의 ::WORLD_ENTER_REPLAY로 진입합니다. 이 방식으로
 *       시작한 스테이지를 재시작하면 부팅 구성이 아니라 같은 탄약대로 되돌아가는 이유가
 *       그것입니다.
 */
int world_start_stage(World *w, const char *root, const char *name);

/**
 * @brief Reads out what the player would keep across a level boundary.
 *
 * ENGLISH
 * -------
 * @param[in]  w   The world.
 * @param[out] out Filled in completely; every field is written.
 *
 * @note One caller today -- ::world_load_level, which reads before the spawn and
 *       writes after it. Exported anyway because the type is the answer to "what
 *       does a player keep", and a save file is exactly this struct.
 *
 * 한국어
 * ------
 * @brief 플레이어가 레벨 경계를 넘어 가져갈 것을 읽어 냅니다.
 * @param[in]  w   월드.
 * @param[out] out 모든 필드가 완전히 채워집니다.
 *
 * @note 오늘의 호출자는 하나입니다. ::world_load_level이 스폰 전에 읽고 스폰 후에 씁니다.
 *       그럼에도 공개하는 이유는, 이 타입이 "플레이어는 무엇을 가져가는가"에 대한 답이며
 *       세이브 파일이 정확히 이 구조체이기 때문입니다.
 */
void world_progress_read(const World *w, PlayerProgress *out);

/**
 * @brief Puts a ::PlayerProgress back, over whatever a spawn just set.
 *
 * ENGLISH
 * -------
 * @param[in,out] w The world.
 * @param[in]     p What to restore.
 * @note ::player_spawn resets health, so this has to run AFTER it rather than
 *       before -- which is the whole reason the read and the write are two calls
 *       and not one.
 *
 * 한국어
 * ------
 * @brief ::PlayerProgress를 스폰이 방금 설정한 값 위에 되돌려 놓습니다.
 * @param[in,out] w 월드.
 * @param[in]     p 복원할 내용.
 * @note ::player_spawn이 체력을 초기화하므로, 이 호출은 그 앞이 아니라 *뒤에* 와야 합니다.
 *       읽기와 쓰기가 하나가 아니라 두 개의 호출인 이유가 바로 그것입니다.
 */
void world_progress_write(World *w, const PlayerProgress *p);

/**
 * @brief Restarts the current level as a fresh run.
 *
 * ENGLISH
 * -------
 * @param[in,out] w The world to restart.
 *
 * @note One implementation, three ways in: the menu's RESTART row, a key on the
 *       death screen and a click on it. All three set
 *       ::RunState::restart_wanted and nothing else, so they cannot clear
 *       different halves of the same state -- a death screen that restarted
 *       without clearing `won`, or a menu restart that left the player dead,
 *       would each be a separate half-working path.
 * @note Clears ::RunState::restart_wanted along with everything else, through
 *       ::run_reset, which is why no caller does so by hand.
 * @note Does not touch the cursor or close the menu. Those belong to whoever
 *       owns the window, and they have to follow this call rather than precede
 *       it: `dead` has just changed what the cursor should be.
 *
 * 한국어
 * ------
 * @brief 현재 레벨을 새 플레이로 재시작합니다.
 * @param[in,out] w 재시작할 월드.
 *
 * @note 구현은 하나이고 진입로는 셋입니다. 메뉴의 RESTART 행, 사망 화면에서의 키, 그곳에서의
 *       클릭입니다. 셋 모두 ::RunState::restart_wanted만을 설정하므로 같은 상태의 서로 다른
 *       절반을 지울 수 없습니다. `won`을 지우지 않고 재시작하는 사망 화면이나 플레이어를
 *       죽은 채로 두는 메뉴 재시작은 각각 절반만 동작하는 별개의 경로가 됩니다.
 * @note ::run_reset을 통해 나머지 전부와 함께 ::RunState::restart_wanted도 지우므로, 손으로
 *       지우는 호출자가 없습니다.
 * @note KEEPS ::RunState::endless, which is the one field ::run_reset's zeroing
 *       is deliberately undone for. A retry is a retry of THIS game, not a
 *       return to the menu that chose it -- and because story and endless are
 *       the same room, an endless run that came back as a story one would look
 *       identical until a banner appeared in a mode that has none.
 * @note ::RunState::endless는 *유지합니다*. ::run_reset의 0 초기화를 의도적으로 되돌리는 유일한
 *       필드입니다. 재시도는 *이* 게임의 재시도이지 그것을 고른 메뉴로 돌아가는 일이 아니며,
 *       스토리와 무한이 같은 방이므로 스토리로 돌아온 무한 플레이는 그것을 갖지 않은 모드에
 *       배너가 뜨기 전까지 똑같아 보입니다.
 * @note 커서를 건드리거나 메뉴를 닫지 않습니다. 그것들은 창을 소유한 쪽에 속하며, 이 호출보다
 *       앞이 아니라 뒤에 와야 합니다. `dead`가 방금 커서의 올바른 상태를 바꾸었기 때문입니다.
 */
void world_restart(World *w);

/**
 * @brief Starts a run in the chosen mode, from the title screen.
 *
 * ENGLISH
 * -------
 * @param[in,out] w       The world to start playing in.
 * @param[in]     endless Non-zero for endless, zero for story. Becomes
 *                        ::RunState::endless and nothing else reads the
 *                        argument.
 * @return 1 when the arena loaded. 0 when it did not, in which case the title
 *         screen is still up and NOTHING has been started -- ::world_load_level
 *         keeps the same contract about a level that will not parse, and a
 *         player whose game did not begin must be left somewhere they can press
 *         a key rather than in a void with no menu.
 *
 * THE ONE PLACE A MODE IS CHOSEN, which is why it is here rather than three
 * lines in main.c. Three facts have to happen together and in order: the run is
 * cleared, the mode is set on the cleared run, and the arena is loaded. Written
 * out at the call site, the second would be the one that goes missing -- and
 * the symptom is the quietest kind, an endless run with a story's banner and a
 * respawn that never comes.
 *
 * @note ::WORLD_ENTER_NEW, always. Both modes begin with the boot belt at full
 *       health, because neither is entered from anywhere: there is no previous
 *       level to carry from and no checkpoint to replay, which is what
 *       ::WORLD_ENTER_NEW means.
 * @note Starts ::STORY_INTRO in story mode and not in endless, which is the
 *       same gate ::BossLine is behind and stated in the same place -- a mode
 *       decides what is said, and both of those decisions are made where the
 *       mode is set rather than where the words are.
 *
 * 한국어
 * ------
 * @brief 타이틀 화면에서, 고른 모드로 플레이를 시작합니다.
 * @param[in,out] w       플레이를 시작할 월드.
 * @param[in]     endless 무한이면 0이 아닌 값, 스토리면 0. ::RunState::endless가 되며 그 밖에
 *                        이 인자를 읽는 것은 없습니다.
 * @return 아레나가 로드되면 1. 로드되지 않으면 0이며, 그 경우 타이틀 화면이 그대로 떠 있고
 *         *아무것도* 시작되지 않습니다. ::world_load_level이 파싱되지 않는 레벨에 대해 지키는
 *         것과 같은 계약이며, 게임이 시작되지 않은 플레이어는 메뉴도 없는 빈 공간이 아니라 키를
 *         누를 수 있는 곳에 남아야 합니다.
 *
 * *모드가 정해지는 유일한 곳이며*, 그것이 main.c의 세 줄이 아니라 이곳인 이유입니다. 세 가지
 * 사실이 함께, 순서대로 일어나야 합니다. 플레이가 지워지고, 지워진 플레이 위에 모드가 세워지고,
 * 아레나가 로드됩니다. 호출 지점에 적어 두면 사라지는 것은 두 번째입니다. 그리고 그 증상은 가장
 * 조용한 종류입니다. 스토리의 배너를 달고 결코 오지 않는 재소환을 가진 무한 플레이입니다.
 *
 * @note *언제나 ::WORLD_ENTER_NEW입니다.* 두 모드 모두 체력이 가득한 부팅 구성으로 시작합니다.
 *       어느 쪽도 어딘가로부터 들어오는 것이 아니기 때문입니다. 이어받을 이전 레벨도, 재생할
 *       체크포인트도 없으며, 그것이 ::WORLD_ENTER_NEW가 뜻하는 바입니다.
 * @note 스토리 모드에서는 ::STORY_INTRO를 시작하고 무한에서는 하지 않습니다. ::BossLine이 놓인
 *       것과 같은 게이트이며 같은 곳에서 진술됩니다. 무엇을 말할지는 모드가 정하고, 그 두 결정은
 *       말이 있는 곳이 아니라 모드가 세워지는 곳에서 내려집니다.
 */
int world_begin(World *w, int endless);

/**
 * @brief Whether the world is stopped this frame.
 *
 * ENGLISH
 * -------
 * @param[in] w      The world.
 * @param[in] paused ::Input::paused -- non-zero while a UI has it stopped.
 * @return Non-zero when nothing in the world may advance.
 *
 * @note Pure, and one definition for the several places that ask. The title
 *       screen, the win screen, the death screen, a cutscene and an open menu
 *       freeze exactly the same things, and the reason this is a function
 *       rather than five conditions is that five conditions repeated across
 *       call sites is how one of them ends up missing a site. The cutscene is
 *       what that bought: it froze the world by being added to one expression.
 * @note ::world_step returns the value it used, so the renderer does not derive
 *       it a second time from state the step has since changed. Asking again
 *       after the step would hide the crosshair on the very frame the player
 *       died, one frame before the death screen it belongs to appears.
 *
 * 한국어
 * ------
 * @brief 이번 프레임에 월드가 정지 상태인지 여부입니다.
 * @param[in] w      월드.
 * @param[in] paused ::Input::paused. UI가 월드를 정지시켰으면 0이 아닙니다.
 * @return 월드의 어떤 것도 진행할 수 없으면 0이 아닙니다.
 *
 * @note 부수 효과가 없으며, 이를 묻는 여러 곳을 위한 하나의 정의입니다. 타이틀 화면, 승리
 *       화면, 사망 화면, 컷신, 열린 메뉴는 정확히 같은 것들을 정지시킵니다. 다섯 개의 조건이
 *       아니라 함수인 이유는, 여러 호출 지점에 반복되는 다섯 개의 조건은 그중 하나가 어느 한
 *       곳을 빠뜨리게 되는 지름길이기 때문입니다. 컷신이 그 대가로 얻은 것입니다. 식 하나에
 *       더해지는 것만으로 월드를 정지시켰습니다.
 * @note ::world_step이 자신이 사용한 값을 반환하므로, 렌더러가 그 사이 갱신이 바꿔 놓은
 *       상태로부터 이 값을 두 번째로 유도하지 않습니다. 갱신 이후에 다시 물으면 플레이어가
 *       죽은 바로 그 프레임에, 그에 해당하는 사망 화면이 나타나기 한 프레임 전에 조준점이
 *       사라집니다.
 */
int world_frozen(const World *w, int paused);

/**
 * @brief Shakes the drawn camera, if this is louder than what is already going.
 *
 * ENGLISH
 * -------
 * @param[in,out] w      The world whose view is shaken.
 * @param[in]     amount Magnitude. Clamped to ::WORLD_SHAKE_MAX; values at or
 *                       below zero do nothing, so a caller may hand over a
 *                       scaled figure without testing it first.
 *
 * @note TAKES THE LOUDER, never the sum. Two events in one frame -- a shotgun
 *       fired inside a blast -- are one violent moment. Adding them would make
 *       a busy frame shake harder than any single thing in it warrants, and the
 *       busiest frames are exactly the ones already hardest to read.
 * @note Public so that a later source -- an explosion, a door slamming, a boss
 *       landing -- is one call rather than a new field. ::world_step raises the
 *       three that exist today from state it already has.
 *
 * 한국어
 * ------
 * @brief 지금 진행 중인 것보다 크다면 그려지는 카메라를 흔듭니다.
 * @param[in,out] w      시야가 흔들릴 월드.
 * @param[in]     amount 크기. ::WORLD_SHAKE_MAX로 제한되며 0 이하이면 아무 일도 하지
 *                       않으므로, 호출자는 먼저 검사하지 않고 조정된 값을 그대로 건네도 됩니다.
 *
 * @note 합이 아니라 *큰 쪽*을 취합니다. 한 프레임의 두 사건(폭발 안에서 쏜 샷건)은 하나의
 *       격렬한 순간입니다. 더하면 분주한 프레임이 그 안의 어떤 단일 사건이 정당화하는 것보다
 *       세게 흔들리는데, 가장 분주한 프레임이야말로 이미 읽기 가장 어려운 프레임입니다.
 * @note 공개하는 이유는 나중의 원천(폭발, 쾅 닫히는 문, 착지하는 보스)이 새 필드가 아니라
 *       호출 하나가 되도록 하기 위함입니다. ::world_step은 오늘 존재하는 셋을 이미 가진
 *       상태로부터 올립니다.
 */
void world_shake(World *w, float amount);

/**
 * @brief Shakes the camera by something that happened SOMEWHERE, with falloff.
 *
 * ENGLISH
 * -------
 * @param[in,out] w      The world whose view is shaken.
 * @param[in]     at     Where it happened, world units.
 * @param[in]     radius The event's own radius in metres -- for a blast, the
 *                       radius its DAMAGE reached. Zero or less does nothing.
 * @param[in]     amount Magnitude at the centre, before the distance is taken
 *                       off. What ::world_shake would have been given.
 *
 * @note THE ONLY THING BETWEEN THIS AND ::world_shake IS THE FALLOFF. It ends
 *       in that call, so the ceiling, the "louder never the sum" rule and the
 *       decay are the ones already written and cannot disagree with them.
 * @note Measured to ::Player::pos, which is the EYE. A shake is what the camera
 *       does, so the camera is what the distance is measured to -- from the
 *       feet, a blast on the floor the player is standing on would read as
 *       1.7m further away than one at their head, which is backwards.
 * @note Reaches ::WORLD_SHAKE_BLAST_REACH times `radius` and stops. Past that
 *       the amount would round to nothing anyway, but the test is written
 *       rather than left to the arithmetic: "a blast across the level does not
 *       shake you" is a rule, and a rule that is only true because of how a
 *       multiply turned out is one that stops being true quietly.
 * @note Public for the same reason ::world_shake is -- the next source that
 *       happens at a PLACE rather than to the player is one call. A door
 *       slamming and a boss landing are both that shape.
 *
 * 한국어
 * ------
 * @brief *어딘가에서* 일어난 일로 카메라를 흔들며, 거리에 따라 감쇠시킵니다.
 * @param[in]     at     일어난 지점 (월드 단위).
 * @param[in]     radius 그 사건 자신의 반경(미터). 폭발이라면 *피해*가 닿은 반경입니다.
 * @param[in]     amount 중심에서의 크기. 거리 감쇠 이전의 값입니다.
 *
 * @note *이것과 ::world_shake의 차이는 감쇠뿐입니다.* 마지막에 그 함수를 호출하므로 상한과
 *       "합이 아니라 큰 쪽" 규칙과 감쇠는 이미 작성된 그것들이며, 그것들과 어긋날 수 없습니다.
 * @note ::Player::pos, 즉 *눈*까지 잽니다. 흔들리는 것은 카메라이므로 거리를 재는 대상도
 *       카메라입니다. 발을 기준으로 하면 플레이어가 딛고 선 바닥에서 터진 폭발이 머리 옆에서
 *       터진 것보다 1.7미터 멀다고 계산되는데, 그것은 거꾸로입니다.
 * @note `radius`의 ::WORLD_SHAKE_BLAST_REACH배까지 닿고 멈춥니다. 그 너머는 어차피 0으로
 *       반올림되지만, 산술에 맡기지 않고 검사를 써 둡니다. "레벨 건너편의 폭발은 당신을 흔들지
 *       않는다"는 것은 규칙이고, 곱셈 결과가 그렇게 나왔기 때문에만 참인 규칙은 조용히 참이기를
 *       그만둡니다.
 * @note ::world_shake와 같은 이유로 공개입니다. 플레이어에게가 아니라 *어떤 자리에서* 일어나는
 *       다음 원천도 호출 하나가 됩니다. 쾅 닫히는 문과 착지하는 보스가 모두 그 형태입니다.
 */
void world_shake_at(World *w, v3 at, float radius, float amount);

/**
 * @brief Is something big alive right now?
 *
 * ENGLISH
 * -------
 * @param[in] w The world.
 * @return Non-zero while a boss-grade monster is standing.
 *
 * THERE IS NO BOSS FLAG, and this deliberately does not add one. The bestiary
 * has five monsters and one of them is plainly the heavy: a brute has 120hp
 * against the next-toughest 40, is the only one slower than the player, and is
 * the tallest thing in the game. "A brute is up" is what a player already reads
 * as the fight getting serious, so deriving the answer from the pool costs
 * nothing and cannot fall out of step with a flag nobody remembered to set.
 *
 * @note Asked every frame by the frame loop and answered by a scan of the
 *       monster pool, which is bounded by ::ENEMY_MAX and cheap. If a later
 *       bestiary needs a real boss the answer moves into ::MonType and this
 *       function keeps its signature.
 *
 * 한국어
 * ------
 * @brief 지금 큰 것이 살아 있는가?
 * @param[in] w 월드.
 * @return 보스급 몬스터가 서 있는 동안 0이 아닌 값.
 *
 * *보스 플래그가 없으며* 이 함수는 의도적으로 만들지 않습니다. 도감에는 몬스터가 다섯 있고 그중
 * 하나가 명백히 중량급입니다. 브루트는 다음으로 강한 것이 40일 때 120hp이고, 플레이어보다 느린
 * 유일한 몬스터이며, 게임에서 가장 큽니다. "브루트가 떴다"는 것은 플레이어가 이미 전투가
 * 심각해졌다고 읽는 신호이므로, 답을 풀에서 유도하면 비용이 들지 않고 아무도 세우는 것을 잊지
 * 않은 플래그와 어긋날 일도 없습니다.
 *
 * @note 프레임 루프가 매 프레임 묻고 몬스터 풀 스캔으로 답합니다. ::ENEMY_MAX로 제한되어 있어
 *       저렴합니다. 이후의 도감이 진짜 보스를 필요로 하면 답은 ::MonType으로 옮겨 가고 이 함수는
 *       시그니처를 유지합니다.
 */
int world_boss_present(const World *w);

/**
 * @brief Advances the world by one frame.
 *
 * ENGLISH
 * -------
 * @param[in,out] w      The world.
 * @param[in]     in     This frame's intent.
 * @param[in]     aspect Viewport aspect the view model's muzzle is solved
 *                       against. See the note below.
 * @param[in]     dt     Timestep in seconds.
 * @return The ::world_frozen value this step used, for the renderer to reuse.
 *
 * @warning The order inside is load-bearing, and tools\steptest.c is what says
 *          so in a form that fails. In brief: the rope constraint resolves
 *          before the move so this frame's correction reaches this frame's
 *          position; the axe's slam resolves after it so it lands where the
 *          player actually ended up; the audio listener is set before anything
 *          can play, or every positional sound this frame is priced by where
 *          the player stood last frame; and death is noticed in exactly one
 *          place, after every source of damage, so a new source cannot forget
 *          to kill the player.
 * @note `aspect` is the WINDOW's, not the offscreen buffer's. The renderer uses
 *       the buffer's for its projection, and the two differ slightly once the
 *       buffer's width has been rounded to whole pixels. That is the behaviour
 *       this call inherited and it is preserved deliberately; whether the
 *       muzzle should be solved against the buffer instead is a real question
 *       and a separate change.
 *
 * 한국어
 * ------
 * @brief 월드를 한 프레임 진행시킵니다.
 * @param[in,out] w      월드.
 * @param[in]     in     이번 프레임의 의도.
 * @param[in]     aspect 뷰 모델 총구를 계산할 기준 뷰포트 종횡비. 아래 참고 사항을
 *                       확인하십시오.
 * @param[in]     dt     시간 간격 (초).
 * @return 이 갱신이 사용한 ::world_frozen 값. 렌더러가 재사용합니다.
 *
 * @warning 내부의 순서는 구조적으로 중요하며, tools\steptest.c가 그것을 *실패할 수 있는*
 *          형태로 진술합니다. 요약하면, 로프 구속은 이동보다 먼저 처리되어야 이번 프레임의
 *          보정이 이번 프레임의 위치에 반영됩니다. 도끼의 내려찍기는 이동 이후에 처리되어야
 *          플레이어가 실제로 도달한 지점에서 터집니다. 오디오 리스너는 무엇이든 재생되기
 *          전에 설정되어야 하며, 그렇지 않으면 이번 프레임의 모든 위치 소리가 지난 프레임의
 *          위치로 값이 매겨집니다. 그리고 사망은 모든 피해원 이후에 정확히 한 곳에서
 *          감지되어야 하며, 그래야 새로운 피해원이 플레이어를 죽이는 것을 잊을 수 없습니다.
 * @note `aspect`는 *창*의 값이며 오프스크린 버퍼의 값이 아닙니다. 렌더러는 투영에 버퍼의
 *       값을 쓰고, 버퍼의 너비가 정수 픽셀로 반올림되고 나면 둘은 미세하게 다릅니다. 이
 *       호출이 물려받은 동작이며 의도적으로 보존했습니다. 총구를 버퍼 기준으로 계산해야
 *       하는지는 실재하는 질문이고, 별개의 변경입니다.
 */
int world_step(World *w, const Input *in, float aspect, float dt);


/**
 * @enum WorldGeom
 * @brief How much of the drawn geometry a pending rebuild has to cover.
 *
 * ENGLISH
 * -------
 * The flag used to be one bit, and one bit cannot tell the two reasons apart.
 * A LEVEL WAS LOADED and A DOOR MOVED both made the drawn mesh stale, so both
 * paid for the whole level: rebuilt, re-lit and re-uploaded, every frame of
 * every swing. levelbench measured that at 0.82x the cost of the load build
 * itself, sixty times a second.
 *
 * They are not the same amount of stale. A load invalidates everything; a door
 * invalidates only what it moves. Saying which is what lets ::scene_frame's
 * neighbour ::scene_rebuild_moving do the cheap one.
 *
 * @note ORDERED, and the order is what lets ::World::geometry_dirty escalate by
 *       comparison rather than by decision -- ::world_step raises it to
 *       ::WORLD_GEOM_MOVING only when it is currently lower. A door that moves
 *       in the same frame a level loads must not downgrade the load's whole
 *       rebuild to a partial one, and taking the larger of the two says so
 *       without anyone having to remember it.
 * @note ::WORLD_GEOM_NONE is 0, so a caller that only wants to know WHETHER
 *       anything is pending can still write `if (world_take_geometry_scope(...))`
 *       and read it the way the one-bit flag read. That is what the wrapper
 *       this enum replaced existed to provide, and why it is gone: a second
 *       entry point answering a narrower version of the same question is a
 *       second thing to keep correct, and the only caller left for it was a
 *       test.
 *
 * 한국어
 * ------
 * @brief 대기 중인 재생성이 그려지는 지오메트리의 얼마만큼을 덮어야 하는가.
 *
 * 플래그는 이전에 1비트였고, 1비트는 두 가지 이유를 구별할 수 없습니다. *레벨이 로드되었다*와
 * *문이 움직였다* 둘 다 그려지는 메시를 낡게 만들었으므로 둘 다 레벨 전체의 비용을 치렀습니다.
 * 모든 여닫힘의 모든 프레임마다 다시 만들고, 다시 조명하고, 다시 올렸습니다. levelbench는
 * 그것을 로드 시 생성 자체의 0.82배로, 초당 60번 측정했습니다.
 *
 * 둘은 같은 정도로 낡은 것이 아닙니다. 로드는 전부를 무효화하고, 문은 자신이 움직이는 것만
 * 무효화합니다. 어느 쪽인지 말하는 것이 ::scene_rebuild_moving이 값싼 쪽을 택할 수 있게
 * 합니다.
 *
 * @note *순서가 있으며*, 그 순서가 ::World::geometry_dirty를 판단이 아니라 비교로
 *       승격할 수 있게 합니다. ::world_step은 현재 값이 더 낮을 때만 ::WORLD_GEOM_MOVING으로
 *       올립니다. 레벨이 로드되는 바로 그 프레임에 움직인 문이 로드의 전체 재생성을 부분
 *       재생성으로 격하시켜서는 안 되며, 둘 중 큰 쪽을 취하는 것이 아무도 그것을 기억하지
 *       않아도 그렇게 말해 줍니다.
 * @note ::WORLD_GEOM_NONE이 0이므로, 대기 중인 것이 있는지만 알고 싶은 호출자는 여전히
 *       `if (world_take_geometry_scope(...))`로 쓰고 1비트 플래그처럼 읽을 수 있습니다. 이
 *       열거형이 대체한 래퍼가 제공하려던 것이 바로 그것이며, 그것이 사라진 이유도
 *       그것입니다. 같은 질문의 더 좁은 판에 답하는 두 번째 진입점은 올바르게 유지해야 할
 *       두 번째 대상이며, 그것에 남은 유일한 호출자는 테스트였습니다.
 */
typedef enum {
    WORLD_GEOM_NONE = 0, /**< Nothing to do. / 할 일 없음. */
    WORLD_GEOM_MOVING,   /**< Only what a door moves. / 문이 움직이는 것만. */
    WORLD_GEOM_ALL       /**< All of it: a level was loaded. / 전부. 레벨이 로드되었습니다. */
} WorldGeom;

/**
 * @brief Claims the pending rebuild and says how much of it is stale.
 *
 * ENGLISH
 * -------
 * @param[in,out] w       The world.
 * @param[out]    dynamic Receives what to pass ::scene_build_level as its
 *                        `dynamic` argument: 0 the first time, so the mesh is
 *                        created, and 1 afterwards, so an existing allocation
 *                        is replaced. May be NULL.
 * @return ::WORLD_GEOM_NONE when there is nothing pending, otherwise how much.
 *
 * @note "Take", not "test": the flag is cleared and the upload is recorded as
 *       having happened, so a caller that asks must then rebuild. That is the
 *       trade for the caller never having to work out `dynamic` itself, which
 *       was previously a literal 0 at one call site and a literal 1 at two
 *       others.
 * @note This is the whole of the interface. A wrapper that flattened the scope
 *       to a yes/no stood beside it until its last non-test caller went away;
 *       see the second note on ::WorldGeom for why it did not survive that.
 *
 * 한국어
 * ------
 * @brief 대기 중인 재생성을 가져오며, 그중 얼마가 낡았는지 함께 말합니다.
 *
 * @param[in,out] w       월드.
 * @param[out]    dynamic ::scene_build_level의 `dynamic` 인자로 넘길 값을 받습니다. 첫
 *                        번째는 0이어서 메시가 생성되고, 이후는 1이어서 기존 할당이
 *                        교체됩니다. NULL이어도 됩니다.
 * @return 대기 중인 것이 없으면 ::WORLD_GEOM_NONE, 아니면 그 범위.
 *
 * @note "검사"가 아니라 "가져오기"입니다. 플래그가 지워지고 업로드가 일어난 것으로
 *       기록되므로, 물어본 호출자는 반드시 재생성해야 합니다. 그것이 호출자가 `dynamic`을
 *       스스로 따지지 않아도 되는 것의 대가입니다. 그 값은 이전에 한 호출 지점에서는 리터럴
 *       0이었고 다른 두 곳에서는 리터럴 1이었습니다.
 * @note 이것이 인터페이스의 전부입니다. 범위를 예/아니오로 뭉개는 래퍼가 곁에 있었으나,
 *       테스트가 아닌 마지막 호출자가 사라지면서 함께 사라졌습니다. 그것이 살아남지 못한
 *       이유는 ::WorldGeom의 두 번째 참고 사항을 보십시오.
 */
WorldGeom world_take_geometry_scope(World *w, int *dynamic);

#endif /* WORLD_H */
