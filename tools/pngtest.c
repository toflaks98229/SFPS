/* pngtest -- the PNG decoder, against every drawing this project ships.
 *
 * ENGLISH
 * -------
 * A DECODER IS THE ONE THING THAT CANNOT BE CHECKED BY LOOKING. A picture that
 * comes out wrong looks like art: the shotgun is a little dark, the imp's
 * shoulder is the wrong green, and every one of those is a thing an artist
 * might have drawn on purpose. The failures worth catching -- a filter type
 * reconstructed against the wrong neighbour, a row read one byte off, a Paeth
 * predictor that picks the second-nearest -- all produce images, and images do
 * not announce that they are wrong.
 *
 * So this compares against a reference rather than against an eye. The
 * reference is written by bake.ps1, which decodes the same PNGs in PowerShell
 * through an entirely separate implementation: two decoders that agree on
 * 523,520 pixels are not agreeing by accident.
 *
 * WHAT EACH SECTION IS FOR
 *   the shipped set   every drawing decodes, at the size its atlas cell wants
 *   the filters       all five predictors are actually exercised by that set
 *   refusals          a PNG this decoder cannot read is refused, not guessed
 *
 * 한국어
 * ------
 * 디코더는 *보는 것으로 검사할 수 없는* 단 하나입니다. 잘못 나온 그림은 아트처럼 보입니다.
 * 샷건이 조금 어둡고, 임프의 어깨가 다른 초록이며, 그 각각은 아티스트가 일부러 그렸을 수도
 * 있는 것입니다. 잡을 가치가 있는 실패(잘못된 이웃에 대고 복원된 필터 종류, 한 바이트
 * 어긋나게 읽힌 행, 두 번째로 가까운 것을 고르는 Paeth 예측기)는 전부 *이미지*를 만들어
 * 내며, 이미지는 자기가 틀렸다고 알리지 않습니다.
 *
 * 그래서 눈이 아니라 기준값에 대고 비교합니다. 기준값은 bake.ps1이 씁니다. 그것은 같은
 * PNG를 PowerShell의 완전히 별개인 구현으로 디코딩합니다. 523,520개 픽셀에 대해 일치하는
 * 두 디코더가 우연히 일치하는 것은 아닙니다.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png.h"
#include "inflate.h"
#include "diag.h"
/* plat_exe_dir -- the test runner's working directory is not the source tree,
   and a bare relative path finds nothing from it. data.c resolves against the
   exe's parent for exactly this reason; so does this.
   plat_exe_dir입니다. 테스트 러너의 작업 디렉터리는 소스 트리가 아니며, 맨 상대 경로는
   그곳에서 아무것도 찾지 못합니다. data.c가 바로 그 이유로 exe의 부모를 기준으로
   해석하며, 이것도 그렇게 합니다. */
