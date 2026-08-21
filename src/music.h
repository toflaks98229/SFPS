/**
 * @file music.h
 * @brief The background music: a sequencer over note streams, not a player.
 *
 * ENGLISH
 * -------
 * WHAT THIS IS NOT. It does not decode anything. There is no audio here at all
 * -- no samples, no stream, no decoder. Freedoom ships its music as ~130 MIDI
 * files, and a MIDI file is a SCORE: hearing one needs a synthesiser with an
 * instrument bank, and this engine has four oscillators and no bank. Rendering
 * the tracks to audio was never on the table either; there is no Vorbis or MP3
 * decoder in this project, only DEFLATE, and one rendered track would eat a
 * third of what is left of the floppy.
 *
 * So the MIDI is parsed once, at bake time, by
 * assets/music/import-freedoom-music.py, and what reaches the game is a flat
 * list of (time, pitch, duration, wave). This file walks that list against a
 * clock and hands each note to ::audio_note as it comes due. The whole runtime
 * cost is a forward scan through a sorted array.
 *
 * WHAT IS LOST, said plainly rather than discovered: the instruments. A
 * Freedoom track played through four oscillators is a chiptune of itself --
 * the melody, the harmony and the structure survive, the timbre does not. That
 * is the trade 1.44MB forces, and it sits well with a game that already
 * quantises its colour to 15 bits and snaps its vertices to a grid.
 *
 * @note Driven from the game thread, one call a frame. Note onsets therefore
 *       land on frame boundaries rather than on samples, which is about 16ms of
 *       jitter at 60fps. Sample-accurate sequencing would mean running this on
 *       the mixer thread, and that means locking the note tables against a
 *       thread that currently touches nothing but voices -- a large change to
 *       buy precision below what this music needs.
 * @note Music has its OWN voices (::MUSIC_VOICES of them, carved out of
 *       ::MAX_VOICES), so a busy passage can never silence a shotgun and a
 *       firefight can never silence the music. That split is audio.c's; the
 *       bake-time reduction that keeps the arrangement inside it is the
 *       importer's.
 *
 * 한국어
 * ------
 * @brief 배경 음악. 재생기가 아니라 노트 스트림 위의 시퀀서입니다.
 *
 * *이것이 무엇이 아닌가.* 아무것도 디코딩하지 않습니다. 이곳에는 오디오가 전혀 없습니다.
 * 샘플도, 스트림도, 디코더도 없습니다. Freedoom은 음악을 약 130개의 MIDI 파일로 제공하는데,
 * MIDI 파일은 *악보*입니다. 그것을 들으려면 악기 뱅크를 가진 신시사이저가 필요하고, 이 엔진에는
 * 오실레이터 넷과 뱅크 없음이 전부입니다. 트랙을 오디오로 렌더링하는 것도 애초에 선택지가
 * 아니었습니다. 이 프로젝트에는 Vorbis도 MP3 디코더도 없고 DEFLATE뿐이며, 렌더된 트랙 하나가
 * 플로피에 남은 용량의 3분의 1을 먹습니다.
 *
 * 그래서 MIDI는 베이크 시점에 assets/music/import-freedoom-music.py가 한 번 파싱하고, 게임에
 * 도달하는 것은 (시각, 음높이, 길이, 파형)의 평평한 목록입니다. 이 파일은 그 목록을 시계에
 * 맞춰 훑으며 차례가 된 음표를 ::audio_note에 건넵니다. 런타임 비용의 전부가 정렬된 배열에
 * 대한 전진 스캔입니다.
 *
 * *무엇을 잃는지*를 발견이 아니라 명시로 적습니다. 악기입니다. 오실레이터 넷으로 연주한
 * Freedoom 트랙은 그 곡의 칩튠 버전입니다. 선율과 화성과 구성은 살아남고 음색은 살아남지
 * 않습니다. 1.44MB가 강제하는 거래이며, 이미 색을 15비트로 양자화하고 정점을 격자에 스냅하는
 * 게임에는 잘 어울립니다.
 *
 * @note 게임 스레드에서 프레임당 한 번 구동됩니다. 따라서 음표 시작은 샘플이 아니라 프레임
 *       경계에 놓이며, 60fps에서 약 16ms의 흔들림입니다. 샘플 단위 정확도를 얻으려면 이것을
 *       믹서 스레드에서 돌려야 하고, 그것은 현재 보이스 외에는 아무것도 건드리지 않는 스레드에
 *       맞서 노트 표를 잠근다는 뜻입니다. 이 음악에 필요한 수준 이하의 정밀도를 사기 위한 큰
 *       변경입니다.
 * @note 음악은 *자기 보이스*를 가집니다(::MAX_VOICES에서 떼어 낸 ::MUSIC_VOICES개). 그래서
 *       빽빽한 악절이 샷건을 침묵시킬 수 없고 총격전이 음악을 침묵시킬 수 없습니다. 그 분할은
 *       audio.c의 것이고, 편곡을 그 안에 들어가도록 줄이는 베이크 시점 작업은 임포터의
 *       것입니다.
 */
