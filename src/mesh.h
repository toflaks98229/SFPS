/**
 * @file mesh.h
 * @brief Authored meshes -- the escape hatch from extruded silhouettes.
 *
 * ENGLISH
 * -------
 * Extrusion and lathe cover weapons and props, and they generate their own
 * UVs, which is why they are the default. What they cannot do is an arbitrary
 * shape with a hand-painted layout. This path takes that: model in Blender,
 * unwrap there, export .obj.
 *
 * The game never reads .obj files. bake.ps1 converts each of them into one
 * integer mesh text embedded as ASSET_MESHES, so no float parser exists
 * anywhere in the project and a vertex costs ~11 bytes instead of ~28.
 *
 * The baked text format is:
 *
 *     x <name>            begin a mesh
 *     p <x y z> ...       positions, 1/1000 units
 *     t <u v> ...         texture coordinates, 1/1000
 *     f <pi ti> x3 ...    triangles, indices into those two lists
 *
 * 한국어
 * ------
 * 압출(extrusion)과 선반(lathe) 방식은 무기와 소품을 처리하며 UV를 자체적으로
 * 생성하므로 기본 방식으로 사용됩니다. 이 방식으로 불가능한 것은 직접 그린
 * 레이아웃을 가진 임의의 형상입니다. 이 경로가 그것을 담당합니다. Blender에서
 * 모델링하고, 그곳에서 UV를 펼친 뒤, .obj로 내보냅니다.
 *
 * 게임은 .obj 파일을 직접 읽지 않습니다. bake.ps1이 각 파일을 ASSET_MESHES로
 * 내장되는 하나의 정수 메시 텍스트로 변환하므로, 프로젝트 어디에도 부동소수점
 * 파서가 존재하지 않으며 정점 하나당 약 28바이트가 아닌 약 11바이트를 사용합니다.
 *
 * 내장되는 텍스트 형식은 다음과 같습니다:
 *
 *     x <name>            메시 시작
 *     p <x y z> ...       위치, 1/1000 단위
 *     t <u v> ...         텍스처 좌표, 1/1000 단위
 *     f <pi ti> x3 ...    삼각형. 위 두 목록에 대한 인덱스
 */
#ifndef MESH_H
#define MESH_H

#include "render.h"

/**
 * @brief Appends the named mesh to a vertex buffer, expanded into triangles.
 *
 * ENGLISH
 * -------
 * @brief Appends the named mesh to a vertex buffer, expanded into triangles.
 * @param[in,out] b    Buffer to append to. Existing contents are preserved,
 *                     so several meshes may be accumulated into one buffer.
 * @param[in]     name Mesh name to look up in the baked ASSET_MESHES text.
 * @return The number of triangles appended, or 0 if there is no such mesh.
 * @retval 0 The name was not found, the mesh text is empty, or scratch
 *           allocation failed. The buffer is left unchanged in every case.
 * @note Normals are computed flat per face rather than read from the source
 *       asset: the exporter's normals would be a third of the vertex data,
 *       and this project's look wants faceting regardless.
 * @note V coordinates are flipped on import. OBJ's V axis runs bottom-up
 *       while a texture uploaded through glTexImage2D has its first row at
 *       v = 0, so without the flip every authored mapping arrives upside down.
 * @warning Allocates and frees process-heap scratch space internally; the
 *          caller owns nothing after this returns. Requires a valid `MeshBuf`
 *          initialised by `mb_init`.
 *
 * 한국어
 * ------
 * @brief 지정된 이름의 메시를 삼각형으로 확장하여 정점 버퍼에 추가합니다.
 * @param[in,out] b    추가 대상 버퍼. 기존 내용은 보존되므로 여러 메시를 하나의
 *                     버퍼에 누적할 수 있습니다.
 * @param[in]     name 내장된 ASSET_MESHES 텍스트에서 찾을 메시 이름.
 * @return 추가된 삼각형의 수. 해당 이름의 메시가 없으면 0입니다.
 * @retval 0 이름을 찾지 못했거나, 메시 텍스트가 비어 있거나, 임시 메모리 할당에
 *           실패한 경우. 모든 경우에 버퍼는 변경되지 않습니다.
 * @note 법선은 원본 에셋에서 읽지 않고 면 단위로 평평하게(flat) 계산됩니다.
 *       내보내기 도구의 법선은 정점 데이터의 3분의 1을 차지하며, 이 프로젝트의
 *       비주얼은 어차피 각진 형태를 지향하기 때문입니다.
 * @note V 좌표는 가져오는 시점에 반전됩니다. OBJ의 V축은 아래에서 위로 향하지만
 *       glTexImage2D로 업로드된 텍스처는 첫 행이 v = 0에 위치하므로, 반전하지
 *       않으면 제작된 모든 매핑이 뒤집힌 채로 들어옵니다.
 * @warning 내부적으로 프로세스 힙의 임시 공간을 할당하고 해제합니다. 이 함수가
 *          반환된 후 호출자가 소유하는 자원은 없습니다. `mb_init`으로 초기화된
 *          유효한 `MeshBuf`가 필요합니다.
 */
int mesh_build(MeshBuf *b, const char *name);

#endif
