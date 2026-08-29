/**
 * @file save.h
 * @brief The two facts that outlive a run: what is unlocked, and how far it got.
 *
 * ENGLISH
 * -------
 * THIS IS THE FIRST THING THIS PROJECT WRITES TO DISK. Everything else it
 * touches is read-only: the assets are baked into the binary, the hot reload
 * path only ever opens files for reading, and the two `fopen`s that existed
 * before this one are a demo recorder and a sprite dump -- both authoring
 * tools, neither in game.exe. data.h's claim that "the shipped exe embeds
 * minimised text and does not touch the filesystem" was about ASSETS and stays
 * true about them; it is not true about the whole binary any more, and that
 * sentence is why this paragraph is here rather than in a commit message.
 *
 * WHAT IT IS FOR, and the reason it is two fields rather than a save game.
 * SFPS is an arena the player survives rather than a chain of levels they
 * finish, so there is no position, no inventory and no partial progress worth
 * restoring -- ::RunState is explicit that a restart clears the whole of a run
 * by construction. What CANNOT be reconstructed is which modes the player has
 * earned the right to choose and what their best wave was, and those are
 * exactly the two things the title screen has to show before any run exists.
 *
 * THE BITS ARE NOT THIS MODULE'S. ::save_unlock takes an unsigned mask and
 * stores it verbatim; what a bit MEANS is the menu's question, and the answer
 * is ::MENU_UNLOCK_ENDLESS and whatever joins it. A save format that also
 * defined the vocabulary would be a second list to keep in step with the rows,
 * and the failure -- a bit that means one thing to the file and another to the
 * screen -- is invisible until somebody adds the second unlock.
 *
 * WRITTEN WHEN IT CHANGES, not at exit. A save flushed on shutdown is a save
 * lost to every crash, every alt-F4 and every Task Manager, and those are not
 * rare in a game with no other reason to be closed politely. Both mutators
 * below write, and both report whether they had anything to write, so the
 * common case -- a wave that did not beat the best -- costs nothing.
 *
 * @note No GL, no window, no ::World. It is handed facts and returns facts, so
 *       tools/savetest.c drives the whole of it headlessly with its file put
 *       somewhere that is not the player's.
 *
 * 한국어
 * ------
 * @brief 플레이보다 오래 사는 두 가지 사실. 무엇이 해금되었는가와 어디까지 갔는가입니다.
 *
 * *이 프로젝트가 디스크에 쓰는 첫 번째 것입니다.* 그 밖에 손대는 모든 것은 읽기 전용입니다.
 * 에셋은 바이너리에 구워져 있고, 핫 리로드 경로는 파일을 읽기로만 열며, 이전에 존재하던 두
 * 개의 `fopen`은 데모 기록기와 스프라이트 덤프입니다. 둘 다 제작 도구이고 어느 쪽도
 * game.exe에 없습니다. "배포되는 exe는 최소화된 텍스트를 내장하고 파일 시스템에 접근하지
 * 않는다"는 data.h의 주장은 *에셋*에 대한 것이었고 그것에 대해서는 여전히 참입니다. 다만
 * 바이너리 전체에 대해서는 더 이상 참이 아니며, 그 문장이 이 문단이 커밋 메시지가 아니라
 * 이곳에 있는 이유입니다.
 *
 * *무엇을 위한 것인가*, 그리고 세이브 게임이 아니라 필드 둘인 이유. SFPS는 끝내는 레벨의
 * 사슬이 아니라 살아남는 아레나이므로 복원할 위치도, 소지품도, 중도 진행도 없습니다.
 * ::RunState는 재시작이 플레이 전체를 구조적으로 지운다고 명시하고 있습니다. *복원할 수 없는*
 * 것은 플레이어가 고를 권리를 얻은 모드가 무엇인지와 최고 웨이브가 얼마인지이며, 그 둘은
 * 정확히 어떤 플레이도 존재하기 전에 타이틀 화면이 보여 주어야 하는 것입니다.
 *
 * *비트는 이 모듈의 것이 아닙니다.* ::save_unlock은 unsigned 마스크를 받아 그대로 저장합니다.
 * 비트가 무엇을 *뜻하는지*는 메뉴의 질문이고, 답은 ::MENU_UNLOCK_ENDLESS와 그 뒤에 합류할
 * 것들입니다. 어휘까지 정의하는 저장 형식은 행들과 보조를 맞춰야 할 두 번째 목록이 되며, 그
 * 실패(파일에게는 이것을 뜻하고 화면에게는 저것을 뜻하는 비트)는 누군가 두 번째 해금을
 * 추가하기 전까지 보이지 않습니다.
 *
 * *바뀔 때 씁니다.* 종료 시에 쓰지 않습니다. 종료 시에 내보내는 저장은 모든 비정상 종료와
 * 모든 alt-F4와 모든 작업 관리자에 잃는 저장이며, 정중하게 닫힐 다른 이유가 없는 게임에서
 * 그것들은 드물지 않습니다. 아래의 두 변경 함수는 모두 쓰기를 수행하고 모두 쓸 것이
 * 있었는지를 보고하므로, 흔한 경우(최고 기록을 넘지 못한 웨이브)는 아무 비용도 들지 않습니다.
 *
 * @note GL도, 창도, ::World도 없습니다. 사실을 건네받아 사실을 돌려주므로 tools/savetest.c가
 *       파일을 플레이어의 것이 아닌 곳에 두고 전체를 헤드리스로 구동합니다.
 */
