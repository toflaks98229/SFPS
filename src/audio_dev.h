/**
 * @file audio_dev.h
 * @brief The line between what audio does and what a machine plays it on.
 *
 * ENGLISH
 * -------
 * audio.c decides what a sound is: which layers, which envelope, which voice
 * gets evicted when all twelve are busy, how a gain falls off with distance.
 * None of that is about Windows. audio_win32.c owns the other half -- opening
 * waveOut, preparing four buffers, running a thread that refills them -- and
 * all of it is.
 *
 * THE TRAFFIC CROSSES IN BOTH DIRECTIONS, which is why this header exists
 * rather than one side simply including the other:
 *
 *   device -> policy   ::audio_mix, called from the mixer thread when a buffer
 *                      comes back empty and needs filling.
 *   policy -> device   ::audio_dev_lock, because the voice table the policy
 *                      writes is the same one the mixer is reading.
 *
 * PRIVATE TO THOSE TWO FILES. audio.h is the public API -- audio_play,
 * audio_listener, audio_init -- and nothing outside this pair should include
 * this. The short constant names below are safe only because of that.
 *
 * TO ADD A HOST: write audio_alsa.c, implementing audio_init, audio_shutdown
 * and the two lock functions, and calling audio_mix when it wants samples.
 * audio.c does not change.
 *
 * 한국어
 * ------
 * audio.c는 소리가 무엇인지를 결정합니다. 어떤 레이어인지, 어떤 엔벨로프인지, 열두 개가 모두
 * 사용 중일 때 어떤 보이스를 밀어낼지, 거리에 따라 음량이 어떻게 감쇠하는지. 그 어느 것도
 * Windows에 관한 것이 아닙니다. audio_win32.c가 나머지 절반을 소유합니다. waveOut을 열고,
 * 버퍼 네 개를 준비하고, 그것들을 다시 채우는 스레드를 돌리는 일이며, 그 전부가 Windows에
 * 관한 것입니다.
 *
 * 통행이 *양방향*이며, 그것이 한쪽이 다른 쪽을 포함하는 대신 이 헤더가 존재하는 이유입니다.
 *
 *   장치 -> 정책   ::audio_mix. 버퍼가 비어 돌아와 채워야 할 때 믹서 스레드에서 호출됩니다.
 *   정책 -> 장치   ::audio_dev_lock. 정책이 쓰는 보이스 표가 믹서가 읽고 있는 바로 그것이기
 *                  때문입니다.
 *
 * 이 두 파일 전용입니다. 공개 API는 audio.h(audio_play, audio_listener, audio_init)이며, 이
 * 짝 바깥의 어떤 것도 이 헤더를 포함해서는 안 됩니다. 아래의 짧은 상수 이름이 안전한 것은
 * 오직 그 때문입니다.
 *
 * 호스트를 추가하려면: audio_alsa.c를 쓰고 audio_init, audio_shutdown과 두 락 함수를 구현한
 * 뒤, 샘플이 필요할 때 audio_mix를 호출하십시오. audio.c는 바뀌지 않습니다.
 */
#ifndef AUDIO_DEV_H
#define AUDIO_DEV_H

/* Shared because both halves need them and a second copy of a buffer size is
   how two files come to disagree about how long a buffer is.
   양쪽 절반 모두 필요로 하므로 공유합니다. 버퍼 크기의 두 번째 사본은 두 파일이 버퍼 길이에
   대해 서로 다른 말을 하게 되는 방식입니다. */
#define RATE        44100   ///< @brief 오디오 샘플 레이트 (Hz).
#define FRAMES      512     ///< @brief 버퍼 당 프레임 수. 약 11.6ms에 해당하며, 약 46ms의 지연 시간을 가집니다.
#define NBUF        4       ///< @brief 오디오 버퍼의 수.

/* How long the mixer spends inside one pass before it looks at g_running again.
 *
 * ENGLISH
 * -------
 * ZERO in every build but one, and `if (0)` costs nothing. It exists because
 * ::audio_shutdown's join has a 500ms deadline and the branch it takes when
 * that deadline is missed is the branch no machine reaches on purpose -- a
 * driver would have to block. Code that only runs when something has gone
 * wrong is the code that goes untested, which is the same argument build.ps1
 * makes for LIGHT_CACHE_SLOTS and MAX_CACHED; this is that argument applied to
 * a deadline rather than to a table.
 *
 * Forced past the deadline, audiorace_stuckmixer asserts what the fix is
 * for: that shutdown returns rather than closing the device and deleting the
 * lock underneath a thread that is still inside both.
 *
 * 한국어
 * ------
 * @brief 믹서가 g_running을 다시 보기까지 한 번의 패스에 쓰는 시간.
 *
 * 하나를 뺀 모든 빌드에서 0이며 `if (0)`은 비용이 없습니다. 이것이 존재하는 이유는
 * ::audio_shutdown의 합류에 500ms 기한이 있고, 그 기한을 놓쳤을 때 타는 갈래는 어떤 기계도
 * 일부러 도달하지 않는 갈래이기 때문입니다. 드라이버가 막혀야만 합니다. 무언가 잘못되었을
 * 때만 실행되는 코드가 곧 시험되지 않는 코드이며, 이는 build.ps1이 LIGHT_CACHE_SLOTS와
 * MAX_CACHED에 대해 펴는 것과 같은 논거입니다. 표가 아니라 기한에 그 논거를 적용한 것이
 * 이것입니다.
 *
 * 기한을 넘기도록 강제하면 audiorace_stuckmixer가 이 수정이 무엇을 위한 것인지 단언합니다.
 * 종료 함수가, 여전히 장치와 락 안에 있는 스레드 밑에서 그 둘을 닫고 삭제하는 대신
 * 반환한다는 것입니다.
 */
