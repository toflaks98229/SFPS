/**
 * @file demo.h
 * @brief A recorded run: where it started, and what the player did each frame.
 *
 * ENGLISH
 * -------
 * ::world_step is a function of (::World, ::Input, aspect, dt) and of nothing
 * else. Every random state it consumes -- the weapon's spread, the monsters'
 * fight rolls, the particles, the lava smoke -- is a field inside the ::World,
 * seeded from a constant; there is no `rand()`, no clock read and no file-scope
 * state on the simulation path. So the same start plus the same inputs is the
 * same run, frame for frame, and that is a property rather than an aspiration:
 * tools\demotest.c drives a World with one input stream, replays the recording
 * into a second World, and compares the two.
 *
 * That was not true a short while ago and it is worth saying why, because the
 * work that made it true was not done for this. The monsters, the items, the
 * projectiles, the particles and the marks a shot leaves were file-scope arrays
 * in five modules; the doors were a sixth; the weapon switch, the grapple's
 * release and the death screen's restart rule were inside a Win32 message
 * handler. Each of those was a piece of the run living somewhere a second
 * ::World could not reach and a recording could not describe. Moving them is
 * what left ::World holding all of it and ::Input carrying all of the intent --
 * see pools.h, ::DoorSet and ::Input.
 *
 * @note WHAT A DEMO IS NOT: a save. It has no world state in it at all, only
 *       the name of a level and a list of intents. Replaying it recomputes
 *       everything, which is exactly why it is worth having -- a demo that
 *       disagrees with the game is a demo that has found a bug, and a save can
 *       never disagree with anything.
 * @note Touches no GL, no window and no file. Recording appends to a struct the
 *       caller owns and playback reads from one; turning that into bytes is
 *       ::demo_write and ::demo_read, and putting those bytes on a disk is
 *       main.c's business. Same seam every other module here keeps.
 *
 * 한국어
 * ------
 * @brief 기록된 플레이. 어디서 시작했고 매 프레임 플레이어가 무엇을 했는가입니다.
 *
 * ::world_step은 (::World, ::Input, aspect, dt)의 함수이며 그 외 무엇의 함수도 아닙니다. 그것이
 * 소비하는 모든 난수 상태(무기의 산포, 몬스터의 전투 판정, 입자, 용암 연기)는 ::World 안의
 * 필드이고 상수로 시드됩니다. 시뮬레이션 경로에는 `rand()`도, 시계 읽기도, 파일 스코프 상태도
 * 없습니다. 따라서 같은 시작에 같은 입력은 프레임 하나까지 같은 플레이이며, 그것은 희망이
 * 아니라 성질입니다. tools\demotest.c가 하나의 입력 스트림으로 World를 구동하고, 그 기록을 두
 * 번째 World에 재생한 뒤 둘을 비교합니다.
 *
 * 얼마 전까지는 그렇지 않았고, 왜 그런지 말할 가치가 있습니다. 그것을 사실로 만든 작업이 이
 * 기능을 위해 이루어진 것이 아니기 때문입니다. 몬스터·아이템·발사체·입자·사격이 남긴 자국은
 * 다섯 모듈의 파일 스코프 배열이었고, 문이 여섯 번째였으며, 무기 전환과 그래플 해제와 사망
 * 화면의 재시작 규칙은 Win32 메시지 핸들러 안에 있었습니다. 그 각각은 두 번째 ::World가 닿을 수
 * 없고 기록이 서술할 수 없는 곳에 살던 플레이의 조각이었습니다. 그것들을 옮긴 것이 ::World가
 * 전부를 쥐고 ::Input이 모든 의도를 나르게 만들었습니다. pools.h와 ::DoorSet과 ::Input을
 * 참조하십시오.
 *
 * @note 데모가 *아닌* 것: 세이브입니다. 월드 상태는 하나도 들어 있지 않으며 레벨 이름과 의도의
 *       목록뿐입니다. 재생은 전부를 다시 계산하는데, 그것이 바로 이것을 가질 가치가 있는
 *       이유입니다. 게임과 어긋나는 데모는 버그를 찾아낸 데모이고, 세이브는 결코 무엇과도
 *       어긋날 수 없습니다.
 * @note GL도, 창도, 파일도 건드리지 않습니다. 기록은 호출자가 소유한 구조체에 덧붙이고 재생은
 *       그것에서 읽습니다. 그것을 바이트로 바꾸는 것이 ::demo_write와 ::demo_read이며, 그
 *       바이트를 디스크에 올리는 것은 main.c의 몫입니다. 이곳의 다른 모든 모듈이 지키는 것과 같은
 *       이음매입니다.
 */
