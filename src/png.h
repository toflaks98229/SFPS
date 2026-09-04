/**
 * @file png.h
 * @brief The one image format this project reads, decoded onto ::inflate_raw.
 *
 * ENGLISH
 * -------
 * WHY A DECODER AT ALL, when the whole project's instinct is to keep the
 * recipe and throw the result away. Because for the drawn art there is no
 * recipe: a Freedoom sprite is a picture somebody made, and the only thing
 * upstream of it is a bigger picture. Everything else here -- a material, a
 * sound, a model -- is generated from a handful of integers and the generator
 * IS the compression. A drawing has no such thing to be compressed into.
 *
 * So it was quantised instead: sixteen colours per subject, median cut,
 * run-length packed into text. That is a lossy codec, and measured against the
 * art it was crushing -- a brute carries 707 colours and got 15 -- it was
 * spending the picture to save 116KB out of a megabyte that was never in
 * danger of running out. PNG carries the drawing intact for those bytes.
 *
 * IT IS ALSO THE LAST ASSET THAT COULD NOT HOT RELOAD, and that is the half
 * of this that is not about size. models, textures, sounds, effects, levels,
 * maps and loot all re-read from disk while the game runs; sprites could not,
 * because what the game held was not the drawing but a quantised derivative
 * of it that only bake.ps1 knew how to produce. A decoder in the game closes
 * that gap: the file on disk and the bytes in the binary are now the same
 * format, so a dev build can read either.
 *
 * WHAT IT DOES NOT DO. 8-bit RGBA, non-interlaced, and nothing else. No
 * palette images, no greyscale, no 16-bit channels, no Adam7. That is not a
 * subset chosen to be small -- it is the entire set of PNGs this project
 * contains, verified over all 53 of them, and every rejected form is rejected
 * LOUDLY through ::DIAG_PNG rather than half-decoded into something that looks
 * like art damage.
 *
 * 한국어
 * ------
 * @brief 이 프로젝트가 읽는 유일한 이미지 형식이며, ::inflate_raw 위에 올렸습니다.
 *
 * *애초에 왜 디코더인가.* 이 프로젝트 전체의 본능은 레시피를 남기고 결과를 버리는
 * 것입니다. 그러나 그려진 아트에는 레시피가 없습니다. Freedoom 스프라이트는 누군가 그린
 * 그림이고, 그 위에 있는 것은 더 큰 그림뿐입니다. 이곳의 다른 모든 것(재질, 사운드, 모델)은
 * 정수 몇 개에서 생성되며 그 생성기가 *곧* 압축입니다. 그림에는 압축되어 들어갈 그런 것이
 * 없습니다.
 *
 * 그래서 대신 양자화했습니다. 주제당 16색, median cut, 런렝스로 텍스트에 담기. 그것은
 * 손실 코덱이고, 자신이 뭉개던 아트에 비추어 재면(브루트는 707색을 지녔는데 15색을 받았습니다)
 * 바닥날 위험이 전혀 없던 1메가바이트 중 116KB를 아끼려고 그림을 지불하고 있었습니다. PNG는
 * 그 바이트로 그림을 온전히 나릅니다.
 *
 * *또한 핫 리로드가 되지 않던 마지막 에셋이었으며*, 그것이 크기와 무관한 나머지 절반입니다.
 * 모델·텍스처·사운드·이펙트·레벨·맵·loot는 전부 실행 중에 디스크에서 다시 읽습니다.
 * 스프라이트만 그러지 못했는데, 게임이 들고 있던 것이 그림이 아니라 bake.ps1만이 만들 줄 아는
 * 그것의 양자화된 파생물이었기 때문입니다. 게임 안의 디코더가 그 틈을 메웁니다. 디스크의
 * 파일과 바이너리 안의 바이트가 이제 같은 형식이므로, 개발 빌드는 어느 쪽이든 읽을 수 있습니다.
 *
 * *하지 않는 일.* 8비트 RGBA, 비인터레이스, 그 외에는 없습니다. 팔레트 이미지도, 그레이스케일도,
 * 16비트 채널도, Adam7도 없습니다. 작게 만들려고 고른 부분집합이 아니라 이 프로젝트가 담고 있는
 * PNG의 *전부*이며 53장 모두에 대해 확인했습니다. 그리고 거부되는 모든 형태는 아트 손상처럼
 * 보이는 무언가로 절반쯤 디코딩되는 대신 ::DIAG_PNG를 통해 *시끄럽게* 거부됩니다.
 */
