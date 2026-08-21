#include "data.h"
#include <stdlib.h>   /* malloc/calloc/free: this file used to reach these through windows.h */
#include "txt.h"          /* txt_copy -- 핫 리로드 경로 조립 */
#include "inflate.h"      /* the baked arrays are deflated; see bake.ps1 */
#include "diag.h"
#include "gen_assets.h"   /* bake.ps1에 의해 에셋 디렉토리에서 생성됨 */

/**
 * @brief 지정된 에셋 종류에 대한 빌드 시 포함된(baked) 데이터 텍스트를 반환합니다.
 * @param which 가져올 데이터 에셋의 종류.
 * @return 빌드에 포함된 데이터 텍스트를 담고 있는 const char 포인터.
 */
/**
 * @struct Baked
 * @brief One baked asset: its deflated bytes, its expanded length, and the
 *        buffer it was expanded into.
 *
 * ENGLISH: The arrays in gen_assets.h are compressed -- see bake.ps1's
 * Compress-AssetArrays and inflate.h for why. Expanded lazily and kept, so an
 * asset nobody asks for costs only its compressed bytes and one asked for
 * twice is expanded once.
 *
 * 한국어: gen_assets.h의 배열들은 압축되어 있습니다. 이유는 bake.ps1의
 * Compress-AssetArrays와 inflate.h를 참조하십시오. 필요할 때 펼쳐 보관하므로, 아무도 찾지
 * 않는 에셋은 압축된 바이트만큼만 비용이 들고 두 번 요청된 것은 한 번만 펼쳐집니다.
 */
typedef struct {
    const unsigned char *lz;   /**< Deflated bytes. / 압축된 바이트. */
    int lz_len;                /**< How many. / 그 길이. */
    int raw_len;               /**< What it expands to, terminator excluded. / 펼쳐지는 길이. 종료 문자 제외. */
    char *text;                /**< The expansion, null until first asked for. / 펼친 결과. 요청 전에는 널. */
} Baked;

#define BAKED(n) { n##_LZ, (int)sizeof(n##_LZ), n##_RAW, 0 }

/* Indexed by ::DataAsset, so a row added to that enum is a row added here.
   ::DataAsset로 인덱싱되므로 그 열거형에 행이 추가되면 이곳에도 추가됩니다. */
static Baked g_baked[DATA_COUNT] = {
    BAKED(ASSET_MODELS), BAKED(ASSET_RECIPES), BAKED(ASSET_SOUNDS),
    BAKED(ASSET_MESHES), BAKED(ASSET_LEVELS),  BAKED(ASSET_SPRITES),
    BAKED(ASSET_EFFECTS), BAKED(ASSET_MUSIC), BAKED(ASSET_MAPS),
};

/* Same argument as FILENAMES below, one step earlier: a row missing here shifts
   every asset after it onto another asset's bytes, and the first symptom would
   be a level that inflates as a sprite sheet.
   아래 FILENAMES와 같은 논거를 한 단계 앞에서 적용합니다. 이곳에 행이 빠지면 그 뒤의 모든
   에셋이 다른 에셋의 바이트로 밀려나며, 첫 증상은 스프라이트 시트로 펼쳐지는 레벨입니다. */
_Static_assert(sizeof(g_baked) / sizeof(g_baked[0]) == DATA_COUNT,
               "g_baked needs exactly one entry per DataAsset");

