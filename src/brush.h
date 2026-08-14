/**
 * @file brush.h
 * @brief Quake-style convex brushes read straight out of a TrenchBroom .map.
 *
 * ENGLISH
 * -------
 * A brush is a set of planes and the solid is what they all agree on. That one
 * sentence is the whole reason this exists: ::Sector says "at this x,z the
 * floor is here", which admits exactly one floor per point and therefore no
 * slope, no room above a room, and no bridge. A convex volume bounded by
 * arbitrary planes has none of those limits, and several of them stacked make
 * any shape a level needs.
 *
 * THE FILE IS THE FORMAT. TrenchBroom writes .map and this reads .map -- there
 * is no converter in between, which is the point. A converter is a second
 * program that has to agree with the first about what a map means, and the
 * failure mode is a level that looks right in the editor and is wrong in the
 * game with nothing to say why. ::level.h's sibling importer
 * (assets/import-doom-level.py) is exactly that shape, and its own header
 * documents what it silently drops.
 *
 * NO BSP TREE IS BUILT. Quake compiled brushes into one because a software
 * renderer has to draw polygons in visibility order and had no depth buffer to
 * do it with. This engine has GL's depth buffer and levels measured in hundreds
 * of brushes, so the tree would buy sorting nobody needs and cost a compiler,
 * a file format and a class of bug where the drawn world and the collided world
 * are two different structures. Faces are drawn as polygons; collision asks the
 * planes directly.
 *
 * WHAT THIS MODULE DOES AND DOES NOT DO. It parses, and it turns planes into
 * polygons. It does not render, collide, or know what a `func_door` is --
 * entities arrive here as key/value text and whichever module owns a classname
 * interprets it, exactly as ::Entity's `kind` is interpreted by pickup.c and
 * enemy.c rather than by level.c.
 *
 * 한국어
 * ------
 * 브러시는 평면들의 집합이고, 고체는 그 평면들이 모두 동의하는 영역입니다. 이 한 문장이
 * 이 파일이 존재하는 이유의 전부입니다. ::Sector는 "이 x,z에서 바닥은 여기"라고 말하며,
 * 이는 지점당 정확히 하나의 바닥만을 허용합니다. 따라서 경사면도, 방 위의 방도, 다리도
 * 불가능합니다. 임의의 평면으로 둘러싸인 볼록 입체에는 그런 제약이 없고, 그것을 여러 개
 * 쌓으면 레벨에 필요한 어떤 형태든 만들어집니다.
 *
 * 파일 형식이 곧 형식입니다. TrenchBroom이 .map을 쓰고 이곳이 .map을 읽습니다. 그 사이에
 * 변환기가 없다는 것이 핵심입니다. 변환기란 "맵이 무엇을 뜻하는가"에 대해 첫 번째 프로그램과
 * 합의해야 하는 두 번째 프로그램이며, 그 실패 양상은 에디터에서는 맞아 보이고 게임에서는
 * 틀린 레벨입니다. 그리고 왜 그런지 말해 주는 것이 아무것도 없습니다.
 * assets/import-doom-level.py가 정확히 그 형태이며, 그 파일의 머리말이 무엇을 조용히
 * 버리는지 스스로 기록하고 있습니다.
 *
 * BSP 트리를 만들지 않습니다. Quake가 브러시를 트리로 컴파일한 이유는 소프트웨어 렌더러가
 * 폴리곤을 가시성 순서로 그려야 했고 그럴 깊이 버퍼가 없었기 때문입니다. 이 엔진에는 GL의
 * 깊이 버퍼가 있고 레벨은 브러시 수백 개 규모이므로, 트리는 아무도 필요로 하지 않는 정렬을
 * 사 오는 대신 컴파일러 하나와 파일 형식 하나, 그리고 "그려지는 월드와 충돌하는 월드가 서로
 * 다른 두 구조"라는 부류의 버그를 치르게 합니다. 면은 폴리곤으로 그리고, 충돌은 평면에 직접
 * 묻습니다.
 *
 * 이 모듈이 하는 일과 하지 않는 일. 파싱하고, 평면을 폴리곤으로 바꿉니다. 렌더링도 충돌도
 * 하지 않으며 `func_door`가 무엇인지도 모릅니다. 엔티티는 이곳에 key/value 텍스트로
 * 도착하고, 해당 classname을 소유한 모듈이 그것을 해석합니다. ::Entity의 `kind`를 level.c가
 * 아니라 pickup.c와 enemy.c가 해석하는 것과 정확히 같습니다.
 */
#ifndef BRUSH_H
#define BRUSH_H

/* Only the maths, for the same reason level.h includes only the maths: this
   header is on the simulation side, and pulling render.h in would drag gl.h,
   windows.h and the whole OpenGL API into every headless test that wants to
   ask a brush where its faces are.
   level.h가 수학만 포함하는 것과 같은 이유로 수학만 포함합니다. 이 헤더는 시뮬레이션
   쪽에 있으며, render.h를 끌어들이면 브러시에 면의 위치를 묻고 싶을 뿐인 모든 헤드리스
   테스트에 gl.h와 windows.h, OpenGL API 전체가 딸려 들어옵니다. */
#include "m.h"

/* Forward declarations: pointers only, exactly as level.h declares the same two
   and for the same reason. ::brush_geometry is the one function here that
   touches render types, and a pointer needs no definition.
   전방 선언입니다. 포인터로만 쓰이며, level.h가 같은 두 타입을 같은 이유로 선언하는 것과
   정확히 같습니다. 이곳에서 렌더 타입에 닿는 함수는 ::brush_geometry 하나뿐이고, 포인터에는
   정의가 필요 없습니다. */
typedef struct MeshBuf  MeshBuf;
typedef struct MdlRange MdlRange;

/* --- Units and axes / 단위와 축 ------------------------------------------ */

/**
 * @brief How many metres one .map unit is worth. 1/32, so grid 32 is one metre.
 *
 * ENGLISH
 * -------
 * THIS IS THE ONE NUMBER TO ARGUE ABOUT, so the argument is written down here
 * rather than left to whoever changes it.
 *
 * A Quake player is 56 units tall. This game's is 1.8m. At 1/32 that comes to
 * 57.6 units, near enough that a mapper's sense of scale transfers intact:
 * Quake's 64-unit doorway, its 128-unit corridor and its 16-unit step are all
 * still the right size here, and so is every prefab and tutorial written
 * against them. That transfer is most of what adopting the format buys, and a
 * scale that broke it would throw the larger half of the benefit away to keep
 * the smaller.
 *
 * The other candidates and why they lost:
 *
 *   1 unit = 1cm      matches the file units ::Sector already stores, so the
 *                     two models would print the same coordinates during the
 *                     transition. Rejected because a metre is then 100 units
 *                     and TrenchBroom's grid is powers of two -- a metre never
 *                     lands on a grid line, and the transition is temporary
 *                     while the grid is forever.
 *   1 unit = 1/64m    makes texture scale 1.0 reproduce today's texel density
 *                     exactly (see ::LEVEL_UV, 0.5 texels per metre against
 *                     128px art). Rejected because the same density is had by
 *                     setting the default face scale to 0.5, which is one
 *                     number in the editor's config, while halving the world
 *                     scale is a permanent break with every Quake dimension.
 *
 * @note At scale 1.0 a 128px texture therefore spans 4m rather than today's 2m.
 *       Set the editor's default face scale to 0.5 to match.
 *
 * 한국어
 * ------
 * @brief .map 단위 1이 몇 미터인지입니다. 1/32이므로 그리드 32가 1미터입니다.
 *
 * 이것이 논쟁의 대상이 될 유일한 숫자이므로, 그 논거를 바꾸는 사람에게 맡기지 않고 이곳에
 * 적어 둡니다.
 *
 * Quake의 플레이어는 56유닛이고 이 게임의 플레이어는 1.8m입니다. 1/32에서 그것은
 * 57.6유닛이며, 맵 제작자의 치수 감각이 그대로 옮겨 올 만큼 가깝습니다. Quake의 64유닛
 * 출입구, 128유닛 복도, 16유닛 계단이 이곳에서도 여전히 옳은 크기이고, 그것들을 기준으로
 * 쓰인 모든 프리팹과 강좌도 마찬가지입니다. 그 이전이 이 형식을 채택해 얻는 것의 대부분이며,
 * 그것을 깨는 스케일은 더 작은 이득을 지키려 더 큰 절반을 버리는 셈입니다.
 *
 * 탈락한 다른 후보와 그 이유는 다음과 같습니다.
 *
 *   1유닛 = 1cm    ::Sector가 이미 저장하는 파일 단위와 일치하여, 전환기 동안 두 모델이 같은
 *                  좌표를 출력하게 됩니다. 그러나 그러면 1미터가 100유닛이고 TrenchBroom의
 *                  그리드는 2의 거듭제곱이므로 1미터가 결코 격자선에 떨어지지 않습니다.
 *                  전환기는 한시적이고 그리드는 영구적이므로 탈락했습니다.
 *   1유닛 = 1/64m  텍스처 배율 1.0이 오늘의 텍셀 밀도를 정확히 재현합니다(::LEVEL_UV는
 *                  미터당 0.5텍셀이고 원본은 128px입니다). 그러나 같은 밀도는 에디터 설정에서
 *                  기본 면 배율을 0.5로 두는 숫자 하나로 얻을 수 있는 반면, 월드 스케일을
 *                  절반으로 줄이는 것은 모든 Quake 치수와의 영구적인 결별입니다.
 *
 * @note 따라서 배율 1.0에서 128px 텍스처는 오늘의 2m가 아니라 4m를 덮습니다. 에디터의 기본
 *       면 배율을 0.5로 맞추십시오.
 */