#ifndef PNG_H
#define PNG_H

/* --- Capacity limits / 용량 제한 --- */

/**
 * @brief The largest image this decoder will accept, per side.
 *
 * ENGLISH
 * -------
 * The scratch below is sized from it, and the scratch is what decides the
 * number: unfiltering needs the whole filtered stream at once, which is
 * (w * 4 + 1) * h -- one extra byte per row for the filter type.
 *
 * IT WAS 256, chosen because ::TEX_SIZE is 256 and a wall drawn at the size the
 * material buffer holds is the obvious next thing somebody tries. 704 is the
 * ward's doing: its drawing is a standing pillar, 160x688, and an authored
 * drawing taller than this is simply refused -- ::png_decode raises DIAG_PNG
 * and the sprite never reaches an atlas, which is what a 688-tall file did
 * before this number moved. 704 is 688 rounded up to a multiple of 64.
 *
 * The cost of the headroom is .bss, which is zeroed at load and occupies
 * nothing on the floppy -- the same bargain ::FX_MAX_PARTICLES and
 * LVL_MAX_RANGES already strike. It is not free in RAM: the two scratch
 * buffers this sizes go from 0.50 MB together to 3.78 MB. That is the price of
 * letting art be authored at the size it was drawn rather than at the size the
 * decoder happened to allow.
 *
 * 256이었고, ::TEX_SIZE가 256이며 재질 버퍼가 담는 크기로 그린 벽이 누구나 다음으로 시도할
 * 것이기 때문이었습니다. 704는 결계핵 때문입니다. 그 그림은 160x688의 선돌이고, 이보다 큰
 * 그림은 그냥 거절됩니다. ::png_decode가 DIAG_PNG를 올리고 스프라이트는 아틀라스에 닿지
 * 못하며, 이 수가 움직이기 전 688 높이의 파일이 실제로 그랬습니다. 704는 688을 64의 배수로
 * 올린 것입니다.
 * 여유의 값은 .bss이며 로드 시 0으로 채워지고 플로피에는 자리를 차지하지 않습니다.
 * ::FX_MAX_PARTICLES와 LVL_MAX_RANGES가 이미 맺은 것과 같은 거래입니다. RAM에서는 공짜가
 * 아닙니다. 이것이 크기를 정하는 두 임시 버퍼가 합쳐 0.50 MB에서 3.78 MB가 됩니다. 그것이
 * 그림을 디코더가 허용한 크기가 아니라 그려진 크기로 저작하게 하는 값입니다.
 *
 * 한국어
 * ------
 * @brief 이 디코더가 받아들이는 최대 이미지 크기(한 변).
 *
 * 아래의 임시 버퍼를 이 값으로 잡으며, 그 버퍼가 이 숫자를 정합니다. 언필터링은 필터된
 * 스트림 전체를 한꺼번에 필요로 하는데 그 크기가 (w * 4 + 1) * h이기 때문입니다. 행마다
 * 필터 종류를 담는 1바이트가 더 붙습니다.
 *
 * 실제 최대 저작 셀이 192x104인데도 256인 이유는 ::TEX_SIZE가 256이고, 재질 버퍼가 담는
 * 크기로 그린 벽이 누군가 다음에 시도할 뻔한 것이기 때문입니다. 그 여유의 비용은 .bss이며,
 * 로드 시 0으로 채워지고 플로피 용량을 차지하지 않습니다. ::FX_MAX_PARTICLES와
 * LVL_MAX_RANGES가 이미 맺고 있는 것과 같은 거래입니다.
 */
#define PNG_MAX_SIDE 704

