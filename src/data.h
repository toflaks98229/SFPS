/**
 * @file data.h
 * @brief 모델 및 재질 텍스트 데이터 소스를 관리합니다.
 *
 * assets/models.txt와 assets/textures.txt가 원본 데이터입니다.
 * bake.ps1 스크립트는 이 파일들의 주석과 공백을 제거하여 src/gen_assets.h 파일로
 * 만듭니다. 릴리스 빌드는 이 생성된 파일을 포함하므로, 배포되는 exe 파일은
 * 최소화된 텍스트를 내장하고 파일 시스템에 접근하지 않습니다.
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