#ifndef SAVE_H
#define SAVE_H

/**
 * @brief The file name, inside whatever directory ::save_init resolved.
 *
 * ENGLISH: Named here rather than spelled into save.c so a person looking for
 * their save has one place to read it off, and so a test can name the same file
 * without a second copy of the string. `.txt` because it IS text -- every other
 * authored file in this project is, and a save a player can open in Notepad is
 * a save they can also tell you the contents of when something goes wrong.
 *
 * 한국어: save.c에 적어 넣지 않고 이곳에 이름을 두는 이유는, 자기 저장 파일을 찾는 사람이 읽을
 * 곳을 한 군데로 하고 테스트가 문자열 사본 없이 같은 파일을 지목할 수 있게 하기 위함입니다.
 * `.txt`인 이유는 실제로 텍스트이기 때문입니다. 이 프로젝트의 다른 모든 제작 파일이 그러하며,
 * 플레이어가 메모장으로 열 수 있는 저장은 무언가 잘못되었을 때 내용을 말해 줄 수도 있는
 * 저장입니다.
 */
#define SAVE_FILE "sfps.txt"

/**
 * @brief What this build writes into the file's `v` line.
 *
 * ENGLISH: Read back and ignored, deliberately. Nothing here needs migrating
 * yet -- unknown keys are skipped and missing keys keep their zero, which is
 * every migration this format can currently require. It is written anyway so
 * that the day a field changes meaning, the file already says which meaning it
 * was written under. A version stamp added AFTER the change is a version stamp
 * that cannot identify the files written before it.
 *
 * 한국어: 다시 읽고 의도적으로 무시합니다. 아직 이관할 것이 없습니다. 알 수 없는 키는
 * 건너뛰고 없는 키는 0으로 남으며, 그것이 이 형식이 현재 요구할 수 있는 이관의 전부입니다.
 * 그럼에도 쓰는 이유는, 어떤 필드가 뜻을 바꾸는 날 파일이 이미 어느 뜻으로 쓰였는지 말하고
 * 있게 하기 위함입니다. 변경 *뒤에* 추가된 버전 표기는 그 이전에 쓰인 파일을 식별할 수 없는
 * 버전 표기입니다.
 */
#define SAVE_VERSION 1

/* --- Lifecycle / 수명 주기 --- */

