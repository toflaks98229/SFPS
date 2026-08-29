/**
 * @file audio.h
 * @brief Sound synthesised at runtime from recipes, and the mixer that plays it.
 *
 * ENGLISH
 * -------
 * There is no sampled audio in the general case. A sound is a handful of
 * integers in assets/sounds.txt naming which oscillator to run and how the
 * pitch and the volume move, and the mixer generates the waveform as it plays.
 * That is the same trade the textures make: a shotgun blast is about thirty
 * bytes of recipe instead of roughly 60KB of 16-bit PCM.
 *
 * Some sounds ARE recorded -- the ones imported from Freedoom arrive as 4-bit
 * ADPCM the bake produced from a WAV. A recipe and a sample are the same thing
 * to every caller here; audio.c decides which path a name takes.
 *
 * The backend is winmm's waveOut, which is already linked and needs no extra
 * library.
 *
 * @note ::audio_render is the only entry point that works with no device open.
 *       Everything else is a harmless no-op until ::audio_init succeeds.
 * @note Call these from the game thread. The mixer runs on its own thread and
 *       audio.c holds the lock that keeps the two apart.
 *
 * 한국어
 * ------
 * 일반적인 경우 샘플된 오디오는 없습니다. 사운드는 assets/sounds.txt에 있는 정수 몇
 * 개이며, 어떤 오실레이터를 실행하고 음높이와 음량이 어떻게 움직이는지를 지정합니다. 믹서는
 * 재생하면서 파형을 생성합니다. 텍스처가 택한 것과 같은 절충입니다. 샷건 발사음은 16비트
 * PCM 약 60KB 대신 약 30바이트의 레시피입니다.
 *
 * 일부 사운드는 *녹음된 것*입니다. Freedoom에서 이식한 것들은 베이크가 WAV로 만든 4비트
 * ADPCM으로 들어옵니다. 이곳의 모든 호출자에게 레시피와 샘플은 같은 것이며, 어떤 이름이
 * 어느 경로를 타는지는 audio.c가 결정합니다.
 *
 * 백엔드는 winmm의 waveOut을 사용하며, 이미 링크되어 있어 추가 라이브러리가 필요 없습니다.
 *
 * @note 장치가 열리지 않은 상태에서 동작하는 유일한 진입점은 ::audio_render입니다. 그 밖의
 *       모든 것은 ::audio_init이 성공하기 전까지 무해한 no-op입니다.
 * @note 이곳의 함수들은 게임 스레드에서 호출하십시오. 믹서는 자기 스레드에서 실행되며 둘을
 *       갈라놓는 락은 audio.c가 보유합니다.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include "m.h"        /* v3 -- a leaf type; audio stays free of everything else */

/* --- Distance / 거리 --- */

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

/* --- Loudness / 음량 --- */

/* HOW LOUD EVERYTHING IS, as a fraction of what the mixer would otherwise
   produce. The game was simply too loud, and 0.70 is that judgement written
   down where it can be found and changed.

   ONE CONSTANT BECAUSE THERE ARE TWO MIXING PATHS. A sampled sound and a
   generated recipe reach the output through different code with different
   scale factors -- 0.55 against a decoded sample, 8000 against an oscillator
   sum -- and those two numbers set the BALANCE between the two kinds, which is
   tuned and must not move. Turning the volume down by editing both is two
   edits that have to agree, and the last time this file had one rule living in
   two places (the `!s->n` test for a sampled sound) the copies disagreed and
   samples went silent. This multiplies both, so the balance is preserved by
   construction and loudness has exactly one home.

   전체 음량이며, 믹서가 원래 낼 소리에 대한 비율입니다. 게임이 너무 시끄러웠고, 0.70은
   그 판단을 찾고 바꿀 수 있는 곳에 적어 둔 것입니다.

   믹싱 경로가 둘이기 때문에 상수는 하나입니다. 샘플 사운드와 생성된 레시피는 서로 다른
   코드와 서로 다른 배율(디코딩된 샘플에 0.55, 오실레이터 합에 8000)로 출력에 도달하며,
   그 두 숫자는 두 종류 사이의 *균형*을 정합니다. 이미 조정된 값이므로 움직여서는 안
   됩니다. 둘을 각각 편집해 음량을 낮추는 것은 서로 일치해야 하는 편집 두 번입니다. 이
   파일에서 하나의 규칙이 두 곳에 살았을 때(샘플 사운드에 대한 `!s->n` 검사) 사본이
   어긋나 샘플이 무음이 되었습니다. 이것은 둘 다에 곱해지므로 균형은 구조적으로 보존되고
   음량이 사는 곳은 정확히 하나입니다. */
#define AUDIO_MASTER 0.70f

/* --- Capacities / 용량 --- */