#ifndef AUDIO_MIXER_STALL_MS
#define AUDIO_MIXER_STALL_MS 0
#endif


/**
 * @brief Mixes every live voice into one buffer. Called by the device.
 *
 * ENGLISH
 * -------
 * @param[out] out    Destination, `frames` samples of 16-bit mono.
 * @param[in]  frames How many to produce. Every one is written, silence
 *                    included -- the caller hands this straight to hardware
 *                    and a partially filled buffer would play whatever was
 *                    there last.
 * @warning Runs on the MIXER thread. It takes ::audio_dev_lock itself, because
 *          the voice table it walks is written by the game thread.
 *
 * 한국어
 * ------
 * @brief 살아 있는 모든 보이스를 하나의 버퍼로 믹싱합니다. 장치가 호출합니다.
 * @param[out] out    대상. 16비트 모노 `frames` 샘플.
 * @param[in]  frames 생성할 개수. 무음을 포함해 전부 기록됩니다. 호출자가 이것을 하드웨어에
 *                    그대로 넘기므로, 일부만 채운 버퍼는 직전에 있던 것을 재생합니다.
 * @warning *믹서* 스레드에서 실행됩니다. 순회하는 보이스 표를 게임 스레드가 쓰므로
 *          ::audio_dev_lock을 스스로 획득합니다.
 */
void audio_mix(short *out, int frames);

/**
 * @brief Takes the lock guarding the voice table, if there is one to take.
 *
 * ENGLISH
 * -------
 * The return value is the whole subtlety, and it used to be a separate flag
 * that every caller tested by hand. There may be NO DEVICE: tools/sndtest.c
 * renders sounds offline and never calls audio_init, and a machine with no
 * sound card gets the same result. In that case there is no mixer thread to
 * race with and no lock to enter -- and entering one that was never
 * initialised is not a missed optimisation, it is the bug.
 *
 * Folding that test in here is what lets audio.c stop knowing about it. The
 * gate and the lock it gates are one thing, and they were two.
 *
 * @return 1 if the lock is now held and ::audio_dev_unlock must be called, 0
 *         if there is no device and the caller may proceed unlocked.
 * @warning Do NOT call ::audio_dev_unlock after a return of 0.
 *
 * 한국어
 * ------
 * @brief 보이스 표를 지키는 락을 획득합니다. 획득할 락이 있다면.
 *
 * 반환값이 미묘함의 전부이며, 이전에는 모든 호출자가 손으로 검사하던 별도 플래그였습니다.
 * *장치가 없을 수 있습니다.* tools/sndtest.c는 사운드를 오프라인으로 렌더링하며 audio_init을
 * 결코 호출하지 않고, 사운드 카드가 없는 기계도 같은 결과를 얻습니다. 그 경우 경쟁할 믹서
 * 스레드도 없고 진입할 락도 없습니다. 그리고 초기화된 적 없는 락에 진입하는 것은 놓친 최적화가
 * 아니라 버그 그 자체입니다.
 *
 * 그 검사를 이곳에 접어 넣는 것이 audio.c가 그것을 모르게 해 주는 방법입니다. 게이트와 그것이
 * 지키는 락은 하나이며, 둘로 나뉘어 있었습니다.
 *
 * @return 락을 획득했고 ::audio_dev_unlock을 호출해야 하면 1. 장치가 없어 호출자가 락 없이
 *         진행해도 되면 0.
 * @warning 0이 반환된 뒤에 ::audio_dev_unlock을 호출하지 *마십시오*.
 */
int audio_dev_lock(void);

/**
 * @brief Releases the lock taken by a ::audio_dev_lock that returned 1.
 *
 * 한국어
 * ------
 * @brief 1을 반환한 ::audio_dev_lock이 획득한 락을 해제합니다.
 */
void audio_dev_unlock(void);

#endif /* AUDIO_DEV_H */