#ifndef DEMO_H
#define DEMO_H

#include "world.h"

/**
 * @brief How many frames one recording holds.
 *
 * ENGLISH
 * -------
 * 18,000, which is five minutes at 60fps and rather more at the rates this
 * actually runs at. Chosen against what a recording is FOR: a bug someone is
 * reporting, or a stage being demonstrated. Neither is ten minutes long, and a
 * recording that fills up stops recording rather than failing -- see
 * ::DIAG_DEMO_FULL.
 *
 * @note Costs `.bss` and nothing on disk: 8 bytes a frame, so 144KB for a
 *       ::Demo. That is why one is a static or a heap block and never a stack
 *       local, the same rule ::Level keeps for the same reason.
 *
 * 한국어
 * ------
 * @brief 하나의 기록이 담는 프레임 수입니다.
 *
 * 18,000이며 60fps에서 5분, 실제로 이 게임이 도는 프레임률에서는 그보다 깁니다. 기록이 *무엇을
 * 위한 것인가*를 기준으로 골랐습니다. 누군가 신고하는 버그이거나 시연되는 스테이지이며, 둘 다
 * 10분짜리가 아닙니다. 가득 찬 기록은 실패하지 않고 기록을 멈춥니다. ::DIAG_DEMO_FULL을
 * 참조하십시오.
 *
 * @note `.bss`를 쓰고 디스크는 쓰지 않습니다. 프레임당 8바이트이므로 ::Demo 하나가 144KB입니다.
 *       그래서 이것은 static이거나 힙 블록이며 결코 스택 지역 변수가 아닙니다. ::Level이 같은
 *       이유로 지키는 규칙입니다.
 */
#define DEMO_MAX_FRAMES 18000

/** @brief Format tag written first, so a file from a later build is refused rather than misread. / 가장 먼저 기록되는 형식 태그. 이후 빌드의 파일을 잘못 읽는 대신 거절합니다. */
#define DEMO_VERSION 1

/* --- the bits one frame's held state packs into / 한 프레임의 유지 상태가 담기는 비트 --- */
enum {
    DEMO_FORWARD = 1 << 0,
    DEMO_BACK    = 1 << 1,
    DEMO_LEFT    = 1 << 2,
    DEMO_RIGHT   = 1 << 3,
    DEMO_JUMP    = 1 << 4,
    DEMO_FIRE    = 1 << 5,
    DEMO_HOOK    = 1 << 6,
    DEMO_PAUSED  = 1 << 7,
    DEMO_CONFIRM = 1 << 8,
    DEMO_LET_GO  = 1 << 9
};

/**
 * @struct DemoFrame
 * @brief One frame of a recording: what the player did, and how long it took.
 *
 * ENGLISH
 * -------
 * INTEGERS, all of them, and that is not only about size. A float written as
 * decimal text and read back is not always the float that was written, and a
 * replay that starts one ulp away from the recording diverges -- slowly at
 * first, and then completely, because the whole point is that the simulation
 * carries its own state forward. Storing what the platform actually measured,
 * in the units it measured it in, removes the question.
 *
 * `look_dx`/`look_dy` really are integers: they are a cursor position minus the
 * window centre, in pixels. `dt` is microseconds, and the frame loop already
 * clamps it to 100ms so it fits. `aspect` is thousandths.
 *
 * 한국어
 * ------
 * @brief 기록의 한 프레임. 플레이어가 무엇을 했고 그것이 얼마나 걸렸는가입니다.
 *
 * 전부 정수이며, 이는 크기만의 문제가 아닙니다. 십진 텍스트로 쓰였다가 다시 읽힌 실수는 언제나
 * 쓰인 그 실수가 아니며, 기록에서 1ulp 떨어져 시작하는 재생은 갈라집니다. 처음에는 천천히,
 * 그리고 완전히 갈라집니다. 시뮬레이션이 자기 상태를 앞으로 나른다는 것이 요점 전부이기
 * 때문입니다. 플랫폼이 실제로 측정한 값을 측정한 단위 그대로 저장하면 그 질문이 사라집니다.
 *
 * `look_dx`/`look_dy`는 실제로 정수입니다. 커서 위치에서 창 중앙을 뺀 픽셀 값입니다. `dt`는
 * 마이크로초이며 프레임 루프가 이미 100ms로 제한하므로 들어갑니다. `aspect`는 1/1000 단위입니다.
 */