/* HOW MANY SOUNDS THE TABLE HOLDS. Public so a test can check the recipe text
   against it by NAME rather than discovering the overflow as some unrelated
   sound going quiet -- which is how the last one was found: the cap was 24 and
   there were exactly 24 sounds, so adding one dropped `switch`, and the
   symptom was a door that stopped clicking.
   표가 담는 사운드 수입니다. 테스트가 레시피 텍스트를 *이름으로* 대조할 수 있도록
   공개합니다. 상관없는 사운드가 조용해지는 것으로 넘침을 발견하지 않기 위해서입니다.
   지난번이 그랬습니다. 상한이 24이고 사운드도 정확히 24개여서 하나를 더하자 `switch`가
   버려졌고, 증상은 더 이상 딸깍이지 않는 문이었습니다. */
#define AUDIO_MAX_SOUNDS 40

/**
 * @brief Voices reserved for music, out of the mixer's total.
 *
 * ENGLISH: Four, leaving eight for effects. The split exists because the voice
 * allocator evicts the OLDEST when it runs out, and a music note is always
 * older than the shot that just fired -- one shared pool has the music cut the
 * gunfire. See the note beside SFX_VOICES in audio.c.
 *
 * 한국어: 넷이며 효과음에 여덟이 남습니다. 이 분할이 존재하는 이유는 보이스 할당기가 부족할 때
 * 가장 *오래된* 것을 밀어내는데 음악 음표는 방금 발사된 총성보다 언제나 오래되기 때문입니다.
 * 풀을 공유하면 음악이 총성을 끊습니다. audio.c의 SFX_VOICES 곁 주석을 참조하십시오.
 */
#define MUSIC_VOICES 4

/* --- Lifecycle / 수명 주기 --- */

/**
 * @brief Opens the audio device and starts the mixing thread.
 *
 * ENGLISH
 * -------
 * @return 1 on success, 0 if no output device could be opened.
 *
 * @note A failure is not fatal and needs no handling. Every other call in this
 *       header becomes a harmless no-op, so the game runs silently rather than
 *       refusing to start.
 *
 * 한국어
 * ------
 * @brief 오디오 장치를 열고 믹싱 스레드를 시작합니다.
 *
 * @return 성공하면 1, 출력 장치를 열 수 없었으면 0입니다.
 *
 * @note 실패는 치명적이지 않으며 따로 처리할 필요가 없습니다. 이 헤더의 다른 모든 호출이
 *       무해한 no-op이 되므로, 게임은 시작을 거부하는 대신 조용히 실행됩니다.
 */
int audio_init(void);

/**
 * @brief Closes the device and stops the mixing thread.
 *
 * ENGLISH
 * -------
 * @note Safe to call whether or not ::audio_init succeeded.
 *
 * 한국어
 * ------
 * @brief 오디오 장치를 닫고 믹싱 스레드를 종료합니다.
 *
 * @note ::audio_init의 성공 여부와 무관하게 호출해도 안전합니다.
 */
void audio_shutdown(void);

/* --- Settings / 설정 --- */

/**
 * @brief Sets overall, effect and music loudness, 0-100 each.
 *
 * ENGLISH
 * -------
 * @param[in] master Scales everything. 0 is silence.
 * @param[in] sfx    Scales effects, under `master`. Both are applied.
 * @param[in] music  Scales ::audio_note, under `master`. Both are applied.
 *
 * @note Applied when a sound STARTS, not per sample. A sound already playing
 *       keeps the loudness it began with; every effect here is a fraction of a
 *       second, so the longest a change takes to be heard in full is one sound.
 *       The alternative is the mixer reading two globals per sample.
 * @note Game thread only, like ::audio_listener. The mixer never reads these.
 * @note Values outside 0-100 are clamped rather than rejected.
 *
 * 한국어
 * ------
 * @brief 전체·효과음·음악 음량을 설정합니다. 각각 0-100입니다.
 *
 * @param[in] master 모든 것을 조정합니다. 0이면 무음입니다.
 * @param[in] sfx    `master` 아래에서 효과음을 조정합니다. 둘 다 적용됩니다.
 * @param[in] music  `master` 아래에서 ::audio_note를 조정합니다. 둘 다 적용됩니다.
 *
 * @note 샘플마다가 아니라 소리가 *시작될 때* 적용됩니다. 이미 재생 중인 소리는 시작할 때의
 *       음량을 유지합니다. 이곳의 모든 효과음이 1초 미만이므로 변경이 온전히 들리기까지 걸리는
 *       최대 시간은 소리 하나입니다. 대안은 믹서가 샘플마다 전역 둘을 읽는 것입니다.
 * @note ::audio_listener와 마찬가지로 게임 스레드 전용입니다. 믹서는 이 값을 읽지 않습니다.
 * @note 0-100을 벗어난 값은 거절하지 않고 범위 안으로 제한합니다.
 */