#define BRUSH_UNIT (1.0f / 32.0f)

/**
 * @brief Half-extent of the world a .map may describe, in map units.
 *
 * ENGLISH
 * -------
 * Not a limit anyone is expected to reach -- 16384 units is 512m, and this
 * game's largest level is a few tens of metres across. It exists because
 * ::brush_face_poly starts from a quad big enough to cover the face's whole
 * plane and clips it down, so something has to say how big "big enough" is.
 *
 * Deliberately not larger. The starting quad sits at twice this radius, and
 * float resolution at 1024m is about 0.12mm -- still well under
 * ::BRUSH_EPSILON, but the margin shrinks as the square of nothing useful. A
 * bound chosen for a world nobody builds would trade real precision for
 * imaginary reach.
 *
 * 한국어
 * ------
 * @brief .map이 기술할 수 있는 세계의 절반 크기이며, 맵 단위입니다.
 *
 * 누가 도달하리라 기대하는 한계가 아닙니다. 16384유닛은 512m이고 이 게임의 가장 큰 레벨은
 * 수십 미터 규모입니다. 이 값이 존재하는 이유는 ::brush_face_poly가 면의 평면 전체를 덮을
 * 만큼 큰 사각형에서 시작해 그것을 깎아 내려가기 때문이며, 따라서 "충분히 크다"가 얼마인지를
 * 무언가가 말해야 합니다.
 *
 * 일부러 더 크게 잡지 않았습니다. 시작 사각형은 이 반지름의 두 배에 놓이고, 1024m에서 float의
 * 분해능은 약 0.12mm입니다. 여전히 ::BRUSH_EPSILON보다 충분히 작지만, 아무 쓸모 없는 것을
 * 위해 여유가 줄어듭니다. 아무도 만들지 않는 세계를 위해 고른 경계는 실재하는 정밀도를 상상
 * 속의 도달 범위와 맞바꾸는 일입니다.
 */
#define BRUSH_MAX_COORD 16384.0f

/**
 * @brief How close to a plane counts as on it, in world units (metres).
 *
 * ENGLISH
 * -------
 * Half a millimetre. Above float noise at the far edge of ::BRUSH_MAX_COORD and
 * far below any feature a level can express -- the smallest thing a mapper
 * places on a grid of 1 unit is 3.1cm, sixty times this.
 *
 * 한국어
 * ------
 * @brief 평면 위에 있다고 볼 만큼 가까운 거리이며, 월드 단위(미터)입니다.
 *
 * 0.5밀리미터입니다. ::BRUSH_MAX_COORD 끝자락에서의 float 잡음보다는 크고, 레벨이 표현할 수
 * 있는 어떤 요소보다도 훨씬 작습니다. 제작자가 1유닛 그리드에 놓는 가장 작은 것이 3.1cm이며
 * 이 값의 60배입니다.
 */
#define BRUSH_EPSILON 0.0005f

/* --- Capacity limits / 용량 제한 ------------------------------------------
 *
 * ENGLISH
 * -------
 * All of this lives in .bss, which the floppy budget does not count -- see the
 * size rules in README.md. What it costs is RAM and nothing else, so these are
 * set generously and the overflow paths report rather than truncate.
 *
 * 한국어
 * ------
 * 이 전부는 .bss에 있으며 플로피 예산은 그것을 세지 않습니다. README.md의 크기 규칙을
 * 참조하십시오. 비용은 RAM일 뿐이므로 넉넉히 잡았고, 초과 경로는 잘라 내는 대신 보고합니다.
 */

/**
 * @brief Maximum faces one brush may have.
 *
 * A box has 6 and a 16-sided cylinder has 18. 32 covers anything TrenchBroom's
 * own primitives produce and leaves room for a mapper carving one by hand.
 *
 * @brief 하나의 브러시가 가질 수 있는 최대 면 수입니다.
 * @note 상자는 6개, 16각 원기둥은 18개입니다. 32면 TrenchBroom의 기본 도형이 만들어 내는
 *       어떤 것도 담으며, 손으로 깎아 만드는 경우의 여유도 남습니다.
 */
#define BR_MAX_FACES 32

/**
 * @brief Faces in the whole map, shared by every brush.
 *
 * ENGLISH
 * -------
 * A POOL rather than ::BR_MAX_FACES per brush, because face counts are not
 * uniform and the product is the wrong shape to budget. Six hundred boxes and
 * two hundred cylinders are both ordinary maps; sizing every brush for the
 * worst case would charge the boxes 32 slots each to hold 6, which is 300KB
 * spent on nothing. The pool lets one map be many simple brushes and another be
 * few complicated ones without either cap being set for the other's sake.
 *
 * 한국어
 * ------
 * @brief 맵 전체의 면 수이며, 모든 브러시가 공유합니다.
 *
 * 브러시당 ::BR_MAX_FACES가 아니라 *풀*인 이유는 면의 개수가 균일하지 않아서 곱셈이 예산의
 * 형태로 맞지 않기 때문입니다. 상자 600개짜리 맵과 원기둥 200개짜리 맵은 둘 다 평범합니다.
 * 모든 브러시를 최악의 경우에 맞춰 잡으면 상자마다 6개를 담으려고 32칸씩 물리게 되며, 이는
 * 아무것도 아닌 것에 300KB를 쓰는 일입니다. 풀 방식은 한 맵이 단순한 브러시 여럿이고 다른
 * 맵이 복잡한 브러시 몇 개인 상황을, 어느 한쪽 상한을 다른 쪽 사정에 맞추지 않고 담습니다.
 */
#define BR_MAX_TOTAL_FACES 4096

#define BR_MAX_BRUSHES 512   ///< @brief Brushes per map. / 맵당 브러시 수.
#define BR_MAX_ENTS     96   ///< @brief Entities per map, worldspawn included. / 맵당 엔티티 수. worldspawn 포함.
#define BR_MAX_KEYS     12   ///< @brief Key/value pairs one entity may carry. / 엔티티 하나가 가질 수 있는 key/value 쌍의 수.

#define BR_TEX  16   ///< @brief Texture name length. Matches ::LVL_MAT and Quake's 15+nul. / 텍스처 이름 길이. ::LVL_MAT 및 Quake의 15+널과 일치합니다.
#define BR_KEY  32   ///< @brief Entity key length. / 엔티티 키 길이.
#define BR_VAL  64   ///< @brief Entity value length; an `origin` triple fits with room to spare. / 엔티티 값 길이. `origin` 삼중항이 여유 있게 들어갑니다.

/**
 * @brief Maximum vertices one face polygon may reach.
 *
 * ENGLISH
 * -------
 * DERIVED, not chosen. ::brush_face_poly starts from a quad and clips it once
 * against each of the brush's other faces, and a convex clip adds at most one
 * vertex. So the true worst case is 4 + (::BR_MAX_FACES - 1) = 35, and the
 * static assert below is what keeps that true if either number moves. The
 * value is 64 rather than 35 because the buffers are stack locals in one
 * function and the round number costs nothing.
 *
 * 한국어
 * ------
 * @brief 하나의 면 폴리곤이 도달할 수 있는 최대 정점 수입니다.
 *
 * 고른 값이 아니라 *유도된* 값입니다. ::brush_face_poly는 사각형에서 시작해 브러시의 나머지
 * 면 각각에 대해 한 번씩 자르며, 볼록 절단은 정점을 최대 하나 늘립니다. 따라서 실제 최악의
 * 경우는 4 + (::BR_MAX_FACES - 1) = 35이고, 아래의 정적 검사가 두 숫자 중 어느 것이 바뀌어도
 * 그 사실을 유지시킵니다. 35가 아니라 64인 이유는 이 버퍼들이 함수 하나의 스택 지역 변수이며
 * 둥근 숫자가 비용을 발생시키지 않기 때문입니다.
 */
#define BR_MAX_POLY 64

_Static_assert(BR_MAX_POLY >= BR_MAX_FACES + 4,
               "a clip against every other face can reach 4 + BR_MAX_FACES - 1 "
               "vertices; BR_MAX_POLY must cover that");