/**
 * @brief The asset id, or a row that exists when the caller named one that
 *        does not.
 *
 * ENGLISH
 * -------
 * ONE PLACE DECIDES, so no accessor has to. Every function in this file
 * indexes a table of DATA_COUNT rows with the id it was handed -- ::g_baked
 * here, ::FILENAMES and ::g_slots in the hot-reload half below -- and each of
 * them was free to disagree about what an id outside the enum meant.
 * ::data_baked clamped and the two beside it did not, so the SHIPPED build was
 * bounded and the authoring build read past the end of a table for the same
 * call. A difference in memory safety between the two binaries is the one
 * difference this file exists to prevent.
 *
 * The single cast is what makes it one comparison: negative ids wrap to a
 * value above DATA_COUNT and are caught by the same test as ids that are too
 * large.
 *
 * @note WHICH row it lands on is not the point and is deliberately left where
 *       it was. An id outside the enum is a caller with a bug, not a data
 *       condition to recover from; the contract is only that the return value
 *       indexes something that exists.
 *
 * 한국어
 * ------
 * @brief 에셋 식별자, 또는 호출자가 존재하지 않는 것을 지목했을 때 존재하는 행.
 *
 * *한 곳이 결정하므로* 어떤 접근자도 결정하지 않아도 됩니다. 이 파일의 모든 함수가 건네받은
 * 식별자로 DATA_COUNT개짜리 표를 인덱싱합니다. 이곳의 ::g_baked, 아래 핫 리로드 절반의
 * ::FILENAMES와 ::g_slots입니다. 그리고 각자가 열거형 바깥의 식별자가 무엇을 뜻하는지에 대해
 * 서로 다른 말을 할 수 있었습니다. ::data_baked는 제한했고 그 곁의 둘은 그러지 않았으므로,
 * *배포* 빌드는 경계가 있고 제작 빌드는 같은 호출에 대해 표 끝을 넘어 읽었습니다. 두 바이너리
 * 사이의 메모리 안전성 차이는 이 파일이 막으려고 존재하는 바로 그 차이입니다.
 *
 * 캐스트 하나가 이것을 비교 하나로 만듭니다. 음수 식별자는 DATA_COUNT보다 큰 값으로
 * 순환하므로, 너무 큰 식별자와 같은 검사에 걸립니다.
 *
 * @note *어느* 행에 떨어지는지는 요점이 아니며 의도적으로 있던 자리에 둡니다. 열거형 바깥의
 *       식별자는 복구할 데이터 상황이 아니라 결함이 있는 호출자이며, 계약은 반환값이 존재하는
 *       무언가를 가리킨다는 것뿐입니다.
 */
static int row_or_default(int which) {
    return ((unsigned)which >= (unsigned)DATA_COUNT) ? DATA_LEVELS : which;
}

const char *data_baked(int which) {
    which = row_or_default(which);
    Baked *b = &g_baked[which];
    if (b->text) return b->text;

    /* One byte past the payload for the terminator every parser assumes:
       txt.h walks a null-terminated string and the compressed form carries no
       terminator of its own.
       모든 파서가 가정하는 종료 문자를 위해 페이로드보다 1바이트 큽니다. txt.h는 널로 끝나는
       문자열을 훑으며, 압축된 형태에는 종료 문자가 없습니다. */
    char *buf = malloc((size_t)b->raw_len + 1);
    if (!buf) return "";

    int n = inflate_raw((unsigned char *)buf, b->raw_len, b->lz, b->lz_len);
    if (n != b->raw_len) {
        /* The header and the binary disagree about this asset, which means
           they were built from different assets. An empty string is a level
           that will not load and a sheet that draws nothing -- loud, and far
           better than parsing a half-written buffer as if it were whole.
           헤더와 바이너리가 이 에셋에 대해 서로 다른 말을 하고 있으며, 둘이 서로 다른
           에셋으로 만들어졌다는 뜻입니다. 빈 문자열은 로드되지 않는 레벨이자 아무것도 그리지
           않는 시트입니다. 눈에 띄며, 절반만 기록된 버퍼를 온전한 것처럼 파싱하는 것보다
           훨씬 낫습니다. */
        free(buf);
        DIAG(DIAG_ASSET_INFLATE);
        return "";
    }
    buf[b->raw_len] = 0;
    b->text = buf;
    return buf;
}

/* ------------------------------------------------------- the maps blob ----
 *
 * ENGLISH
 * -------
 * `m <name> <bytes> <payload>`, repeated, with a space between records. bake.ps1
 * builds it; see the note there for why the .map files are packed rather than
 * shipped one asset each.
 *
 * SCANNED BY HAND rather than with txt.h. ::txt_skip treats `#` as a comment
 * running to the end of a line, and the bake has already flattened every
 * newline to a space -- so a single `#` anywhere in a map would swallow the
 * whole rest of the blob. A `#` is perfectly legal in a texture name or a
 * `message` value, and nothing about writing one looks like a mistake.
 *
 * The LENGTH is what separates one record from the next, not a delimiter. A
 * delimiter is a byte sequence a map could contain; a length cannot be
 * imitated by content.
 *
 * 한국어
 * ------
 * `m <name> <바이트 수> <내용>`이 반복되며 레코드 사이에 공백 하나가 있습니다. bake.ps1이
 * 만듭니다. .map 파일을 각각의 에셋으로 싣지 않고 포장하는 이유는 그곳의 설명을
 * 참조하십시오.
 *
 * txt.h가 아니라 직접 훑습니다. ::txt_skip은 `#`을 줄 끝까지 이어지는 주석으로 취급하는데,
 * 베이크는 이미 모든 줄바꿈을 공백으로 평탄화했습니다. 따라서 어느 맵에든 `#`이 하나 있으면
 * 블롭의 나머지 전체를 삼킵니다. `#`은 텍스처 이름이나 `message` 값에 완벽히 적법하며, 그것을
 * 쓰는 일의 무엇도 실수처럼 보이지 않습니다.
 *
 * 레코드를 나누는 것은 구분자가 아니라 *길이*입니다. 구분자는 맵이 담을 수 있는 바이트
 * 나열이지만, 길이는 내용이 흉내 낼 수 없습니다.
 */