void audio_set_volume(int master, int sfx, int music);

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

/* --- Playback / 재생 --- */

/**
 * @brief Plays the sound with the given name.
 *
 * ENGLISH
 * -------
 * @param[in] name Name from assets/sounds.txt. An unknown name is ignored.
 * @param[in] gain 0-100, scaling the recipe's own level. Distance attenuation
 *                 and anything else is the caller's business, not the
 *                 recipe's -- see ::audio_play_at for the usual way to get it.
 *
 * @note Safe to call from the game thread while the mixer is running; the
 *       voice table is under a lock.
 * @note Takes an EFFECT voice, and evicts the oldest effect if all of them are
 *       busy. It can never evict a music note.
 *
 * 한국어
 * ------
 * @brief 지정된 이름의 사운드를 재생합니다.
 *
 * @param[in] name assets/sounds.txt의 이름. 알 수 없는 이름은 무시됩니다.
 * @param[in] gain 0-100. 레시피 자체의 레벨을 조정합니다. 거리 감쇠를 비롯한 나머지는
 *                 레시피가 아니라 호출자의 몫입니다. 보통의 방법은 ::audio_play_at을
 *                 참조하십시오.
 *
 * @note 믹서가 실행 중인 동안 게임 스레드에서 호출해도 안전합니다. 보이스 표는 락으로
 *       보호됩니다.
 * @note *효과음* 보이스를 가져가며, 모두 사용 중이면 가장 오래된 효과음을 밀어냅니다. 음악
 *       음표는 결코 밀어낼 수 없습니다.
 */
void audio_play(const char *name, int gain);

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

/**
 * @brief Sounds one bare note. music.c's only way into the mixer.
 *
 * ENGLISH
 * -------
 * @param[in] wave   0 square, 1 saw, 2 sine, 3 noise -- as a recipe layer's.
 * @param[in] freq   Hz. Held for the whole note; there is no sweep.
 * @param[in] dur_ms How long it sounds.
 * @param[in] gain   0-100, before the master and music settings scale it.
 *
 * @note Takes a MUSIC voice and can only evict another note, never an effect.
 * @note A note is built as a one-layer recipe, so ::audio_mix renders it with
 *       the same oscillator and envelope code every sound effect uses. That is
 *       why music needed no changes to the mixer at all.
 *
 * 한국어
 * ------
 * @brief 맨 음표 하나를 소리 냅니다. music.c가 믹서로 들어가는 유일한 통로입니다.
 * @param[in] wave   0 사각파, 1 톱니, 2 사인, 3 노이즈. 레시피 레이어와 같습니다.
 * @param[in] freq   Hz. 음표 내내 유지되며 스윕이 없습니다.
 * @param[in] dur_ms 지속 시간.
 * @param[in] gain   0-100. 마스터와 음악 설정이 조정하기 전의 값입니다.
 *
 * @note *음악* 보이스를 가져가며, 다른 음표만 밀어낼 수 있을 뿐 효과음은 결코 밀어내지 못합니다.
 * @note 음표는 1레이어 레시피로 만들어지므로 ::audio_mix가 모든 효과음이 쓰는 것과 같은
 *       오실레이터·엔벨로프 코드로 렌더링합니다. 그래서 음악에 믹서 변경이 전혀 필요 없었습니다.
 */
void audio_note(int wave, int freq, int dur_ms, int gain);

/* --- Reload and introspection / 재적재와 조회 --- */

/**
 * @brief Drops the parsed cache so the next play re-reads the recipe text.
 *
 * ENGLISH
 * -------
 * @note Called from the hot-reload path. In a shipped build, where the text
 *       cannot change, it costs one flag write and nothing re-reads anything.
 * @note Does NOT reparse here. The next ::audio_play does it, on the game
 *       thread and under the lock, which is what keeps a reload from rewriting
 *       the recipe table while the mixer is reading it.
 *
 * 한국어
 * ------
 * @brief 파싱된 캐시를 버려, 다음 재생 때 레시피 텍스트를 다시 읽도록 합니다.
 *
 * @note 핫 리로드 경로에서 호출됩니다. 텍스트가 바뀔 수 없는 배포 빌드에서는 플래그 하나를
 *       쓰는 비용뿐이며 다시 읽는 것은 없습니다.
 * @note 이곳에서 다시 파싱하지 *않습니다*. 다음 ::audio_play가 게임 스레드에서 락을 보유한
 *       채 수행하며, 그것이 믹서가 읽고 있는 동안 재적재가 레시피 표를 다시 쓰는 일을
 *       막습니다.
 */
void audio_reload(void);