typedef struct {
    unsigned short dt_us;    /**< Frame time in microseconds. / 프레임 시간(마이크로초). */
    short          look_dx;  /**< Mouse delta, pixels. / 마우스 변화량(픽셀). */
    short          look_dy;  /**< Mouse delta, pixels. / 마우스 변화량(픽셀). */
    unsigned short bits;     /**< DEMO_* held state, and the weapon request in the top 4 bits. / DEMO_* 유지 상태와 상위 4비트의 무기 요청. */
} DemoFrame;

/**
 * @struct Demo
 * @brief A whole recording. Large; never a stack local.
 *
 * 한국어: 기록 전체입니다. 크므로 결코 스택 지역 변수가 아닙니다.
 */
typedef struct {
    char      level[WORLD_LEVEL_MAX];  /**< The stage it started in. / 시작한 스테이지. */
    int       n;                       /**< Frames recorded. / 기록된 프레임 수. */
    /**
     * @brief The viewport it was played at, in pixels.
     *
     * ENGLISH: THE SHAPE, NOT THE RATIO, and the difference is the whole reason
     * this field is two integers. ::world_step takes an aspect and the muzzle
     * solve reads it, so a replay that reconstructs a slightly different aspect
     * puts shots somewhere slightly different -- and a simulation that carries
     * its own state forward turns "slightly" into "completely" within seconds.
     * A ratio stored as thousandths cannot come back: 1280/720 quantises to
     * 1.778 and the recording was made at 1.7777778. Storing what the ratio was
     * DERIVED from, and dividing again on the way out, is exact by construction.
     * demotest caught this on its first run.
     *
     * 한국어: 비율이 아니라 *형태*이며, 그 차이가 이 필드가 정수 둘인 이유 전부입니다.
     * ::world_step은 종횡비를 받고 총구 계산이 그것을 읽으므로, 조금 다른 종횡비를 복원하는
     * 재생은 사격을 조금 다른 곳에 놓습니다. 그리고 자기 상태를 앞으로 나르는 시뮬레이션은 몇 초
     * 만에 "조금"을 "완전히"로 바꿉니다. 1/1000로 저장된 비율은 되돌아올 수 없습니다. 1280/720은
     * 1.778로 양자화되는데 기록은 1.7777778에서 이루어졌습니다. 비율이 무엇으로부터 *유도*
     * 되었는지를 저장하고 나갈 때 다시 나누면 구성상 정확합니다. demotest가 첫 실행에서 이것을
     * 잡아냈습니다.
     */
    int       vw, vh;
    DemoFrame f[DEMO_MAX_FRAMES];
} Demo;

/* --- recording / 기록 --- */

/**
 * @brief Starts a recording of `level`, discarding whatever was there.
 * @param[out] d     Recording to begin.
 * @param[in]  level Stage name the replay will load.
 *
 * @brief `level`의 기록을 시작하며 기존 내용을 버립니다.
 */
void demo_begin(Demo *d, const char *level);

/**
 * @brief Appends one frame of intent.
 *
 * ENGLISH
 * -------
 * @param[in,out] d      Recording.
 * @param[in]     in     The intent handed to ::world_step this frame.
 * @param[in]     vw,vh  Viewport in pixels; recorded once, from the first frame.
 * @param[in]     dt     Seconds this frame covered.
 *
 * @note Called with the SAME ::Input the step is given, on the same frame,
 *       before or after does not matter -- ::world_step does not write to it.
 *       Recording anything derived from the input instead would record a
 *       conclusion, and the conclusion is what the replay is supposed to reach
 *       on its own.
 * @note A full recording quietly stops growing. A demo that ends early is a
 *       shorter demo; a demo that overwrites its own start is not a demo.
 *
 * 한국어
 * ------
 * @brief 한 프레임의 의도를 덧붙입니다.
 * @note 스텝에 건네는 것과 *같은* ::Input으로, 같은 프레임에 호출합니다. 앞이든 뒤든
 *       상관없습니다. ::world_step은 그것에 기록하지 않습니다. 입력에서 유도된 무언가를 대신
 *       기록하면 결론을 기록하는 셈이며, 그 결론은 재생이 스스로 도달해야 하는 것입니다.
 * @note 가득 찬 기록은 조용히 자라기를 멈춥니다. 일찍 끝나는 데모는 짧은 데모이지만, 자기
 *       시작을 덮어쓰는 데모는 데모가 아닙니다.
 */