static const char *map_in_blob(const char *name, int *out_len) {
    const char *p = data_baked(DATA_MAPS);
    /* raw_len is the authority on where the blob ends. An inflate that failed
       leaves `text` null and data_baked returns "", so this is 0 and the loop
       below stops immediately rather than walking off an empty string.
       블롭이 어디서 끝나는지에 대한 권위는 raw_len입니다. 펼치기에 실패하면 `text`가 널로
       남고 data_baked가 ""를 반환하므로 이 값은 0이 되며, 아래 루프는 빈 문자열 밖으로
       걸어 나가는 대신 즉시 멈춥니다. */
    const char *end = p + (g_baked[DATA_MAPS].text ? g_baked[DATA_MAPS].raw_len : 0);

    while (p < end) {
        while (p < end && *p == ' ') p++;
        if (end - p < 2 || p[0] != 'm' || p[1] != ' ') return 0;
        p += 2;
        while (p < end && *p == ' ') p++;

        const char *nm = p;
        while (p < end && *p != ' ') p++;
        int nlen = (int)(p - nm);
        while (p < end && *p == ' ') p++;

        if (p >= end || *p < '0' || *p > '9') return 0;
        int len = 0;
        while (p < end && *p >= '0' && *p <= '9') len = len * 10 + (*p++ - '0');

        /* Exactly one space, then the payload starts. Consuming a run here
           would shift the payload and make every length in the blob one too
           many; the bake writes one, so one is read.
           공백 정확히 하나 뒤에 내용이 시작됩니다. 이곳에서 연속된 공백을 소비하면 내용이
           밀려 블롭의 모든 길이가 하나씩 어긋납니다. 베이크가 하나를 쓰므로 하나를 읽습니다. */
        if (p >= end || *p != ' ') return 0;
        p++;
        if (len < 0 || len > (int)(end - p)) return 0;

        if (txt_is(nm, nlen, name)) { *out_len = len; return p; }
        p += len;
    }
    return 0;
}

/* Public on both paths, because the question it answers -- "what did the bake
   produce" -- is the same one in either build, and only a HOT_RELOAD build has
   anything to compare it against.
   양쪽 경로 모두에서 공개됩니다. 이 함수가 답하는 질문("베이크가 무엇을 만들었는가")은 어느
   빌드에서나 같고, 그것과 비교할 무언가를 가진 것은 HOT_RELOAD 빌드뿐이기 때문입니다. */
const char *data_map_baked(const char *name, int *out_len) {
    if (!name || !out_len) return 0;
    return map_in_blob(name, out_len);
}

#ifndef HOT_RELOAD
/*
 * 릴리스 빌드 구현:
 * 텍스트는 .rdata 섹션의 문자열 리터럴입니다. 파일 I/O, 파일 감시,
 * 또는 다른 사람의 컴퓨터에서 경로가 틀릴 위험이 없습니다.
 */

const char *data_text(int which) { return data_baked(which); }
int data_poll(void) { return 0; }
int data_from_file(int which) { (void)which; return 0; }

const char *data_map(const char *name, int *out_len) {
    if (!name || !out_len) return 0;
    return map_in_blob(name, out_len);
}

#else
/*
 * 핫 리로드 빌드 구현:
 * 에셋 파일을 직접 읽고 변경 사항을 감시하여 실시간으로 게임에 반영합니다.
 */

#include <stdio.h>    /* fopen/fread: reading a file is standard C, and was not */
#include "plat.h"     /* where the exe lives, and whether a file has changed */

