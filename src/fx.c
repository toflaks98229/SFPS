/**
 * @file fx.c
 * @brief Implements the data-driven particle system.
 *
 * ENGLISH
 * -------
 * Two halves, split the way the rest of this project splits them: the spawn
 * and the simulation touch no GL and can be driven headlessly, while the draw
 * owns the buffers and the blend state. tools/fxtest.c exercises the first
 * half without ever creating a context.
 *
 * The particles are ONE flat array, not a list per effect. A per-effect array
 * would need a capacity decision per effect and would waste whichever ones are
 * quiet, where a shared pool spends its budget wherever the action is. The
 * cost is that a draw has to group by blend mode at draw time, which is two
 * passes over a 256-entry array -- cheaper than the bookkeeping the split
 * would need.
 *
 * 한국어
 * ------
 * 이 프로젝트의 다른 부분과 동일한 방식으로 두 반쪽으로 나뉩니다. 생성과 시뮬레이션은
 * GL을 사용하지 않아 헤드리스로 구동할 수 있고, 드로우가 버퍼와 블렌드 상태를
 * 소유합니다. tools/fxtest.c가 컨텍스트를 만들지 않고 전자를 검증합니다.
 *
 * 입자는 이펙트별 배열이 아니라 *하나의* 평면 배열입니다. 이펙트별 배열은 이펙트마다
 * 용량을 결정해야 하고 조용한 쪽의 할당을 낭비하지만, 공유 풀은 예산을 상황이 벌어지는
 * 곳에 씁니다. 그 대가로 드로우 시점에 블렌드 모드별로 묶어야 하는데, 이는 256개 배열을
 * 두 번 훑는 것이며 분리에 필요한 관리 비용보다 저렴합니다.
 */

#include "fx.h"
#include "pools.h"
#include "render.h"
#include "data.h"
#include "txt.h"
#include "diag.h"

/* --- Type definitions / 타입 정의 --- */

/** @brief Blend modes an effect may ask for. / 이펙트가 요청할 수 있는 블렌드 모드. */
enum { FX_BLEND_ALPHA = 0, FX_BLEND_ADD };

/** @brief How a particle is oriented. / 입자의 방향 결정 방식. */
enum { FX_FACE_CAMERA = 0, FX_FACE_NORMAL };

/**
 * @struct FxDef
 * @brief One effect recipe, parsed from the text.
 *
 * ENGLISH
 * -------
 * Stored in the units the text uses -- centimetres and milliseconds -- and
 * converted on spawn. Keeping the table in file units means a reload can
 * rewrite it without touching any conversion logic.
 *
 * 한국어
 * ------
 * 텍스트가 사용하는 단위(센티미터, 밀리초) 그대로 저장하고 생성 시점에 변환합니다.
 * 테이블을 파일 단위로 유지하면 리로드가 변환 로직을 건드리지 않고 재작성할 수 있습니다.
 */
typedef struct {
    char  name[FX_NAME_LEN];
    short count;            /**< Particles per spawn. / 생성당 입자 수. */
    short life_ms;          /**< Lifetime, milliseconds. / 수명 (밀리초). */
    short size0, size1;     /**< Size at birth and death, 1/100 units. / 생성/소멸 시 크기 (1/100 단위). */
    short r,  g,  b;        /**< Colour at birth, 0..255. / 생성 시 색상 (0..255). */
    short r2, g2, b2;       /**< Colour at death, 0..255. / 소멸 시 색상 (0..255). */
    short alpha0, alpha1;   /**< Opacity at birth and death, percent. / 생성/소멸 시 불투명도 (퍼센트). */
    short speed, spread;    /**< Initial speed and its variation, cm/s. / 초기 속도와 그 편차 (cm/s). */
    short spawn_r;          /**< Radius the burst starts from, 1/100 units. / 폭발이 시작되는 반경 (1/100 단위). */
    /** Non-zero to spread evenly over the hemisphere at one exact speed, so
        the particles form an expanding shell with a readable edge instead of
        a burst that smears. / 0이 아니면 반구에 균일하게, 정확히 하나의 속력으로
        퍼뜨려 가장자리가 읽히는 팽창하는 껍질을 만듭니다. */
    int   dome;
    /** Non-zero to spread evenly around the RING perpendicular to the normal,
        at one exact speed. `dome` is a shell and this is its equator: with
        `face normal` the quads lie in that same plane, so a disc spawned
        against a floor is a ring of light travelling out across it. The dome
        cannot do this -- half its particles leave the ground.
        / 0이 아니면 법선에 수직인 *고리*를 따라 정확히 하나의 속력으로 균일하게 퍼뜨립니다.
        `dome`이 껍질이라면 이것은 그 적도입니다. `face normal`과 함께 쓰면 사각형들이 같은
        평면에 눕게 되므로, 바닥에 대고 생성한 디스크는 바닥을 가로질러 퍼져 나가는 빛의
        고리가 됩니다. 돔으로는 이것이 되지 않습니다. 입자의 절반이 지면을 떠납니다. */
    int   disc;
    short drag;             /**< Speed lost per second, percent. / 초당 속도 손실 (퍼센트). */
    short spin;             /**< Roll rate, degrees per second. / 회전 속도 (초당 도). */
    short gravity;          /**< Downward acceleration, cm/s^2. / 하향 가속도 (cm/s^2). */
    /** Milliseconds of the particle's OWN travel the quad is drawn over, along
        its velocity. 0 is the square this file has always drawn.

        A SPARK IS NOT A SQUARE AND NEVER WAS. Every burst here is fast enough
        that a real one would smear across the frame, and the square says the
        opposite -- it says the thing is sitting still and merely fading. The
        length is taken from the speed rather than authored outright so that
        the streak SHORTENS as `drag` bleeds the speed off, which is the part
        the eye reads as the spark landing rather than as it vanishing.

        / 입자 *자신의* 이동을 몇 밀리초분 그릴지이며, 속도 방향으로 늘입니다. 0이면 이
        파일이 언제나 그려 온 정사각형입니다.

        *불꽃은 정사각형이 아니며 그런 적도 없습니다.* 이곳의 모든 폭발은 실제라면 화면에
        번질 만큼 빠르고, 정사각형은 그 반대를 말합니다. 가만히 있으면서 흐려질 뿐이라고
        말합니다. 길이를 직접 적지 않고 속력에서 가져오는 이유는, `drag`가 속력을 빼는
        동안 줄무늬가 *짧아지게* 하기 위해서입니다. 눈이 불꽃이 사라진다가 아니라
        내려앉는다고 읽는 것이 바로 그 부분입니다. */
    short stretch;
    /** The effect this particle leaves BEHIND it, as an index into this table,
        or -1 for none. See ::FxDef::trail_ms for the pacing and the note above
        ::spawn_def for why a wake cannot have a wake of its own.
        / 이 입자가 *뒤에* 남기는 이펙트이며, 이 표의 인덱스입니다. 없으면 -1입니다. 간격은
        ::FxDef::trail_ms를, 자취가 자기 자취를 가질 수 없는 이유는 ::spawn_def 위의
        설명을 참조하십시오. */
    short trail;
    /** Milliseconds between wakes. Fixed time, not per frame, for the reason
        ::SHOT_TRAIL_INTERVAL exists: a rate that follows the frame rate makes
        the same spark denser on a faster machine and lets one burst empty the
        shared pool. What is alive at once is life / trail_ms per particle, and
        THAT is the number to budget against ::FX_MAX_PARTICLES -- not the
        total spawned, which is far larger and does not matter.
        / 자취 사이의 밀리초입니다. 프레임마다가 아니라 고정 시간인 이유는
        ::SHOT_TRAIL_INTERVAL이 존재하는 이유와 같습니다. 프레임률을 따르는 방출은 같은
        불꽃을 빠른 기기에서 더 조밀하게 만들고, 한 번의 폭발이 공유 풀을 비우게 합니다.
        동시에 살아 있는 수는 입자당 life / trail_ms이며, ::FX_MAX_PARTICLES에 대해
        예산을 잡아야 하는 것은 *그 수*입니다. 총 생성 수가 아닙니다. 그쪽은 훨씬 크고
        중요하지 않습니다. */
    short trail_ms;
    /** The wake's NAME, held until the whole file has been read. A definition
        may name one that appears further down the file -- `blastember` names
        `emberwake`, which is written after it because that is the order they
        are read in -- so the index cannot be resolved at the line that sets
        it. ::resolve_trails does it once, after the last `e`.
        / 자취의 *이름*이며, 파일 전체를 읽을 때까지 들고 있습니다. 정의는 파일 아래쪽에
        나오는 것을 이름으로 댈 수 있으므로(`blastember`가 `emberwake`를 대며, 그것은
        읽는 순서가 그러하기에 뒤에 쓰여 있습니다) 그 줄에서는 인덱스를 확정할 수
        없습니다. 마지막 `e` 뒤에 ::resolve_trails가 한 번에 처리합니다. */
    char  trail_name[FX_NAME_LEN];
    short blend;            /**< FX_BLEND_*. */
    short face;             /**< FX_FACE_*. */
} FxDef;



