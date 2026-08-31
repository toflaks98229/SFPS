/**
 * @file data.h
 * @brief 모델 및 재질 텍스트 데이터 소스를 관리합니다.
 *
 * assets/models.txt와 assets/textures.txt가 원본 데이터입니다.
 * bake.ps1 스크립트는 이 파일들의 주석과 공백을 제거하여 src/gen_assets.h 파일로
 * 만듭니다. 릴리스 빌드는 이 생성된 파일을 포함하므로, 배포되는 exe 파일은
 * 최소화된 텍스트를 내장하고 *에셋을 위해서는* 파일 시스템에 접근하지 않습니다.
 *
 * @note 이 문장은 한때 에셋에 대한 것이 아니라 바이너리 전체에 대한 것이었고, 더 이상
 *       그렇지 않습니다. src/save.c가 %APPDATA%에 해금 비트와 최고 웨이브를 씁니다. 그것이
 *       배포 빌드가 여는 유일한 파일이며, 이 모듈이 여는 파일은 여전히 하나도 없습니다.
 *       무엇이 바뀌었고 왜 바뀌었는지는 save.h를 참조하십시오.
 *       This sentence was once about the whole binary rather than about assets, and is not
 *       any more: src/save.c writes unlock bits and a best wave under %APPDATA%. That is the
 *       only file the shipped build opens, and this module still opens none. See save.h.
 *
 * HOT_RELOAD 빌드는 파일을 직접 읽고 변경 사항을 감시합니다. 이를 통해
 * 실루엣을 편집하면 재빌드 없이 실행 중인 게임에 변경 사항이 반영됩니다.
 * 이것이 바로 인게임 프리뷰 기능입니다: 실제 레벨, 실제 조명, 실제 안개, 실제 총기.
 */
#ifndef DATA_H
#define DATA_H

/**
 * @enum DataAsset
 * @brief 관리되는 데이터 에셋의 종류를 정의합니다.
 *
 * DATA_MESHES는 감시할 파일이 없습니다. 이 데이터는 .obj 파일로부터 bake.ps1에 의해
 * 생성되므로, 항상 빌드에 포함된 복사본에서만 가져옵니다. 메시를 편집하려면
 * Blender로 돌아가 재빌드해야 하며, 빌드 프로세스가 이를 처리합니다.
 */
enum DataAsset {
    DATA_MODELS,      /**< 모델 정의 (실루엣, 파트) */
    DATA_RECIPES,     /**< 텍스처 레시피 */
    DATA_SOUNDS,      /**< 사운드 합성 레시피 */
    DATA_MESHES,      /**< .obj에서 변환된 정점 데이터 */
    DATA_LEVELS,      /**< 레벨 레이아웃 및 엔티티 */
    DATA_SPRITES,     /**< PNG에서 변환된 팔레트 인덱스 스프라이트. DATA_MESHES와 마찬가지로 감시할 파일이 없습니다. */
    DATA_EFFECTS,     /**< 파티클 이펙트 레시피 (fx.c). 감시 대상 파일이 있습니다. */

    /**
     * @brief Note streams for the music, one track per `t` line.
     *
     * ENGLISH: Notes, not audio. Freedoom ships MIDI and this project has no
     * synthesiser to play one with, so the parsing happens at bake time and
     * what lands here is a flat list of times, pitches and durations that
     * music.c walks and audio.c's oscillators sound. See
     * assets/music/import-freedoom-music.py.
     *
     * 한국어: 오디오가 아니라 *음표*입니다. Freedoom은 MIDI를 제공하고 이 프로젝트에는
     * 그것을 연주할 신시사이저가 없으므로, 파싱은 베이크 시점에 일어나고 이곳에 도착하는
     * 것은 시각·음높이·길이의 평평한 목록입니다. music.c가 그것을 훑고 audio.c의
     * 오실레이터가 소리를 냅니다.
     */
    DATA_MUSIC,
    /**
     * @brief Every assets\maps\*.map packed into one blob. Read with ::data_map.
     *
     * ENGLISH: No single file behind it, like DATA_SPRITES and for the same
     * reason -- there are many source files, not one. Unlike those two, the
     * sources here are plain text a person edits, so ::data_map has its own
     * hot-reload path that reads the individual .map rather than this blob.
     *
     * 한국어: DATA_SPRITES와 마찬가지로, 그리고 같은 이유로 뒤에 파일 하나가 없습니다.
     * 원본 파일이 하나가 아니라 여럿이기 때문입니다. 다만 그 둘과 달리 이곳의 원본은
     * 사람이 편집하는 평문이므로, ::data_map은 이 블롭이 아니라 개별 .map을 읽는 자체
     * 핫 리로드 경로를 가집니다.
     */
    DATA_MAPS,

