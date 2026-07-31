#include "data.h"
#include "gen_assets.h"   /* bake.ps1에 의해 에셋 디렉토리에서 생성됨 */

/**
 * @brief 지정된 에셋 종류에 대한 빌드 시 포함된(baked) 데이터 텍스트를 반환합니다.
 * @param which 가져올 데이터 에셋의 종류.
 * @return 빌드에 포함된 데이터 텍스트를 담고 있는 const char 포인터.
 */
static const char *baked(int which) {
    if (which == DATA_MODELS)  return ASSET_MODELS;
    if (which == DATA_RECIPES) return ASSET_RECIPES;
    if (which == DATA_SOUNDS)  return ASSET_SOUNDS;
    if (which == DATA_MESHES)  return ASSET_MESHES;
    if (which == DATA_SPRITES) return ASSET_SPRITES;
    return ASSET_LEVELS;
}

#ifndef HOT_RELOAD
/*
 * 릴리스 빌드 구현:
 * 텍스트는 .rdata 섹션의 문자열 리터럴입니다. 파일 I/O, 파일 감시,
 * 또는 다른 사람의 컴퓨터에서 경로가 틀릴 위험이 없습니다.
 */

const char *data_text(int which) { return baked(which); }
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
    0                         /* 스프라이트는 .png에서 구워지므로 파일 없음 */
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

    int i = 0;
    for (; i < (int)n && i < MAX_PATH - 1; i++) s->path[i] = exe[i];
    for (int k = 0; rel[k] && i < MAX_PATH - 1; k++)  s->path[i++] = rel[k];
    s->path[i] = 0;
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
    if (!FILENAMES[which]) return baked(which);

    Slot *s = &g_slots[which];
    if (!s->resolved) {
        resolve(s, FILENAMES[which]);
        if (reload(s)) stamp_of(s->path, &s->stamp);
    }
    return s->text ? s->text : baked(which);
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