/**
 * @brief Maximum material runs ::brush_geometry can produce.
 *
 * ENGLISH
 * -------
 * One run per change of texture along the face order, not one per texture: a
 * map that alternates two materials brush by brush produces two runs per
 * brush, and merging them would mean sorting the whole level by material and
 * losing the brush order the author sees in the editor.
 *
 * ::LVL_MAX_RANGES is `LVL_MAX_SECTORS * 3` for the same reason at a different
 * scale. Here the worst case is genuinely one run per face, so this is set
 * against the brush count rather than the face pool -- a map where every face
 * differs from its neighbour is not something anyone builds, and the overflow
 * merges rather than drops.
 *
 * 한국어
 * ------
 * @brief ::brush_geometry가 만들어 낼 수 있는 최대 재질 구간 수입니다.
 *
 * 텍스처마다 하나가 아니라 면 순서상 텍스처가 바뀔 때마다 하나입니다. 두 재질을 브러시마다
 * 번갈아 쓰는 맵은 브러시당 구간 두 개를 만들며, 그것을 병합하려면 레벨 전체를 재질로
 * 정렬해야 하고 제작자가 에디터에서 보는 브러시 순서를 잃게 됩니다.
 *
 * ::LVL_MAX_RANGES가 `LVL_MAX_SECTORS * 3`인 것도 규모만 다른 같은 이유입니다. 이곳의 최악은
 * 진정으로 면당 구간 하나이므로, 면 풀이 아니라 브러시 수를 기준으로 잡았습니다. 모든 면이
 * 이웃과 다른 맵은 아무도 만들지 않으며, 초과 시에는 버리지 않고 병합합니다.
 */
#define BR_MAX_RANGES (BR_MAX_BRUSHES * 2)

/**
 * @brief The texture size the editor and the engine agree on, in pixels.
 *
 * ENGLISH
 * -------
 * A Valve 220 face gives its offsets and scales in TEXELS, so turning them into
 * a 0..1 coordinate needs to know how many texels a tile has. That number is
 * not the engine's: ::TEX_SIZE is 256 because materials are generated at 256
 * for filtering headroom, while the hand-drawn wall art is 128 and 128 is what
 * TrenchBroom will show when the texture collection is exported.
 *
 * IT MUST MATCH THE EDITOR, not the renderer. If they disagree, a texture the
 * author fitted to a face in TrenchBroom arrives in the game at half or double
 * scale, and every face in the level is wrong by the same factor -- which reads
 * as a scale mistake in the map rather than as a constant here.
 *
 * 한국어
 * ------
 * @brief 에디터와 엔진이 합의하는 텍스처 크기이며 픽셀 단위입니다.
 *
 * Valve 220 면은 오프셋과 배율을 *텍셀*로 줍니다. 따라서 그것을 0..1 좌표로 바꾸려면 타일
 * 하나에 텍셀이 몇 개인지 알아야 합니다. 그 숫자는 엔진의 것이 아닙니다. ::TEX_SIZE가 256인
 * 것은 필터링 여유를 위해 재질을 256으로 생성하기 때문이고, 손으로 그린 벽 그림은 128이며
 * 텍스처 모음을 내보냈을 때 TrenchBroom이 보여 줄 것도 128입니다.
 *
 * 렌더러가 아니라 *에디터*와 일치해야 합니다. 둘이 어긋나면 제작자가 TrenchBroom에서 면에
 * 맞춘 텍스처가 게임에서는 절반이나 두 배 크기로 도착하고, 레벨의 모든 면이 같은 배수만큼
 * 틀립니다. 그것은 이곳의 상수가 아니라 맵의 배율 실수처럼 읽힙니다.
 */
#define BRUSH_TEXELS 128.0f

/* --- Types / 타입 --------------------------------------------------------- */

/**
 * @struct BrushFace
 * @brief One bounding plane of a brush, with the texture mapping on it.
 *
 * ENGLISH
 * -------
 * STORED AS A PLANE, not as the three points the file wrote. The points are one
 * of infinitely many ways to name the same plane, and every consumer -- the
 * clip that finds the polygon, the swept trace that will collide against it --
 * wants the normal and the distance. Keeping the points as well would be a
 * second description of one fact.
 *
 * `normal` points OUT of the solid, so a point is inside the brush when
 * `dot(normal, p) - dist <= 0` holds for every face. That is Quake's convention
 * and it survives the axis change unaltered, because the change is a rotation
 * (see ::brush_parse).
 *
 * THE UV AXES ARE WHY THIS FORMAT WAS ADOPTED. A Valve 220 face names its own
 * u and v directions, so "one texture fitted to this face" is something the
 * file can say. The sector model could not: render.c's ::planar_uv projects
 * along whichever world axis the normal points down, so a door's texture was
 * laid out in world coordinates and tiled across the door instead of fitting
 * it. Nothing about a door could fix that, because the projection never knew
 * the door was there.
 *
 * @note A Standard-format face carries no axes, so they are derived on parse
 *       from Quake's base-axis table and the face's rotation -- the same
 *       derivation qbsp does. After parsing, a Standard face and a Valve 220
 *       face are indistinguishable here, which is what lets one file mix them.
 * @note The rotation angle itself is NOT stored. In Valve 220 it is already
 *       baked into the axes and keeping it would invite someone to apply it
 *       twice; in Standard it has been applied by the time parsing ends.
 *
 * 한국어
 * ------
 * @brief 브러시의 경계 평면 하나이며, 그 위의 텍스처 매핑을 함께 담습니다.
 *
 * 파일이 기록한 세 점이 아니라 *평면으로* 저장합니다. 세 점은 같은 평면을 지목하는 무한히
 * 많은 방법 중 하나일 뿐이고, 모든 소비자는(폴리곤을 찾는 절단도, 앞으로 이것과 충돌할 스윕
 * 판정도) 법선과 거리를 원합니다. 점까지 함께 보관하는 것은 하나의 사실에 대한 두 번째
 * 기술입니다.
 *
 * `normal`은 고체의 *바깥*을 향하므로, 모든 면에 대해 `dot(normal, p) - dist <= 0`이면 점은
 * 브러시 안에 있습니다. 이는 Quake의 관례이며 축 변경을 겪고도 그대로 유지됩니다. 그 변경이
 * 회전이기 때문입니다(::brush_parse 참조).
 *
 * UV 축이 이 형식을 채택한 이유입니다. Valve 220 면은 자신의 u/v 방향을 스스로 지목하므로
 * "이 면에 텍스처 하나를 맞춘다"를 파일이 말할 수 있습니다. 섹터 모델은 그러지 못했습니다.
 * render.c의 ::planar_uv는 법선이 향하는 월드 축을 따라 투영하므로, 문의 텍스처는 월드
 * 좌표계에 놓여 문에 맞춰지는 대신 문을 가로질러 타일로 반복되었습니다. 문에 관한 무엇으로도
 * 그것을 고칠 수 없었는데, 투영이 그곳에 문이 있다는 사실을 애초에 알지 못했기 때문입니다.
 *
 * @note Standard 형식 면에는 축이 없으므로, 파싱 시점에 Quake의 기저 축 표와 면의 회전값에서
 *       유도합니다. qbsp가 하는 것과 동일한 유도입니다. 파싱이 끝나면 Standard 면과 Valve 220
 *       면은 이곳에서 구별되지 않으며, 그것이 한 파일이 둘을 섞을 수 있는 이유입니다.
 * @note 회전 각도 자체는 저장하지 *않습니다*. Valve 220에서는 이미 축에 반영되어 있어 보관하면
 *       누군가 두 번 적용하도록 부추기게 되고, Standard에서는 파싱이 끝난 시점에 이미 적용된
 *       뒤이기 때문입니다.
 */
typedef struct {
    v3    normal;          /**< Unit outward normal, engine axes. / 바깥 방향 단위 법선. 엔진 축. */
    float dist;            /**< Plane offset: the solid is `dot(n,p) <= dist`. / 평면 오프셋. 고체는 `dot(n,p) <= dist`입니다. */

    v3    uaxis, vaxis;    /**< Unit texture directions, engine axes. / 단위 텍스처 방향. 엔진 축. */
    float uoff, voff;      /**< Texture offset in texels, as authored. / 텍셀 단위 텍스처 오프셋. 제작값 그대로. */

    /**
     * @brief Texture scale in map units per texel, as authored. Never 0.
     *
     * KEPT AS AUTHORED rather than folded into the axes. Folding would save a
     * divide per vertex at build time and cost the ability to read a face's
     * numbers back out and recognise them as the ones in the .map -- which is
     * what anyone debugging a misaligned texture is trying to do. The divide
     * happens once per vertex when geometry is built, never per frame.
     *
     * A scale of 0 in the file is replaced by 1: it would divide by zero, and
     * every other reading of "no scale given" is 1.
     *
     * @brief 텍셀당 맵 단위로 표현된 텍스처 배율입니다. 제작값 그대로이며 0이 되지 않습니다.
     * @note 축에 접어 넣지 않고 제작값 그대로 둡니다. 접어 넣으면 빌드 시점의 정점당 나눗셈
     *       하나를 아끼는 대신, 면의 숫자를 다시 읽어 .map에 적힌 그 숫자로 알아볼 수 있는
     *       능력을 잃습니다. 어긋난 텍스처를 디버깅하는 사람이 하려는 일이 바로 그것입니다.
     *       나눗셈은 지오메트리를 만들 때 정점마다 한 번뿐이며 프레임마다가 아닙니다.
     * @note 파일의 배율 0은 1로 대체됩니다. 0으로 나누게 되며, "배율이 주어지지 않았다"의 다른
     *       모든 해석이 1이기 때문입니다.
     */
    float uscale, vscale;

    char  tex[BR_TEX];     /**< Texture name; a material name to tex.c. / 텍스처 이름. tex.c에게는 재질 이름입니다. */
} BrushFace;

