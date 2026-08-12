/**
 * @file audio.h
 * @brief 런타임에 합성된 사운드를 처리합니다.
 *
 * 이 모듈은 샘플된 오디오를 사용하지 않습니다. 사운드는 assets/sounds.txt에 있는
 * 몇 개의 정수 값으로 정의되며, 이 값들은 어떤 오실레이터를 실행하고 피치와 볼륨이
 * 어떻게 변하는지를 명시합니다. 믹서는 재생 중에 파형을 생성합니다. 이는 텍스처와
 * 동일한 트레이드오프입니다: 샷건 발사음은 16비트 PCM으로 약 60KB 대신, 약 30바이트의
 * 레시피로 표현됩니다.
 *
 * 백엔드는 winmm의 waveOut을 사용하며, 이미 링크되어 있어 추가 라이브러리가 필요 없습니다.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include "m.h"        /* v3 -- a leaf type; audio stays free of everything else */

/* HOW FAR A SOUND CARRIES, in metres.
   Full volume within NEAR, silent at FAR, straight line between -- which is
   Doom's model rather than an inverse square. Inverse square is what physics
   does and it is wrong here: it drops off so fast that a monster two rooms
   away is inaudible while one across the room is still deafening, and the
   band where a sound is quiet-but-informative is where all the play is.
   소리가 얼마나 멀리 가는지를 미터로 나타냅니다. NEAR 안에서는 최대 음량, FAR에서
   무음, 그 사이는 직선입니다. 역제곱이 아니라 Doom의 모델입니다. 역제곱은 물리적으로는
   맞지만 여기서는 틀립니다. 감쇠가 너무 빨라 두 방 건너의 몬스터는 들리지 않는데 같은
   방의 몬스터는 여전히 귀청이 떨어지며, 정작 플레이가 일어나는 곳은 소리가 작지만
   정보를 주는 그 구간입니다. */
#define AUDIO_NEAR  5.0f
#define AUDIO_FAR  34.0f

/**
 * @brief 오디오 장치를 열고 믹싱 스레드를 시작합니다.
 *
 * 사용 가능한 출력 장치가 없으면 0을 반환합니다. 이 경우 다른 모든 호출은
 * 무해한 no-op이 되며, 게임은 시작을 거부하는 대신 조용히 실행됩니다.
 * @return 성공 시 1, 실패 시 0을 반환합니다.
 */
int audio_init(void);

/**
 * @brief 오디오 장치를 닫고 믹싱 스레드를 종료합니다.
 */
void audio_shutdown(void);

/**
 * @brief 지정된 이름의 사운드를 재생합니다.
 *
 * 믹서가 실행 중인 동안 게임 스레드에서 호출해도 안전합니다. 보이스 테이블은
 * 락으로 보호됩니다. 알 수 없는 이름은 무시됩니다.
 * @param name 재생할 사운드의 이름.
 * @param gain 사운드 레시피의 자체 레벨을 조절하는 0-100 사이의 게인 값.
 *             거리 감쇠 등은 레시피가 아닌 호출자에게 달려 있습니다.
 */
void audio_play(const char *name, int gain);

/**
 * @brief Sets where the player's ears are, in world metres.
 *
 * ENGLISH
 * -------
 * @param[in] pos The listener's position.
 * @note Call once a frame, before anything plays. ::audio_play_at reads it to
 *       work out how far away a sound is, and a stale listener makes every
 *       sound in that frame loud or quiet by where the player USED to be.
 * @note Read on the game thread only, by audio_play_at, so it needs no lock:
 *       the mixer never sees it. That is deliberate -- a voice stores the gain
 *       it was given and never looks at the world again, so the mixer stays
 *       free of game state and a sound cannot change volume because the thing
 *       that made it moved or died.
 *
 * 한국어
 * ------
 * @brief 플레이어의 귀가 어디 있는지를 월드 미터 단위로 설정합니다.
 * @param[in] pos 청취자의 위치.
 * @note 무언가 재생되기 전에 프레임당 한 번 호출하십시오. 오래된 청취자 위치는 그
 *       프레임의 모든 소리를 플레이어가 *있었던* 자리 기준으로 키우거나 줄입니다.
 * @note audio_play_at이 게임 스레드에서만 읽으므로 락이 필요 없습니다. 믹서는 이를
 *       보지 않습니다. 이는 의도적입니다. 보이스는 주어진 음량을 저장할 뿐 다시는
 *       월드를 보지 않으므로, 믹서는 게임 상태로부터 자유롭게 유지되고 소리를 낸 대상이
 *       움직이거나 죽었다고 해서 소리의 음량이 변할 수 없습니다.
 */
void audio_listener(v3 pos);

/**
 * @brief Plays a sound at a world position, quieter the further away it is.
 *
 * ENGLISH
 * -------
 * @param[in] name The sound's name.
 * @param[in] gain 0-100, what it would be worth at the listener's feet.
 * @param[in] pos  Where in the world it happens.
 * @note Distance is taken ONCE, when the sound starts, the way Doom does it.
 *       A one-shot effect that tracked its emitter would need the mixer to
 *       read the world every buffer, and would swell as a corpse slid past.
 * @note Beyond ::AUDIO_FAR nothing is queued at all, so a firefight across the
 *       level cannot spend every voice on sounds that would be inaudible.
 *
 * 한국어
 * ------
 * @brief 월드의 한 지점에서 소리를 재생하며, 멀수록 조용해집니다.
 * @param[in] name 사운드 이름.
 * @param[in] gain 0-100. 청취자의 발밑에서 났다면 가졌을 음량.
 * @param[in] pos  월드에서 그 일이 일어나는 지점.
 * @note 거리는 소리가 *시작될 때* 한 번만 측정합니다. Doom과 같은 방식입니다. 발생원을
 *       계속 따라가는 일회성 효과음은 믹서가 버퍼마다 월드를 읽어야 하고, 시체가 미끄러져
 *       지나가면 소리가 커지게 됩니다.
 * @note ::AUDIO_FAR를 넘으면 아예 큐에 넣지 않으므로, 레벨 반대편의 총격전이 들리지도
 *       않을 소리에 보이스를 전부 쓰는 일이 없습니다.
 */
