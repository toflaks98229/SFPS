/**
 * @file diag.h
 * @brief Capacity-overflow reporting: makes silent truncation visible in dev builds.
 *
 * ENGLISH
 * -------
 * Several subsystems here have fixed capacities and drop the surplus rather
 * than failing: a MeshBuf stops appending vertices, a mesh parser stops
 * storing points, a level stops spawning monsters. That is the right
 * behaviour for a size-bound game -- refusing to draw is worse than drawing
 * slightly less -- but it is invisible, and invisible truncation has already
 * cost this project real debugging time. `LVL_MAX_RANGES` was sized for a
 * weapon's handful of parts, a level reached six materials, and the surplus
 * walls simply stopped being drawn; it read as a hole in the geometry and
 * took a headless check to find.
 *
 * This module is the counter to that. A subsystem reports an overflow once
 * per counter, the count accumulates, and the debug HUD shows it. Nothing
 * changes about the truncation itself -- the game still degrades gracefully;
 * it just stops doing so in silence.
 *
 * @note COMPILED OUT ENTIRELY in release. Every macro becomes `((void)0)` and
 *       every function disappears, so the shipped binary carries no counters,
 *       no strings and no calls. Report sites may therefore be placed freely
 *       in hot paths.
 * @warning Not thread-safe, and deliberately so: it is a dev aid, and a lock
 *          around a counter would cost more than the counter. Only the game
 *          thread may report. The audio mixer thread must NOT.
 *
 * 한국어
 * ------
 * 이 프로젝트의 여러 서브시스템은 고정된 용량을 가지며, 초과분에 대해 실패를
 * 반환하는 대신 조용히 버립니다. MeshBuf는 정점 추가를 멈추고, 메시 파서는 점 저장을
 * 멈추며, 레벨은 몬스터 생성을 멈춥니다. 크기가 제한된 게임에서는 이것이 올바른
 * 동작입니다. 아예 그리지 않는 것보다 조금 덜 그리는 편이 낫기 때문입니다. 그러나 이는
 * 눈에 보이지 않으며, 보이지 않는 절단은 이미 이 프로젝트에서 실제 디버깅 시간을
 * 소모시켰습니다. `LVL_MAX_RANGES`는 무기의 몇 안 되는 부품을 기준으로 정해졌는데
 * 레벨이 재질 6개에 도달했고, 초과된 벽은 그냥 그려지지 않았습니다. 이는 지오메트리에
 * 뚫린 구멍처럼 보였고 헤드리스 검사를 거쳐서야 발견되었습니다.
 *
 * 이 모듈은 그에 대한 대응책입니다. 서브시스템이 카운터별로 초과를 보고하면 그 횟수가
 * 누적되고, 디버그 HUD가 이를 표시합니다. 절단 동작 자체는 달라지지 않습니다. 게임은
 * 여전히 우아하게 성능을 낮추며, 다만 그것을 조용히 하지 않게 될 뿐입니다.
 *
 * @note 릴리스에서는 완전히 컴파일 아웃됩니다. 모든 매크로가 `((void)0)`이 되고 모든
 *       함수가 사라지므로, 배포되는 바이너리에는 카운터도 문자열도 호출도 남지
 *       않습니다. 따라서 보고 지점을 성능이 중요한 경로에 자유롭게 배치해도 됩니다.
 * @warning 스레드 안전하지 않으며, 이는 의도된 것입니다. 개발 보조 도구이며, 카운터에
 *          락을 거는 비용이 카운터 자체보다 크기 때문입니다. 게임 스레드만 보고할 수
 *          있습니다. 오디오 믹서 스레드는 보고해서는 안 됩니다.
 */
#ifndef DIAG_H
#define DIAG_H