/**
 * @brief How many sounds the recipe text defined.
 *
 * ENGLISH
 * -------
 * @return The count, at most ::AUDIO_MAX_SOUNDS.
 * @note For checking the text against the table BY NAME. Anything past the cap
 *       is parsed and thrown away, so the count saturating is the only sign
 *       from inside that a sound went missing.
 * @note Parses on demand, because a tool may ask before anything has played.
 *
 * 한국어
 * ------
 * @brief 레시피 텍스트가 정의한 사운드의 수를 반환합니다.
 *
 * @return 개수이며, 최대 ::AUDIO_MAX_SOUNDS입니다.
 * @note 텍스트를 표와 *이름으로* 대조하기 위한 것입니다. 상한을 넘은 것은 파싱된 뒤
 *       버려지므로, 개수가 포화되었다는 것이 내부에서 사운드가 사라졌음을 알 수 있는
 *       유일한 신호입니다.
 * @note 아무것도 재생되기 전에 도구가 물어볼 수 있으므로 필요할 때 파싱합니다.
 */
int audio_sound_count(void);

/**
 * @brief The mixer's sample rate.
 *
 * ENGLISH
 * -------
 * @return Samples per second, in Hz.
 * @note Needed to interpret what ::audio_render writes. It is a constant, but
 *       exposed as a call so nothing outside audio.c has to hard-code it.
 *
 * 한국어
 * ------
 * @brief 믹서의 샘플 레이트.
 *
 * @return 초당 샘플 수(Hz).
 * @note ::audio_render가 기록한 것을 해석하는 데 필요합니다. 상수이지만, audio.c 바깥의
 *       무엇도 이 값을 직접 박아 넣지 않도록 호출로 노출합니다.
 */
int audio_rate(void);

/* --- Offline rendering / 오프라인 렌더링 --- */

/**
 * @brief Renders one sound into `out` without touching the device.
 *
 * ENGLISH
 * -------
 * @param[in]  name       Sound to render. An unknown name renders nothing.
 * @param[out] out        Buffer for the samples. Must hold `max_frames`.
 * @param[in]  max_frames Capacity of `out`, in samples.
 * @return Frames written, 0 if the name is unknown or has neither layers nor
 *         samples. Interpret them at ::audio_rate.
 *
 * @note Runs the SAME synthesiser the mixer does, so what a tool sees is what
 *       the game plays. A separate offline copy is one that can drift.
 * @warning Offline/tool use only. This is the one entry point that works with
 *          no device open (tools/sndtest.c never calls audio_init), but it
 *          renders from a recipe pointer held across the render loop. Calling
 *          it concurrently with audio_reload() from another thread is NOT
 *          supported -- the reload could rewrite the recipe mid-render. The
 *          game never does this: it uses audio_play(), which is fully locked.
 *
 * 한국어
 * ------
 * @brief 장치를 건드리지 않고 사운드 하나를 `out`에 렌더링합니다.
 *
 * @param[in]  name       렌더링할 사운드. 알 수 없는 이름은 아무것도 렌더링하지 않습니다.
 * @param[out] out        샘플을 담을 버퍼. `max_frames`만큼을 담아야 합니다.
 * @param[in]  max_frames `out`의 용량(샘플 수).
 * @return 기록된 프레임 수. 이름을 알 수 없거나 레이어도 샘플도 없으면 0입니다. 해석은
 *         ::audio_rate를 기준으로 하십시오.
 *
 * @note 믹서와 *같은* 신디사이저를 실행하므로 도구에서 보는 것이 게임에서 재생되는 것과
 *       같습니다. 별도의 오프라인 사본은 어긋날 수 있는 사본입니다.
 * @warning 오프라인/도구 전용입니다. 장치가 열리지 않은 상태에서 동작하는 유일한
 *          진입점이지만(tools/sndtest.c는 audio_init을 호출하지 않습니다), 렌더링
 *          루프 전체에 걸쳐 레시피 포인터를 보유한 채 렌더링합니다. 다른 스레드의
 *          audio_reload()와 동시에 호출하는 것은 지원하지 않습니다. 리로드가 렌더링
 *          도중 레시피를 재작성할 수 있기 때문입니다. 게임은 이런 호출을 하지 않으며,
 *          완전히 락으로 보호되는 audio_play()를 사용합니다.
 */
int audio_render(const char *name, short *out, int max_frames);

/* --- Test hooks, authoring builds only / 테스트 훅. 제작 빌드 전용 --- */

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
 * @param[in] gain 청취자의 발밑에서의 0-100 값.
 * @param[in] pos  소리가 날 지점.
 * @return 감쇠된 게인. ::AUDIO_FAR를 넘으면 0입니다.
 * @note 테스트가 ::audio_play_at이 쓰는 것과 *같은* 함수에 물어볼 수 있도록 노출합니다.
 *       곡선을 다시 구현한 테스트는 게임이 다른 곡선을 쓰는 동안에도 통과하며, 그것이
 *       이 함수가 막으려는 실패입니다.
 */
int audio_gain_at(int gain, v3 pos);
#endif

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