/* --- Static state / 정적 상태 --- */

static FxDef      g_defs[FX_MAX_DEFS];
static int        g_n_defs;
static int        g_parsed;


static MeshBuf    g_buf;
static Mesh       g_mesh;
static int        g_buf_ready;

/** @brief Spawn randomness. Its own state so effects never disturb the weapon's. / 생성용 난수. 이펙트가 무기 쪽 난수를 교란하지 않도록 자체 상태를 둡니다. */
/* The seed a pool starts from. A zeroed FxPool means "not seeded yet", which
   is what lets a Pools live in a zero-initialised World and still produce a
   spread rather than a straight line.
   풀이 출발하는 씨앗입니다. 0인 FxPool은 "아직 씨앗이 채워지지 않음"을 뜻하며, 그래서
   Pools가 0으로 초기화된 World 안에 있어도 직선이 아니라 퍼짐을 만들어 냅니다. */
#define FX_RNG_SEED 0x9e3779b9u

/* --- Static helpers / 정적 헬퍼 --- */

static float frand(FxPool *fx) {
    /* Seeded on first use rather than at construction, so that a zeroed FxPool
       is a valid one. Pools lives inside a World that world_init clears
       wholesale, and requiring a separate fx_init would be a second thing to
       remember that nothing would remind anybody of -- exactly the shape of
       fault this whole move is removing.
       생성 시점이 아니라 첫 사용 시에 씨앗을 채우므로 0인 FxPool도 유효한 풀입니다. Pools는
       world_init이 통째로 비우는 World 안에 있으며, 별도의 fx_init을 요구하는 것은 아무도
       상기시켜 주지 않는 두 번째 기억거리가 됩니다. 이 이동 전체가 제거하고 있는 결함의
       모양 그대로입니다. */
    if (!fx->rng) fx->rng = FX_RNG_SEED;
    fx->rng = fx->rng * 1664525u + 1013904223u;
    return ((fx->rng >> 8) & 0xffff) / 65536.0f;
}
static float frand_signed(FxPool *fx) { return frand(fx) * 2.0f - 1.0f; }

/**
 * @brief Parses assets\effects.txt into ::g_defs.
 *
 * ENGLISH
 * -------
 * One `e <name>` starts a definition and every later keyword applies to it,
 * the same shape sounds.txt and models.txt use -- a reader who has seen one of
 * those can read this without being told.
 *
 * Unknown keywords are SKIPPED rather than treated as an error. The alternative
 * is refusing to load an otherwise valid file because it mentions a parameter
 * this build does not have, which makes the format impossible to extend without
 * breaking older builds.
 *
 * 한국어
 * ------
 * `e <이름>`이 정의를 시작하고 이후의 모든 키워드가 그 정의에 적용됩니다. sounds.txt와
 * models.txt가 쓰는 것과 같은 형태이므로, 그중 하나를 본 사람은 설명 없이도 이것을 읽을
 * 수 있습니다.
 *
 * 알 수 없는 키워드는 오류로 처리하지 않고 *건너뜁니다*. 그러지 않으면 이 빌드에 없는
 * 매개변수를 언급했다는 이유로 그 외에는 멀쩡한 파일의 로드를 거부하게 되며, 오래된
 * 빌드를 깨뜨리지 않고는 형식을 확장할 수 없게 됩니다.
 */
/* Forward-declared for the one line at the bottom of ::parse_defs that needs
   it. The definition sits beside ::find_def instead, because the name lookup
   is the whole of what it does and a reader who wants to check it wants both
   at once.
   ::parse_defs 맨 아래의 한 줄을 위해 전방 선언합니다. 정의는 ::find_def 곁에 두었습니다.
   이름 찾기가 하는 일의 전부이며, 확인하려는 독자는 둘을 함께 보고 싶어 하기
   때문입니다. */
static void resolve_trails(void);

