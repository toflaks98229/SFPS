/**
 * @file m.h
 * @brief Minimal 3D math: a 3-component vector and a 4x4 matrix in GL memory order.
 *
 * ENGLISH
 * -------
 * Every routine is `static inline`. This is a deliberate size decision rather
 * than a performance one: an inline function that nobody calls emits no code
 * at all, so the unused half of this header costs zero bytes after
 * `--gc-sections`. A conventionally compiled math library would drag its
 * entire object file into the binary whether or not the game used it.
 *
 * @note Matrices are COLUMN-MAJOR, matching what OpenGL expects, so a mat4
 *       may be handed to `glUniformMatrix4fv` with `transpose = GL_FALSE` and
 *       no repacking. Element (row `r`, column `c`) lives at `m[c * 4 + r]`.
 *       Getting this backwards produces a transposed transform that still
 *       looks plausible on screen, so treat the indexing convention as part
 *       of the type's contract.
 * @note The projection helpers produce a right-handed clip space with a depth
 *       range of [-1, 1], which is OpenGL's default convention.
 *
 * 한국어
 * ------
 * 모든 루틴은 `static inline`입니다. 이는 성능이 아닌 크기를 위한 의도적인
 * 결정입니다. 아무도 호출하지 않는 인라인 함수는 코드를 전혀 생성하지 않으므로,
 * 이 헤더에서 사용되지 않는 부분은 `--gc-sections` 이후 0바이트를 차지합니다.
 * 일반적으로 컴파일된 수학 라이브러리는 게임의 사용 여부와 관계없이 오브젝트
 * 파일 전체를 바이너리로 끌어들입니다.
 *
 * @note 행렬은 OpenGL이 기대하는 형식과 일치하는 열 우선(COLUMN-MAJOR) 방식입니다.
 *       따라서 mat4는 재포장 없이 `transpose = GL_FALSE`로 `glUniformMatrix4fv`에
 *       전달할 수 있습니다. (행 `r`, 열 `c`) 원소는 `m[c * 4 + r]`에 위치합니다.
 *       이 규칙을 반대로 적용하면 화면상으로는 그럴듯해 보이는 전치된 변환이
 *       생성되므로, 인덱싱 규칙을 해당 타입의 계약으로 취급하십시오.
 * @note 투영 헬퍼는 OpenGL의 기본 규칙인 깊이 범위 [-1, 1]의 오른손 좌표계
 *       클립 공간을 생성합니다.
 */
#ifndef M_H
#define M_H

#include <math.h>

/* --- Macros and constants / 매크로 및 상수 --- */

#define M_TAU 6.2831853f    ///< @brief A full turn in radians (2*pi). / 라디안 단위의 한 바퀴 (2*pi).
#define M_PI_F 3.14159265f  ///< @brief Pi as a float, avoiding the double-precision math.h macro. / float 타입의 파이. math.h의 배정밀도 매크로를 회피합니다.

/* --- Type definitions / 타입 정의 --- */

/**
 * @struct v3
 * @brief A 3-component vector, used for positions, directions and RGB colours alike.
 *
 * ENGLISH
 * -------
 * @brief A 3-component vector, used for positions, directions and RGB colours alike.
 *
 * 한국어
 * ------
 * @brief 위치, 방향, RGB 색상에 공통으로 사용되는 3성분 벡터입니다.
 */
typedef struct { float x, y, z; } v3;

/**
 * @struct mat4
 * @brief A 4x4 matrix stored in column-major order.
 *
 * ENGLISH
 * -------
 * @brief A 4x4 matrix stored in column-major order.
 * @note Element (row `r`, column `c`) is at `m[c * 4 + r]`. The translation
 *       component therefore occupies `m[12]`, `m[13]` and `m[14]`.
 *
 * 한국어
 * ------
 * @brief 열 우선 순서로 저장된 4x4 행렬입니다.
 * @note (행 `r`, 열 `c`) 원소는 `m[c * 4 + r]`에 있습니다. 따라서 이동(translation)
 *       성분은 `m[12]`, `m[13]`, `m[14]`를 차지합니다.
 */