    /**
     * @brief Drop rates, the wave reward, and the glow a fresh item carries.
     *
     * ENGLISH: Behind assets\loot.txt and watched like the others, because it
     * is the one asset in this list whose whole reason for existing is being
     * retuned -- a drop rate is not authored once and shipped, it is played
     * against and moved. A rate that needs a rebuild to change is a rate
     * nobody changes. See loot.h.
     *
     * 한국어: assets\loot.txt가 뒤에 있으며 다른 것들처럼 감시됩니다. 이 목록에서 존재
     * 이유 자체가 *다시 조정되는 것*인 유일한 에셋이기 때문입니다. 드롭 확률은 한 번
     * 제작하고 배포하는 것이 아니라, 플레이해 보고 옮기는 것입니다. 바꾸는 데 재빌드가
     * 필요한 확률은 아무도 바꾸지 않는 확률입니다. loot.h를 참조하십시오.
     */
    DATA_LOOT,

    /**
     * @brief The intro, victory and defeat cutscenes.
     *
     * ENGLISH: Behind assets\story.txt and watched like the others, for
     * ::DATA_LOOT's reason applied to words instead of numbers: a line is the
     * thing most likely to be rewritten and least likely to be worth waiting
     * for a build, and a line that needs a rebuild to change is a line that
     * stays as first drafted. See story.h.
     *
     * 한국어: assets\story.txt가 뒤에 있으며 다른 것들처럼 감시됩니다. ::DATA_LOOT의 이유를
     * 숫자가 아니라 말에 적용한 것입니다. 대사는 가장 다시 쓰이기 쉬운 것이자 빌드를 기다릴
     * 가치가 가장 적은 것이며, 바꾸는 데 재빌드가 필요한 대사는 초고 그대로 남는 대사입니다.
     * story.h를 참조하십시오.
     */
    DATA_STORY,
    DATA_COUNT        /**< 총 데이터 에셋 수 */
};

/**
 * @brief 지정된 데이터 세트의 라이브 텍스트를 반환합니다.
 *
 * 절대 NULL을 반환하지 않습니다. 파일이 없거나 읽을 수 없는 경우, 빌드에 포함된
 * 복사본이 사용됩니다. 따라서 잘못된 경로는 빈 월드 대신 배포된 콘텐츠로
 * 안전하게 대체됩니다.
 * @param which 가져올 데이터 에셋의 종류 (DataAsset 열거형 값).
 * @return 데이터 텍스트를 담고 있는 const char 포인터.
 */
const char *data_text(int which);
/**
 * @brief The BUILT-IN text for an asset kind, ignoring any hot-reloaded file.
 *
 * ENGLISH
 * -------
 * @param[in] which One of the DATA_* kinds.
 * @return The string bake.ps1 embedded, never a file's contents.
 * @note Exists for data that cannot come from a file even in a dev build.
 *       The sampled sounds are the case: they are ADPCM produced from WAVs by
 *       the bake, so assets/sounds.txt has only the synthesised recipes and a
 *       hot-reload build that read only the file heard the recipes while the
 *       shipped build played the samples. Two builds that sound different is
 *       the kind of gap that gets found by shipping.
 *
 * 한국어
 * ------
 * @brief 핫 리로드된 파일을 무시하고 에셋 종류의 *내장* 텍스트를 반환합니다.
 * @param[in] which DATA_* 종류 중 하나.
 * @return bake.ps1이 삽입한 문자열이며 결코 파일의 내용이 아닙니다.
 * @note 개발 빌드에서도 파일에서 올 수 없는 데이터를 위해 존재합니다. 샘플 사운드가 그
 *       경우입니다. 베이크가 WAV로부터 만든 ADPCM이므로 assets/sounds.txt에는 합성
 *       레시피만 있고, 파일만 읽는 핫 리로드 빌드는 레시피를 듣는 반면 배포 빌드는
 *       샘플을 재생했습니다. 두 빌드가 다르게 들리는 것은 출시하고 나서야 발견되는
 *       종류의 틈입니다.
 */