static void parse_defs(void) {
    const char *p = data_text(DATA_EFFECTS);
    FxDef *cur = 0;

    g_n_defs = 0;

    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;

        if (txt_is(t, len, "e")) {
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;

            /* Past the cap the recipe is parsed and discarded, so the effect
               simply never plays -- reported, because "that puff is missing"
               names no cause on its own.
               한계를 넘으면 레시피가 파싱되되 버려지므로 해당 이펙트는 아예 재생되지
               않습니다. "그 효과가 안 나온다"만으로는 원인을 알 수 없으므로 보고합니다. */
            if (g_n_defs >= FX_MAX_DEFS) { DIAG(DIAG_FX_CAP); cur = 0; continue; }

            cur = &g_defs[g_n_defs++];

            /* Defaults, so a definition that sets only what it cares about is
               still drawable. A count of 0 would make the effect invisible for
               a reason no one would guess.
               관심 있는 값만 설정한 정의도 그릴 수 있도록 기본값을 둡니다. count가 0이면
               아무도 짐작하지 못할 이유로 이펙트가 보이지 않게 됩니다. */
            txt_copy(cur->name, FX_NAME_LEN, nm, len);
            cur->count = 1;   cur->life_ms = 300;
            cur->size0 = 10;  cur->size1   = 10;
            cur->r = 255; cur->g = 255; cur->b = 255;
            /* Death colour defaults to the birth colour, so an effect that
               says nothing about rgb2 holds one tone -- which is what every
               definition written before rgb2 existed expects.
               소멸 색상은 생성 색상을 기본값으로 삼으므로, rgb2를 지정하지 않은 이펙트는
               한 가지 색을 유지합니다. rgb2가 생기기 전에 작성된 모든 정의가 기대하는
               동작입니다. */
            cur->r2 = -1; cur->g2 = -1; cur->b2 = -1;
            cur->alpha0 = 100; cur->alpha1 = 0;
            cur->speed = 0;   cur->spread = 0;
            cur->spawn_r = 0; cur->drag = 0; cur->spin = 0;
            cur->dome = 0; cur->disc = 0;
            cur->gravity = 0; cur->blend = FX_BLEND_ALPHA;
            cur->face = FX_FACE_CAMERA;
            /* No streak and no wake unless the definition asks. Both are read
               on every particle of every effect, so the default has to be the
               one that costs nothing -- and it has to be the behaviour the
               thirty-five definitions written before these existed already
               expect.
               정의가 요청하지 않는 한 줄무늬도 자취도 없습니다. 둘 다 모든 이펙트의 모든
               입자에서 읽히므로 기본값은 비용이 없는 쪽이어야 하고, 이들이 생기기 전에
               작성된 서른다섯 개의 정의가 이미 기대하고 있는 동작이어야 합니다. */
            cur->stretch = 0; cur->trail = -1; cur->trail_ms = 0;
            cur->trail_name[0] = 0;
            continue;
        }

        if (!cur) continue;   /* a keyword before any `e` has nothing to apply to */

        /* Zeroed, not merely declared: txt_read_int leaves its output alone on
           a malformed number, so a line like `size 12` (one value where two
           are expected) would otherwise read whatever was on the stack. `ok`
           gates the assignment, but a value the compiler cannot prove is
           initialised is a value worth initialising.
           선언만 하지 않고 0으로 채웁니다. txt_read_int는 숫자 형식이 잘못되면 출력을
           건드리지 않으므로, `size 12`처럼 값이 두 개여야 할 곳에 하나만 있으면 스택에
           남아 있던 값을 읽게 됩니다. `ok`가 대입을 막기는 하지만, 컴파일러가 초기화를
           증명할 수 없는 값이라면 초기화해 두는 편이 낫습니다. */
        int ok = 1, v[3] = {0, 0, 0};

        if (txt_is(t, len, "count"))
            { p = txt_read_int(p, &v[0], &ok); if (ok) cur->count = (short)v[0]; }
        else if (txt_is(t, len, "life"))
            { p = txt_read_int(p, &v[0], &ok); if (ok) cur->life_ms = (short)v[0]; }
        else if (txt_is(t, len, "gravity"))
            { p = txt_read_int(p, &v[0], &ok); if (ok) cur->gravity = (short)v[0]; }
        else if (txt_is(t, len, "spawn"))
            { p = txt_read_int(p, &v[0], &ok); if (ok) cur->spawn_r = (short)v[0]; }
        else if (txt_is(t, len, "dome"))
            p = txt_read_int(p, &cur->dome, &ok);
        else if (txt_is(t, len, "disc"))
            p = txt_read_int(p, &cur->disc, &ok);
        else if (txt_is(t, len, "drag"))
            { p = txt_read_int(p, &v[0], &ok); if (ok) cur->drag = (short)v[0]; }
        else if (txt_is(t, len, "spin"))
            { p = txt_read_int(p, &v[0], &ok); if (ok) cur->spin = (short)v[0]; }
        else if (txt_is(t, len, "stretch"))
            { p = txt_read_int(p, &v[0], &ok); if (ok) cur->stretch = (short)v[0]; }
        else if (txt_is(t, len, "trailms"))
            { p = txt_read_int(p, &v[0], &ok); if (ok) cur->trail_ms = (short)v[0]; }
        else if (txt_is(t, len, "trail")) {
            /* The NAME, not a number: the effect it points at may not have
               been read yet. ::resolve_trails turns these into indices after
               the last definition.
               숫자가 아니라 *이름*입니다. 가리키는 이펙트가 아직 읽히지 않았을 수
               있습니다. 마지막 정의 뒤에 ::resolve_trails가 이것을 인덱스로 바꿉니다. */
            const char *m = txt_token(p, &len);
            if (m) { p = m + len; txt_copy(cur->trail_name, FX_NAME_LEN, m, len); }
        }
        else if (txt_is(t, len, "rgb2")) {
            p = txt_read_int(p, &v[0], &ok);
            p = txt_read_int(p, &v[1], &ok);
            p = txt_read_int(p, &v[2], &ok);
            if (ok) { cur->r2 = (short)v[0]; cur->g2 = (short)v[1]; cur->b2 = (short)v[2]; }
        }
        else if (txt_is(t, len, "size")) {
            p = txt_read_int(p, &v[0], &ok);
            p = txt_read_int(p, &v[1], &ok);
            if (ok) { cur->size0 = (short)v[0]; cur->size1 = (short)v[1]; }
        } else if (txt_is(t, len, "alpha")) {
            p = txt_read_int(p, &v[0], &ok);
            p = txt_read_int(p, &v[1], &ok);
            if (ok) { cur->alpha0 = (short)v[0]; cur->alpha1 = (short)v[1]; }
        } else if (txt_is(t, len, "speed")) {
            p = txt_read_int(p, &v[0], &ok);
            p = txt_read_int(p, &v[1], &ok);
            if (ok) { cur->speed = (short)v[0]; cur->spread = (short)v[1]; }
        } else if (txt_is(t, len, "rgb")) {
            p = txt_read_int(p, &v[0], &ok);
            p = txt_read_int(p, &v[1], &ok);
            p = txt_read_int(p, &v[2], &ok);
            if (ok) { cur->r = (short)v[0]; cur->g = (short)v[1]; cur->b = (short)v[2]; }
        } else if (txt_is(t, len, "blend")) {
            const char *m = txt_token(p, &len);
            if (m) { p = m + len; cur->blend = txt_is(m, len, "add")
                                              ? FX_BLEND_ADD : FX_BLEND_ALPHA; }
        } else if (txt_is(t, len, "face")) {
            const char *m = txt_token(p, &len);
            if (m) { p = m + len; cur->face = txt_is(m, len, "normal")
                                             ? FX_FACE_NORMAL : FX_FACE_CAMERA; }
        }
        /* anything else: skipped, see the note above */
    }

    resolve_trails();
    g_parsed = 1;
}

