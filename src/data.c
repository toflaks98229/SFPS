#include "data.h"
#include "txt.h"          /* txt_copy -- 핫 리로드 경로 조립 */
#include "inflate.h"      /* the baked arrays are deflated; see bake.ps1 */
#include "diag.h"
#include <windows.h>     /* HeapAlloc for the expanded text */
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
    BAKED(ASSET_EFFECTS),
};

const char *data_baked(int which) {
    if ((unsigned)which >= (unsigned)DATA_COUNT) which = DATA_LEVELS;
    Baked *b = &g_baked[which];
    if (b->text) return b->text;

    /* One byte past the payload for the terminator every parser assumes:
       txt.h walks a null-terminated string and the compressed form carries no
       terminator of its own.
       모든 파서가 가정하는 종료 문자를 위해 페이로드보다 1바이트 큽니다. txt.h는 널로 끝나는
       문자열을 훑으며, 압축된 형태에는 종료 문자가 없습니다. */
    char *buf = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)b->raw_len + 1);
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
        HeapFree(GetProcessHeap(), 0, buf);
        DIAG(DIAG_ASSET_INFLATE);
        return "";
    }
    buf[b->raw_len] = 0;
    b->text = buf;
    return buf;
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

#else
/*
 * 핫 리로드 빌드 구현:
 * 에셋 파일을 직접 읽고 변경 사항을 감시하여 실시간으로 게임에 반영합니다.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/** @brief 핫 리로드를 위해 감시할 파일의 경로입니다. */
static const char *FILENAMES[DATA_COUNT] = {
    "assets\\models.txt",
    "assets\\textures.txt",
    "assets\\sounds.txt",
    0,                        /* 메시는 .obj 파일에서 구워지므로 파일 없음 */
    "assets\\levels.txt",
    0,                        /* 스프라이트는 .png에서 구워지므로 파일 없음 */
    "assets\\effects.txt"
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
    char     path[MAX_PATH]; /**< 파일의 전체 경로. */
    char    *text;           /**< 힙에 복사된 파일 내용, 첫 로드 전까지 NULL. */
    FILETIME stamp;          /**< 마지막으로 확인한 파일의 타임스탬프. */
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
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(0, exe, MAX_PATH);
    while (n > 0 && exe[n - 1] != '\\') n--;          /* 파일 이름 제거 */
    if (n > 1) n--;                                    /* 마지막 '\' 제거 */
    while (n > 0 && exe[n - 1] != '\\') n--;          /* build\ 디렉토리 제거 */

    /* Directory then relative path. txt_copy returns what it wrote, so the
       second call starts where the first stopped and the remaining capacity is
       whatever is left -- a concatenation that cannot overrun even if the exe
       path is long enough to fill the buffer on its own.
       디렉토리 다음에 상대 경로입니다. txt_copy가 기록한 길이를 반환하므로 두 번째
       호출은 첫 번째가 멈춘 곳에서 시작하고 남은 용량만큼만 씁니다. exe 경로만으로
       버퍼가 가득 차더라도 넘칠 수 없는 연결입니다. */
    int i = txt_copy(s->path, MAX_PATH, exe, (int)n);
    txt_copy(s->path + i, MAX_PATH - i, rel, -1);
    s->resolved = 1;
}

/**
 * @brief 파일의 마지막 쓰기 타임스탬프를 가져옵니다.
 * @param path 파일 경로.
 * @param out 타임스탬프를 저장할 FILETIME 구조체 포인터.
 * @return 성공 시 1, 실패 시 0.
 */
static int stamp_of(const char *path, FILETIME *out) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) return 0;
    *out = fad.ftLastWriteTime;
    return 1;
}

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
    HANDLE f = CreateFileA(s->path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE_VALUE) return 0;

    DWORD size = GetFileSize(f, 0), got = 0;
    char *buf = 0;
    if (size != INVALID_FILE_SIZE)
        buf = HeapAlloc(GetProcessHeap(), 0, size + 1);

    if (buf && ReadFile(f, buf, size, &got, 0)) {
        buf[got] = 0;
        if (s->text) HeapFree(GetProcessHeap(), 0, s->text);
        s->text = buf;
    } else {
        if (buf) HeapFree(GetProcessHeap(), 0, buf);
        buf = 0;
    }
    CloseHandle(f);
    return buf != 0;
}

const char *data_text(int which) {
    if (!FILENAMES[which]) return data_baked(which);

    Slot *s = &g_slots[which];
    if (!s->resolved) {
        resolve(s, FILENAMES[which]);
        if (reload(s)) stamp_of(s->path, &s->stamp);
    }
    return s->text ? s->text : data_baked(which);
}

int data_from_file(int which) {
    if (!FILENAMES[which]) return 0;
    Slot *s = &g_slots[which];
    if (!s->resolved) data_text(which);      /* 첫 로드 강제 */
    return s->text != 0;
}

int data_poll(void) {
    int changed = 0;
    for (int i = 0; i < DATA_COUNT; i++) {
        if (!FILENAMES[i]) continue;
        Slot *s = &g_slots[i];
        if (!s->resolved) continue;

        FILETIME now;
        if (!stamp_of(s->path, &now)) continue;
        if (now.dwLowDateTime == s->stamp.dwLowDateTime &&
            now.dwHighDateTime == s->stamp.dwHighDateTime) continue;

        s->stamp = now;
        if (reload(s)) changed = 1;
    }
    return changed;
}

#endif