/**
 * @struct Brush
 * @brief One convex solid: a run of faces in ::BrushMap::faces, plus its box.
 *
 * ENGLISH
 * -------
 * @note The bounding box is DERIVED -- ::brush_parse computes it from the face
 *       polygons once, and nothing authored contributes to it. It follows the
 *       same contract ::Sector's box does: it is an acceleration structure and
 *       never a second answer about where the brush is. An invalid box (min >
 *       max), which is what zeroed memory leaves, means "no faces bounded
 *       this" and is what an unclosed brush gets.
 *
 * 한국어
 * ------
 * @brief 하나의 볼록 고체입니다. ::BrushMap::faces 안의 연속 구간과 그 바운딩 박스입니다.
 * @note 바운딩 박스는 *파생값*입니다. ::brush_parse가 면 폴리곤으로부터 한 번 계산하며,
 *       제작된 값은 아무것도 기여하지 않습니다. ::Sector의 박스와 같은 계약을 따릅니다. 가속
 *       구조일 뿐 브러시 위치에 대한 두 번째 답이 아닙니다. 유효하지 않은 박스(min > max)는
 *       0으로 초기화된 메모리가 남기는 상태이며 "이것을 둘러싼 면이 없다"를 뜻합니다. 닫히지
 *       않은 브러시가 그 상태가 됩니다.
 */
typedef struct {
    short first_face;   /**< Index into ::BrushMap::faces. / ::BrushMap::faces의 인덱스. */
    short n_faces;      /**< How many faces from there. / 그곳부터의 면 개수. */
    v3    min, max;     /**< Bounding box in world units. Derived. / 월드 단위 바운딩 박스. 파생값. */
} Brush;

/**
 * @struct BrushEnt
 * @brief One `{ "key" "value" ... }` block, with any brushes it owns.
 *
 * ENGLISH
 * -------
 * KEY/VALUE TEXT, UNINTERPRETED. This module does not know what a `light` or a
 * `func_door` is and must not learn -- that is ::Entity's rule (see level.h)
 * and it is the reason a new entity type needs no change here. What arrives is
 * what the file said; whichever module claims the classname reads it.
 *
 * A brush entity is an entity that owns brushes: `func_door` is a door, and the
 * brushes inside its block are the door. Worldspawn is the same structure with
 * the classname `worldspawn`, and its brushes are the level. Storing them the
 * same way is what makes "a door is a group of brushes that moves" a fact about
 * the data rather than a special case in the loader.
 *
 * 한국어
 * ------
 * @brief `{ "key" "value" ... }` 블록 하나이며, 그것이 소유한 브러시를 함께 담습니다.
 *
 * 해석되지 않은 key/value 텍스트입니다. 이 모듈은 `light`나 `func_door`가 무엇인지 모르며
 * 알아서도 안 됩니다. 그것이 ::Entity의 규칙이고(level.h 참조), 새로운 엔티티 종류를 추가해도
 * 이곳을 고칠 필요가 없는 이유입니다. 도착하는 것은 파일이 말한 그대로이며, 해당 classname을
 * 차지한 모듈이 그것을 읽습니다.
 *
 * 브러시 엔티티는 브러시를 소유한 엔티티입니다. `func_door`는 문이고, 그 블록 안의 브러시들이
 * 곧 그 문입니다. worldspawn도 classname이 `worldspawn`인 동일한 구조이며, 그 브러시들이 곧
 * 레벨입니다. 둘을 같은 방식으로 저장하는 것이 "문은 움직이는 브러시 그룹이다"를 로더의 특수
 * 처리가 아니라 데이터에 관한 사실로 만듭니다.
 */
typedef struct {
    char  keys[BR_MAX_KEYS][BR_KEY];   /**< Key names, in file order. / 파일 순서대로의 키 이름. */
    char  vals[BR_MAX_KEYS][BR_VAL];   /**< Values, uninterpreted. / 값. 해석하지 않습니다. */
    short n_keys;                      /**< Pairs in use. / 사용 중인 쌍의 수. */
    short first_brush;                 /**< Index into ::BrushMap::brushes. / ::BrushMap::brushes의 인덱스. */
    short n_brushes;                   /**< How many brushes from there; 0 for a point entity. / 그곳부터의 브러시 개수. 지점 엔티티는 0입니다. */
} BrushEnt;

/**
 * @struct BrushMap
 * @brief A parsed .map: its entities, their brushes, and every face.
 *
 * ENGLISH
 * -------
 * @warning A large struct -- some hundreds of kilobytes -- and must not be a
 *          stack local, exactly as ::Level must not. It is .bss, so a file
 *          static costs nothing on disk.
 *
 * 한국어
 * ------
 * @brief 파싱된 .map입니다. 엔티티, 그 브러시들, 그리고 모든 면입니다.
 * @warning 수백 킬로바이트에 이르는 큰 구조체이며, ::Level과 마찬가지로 스택 지역 변수여서는
 *          안 됩니다. .bss이므로 파일 정적 변수로 두면 디스크 비용이 없습니다.
 */
/* The `struct BrushMap` tag is deliberate, not decoration: it is what lets
   level.h name this type in a forward declaration without including this
   header, which is the same reason ::MeshBuf and ::MdlRange carry theirs.
   `struct BrushMap` 태그는 장식이 아니라 의도적입니다. level.h가 이 헤더를 포함하지 않고
   전방 선언으로 이 타입을 지목할 수 있게 하는 것이며, ::MeshBuf와 ::MdlRange가 태그를 지닌
   것과 같은 이유입니다. */
typedef struct BrushMap {
    BrushFace faces[BR_MAX_TOTAL_FACES];  /**< Shared face pool. / 공유 면 풀. */
    int       n_faces;                    /**< Faces in use. / 사용 중인 면의 수. */
    Brush     brushes[BR_MAX_BRUSHES];    /**< Brushes, in file order. / 파일 순서대로의 브러시. */
    int       n_brushes;                  /**< Brushes in use. / 사용 중인 브러시의 수. */
    BrushEnt  ents[BR_MAX_ENTS];          /**< Entities, in file order. / 파일 순서대로의 엔티티. */
    int       n_ents;                     /**< Entities in use. / 사용 중인 엔티티의 수. */
} BrushMap;

/* --- Public function prototypes / 공개 함수 프로토타입 --- */

/**
 * @brief Parses .map text into brushes, faces and entities.
 *
 * ENGLISH
 * -------
 * @param[in]  text .map source. Standard and Valve 220 faces may be mixed
 *                  freely, including within one brush.
 * @param[in]  len  How many bytes of `text` to read, or -1 to read to its null
 *                  terminator. The same convention ::txt_copy uses.
 * @param[out] out  Receives the parsed map; cleared first, so a failed parse
 *                  leaves an empty map rather than the previous one.
 * @return How many entities were parsed. 0 means nothing usable was found.
 *
 * @note THE LENGTH IS NOT OPTIONAL IN PRACTICE. ::data_map hands back a slice
 *       of the packed maps blob, where the next map begins immediately after
 *       this one with no terminator between them. A parser that stopped only at
 *       a null byte would read the whole rest of the blob and report every
 *       later level's entities as belonging to this one.
 *
 * COORDINATES ARE CONVERTED HERE, once, at the boundary. A .map is right-handed
 * with **z up** (x east, y north, z up); this engine is right-handed with **y
 * up** (x east, y up, z south). The map from one to the other is
 *
 *     (x, y, z)_map  ->  (x, z, -y)_engine * BRUSH_UNIT
 *
 * which is a rotation of -90 degrees about x. Being a ROTATION and not a
 * reflection is the load-bearing part: handedness is preserved, so Quake's
 * plane convention -- three points clockwise seen from outside, normal pointing
 * out -- comes through unaltered and no winding anywhere has to be flipped to
 * compensate. A conversion that mirrored an axis instead would turn every
 * brush inside out, and the symptom would be a level drawn only from behind.
 *
 * @note Converting at the boundary rather than at each query is the same choice
 *       ::level_load makes for its file units: one place multiplies, and
 *       everything downstream reads world units without knowing a file format
 *       existed.
 * @warning Capacity overflows drop what does not fit and report it -- through
 *          ::DIAG_BRUSH_CAP for brushes and faces, ::DIAG_MAPENT_CAP for
 *          entities and their keys. A dropped brush is a hole in the world with
 *          no other symptom, which is exactly what diag.h exists for.
 *
 * 한국어
 * ------
 * @brief .map 텍스트를 브러시, 면, 엔티티로 파싱합니다.
 * @param[in]  text .map 원본. Standard와 Valve 220 면을 자유롭게 섞을 수 있으며 한 브러시
 *                  안에서도 그렇습니다.
 * @param[in]  len  `text`에서 읽을 바이트 수. -1이면 널 종료 문자까지 읽습니다.
 *                  ::txt_copy와 같은 관례입니다.
 * @param[out] out  파싱된 맵을 받습니다. 먼저 비우므로 파싱에 실패해도 이전 맵이 아니라 빈
 *                  맵이 남습니다.
 * @return 파싱된 엔티티의 수. 0이면 쓸 만한 것을 찾지 못했다는 뜻입니다.
 *
 * @note 실제로는 길이가 선택 사항이 아닙니다. ::data_map은 포장된 맵 블롭의 일부를 돌려주며,
 *       그곳에서는 다음 맵이 이 맵 바로 뒤에서 종료 문자 없이 시작합니다. 널 바이트에서만
 *       멈추는 파서는 블롭의 나머지 전체를 읽고, 이후 모든 레벨의 엔티티를 이 레벨의 것으로
 *       보고하게 됩니다.
 *
 * 좌표 변환은 이곳에서, 경계에서 한 번 이루어집니다. .map은 **z가 위**인 오른손 좌표계이고(x
 * 동, y 북, z 상), 이 엔진은 **y가 위**인 오른손 좌표계입니다(x 동, y 상, z 남). 둘 사이의
 * 대응은 위의 식과 같으며, 이는 x축을 중심으로 한 -90도 회전입니다.
 *
 * 그것이 반사가 아니라 *회전*이라는 점이 핵심입니다. 손잡이성이 보존되므로 Quake의 평면
 * 관례(바깥에서 볼 때 시계 방향인 세 점, 바깥을 향하는 법선)가 변형 없이 그대로 통과하며,
 * 이를 보정하려고 어딘가의 감김 방향을 뒤집을 필요가 없습니다. 축 하나를 거울처럼 뒤집는
 * 변환이었다면 모든 브러시가 안팎이 뒤집혔을 것이고, 그 증상은 뒤에서만 보이는 레벨입니다.
 *
 * @note 질의마다가 아니라 경계에서 변환하는 것은 ::level_load가 파일 단위에 대해 내리는 것과
 *       같은 선택입니다. 한 곳에서만 곱하고, 하류의 모든 것은 파일 형식이 있었다는 사실을
 *       모른 채 월드 단위를 읽습니다.
 * @warning 용량 초과 시 들어가지 않는 것을 버리고 보고합니다. 브러시와 면은
 *          ::DIAG_BRUSH_CAP, 엔티티와 그 키는 ::DIAG_MAPENT_CAP입니다. 버려진 브러시는 다른
 *          증상 없는 월드의 구멍이며, 그것이 바로 diag.h가 존재하는 이유입니다.
 */