/** @brief 파일 경로 버퍼의 용량.
 *
 * Win32의 MAX_PATH를 대신합니다. 그 매크로 하나 때문에 windows.h가 필요했고, 그것은 이
 * 파일이 자신이 어떤 OS 위에 있는지 알아야 할 이유가 되지 못합니다. 260이 아니라 512인
 * 것은, 이 값이 이제 Windows의 한계를 뜻하는 것이 아니라 이 프로그램이 경로에 쓰기로 한
 * 분량을 뜻하기 때문입니다. 핫 리로드 빌드에서만 존재하는 .bss입니다. */
#define PATH_CAP 512

/** @brief 핫 리로드를 위해 감시할 파일의 경로입니다. */
static const char *FILENAMES[DATA_COUNT] = {
    "assets\\models.txt",
    "assets\\textures.txt",
    "assets\\sounds.txt",
    0,                        /* 메시는 .obj 파일에서 구워지므로 파일 없음 */
    "assets\\levels.txt",
    0,                        /* 스프라이트는 .png에서 구워지므로 파일 없음 */
    "assets\\effects.txt",
    "assets\\music\\music.txt",
    /* 맵은 파일이 하나가 아니라 여럿이므로 이 표에 담을 수 없습니다. ::data_map이 이름으로
       경로를 만들어 직접 읽습니다.
       Maps are many files rather than one, so they cannot sit in this table.
       ::data_map builds the path from the name and reads it itself. */
    0
};

/* Indexed by DataAsset, so a missing entry would silently shift every path
   after it onto the wrong asset -- textures would be watched as sounds. The
   length is checked here rather than trusted to whoever adds the next one.
   DataAsset로 인덱싱되므로, 항목이 누락되면 그 뒤의 모든 경로가 조용히 다른 에셋으로
   밀려납니다. 텍스처가 사운드로 감시되는 식입니다. 다음에 항목을 추가하는 사람에게
   맡기지 않고 이곳에서 길이를 검사합니다. */
_Static_assert(sizeof(FILENAMES) / sizeof(FILENAMES[0]) == DATA_COUNT,
               "FILENAMES needs exactly one entry per DataAsset");

/**
 * @struct Slot
 * @brief 단일 핫 리로드 에셋의 상태를 관리합니다.
 */
typedef struct {
    char     path[PATH_CAP]; /**< 파일의 전체 경로. */
    char    *text;           /**< 힙에 복사된 파일 내용, 첫 로드 전까지 NULL. */
    int      len;            /**< 그 길이(바이트). 널 종료 문자 제외. */
    unsigned long long stamp; /**< 마지막으로 확인한 파일의 토큰. ::plat_file_stamp 참조. */
    int      resolved;       /**< 경로가 확인되었는지 여부. */
} Slot;

/** @brief 각 데이터 에셋에 대한 핫 리로드 슬롯입니다. */
static Slot g_slots[DATA_COUNT];

/**
 * @brief 상대 경로를 실행 파일의 부모 디렉토리를 기준으로 한 절대 경로로 변환합니다.
 *
 * 에셋은 소스 트리 옆에 있지만, exe는 build\에 빌드됩니다. 따라서 경로는
 * 실행 파일의 부모 디렉토리를 기준으로 확인해야 합니다. 이는 게임이 어떻게
 * 실행되었는지에 따라 달라지는 작업 디렉토리에 의존하지 않기 위함입니다.
 * @param s 경로를 저장할 Slot 구조체.
 * @param rel 상대 파일 경로.
 */
static void resolve(Slot *s, const char *rel) {
    char dir[PATH_CAP];
    int  n = plat_exe_dir(dir, sizeof(dir));

    /* Directory then relative path. txt_copy returns what it wrote, so the
       second call starts where the first stopped and the remaining capacity is
       whatever is left -- a concatenation that cannot overrun even if the exe
       path is long enough to fill the buffer on its own.
       디렉토리 다음에 상대 경로입니다. txt_copy가 기록한 길이를 반환하므로 두 번째
       호출은 첫 번째가 멈춘 곳에서 시작하고 남은 용량만큼만 씁니다. exe 경로만으로
       버퍼가 가득 차더라도 넘칠 수 없는 연결입니다. */
    int i = txt_copy(s->path, PATH_CAP, dir, n);
    txt_copy(s->path + i, PATH_CAP - i, rel, -1);
    s->resolved = 1;
}

/* stamp_of는 ::plat_file_stamp가 되었습니다. 이 파일이 알고 싶은 것은 "파일이 바뀌었는가"
   뿐이었으므로 함수도 그렇게만 답합니다. 타임스탬프의 *모양*은 기계마다 다르고 이 파일이
   신경 쓸 일이 아니었습니다. plat.h를 참조하십시오. */