static int find_def(const char *name) {
    for (int i = 0; i < g_n_defs; i++) {
        const char *a = g_defs[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}

/**
 * @brief Turns every `trail <name>` into an index, once the file is fully read.
 *
 * ENGLISH: A name that matches nothing leaves the trail at -1, which is the
 * same as not having asked for one -- the file's own rule for every other
 * unknown token, and the rule ::fx_spawn_scaled already follows when it is
 * handed a name that is not there. A DEFINITION MAY NAME ITSELF and the loop
 * does not stop it: a particle that emits a copy of itself every trail_ms is a
 * chain that ends anyway, because ::spawn_def refuses the second link. That is
 * a property of the emitter and not of this table, so this function has no
 * opinion about it.
 *
 * 한국어: 아무것과도 맞지 않는 이름은 자취를 -1로 남기며, 이는 자취를 요청하지 않은 것과
 * 같습니다. 다른 모든 미지의 토큰에 대한 이 파일 자신의 규칙이고, 없는 이름을 건네받았을
 * 때 ::fx_spawn_scaled가 이미 따르는 규칙입니다. *정의는 자기 자신을 이름으로 댈 수
 * 있으며* 이 반복문은 그것을 막지 않습니다. trail_ms마다 자기 복제를 방출하는 입자도 결국
 * 끝나는 사슬입니다. ::spawn_def가 두 번째 고리를 거절하기 때문입니다. 그것은 방출하는
 * 쪽의 성질이지 이 표의 성질이 아니므로, 이 함수는 그에 대해 아무 견해도 갖지 않습니다.
 */
static void resolve_trails(void) {
    for (int i = 0; i < g_n_defs; i++)
        g_defs[i].trail = g_defs[i].trail_name[0]
                        ? (short)find_def(g_defs[i].trail_name) : (short)-1;
}

void fx_tint_colour(FxTint tint, float rgb[3]) {
    /* {0,0,0} is "as authored", and it is the common case: every effect in the
       text that nobody recolours arrives here and leaves untouched.
       {0,0,0}은 "작성된 그대로"이며 그것이 일반적인 경우입니다. 아무도 다시 칠하지 않는
       텍스트의 모든 이펙트가 이곳에 와서 그대로 나갑니다. */
    if (!(tint.r | tint.g | tint.b)) return;

    float v = rgb[0] > rgb[1] ? rgb[0] : rgb[1];
    if (rgb[2] > v) v = rgb[2];
    if (v <= 0.0f) return;

    float m = rgb[0] < rgb[1] ? rgb[0] : rgb[1];
    if (rgb[2] < m) m = rgb[2];

    /* How far the authored colour is from white, 0..1. This is the whole of
       what is borrowed from the text: `v` says how bright the particle is at
       this instant of its life and `s` says how much colour that instant
       carries, which together are the ramp the artist drew. The tint only
       decides WHICH colour.
       작성된 색이 흰색에서 얼마나 떨어져 있는가입니다(0..1). 텍스트에서 빌려 오는 것은
       이것이 전부입니다. `v`는 이 순간 입자가 얼마나 밝은지를, `s`는 그 순간이 색을 얼마나
       띠는지를 말하며, 둘이 합쳐 작성자가 그린 변화 곡선입니다. 색조가 정하는 것은 *어느*
       색인가뿐입니다. */
    float s = (v - m) / v;

    float t[3] = { tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f };
    float tv = t[0] > t[1] ? t[0] : t[1];
    if (t[2] > tv) tv = t[2];
    if (tv <= 0.0f) return;

    /* Normalised by its own brightest channel, so the tint names a hue and
       cannot dim what it paints -- see ::FxTint.
       가장 밝은 자기 채널로 정규화하므로, 색조는 색상을 지칭할 뿐 자신이 칠하는 것을 어둡게
       만들 수 없습니다. ::FxTint를 참조하십시오. */
    for (int i = 0; i < 3; i++) rgb[i] = v * (1.0f - s + s * t[i] / tv);
}

/* --- Spawning / 생성 --------------------------------------------------------
 *
 * ENGLISH
 * -------
 * The whole of spawning, taking a resolved INDEX rather than a name. The two
 * public entry points below look the name up and call this; ::fx_update calls
 * it with the index a definition's `trail` already holds, which is the reason
 * the split exists at all -- a wake is emitted sixteen times a second per
 * particle and must not pay for a string compare against every definition in
 * the file each time.
 *
 * `wake` IS WHERE THE CHAIN ENDS, and it ends by construction rather than by
 * validation. A particle spawned as somebody's wake is created with no wake of
 * its own, so `trail` can never be more than one link deep however the text is
 * written -- including the case where a definition names itself. The
 * alternative was to check the table for cycles at parse time, which is a rule
 * that has to be right in a place nobody looks, to prevent something the
 * emitter can simply decline to do.
 *
 * 한국어
 * ------
 * 생성의 전부이며, 이름이 아니라 확정된 *인덱스*를 받습니다. 아래의 두 공개 진입점이
 * 이름을 찾아 이것을 호출하고, ::fx_update는 정의의 `trail`이 이미 들고 있는 인덱스로
 * 호출합니다. 분리한 이유가 바로 그것입니다. 자취는 입자 하나당 초당 열여섯 번쯤
 * 방출되며, 그때마다 파일의 모든 정의와 문자열을 비교하는 값을 치를 수 없습니다.
 *
 * *`wake`가 사슬이 끝나는 곳이며*, 검증이 아니라 구조로 끝납니다. 누군가의 자취로 생성된
 * 입자는 자기 자취 없이 만들어지므로, 텍스트를 어떻게 쓰든 `trail`은 한 고리보다 깊어질
 * 수 없습니다. 정의가 자기 자신을 이름으로 대는 경우까지 포함해서입니다. 대안은 파싱
 * 시점에 표에서 순환을 검사하는 것이었는데, 그것은 아무도 보지 않는 곳에서 옳아야 하는
 * 규칙이며, 막으려는 대상은 방출하는 쪽이 그냥 하지 않으면 그만인 일입니다.
 */
static void spawn_def(Pools *pl, int di, v3 pos, v3 normal, float scale, int wake,
                      FxTint tint) {
    const FxDef *d = &g_defs[di];

    /* A basis around the normal, so `spread` can throw particles off-axis
       without every effect having to pick its own arbitrary side vector.
       법선 주위의 기저입니다. 덕분에 모든 이펙트가 각자 임의의 측면 벡터를 고르지 않고도
       `spread`가 입자를 축에서 벗어나게 던질 수 있습니다. */
    v3 n = v3len(normal) > 1e-4f ? v3norm(normal) : v3f(0, 1, 0);
    v3 hint = (n.y > 0.9f || n.y < -0.9f) ? v3f(1, 0, 0) : v3f(0, 1, 0);
    v3 t = v3norm(v3cross(hint, n));
    v3 b = v3cross(n, t);

    for (int i = 0; i < d->count; i++) {
        /* Overwrite oldest-first. A full pool drops the OLDEST rather than
           refusing the newest: the particle that just spawned is the one the
           player is looking at.
           오래된 것부터 덮어씁니다. 풀이 가득 차면 최신 것을 거부하지 않고 *가장 오래된*
           것을 버립니다. 방금 생성된 입자가 바로 플레이어가 보고 있는 것이기 때문입니다. */
        FxParticle *q = &pl->fx.parts[pl->fx.next];
        if (q->life > 0.0f) DIAG(DIAG_FX_CAP);
        pl->fx.next = (pl->fx.next + 1) % FX_MAX_PARTICLES;

        float sp = (d->speed + d->spread * frand_signed(&pl->fx)) * 0.01f * scale;
        v3 dir = n;

        /* A SHELL, not a burst. Every particle gets the same speed and a
           direction spread evenly over the hemisphere around the normal, so
           they all reach the same distance at the same moment and the cloud
           reads as an expanding dome with an edge.
           `spread` cannot do this: it scatters the direction but keeps the
           speed varying, so the front smears out and the thing you are trying
           to show -- where the blast stops -- is the one part that is not
           visible. Which is the whole reason the dome is drawn: the radius is
           a gameplay number, and a player who cannot see it is guessing.
           폭발이 아니라 *껍질*입니다. 모든 입자가 같은 속력과, 법선 주위 반구에 고르게
           퍼진 방향을 받으므로 같은 순간에 같은 거리에 도달하고, 구름이 가장자리를 가진
           팽창하는 돔으로 읽힙니다. `spread`로는 이것이 되지 않습니다. 방향은 흩뜨리되
           속력이 계속 변하므로 앞면이 번지고, 정작 보여 주려던 것(폭발이 멈추는 지점)이
           보이지 않는 유일한 부분이 됩니다. 돔을 그리는 이유가 바로 그것입니다. 반경은
           게임플레이 수치이고, 그것을 볼 수 없는 플레이어는 짐작하게 됩니다. */
        if (d->dome) {
            /* Uniform over the hemisphere: cos is uniform in [0,1] rather than
               the angle, or the particles bunch at the pole and the dome comes
               out with a bright cap and a thin skirt.
               반구에 균일하게 분포시킵니다. 각도가 아니라 코사인을 [0,1]에서 균일하게
               뽑습니다. 그러지 않으면 입자가 극에 몰려 돔의 꼭대기만 밝고 자락이
               얇아집니다. */
            float c   = frand(&pl->fx);
            float s2  = sqrtf(1.0f - c * c);
            float phi = frand(&pl->fx) * 6.2831853f;
            dir = v3norm(v3add(v3scale(n, c),
                               v3add(v3scale(t, s2 * cosf(phi)),
                                     v3scale(b, s2 * sinf(phi)))));
            sp = d->speed * 0.01f * scale;  /* no jitter: the edge is the point */
        }
        else if (d->disc) {
            /* The dome's equator. Same fixed speed for the same reason -- the
               edge is the point -- but the direction stays in the plane the
               normal is perpendicular to, so nothing leaves the surface.
               돔의 적도입니다. 같은 이유(가장자리가 요점입니다)로 속력도 고정이지만, 방향은
               법선이 수직인 그 평면 안에 머무르므로 아무것도 표면을 떠나지 않습니다. */
            float phi = frand(&pl->fx) * 6.2831853f;
            dir = v3norm(v3add(v3scale(t, cosf(phi)), v3scale(b, sinf(phi))));
            sp  = d->speed * 0.01f * scale;
        }
        else if (d->spread) {
            /* Scatter around the normal by up to the spread's own share of a
               right angle, so a wide spread reads as a burst and a narrow one
               as a jet. */
            float k = 0.5f * (float)d->spread / (float)(d->speed ? d->speed : 1);
            if (k > 1.0f) k = 1.0f;
            dir = v3norm(v3add(n, v3add(v3scale(t, frand_signed(&pl->fx) * k),
                                        v3scale(b, frand_signed(&pl->fx) * k))));
        }

        /* Scatter the start point. Every particle leaving the same spot makes
           them overlap while they are still large, and overlapping additive
           quads saturate at once -- two at 90% alpha composite to 99.7%. The
           offset is what turns a white blob into separable sparks.
           시작점을 흩뜨립니다. 모든 입자가 같은 지점에서 출발하면 아직 큰 상태에서 서로
           겹치는데, 겹친 가산 사각형은 즉시 포화됩니다. 알파 90% 두 장이 99.7%가 됩니다.
           이 오프셋이 흰 얼룩을 구분 가능한 불꽃으로 바꿉니다. */
        v3 at = pos;
        if (d->spawn_r) {
            float rr = d->spawn_r * 0.01f;
            at = v3add(at, v3f(frand_signed(&pl->fx) * rr,
                               frand_signed(&pl->fx) * rr,
                               frand_signed(&pl->fx) * rr));
        }

        q->pos      = at;
        q->vel      = v3scale(dir, sp);
        q->axis     = n;
        q->life     = d->life_ms * 0.001f;
        q->life_max = q->life > 0.0f ? q->life : 1e-4f;
        /* Random starting roll even when spin is 0: the angle matters on its
           own, because identically-oriented squares line their edges up and
           read as one shape.
           spin이 0이어도 시작 회전각은 무작위입니다. 각도 자체가 중요한데, 동일하게
           정렬된 정사각형들은 모서리가 맞아떨어져 하나의 형태로 읽히기 때문입니다. */
        q->roll     = frand(&pl->fx) * 6.2831853f;
        q->def      = (short)di;
        q->tint[0]  = tint.r;
        q->tint[1]  = tint.g;
        q->tint[2]  = tint.b;

        /* WHEN THIS ONE FIRST LEAVES SOMETHING BEHIND, and a negative means
           never -- which is what a wake gets, and what anything with no
           `trail` gets for free.
           The first interval is a fraction of a whole one rather than a whole
           one, because a burst of fourteen embers spawns on a single frame and
           fourteen wakes emitted together, forever, on the same frame, is a
           dotted line of clumps rather than a trail. Scattering the first
           interval is all it takes; after that they stay apart on their own.
           *이것이 처음으로 무언가를 뒤에 남기는 시점*이며, 음수는 결코 남기지 않음을
           뜻합니다. 자취가 받는 값이고, `trail`이 없는 것이 거저 받는 값입니다.
           첫 간격이 온전한 하나가 아니라 그 일부인 이유는, 불티 열넷이 한 프레임에
           생성되는데 열넷의 자취가 언제까지나 같은 프레임에 함께 방출되면 궤적이 아니라
           덩어리들의 점선이 되기 때문입니다. 첫 간격만 흩어 놓으면 되고, 그 뒤로는 스스로
           떨어져 있습니다. */
        q->trail_t  = (wake && d->trail >= 0 && d->trail_ms > 0)
                    ? d->trail_ms * 0.001f * frand(&pl->fx) : -1.0f;
    }
}

/* --- Public API / 공개 API --- */

void fx_spawn(Pools *pl, const char *name, v3 pos, v3 normal) {
    fx_spawn_scaled(pl, name, pos, normal, 1.0f);
}

void fx_spawn_scaled(Pools *pl, const char *name, v3 pos, v3 normal, float scale) {
    FxTint none = { 0, 0, 0 };
    fx_spawn_tinted(pl, name, pos, normal, scale, none);
}

void fx_spawn_tinted(Pools *pl, const char *name, v3 pos, v3 normal, float scale,
                     FxTint tint) {
    if (!g_parsed) parse_defs();

    int di = find_def(name);
    if (di < 0) return;                 /* unknown name: nothing, and not an error */
    spawn_def(pl, di, pos, normal, scale, 1, tint);
}

void fx_update(Pools *pl, float dt) {
    for (int i = 0; i < FX_MAX_PARTICLES; i++) {
        FxParticle *q = &pl->fx.parts[i];
        if (q->life <= 0.0f) continue;

        const FxDef *d = &g_defs[q->def];

        q->vel.y -= d->gravity * 0.01f * dt;

        /* Air drag. A burst whose particles hold their speed for the whole
           life flies apart at a constant rate and reads as a expanding shell;
           bleeding the speed off makes them shoot out and settle, which is
           what debris actually does.
           공기 저항입니다. 입자가 수명 내내 속도를 유지하는 폭발은 일정한 속도로 퍼져
           나가 팽창하는 껍질처럼 보입니다. 속도를 빼면 튀어 나갔다가 잦아드는데, 이것이
           실제 파편의 움직임입니다. */
        if (d->drag) {
            float k = 1.0f - d->drag * 0.01f * dt;
            if (k < 0.0f) k = 0.0f;
            q->vel = v3scale(q->vel, k);
        }

        q->pos   = v3add(q->pos, v3scale(q->vel, dt));
        q->roll += d->spin * (3.14159265f / 180.0f) * dt;

        q->life -= dt;
        if (q->life < 0.0f) q->life = 0.0f;

        /* --- the wake / 자취 -------------------------------------------
           A thrown ember reads as thrown because of what is behind it, not
           because of the ember: a bright dot arcing across a dark room is a
           dot moving, and the same dot with a metre of smoke trailing it is
           something that was HURLED out of the blast. That is the one thing
           in the reference this file could not say, because every effect here
           begins and ends at the point it was spawned at.
           LAST IN THE BODY AND NOTHING AFTER IT, deliberately. The spawn
           writes through the pool's ring cursor, and the ring may land on this
           very slot -- the pool overwrites oldest-first and makes no exception
           for the particle whose own update is in progress. Everything this
           iteration needed from `q` has been read and written by the time the
           call happens, so the worst case is a particle that ends one frame
           early in favour of the wake that replaced it, which is the trade
           ::FX_MAX_PARTICLES already makes everywhere else.
           던져진 불티가 던져진 것으로 읽히는 이유는 불티 자체가 아니라 그 *뒤에* 있는
           것 때문입니다. 어두운 방을 가로질러 포물선을 그리는 밝은 점은 그냥 움직이는
           점이고, 같은 점에 1미터의 연기가 딸리면 폭발에서 *내던져진* 무언가입니다.
           참고한 영상에서 이 파일이 말할 수 없었던 것이 그 하나입니다. 이곳의 모든
           이펙트가 생성된 지점에서 시작해 그 지점에서 끝나기 때문입니다.
           *본문의 마지막이며 뒤에 아무것도 없습니다.* 의도적입니다. 생성은 풀의 링 커서를
           통해 쓰이고, 링은 바로 이 슬롯에 내려앉을 수 있습니다. 풀은 오래된 것부터
           덮어쓰며 자기 갱신이 진행 중인 입자라고 예외를 두지 않습니다. 이 반복이 `q`에서
           필요로 한 모든 것은 호출 시점에 이미 읽고 쓰였으므로, 최악의 경우는 자신을
           대체한 자취에게 자리를 내주고 한 프레임 일찍 끝나는 입자이며, 그것은
           ::FX_MAX_PARTICLES가 이미 다른 모든 곳에서 하고 있는 거래입니다. */
        if (q->trail_t >= 0.0f && q->life > 0.0f) {
            q->trail_t -= dt;
            if (q->trail_t <= 0.0f) {
                q->trail_t = d->trail_ms * 0.001f;

                /* Thrown BACKWARD along the flight, so whatever spread the
                   wake has drifts behind the particle rather than ahead of
                   it -- the same rule enemy.c's bolt trail follows, and for
                   the same reason.
                   비행 방향의 *반대로* 던집니다. 그래야 자취가 가진 산포가 입자 앞이
                   아니라 뒤로 흩어집니다. enemy.c의 볼트 궤적이 따르는 규칙과 같고,
                   이유도 같습니다. */
                float sp = v3len(q->vel);
                v3 back = sp > 1e-4f ? v3scale(q->vel, -1.0f / sp) : v3f(0, 1, 0);
                FxTint wt = { q->tint[0], q->tint[1], q->tint[2] };
                spawn_def(pl, d->trail, q->pos, back, 1.0f, 0, wt);
            }
        }
    }
}

void fx_reload(Pools *pl) {
    for (int i = 0; i < FX_MAX_PARTICLES; i++) pl->fx.parts[i].life = 0.0f;
    pl->fx.next   = 0;
    g_parsed = 0;
}

int fx_live_count(const Pools *pl) {
    int n = 0;
    for (int i = 0; i < FX_MAX_PARTICLES; i++) if (pl->fx.parts[i].life > 0.0f) n++;
    return n;
}

float fx_mean_height(const Pools *pl) {
    float sum = 0.0f;
    int   n   = 0;
    for (int i = 0; i < FX_MAX_PARTICLES; i++)
        if (pl->fx.parts[i].life > 0.0f) { sum += pl->fx.parts[i].pos.y; n++; }
    return n ? sum / (float)n : 0.0f;
}

void fx_radius_spread(const Pools *pl, v3 origin, float *mean, float *width) {
    float sum = 0.0f, wide = 0.0f;
    int   n   = 0;
    for (int i = 0; i < FX_MAX_PARTICLES; i++) {
        if (pl->fx.parts[i].life <= 0.0f) continue;
        v3 d = v3sub(pl->fx.parts[i].pos, origin);
        sum += v3len(d); n++;
        /* Distance from the vertical axis, not from the point. A dome's
           equator goes sideways; a burst thrown along one direction does not,
           however far it travels.
           점이 아니라 *수직축*으로부터의 거리입니다. 돔의 적도는 옆으로 뻗지만, 한 방향으로
           던져진 폭발은 아무리 멀리 가도 그렇지 않습니다. */
        float h = sqrtf(d.x * d.x + d.z * d.z);
        if (h > wide) wide = h;
    }
    if (mean)  *mean  = n ? sum / (float)n : 0.0f;
    if (width) *width = wide;
}

int fx_def_count(void) {
    if (!g_parsed) parse_defs();
    return g_n_defs;
}

/**
 * @brief Draws every particle of one blend mode.
 *
 * Takes no matrix: fx_draw sets the MVP once for both passes, so passing it
 * down would be a parameter that could disagree with the state actually bound.
 * 행렬을 받지 않습니다. fx_draw가 두 패스 모두에 대해 MVP를 한 번 설정하므로, 이를
 * 내려보내면 실제로 바인딩된 상태와 어긋날 수 있는 매개변수가 됩니다.
 *
 * ENGLISH
 * -------
 * Split out so the two modes can be issued as two passes without duplicating
 * the build. Each particle gets its own draw call because each carries its own
 * colour and alpha -- the same trade the monster billboards make, and for the
 * same reason: a per-vertex colour attribute would batch them, at the cost of
 * four bytes on every vertex in the project.
 *
 * 한국어
 * ------
 * 생성 코드를 중복하지 않고 두 모드를 두 패스로 발행할 수 있도록 분리했습니다. 각 입자가
 * 고유한 색과 알파를 지니므로 그리기 호출도 하나씩입니다. 몬스터 빌보드와 동일한
 * 트레이드오프이며 이유도 같습니다. 정점별 색상 속성을 두면 일괄 처리가 가능하지만,
 * 프로젝트의 모든 정점에 4바이트를 더하는 대가를 치릅니다.
 */
static void draw_pass(const Pools *pl, int blend, v3 cam_right, v3 cam_up) {
    int idx[FX_MAX_PARTICLES], n = 0;

    mb_reset(&g_buf);
    for (int i = 0; i < FX_MAX_PARTICLES; i++) {
        const FxParticle *q = &pl->fx.parts[i];
        if (q->life <= 0.0f) continue;
        const FxDef *d = &g_defs[q->def];
        if (d->blend != blend) continue;

        float u = 1.0f - q->life / q->life_max;          /* 0 at birth, 1 at death */
        float sz = (d->size0 + (d->size1 - d->size0) * u) * 0.01f;

        /* Roll the basis rather than the quad: mb_billboard builds from two
           axes, so rotating those rotates the sprite for free. Without it
           every quad in a burst is axis-aligned and their square edges line
           up, which reads as one shape being scaled rather than as separate
           particles.
           사각형이 아니라 기저를 회전시킵니다. mb_billboard가 두 축으로부터 생성하므로,
           그 축을 회전시키면 스프라이트가 추가 비용 없이 회전합니다. 이것이 없으면 폭발의
           모든 사각형이 축에 정렬되어 정사각형 모서리가 맞아떨어지고, 개별 입자가 아니라
           하나의 형태가 커지는 것으로 읽힙니다. */
        float cr = cosf(q->roll), sr = sinf(q->roll);

        if (d->face == FX_FACE_NORMAL) {
            /* Lie flat on the surface: a decal, not a billboard. */
            v3 nn = q->axis;
            v3 hint = (nn.y > 0.9f || nn.y < -0.9f) ? v3f(1,0,0) : v3f(0,1,0);
            v3 tt = v3norm(v3cross(hint, nn));
            v3 bb = v3cross(nn, tt);
            v3 rr = v3add(v3scale(tt,  cr), v3scale(bb, sr));
            v3 uu = v3add(v3scale(tt, -sr), v3scale(bb, cr));
            mb_billboard(&g_buf, q->pos, rr, uu, sz, sz);
        } else if (d->stretch && v3len(q->vel) > 1e-4f) {
            /* --- the streak / 줄무늬 ------------------------------------
               Along the velocity, not along the roll: the direction of travel
               IS the orientation here, so `spin` and the random starting angle
               have nothing to say and are not read. A spark whose streak sat
               at its own angle would be a streak pointing somewhere the spark
               is not going, which is worse than the square it replaces.
               The length is speed x time and the width is the authored size,
               so a definition tunes the streak by tuning `stretch` alone and
               `size` keeps meaning what it means everywhere else in the file.
               회전이 아니라 속도 방향입니다. 이곳에서는 진행 방향이 곧 방향이므로 `spin`과
               무작위 시작 각도는 할 말이 없고 읽히지도 않습니다. 자기 각도로 놓인 줄무늬는
               불꽃이 가지 않는 쪽을 가리키는 줄무늬이며, 그것이 대체하는 정사각형보다
               나쁩니다. 길이는 속력 x 시간이고 폭은 작성된 크기이므로, 정의는 `stretch`
               하나만 조정해 줄무늬를 다듬고 `size`는 파일의 다른 모든 곳에서 뜻하는 바를
               그대로 유지합니다. */
            float sp = v3len(q->vel);
            v3 vd  = v3scale(q->vel, 1.0f / sp);
            v3 fwd = v3cross(cam_right, cam_up);

            /* The screen-space perpendicular. Degenerate exactly when the
               particle is flying at the camera, where a streak has no
               direction on screen to have -- cam_right is as good an answer as
               any and the quad is square-on anyway.
               화면 공간의 수직입니다. 입자가 카메라를 향해 날아올 때 정확히 퇴화하는데,
               그때 줄무늬는 화면상에 가질 방향이 없습니다. cam_right가 다른 어떤 답 못지
               않고, 어차피 사각형은 정면을 향합니다. */
            v3 side = v3cross(vd, fwd);
            side = v3len(side) > 1e-3f ? v3norm(side) : cam_right;

            float len = sz + sp * d->stretch * 0.001f;

            /* Pulled back by what the stretch added, so the HEAD stays at the
               particle. Centred on it instead, a fast spark draws half its
               streak in front of itself and arrives somewhere it has not got
               to yet -- and the bright end is the end the eye tracks.
               늘어난 만큼 뒤로 당깁니다. 그래야 *머리*가 입자 위에 남습니다. 대신 중심을
               맞추면 빠른 불꽃은 줄무늬의 절반을 자기 앞에 그리며 아직 도달하지 않은 곳에
               도착하고, 눈이 따라가는 쪽은 그 밝은 끝입니다. */
            v3 at = v3sub(q->pos, v3scale(vd, (len - sz) * 0.5f));
            mb_billboard(&g_buf, at, side, vd, sz, len);
        } else {
            v3 rr = v3add(v3scale(cam_right,  cr), v3scale(cam_up, sr));
            v3 uu = v3add(v3scale(cam_right, -sr), v3scale(cam_up, cr));
            mb_billboard(&g_buf, q->pos, rr, uu, sz, sz);
        }
        idx[n++] = i;
    }
    if (!n) return;

    mesh_upload(&g_mesh, &g_buf, 1);
    glBindVertexArray(g_mesh.vao);
    for (int k = 0; k < n; k++) {
        const FxParticle *q = &pl->fx.parts[idx[k]];
        const FxDef *d = &g_defs[q->def];
        float u = 1.0f - q->life / q->life_max;
        float a = (d->alpha0 + (d->alpha1 - d->alpha0) * u) * 0.01f;

        /* Shift toward the death colour over the life. A spark that cools
           from white to orange as it falls, or blood that darkens, carries
           more of the event than a fade to nothing does -- the alpha ramp
           alone only says "this is ending", where the colour says what it was.
           수명에 걸쳐 소멸 색상으로 이동합니다. 떨어지며 흰색에서 주황으로 식는 불꽃이나
           어두워지는 피는, 단순히 사라지는 것보다 사건을 더 많이 전달합니다. 알파 변화만
           있으면 "끝나는 중"이라고만 말하지만, 색은 그것이 무엇이었는지를 말합니다.
           -1 means the definition set no death colour, so the tone is held. */
        float rr = d->r / 255.0f, gg = d->g / 255.0f, bb = d->b / 255.0f;
        if (d->r2 >= 0) {
            rr += (d->r2 / 255.0f - rr) * u;
            gg += (d->g2 / 255.0f - gg) * u;
            bb += (d->b2 / 255.0f - bb) * u;
        }
        /* Last, and after the birth-to-death ramp rather than instead of it: the
           tint is what this particular blast was made of, and the ramp is what
           any fire does while it cools. Doing it here rather than at spawn
           costs a dozen instructions on a particle that was already getting its
           own draw call, and buys a definition table that stays one entry per
           SHAPE instead of one per shape per colour. See ::fx_tint_colour.
           마지막이며, 생성-소멸 변화를 대신하는 것이 아니라 그 뒤에 옵니다. 색조는 *이*
           폭발이 무엇으로 이루어졌는지이고, 변화 곡선은 어떤 불이든 식으면서 겪는
           것입니다. 생성 시점이 아니라 이곳에서 하는 비용은 어차피 자기 드로우 호출을
           받던 입자에 명령 열두어 개이며, 그 대가로 정의 표가 색상별이 아니라 *형태*별로
           한 행씩 유지됩니다. ::fx_tint_colour를 참조하십시오. */
        FxTint tint = { q->tint[0], q->tint[1], q->tint[2] };
        float  col[3] = { rr, gg, bb };
        fx_tint_colour(tint, col);

        rd_color(col[0], col[1], col[2], a);
        glDrawArrays(GL_TRIANGLES, k * 6, 6);
    }
}

/* Beside ::fx_draw rather than beside the declarations at the top, because the
   mb_init it pairs with is inside fx_draw, and a pair that is read together is
   a pair that stays together.
   선언부가 아니라 ::fx_draw 곁입니다. 짝을 이루는 mb_init이 fx_draw 안에 있으며, 함께 읽히는
   짝이 함께 남는 짝이기 때문입니다. */
void fx_free(void) {
    if (!g_buf_ready) return;
    mb_free(&g_buf);
    g_buf_ready = 0;
}

void fx_draw(const Pools *pl, mat4 vp, v3 cam_right, v3 cam_up) {
    DIAG_WANT_WORLD_PASS();

    if (!g_buf_ready) {
        /* Sized for the worst case -- every particle alive at once -- so the
           buffer can never grow mid-frame and drop geometry.
           최악의 경우(모든 입자가 동시에 살아 있는 경우)를 기준으로 크기를 정하므로,
           버퍼가 프레임 도중에 확장되며 지오메트리를 잃는 일이 없습니다. */
        mb_init(&g_buf, FX_MAX_PARTICLES * 6);
        g_buf_ready = 1;
    }

    rd_mode(RD_FLAT);
    rd_mvp(vp);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);        /* particles do not occlude each other */
    glEnable(GL_BLEND);

    /* Alpha first, additive second: a glow belongs on top of the smoke it came
       from, and drawing them the other way round buries it.
       알파를 먼저, 가산을 나중에 그립니다. 발광은 자신이 나온 연기 위에 놓여야 하며,
       순서를 바꾸면 발광이 묻힙니다. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw_pass(pl, FX_BLEND_ALPHA, cam_right, cam_up);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    draw_pass(pl, FX_BLEND_ADD, cam_right, cam_up);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}