void demo_record(Demo *d, const Input *in, int vw, int vh, float dt);

/* --- playback / 재생 --- */

/**
 * @brief Reads frame `i` back out as the intent it was.
 *
 * ENGLISH
 * -------
 * @param[in]  d      Recording.
 * @param[in]  i      Frame index.
 * @param[out] in     Filled completely; every field is written.
 * @param[out] aspect The aspect the recording was made at, divided out of ::Demo::vw and ::Demo::vh.
 * @param[out] dt     Seconds that frame covered.
 * @return Non-zero while `i` names a recorded frame, zero past the end.
 *
 * @note Writes EVERY field of `in`, including the ones that are always zero in
 *       a recording. A partially filled Input would carry whatever the caller's
 *       struct had in it, which is the one way a replay can differ from the
 *       recording without the recording being wrong.
 *
 * 한국어
 * ------
 * @brief 프레임 `i`를 그것이었던 의도로 되읽습니다.
 * @return `i`가 기록된 프레임을 가리키는 동안 0이 아니고, 끝을 지나면 0입니다.
 * @note `in`의 *모든* 필드를 씁니다. 기록에서 언제나 0인 것들도 포함합니다. 일부만 채워진
 *       Input은 호출자의 구조체에 있던 값을 나르게 되며, 그것이 기록이 틀리지 않았는데도 재생이
 *       기록과 달라질 수 있는 유일한 경로입니다.
 */
int demo_replay(const Demo *d, int i, Input *in, float *aspect, float *dt);

/* --- bytes / 바이트 --- */

/**
 * @brief Writes a recording as the text a person can read and a diff can show.
 *
 * ENGLISH
 * -------
 * @param[in]  d   Recording.
 * @param[out] buf Destination.
 * @param[in]  cap Bytes available.
 * @return Bytes written, or 0 if it did not fit.
 *
 * @note Text, like every other asset in this project, for the same reason: a
 *       recording that will not replay is a thing somebody has to look at, and
 *       looking at it should not need a tool. One line per frame, and the four
 *       numbers on it are the four in ::DemoFrame.
 *
 * 한국어
 * ------
 * @brief 기록을, 사람이 읽고 diff가 보여 줄 수 있는 텍스트로 씁니다.
 * @return 기록한 바이트 수. 들어가지 않으면 0입니다.
 * @note 이 프로젝트의 다른 모든 에셋과 같이 텍스트이며 이유도 같습니다. 재생되지 않는 기록은
 *       누군가 들여다봐야 하는 것이고, 들여다보는 데 도구가 필요해서는 안 됩니다. 프레임당 한
 *       줄이며 그 위의 네 숫자가 ::DemoFrame의 넷입니다.
 */
int demo_write(const Demo *d, char *buf, int cap);

/**
 * @brief Reads back what ::demo_write produced.
 *
 * @param[out] d    Recording to fill.
 * @param[in]  text Bytes to parse.
 * @param[in]  len  How many.
 * @return Non-zero on success; zero for a version this build does not know or
 *         text that does not begin with the tag.
 *
 * @brief ::demo_write가 만든 것을 되읽습니다.
 * @return 성공하면 0이 아닌 값. 이 빌드가 모르는 버전이거나 태그로 시작하지 않는 텍스트이면 0.
 */
int demo_read(Demo *d, const char *text, int len);

/* --- driving a frame from one / 데모로 한 프레임을 구동하기 --- */

/**
 * @brief Which of the three things this process is doing about a demo.
 *
 * 한국어: 이 프로세스가 데모에 대해 하고 있는 세 가지 중 무엇인가.
 */
enum { DEMO_OFF, DEMO_RECORD, DEMO_PLAY };

