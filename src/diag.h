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
    DIAG_TEX_CACHE,     /**< Material cache full; the material was rebuilt per call. / 재질 캐시가 가득 참. 해당 재질이 호출마다 재생성되었습니다. */
    DIAG_FX_CAP,        /**< Effect defs or particles exceeded their pool. / 이펙트 정의 또는 입자가 풀을 초과했습니다. */
    DIAG_LIGHT_CAP,     /**< Level declared more lights than LVL_MAX_LIGHTS. / 레벨이 LVL_MAX_LIGHTS보다 많은 광원을 선언했습니다. */
    DIAG_ENEMY_CAP,     /**< Level wanted more monsters than ENEMY_MAX. / 레벨이 ENEMY_MAX보다 많은 몬스터를 요구했습니다. */
    DIAG_PICKUP_CAP,    /**< Level wanted more pickups than PICKUP_MAX. / 레벨이 PICKUP_MAX보다 많은 아이템을 요구했습니다. */
    DIAG_SHOT_CAP,      /**< A monster wanted to fire with ENEMY_MAX_SHOTS already in flight. / ENEMY_MAX_SHOTS가 이미 비행 중인 상태에서 몬스터가 발사를 시도했습니다. */
    DIAG_SOUND_CAP,     /**< Recipe text exceeded MAX_SOUNDS or MAX_LAYERS. / 레시피 텍스트가 MAX_SOUNDS 또는 MAX_LAYERS를 초과했습니다. */
    DIAG_DOOR_CAP,      /**< Level declared more doors than LVL_MAX_DOORS. / 레벨이 LVL_MAX_DOORS보다 많은 문을 선언했습니다. */
    DIAG_ASSET_INFLATE, /**< A baked asset did not expand to its recorded length. / 구워 넣은 에셋이 기록된 길이로 펼쳐지지 않았습니다. */
    /**
     * @brief A door's saved shape belongs to a sector its definition no longer
     *        names -- ::door_reset was not called for the level being stepped.
     *
     * ENGLISH: ::Level::door_run holds one runtime state per door, matched to
     * ::Level::doors by index. They are brought into agreement by ::door_reset.
     * When they disagree, ::door_update would write one sector's geometry out
     * of another sector's snapshot: a wall in the wrong place, moving. This
     * counter is raised instead, and the door is left alone.
     *
     * WHAT THIS NO LONGER CATCHES, because it can no longer happen: the state
     * used to be a file-scope array in door.c, so a SECOND Level being stepped
     * arrived carrying the first one's doors. Moving it into ::Level made that
     * unconstructable -- ::door_update takes one level and reads that level's
     * own state. What is left is the case in the summary: a level loaded and
     * stepped without ::door_reset between, which ::level_load turns into a
     * count of 0 against a level full of definitions.
     *
     * 한국어: 문의 저장된 형상이, 그 정의가 더 이상 지목하지 않는 섹터의 것입니다.
     * 진행 중인 레벨에 대해 ::door_reset이 호출되지 않았습니다.
     *
     * ::Level::door_run이 문마다 하나의 런타임 상태를 담으며 ::Level::doors와 인덱스로
     * 대응합니다. 둘을 일치시키는 것은 ::door_reset입니다. 어긋나면 ::door_update가 한 섹터의
     * 지오메트리를 *다른* 섹터의 스냅숏으로부터 씁니다. 엉뚱한 자리에서 움직이는 벽입니다.
     * 대신 이 카운터를 올리고 그 문은 건드리지 않습니다.
     *
     * 이제 더 이상 잡지 않는 경우이며, 그것은 더 이상 일어날 수 없기 때문입니다. 상태는 예전에
     * door.c의 파일 스코프 배열이었으므로, 진행 중인 *두 번째* Level이 첫 번째의 문을 들고
     * 도착했습니다. 그것을 ::Level 안으로 옮겨 구성 불가능하게 만들었습니다. ::door_update는
     * 레벨 하나를 받고 그 레벨 자신의 상태를 읽습니다. 남은 것은 요약에 적힌 경우입니다. 사이에
     * ::door_reset 없이 로드되고 진행된 레벨이며, ::level_load가 그것을 정의로 가득 찬 레벨에
     * 대한 개수 0으로 만듭니다.
     */
    DIAG_DOOR_STALE,
    /**
     * @brief A monster type's archetype and its stats disagree.
     *
     * ENGLISH: A ::MON_CASTER row with no `shot_speed` stands in its preferred
     * band and never fires; a ::MON_BRAWLER row that carries one has a stat
     * nothing reads. Neither crashes and neither looks like an error -- the
     * monster simply behaves like a worse version of itself, which reads as
     * tuning rather than as a table row that is wrong.
     *
     * 한국어: `shot_speed`가 없는 ::MON_CASTER 행은 선호 대역에 서서 결코 발사하지
     * 않고, 그것을 가진 ::MON_BRAWLER 행은 아무도 읽지 않는 수치를 지닙니다. 어느 쪽도
     * 죽지 않고 어느 쪽도 오류처럼 보이지 않습니다. 그 몬스터는 그저 자기 자신의 못한
     * 판본처럼 행동하며, 그것은 잘못된 표의 행이 아니라 수치 조정처럼 읽힙니다.
     */
    DIAG_MON_TABLE,
    DIAG_PASS_ORDER,    /**< A draw was made on the wrong side of the world/UI pass boundary. / 월드/UI 패스 경계의 잘못된 쪽에서 그리기가 수행되었습니다. */

    /**
     * @brief The baked-light cache filled up; the surplus vertices re-trace.
     *
     * ENGLISH: Costs frame time rather than correctness -- a vertex that
     * cannot be cached is traced against the level exactly as it was before
     * the cache existed, so the picture is identical and the door that moved
     * is merely as expensive as it used to be. It is worth counting because
     * that is a cost with no symptom: the level looks right and the frame is
     * slow, which is the hardest pair to connect by eye.
     *
     * 한국어: 정확성이 아니라 프레임 시간의 문제입니다. 캐시에 넣지 못한 정점은 캐시가
     * 없던 시절과 똑같이 레벨에 대해 판정되므로 화면은 동일하고, 움직인 문이 예전만큼
     * 비싸질 뿐입니다. 세어 둘 가치가 있는 이유는 이것이 *증상이 없는* 비용이기
     * 때문입니다. 레벨은 멀쩡해 보이는데 프레임만 느리며, 눈으로 이어 붙이기 가장 어려운
     * 조합입니다.
     */
    DIAG_LIGHT_CACHE,

    /**
     * @brief A .map asked brush.h's tables for more than they hold: brushes,
     *        faces, or a texture name longer than ::BR_TEX.
     *
     * ENGLISH: A dropped brush is a hole in the world and nothing else -- the
     * room around it still draws, the player still walks up to where the wall
     * was, and walks through. That is the same class of silent fault as a
     * truncated sector, and it is worth a counter for the same reason: the
     * symptom points at the collision code, and the cause is a number in
     * brush.h.
     *
     * One counter for both caps rather than two, because they fail together in
     * practice -- a map too big for the brush table is too big for the face
     * pool -- and because the fix is the same edit either way.
     *
     * 한국어: 버려진 브러시는 월드의 구멍일 뿐 그 이상이 아닙니다. 주변의 방은 여전히
     * 그려지고, 플레이어는 벽이 있던 자리까지 걸어가 그대로 통과합니다. 잘려 나간 섹터와
     * 같은 부류의 조용한 결함이며, 같은 이유로 카운터를 둘 가치가 있습니다. 증상은 충돌
     * 코드를 가리키지만 원인은 brush.h의 숫자입니다.
     *
     * 두 상한에 카운터를 둘이 아니라 하나만 두는 이유는, 실제로 둘이 함께 실패하기
     * 때문입니다. 브러시 표에 비해 큰 맵은 면 풀에 비해서도 큽니다. 그리고 어느 쪽이든
     * 고치는 방법이 같은 수정이기 때문입니다.
     */
    DIAG_BRUSH_CAP,

    /**
     * @brief A .map declared more entities or entity keys than ::BR_MAX_ENTS
     *        and ::BR_MAX_KEYS hold; the surplus was dropped.
     *
     * ENGLISH: Separate from ::DIAG_BRUSH_CAP because the symptom is different
     * and so is the map that causes it. Too many brushes is a level that is too
     * detailed; too many entities is a level that is too populated, and the
     * result is a missing monster or a door that never opens because the key
     * naming its target did not fit. Reading one counter and finding the other
     * cap was the real one would send the reader to the wrong number.
     *
     * 한국어: ::DIAG_BRUSH_CAP과 분리하는 이유는 증상이 다르고 그것을 유발하는 맵도 다르기
     * 때문입니다. 브러시가 너무 많은 것은 지나치게 정교한 레벨이고, 엔티티가 너무 많은 것은
     * 지나치게 붐비는 레벨입니다. 그 결과는 사라진 몬스터이거나, 대상을 지목하는 키가 들어가지
     * 못해 결코 열리지 않는 문입니다. 한쪽 카운터를 읽고서 실제 원인이 다른 쪽 상한이었음을
     * 알게 되는 일은 읽는 사람을 엉뚱한 숫자로 보냅니다.
     */
    DIAG_MAPENT_CAP,

    /**
     * @brief A brush's planes do not close a volume; the unbounded faces were
     *        dropped.
     *
     * ENGLISH: Not a capacity overflow but an authoring error, and it is here
     * because it has the same shape: something is missing from the world and
     * nothing on screen says why. ::brush_face_poly clips a face's plane
     * against the brush's other planes, and on a closed brush what remains is
     * the face. On an open one -- five planes where six were needed, or a
     * brush dragged inside out -- what remains is the starting quad, kilometres
     * across. Drawing that would fill the screen with one surface; the face is
     * dropped instead and counted here.
     *
     * 한국어: 용량 초과가 아니라 제작 오류이며, 형태가 같기 때문에 이곳에 있습니다. 월드에서
     * 무언가가 사라졌는데 화면의 무엇도 그 이유를 말하지 않습니다. ::brush_face_poly는 면의
     * 평면을 브러시의 다른 평면들로 잘라 내며, 닫힌 브러시에서 남는 것이 곧 그 면입니다.
     * 열린 브러시에서는(여섯이 필요한 자리에 평면 다섯 개이거나, 안팎이 뒤집히도록 끌린
     * 브러시) 남는 것이 수 킬로미터 크기의 시작 사각형입니다. 그것을 그리면 화면이 하나의
     * 면으로 가득 차므로, 대신 그 면을 버리고 이곳에 셉니다.
     */
    DIAG_BRUSH_OPEN,

    /**
     * @brief More brush-backed levels were loaded at once than ::Level has
     *        storage for; the oldest lost its geometry.
     *
     * ENGLISH: A ::BrushMap is seventeen times the size of a ::Level and cannot
     * live inside one -- ::World and a dozen tools put Levels on the stack, and
     * embedding it there overflows before anything runs. So level.c keeps a
     * small pool and a Level points into it, which means the pool can run out.
     *
     * The evicted Level is left pointing at nothing, which reads as a level
     * with no geometry: you fall through the floor of a level that loaded
     * without complaint. Worth counting because the cause is three levels being
     * live at once, somewhere else entirely, and nothing on screen says so.
     *
     * 한국어: ::BrushMap은 ::Level의 열일곱 배이고 그 안에 들어갈 수 없습니다. ::World와 도구
     * 여남은 개가 Level을 스택에 올리며, 그곳에 넣으면 무엇이 실행되기도 전에 넘칩니다. 그래서
     * level.c가 작은 풀을 두고 Level이 그곳을 가리키며, 이는 풀이 고갈될 수 있다는 뜻입니다.
     *
     * 축출된 Level은 아무것도 가리키지 않게 되고, 그것은 지오메트리가 없는 레벨로 나타납니다.
     * 불평 없이 로드된 레벨의 바닥을 통과해 떨어집니다. 세어 둘 가치가 있는 이유는 원인이 전혀
     * 다른 곳에서 레벨 셋이 동시에 살아 있는 것이고, 화면의 무엇도 그것을 말하지 않기
     * 때문입니다.
     */
    DIAG_LEVEL_SLOTS,

    /**
     * @brief A level declared more entity markers than ::LVL_MAX_ENTS holds.
     *
     * ENGLISH: The surplus is dropped, and what that looks like is a room the
     * author populated and the player walks through empty. Distinct from
     * ::DIAG_ENEMY_CAP and ::DIAG_PICKUP_CAP, which fire when the MARKERS were
     * all read and the pools they spawn into ran out -- same symptom, different
     * number to raise.
     *
     * 한국어: 초과분은 버려지며, 그 모습은 제작자가 채워 넣은 방을 플레이어가 텅 빈 채로
     * 지나가는 것입니다. ::DIAG_ENEMY_CAP, ::DIAG_PICKUP_CAP과 구별됩니다. 그 둘은 표식은
     * 모두 읽혔고 그것이 생성해 넣는 풀이 고갈되었을 때 발생합니다. 증상은 같고 올려야 할
     * 숫자가 다릅니다.
     */
    /**
     * @brief A ray met more openness changes than the trace's table holds.
     *
     * ENGLISH: ::level_trace works by finding every point along a ray where
     * open and solid could swap -- a sector outline crossed in plan, a floor or
     * ceiling crossed in height -- and sampling between them. That list has a
     * fixed cap. Over it, the trace falls back to the fixed-step sampler it
     * replaced: slower, and not wrong, which is why this is a counter rather
     * than a failure. A level that raises it is one with an unusual number of
     * overlapping sectors along one line, and the number to raise is
     * TRACE_MAX_EVENTS in level.c.
     *
     * 한국어: ::level_trace는 광선을 따라 개방과 폐쇄가 뒤바뀔 수 있는 모든 지점(평면에서
     * 넘는 섹터 외곽선, 높이에서 넘는 바닥이나 천장)을 찾아 그 사이를 샘플링하는 방식으로
     * 동작합니다. 그 목록에는 고정된 상한이 있습니다. 넘으면 판정은 자신이 대체한 고정 보폭
     * 샘플러로 되돌아갑니다. 느릴 뿐 틀리지 않으며, 그래서 이것이 실패가 아니라 카운터인
     * 이유입니다. 이것을 발생시키는 레벨은 한 직선 위에 겹치는 섹터가 유난히 많은 레벨이고,
     * 올려야 할 값은 level.c의 TRACE_MAX_EVENTS입니다.
     */
    /**
     * @brief A recording ran out of frames, and stopped growing rather than wrapping.
     *
     * ENGLISH: ::DEMO_MAX_FRAMES is five minutes at 60fps and a recording that
     * reaches it keeps what it has. A demo that ends early is a shorter demo; a
     * demo that overwrote its own start would be one that replays into a
     * different run and looks like the simulation having drifted.
     *
     * 한국어: ::DEMO_MAX_FRAMES는 60fps에서 5분이며 그에 도달한 기록은 가진 것을 유지합니다.
     * 일찍 끝나는 데모는 짧은 데모이지만, 자기 시작을 덮어쓴 데모는 다른 플레이로 재생되면서
     * 시뮬레이션이 어긋난 것처럼 보이게 됩니다.
     */
    DIAG_DEMO_FULL,
    DIAG_TRACE_EVENTS,
    DIAG_ENT_CAP,

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