/**
 * @brief The largest compressed stream a file may carry, bytes.
 *
 * ENGLISH: Gathered from the IDAT chunks before inflating, because a PNG may
 * split its zlib stream across several of them and ::inflate_raw takes one
 * contiguous run. Every file in this project has exactly one IDAT and the
 * largest is 13KB, so this is ten times the measured worst case rather than a
 * guess -- and a stream past it is reported rather than truncated, which would
 * inflate into a half-decoded picture.
 *
 * 한국어: 펼치기 전에 IDAT 청크들에서 모읍니다. PNG는 zlib 스트림을 여러 IDAT에 나눠 담을
 * 수 있고 ::inflate_raw는 연속된 하나를 받기 때문입니다. 이 프로젝트의 모든 파일은 IDAT가
 * 정확히 하나이고 가장 큰 것이 13KB이므로, 이 값은 짐작이 아니라 실측된 최악의 경우의 열
 * 배입니다. 이를 넘는 스트림은 잘리지 않고 보고됩니다. 자르면 절반만 디코딩된 그림으로
 * 펼쳐지기 때문입니다.
 */
#define PNG_MAX_ZDATA (128 * 1024)

/* --- Public function prototypes / 공개 함수 프로토타입 --- */

/**
 * @brief Decodes an 8-bit RGBA PNG into a caller-supplied buffer.
 *
 * ENGLISH
 * -------
 * @param[in]  src     The whole file, signature included.
 * @param[in]  src_len Its length in bytes.
 * @param[out] dst     Receives `w * h * 4` bytes, RGBA, top row first.
 * @param[in]  dst_cap Its capacity.
 * @param[out] out_w   Width, written only on success.
 * @param[out] out_h   Height, written only on success.
 * @return Non-zero on success. Zero leaves @p dst untouched in every way that
 *         matters -- a partially written buffer is never reported as success.
 *
 * @note EVERY REJECTION RAISES ::DIAG_PNG. A drawing that fails to decode is
 *       a monster that shows its generated silhouette instead, which looks
 *       exactly like a drawing nobody has made yet -- so the difference
 *       between "not drawn" and "would not decode" has to be counted or it
 *       cannot be told.
 * @note Not re-entrant: the filtered stream and the gathered zlib data live in
 *       file-scope scratch. Sprites are decoded once at atlas build time, on
 *       one thread, which is the only caller there has ever been.
 *
 * 한국어
 * ------
 * @brief 8비트 RGBA PNG를 호출자가 제공한 버퍼로 디코딩합니다.
 * @param[in]  src     시그니처를 포함한 파일 전체.
 * @param[in]  src_len 바이트 길이.
 * @param[out] dst     `w * h * 4` 바이트를 받습니다. RGBA이며 위쪽 행부터입니다.
 * @param[in]  dst_cap 그 용량.
 * @param[out] out_w   너비. 성공했을 때만 기록됩니다.
 * @param[out] out_h   높이. 성공했을 때만 기록됩니다.
 * @return 성공하면 0이 아닙니다. 0이면 절반만 기록된 버퍼가 성공으로 보고되는 일은
 *         결코 없습니다.
 *
 * @note *모든 거부가 ::DIAG_PNG를 올립니다.* 디코딩에 실패한 그림은 생성된 실루엣을 대신
 *       보여 주는 몬스터가 되는데, 그것은 아직 아무도 그리지 않은 그림과 똑같이 보입니다.
 *       그래서 "그려지지 않음"과 "디코딩되지 않음"의 차이는 세지 않으면 구분할 수 없습니다.
 * @note 재진입 불가입니다. 필터된 스트림과 모아 둔 zlib 데이터가 파일 스코프의 임시 공간에
 *       삽니다. 스프라이트는 아틀라스를 만들 때 한 스레드에서 한 번 디코딩되며, 지금까지
 *       존재한 호출자는 그것뿐입니다.
 */
int png_decode(const unsigned char *src, int src_len,
               unsigned char *dst, int dst_cap, int *out_w, int *out_h);

#endif