void audio_play_at(const char *name, int gain, v3 pos);

#ifdef HOT_RELOAD
/**
 * @brief What `gain` is worth from `pos`, given the current listener.
 *
 * ENGLISH
 * -------
 * @param[in] gain 0-100 at the listener's feet.
 * @param[in] pos  Where the sound would happen.
 * @return The attenuated gain, 0 past ::AUDIO_FAR.
 * @note Exposed so a test can ask the SAME function ::audio_play_at uses. A
 *       test that reimplemented the curve would pass while the game applied a
 *       different one, which is the failure it exists to catch.
 *
 * 한국어
 * ------
 * @brief 현재 청취자를 기준으로 `pos`에서의 `gain` 값을 반환합니다.
 * @note 테스트가 ::audio_play_at이 쓰는 것과 *같은* 함수에 물어볼 수 있도록 노출합니다.
 *       곡선을 다시 구현한 테스트는 게임이 다른 곡선을 쓰는 동안에도 통과하며, 그것이
 *       이 함수가 막으려는 실패입니다.
 */
int audio_gain_at(int gain, v3 pos);
#endif

/**
 * @brief 파싱된 캐시를 삭제하여 다음 재생 시 레시피 텍스트를 다시 읽도록 합니다.
 *
 * 핫 리로딩 경로에서 호출됩니다. 텍스트가 변경될 수 없는 릴리스 빌드에서는
 * no-op 비용입니다.
 */
void audio_reload(void);

/**
 * @brief 하나의 사운드를 장치를 건드리지 않고 `out` 버퍼에 렌더링합니다.
 *
 * 믹서와 동일한 신디사이저를 실행하므로, 도구에서 보는 것이 게임에서 재생되는 것과
 * 동일합니다. 별도의 오프라인 복사본은 동기화되지 않을 수 있습니다.
 * 샘플 레이트는 audio_rate() 함수로 얻을 수 있습니다.
 * @param name 렌더링할 사운드의 이름.
 * @param out 렌더링된 오디오 데이터를 저장할 버퍼.
 * @param max_frames 렌더링할 최대 프레임 수.
 * @return 기록된 프레임 수를 반환합니다.
 *
 * @warning Offline/tool use only. This is the one entry point that works with
 *          no device open (tools/sndtest.c never calls audio_init), but it
 *          renders from a recipe pointer held across the render loop. Calling
 *          it concurrently with audio_reload() from another thread is NOT
 *          supported -- the reload could rewrite the recipe mid-render. The
 *          game never does this: it uses audio_play(), which is fully locked.
 * @warning 오프라인/도구 전용입니다. 장치가 열리지 않은 상태에서 동작하는 유일한
 *          진입점이지만(tools/sndtest.c는 audio_init을 호출하지 않습니다), 렌더링
 *          루프 전체에 걸쳐 레시피 포인터를 보유한 채 렌더링합니다. 다른 스레드의
 *          audio_reload()와 동시에 호출하는 것은 지원하지 않습니다. 리로드가 렌더링
 *          도중 레시피를 재작성할 수 있기 때문입니다. 게임은 이런 호출을 하지 않으며,
 *          완전히 락으로 보호되는 audio_play()를 사용합니다.
 */
int audio_render(const char *name, short *out, int max_frames);

/**
 * @brief 오디오 시스템의 샘플 레이트를 반환합니다.
 *
 * @return 초당 샘플 수 (Hz).
 */
int audio_rate(void);

#ifdef HOT_RELOAD
/**
 * @brief Decodes one character of the sampled-sound alphabet.
 *
 * ENGLISH
 * -------
 * @param[in] c A character from a `w` data line.
 * @return Its 0-63 value, or -1 if it is not in the alphabet.
 * @note Exposed so a test can compare this against the alphabet bake.ps1
 *       encodes with. That contract spans PowerShell and C, no compiler can
 *       see it, and if the two ever disagree every sampled sound decodes to
 *       noise -- which sounds like a bad recording rather than like a bug.
 *
 * 한국어
 * ------
 * @brief 샘플 사운드 알파벳의 한 문자를 해석합니다.
 * @param[in] c `w` 데이터 줄의 문자.
 * @return 0-63 값, 알파벳에 없으면 -1.
 * @note bake.ps1이 인코딩에 쓰는 알파벳과 비교할 수 있도록 노출합니다. 이 계약은
 *       PowerShell과 C에 걸쳐 있어 어떤 컴파일러도 볼 수 없으며, 둘이 어긋나면 모든
 *       샘플 사운드가 잡음으로 디코딩됩니다. 그것은 버그가 아니라 녹음이 나쁜 것처럼
 *       들립니다.
 */
int audio_b64val(char c);
#endif

#endif