/**
 * @brief Records a misplaced draw: one made on the wrong side of the pass boundary.
 *
 * ENGLISH
 * -------
 * The frame has two halves. Everything before post_end is the WORLD pass and
 * gets pixelised and dithered; everything after is UI at native resolution.
 * Which side a draw belongs on is a real decision -- the view model is
 * deliberately in the world pass because it shares the scene's lighting,
 * while 5x7 glyphs must not be, because magnified and dithered they are
 * unreadable.
 *
 * Getting it wrong produces no error and no crash, just a cosmetic oddity, so
 * it is the kind of mistake that survives review and ships. These record it
 * instead. The counter shows up in the HUD like any other, and costs nothing
 * in release.
 *
 * 한국어
 * ------
 * @brief 잘못된 패스에서 수행된 그리기를 기록합니다.
 *
 * 프레임은 두 부분으로 나뉩니다. post_end 이전은 *월드* 패스로 픽셀화와 디더링을
 * 거치고, 이후는 원해상도 UI입니다. 어떤 그리기가 어느 쪽에 속하는지는 실제 판단이
 * 필요한 문제입니다. 뷰 모델은 장면의 조명을 공유하므로 의도적으로 월드 패스에 두는
 * 반면, 5x7 글리프는 확대되고 디더링되면 읽을 수 없으므로 그쪽에 있어서는 안 됩니다.
 *
 * 잘못 두어도 오류나 충돌이 발생하지 않고 외관상 어색함만 남으므로, 리뷰를 통과해
 * 출시까지 살아남는 종류의 실수입니다. 이 매크로들이 대신 그것을 기록합니다. 카운터는
 * 다른 것들과 마찬가지로 HUD에 표시되며 릴리스에서는 비용이 없습니다.
 */