/**
 * @brief Resolves the save's location and reads whatever is there.
 *
 * ENGLISH
 * -------
 * ::plat_save_dir first, ::plat_exe_dir second, and the working directory last.
 * That order is the policy plat.h deliberately refused to hold: the host's own
 * answer is the right one, the tree the game was built in is a usable answer
 * for a developer, and a bare file name still opens somewhere rather than
 * nowhere.
 *
 * @note A missing file is an EMPTY save, not a failure. First launch is the
 *       common case and there is nothing to report about it -- see
 *       ::DIAG_SAVE_IO, which is raised on a failed WRITE and never on this.
 * @note Safe to call more than once; the second call re-reads.
 *
 * 한국어
 * ------
 * @brief 저장의 위치를 확인하고 그곳에 있는 것을 읽습니다.
 *
 * ::plat_save_dir이 먼저, ::plat_exe_dir이 그다음, 작업 디렉토리가 마지막입니다. 그 순서가
 * plat.h가 의도적으로 갖기를 거절한 정책입니다. 호스트 자신의 답이 옳은 답이고, 게임이 빌드된
 * 트리는 개발자에게 쓸 만한 답이며, 맨 파일 이름도 아무 데도 아닌 곳이 아니라 어딘가에는
 * 열립니다.
 *
 * @note 없는 파일은 실패가 아니라 *빈 저장*입니다. 첫 실행이 흔한 경우이고 그것에 대해 보고할
 *       것은 없습니다. ::DIAG_SAVE_IO를 참조하십시오. 실패한 *쓰기*에서 올라가며 이것에
 *       대해서는 결코 올라가지 않습니다.
 * @note 두 번 이상 호출해도 안전합니다. 두 번째 호출은 다시 읽습니다.
 */
void save_init(void);

/**
 * @brief ::save_init with the directory named instead of resolved.
 *
 * ENGLISH
 * -------
 * @param[in] dir Directory to keep ::SAVE_FILE in, ending with a separator, or
 *                NULL for the resolved location -- which makes this identical
 *                to ::save_init.
 *
 * FOR A TEST, and that is the whole of it, the same way ::world_init_in exists
 * for a second world. A test that wrote to the resolved location would edit the
 * save of whoever ran it, and a test that could not write at all would be
 * asserting about a module with its one side effect removed.
 *
 * 한국어
 * ------
 * @brief 위치를 확인하는 대신 디렉토리를 지목하는 ::save_init입니다.
 * @param[in] dir ::SAVE_FILE을 둘 디렉토리이며 구분자로 끝나야 합니다. NULL이면 확인된
 *                위치이며, 그 경우 이 함수는 ::save_init과 동일합니다.
 *
 * *테스트를 위한 것이고* 그것이 전부입니다. ::world_init_in이 두 번째 월드를 위해 존재하는
 * 것과 같습니다. 확인된 위치에 쓰는 테스트는 그것을 실행한 사람의 저장을 편집하게 되고, 아예
 * 쓸 수 없는 테스트는 유일한 부작용이 제거된 모듈에 대해 단언하게 됩니다.
 */
void save_init_in(const char *dir);

/**
 * @brief Forgets everything read, without touching the file.
 *
 * ENGLISH: The in-memory half of a fresh start. Nothing in the game calls it --
 * there is no "erase my progress" row and this is not one. It exists so a test
 * can put the module into the state a first launch produces without deleting a
 * file it would then have to recreate, and so that state has a name.
 *
 * 한국어: 새로 시작하는 것의 메모리 쪽 절반입니다. 게임의 어떤 것도 이것을 부르지 않습니다.
 * "진행 상황 지우기" 행은 없으며 이것이 그것도 아닙니다. 테스트가 지웠다가 다시 만들어야 할
 * 파일을 건드리지 않고도 첫 실행이 만들어 내는 상태로 모듈을 놓을 수 있도록, 그리고 그 상태에
 * 이름이 있도록 존재합니다.
 */
void save_forget(void);

/* --- Reading / 읽기 --- */

/**
 * @brief The full path the save is kept at.
 * @param[out] out Destination, always null-terminated.
 * @param[in]  cap Capacity in bytes, at least 1.
 * @return Characters written, terminator excluded.
 *
 * 한국어
 * ------
 * @brief 저장이 보관되는 전체 경로입니다.
 */
int save_path(char *out, int cap);

/** @brief Every unlock bit set so far. / 지금까지 설정된 모든 해금 비트. */
unsigned save_unlocks(void);