/**
 * @brief 파일 전체를 새로 할당된 힙 버퍼로 읽어들입니다.
 *
 * 실패 시 0을 반환하고 이전 텍스트는 그대로 둡니다. 이는 에디터가 저장하는 동안
 * 일시적으로 파일을 읽을 수 없는 상태가 될 수 있기 때문입니다. 이 때 모델 데이터를
 * 버리는 것보다 이전 프레임의 데이터를 한 번 더 보여주는 것이 낫습니다.
 * @param s 리로드할 파일의 Slot 구조체.
 * @return 성공 시 1, 실패 시 0.
 */
static int reload(Slot *s) {
    /* "rb", and the b is not decoration: a .map is read by LENGTH, and text
       mode on Windows would silently collapse every CRLF and hand back fewer
       bytes than the file has.
       "rb"이며 b는 장식이 아닙니다. .map은 *길이*로 읽히고, Windows의 텍스트 모드는 모든
       CRLF를 조용히 접어 파일이 가진 것보다 적은 바이트를 돌려줍니다. */
    FILE *f = fopen(s->path, "rb");
    if (!f) return 0;

    /* Seek to the end and back rather than stat: the stream is already open,
       and a size taken from a separate call is a size that could have been
       measured on a different version of the file.
       stat 대신 끝까지 갔다가 돌아옵니다. 스트림은 이미 열려 있고, 별도 호출로 얻은 크기는
       파일의 다른 판본에서 잰 크기일 수 있습니다. */
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }

    size_t got = 0;
    char  *buf = malloc((size_t)size + 1);

    if (buf && (got = fread(buf, 1, (size_t)size, f)) == (size_t)size) {
        buf[got] = 0;
        if (s->text) free(s->text);
        s->text = buf;
        /* Recorded because a .map is read by LENGTH, not to a terminator: the
           baked blob packs the maps end to end and ::data_map must hand back
           the same shape from either source, or a hot-reload build would parse
           a different amount of text than the shipped one.
           .map은 종료 문자까지가 아니라 *길이*로 읽히므로 기록합니다. 구워 넣은 블롭은
           맵을 끝과 끝을 맞대어 포장하며, ::data_map은 어느 출처에서든 같은 형태를
           돌려주어야 합니다. 그러지 않으면 핫 리로드 빌드가 배포 빌드와 다른 분량의
           텍스트를 파싱하게 됩니다. */
        s->len = (int)got;
    } else {
        if (buf) free(buf);
        buf = 0;
    }
    fclose(f);
    return buf != 0;
}

const char *data_text(int which) {
    /* FIRST, because both tables below are indexed by it. This is the line the
       release build has had all along through ::data_baked; without it here,
       the two builds answered the same call differently -- one bounded, one
       reading whatever followed ::FILENAMES.
       두 표 모두 이 값으로 인덱싱되므로 가장 먼저입니다. 릴리스 빌드는 ::data_baked를 통해
       줄곧 이 줄을 가지고 있었습니다. 이곳에 없으면 두 빌드가 같은 호출에 다르게 답합니다.
       한쪽은 경계가 있고 한쪽은 ::FILENAMES 뒤에 오는 것을 읽습니다. */
    which = row_or_default(which);

    if (!FILENAMES[which]) return data_baked(which);

    Slot *s = &g_slots[which];
    if (!s->resolved) {
        resolve(s, FILENAMES[which]);
        if (reload(s)) s->stamp = plat_file_stamp(s->path);
    }
    return s->text ? s->text : data_baked(which);
}

int data_from_file(int which) {
    /* REJECTED rather than clamped, unlike ::data_text above, and the two
       differ because the questions do. "Give me this asset's text" has to
       return a string and so needs a row that exists. "Is this asset coming
       from a live file" has an honest answer for an id that names no asset,
       and it is no -- clamping would report on a DIFFERENT asset's state and
       call it this one's. It is also what the release build answers for every
       id, which is the shape of the truth here: nothing is live.
       위의 ::data_text와 달리 제한하지 않고 *거절*하며, 질문이 다르기 때문에 둘이
       다릅니다. "이 에셋의 텍스트를 달라"는 문자열을 돌려주어야 하므로 존재하는 행이
       필요합니다. "이 에셋이 살아 있는 파일에서 오고 있는가"는 어떤 에셋도 지목하지 않는
       식별자에 대해 정직한 답을 가지며, 그 답은 아니오입니다. 제한하면 *다른* 에셋의
       상태를 보고하면서 이것의 것이라고 부르게 됩니다. 릴리스 빌드가 모든 식별자에 대해
       내놓는 답이기도 하며, 이곳의 진실이 그 모양입니다. 살아 있는 것은 없습니다. */
    if ((unsigned)which >= (unsigned)DATA_COUNT) return 0;

    if (!FILENAMES[which]) return 0;
    Slot *s = &g_slots[which];
    if (!s->resolved) data_text(which);      /* 첫 로드 강제 */
    return s->text != 0;
}