#define DIAG_WANT_WORLD_PASS(in_world) \
    do { if (!(in_world)) diag_report(DIAG_PASS_ORDER); } while (0)
#define DIAG_WANT_UI_PASS(in_world) \
    do { if  ((in_world)) diag_report(DIAG_PASS_ORDER); } while (0)

#else  /* release: every trace of this module disappears */

/* The cast to void keeps `DIAG(x);` a valid statement and silences any
   unused-value warning, while emitting no code. The argument is NOT
   evaluated, so a report site may reference a DiagKind constant that does not
   exist in this build.
   void 캐스트는 `DIAG(x);`를 유효한 문장으로 유지하고 미사용 값 경고를 억제하면서도
   코드를 전혀 생성하지 않습니다. 인자는 평가되지 않으므로, 보고 지점이 해당 빌드에
   존재하지 않는 DiagKind 상수를 참조해도 무방합니다. */
#define DIAG(kind) ((void)0)

/* The pass-boundary guards, likewise. The argument is a function call at the
   call sites, so it is cast to void rather than dropped -- that keeps it from
   looking unused to a reader without evaluating it.
   패스 경계 가드도 마찬가지입니다. 호출 지점에서 인자가 함수 호출이므로, 버리지 않고
   void로 캐스트합니다. 평가하지 않으면서도 읽는 사람에게 미사용으로 보이지 않게 하기
   위함입니다. */
#define DIAG_WANT_WORLD_PASS(in_world) ((void)0)
#define DIAG_WANT_UI_PASS(in_world)    ((void)0)

#endif /* DIAG_ENABLED */

#endif /* DIAG_H */