typedef struct { float m[16]; } mat4;

/* --- Scalar helpers / 스칼라 헬퍼 --- */

/**
 * @brief Constrains a value to an inclusive range.
 *
 * ENGLISH
 * -------
 * @brief Constrains a value to an inclusive range.
 * @param[in] v  Value to clamp.
 * @param[in] lo Lower bound, returned when `v` falls below it.
 * @param[in] hi Upper bound, returned when `v` rises above it.
 * @return `v` limited to the range [`lo`, `hi`].
 * @warning Passing `lo > hi` yields `hi`; the arguments are not validated.
 *
 * 한국어
 * ------
 * @brief 값을 지정된 범위 내로 제한합니다 (경계값 포함).
 * @param[in] v  제한할 값.
 * @param[in] lo 하한값. `v`가 이보다 작으면 이 값이 반환됩니다.
 * @param[in] hi 상한값. `v`가 이보다 크면 이 값이 반환됩니다.
 * @return [`lo`, `hi`] 범위로 제한된 `v`.
 * @warning `lo > hi`를 전달하면 `hi`가 반환됩니다. 인자는 검증되지 않습니다.
 */
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* --- Vector construction and arithmetic / 벡터 생성 및 산술 --- */

/**
 * @brief Builds a vector from three components.
 *
 * ENGLISH
 * -------
 * @brief Builds a vector from three components.
 * @param[in] x X component.
 * @param[in] y Y component.
 * @param[in] z Z component.
 * @return The assembled vector.
 *
 * 한국어
 * ------
 * @brief 세 개의 성분으로 벡터를 생성합니다.
 * @param[in] x X 성분.
 * @param[in] y Y 성분.
 * @param[in] z Z 성분.
 * @return 조합된 벡터.
 */
static inline v3 v3f(float x, float y, float z) { v3 r = {x, y, z}; return r; }

/**
 * @brief Adds two vectors component-wise.
 *
 * ENGLISH
 * -------
 * @brief Adds two vectors component-wise.
 * @param[in] a First operand.
 * @param[in] b Second operand.
 * @return The sum `a + b`.
 *
 * 한국어
 * ------
 * @brief 두 벡터를 성분별로 더합니다.
 * @param[in] a 첫 번째 피연산자.
 * @param[in] b 두 번째 피연산자.
 * @return 합 `a + b`.
 */
static inline v3 v3add(v3 a, v3 b)   { return v3f(a.x+b.x, a.y+b.y, a.z+b.z); }

/**
 * @brief Subtracts one vector from another component-wise.
 *
 * ENGLISH
 * -------
 * @brief Subtracts one vector from another component-wise.
 * @param[in] a Vector to subtract from.
 * @param[in] b Vector to subtract.
 * @return The difference `a - b`, which points from `b` toward `a`.
 *
 * 한국어
 * ------
 * @brief 한 벡터에서 다른 벡터를 성분별로 뺍니다.
 * @param[in] a 피감수 벡터.
 * @param[in] b 감수 벡터.
 * @return 차이 `a - b`. `b`에서 `a`를 향하는 방향입니다.
 */
static inline v3 v3sub(v3 a, v3 b)   { return v3f(a.x-b.x, a.y-b.y, a.z-b.z); }

/**
 * @brief Multiplies a vector by a scalar.
 *
 * ENGLISH
 * -------
 * @brief Multiplies a vector by a scalar.
 * @param[in] a Vector to scale.
 * @param[in] s Scale factor; a negative value also reverses the direction.
 * @return The scaled vector `a * s`.
 *
 * 한국어
 * ------
 * @brief 벡터에 스칼라를 곱합니다.
 * @param[in] a 배율을 적용할 벡터.
 * @param[in] s 배율 계수. 음수 값은 방향을 반전시킵니다.
 * @return 배율이 적용된 벡터 `a * s`.
 */
static inline v3 v3scale(v3 a, float s) { return v3f(a.x*s, a.y*s, a.z*s); }