/**
 * @def DIAG_ENABLED
 * @brief Whether the counters exist at all.
 *
 * ENGLISH
 * -------
 * On in dev builds (DEBUG_HUD) and in every tool (HOT_RELOAD, which build.ps1
 * makes mandatory for tools). Off in release, where the entire module
 * compiles away.
 *
 * Tools are included deliberately rather than incidentally: a headless check
 * that loads a level is exactly the place a capacity overflow should be
 * reportable, and it is the only place it can be asserted on. Gating solely
 * on DEBUG_HUD would leave the counters unbuildable from the test suite that
 * is supposed to verify them.
 *
 * 한국어
 * ------
 * 개발 빌드(DEBUG_HUD)와 모든 도구(build.ps1이 도구에 필수로 지정하는
 * HOT_RELOAD)에서 활성화됩니다. 릴리스에서는 비활성화되며 모듈 전체가 컴파일에서
 * 사라집니다.
 *
 * 도구를 포함시킨 것은 우연이 아니라 의도입니다. 레벨을 로드하는 헤드리스 검사야말로
 * 용량 초과를 보고해야 할 지점이며, 그것을 단언할 수 있는 유일한 장소이기 때문입니다.
 * DEBUG_HUD만으로 제한하면, 정작 이 카운터를 검증해야 할 테스트 스위트에서 카운터를
 * 빌드할 수 없게 됩니다.
 */
#if defined(DEBUG_HUD) || defined(HOT_RELOAD)
#define DIAG_ENABLED 1
#endif

#ifdef DIAG_ENABLED

/* --- Enumerations / 열거형 --- */

/**
 * @brief The capacity limits worth reporting on.
 *
 * ENGLISH
 * -------
 * One counter per limit that can silently drop data. Keep this list in step
 * with the DIAG_NAMES table in diag.c -- ::diag_name indexes straight into it.
 *
 * 한국어
 * ------
 * 데이터를 조용히 버릴 수 있는 각 한계마다 하나의 카운터를 둡니다. 이 목록은 diag.c의
 * DIAG_NAMES 테이블과 동기화되어야 합니다. ::diag_name이 이 값을 인덱스로 직접
 * 사용하기 때문입니다.
 */
typedef enum {
    DIAG_VERTEX_BUF,    /**< MeshBuf full: vertices dropped. / MeshBuf 초과. 정점이 버려졌습니다. */
    DIAG_MESH_POINTS,   /**< Authored mesh exceeded the scratch tables. / 제작된 메시가 임시 테이블을 초과했습니다. */
    DIAG_MODEL_POINTS,  /**< Model silhouette exceeded MB_MAX_SILHOUETTE. / 모델 실루엣이 MB_MAX_SILHOUETTE를 초과했습니다. */
    DIAG_MAT_RANGES,    /**< Material runs exceeded the range table; runs merged. / 재질 구간이 테이블을 초과하여 병합되었습니다. */
    DIAG_TEX_OPS,       /**< Texture recipe exceeded MAX_OPS. / 텍스처 레시피가 MAX_OPS를 초과했습니다. */
    DIAG_ENEMY_CAP,     /**< Level wanted more monsters than ENEMY_MAX. / 레벨이 ENEMY_MAX보다 많은 몬스터를 요구했습니다. */
    DIAG_PICKUP_CAP,    /**< Level wanted more pickups than PICKUP_MAX. / 레벨이 PICKUP_MAX보다 많은 아이템을 요구했습니다. */
    DIAG_SOUND_CAP,     /**< Recipe text exceeded MAX_SOUNDS or MAX_LAYERS. / 레시피 텍스트가 MAX_SOUNDS 또는 MAX_LAYERS를 초과했습니다. */
    DIAG_COUNT          /**< Number of counters. / 카운터의 개수. */
} DiagKind;

/* --- Public function prototypes / 공개 함수 프로토타입 --- */

/**
 * @brief Records one capacity overflow.
 *
 * ENGLISH
 * -------
 * @param[in] kind Which limit was hit.
 * @note Cheap by design -- an increment and a bounds check -- so it may be
 *       called from inside a per-vertex loop without distorting a profile.
 * @warning Game thread only. See the file-level warning.
 *
 * 한국어
 * ------
 * @brief 용량 초과 1건을 기록합니다.
 * @param[in] kind 초과된 한계의 종류.
 * @note 증가 연산과 범위 검사뿐이므로 의도적으로 저렴합니다. 정점 단위 루프 안에서
 *       호출해도 프로파일을 왜곡하지 않습니다.
 * @warning 게임 스레드 전용입니다. 파일 수준의 경고를 참조하십시오.
 */
void diag_report(DiagKind kind);