/**
 * @brief Whether ALL of `bits` are set.
 * @param[in] bits Mask to test, e.g. ::MENU_UNLOCK_ENDLESS.
 * @return Non-zero when every bit in `bits` is set. A `bits` of 0 is trivially
 *         true, which is what a row with no requirement wants.
 *
 * 한국어
 * ------
 * @brief `bits`가 *전부* 설정되어 있는지 여부입니다.
 * @return `bits`의 모든 비트가 설정되어 있으면 0이 아닌 값. `bits`가 0이면 자명하게 참이며,
 *         요구 조건이 없는 행이 원하는 답입니다.
 */
int save_unlocked(unsigned bits);

/** @brief The best wave any run has reached. 0 before any arena was played. / 어떤 플레이든 도달한 최고 웨이브. 아레나를 하기 전에는 0입니다. */
int save_best_wave(void);

/* --- Writing / 쓰기 --- */

/**
 * @brief Sets unlock bits and persists them if any were new.
 *
 * ENGLISH
 * -------
 * @param[in] bits Mask to add.
 * @return Non-zero when something was actually unlocked by this call.
 * @note Idempotent, and the return is what makes that cheap: beating the maw
 *       twice writes the file once. The caller can raise this every frame the
 *       condition holds without deciding whether it already has -- the same
 *       bargain ::music_play makes.
 *
 * 한국어
 * ------
 * @brief 해금 비트를 설정하고, 새로 설정된 것이 있으면 저장합니다.
 * @param[in] bits 추가할 마스크.
 * @return 이 호출로 실제로 해금된 것이 있으면 0이 아닌 값.
 * @note 멱등이며, 반환값이 그것을 싸게 만듭니다. 아귀를 두 번 쓰러뜨려도 파일은 한 번
 *       쓰입니다. 호출자는 조건이 성립하는 매 프레임 이것을 올리면서 이미 그랬는지 판단하지
 *       않아도 됩니다. ::music_play가 맺는 것과 같은 거래입니다.
 */
int save_unlock(unsigned bits);

/**
 * @brief Records a wave if it beats the best, and persists it.
 *
 * ENGLISH
 * -------
 * @param[in] wave The wave a finished run reached -- ::RunState::wave_best.
 * @return Non-zero when this was a new best.
 * @note A wave of 0 or less is ignored rather than recorded. Every level
 *       without spawners ends with ::RunState::wave_best at 0, and a save that
 *       wrote it would be the file learning something from a corridor.
 *
 * 한국어
 * ------
 * @brief 최고 기록을 넘는 웨이브를 기록하고 저장합니다.
 * @param[in] wave 끝난 플레이가 도달한 웨이브. ::RunState::wave_best입니다.
 * @return 새 최고 기록이었으면 0이 아닌 값.
 * @note 0 이하의 웨이브는 기록하지 않고 무시합니다. 스포너가 없는 모든 레벨은
 *       ::RunState::wave_best가 0인 채로 끝나며, 그것을 기록하는 저장은 복도에서 무언가를
 *       배우는 파일입니다.
 */
int save_note_wave(int wave);

/**
 * @brief Writes the file now, whether or not anything changed.
 *
 * ENGLISH
 * -------
 * @return Non-zero on success. On failure ::DIAG_SAVE_IO is raised and the
 *         in-memory state is left exactly as it was -- the run continues with
 *         its unlock, and only the next launch is poorer for it.
 * @note The two mutators above call this themselves. It is public because a
 *       caller that has just been handed a save directory it could not write to
 *       has no other way to find that out, and tools/savetest.c is that caller.
 *
 * 한국어
 * ------
 * @brief 바뀐 것이 있든 없든 지금 파일을 씁니다.
 * @return 성공하면 0이 아닌 값. 실패하면 ::DIAG_SAVE_IO가 올라가고 메모리 상태는 그대로
 *         남습니다. 플레이는 해금을 지닌 채 계속되며, 손해를 보는 것은 다음 실행뿐입니다.
 * @note 위의 두 변경 함수가 스스로 이것을 부릅니다. 공개인 이유는, 쓸 수 없는 저장 디렉토리를
 *       방금 건네받은 호출자에게 그것을 알아낼 다른 방법이 없기 때문이며, tools/savetest.c가
 *       그 호출자입니다.
 */
int save_write(void);

#endif /* SAVE_H */