/**
 * @struct DemoDrive
 * @brief A recording plus where the frame loop has got to in it.
 *
 * ENGLISH
 * -------
 * THE CURSOR IS THE POINT. A ::Demo is inert data; what the frame loop needs is
 * "give me this frame's intent, and tell me when you have run out". That is a
 * rule -- when a replay ends, control goes back to the player -- and it lived in
 * the body of `WinMain`, which is the one place in this project no test can
 * reach.
 *
 * @note Holds the ::Demo by value, so this is 144KB and follows the same rule:
 *       a static or a heap block, never a stack local.
 *
 * 한국어
 * ------
 * @brief 기록과, 프레임 루프가 그 안에서 어디까지 왔는가.
 *
 * 커서가 요점입니다. ::Demo는 움직이지 않는 데이터이고, 프레임 루프가 필요로 하는 것은 "이번
 * 프레임의 의도를 달라, 그리고 다 떨어지면 알려 달라"입니다. 그것은 규칙이며(재생이 끝나면
 * 조작권이 플레이어에게 돌아간다) `WinMain`의 본문에 있었습니다. 이 프로젝트에서 어떤 테스트도
 * 닿을 수 없는 유일한 곳입니다.
 *
 * @note ::Demo를 값으로 담으므로 144KB이며 같은 규칙을 따릅니다. static이거나 힙 블록이고,
 *       결코 스택 지역 변수가 아닙니다.
 */
typedef struct {
    Demo d;      /**< The recording. / 기록. */
    int  mode;   /**< DEMO_OFF, DEMO_RECORD or DEMO_PLAY. / 셋 중 하나. */
    int  frame;  /**< Playback cursor. / 재생 커서. */
} DemoDrive;

/**
 * @brief Takes this frame's intent from the recording, if one is playing.
 *
 * ENGLISH
 * -------
 * @param[in,out] dr     The drive; its cursor advances, and its mode is cleared
 *                       when the recording runs out.
 * @param[out]    in     Filled only when 1 is returned.
 * @param[out]    aspect Written only when 1 is returned.
 * @param[out]    dt     Written only when 1 is returned.
 * @return 1 when the recording supplied the frame; 0 when the caller must
 *         gather it from the hardware.
 *
 * @note Writes NOTHING on a zero return, so the caller's own aspect and dt
 *       survive. A function that clobbered them on the way to saying "not mine"
 *       would make the live path depend on whether a demo had ever been loaded.
 * @note THE END OF A RECORDING HANDS CONTROL BACK rather than quitting. A demo
 *       is a thing to watch, and what a player does when it finishes is play --
 *       which also makes it an attract mode without anything being added.
 *
 * 한국어
 * ------
 * @brief 재생 중인 기록이 있으면 이번 프레임의 의도를 그것에서 가져옵니다.
 * @return 기록이 이번 프레임을 공급했으면 1, 호출자가 하드웨어에서 모아야 하면 0.
 *
 * @note 0을 반환할 때는 아무것도 쓰지 않으므로 호출자 자신의 종횡비와 dt가 보존됩니다.
 *       "내 것이 아니다"라고 말하러 가는 길에 그것들을 덮어쓰는 함수는, 라이브 경로가 데모를
 *       한 번이라도 로드했는지에 의존하게 만듭니다.
 * @note 기록의 끝은 종료가 아니라 *조작권 반환*입니다. 데모는 보는 것이고, 그것이 끝났을 때
 *       플레이어가 하는 일은 플레이입니다. 덕분에 아무것도 더하지 않고도 어트랙트 모드가
 *       됩니다.
 */
int demo_take(DemoDrive *dr, Input *in, float *aspect, float *dt);

/**
 * @brief Appends this frame to the recording, if one is being made.
 *
 * @param[in,out] dr    The drive. A no-op unless its mode is ::DEMO_RECORD.
 * @param[in]     in    The intent handed to ::world_step this frame.
 * @param[in]     vw,vh Viewport in pixels.
 * @param[in]     dt    Seconds this frame covered.
 *
 * @note A no-op rather than something the caller guards, so the frame loop has
 *       no branch and no opinion about whether a recording is being made.
 *
 * @brief 기록 중이면 이번 프레임을 덧붙입니다.
 * @note 호출자가 감싸는 것이 아니라 스스로 아무 일도 하지 않으므로, 프레임 루프에는 분기도
 *       기록 여부에 대한 견해도 없습니다.
 */
void demo_put(DemoDrive *dr, const Input *in, int vw, int vh, float dt);

#endif