/**
 * @brief Returns how many times a limit has been hit.
 *
 * ENGLISH
 * -------
 * @param[in] kind Counter to read.
 * @return The accumulated count, or 0 for an out-of-range `kind`.
 *
 * 한국어
 * ------
 * @brief 특정 한계가 몇 번 초과되었는지 반환합니다.
 * @param[in] kind 읽을 카운터.
 * @return 누적된 횟수. `kind`가 범위를 벗어나면 0입니다.
 */
int diag_count(DiagKind kind);

/**
 * @brief Returns the short name of a counter, for display.
 *
 * ENGLISH
 * -------
 * @param[in] kind Counter to name.
 * @return A static, null-terminated label. Never NULL; an out-of-range `kind`
 *         yields "?" so a display path cannot crash on a bad index.
 * @warning The returned pointer is to static storage and must not be freed.
 *
 * 한국어
 * ------
 * @brief 표시용으로 카운터의 짧은 이름을 반환합니다.
 * @param[in] kind 이름을 얻을 카운터.
 * @return 정적이며 널로 끝나는 레이블. 절대 NULL이 아닙니다. `kind`가 범위를 벗어나면
 *         "?"를 반환하므로, 잘못된 인덱스로 표시 경로가 중단되지 않습니다.
 * @warning 반환된 포인터는 정적 저장 공간을 가리키므로 해제해서는 안 됩니다.
 */
const char *diag_name(DiagKind kind);

/**
 * @brief Summarises every non-zero counter into a caller-supplied buffer.
 *
 * ENGLISH
 * -------
 * @param[out] out Destination buffer; always null-terminated on return.
 * @param[in]  cap Capacity of `out` in bytes. Must be at least 1.
 * @return 1 when at least one counter was non-zero, 0 when all were clear.
 * @note Writes nothing but the terminator when everything is clear, so the
 *       caller can append it to a HUD string unconditionally.
 * @note Truncates rather than overflowing if `cap` is too small -- which
 *       would be its own silent truncation, so the format is kept terse
 *       enough that the HUD's buffer cannot realistically be exceeded.
 *
 * 한국어
 * ------
 * @brief 0이 아닌 모든 카운터를 호출자가 제공한 버퍼에 요약합니다.
 * @param[out] out 대상 버퍼. 반환 시 항상 널로 종료됩니다.
 * @param[in]  cap `out`의 용량 (바이트). 최소 1 이상이어야 합니다.
 * @return 0이 아닌 카운터가 하나라도 있으면 1, 모두 0이면 0.
 * @note 모든 카운터가 0이면 종료 문자만 기록하므로, 호출자가 조건 없이 HUD 문자열에
 *       덧붙일 수 있습니다.
 * @note `cap`이 너무 작으면 넘치지 않고 잘라 냅니다. 그것 자체가 또 다른 조용한
 *       절단이 되므로, HUD 버퍼를 현실적으로 초과할 수 없을 만큼 형식을 간결하게
 *       유지합니다.
 */
int diag_summary(char *out, int cap);

/**
 * @brief Reports one overflow. Compiles to nothing in release.
 *
 * ENGLISH
 * -------
 * Use this rather than calling ::diag_report directly, so report sites need
 * no `#ifdef` of their own.
 *
 * 한국어
 * ------
 * ::diag_report를 직접 호출하는 대신 이 매크로를 사용하십시오. 그래야 보고 지점마다
 * 별도의 `#ifdef`가 필요 없습니다.
 */
#define DIAG(kind) diag_report(kind)

#else  /* release: every trace of this module disappears */

/* The cast to void keeps `DIAG(x);` a valid statement and silences any
   unused-value warning, while emitting no code. The argument is NOT
   evaluated, so a report site may reference a DiagKind constant that does not
   exist in this build.
   void 캐스트는 `DIAG(x);`를 유효한 문장으로 유지하고 미사용 값 경고를 억제하면서도
   코드를 전혀 생성하지 않습니다. 인자는 평가되지 않으므로, 보고 지점이 해당 빌드에
   존재하지 않는 DiagKind 상수를 참조해도 무방합니다. */
#define DIAG(kind) ((void)0)

#endif /* DIAG_ENABLED */

#endif /* DIAG_H */
