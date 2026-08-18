/**
 * @file brushstore.h
 * @brief Storage for the brush maps that loaded levels point at, owned by a caller.
 *
 * ENGLISH
 * -------
 * A ::BrushMap is 420KB against ::Level's 24KB, and Levels are stack locals
 * throughout the test suite -- see the note on ::Level::brushes for why the
 * field is a pointer and not a value. Something has to hold the storage those
 * pointers aim at. This is that something.
 *
 * WHAT CHANGED, and why it is a header rather than four lines in level.c. The
 * storage used to be a file-scope array in level.c, which made "how many levels
 * may be live at once" a fact about the PROCESS. Two was right for the game --
 * the level being played, plus the scratch Level world.c walks the level chain
 * with -- and it was a ceiling for anything that wanted to hold a level beside
 * the running one. An editor previewing a map next to a live game is three, and
 * three was refused.
 *
 * Owning it here makes the number a fact about a CALLER instead. The game keeps
 * one store and behaves exactly as it did; a second consumer brings its own and
 * neither can starve the other. That is the same move ::Pools made one layer
 * down, and for the same reason: state a module keeps for itself is state a
 * second instance of the caller cannot have.
 *
 * WHY A SEPARATE HEADER. level.h cannot hold this. It is included by pools.h,
 * which is included by world.h, and world.h names no GL and no ::BrushMap by
 * value on purpose -- ::Level only ever forward-declares one. A struct with
 * ::BrushMap in it must include brush.h, and putting that in level.h would push
 * brush.h into every translation unit that touches a ::World. So the definition
 * lives here, level.h forward-declares the type, and only the files that
 * actually allocate a store include this.
 *
 * @note A store is plain memory. Zero-initialised is a valid empty state, so a
 *       `static BrushStore s;` needs no init call and there is nothing to free.
 *       That is deliberate: nothing in this project has a destructor to run, and
 *       a level that is loaded and abandoned is the normal case rather than the
 *       exception.
 * @warning A store must outlive every ::Level loaded into it, because
 *          ::Level::brushes points inside it. A store with automatic storage
 *          duration is a mistake for the same reason a `BrushMap` local would
 *          be; give one static storage or make it a field of something that
 *          lives as long.
 *
 * 한국어
 * ------
 * @brief 로드된 레벨들이 가리키는 브러시 맵의 저장 공간이며, 호출자가 소유합니다.
 *
 * ::BrushMap은 ::Level의 24KB에 대해 420KB이고, Level은 테스트 묶음 전반에서 스택 지역
 * 변수입니다. 그 필드가 값이 아니라 포인터인 이유는 ::Level::brushes의 주석을 참조하십시오.
 * 그 포인터들이 겨누는 저장 공간을 무언가는 쥐고 있어야 합니다. 이것이 그 무언가입니다.
 *
 * *무엇이 바뀌었고* 왜 level.c의 네 줄이 아니라 헤더인가. 저장 공간은 이전에 level.c의 파일
 * 스코프 배열이었고, 그것이 "동시에 몇 개의 레벨이 살아 있을 수 있는가"를 *프로세스*에 대한
 * 사실로 만들었습니다. 둘은 게임에게 맞는 수였습니다. 플레이 중인 레벨에 더해 world.c가 레벨
 * 사슬을 걸을 때 쓰는 임시 Level입니다. 그리고 그것은 실행 중인 것 곁에 레벨을 쥐려는 무엇에게든
 * 천장이었습니다. 살아 있는 게임 옆에서 맵을 미리 보는 에디터는 셋이고, 셋은 거절되었습니다.
 *
 * 이곳에서 소유하면 그 수가 *호출자*에 대한 사실이 됩니다. 게임은 저장소 하나를 두고 이전과
 * 정확히 같이 동작하며, 두 번째 소비자는 자기 것을 가져오고 어느 쪽도 상대를 굶길 수 없습니다.
 * ::Pools가 한 계층 아래에서 한 것과 같은 동작이며 이유도 같습니다. 모듈이 자기 것으로 쥔
 * 상태는 호출자의 두 번째 인스턴스가 가질 수 없는 상태입니다.
 *
 * *왜 별도의 헤더인가.* level.h는 이것을 담을 수 없습니다. level.h는 pools.h가 포함하고 pools.h는
 * world.h가 포함하는데, world.h는 의도적으로 GL도 값으로서의 ::BrushMap도 언급하지 않습니다.
 * ::Level은 언제나 그것을 전방 선언만 합니다. ::BrushMap을 담은 구조체는 brush.h를 포함해야
 * 하고, 그것을 level.h에 두면 ::World에 닿는 모든 번역 단위로 brush.h가 밀려듭니다. 그래서
 * 정의는 이곳에 살고, level.h는 타입을 전방 선언하며, 실제로 저장소를 할당하는 파일만 이것을
 * 포함합니다.
 *
 * @note 저장소는 평범한 메모리입니다. 0 초기화가 유효한 빈 상태이므로 `static BrushStore s;`에는
 *       초기화 호출이 필요 없고 해제할 것도 없습니다. 의도적입니다. 이 프로젝트의 무엇도 실행할
 *       소멸자를 갖고 있지 않으며, 로드된 뒤 버려지는 레벨이 예외가 아니라 평범한 경우입니다.
 * @warning 저장소는 그 안으로 로드된 모든 ::Level보다 오래 살아야 합니다. ::Level::brushes가 그
 *          내부를 가리키기 때문입니다. 자동 저장 기간을 가진 저장소는 `BrushMap` 지역 변수가
 *          잘못인 것과 같은 이유로 잘못입니다. 정적 저장 기간을 주거나, 그만큼 오래 사는 무언가의
 *          필드로 만드십시오.
 */