int brush_parse(const char *text, int len, BrushMap *out);

/**
 * @brief Builds the drawable faces of a set of brushes into a vertex buffer.
 *
 * ENGLISH
 * -------
 * @param[in,out] b          Buffer receiving the geometry.
 * @param[in]     m          The parsed map.
 * @param[in]     first      First brush to build.
 * @param[in]     count      How many brushes from there.
 * @param[out]    ranges     Receives one entry per run of triangles sharing a
 *                           texture. May be NULL when the caller only wants
 *                           the vertices.
 * @param[in]     max_ranges Capacity of `ranges`; use ::BR_MAX_RANGES.
 * @return How many ranges were written.
 *
 * A RANGE OF BRUSHES rather than the whole map, because that is what a door is.
 * ::BrushEnt records the brushes it owns as a run, so `func_door`'s leaf is
 * built by passing that run -- and worldspawn's room is built by passing its
 * own. One function, and "a door is a group of brushes that moves" needs no
 * second path through here.
 *
 * @note UVs come from each face's own axes, so a texture fitted to a face in
 *       TrenchBroom arrives fitted. This is the whole of what the sector model
 *       could not do: render.c's ::planar_uv projects along a world axis and
 *       cannot be told about a door.
 * @note FACES WITH A NON-DRAWN TEXTURE ARE SKIPPED but still bound the solid.
 *       `__TB_empty` is what TrenchBroom gives a face nobody has textured yet,
 *       and `clip`, `skip` and `trigger` are Quake's invisible surfaces. Drawing
 *       them would put a flat grey slab across the level; forgetting they are
 *       still planes would let the player walk through a clip brush.
 * @warning A `max_ranges` too small MERGES the surplus into the previous run
 *          rather than dropping the geometry, and reports ::DIAG_MAT_RANGES.
 *          The wrong texture on some faces is a visible fault; a missing wall
 *          is one you fall through.
 *
 * 한국어
 * ------
 * @brief 브러시 집합의 그려지는 면들을 정점 버퍼에 생성합니다.
 * @param[in,out] b          지오메트리를 받을 버퍼.
 * @param[in]     m          파싱된 맵.
 * @param[in]     first      생성을 시작할 브러시.
 * @param[in]     count      그곳부터의 브러시 개수.
 * @param[out]    ranges     하나의 텍스처를 공유하는 삼각형 구간마다 항목 하나를 받습니다.
 *                           정점만 필요한 호출자는 NULL을 넘겨도 됩니다.
 * @param[in]     max_ranges `ranges`의 용량. ::BR_MAX_RANGES를 사용하십시오.
 * @return 기록된 구간의 개수.
 *
 * 맵 전체가 아니라 브러시 *구간*을 받는 이유는 그것이 곧 문이기 때문입니다. ::BrushEnt는
 * 자신이 소유한 브러시를 연속 구간으로 기록하므로, `func_door`의 문짝은 그 구간을 넘겨
 * 생성하고 worldspawn의 방은 자기 구간을 넘겨 생성합니다. 함수 하나면 되고, "문은 움직이는
 * 브러시 그룹이다"에 이곳을 지나는 두 번째 경로가 필요하지 않습니다.
 *
 * @note UV는 각 면 자신의 축에서 옵니다. TrenchBroom에서 면에 맞춘 텍스처가 맞춰진 채로
 *       도착합니다. 섹터 모델이 할 수 없던 것의 전부입니다. render.c의 ::planar_uv는 월드 축을
 *       따라 투영하며 문이 있다는 것을 들을 방법이 없습니다.
 * @note 그리지 않는 텍스처를 가진 면은 건너뛰되 여전히 고체를 한정합니다. `__TB_empty`는
 *       아직 아무도 텍스처를 입히지 않은 면에 TrenchBroom이 주는 이름이고, `clip`, `skip`,
 *       `trigger`는 Quake의 보이지 않는 표면입니다. 그것을 그리면 레벨을 가로지르는 평평한
 *       회색 판이 생기고, 그것이 여전히 평면이라는 사실을 잊으면 플레이어가 clip 브러시를
 *       통과합니다.
 * @warning `max_ranges`가 너무 작으면 초과분을 버리지 않고 직전 구간에 *병합*하며
 *          ::DIAG_MAT_RANGES를 보고합니다. 일부 면의 잘못된 텍스처는 눈에 보이는 결함이지만,
 *          사라진 벽은 그리로 떨어지는 결함입니다.
 */
int brush_geometry(MeshBuf *b, const BrushMap *m, int first, int count,
                   MdlRange *ranges, int max_ranges);

/**
 * @brief Whether a texture name means "solid but not drawn".
 *
 * @param[in] tex A ::BrushFace texture name.
 * @return 1 for `__TB_empty`, `clip`, `skip` and `trigger`, 0 otherwise.
 * @note Exposed so collision can be sure it is using the same list geometry
 *       used. Two copies of this rule would drift, and the symptom is a wall
 *       that draws and is not solid, or the reverse.
 *
 * @brief 텍스처 이름이 "고체이지만 그리지 않음"을 뜻하는지 여부입니다.
 * @return `__TB_empty`, `clip`, `skip`, `trigger`이면 1, 그 밖에는 0.
 * @note 충돌 판정이 지오메트리와 동일한 목록을 쓰고 있음을 확신할 수 있도록 공개합니다. 이
 *       규칙의 사본이 둘이면 서로 어긋나며, 그 증상은 그려지지만 막지 않는 벽이거나 그 반대입니다.
 */
int brush_tex_nodraw(const char *tex);

/* --- Collision / 충돌 ------------------------------------------------------ */

/**
 * @brief How close a swept box is left to the surface it stopped against.
 *
 * ENGLISH
 * -------
 * A box parked EXACTLY on a plane is a box the next trace may find already
 * inside it, because the arithmetic that put it there and the arithmetic that
 * tests it are not the same arithmetic. Backing off by a fixed amount makes
 * "touching" a state the next frame can still recognise.
 *
 * Half a centimetre, which is ::SKIN in player.c times five. player.c's value
 * is small because it only had to survive one round trip through
 * `y = floor + PLAYER_EYE` and back; this one has to survive a plane test at
 * any orientation, where the error scales with how oblique the surface is.
 *
 * 한국어
 * ------
 * @brief 스윕된 상자가 멈춰 선 표면으로부터 얼마나 떨어진 채 남겨지는지입니다.
 *
 * 평면에 *정확히* 붙여 놓은 상자는 다음 트레이스가 이미 안에 들어와 있다고 판정할 수 있는
 * 상자입니다. 그곳에 놓은 산술과 그것을 검사하는 산술이 같은 산술이 아니기 때문입니다. 일정
 * 거리만큼 물러나게 하면 "닿아 있음"이 다음 프레임도 알아볼 수 있는 상태가 됩니다.
 *
 * 0.5센티미터이며 player.c의 ::SKIN의 다섯 배입니다. player.c의 값이 작은 이유는
 * `y = floor + PLAYER_EYE`의 왕복 한 번만 견디면 되었기 때문입니다. 이 값은 임의의 방향에
 * 놓인 평면 검사를 견뎌야 하고, 그곳에서는 오차가 표면이 얼마나 비스듬한지에 따라 커집니다.
 */