#ifndef MUSIC_H
#define MUSIC_H

/**
 * @brief Which piece is playing.
 *
 * ENGLISH: Three, and the choice of three is the budget rather than taste --
 * see the importer. ::MUSIC_NONE is silence and is what a fresh process holds,
 * so nothing plays until something asks.
 *
 * 한국어: 셋이며, 셋이라는 선택은 취향이 아니라 예산입니다. 임포터를 참조하십시오.
 * ::MUSIC_NONE은 무음이며 새 프로세스가 가진 값이므로, 무언가 요청하기 전까지는 아무것도
 * 재생되지 않습니다.
 */
typedef enum {
    MUSIC_NONE = 0,  /**< Silence. / 무음. */
    MUSIC_TITLE,     /**< The title screen. / 타이틀 화면. */
    MUSIC_LEVEL,     /**< Ordinary play. / 평상시 플레이. */
    MUSIC_BOSS,      /**< While something big is alive. / 큰 것이 살아 있는 동안. */
    MUSIC_TRACKS     /**< How many there are. / 개수. */
} MusicTrack;

/**
 * @brief Asks for a track. A no-op if it is already the one playing.
 *
 * ENGLISH
 * -------
 * @param[in] track Which piece, or ::MUSIC_NONE to stop.
 *
 * @note IDEMPOTENT ON PURPOSE, because the callers are per-frame. "Play boss
 *       music while a brute is alive" is a condition rather than an event, so
 *       the frame loop states it every frame and this decides whether anything
 *       actually changed. A version that restarted on every call would hold the
 *       first bar of the boss theme forever.
 * @note Switching tracks starts the new one from its beginning rather than
 *       crossfading. There is no mixing bus to fade on, and a cut is what a
 *       game of this vintage did.
 *
 * 한국어
 * ------
 * @brief 트랙을 요청합니다. 이미 재생 중이면 아무 일도 하지 않습니다.
 * @param[in] track 어느 곡인지, 또는 멈추려면 ::MUSIC_NONE.
 *
 * @note *의도적으로 멱등입니다.* 호출자가 프레임 단위이기 때문입니다. "브루트가 살아 있는 동안
 *       보스 음악"은 사건이 아니라 *조건*이므로 프레임 루프가 매 프레임 그것을 진술하고, 실제로
 *       무엇이 바뀌었는지는 이 함수가 판단합니다. 호출마다 다시 시작하는 판이라면 보스 테마의
 *       첫 마디를 영원히 붙잡고 있게 됩니다.
 * @note 트랙 전환은 크로스페이드가 아니라 새 곡을 처음부터 시작합니다. 페이드할 믹싱 버스가
 *       없고, 이 시대의 게임이 하던 것이 컷입니다.
 */
void music_play(MusicTrack track);

/**
 * @brief Advances the clock and sounds whatever came due. One call a frame.
 *
 * ENGLISH
 * -------
 * @param[in] dt Seconds since the last call.
 *
 * @note Takes REAL seconds rather than world time, and that is deliberate: the
 *       music keeps playing behind a pause menu and across the between-levels
 *       screen. It belongs to the game being on, not to the world advancing --
 *       unlike everything driven by ::RunState::world_time.
 * @note Loops at the track's own length. Freedoom's tracks were written to
 *       loop, so nothing has to fade.
 *
 * 한국어
 * ------
 * @brief 시계를 진행시키고 차례가 된 것을 소리 냅니다. 프레임당 한 번 호출합니다.
 * @param[in] dt 마지막 호출 이후의 초.
 *
 * @note 월드 시간이 아니라 *실제* 초를 받으며 이는 의도적입니다. 음악은 일시정지 메뉴 뒤에서도
 *       레벨 사이 화면에서도 계속 재생됩니다. 월드가 진행하는 것이 아니라 게임이 켜져 있는 것에
 *       속합니다. ::RunState::world_time이 구동하는 모든 것과 다릅니다.
 * @note 트랙 자신의 길이에서 순환합니다. Freedoom의 트랙은 순환하도록 작곡되었으므로 페이드가
 *       필요 없습니다.
 */
void music_update(float dt);

/** @brief What is playing now. / 지금 재생 중인 것. */
MusicTrack music_now(void);

/** @brief How many notes a track holds, for tests. 0 if it has none. / 트랙이 담은 음표 수. 검사용이며 없으면 0입니다. */
int music_note_count(MusicTrack track);

#endif /* MUSIC_H */