#ifndef BRUSHSTORE_H
#define BRUSHSTORE_H

#include "brush.h"   /* BrushMap, by value: the whole reason this is its own header */

/**
 * @brief How many brush levels one store can hold at once.
 *
 * ENGLISH
 * -------
 * TWO, because two is how many levels one run keeps live: the one being played
 * and the scratch ::Level world.c walks the level chain with. A third against
 * the SAME store is still something new, and ::DIAG_LEVEL_SLOTS is there to say
 * so rather than let it pass.
 *
 * @note This is now a per-store number rather than a per-process one. A caller
 *       that wants to hold three levels does not raise this -- it takes a second
 *       store. Raising it costs 420KB a slot in every store that exists.
 *
 * 한국어
 * ------
 * @brief 저장소 하나가 동시에 담을 수 있는 브러시 레벨의 수.
 *
 * 둘인 이유는 한 판의 플레이가 살려 두는 레벨이 둘이기 때문입니다. 플레이 중인 것과, world.c가
 * 레벨 사슬을 걸을 때 쓰는 임시 ::Level입니다. *같은* 저장소에 대한 셋째는 여전히 새로운
 * 무언가이며, 그것을 그냥 지나가게 두는 대신 ::DIAG_LEVEL_SLOTS가 말해 줍니다.
 *
 * @note 이제 프로세스당이 아니라 저장소당 숫자입니다. 레벨 셋을 쥐려는 호출자는 이 값을 올리는
 *       것이 아니라 두 번째 저장소를 가집니다. 이 값을 올리면 존재하는 모든 저장소에서 슬롯당
 *       420KB가 듭니다.
 */
#define LVL_BRUSH_SLOTS 2

/**
 * @struct BrushStore
 * @brief A small pool of ::BrushMap slots and the serials that claim them.
 *
 * ENGLISH
 * -------
 * @note ::BrushStore::key holds SERIALS, not addresses, and that is load
 *       bearing. The pool was once keyed by the `Level *` that took a slot, so a
 *       Level that went out of scope left a dead address claiming one -- and
 *       world.c's chain scan builds exactly such a Level. A serial is issued
 *       once and never reused, so a stale key matches nothing and is simply not
 *       a claim. Nothing here records where a ::Level lives, which is what makes
 *       "is that Level still alive?" a question this never has to answer. See
 *       ::Level::brush_key.
 *
 * 한국어
 * ------
 * @brief ::BrushMap 슬롯의 작은 풀과, 그것을 주장하는 일련번호들.
 *
 * @note ::BrushStore::key는 주소가 아니라 *일련번호*를 담으며 그것이 구조적으로 중요합니다.
 *       풀은 한때 슬롯을 가져간 `Level *`로 키잉되었고, 그래서 스코프를 벗어난 Level이 죽은
 *       주소로 슬롯을 주장한 채 남았습니다. world.c의 사슬 스캔이 정확히 그런 Level을
 *       만듭니다. 일련번호는 한 번 발급되고 재사용되지 않으므로 낡은 키는 어느 것과도 맞지 않고
 *       그저 주장이 아닐 뿐입니다. 이곳의 무엇도 ::Level이 어디 사는지 기록하지 않으며, 그것이
 *       "그 Level이 아직 살아 있는가"를 결코 답하지 않아도 되게 만듭니다.
 *       ::Level::brush_key를 참조하십시오.
 */
struct BrushStore {
    BrushMap map[LVL_BRUSH_SLOTS];   /**< The storage itself. / 저장 공간 자체. */
    unsigned key[LVL_BRUSH_SLOTS];   /**< Which serial holds each slot; 0 is free. / 각 슬롯을 어느 일련번호가 쥐고 있는지. 0이면 비어 있음. */

    /**
     * @brief The next serial to issue.
     *
     * ENGLISH: Zero-initialised means 0, and 0 must never be issued -- it is
     * what `Level l = {0}` holds and has to mean "no claim". ::level_load_in
     * steps this past 0 on first use rather than requiring an init call, which
     * is what keeps a zeroed store a valid empty one.
     *
     * 한국어: 0으로 초기화되면 0이며, 0은 결코 발급되어서는 안 됩니다. `Level l = {0}`이 가진
     * 값이자 "주장 없음"을 뜻해야 하기 때문입니다. ::level_load_in이 초기화 호출을 요구하는
     * 대신 첫 사용에서 이 값을 0 너머로 넘기며, 그것이 0으로 초기화된 저장소를 유효한 빈
     * 저장소로 유지하는 방법입니다.
     */
    unsigned next_key;
};

#endif /* BRUSHSTORE_H */