/**
 * The one map most recently asked for, so ::data_poll can watch it.
 *
 * ENGLISH: One slot rather than a table, because that is the shape of the
 * question: the game has one level open, and the map an author is editing is
 * the one they just loaded. A cache of several would have to decide which to
 * watch and would keep pointers alive that ::data_map's contract says expire.
 *
 * 한국어: 표가 아니라 슬롯 하나인 이유는 질문의 형태가 그렇기 때문입니다. 게임은 레벨 하나를
 * 열어 두고 있고, 제작자가 편집 중인 맵은 방금 불러온 그 맵입니다. 여러 개를 담는 캐시는
 * 어느 것을 감시할지 정해야 하고, ::data_map의 계약이 만료된다고 말한 포인터를 살려 두게
 * 됩니다.
 */
static Slot g_map;
static int  g_map_watched;

const char *data_map(const char *name, int *out_len) {
    if (!name || !out_len) return 0;

    /* assets\maps\<name>.map. Built by appending into the remaining capacity
       each time, which is the concatenation ::resolve already uses -- a name
       long enough to fill the buffer truncates instead of overrunning.
       assets\maps\<name>.map입니다. 매번 남은 용량에 이어 붙이며, 이는 ::resolve가 이미
       쓰는 연결 방식입니다. 버퍼를 채울 만큼 긴 이름은 넘치지 않고 잘립니다. */
    char rel[PATH_CAP];
    int n = txt_copy(rel, (int)sizeof(rel), "assets\\maps\\", -1);
    n += txt_copy(rel + n, (int)sizeof(rel) - n, name, -1);
    txt_copy(rel + n, (int)sizeof(rel) - n, ".map", -1);

    g_map.resolved = 0;
    resolve(&g_map, rel);

    if (reload(&g_map)) {
        g_map.stamp = plat_file_stamp(g_map.path);
        g_map_watched = 1;
        *out_len = g_map.len;
        return g_map.text;
    }

    /* No such file. Falling back to the baked copy is the same promise
       ::data_text makes: a missing file gives the shipped content, never an
       empty world. Watching stops, because there is nothing to watch.
       그런 파일이 없습니다. 구워 넣은 사본으로 되돌아가는 것은 ::data_text가 하는 것과 같은
       약속입니다. 없는 파일은 빈 세계가 아니라 배포된 내용을 줍니다. 감시할 것이 없으므로
       감시는 멈춥니다. */
    g_map_watched = 0;
    return map_in_blob(name, out_len);
}

int data_poll(void) {
    int changed = 0;

    /* The open map, checked but NOT re-read. Re-reading here would free the
       buffer the last ::data_map call handed out while a caller may still be
       parsing it; reporting the change instead lets that caller ask again when
       it is ready to.
       열려 있는 맵은 검사하되 다시 읽지 *않습니다*. 이곳에서 다시 읽으면 마지막
       ::data_map 호출이 건네준 버퍼를, 호출자가 아직 파싱 중일 수 있는 동안 해제하게
       됩니다. 대신 변경 사실만 보고하면 그 호출자가 준비되었을 때 다시 물을 수 있습니다. */
    if (g_map_watched && g_map.resolved) {
        unsigned long long now = plat_file_stamp(g_map.path);
        if (now && now != g_map.stamp) {
            g_map.stamp = now;
            changed = 1;
        }
    }

    for (int i = 0; i < DATA_COUNT; i++) {
        if (!FILENAMES[i]) continue;
        Slot *s = &g_slots[i];
        if (!s->resolved) continue;

        unsigned long long now = plat_file_stamp(s->path);
        if (!now || now == s->stamp) continue;

        s->stamp = now;
        if (reload(s)) changed = 1;
    }
    return changed;
}

#endif