#define BRUSH_SKIN 0.005f

/**
 * @struct BrushTrace
 * @brief The result of sweeping a box through the map.
 *
 * ENGLISH
 * -------
 * @note `end` is where the box may safely sit, already backed off by
 *       ::BRUSH_SKIN. Callers should use it rather than recomputing
 *       `start + (end - start) * t`, which lands on the surface exactly and is
 *       the position the next trace will call `start_solid`.
 *
 * 한국어
 * ------
 * @brief 상자를 맵에 통과시킨 결과입니다.
 * @note `end`는 상자가 안전하게 놓일 수 있는 위치이며 이미 ::BRUSH_SKIN만큼 물러나 있습니다.
 *       호출자는 `start + (end - start) * t`를 다시 계산하지 말고 이것을 써야 합니다. 그
 *       계산은 표면에 정확히 닿는 위치를 주며, 그것이 다음 트레이스가 `start_solid`라고
 *       부를 위치입니다.
 */
typedef struct {
    float t;            /**< Fraction of the sweep completed, 0..1. / 완료된 스윕의 비율, 0..1. */
    v3    end;          /**< Where the box ended up, backed off by ::BRUSH_SKIN. / 상자가 도달한 위치. ::BRUSH_SKIN만큼 물러나 있습니다. */
    v3    normal;       /**< Outward normal of what was hit; zero on a miss. / 부딪힌 것의 바깥 방향 법선. 빗나가면 0. */
    int   hit;          /**< Non-zero when something stopped the sweep. / 스윕을 멈춘 것이 있으면 0이 아닙니다. */
    int   start_solid;  /**< Non-zero when the box began inside a brush. / 상자가 브러시 안에서 시작했으면 0이 아닙니다. */
    int   brush;        /**< Index of the brush hit, or -1. / 부딪힌 브러시의 인덱스. 없으면 -1. */
} BrushTrace;

/**
 * @brief Sweeps an axis-aligned box from one point to another.
 *
 * ENGLISH
 * -------
 * @param[in]  m     The parsed map.
 * @param[in]  first First brush to collide against.
 * @param[in]  count How many brushes from there.
 * @param[in]  start Where the box's reference point begins.
 * @param[in]  end   Where it would end without obstruction.
 * @param[in]  mins  Box extent below/behind the reference point. Usually
 *                   negative on x and z, and 0 on y for a point that is the feet.
 * @param[in]  maxs  Box extent above/ahead of it.
 * @param[out] out   Receives the result; every field is written.
 *
 * A SWEEP, NOT A SERIES OF SAMPLES, and that is the whole difference from what
 * it replaces. ::level_trace walked a ray in 5cm steps and asked ::level_ground
 * at each one, so its cost scaled with distance and anything thinner than the
 * step could be crossed without being noticed. This solves for the moment of
 * contact against each plane, so a 40m move through a 1cm wall stops at the
 * wall and costs the same as a 1m move.
 *
 * THE BOX IS FOLDED INTO THE PLANES rather than swept as a shape. For each
 * plane, the box's furthest corner along the inward normal is the first part of
 * it to cross, so pushing the plane out by that corner's distance turns the box
 * sweep back into a point sweep with an exact answer. That is Quake 3's trick
 * and it is why this is a hundred lines rather than a general convex solver.
 *
 * @note Replaces ::level_ground: the floor under a point is a downward trace,
 *       and unlike a 2D query it has an answer on a slope, under a balcony and
 *       inside a room stacked over another room.
 * @note EVERY BRUSH IN THE RANGE IS SOLID. A face textured `clip` or `skip` is
 *       not drawn (see ::brush_tex_nodraw) and still bounds the volume; whether
 *       a whole ENTITY is solid -- a trigger is not -- is a question about
 *       classnames and belongs to whichever module owns them.
 * @warning A box that begins inside a brush sets `start_solid`, leaves `t` at 0
 *          and does not move. It is not pushed out. Resolving penetration is a
 *          policy decision -- which way, and how far -- and a trace that made it
 *          silently would hide the bug that caused it.
 *
 * 한국어
 * ------
 * @brief 축 정렬 상자를 한 점에서 다른 점으로 스윕합니다.
 * @param[in]  mins  기준점으로부터 아래/뒤쪽 범위. 보통 x와 z는 음수이고, 기준점이 발이면
 *                   y는 0입니다.
 * @param[out] out   결과를 받습니다. 모든 필드가 기록됩니다.
 *
 * 표본의 나열이 아니라 *스윕*이며, 그것이 대체 대상과의 차이 전부입니다. ::level_trace는
 * 광선을 5cm 간격으로 걸으며 매번 ::level_ground에 물었으므로 비용이 거리에 비례했고, 간격보다
 * 얇은 것은 알아채지 못한 채 통과할 수 있었습니다. 이 함수는 각 평면에 대해 접촉의 순간을
 * 풀어내므로, 1cm 벽을 지나는 40m 이동이 그 벽에서 멈추고 비용은 1m 이동과 같습니다.
 *
 * 상자를 형태로 스윕하지 않고 *평면에 접어 넣습니다*. 각 평면에 대해, 안쪽 법선 방향으로 가장
 * 먼 상자의 모서리가 가장 먼저 넘어가는 부분이므로, 평면을 그 모서리만큼 바깥으로 밀면 상자
 * 스윕이 정확한 답을 지닌 점 스윕으로 되돌아갑니다. Quake 3의 요령이며, 이것이 일반 볼록
 * 솔버가 아니라 백여 줄인 이유입니다.
 *
 * @note ::level_ground를 대체합니다. 한 점 아래의 바닥은 아래 방향 트레이스이며, 2D 질의와
 *       달리 경사면에서도, 발코니 아래에서도, 방 위에 쌓인 방 안에서도 답이 존재합니다.
 * @note 구간 안의 모든 브러시가 고체입니다. `clip`이나 `skip`으로 텍스처된 면은 그려지지
 *       않지만(::brush_tex_nodraw 참조) 여전히 부피를 한정합니다. *엔티티 전체*가 고체인지는
 *       (트리거는 아닙니다) classname에 관한 질문이며 그것을 소유한 모듈에 속합니다.
 * @warning 브러시 안에서 시작한 상자는 `start_solid`를 설정하고 `t`를 0으로 둔 채 움직이지
 *          않습니다. 밀어내지 않습니다. 관통을 해소하는 것은 정책 결정이며(어느 방향으로,
 *          얼마나) 그것을 조용히 수행하는 트레이스는 그 원인이 된 버그를 숨깁니다.
 */
void brush_trace(const BrushMap *m, int first, int count,
                 v3 start, v3 end, v3 mins, v3 maxs, BrushTrace *out);

/* --- Moving / 이동 --------------------------------------------------------- */

/**
 * @brief How near vertical a surface must be to stand on rather than slide off.
 *
 * ENGLISH
 * -------
 * The y component of the surface normal, so 1.0 is flat and 0 is a wall.
 * 0.7 is a little under 46 degrees, which is Quake's number and the one every
 * mapper's instinct is calibrated against: a ramp built to be walked up is
 * built shallower than 45, and one built as scenery is built steeper.
 *
 * It is a GAMEPLAY constant, not a numerical one. Nothing breaks at 0.6 or 0.8;
 * what changes is which ramps in a level are routes and which are walls, and
 * that is a decision about the game rather than about the mathematics.
 *
 * 한국어
 * ------
 * @brief 미끄러져 내리지 않고 설 수 있으려면 표면이 얼마나 수평에 가까워야 하는지입니다.
 *
 * 표면 법선의 y 성분이므로 1.0이 평평하고 0이 벽입니다. 0.7은 46도가 채 안 되며, Quake의
 * 숫자이자 모든 맵 제작자의 감각이 맞춰져 있는 값입니다. 걸어 올라가라고 만든 경사로는 45도보다
 * 완만하게, 배경으로 만든 것은 그보다 가파르게 만들어집니다.
 *
 * 수치가 아니라 *게임플레이* 상수입니다. 0.6이나 0.8에서 무엇이 망가지지는 않습니다. 달라지는
 * 것은 레벨의 어떤 경사로가 경로이고 어떤 것이 벽인가이며, 그것은 수학이 아니라 게임에 관한
 * 결정입니다.
 */
#define BRUSH_GROUND_NORMAL 0.7f

/**
 * @brief How many surfaces one move may slide along before giving up.
 *
 * ENGLISH
 * -------
 * Each bump costs a trace and resolves one contact, so the count is the number
 * of distinct surfaces a single frame's move may touch. Four is Quake's, and
 * covers a floor, a wall and the two walls of a corner -- past that the box is
 * wedged and the honest answer is to stop rather than to keep trying.
 *
 * 한국어
 * ------
 * @brief 한 번의 이동이 포기하기 전까지 몇 개의 표면을 따라 미끄러질 수 있는지입니다.
 *
 * 충돌 한 번마다 트레이스 하나를 치르고 접촉 하나를 해소하므로, 이 수는 한 프레임의 이동이
 * 닿을 수 있는 서로 다른 표면의 개수입니다. 4는 Quake의 값이며 바닥 하나, 벽 하나, 모서리의 벽
 * 둘을 감당합니다. 그것을 넘으면 상자는 끼인 것이고, 정직한 답은 계속 시도하는 것이 아니라
 * 멈추는 것입니다.
 */