/**
 * @brief Computes the dot product of two vectors.
 *
 * ENGLISH
 * -------
 * @brief Computes the dot product of two vectors.
 * @param[in] a First operand.
 * @param[in] b Second operand.
 * @return The scalar `a . b`. For unit vectors this is the cosine of the
 *         angle between them, so the sign alone reports whether they point
 *         broadly the same way.
 *
 * 한국어
 * ------
 * @brief 두 벡터의 내적을 계산합니다.
 * @param[in] a 첫 번째 피연산자.
 * @param[in] b 두 번째 피연산자.
 * @return 스칼라 `a . b`. 단위 벡터의 경우 두 벡터 사이 각도의 코사인 값이므로,
 *         부호만으로도 두 벡터가 대체로 같은 방향을 가리키는지 알 수 있습니다.
 */
static inline float v3dot(v3 a, v3 b){ return a.x*b.x + a.y*b.y + a.z*b.z; }

/**
 * @brief Computes the length (magnitude) of a vector.
 *
 * ENGLISH
 * -------
 * @brief Computes the length (magnitude) of a vector.
 * @param[in] a Vector to measure.
 * @return The Euclidean length, always non-negative.
 * @note Involves a square root. When only comparing two lengths, comparing
 *       `v3dot(a, a)` against a squared threshold avoids it.
 *
 * 한국어
 * ------
 * @brief 벡터의 길이(크기)를 계산합니다.
 * @param[in] a 측정할 벡터.
 * @return 유클리드 길이. 항상 0 이상입니다.
 * @note 제곱근 연산을 포함합니다. 단순히 두 길이를 비교할 때는 `v3dot(a, a)`를
 *       제곱된 임계값과 비교하면 이 연산을 피할 수 있습니다.
 */
static inline float v3len(v3 a)      { return sqrtf(v3dot(a, a)); }

/**
 * @brief Scales a vector to unit length.
 *
 * ENGLISH
 * -------
 * @brief Scales a vector to unit length.
 * @param[in] a Vector to normalise.
 * @return The unit-length vector, or the zero vector when `a` is shorter than
 *         1e-6 units.
 * @note The zero-length case returns (0,0,0) rather than dividing by zero, so
 *       a degenerate direction propagates as "no direction" instead of NaN.
 *       Callers that must distinguish the two should test the length first.
 *
 * 한국어
 * ------
 * @brief 벡터를 단위 길이로 정규화합니다.
 * @param[in] a 정규화할 벡터.
 * @return 단위 길이 벡터. `a`의 길이가 1e-6 미만이면 영벡터를 반환합니다.
 * @note 길이가 0인 경우 0으로 나누는 대신 (0,0,0)을 반환하므로, 퇴화된 방향은
 *       NaN이 아닌 "방향 없음"으로 전파됩니다. 두 경우를 구분해야 하는
 *       호출자는 먼저 길이를 검사해야 합니다.
 */
static inline v3 v3norm(v3 a) {
    float l = v3len(a);
    /* Guard against division by zero: a vector this short has no meaningful
       direction to preserve.
       0으로 나누는 것을 방지합니다. 이 정도로 짧은 벡터는 보존할 만한 의미 있는
       방향을 갖지 않습니다. */
    return l > 1e-6f ? v3scale(a, 1.0f / l) : v3f(0, 0, 0);
}

/**
 * @brief Computes the cross product of two vectors.
 *
 * ENGLISH
 * -------
 * @brief Computes the cross product of two vectors.
 * @param[in] a First operand.
 * @param[in] b Second operand.
 * @return A vector perpendicular to both `a` and `b`, following the
 *         right-hand rule. Its length equals the area of the parallelogram
 *         they span, so parallel inputs yield the zero vector.
 * @warning Not commutative: `v3cross(a, b)` is the negation of
 *          `v3cross(b, a)`. Swapping the operands flips a surface normal.
 *
 * 한국어
 * ------
 * @brief 두 벡터의 외적을 계산합니다.
 * @param[in] a 첫 번째 피연산자.
 * @param[in] b 두 번째 피연산자.
 * @return 오른손 법칙에 따라 `a`와 `b` 양쪽에 수직인 벡터. 그 길이는 두 벡터가
 *         이루는 평행사변형의 넓이와 같으므로, 평행한 입력은 영벡터를 반환합니다.
 * @warning 교환법칙이 성립하지 않습니다. `v3cross(a, b)`는 `v3cross(b, a)`의
 *          부호를 반전한 값입니다. 피연산자를 바꾸면 표면 법선이 뒤집힙니다.
 */
