/**
 * @file png.c
 * @brief Implements ::png_decode. See png.h for why this exists at all.
 *
 * ENGLISH
 * -------
 * Four steps and no more: walk the chunks, gather the IDATs, inflate, unfilter.
 * The third is ::inflate_raw, which was already here for the baked assets and
 * is the reason this decoder is small -- DEFLATE is the whole of PNG's
 * compression, and everything this file adds is the container around it plus
 * five one-line predictors.
 *
 * THE ADLER-32 IS NOT CHECKED, and that is a decision rather than a gap.
 * A corrupt stream fails in ::inflate_raw, which already refuses a malformed
 * one and refuses to overrun its buffer; what a checksum would catch on top of
 * that is a stream that decompresses cleanly into the wrong pixels, which is
 * neither a failure mode this project has nor one worth a second pass over
 * every byte at load. The two-byte zlib header is skipped for the same reason:
 * the only thing in it this decoder could act on is the compression method,
 * and a PNG with a method other than DEFLATE is not a PNG.
 *
 * 한국어
 * ------
 * @brief ::png_decode를 구현합니다. 애초에 왜 존재하는지는 png.h를 참조하십시오.
 *
 * 네 단계이고 그 이상은 없습니다. 청크를 훑고, IDAT를 모으고, 펼치고, 필터를 되돌립니다.
 * 세 번째가 ::inflate_raw이며, 구운 에셋을 위해 이미 이곳에 있던 것이고 이 디코더가 작은
 * 이유입니다. DEFLATE가 PNG 압축의 전부이며, 이 파일이 더하는 것은 그것을 둘러싼 컨테이너와
 * 한 줄짜리 예측기 다섯 개뿐입니다.
 *
 * *Adler-32는 검사하지 않으며*, 그것은 빠뜨린 것이 아니라 결정입니다. 손상된 스트림은
 * ::inflate_raw에서 실패합니다. 그것은 이미 잘못된 스트림을 거부하고 버퍼를 넘어서기를
 * 거부합니다. 체크섬이 그 위에서 더 잡아 줄 것은 깨끗하게 압축 해제되어 *틀린 픽셀*이 되는
 * 스트림인데, 그것은 이 프로젝트에 있는 실패 양상도 아니고 로드할 때 모든 바이트를 한 번 더
 * 훑을 값어치가 있는 것도 아닙니다. 2바이트 zlib 헤더를 건너뛰는 이유도 같습니다. 이 디코더가
 * 그 안에서 반응할 수 있는 유일한 것은 압축 방식인데, DEFLATE가 아닌 PNG는 PNG가 아닙니다.
 */

#include "png.h"
#include "inflate.h"
#include "diag.h"

/* --- Static variable definitions / 정적 변수 정의 --- */

/**
 * @brief The filtered stream, one extra byte per row for its filter type.
 *
 * ENGLISH: .bss, so it is zeroed at load and costs the floppy nothing -- the
 * same bargain FX_MAX_PARTICLES strikes. It is the reason ::PNG_MAX_SIDE has
 * a value at all.
 *
 * 한국어: .bss이므로 로드 시 0으로 채워지고 플로피 용량이 들지 않습니다.
 * FX_MAX_PARTICLES가 맺는 것과 같은 거래이며, ::PNG_MAX_SIDE가 값을 갖는 이유입니다.
 */
static unsigned char g_rows[(PNG_MAX_SIDE * 4 + 1) * PNG_MAX_SIDE];

/** @brief The IDAT payloads, gathered into one run for ::inflate_raw. / ::inflate_raw를 위해 하나로 모은 IDAT 페이로드. */
static unsigned char g_zdata[PNG_MAX_ZDATA];

/* --- Static function definitions / 정적 함수 정의 --- */

/** @brief A big-endian 32-bit field, which is every length and dimension in a PNG. / 빅엔디안 32비트 필드. PNG의 모든 길이와 크기가 이것입니다. */
static unsigned be32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] <<  8) |  (unsigned)p[3];
}

/** @brief Whether a four-byte chunk type is the one named. / 4바이트 청크 종류가 지목한 것인지. */
static int is_chunk(const unsigned char *p, const char *name) {
    return p[0] == (unsigned char)name[0] && p[1] == (unsigned char)name[1] &&
           p[2] == (unsigned char)name[2] && p[3] == (unsigned char)name[3];
}