const char *data_baked(int which);


/**
 * @brief Finds one named .map inside the packed maps blob.
 *
 * ENGLISH
 * -------
 * @param[in]  name    Level name, which is the .map's filename without the
 *                     extension: `assets\maps\lqdm4.map` is "lqdm4".
 * @param[out] out_len Receives the length in bytes. The text is NOT null
 *                     terminated -- the next map follows it -- so the length is
 *                     the only thing that says where this map ends. Pass it to
 *                     ::brush_parse rather than relying on a terminator.
 * @return A pointer to the map text, or NULL when no map of that name exists.
 *
 * @warning The returned pointer is valid until the NEXT call. A hot-reload
 *          build reads the file into one reusable buffer, so holding two maps
 *          at once is holding one map twice. Nothing needs to: a level is
 *          parsed into a ::BrushMap and the text is done with.
 *
 * @note THE NAME IS THE FILENAME, deliberately. TrenchBroom's unit of work is a
 *       file, so a level is a file and its name is what the author called it --
 *       there is no name recorded inside the map that could disagree with the
 *       one on disk. ::Level::name comes from a `l <name>` line in levels.txt
 *       and could; that is the shape being left behind.
 * @note A HOT_RELOAD build reads `assets\maps\<name>.map` straight off disk and
 *       falls back to the baked blob when there is no such file, which is what
 *       lets an author save in the editor and reload without a build. See
 *       ::data_poll.
 *
 * 한국어
 * ------
 * @brief 포장된 맵 블롭 안에서 이름이 주어진 .map 하나를 찾습니다.
 * @param[in]  name    레벨 이름이며 확장자를 뺀 .map 파일명입니다.
 *                     `assets\maps\lqdm4.map`은 "lqdm4"입니다.
 * @param[out] out_len 바이트 길이를 받습니다. 텍스트는 널로 끝나지 *않습니다*. 뒤에 다음
 *                     맵이 이어지므로 이 맵이 어디서 끝나는지를 말해 주는 것은 길이뿐입니다.
 *                     종료 문자에 기대지 말고 ::brush_parse에 그대로 넘기십시오.
 * @return 맵 텍스트를 가리키는 포인터. 그런 이름의 맵이 없으면 NULL.
 *
 * @warning 반환된 포인터는 *다음* 호출까지만 유효합니다. 핫 리로드 빌드는 파일을 재사용
 *          버퍼 하나에 읽으므로, 두 맵을 동시에 들고 있는 것은 한 맵을 두 번 들고 있는
 *          것입니다. 그럴 필요가 있는 것은 없습니다. 레벨은 ::BrushMap으로 파싱되고 텍스트는
 *          그것으로 끝입니다.
 *
 * @note 이름이 곧 파일명인 것은 의도적입니다. TrenchBroom의 작업 단위는 파일이므로 레벨은
 *       파일이고 그 이름은 제작자가 붙인 이름입니다. 맵 *안에* 기록되어 디스크의 이름과
 *       어긋날 수 있는 이름이 존재하지 않습니다. ::Level::name은 levels.txt의 `l <name>`
 *       줄에서 오며 어긋날 수 있습니다. 그것이 떠나보내는 형태입니다.
 * @note HOT_RELOAD 빌드는 `assets\maps\<name>.map`을 디스크에서 바로 읽고, 그런 파일이 없으면
 *       구워 넣은 블롭으로 되돌아갑니다. 그 덕분에 제작자가 에디터에서 저장하고 빌드 없이
 *       다시 불러올 수 있습니다. ::data_poll을 참조하십시오.
 */
const char *data_map(const char *name, int *out_len);