#define BRUSH_MAX_BUMPS 4

/**
 * @struct BrushMove
 * @brief A box being moved through the map, and what became of it.
 *
 * ENGLISH
 * -------
 * VELOCITY IS IN AND OUT, which is the field most easily mistaken for input
 * only. A move that stops against a wall must also remove the part of the
 * velocity that was heading into it; leaving it would push into the wall again
 * next frame, and the speed would build until whatever was in the way ended and
 * it all came out at once.
 *
 * 한국어
 * ------
 * @brief 맵을 통과해 이동하는 상자와 그 결과입니다.
 *
 * 속도는 입력이자 출력이며, 입력만으로 오해하기 가장 쉬운 필드입니다. 벽에 부딪혀 멈춘 이동은
 * 그 벽으로 향하던 속도 성분도 제거해야 합니다. 남겨 두면 다음 프레임에 다시 벽을 밀고, 앞을
 * 막던 것이 끝나는 순간까지 속도가 쌓였다가 한꺼번에 터져 나옵니다.
 */
typedef struct {
    v3    pos;            /**< In: where the box is. Out: where it ended. / 입력: 상자의 위치. 출력: 도달한 위치. */
    v3    vel;            /**< In: velocity. Out: clipped by what it slid along. / 입력: 속도. 출력: 미끄러진 대상에 의해 잘린 속도. */
    v3    mins, maxs;     /**< The box, relative to `pos`. / `pos` 기준의 상자. */

    /**
     * @brief How high a surface the box may climb onto, or 0 for none.
     *
     * Only ever used when there is standable ground underfoot -- see
     * ::brush_slide_move. Passing PLAYER_STEP is the normal thing; passing 0
     * says "climb nothing", which is what a projectile or a corpse wants.
     *
     * @brief 상자가 올라설 수 있는 표면의 최대 높이이며, 0이면 오르지 않습니다.
     * @note 발밑에 설 수 있는 지면이 있을 때만 쓰입니다. ::brush_slide_move를 참조하십시오.
     */
    float step_height;

    int   grounded;       /**< Out: standing on a surface flat enough to stand on. / 출력: 설 수 있을 만큼 평평한 표면 위에 있습니다. */
    v3    ground_normal;  /**< Out: what is underfoot, zero when nothing is. / 출력: 발밑의 법선. 아무것도 없으면 0. */
    int   blocked;        /**< Out: the move ran out of bumps still pushing into something. / 출력: 무언가를 계속 밀면서 충돌 횟수를 소진했습니다. */
} BrushMove;

/**
 * @brief Moves a box for one frame, sliding along whatever it meets.
 *
 * ENGLISH
 * -------
 * @param[in]     m     The parsed map.
 * @param[in]     first First brush to collide against.
 * @param[in]     count How many brushes from there.
 * @param[in,out] mv    The box, its velocity, and what became of both.
 * @param[in]     dt    Seconds of travel.
 *
 * TIME IS SPENT, NOT DISTANCE. When a move is stopped a third of the way along,
 * the remaining two thirds of the SECOND are spent travelling in the new,
 * clipped direction -- not the remaining two thirds of the original
 * displacement. Those differ, and using distance makes a player who brushes a
 * wall slower than one who does not, for no reason they can see.
 *
 * WHAT IT REPLACES is player.c's ::move_axis, which slides by moving x and z in
 * separate calls. That works for a wall square to an axis and for nothing else:
 * a diagonal wall blocks one axis completely and passes the other, so the box
 * slides in a direction the wall does not point, at a speed the input did not
 * ask for. Clipping the velocity into the plane actually hit is what
 * generalises -- and it is the same operation that makes a ramp walkable, which
 * is why a slope needs no separate code path here.
 *
 * @note STEPPING NEEDS GROUND. `step_height` is only spent when a trace
 *       straight down from the start finds a surface at least as flat as
 *       ::BRUSH_GROUND_NORMAL. Without that condition a box climbs from
 *       mid-air, and a player who holds forward against a wall walks up it one
 *       step per frame.
 * @note The plain move and the stepped move are both attempted and the one that
 *       travelled further horizontally is kept. Stepping unconditionally can
 *       end worse than not stepping -- onto a surface that blocks the rest of
 *       the move -- and comparing costs one extra sweep against a class of
 *       stutter that is very hard to see and impossible to explain.
 * @warning `grounded` is derived here every call and never remembered, which is
 *          the rule player.c already follows for the same field. A grounded
 *          flag that persists is a player who jumps twice off one floor.
 *
 * 한국어
 * ------
 * @brief 상자를 한 프레임 동안 이동시키며, 마주치는 것을 따라 미끄러집니다.
 * @param[in,out] mv 상자와 속도, 그리고 둘의 결과.
 * @param[in]     dt 이동할 시간(초).
 *
 * 소비되는 것은 거리가 아니라 *시간*입니다. 이동이 3분의 1 지점에서 막히면, 남은 3분의 2
 * *초* 동안 새로 잘린 방향으로 이동합니다. 원래 변위의 남은 3분의 2가 아닙니다. 둘은 다르며,
 * 거리를 쓰면 벽을 스친 플레이어가 그러지 않은 플레이어보다 느려집니다. 본인에게는 보이지 않는
 * 이유로 말입니다.
 *
 * 대체하는 것은 player.c의 ::move_axis이며, 그것은 x와 z를 별도 호출로 움직여 미끄러집니다.
 * 축에 반듯한 벽에서는 동작하고 그 밖의 무엇에서도 동작하지 않습니다. 대각선 벽은 한 축을 완전히
 * 막고 다른 축을 통과시키므로, 상자는 벽이 가리키지 않는 방향으로 입력이 요구하지 않은 속도로
 * 미끄러집니다. 실제로 부딪힌 평면에 속도를 투영하는 것이 일반화되는 방법이며, 그것이 경사로를
 * 걸을 수 있게 만드는 것과 동일한 연산입니다. 이곳에 경사면을 위한 별도 경로가 필요 없는
 * 이유입니다.
 *
 * @note 계단을 오르려면 지면이 필요합니다. `step_height`는 시작 지점에서 곧장 아래로 쏜
 *       트레이스가 ::BRUSH_GROUND_NORMAL 이상으로 평평한 표면을 찾았을 때만 쓰입니다. 그
 *       조건이 없으면 상자가 공중에서 올라가고, 벽에 대고 전진을 누른 플레이어는 프레임마다 한
 *       칸씩 그 벽을 걸어 올라갑니다.
 * @note 그냥 가는 이동과 올라서는 이동을 둘 다 시도하고, 수평으로 더 멀리 간 쪽을 채택합니다.
 *       무조건 올라서면 오르지 않은 것보다 나쁘게 끝날 수 있으며(나머지 이동을 막는 표면 위로
 *       올라서는 경우) 비교는 스윕 한 번을 더 치르는 대신, 보기 매우 어렵고 설명하기는 불가능한
 *       종류의 덜컥거림을 막습니다.
 * @warning `grounded`는 호출마다 이곳에서 유도되며 기억되지 않습니다. player.c가 같은 필드에
 *          대해 이미 지키는 규칙입니다. 유지되는 접지 플래그는 바닥 하나에서 두 번 점프하는
 *          플레이어입니다.
 */
void brush_slide_move(const BrushMap *m, int first, int count,
                      BrushMove *mv, float dt);