static inline v3 v3cross(v3 a, v3 b) {
    return v3f(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}

/* --- Matrix construction / 행렬 생성 --- */

/**
 * @brief Builds the identity matrix.
 *
 * ENGLISH
 * -------
 * @brief Builds the identity matrix.
 * @return A matrix that leaves any vector it transforms unchanged.
 *
 * 한국어
 * ------
 * @brief 단위 행렬을 생성합니다.
 * @return 변환하는 모든 벡터를 변경하지 않고 그대로 두는 행렬.
 */
static inline mat4 mat4_identity(void) {
    mat4 r = {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    return r;
}

/**
 * @brief Builds a translation matrix.
 *
 * ENGLISH
 * -------
 * @brief Builds a translation matrix.
 * @param[in] t Offset to translate by.
 * @return A matrix that displaces points by `t` and leaves directions
 *         (w = 0) untouched.
 *
 * 한국어
 * ------
 * @brief 이동 행렬을 생성합니다.
 * @param[in] t 이동할 오프셋.
 * @return 점을 `t`만큼 변위시키고 방향 벡터(w = 0)는 그대로 두는 행렬.
 */
static inline mat4 mat4_translate(v3 t) {
    mat4 r = mat4_identity();
    /* Column-major layout puts the translation in the fourth column.
       열 우선 배치에서는 이동 성분이 네 번째 열에 위치합니다. */
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

/**
 * @brief Builds a non-uniform scale matrix.
 *
 * ENGLISH
 * -------
 * @brief Builds a non-uniform scale matrix.
 * @param[in] s Per-axis scale factors.
 * @return A matrix scaling each axis independently.
 * @warning A zero component collapses that axis and makes the matrix
 *          singular, which cannot be inverted. A negative component mirrors
 *          the axis and reverses triangle winding, so backface culling may
 *          discard the geometry.
 *
 * 한국어
 * ------
 * @brief 비균등 크기 조절 행렬을 생성합니다.
 * @param[in] s 축별 크기 조절 계수.
 * @return 각 축을 독립적으로 조절하는 행렬.
 * @warning 성분이 0이면 해당 축이 붕괴되어 행렬이 특이 행렬이 되며, 역행렬을
 *          구할 수 없습니다. 성분이 음수이면 축이 반전되어 삼각형의 감기 순서가
 *          뒤집히므로, 후면 컬링에 의해 지오메트리가 제거될 수 있습니다.
 */
static inline mat4 mat4_scale(v3 s) {
    mat4 r = mat4_identity();
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}

/**
 * @brief Builds a rotation matrix about the X axis.
 *
 * ENGLISH
 * -------
 * @brief Builds a rotation matrix about the X axis.
 * @param[in] a Angle in radians, counter-clockwise looking down the +X axis.
 * @return The rotation matrix.
 *
 * 한국어
 * ------
 * @brief X축을 중심으로 회전하는 행렬을 생성합니다.
 * @param[in] a 라디안 단위 각도. +X축을 내려다볼 때 반시계 방향입니다.
 * @return 회전 행렬.
 */
static inline mat4 mat4_rot_x(float a) {
    float c = cosf(a), s = sinf(a);
    mat4 r = mat4_identity();
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}

/**
 * @brief Builds a rotation matrix about the Y axis.
 *
 * ENGLISH
 * -------
 * @brief Builds a rotation matrix about the Y axis.
 * @param[in] a Angle in radians. This is the yaw axis for an upright world.
 * @return The rotation matrix.
 *
 * 한국어
 * ------
 * @brief Y축을 중심으로 회전하는 행렬을 생성합니다.
 * @param[in] a 라디안 단위 각도. 수직으로 선 월드에서는 요(yaw) 축에 해당합니다.
 * @return 회전 행렬.
 */
static inline mat4 mat4_rot_y(float a) {
    float c = cosf(a), s = sinf(a);
    mat4 r = mat4_identity();
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}

/**
 * @brief Builds a rotation matrix about the Z axis.
 *
 * ENGLISH
 * -------
 * @brief Builds a rotation matrix about the Z axis.
 * @param[in] a Angle in radians. This is the roll axis for a forward-facing view.
 * @return The rotation matrix.
 *
 * 한국어
 * ------
 * @brief Z축을 중심으로 회전하는 행렬을 생성합니다.
 * @param[in] a 라디안 단위 각도. 전방을 바라보는 시점에서는 롤(roll) 축에 해당합니다.
 * @return 회전 행렬.
 */
static inline mat4 mat4_rot_z(float a) {
    float c = cosf(a), s = sinf(a);
    mat4 r = mat4_identity();
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}

/* --- Matrix operations / 행렬 연산 --- */

/**
 * @brief Multiplies two matrices.
 *
 * ENGLISH
 * -------
 * @brief Multiplies two matrices.
 * @param[in] a Left-hand operand, applied second.
 * @param[in] b Right-hand operand, applied first.
 * @return The composed transform `a * b`.
 * @warning Matrix multiplication is not commutative. Read the result
 *          right-to-left: `mat4_mul(view, model)` transforms a vertex by
 *          `model` and only then by `view`.
 *
 * 한국어
 * ------
 * @brief 두 행렬을 곱합니다.
 * @param[in] a 좌변 피연산자. 두 번째로 적용됩니다.
 * @param[in] b 우변 피연산자. 첫 번째로 적용됩니다.
 * @return 합성된 변환 `a * b`.
 * @warning 행렬 곱셈은 교환법칙이 성립하지 않습니다. 결과는 오른쪽에서 왼쪽으로
 *          읽으십시오. `mat4_mul(view, model)`은 정점에 `model`을 적용한 뒤
 *          `view`를 적용합니다.
 */
static inline mat4 mat4_mul(mat4 a, mat4 b) {
    mat4 r;
    /* Each output element is the dot product of a row of `a` with a column of
       `b`; the index arithmetic reflects the column-major layout.
       각 출력 원소는 `a`의 행과 `b`의 열의 내적입니다. 인덱스 연산은 열 우선
       배치를 반영합니다. */
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++)
            r.m[c*4+i] = a.m[0*4+i]*b.m[c*4+0] + a.m[1*4+i]*b.m[c*4+1]
                       + a.m[2*4+i]*b.m[c*4+2] + a.m[3*4+i]*b.m[c*4+3];
    return r;
}

/**
 * @brief Transforms a point by a matrix, treating it as having w = 1.
 *
 * ENGLISH
 * -------
 * @brief Transforms a point by a matrix, treating it as having w = 1.
 * @param[in] m Transform to apply.
 * @param[in] p Point to transform.
 * @return The transformed point.
 * @note Translation IS applied, because w is taken to be 1. The projection
 *       row is ignored and no perspective divide is performed, so this is
 *       suited to model/view transforms rather than to clip-space work.
 *       Directions and normals must not be passed here, or they will be
 *       displaced by the translation.
 *
 * 한국어
 * ------
 * @brief w = 1로 간주하여 점에 행렬 변환을 적용합니다.
 * @param[in] m 적용할 변환.
 * @param[in] p 변환할 점.
 * @return 변환된 점.
 * @note w를 1로 간주하므로 이동 성분이 적용됩니다. 투영 행은 무시되며 원근
 *       나눗셈도 수행하지 않으므로, 클립 공간 작업보다는 모델/뷰 변환에
 *       적합합니다. 방향 벡터나 법선은 이 함수에 전달해서는 안 됩니다.
 *       이동 성분만큼 변위되기 때문입니다.
 */
static inline v3 mat4_mul_pt(mat4 m, v3 p) {
    /* The trailing m[12..14] terms are the translation, included because the
       implicit w is 1.
       마지막 m[12..14] 항은 이동 성분이며, 암묵적인 w가 1이므로 포함됩니다. */
    return v3f(m.m[0]*p.x + m.m[4]*p.y + m.m[8] *p.z + m.m[12],
               m.m[1]*p.x + m.m[5]*p.y + m.m[9] *p.z + m.m[13],
               m.m[2]*p.x + m.m[6]*p.y + m.m[10]*p.z + m.m[14]);
}

/* --- Projection and view matrices / 투영 및 뷰 행렬 --- */

/**
 * @brief Builds a right-handed orthographic projection.
 *
 * ENGLISH
 * -------
 * @brief Builds a right-handed orthographic projection.
 * @param[in] l  Left clip plane.
 * @param[in] r  Right clip plane.
 * @param[in] b  Bottom clip plane.
 * @param[in] t  Top clip plane.
 * @param[in] zn Near clip plane.
 * @param[in] zf Far clip plane.
 * @return The projection matrix, mapping the box to a depth range of [-1, 1].
 * @warning Degenerate bounds (`l == r`, `b == t` or `zn == zf`) divide by
 *          zero. Used by the map editor's 2D profile viewport.
 *
 * 한국어
 * ------
 * @brief 오른손 좌표계 직교 투영 행렬을 생성합니다.
 * @param[in] l  좌측 클립 평면.
 * @param[in] r  우측 클립 평면.
 * @param[in] b  하단 클립 평면.
 * @param[in] t  상단 클립 평면.
 * @param[in] zn 근거리 클립 평면.
 * @param[in] zf 원거리 클립 평면.
 * @return 해당 영역을 깊이 범위 [-1, 1]로 매핑하는 투영 행렬.
 * @warning 퇴화된 경계값(`l == r`, `b == t`, `zn == zf`)은 0으로 나누기를
 *          유발합니다. 맵 에디터의 2D 프로파일 뷰포트에서 사용됩니다.
 */
static inline mat4 mat4_ortho(float l, float r, float b, float t,
                              float zn, float zf) {
    mat4 m = mat4_identity();
    m.m[0]  =  2.0f / (r - l);
    m.m[5]  =  2.0f / (t - b);
    /* Negated so that increasing view-space depth maps to increasing NDC
       depth under the right-handed convention.
       오른손 좌표계 규칙에서 뷰 공간 깊이의 증가가 NDC 깊이의 증가로 매핑되도록
       부호를 반전합니다. */
    m.m[10] = -2.0f / (zf - zn);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(zf + zn) / (zf - zn);
    return m;
}

/**
 * @brief Builds a right-handed perspective projection.
 *
 * ENGLISH
 * -------
 * @brief Builds a right-handed perspective projection.
 * @param[in] fov_y  Vertical field of view in radians.
 * @param[in] aspect Viewport width divided by height.
 * @param[in] zn     Near clip plane; must be greater than zero.
 * @param[in] zf     Far clip plane; must exceed `zn`.
 * @return The projection matrix, with a depth range of [-1, 1].
 * @warning A zero `aspect`, a `zn` of zero, or `zn == zf` divides by zero.
 *          Guard the aspect ratio when the window height can reach zero
 *          during a resize.
 * @note Depth precision concentrates near `zn`. Prefer pushing `zn` outward
 *       over pulling `zf` inward when z-fighting appears in the distance.
 *
 * 한국어
 * ------
 * @brief 오른손 좌표계 원근 투영 행렬을 생성합니다.
 * @param[in] fov_y  라디안 단위의 수직 시야각.
 * @param[in] aspect 뷰포트 너비를 높이로 나눈 값.
 * @param[in] zn     근거리 클립 평면. 0보다 커야 합니다.
 * @param[in] zf     원거리 클립 평면. `zn`보다 커야 합니다.
 * @return 깊이 범위가 [-1, 1]인 투영 행렬.
 * @warning `aspect`가 0이거나, `zn`이 0이거나, `zn == zf`이면 0으로 나누기가
 *          발생합니다. 창 크기 조절 중 높이가 0이 될 수 있다면 종횡비를
 *          보호하십시오.
 * @note 깊이 정밀도는 `zn` 근처에 집중됩니다. 원거리에서 z-파이팅이 발생하면
 *       `zf`를 당기기보다 `zn`을 밀어내는 편이 낫습니다.
 */
static inline mat4 mat4_perspective(float fov_y, float aspect, float zn, float zf) {
    /* Cotangent of the half-angle: the focal length in NDC units.
       반각의 코탄젠트이며, NDC 단위의 초점 거리에 해당합니다. */
    float f = 1.0f / tanf(fov_y * 0.5f);
    mat4 r = {{0}};
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (zf + zn) / (zn - zf);
    /* Copies -z into w so the hardware's perspective divide shrinks distant
       geometry.
       -z를 w로 복사하여 하드웨어의 원근 나눗셈이 먼 지오메트리를 축소하도록
       합니다. */
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zf * zn) / (zn - zf);
    return r;
}

/**
 * @brief Builds a view matrix for an FPS camera.
 *
 * ENGLISH
 * -------
 * @brief Builds a view matrix for an FPS camera.
 * @param[in] eye   Camera position in world space.
 * @param[in] yaw   Rotation about the world +Y axis, in radians. A yaw of
 *                  zero looks down -Z.
 * @param[in] pitch Rotation about the camera's own right axis, in radians;
 *                  positive looks upward.
 * @return The world-to-view matrix.
 * @note Roll is deliberately absent, and the right vector is kept horizontal,
 *       which is what keeps the horizon level however far the view pitches.
 *       Clamp `pitch` short of +/-pi/2 in the caller: exactly vertical makes
 *       the forward and world-up vectors parallel and the basis degenerate.
 *
 * 한국어
 * ------
 * @brief FPS 카메라용 뷰 행렬을 생성합니다.
 * @param[in] eye   월드 공간에서의 카메라 위치.
 * @param[in] yaw   월드 +Y축 기준 회전 (라디안). yaw가 0이면 -Z 방향을 바라봅니다.
 * @param[in] pitch 카메라 자체의 우측 축 기준 회전 (라디안). 양수는 위를 봅니다.
 * @return 월드 공간에서 뷰 공간으로의 변환 행렬.
 * @note 롤(roll)은 의도적으로 제외되었으며 우측 벡터는 수평으로 유지됩니다.
 *       이것이 시점이 아무리 기울어져도 수평선이 유지되는 이유입니다. 호출자는
 *       `pitch`를 +/-pi/2 미만으로 제한해야 합니다. 정확히 수직이 되면 전방
 *       벡터와 월드 상향 벡터가 평행해져 기저가 퇴화됩니다.
 */
static inline mat4 mat4_fps_view(v3 eye, float yaw, float pitch) {
    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    v3 fwd   = v3f(-sy * cp, sp, -cy * cp);
    /* The right vector ignores pitch entirely, so the camera can never roll.
       우측 벡터는 피치를 완전히 무시하므로 카메라는 절대 롤링하지 않습니다. */
    v3 right = v3f(cy, 0, -sy);
    v3 up    = v3cross(right, fwd);
    mat4 r = mat4_identity();
    /* The basis is written transposed, which inverts the rotation -- a view
       matrix maps the world into the camera, not the reverse.
       기저는 전치된 형태로 기록되며, 이는 회전을 역으로 만듭니다. 뷰 행렬은
       월드를 카메라 공간으로 매핑하는 것이지 그 반대가 아니기 때문입니다. */
    r.m[0] = right.x; r.m[4] = right.y; r.m[8]  = right.z;
    r.m[1] = up.x;    r.m[5] = up.y;    r.m[9]  = up.z;
    r.m[2] = -fwd.x;  r.m[6] = -fwd.y;  r.m[10] = -fwd.z;
    /* Translation expressed in the rotated basis, equivalent to rotating the
       negated eye position.
       회전된 기저로 표현된 이동 성분이며, 부호를 반전한 시점 위치를 회전시킨
       것과 동일합니다. */
    r.m[12] = -v3dot(right, eye);
    r.m[13] = -v3dot(up, eye);
    r.m[14] =  v3dot(fwd, eye);
    return r;
}

#endif