#include "plat.h"

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void oki(int cond, const char *what, int got, int want) {
    printf("  %-58s %5d / %-5d %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* The project root, with its trailing separator, resolved once. */
static const char *root(void) {
    static char buf[512];
    static int  done;
    if (!done) { plat_exe_dir(buf, (int)sizeof buf); done = 1; }
    return buf;
}

/* `<root><rel>`, in a rotating pair of buffers so two can be live at once. */
static const char *from_root(const char *rel) {
    static char buf[2][640];
    static int  which;
    char *b = buf[which = !which];
    snprintf(b, sizeof buf[0], "%s%s", root(), rel);
    return b;
}

/* The whole file, or NULL. Freed by the caller. */
static unsigned char *slurp(const char *path, int *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = 0; }
    fclose(f);
    if (b) *len = (int)n;
    return b;
}

static unsigned char g_px[PNG_MAX_SIDE * PNG_MAX_SIDE * 4];

/* One drawing, and what the reference says it should come out as.
 *
 * The list is read from a file bake.ps1 writes rather than being typed here:
 * a table of expected sizes in this file would be a second place the art is
 * described, and it would go stale the first time somebody adds a frame --
 * which is the failure the sprite name list in fxtest keeps demonstrating.
 * 목록은 이곳에 타이핑하지 않고 bake.ps1이 쓰는 파일에서 읽습니다. 이 파일 안의 기대 크기
 * 표는 아트가 기술되는 두 번째 장소가 되며, 누군가 프레임을 추가하는 순간 낡습니다.
 * fxtest의 스프라이트 이름 목록이 계속 보여 주고 있는 실패입니다. */
int main(int argc, char **argv) {
    /* COPIED, not pointed at. ::from_root hands back one of two rotating
       buffers so two paths can be live at once, and this one has to outlive
       every drawing it names -- pointing at it meant the second section
       reopened whatever the last drawing's path had overwritten, and every
       count below came out zero.
       가리키지 않고 *복사합니다.* ::from_root는 두 경로가 동시에 살아 있을 수 있도록 회전하는
       버퍼 둘 중 하나를 돌려주는데, 이 경로는 자신이 지목하는 모든 그림보다 오래 살아야
       합니다. 가리키기만 했더니 두 번째 절이 마지막 그림의 경로가 덮어쓴 것을 다시 열었고,
       아래의 모든 개수가 0으로 나왔습니다. */
    char refbuf[640];
    snprintf(refbuf, sizeof refbuf, "%s",
             (argc > 1) ? argv[1] : from_root("build/png_ref.txt"));
    const char *ref = refbuf;

    printf("png\n\nthe shipped drawings decode\n");

    FILE *r = fopen(ref, "rb");
    if (!r) {
        printf("  no reference at %s -- run bake.ps1 first\n", ref);
        return 1;
    }

    int n = 0;
    char path[512];
    int want_w, want_h;
    unsigned long want_sum;

    /* `<path> <w> <h> <checksum>` per line, written by bake.ps1. The checksum
       is a plain running sum over every RGBA byte -- not a hash, because what
       it has to catch is a decoder that is wrong, not one that is hostile. */
    while (fscanf(r, "%511s %d %d %lu", path, &want_w, &want_h, &want_sum) == 4) {
        int len = 0;
        unsigned char *file = slurp(from_root(path), &len);
        if (!file) { oki(0, path, 0, 1); continue; }

        int w = 0, h = 0;
        int good = png_decode(file, len, g_px, (int)sizeof g_px, &w, &h);

        unsigned long sum = 0;
        for (int i = 0; i < w * h * 4; i++) sum += g_px[i];

        if (!good || w != want_w || h != want_h || sum != want_sum) {
            printf("  %-40s %dx%d sum %lu / want %dx%d sum %lu  FAIL\n",
                   path, w, h, sum, want_w, want_h, want_sum);
            fails++;
        }

        free(file);
        n++;
    }
    fclose(r);

    oki(n > 0, "the reference names some drawings", n, n ? n : 1);
    ok(fails == 0, "every one decodes to the byte the reference expects");

    /* --- every predictor, reconstructed exactly -------------------------
       THE ART DOES NOT USE ALL FIVE. Measured over the shipped set below,
       filter 3 (Average) never appears -- so an Average branch that
       reconstructed against the wrong neighbour would ship, pass every check
       in this file, and wait for the first drawing whose encoder happened to
       pick it. A test that only checks what the current art exercises is a
       test that gets weaker every time the art gets simpler.

       So the fixture is built here instead: a small image, each row filtered
       with a different predictor, and the decoder has to give back exactly
       what went in. DEFLATE's STORED block is what makes that possible without
       a compressor -- the project has an inflater and no deflater, and a
       stored block is a length, its complement, and the bytes.

       CRCs ARE LEFT ZERO, deliberately. png.c does not verify them (its header
       says why), so filling them would be testing PowerShell's opinion of this
       fixture rather than the decoder's. It is a PNG for this decoder, not for
       the world, and saying so here is cheaper than a CRC32 nothing reads.

       아트가 다섯을 다 쓰지 않습니다. 아래에서 배포 집합에 대해 재어 보면 필터 3(Average)은
       한 번도 나오지 않습니다. 그래서 잘못된 이웃에 대고 복원하는 Average 분기는 출시되고,
       이 파일의 모든 검사를 통과하고, 인코더가 우연히 그것을 고르는 첫 그림을 기다립니다.
       지금 아트가 쓰는 것만 검사하는 검사는 아트가 단순해질 때마다 약해지는 검사입니다.

       그래서 픽스처를 이곳에서 만듭니다. 작은 이미지의 행마다 다른 예측기로 필터를 걸고,
       디코더가 들어간 것을 정확히 돌려주어야 합니다. 압축기 없이 그것을 가능하게 하는 것이
       DEFLATE의 *stored* 블록입니다. 이 프로젝트에는 펼치는 쪽만 있고 압축하는 쪽은 없으며,
       stored 블록은 길이와 그 보수와 바이트가 전부입니다.

       CRC는 의도적으로 0으로 둡니다. png.c가 검사하지 않으므로(이유는 그 헤더에 있습니다)
       채우는 것은 디코더가 아니라 이 픽스처에 대한 PowerShell의 의견을 검사하는 일이 됩니다.
       이것은 세상을 위한 PNG가 아니라 *이 디코더를 위한* PNG이며, 그렇다고 적어 두는 편이
       아무도 읽지 않는 CRC32보다 쌉니다. */
    printf("\nevery predictor reconstructs exactly\n");
    {
        enum { FW = 6, FH = 5, FSTRIDE = FW * 4 };
        unsigned char want[FH][FSTRIDE], filt[FH][FSTRIDE + 1];

        /* Varied on purpose: a source that is flat decodes correctly under a
           predictor that is wrong, because every neighbour holds the same
           value. */
        for (int y = 0; y < FH; y++)
            for (int x = 0; x < FSTRIDE; x++)
                want[y][x] = (unsigned char)(17 * x + 53 * y + (x * y) % 7);

        for (int y = 0; y < FH; y++) {
            int f = y % 5;
            filt[y][0] = (unsigned char)f;
            for (int x = 0; x < FSTRIDE; x++) {
                int a = (x >= 4) ? want[y][x - 4] : 0;
                int b = (y > 0)  ? want[y - 1][x] : 0;
                int c = (y > 0 && x >= 4) ? want[y - 1][x - 4] : 0;
                int pr = 0;
                switch (f) {
                case 1: pr = a; break;
                case 2: pr = b; break;
                case 3: pr = (a + b) >> 1; break;
                case 4: {
                    int pp = a + b - c;
                    int pa = pp > a ? pp - a : a - pp;
                    int pb = pp > b ? pp - b : b - pp;
                    int pc = pp > c ? pp - c : c - pp;
                    pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                } break;
                }
                filt[y][x + 1] = (unsigned char)((want[y][x] - pr) & 0xff);
            }
        }

        unsigned char png[512];
        int n = 0;
        static const unsigned char SIG[8] = { 0x89,'P','N','G','\r','\n',0x1a,'\n' };
        for (int i = 0; i < 8; i++) png[n++] = SIG[i];

        /* IHDR */
        png[n++]=0; png[n++]=0; png[n++]=0; png[n++]=13;
        png[n++]='I'; png[n++]='H'; png[n++]='D'; png[n++]='R';
        png[n++]=0; png[n++]=0; png[n++]=0; png[n++]=FW;
        png[n++]=0; png[n++]=0; png[n++]=0; png[n++]=FH;
        png[n++]=8; png[n++]=6; png[n++]=0; png[n++]=0; png[n++]=0;
        png[n++]=0; png[n++]=0; png[n++]=0; png[n++]=0;      /* CRC: see above */

        /* IDAT: a zlib header, one stored DEFLATE block, and four bytes where
           the Adler-32 goes. */
        int raw = FH * (FSTRIDE + 1);
        int idat = 2 + 5 + raw + 4;
        png[n++]=(unsigned char)(idat>>24); png[n++]=(unsigned char)(idat>>16);
        png[n++]=(unsigned char)(idat>>8);  png[n++]=(unsigned char)idat;
        png[n++]='I'; png[n++]='D'; png[n++]='A'; png[n++]='T';
        png[n++]=0x78; png[n++]=0x01;
        png[n++]=0x01;                                    /* final, stored */
        png[n++]=(unsigned char)(raw & 0xff); png[n++]=(unsigned char)(raw >> 8);
        png[n++]=(unsigned char)(~raw & 0xff); png[n++]=(unsigned char)((~raw >> 8) & 0xff);
        for (int y = 0; y < FH; y++)
            for (int x = 0; x < FSTRIDE + 1; x++) png[n++] = filt[y][x];
        png[n++]=0; png[n++]=0; png[n++]=0; png[n++]=0;      /* Adler-32 */
        png[n++]=0; png[n++]=0; png[n++]=0; png[n++]=0;      /* CRC */

        /* IEND */
        png[n++]=0; png[n++]=0; png[n++]=0; png[n++]=0;
        png[n++]='I'; png[n++]='E'; png[n++]='N'; png[n++]='D';
        png[n++]=0; png[n++]=0; png[n++]=0; png[n++]=0;

        int w = 0, h = 0;
        ok(png_decode(png, n, g_px, (int)sizeof g_px, &w, &h),
           "a fixture using all five predictors decodes");
        oki(w == FW && h == FH, "at the size its header declares", w * 1000 + h,
            FW * 1000 + FH);

        int wrong = 0, first = -1;
        for (int y = 0; y < FH; y++)
            for (int x = 0; x < FSTRIDE; x++)
                if (g_px[y * FSTRIDE + x] != want[y][x]) {
                    if (first < 0) first = y;
                    wrong++;
                }
        oki(wrong == 0, "and every byte comes back as it went in", wrong, 0);
        if (wrong) printf("      first wrong row %d, filter %d\n", first, first % 5);
    }

    /* --- which predictors the shipped art actually uses --------------------
       Reported rather than asserted, because it is a fact about the ART and
       not about the decoder: an import that produced flatter drawings would
       narrow this list without anything being broken. The assertion that the
       decoder handles all five is above, where it does not depend on what
       Pillow happened to choose.
       단언하지 않고 보고합니다. 이것은 디코더가 아니라 *아트*에 대한 사실이며, 더 평평한
       그림을 만들어 내는 이식은 아무것도 망가뜨리지 않고 이 목록을 좁힙니다. 디코더가 다섯을
       모두 처리한다는 단언은 위에 있고, 그곳은 Pillow가 무엇을 골랐는지에 기대지 않습니다. */
    printf("\nwhich predictors the shipped art uses\n");
    {
        static unsigned char zbuf[PNG_MAX_ZDATA];
        static unsigned char rows[(PNG_MAX_SIDE * 4 + 1) * PNG_MAX_SIDE];
        int seen[5] = { 0, 0, 0, 0, 0 };

        r = fopen(ref, "rb");
        while (fscanf(r, "%511s %d %d %lu", path, &want_w, &want_h, &want_sum) == 4) {
            int len = 0;
            unsigned char *file = slurp(from_root(path), &len);
            if (!file) continue;

            int zlen = 0, w = 0, h = 0;
            for (int i2 = 8; i2 + 12 <= len; ) {
                unsigned l = ((unsigned)file[i2] << 24) | ((unsigned)file[i2+1] << 16) |
                             ((unsigned)file[i2+2] << 8) | file[i2+3];
                if (l > (unsigned)(len - i2 - 12)) break;
                const unsigned char *ty = file + i2 + 4, *bd = file + i2 + 8;
                if (ty[0]=='I'&&ty[1]=='H'&&ty[2]=='D'&&ty[3]=='R') {
                    w = (int)(((unsigned)bd[0]<<24)|((unsigned)bd[1]<<16)|((unsigned)bd[2]<<8)|bd[3]);
                    h = (int)(((unsigned)bd[4]<<24)|((unsigned)bd[5]<<16)|((unsigned)bd[6]<<8)|bd[7]);
                }
                if (ty[0]=='I'&&ty[1]=='D'&&ty[2]=='A'&&ty[3]=='T' &&
                    zlen + (int)l <= (int)sizeof zbuf) {
                    memcpy(zbuf + zlen, bd, l); zlen += (int)l;
                }
                i2 += 12 + (int)l;
            }

            int stride = w * 4;
            if (zlen > 2 && inflate_raw(rows, (stride + 1) * h,
                                        zbuf + 2, zlen - 2) == (stride + 1) * h)
                for (int y = 0; y < h; y++) {
                    int f = rows[y * (stride + 1)];
                    if (f >= 0 && f < 5) seen[f]++;
                }
            free(file);
        }
        fclose(r);

        static const char *NAME[5] = { "None", "Sub", "Up", "Average", "Paeth" };
        for (int f = 0; f < 5; f++)
            printf("      %-8s %6d row(s)%s\n", NAME[f], seen[f],
                   seen[f] ? "" : "   -- unused by this art, covered above");
    }

    /* --- a PNG it cannot read is refused ---------------------------------
       The forms this decoder rejects are rejected because reading them wrong
       produces an IMAGE -- something that looks like damaged art rather than
       like a refusal. Each is built here by corrupting a real header, so the
       thing under test is the check and not a fixture nobody maintains. */
    printf("\nwhat it will not read, it refuses\n");
    {
        int len = 0;
        r = fopen(ref, "rb");
        int have = (fscanf(r, "%511s %d %d %lu", path, &want_w, &want_h, &want_sum) == 4);
        fclose(r);
        unsigned char *file = have ? slurp(from_root(path), &len) : 0;

        if (!file) { ok(0, "a drawing to corrupt"); return fails != 0; }

        int w, h, before = diag_count(DIAG_PNG);

        file[1] ^= 0xff;
        ok(!png_decode(file, len, g_px, (int)sizeof g_px, &w, &h),
           "a file that is not a PNG");
        file[1] ^= 0xff;

        /* colour type 6 is RGBA; 3 is a palette image, which this cannot read
           and which would otherwise be unfiltered as though it were 4 bytes a
           pixel -- a picture, and the wrong one. */
        file[8 + 8 + 9] = 3;
        ok(!png_decode(file, len, g_px, (int)sizeof g_px, &w, &h),
           "a palette image rather than RGBA");
        file[8 + 8 + 9] = 6;

        file[8 + 8 + 12] = 1;            /* Adam7 */
        ok(!png_decode(file, len, g_px, (int)sizeof g_px, &w, &h),
           "an interlaced image");
        file[8 + 8 + 12] = 0;

        ok(!png_decode(file, len, g_px, 16, &w, &h),
           "a destination too small to hold it");

        ok(png_decode(file, len, g_px, (int)sizeof g_px, &w, &h),
           "and the same file, restored, still decodes");

        oki(diag_count(DIAG_PNG) - before == 4,
            "every refusal was counted rather than silent",
            diag_count(DIAG_PNG) - before, 4);
        free(file);
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall png checks passed\n", fails);
    return fails != 0;
}