/**
 * @brief The polygon one face bounds, in world units.
 *
 * ENGLISH
 * -------
 * @param[in]  m     The map.
 * @param[in]  brush Brush index.
 * @param[in]  face  Face index WITHIN that brush, 0..::Brush::n_faces-1.
 * @param[out] out   Receives the vertices, counter-clockwise seen from outside
 *                   (right-hand rule about ::BrushFace::normal), which is the
 *                   winding GL's front-facing test wants.
 * @param[in]  max   Capacity of `out`; use ::BR_MAX_POLY.
 * @return How many vertices were written. 0 means the face bounds nothing.
 *
 * A face's polygon is not stored in the file, because a brush is not a mesh --
 * it is planes, and the polygon is what they cut out of each other. This starts
 * from a quad covering the face's whole plane and clips it against every other
 * face of the brush; whatever survives is the face.
 *
 * @note ZERO IS A NORMAL ANSWER, not a failure. A mapper can leave a plane that
 *       every other plane already excludes -- dragging a face past the far side
 *       of a brush does exactly that -- and the file keeps it. Such a face has
 *       no polygon and is simply not drawn. It still bounds the solid, so
 *       collision must keep asking it.
 * @note DERIVED ON DEMAND rather than stored. The result is a few hundred bytes
 *       per face and would be the largest thing in ::BrushMap by far; the clip
 *       is a few dozen dot products and runs when geometry is built, not per
 *       frame. Storing it would also make it a second answer to keep in step
 *       with the planes, which is the failure ::Sector's cached box carries a
 *       warning about.
 * @warning A brush whose planes do not close a volume has faces the clip cannot
 *          bound. Those are reported through ::DIAG_BRUSH_OPEN and return 0
 *          rather than a polygon the size of the world.
 *
 * 한국어
 * ------
 * @brief 하나의 면이 둘러싸는 폴리곤이며, 월드 단위입니다.
 * @param[in]  m     대상 맵.
 * @param[in]  brush 브러시 인덱스.
 * @param[in]  face  *그 브러시 안에서의* 면 인덱스. 0..::Brush::n_faces-1.
 * @param[out] out   정점을 받습니다. 바깥에서 볼 때 반시계 방향이며(::BrushFace::normal에 대한
 *                   오른손 법칙), 이는 GL의 전면 판정이 원하는 감김 방향입니다.
 * @param[in]  max   `out`의 용량. ::BR_MAX_POLY를 사용하십시오.
 * @return 기록된 정점의 수. 0이면 그 면이 아무것도 둘러싸지 않는다는 뜻입니다.
 *
 * 면의 폴리곤은 파일에 저장되어 있지 않습니다. 브러시는 메시가 아니라 평면들이고, 폴리곤은
 * 그것들이 서로를 잘라 내고 남은 것이기 때문입니다. 이 함수는 면의 평면 전체를 덮는 사각형에서
 * 시작해 브러시의 나머지 모든 면으로 잘라 내며, 살아남은 것이 곧 그 면입니다.
 *
 * @note 0은 실패가 아니라 정상적인 답입니다. 제작자는 다른 모든 평면이 이미 배제하는 평면을
 *       남겨 둘 수 있고(브러시의 반대편 너머로 면을 끌면 정확히 그렇게 됩니다) 파일은 그것을
 *       보존합니다. 그런 면은 폴리곤이 없고 그리지 않을 뿐입니다. 여전히 고체를 한정하므로
 *       충돌 판정은 계속 물어야 합니다.
 * @note 저장하지 않고 필요할 때 유도합니다. 결과는 면당 수백 바이트이고 ::BrushMap에서 단연 가장
 *       큰 것이 될 것입니다. 절단은 내적 수십 번이며 지오메트리를 만들 때 실행되지 프레임마다가
 *       아닙니다. 저장하면 평면과 보조를 맞춰야 할 두 번째 답이 되기도 하는데, 그것이
 *       ::Sector의 캐시된 박스가 경고를 달고 있는 바로 그 실패입니다.
 * @warning 평면들이 부피를 닫지 못하는 브러시에는 절단이 한정할 수 없는 면이 생깁니다. 그런
 *          면은 ::DIAG_BRUSH_OPEN으로 보고되며, 세계만 한 폴리곤 대신 0을 반환합니다.
 */
int brush_face_poly(const BrushMap *m, int brush, int face, v3 *out, int max);

/**
 * @brief The texture coordinate a world point takes on a face.
 *
 * ENGLISH
 * -------
 * @param[in]  f     The face.
 * @param[in]  p     A world-space point, normally a vertex from ::brush_face_poly.
 * @param[in]  tex_w Texture width in pixels.
 * @param[in]  tex_h Texture height in pixels.
 * @param[out] u     Receives the u coordinate, 0..1 across one tile.
 * @param[out] v     Receives the v coordinate.
 *
 * @note The texture SIZE is a parameter rather than a field, because the face
 *       does not know it: the .map names a texture and the size is whatever the
 *       material turns out to be. Passing it in keeps this module free of tex.c
 *       and keeps the answer correct if a material is ever rebuilt at a
 *       different resolution.
 * @note This is where ::BRUSH_UNIT is undone. The axes and scales are in map
 *       units because that is what the file authored, and `p` is in metres
 *       because everything downstream is, so one division reconciles them.
 *
 * 한국어
 * ------
 * @brief 월드상의 한 점이 면 위에서 갖는 텍스처 좌표입니다.
 * @param[in]  f     대상 면.
 * @param[in]  p     월드 공간의 점. 보통 ::brush_face_poly가 준 정점입니다.
 * @param[in]  tex_w 텍스처 너비 (픽셀).
 * @param[in]  tex_h 텍스처 높이 (픽셀).
 * @param[out] u     타일 하나를 0..1로 나타내는 u 좌표를 받습니다.
 * @param[out] v     v 좌표를 받습니다.
 *
 * @note 텍스처 *크기*가 필드가 아니라 매개변수인 이유는 면이 그것을 모르기 때문입니다. .map은
 *       텍스처의 이름을 말할 뿐이고 크기는 그 재질이 만들어진 결과입니다. 인자로 받으면 이
 *       모듈이 tex.c로부터 자유로워지고, 재질이 다른 해상도로 다시 만들어져도 답이 계속
 *       맞습니다.
 * @note ::BRUSH_UNIT이 되돌려지는 곳입니다. 축과 배율은 파일이 그렇게 제작했으므로 맵 단위이고
 *       `p`는 하류의 모든 것이 그러하므로 미터입니다. 나눗셈 한 번이 둘을 화해시킵니다.
 */
void brush_face_uv(const BrushFace *f, v3 p, float tex_w, float tex_h,
                   float *u, float *v);

/**
 * @brief Looks a key up on an entity.
 *
 * @param[in] e   The entity.
 * @param[in] key Key name, e.g. "classname".
 * @return The value, or NULL when the entity does not carry that key.
 * @note NULL rather than "" so a caller can tell "absent" from "present and
 *       empty". A `targetname` of "" is a mapper's mistake worth seeing.
 *
 * @brief 엔티티에서 키를 조회합니다.
 * @return 값. 해당 키가 없으면 NULL.
 * @note ""가 아니라 NULL인 이유는 호출자가 "없음"과 "있으나 비어 있음"을 구별할 수 있게 하기
 *       위함입니다. 빈 `targetname`은 제작자의 실수이며 볼 가치가 있습니다.
 */
const char *brush_ent_value(const BrushEnt *e, const char *key);

/**
 * @brief Reads a three-number key as a world-space POSITION.
 *
 * ENGLISH
 * -------
 * @param[in]  e   The entity.
 * @param[in]  key Key name, normally "origin".
 * @param[out] out Receives the point in world units, converted from map axes.
 * @return 1 when the key was present and held three numbers, 0 otherwise.
 *
 * @note Converted the same way ::brush_parse converts a brush, so an entity
 *       placed on a brush in the editor is on that brush here. Two conversions
 *       that disagreed would put every monster a quarter-turn away from where
 *       it was placed.
 * @warning A POSITION, not any triple. `origin` is a place and converts;
 *          `_color` is three channel values and must NOT, because the axis
 *          swap would send green to blue and negate it. Named `point` rather
 *          than `vec` for exactly that reason -- a general-sounding name is
 *          what would get it pointed at a colour. When something needs a raw
 *          triple, that is a second function, not a flag on this one.
 *
 * 한국어
 * ------
 * @brief 세 숫자로 된 키를 월드 공간 *위치*로 읽습니다.
 * @param[out] out 맵 축에서 변환된 월드 단위 점을 받습니다.
 * @return 키가 존재하고 숫자 세 개를 담고 있으면 1, 그렇지 않으면 0.
 *
 * @note ::brush_parse가 브러시를 변환하는 것과 같은 방식으로 변환하므로, 에디터에서 브러시 위에
 *       놓인 엔티티는 이곳에서도 그 브러시 위에 있습니다. 두 변환이 어긋나면 모든 몬스터가
 *       배치된 곳에서 4분의 1 바퀴 떨어진 자리에 서게 됩니다.
 * @warning 임의의 삼중항이 아니라 *위치*입니다. `origin`은 장소이므로 변환되지만, `_color`는
 *          세 채널 값이므로 변환되어서는 안 됩니다. 축 교환이 초록을 파랑으로 보내고 부호까지
 *          뒤집기 때문입니다. `vec`이 아니라 `point`라고 이름 붙인 이유가 바로 그것입니다.
 *          범용처럼 들리는 이름이야말로 이 함수를 색상에 겨누게 만들 이름입니다. 가공되지 않은
 *          삼중항이 필요해지면 그것은 이 함수의 플래그가 아니라 두 번째 함수입니다.
 */
int brush_ent_point(const BrushEnt *e, const char *key, v3 *out);

/**
 * @brief Reads a numeric key, with a fallback for absent or malformed values.
 *
 * @param[in] e   The entity.
 * @param[in] key Key name, e.g. "light" or "angle".
 * @param[in] def What to return when the key is missing or not a number.
 * @return The value as a float, or `def`.
 * @note A fallback rather than an out-parameter and a status, because every
 *       caller of this has a default in mind -- an absent `angle` is 0, an
 *       absent `light` is the standard brightness -- and the two-line form at
 *       each call site was the same two lines every time.
 *
 * @brief 숫자 키를 읽으며, 값이 없거나 잘못된 경우의 대체값을 받습니다.
 * @return float로 변환한 값 또는 `def`.
 * @note 출력 인자와 상태값이 아니라 대체값을 받는 이유는, 이 함수의 모든 호출자가 이미 기본값을
 *       염두에 두고 있기 때문입니다(없는 `angle`은 0, 없는 `light`는 표준 밝기). 호출 지점마다
 *       쓰이던 두 줄짜리 형태는 매번 똑같은 두 줄이었습니다.
 */
float brush_ent_num(const BrushEnt *e, const char *key, float def);

#endif