/**
 * @brief Paeth, the one predictor that is not one line of arithmetic.
 *
 * ENGLISH: PNG's own definition, unchanged. It picks whichever of the three
 * neighbours the linear estimate a + b - c lands nearest, which is what makes
 * it beat the other four on the edges inside a drawing rather than only on
 * its flat runs.
 *
 * 한국어: PNG 자신의 정의 그대로입니다. 선형 추정값 a + b - c가 가장 가까이 떨어지는 이웃을
 * 고르며, 그 덕분에 평평한 구간뿐 아니라 그림 *안쪽의 경계*에서 나머지 넷을 이깁니다.
 */
static int paeth(int a, int b, int c) {
    int p  = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

/* --- Public function definitions / 공개 함수 정의 --- */

int png_decode(const unsigned char *src, int src_len,
               unsigned char *dst, int dst_cap, int *out_w, int *out_h) {
    static const unsigned char SIG[8] = { 0x89,'P','N','G','\r','\n',0x1a,'\n' };

    if (!src || !dst || src_len < 8 + 25) { DIAG(DIAG_PNG); return 0; }
    for (int i = 0; i < 8; i++)
        if (src[i] != SIG[i]) { DIAG(DIAG_PNG); return 0; }

    int w = 0, h = 0, zlen = 0, have_ihdr = 0;

    /* --- the chunk walk --------------------------------------------------
       Length, type, payload, CRC. Anything not IHDR or IDAT is skipped by its
       own length rather than by a list of what to skip, which is the property
       that makes the format extensible and the reason a decoder does not have
       to know what a chunk it has never heard of contains.
       길이, 종류, 페이로드, CRC입니다. IHDR도 IDAT도 아닌 것은 건너뛸 목록이 아니라 자기
       길이로 건너뜁니다. 그 성질이 이 형식을 확장 가능하게 만들며, 디코더가 한 번도 들어 본
       적 없는 청크의 내용을 알 필요가 없는 이유입니다. */
    for (int i = 8; i + 12 <= src_len; ) {
        unsigned len = be32(src + i);
        const unsigned char *type = src + i + 4;
        const unsigned char *body = src + i + 8;

        /* The length is a 31-bit field by the spec, and it is read from the
           file: a corrupt one that wrapped would step the cursor backwards and
           spin here for ever. Checked against what is left rather than against
           a constant, so this is the same test for a truncated file.
           명세상 길이는 31비트 필드이고 파일에서 읽습니다. 손상되어 뒤집힌 값은 커서를
           뒤로 옮겨 이곳에서 영원히 돌게 합니다. 상수가 아니라 *남은 바이트*에 대고
           검사하므로, 잘린 파일에 대해서도 같은 검사가 됩니다. */
        if (len > (unsigned)(src_len - i - 12)) { DIAG(DIAG_PNG); return 0; }

        if (is_chunk(type, "IHDR")) {
            if (len < 13) { DIAG(DIAG_PNG); return 0; }
            w = (int)be32(body);
            h = (int)be32(body + 4);

            /* bit depth 8, colour type 6 (RGBA), compression 0, filter 0,
               interlace 0. Every other combination is a PNG this decoder
               cannot read, and reading it wrong would produce something that
               looks like damaged art rather than like a refusal.
               비트 깊이 8, 색 종류 6(RGBA), 압축 0, 필터 0, 인터레이스 0입니다. 그 밖의
               모든 조합은 이 디코더가 읽을 수 없는 PNG이며, 잘못 읽으면 거부가 아니라
               손상된 아트처럼 보이는 무언가가 나옵니다. */
            if (body[8] != 8 || body[9] != 6 ||
                body[10] != 0 || body[11] != 0 || body[12] != 0) {
                DIAG(DIAG_PNG); return 0;
            }
            if (w <= 0 || h <= 0 ||
                w > PNG_MAX_SIDE || h > PNG_MAX_SIDE) { DIAG(DIAG_PNG); return 0; }
            if (w * h * 4 > dst_cap) { DIAG(DIAG_PNG); return 0; }
            have_ihdr = 1;
        }
        else if (is_chunk(type, "IDAT")) {
            /* GATHERED RATHER THAN POINTED AT. A PNG may split its zlib stream
               across any number of IDATs and ::inflate_raw takes one contiguous
               run; every file this project ships has exactly one, so the copy
               is usually the whole stream moved once, and the alternative is a
               decoder that works until somebody saves a big drawing from a tool
               that chunks at 64KB.
               가리키지 않고 *모읍니다.* PNG는 zlib 스트림을 임의 개수의 IDAT에 나눠 담을 수
               있고 ::inflate_raw는 연속된 하나를 받습니다. 이 프로젝트가 담고 있는 모든
               파일은 정확히 하나이므로 대개 복사는 스트림 전체를 한 번 옮기는 것이고, 그러지
               않으면 누군가 64KB로 쪼개는 도구에서 큰 그림을 저장하기 전까지만 동작하는
               디코더가 됩니다. */
            if (!have_ihdr) { DIAG(DIAG_PNG); return 0; }
            if (zlen + (int)len > PNG_MAX_ZDATA) { DIAG(DIAG_PNG); return 0; }
            for (unsigned k = 0; k < len; k++) g_zdata[zlen + (int)k] = body[k];
            zlen += (int)len;
        }
        else if (is_chunk(type, "IEND")) break;

        i += 12 + (int)len;
    }

    /* The zlib wrapper: two header bytes in front, four of Adler-32 behind.
       DEFLATE is self-terminating so the trailer needs no trimming -- see the
       note at the top of this file for why it is not checked.
       zlib 껍데기입니다. 앞에 헤더 2바이트, 뒤에 Adler-32 4바이트입니다. DEFLATE는 스스로
       끝나므로 꼬리를 잘라 낼 필요가 없습니다. 검사하지 않는 이유는 이 파일 머리글의 설명을
       참조하십시오. */
    if (!have_ihdr || zlen < 3) { DIAG(DIAG_PNG); return 0; }

    int stride  = w * 4;
    int filtered = (stride + 1) * h;

    int got = inflate_raw(g_rows, filtered, g_zdata + 2, zlen - 2);
    if (got != filtered) { DIAG(DIAG_PNG); return 0; }

    /* --- unfiltering -----------------------------------------------------
       Each row carries its own predictor and is reconstructed against the row
       ABOVE IT AS ALREADY RECONSTRUCTED, not as it arrived -- which is why
       this walks forward and writes into `dst` rather than fixing `g_rows` in
       place and copying afterwards: `dst` is where the reconstructed rows are,
       so it is what the next row has to read.
       행마다 자기 예측기를 지니며, 위 행의 *도착한 모습이 아니라 이미 복원된 모습*에 대고
       복원됩니다. 이 루프가 앞으로 나아가며 `g_rows`를 제자리에서 고친 뒤 복사하지 않고
       `dst`에 쓰는 이유가 그것입니다. 복원된 행이 있는 곳이 `dst`이므로, 다음 행이 읽어야
       하는 것도 그곳입니다. */
    for (int y = 0; y < h; y++) {
        const unsigned char *in = g_rows + y * (stride + 1);
        int f = *in++;
        unsigned char *cur = dst + y * stride;
        const unsigned char *up = (y > 0) ? dst + (y - 1) * stride : 0;

        for (int x = 0; x < stride; x++) {
            /* a: the pixel to the left, b: the one above, c: above-left. Off
               the edge they are zero, which the spec defines and which is why
               the first row and the first pixel need no special case.
               a는 왼쪽 픽셀, b는 위쪽, c는 왼쪽 위입니다. 가장자리 밖에서는 0이며, 명세가
               그렇게 정의하기에 첫 행과 첫 픽셀에 특별한 처리가 필요 없습니다. */
            int a = (x >= 4) ? cur[x - 4] : 0;
            int b = up ? up[x] : 0;
            int c = (up && x >= 4) ? up[x - 4] : 0;
            int v = in[x];

            switch (f) {
            case 0: break;                                  /* None    */
            case 1: v += a;                       break;    /* Sub     */
            case 2: v += b;                       break;    /* Up      */
            case 3: v += (a + b) >> 1;            break;    /* Average */
            case 4: v += paeth(a, b, c);          break;    /* Paeth   */
            default: DIAG(DIAG_PNG); return 0;
            }
            cur[x] = (unsigned char)v;
        }
    }

    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return 1;
}