/**
 * @brief The BAKED text for one map, ignoring any file on disk.
 *
 * ENGLISH
 * -------
 * @param[in]  name    Level name, as ::data_map takes it.
 * @param[out] out_len Receives the length in bytes.
 * @return A pointer into the packed blob, or NULL when no map of that name was
 *         baked. Valid for the life of the process; unlike ::data_map's return
 *         there is no reusable buffer involved.
 *
 * THE SHIPPED PATH HAS NO OTHER WAY IN. build.ps1 compiles every tool with
 * HOT_RELOAD, so ::data_map in a tool reads assets\maps\<name>.map and the blob
 * scanner -- the code the release binary actually runs -- is never executed by
 * anything that can assert on it. That is the shape of bug that ships: it works
 * in every build a person looks at and fails in the only one anybody plays.
 *
 * It also makes the bake checkable. The blob is the file with its comments
 * stripped and its newlines flattened, which is a transformation with a right
 * answer: parsing both must give the same brushes, the same planes and the same
 * entities. A test that compares them catches a packing fault -- a length off
 * by one, an escape that did not survive -- at the point it is introduced
 * rather than at the point somebody loads the second level in a shipped game.
 *
 * 한국어
 * ------
 * @brief 디스크의 파일을 무시하고 맵 하나의 *구워 넣은* 텍스트를 반환합니다.
 * @param[in]  name    ::data_map이 받는 것과 같은 레벨 이름.
 * @param[out] out_len 바이트 길이를 받습니다.
 * @return 포장된 블롭 안을 가리키는 포인터. 그런 이름으로 구워진 맵이 없으면 NULL.
 *         프로세스가 살아 있는 동안 유효하며, ::data_map의 반환값과 달리 재사용 버퍼가
 *         개입하지 않습니다.
 *
 * 배포 경로에는 다른 입구가 없습니다. build.ps1은 모든 도구를 HOT_RELOAD로 컴파일하므로
 * 도구 안의 ::data_map은 assets\maps\<name>.map을 읽고, 블롭 스캐너(배포 바이너리가 실제로
 * 실행하는 코드)는 그것에 대해 단언할 수 있는 무엇에 의해서도 실행되지 않습니다. 그것이
 * 출하되는 버그의 형태입니다. 사람이 들여다보는 모든 빌드에서 동작하고, 정작 사람들이
 * 플레이하는 단 하나의 빌드에서 실패합니다.
 *
 * 베이크 자체도 검사 가능해집니다. 블롭은 주석을 걷어 내고 줄바꿈을 평탄화한 파일이며, 이는
 * 정답이 있는 변환입니다. 양쪽을 파싱하면 같은 브러시, 같은 평면, 같은 엔티티가 나와야
 * 합니다. 둘을 비교하는 테스트는 포장 결함(하나 어긋난 길이, 살아남지 못한 이스케이프)을
 * 도입되는 시점에 잡아냅니다. 누군가 배포된 게임에서 두 번째 레벨을 불러오는 시점이 아니라.
 */
const char *data_map_baked(const char *name, int *out_len);

/**
 * @brief 감시 중인 파일이 이전 호출 이후 변경되었는지 확인합니다.
 *
 * 변경된 경우 0이 아닌 값을 반환하여, 호출자가 텍스트에서 파생된 데이터를
 * 재빌드할 수 있도록 합니다. 릴리스 빌드에서는 텍스트가 변경될 수 없으므로
 * 항상 0을 반환합니다.
 * @return 파일 변경 시 1, 그렇지 않으면 0.
 */
int data_poll(void);

/**
 * @brief data_text()가 현재 빌드된 복사본 대신 파일에서 직접 데이터를 제공하는지 확인합니다.
 *
 * 이 함수는 안전 장치입니다. 파일 누락 시 자동으로 대체되는 것은 의도된 동작이지만,
 * 에디터의 경우 오래된 스냅샷을 편집하다가 사용자의 파일을 덮어쓸 위험이 있습니다.
 * 도구는 쓰기 전에 반드시 이 함수를 확인해야 합니다.
 * @param which 확인할 데이터 에셋의 종류 (DataAsset 열거형 값).
 * @return 파일에서 직접 제공하는 경우 1, 그렇지 않으면 0.
 */
int data_from_file(int which);

#endif
