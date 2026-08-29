/* matdump -- run the procedural recipes headlessly and write one PNG each, so
 * TrenchBroom's material browser can show the materials the shader computes.
 *
 * ENGLISH
 * -------
 * assets/trenchbroom/README.md carried this as an open item for as long as the
 * profile existed: the browser is pointed at assets/sprites, which holds the
 * hand-drawn wall art, and the materials that are RECIPES rather than images
 * have no file there to show. Type `pgrid` onto a face and the game draws it
 * correctly; the editor draws nothing and the author is picking blind.
 *
 * There is no CPU-side copy of those surfaces to dump. A procedural material
 * has no pixels anywhere -- tex.c's mat_make skips tex_make entirely when a
 * recipe carries `proc`, and the surface exists only as GLSL. So this creates a
 * real context, draws each material through the REAL shader, and reads the
 * frame back. What lands in the file is what the game draws, because it was
 * drawn by the thing that draws the game.
 *
 * RD_SWATCH is the mode for exactly this. It returns the material with no
 * lighting, no fog and no dither -- "the material with nothing done to it, so
 * the editor's palette shows the surface itself rather than the surface under
 * some particular light", as render.c puts it. mapedit's own palette already
 * uses it; this writes the same swatch to disk instead of to a panel.
 *
 * ONLY PROCEDURAL MATERIALS ARE WRITTEN, and that is a safety rule rather than
 * a scope decision. assets/sprites/wall_brick.png is SOURCE ART -- imported
 * from Freedoom by import-walls.py and the only copy of it -- and a sweep that
 * rendered every material would overwrite it with the engine's own rendering
 * of itself. The rule mirrors the engine's own precedence: mat_make reaches for
 * pixels only when a recipe has no `proc`, so a material with `proc` is exactly
 * one whose PNG the game will never read.
 *
 * WHICH IS ALSO WHY THE BAKE MUST SKIP THEM. Every .png under assets/sprites
 * becomes an entry in the baked sprite library, and one that nothing draws is
 * still quantised, encoded and carried in .rdata for the life of the binary --
 * the importer's contact sheet cost 411KB of a 1.44MB budget that way. bake.ps1
 * skips a sprite that names a procedural material now, by the same rule this
 * file uses to pick what to write. The two must agree; they agree because they
 * ask the same question of the same recipe text.
 *
 *     .\build.ps1 -Tool matdump
 *
 * Run it again after editing a `proc` recipe or the shader behind it. The
 * previews are generated, not tracked -- see the .gitignore this writes beside
 * them -- so there is no committed copy to go stale, only a local one to
 * refresh.
 *
 * 한국어
 * ------
 * 절차적 레시피를 헤드리스로 실행해 재질마다 PNG를 하나씩 씁니다. TrenchBroom의 재질
 * 브라우저가 셰이더로 계산되는 재질을 보여 줄 수 있게 하기 위해서입니다.
 *
 * assets/trenchbroom/README.md는 프로필이 존재하는 내내 이것을 미해결 항목으로 안고
 * 있었습니다. 브라우저는 손으로 그린 벽 아트가 있는 assets/sprites를 가리키는데, 이미지가
 * 아니라 *레시피*인 재질에는 보여 줄 파일이 그곳에 없습니다. 면에 `pgrid`를 입력하면 게임은
 * 올바르게 그리지만 에디터는 아무것도 그리지 않고, 작성자는 눈을 감고 고르게 됩니다.
 *
 * 덤프할 CPU 측 사본이 없습니다. 절차적 재질은 어디에도 픽셀을 갖지 않습니다. tex.c의
 * mat_make는 레시피에 `proc`가 있으면 tex_make를 통째로 건너뛰며, 그 표면은 GLSL로만
 * 존재합니다. 그래서 이 도구는 실제 컨텍스트를 만들고 각 재질을 *실제* 셰이더로 그린 뒤
 * 프레임을 되읽습니다. 파일에 담기는 것이 곧 게임이 그리는 것인데, 게임을 그리는 그것이
 * 그렸기 때문입니다.
 *
 * RD_SWATCH가 정확히 이 용도의 모드입니다. 조명도 안개도 디더도 없이 재질을 반환합니다.
 * render.c의 표현으로는 "에디터의 팔레트가 어떤 특정한 빛 아래의 표면이 아니라 표면 자체를
 * 보여 주도록, 아무 처리도 하지 않은 재질"입니다. mapedit의 팔레트가 이미 이 모드를 쓰며,
 * 이 도구는 같은 스와치를 패널이 아니라 디스크에 씁니다.
 *
 * *절차적 재질만 씁니다.* 이는 범위의 문제가 아니라 안전 규칙입니다.
 * assets/sprites/wall_brick.png는 *원본 아트*이며 import-walls.py가 Freedoom에서 가져온
 * 유일한 사본인데, 모든 재질을 훑는 도구는 그것을 엔진이 스스로를 렌더링한 결과로 덮어씁니다.
 * 이 규칙은 엔진 자신의 우선순위를 그대로 따릅니다. mat_make는 레시피에 `proc`가 없을
 * 때에만 픽셀을 찾으므로, `proc`가 있는 재질은 정확히 게임이 그 PNG를 결코 읽지 않을
 * 재질입니다.
 *
 * *그리고 그것이 베이크가 이들을 건너뛰어야 하는 이유이기도 합니다.* assets/sprites의 모든
 * .png는 구워진 스프라이트 라이브러리의 항목이 되며, 아무도 그리지 않는 것도 여전히
 * 양자화되고 인코딩되어 바이너리가 사는 내내 .rdata에 실립니다. 임포터의 대조 시트가 그렇게
 * 1.44MB 예산 중 411KB를 썼습니다. 이제 bake.ps1은 절차적 재질의 이름을 가진 스프라이트를
 * 건너뛰며, 그 판단은 이 파일이 무엇을 쓸지 고르는 규칙과 같습니다. 둘은 일치해야 하고, 같은
 * 레시피 텍스트에 같은 질문을 하므로 일치합니다.
 *
 * `proc` 레시피나 그 뒤의 셰이더를 고친 뒤에는 다시 실행하십시오. 미리보기는 추적되지 않는
 * 생성물이므로(옆에 함께 쓰이는 .gitignore 참조) 낡아 갈 커밋본은 없고 새로 고칠 로컬
 * 사본만 있습니다.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl.h"
#include "wgl.h"
#include "render.h"
#include "tex.h"
#include "data.h"
#include "txt.h"
#include "m.h"
#include "plat.h"

/* --- where the files go ------------------------------------------------------
 *
 * ENGLISH
 * -------
 * Resolved against the EXE'S PARENT, never against the working directory, which
 * is the same rule data.c's resolve() follows and for the same reason: the
 * binary lives in build\ and the assets live beside it, so plat_exe_dir walks
 * back one level and a relative path appends directly.
 *
 * The first version used a bare "assets/sprites/..." and worked exactly once --
 * when build.ps1 -Tool launches a tool the working directory happens to be the
 * repo root. build.ps1 -Test runs the same binaries FROM build\, deliberately
 * (its own comment explains that a tool run from anywhere else resolves assets\
 * against the wrong parent), and there every single write failed. The tool said
 * so and exited non-zero, which is the only reason this was a caught bug rather
 * than a silently empty directory.
 *
 * 한국어
 * ------
 * 작업 디렉터리가 아니라 *EXE의 부모*를 기준으로 해석합니다. data.c의 resolve()가 따르는
 * 규칙과 같고 이유도 같습니다. 바이너리는 build\에 있고 에셋은 그 옆에 있으므로,
 * plat_exe_dir이 한 단계 거슬러 올라가면 상대 경로를 그대로 이어 붙일 수 있습니다.
 *
 * 첫 판은 "assets/sprites/..."를 그대로 썼고 정확히 한 경우에만 동작했습니다.
 * build.ps1 -Tool이 도구를 실행할 때 작업 디렉터리가 마침 저장소 루트이기 때문입니다.
 * build.ps1 -Test는 같은 바이너리를 *build\에서* 의도적으로 실행하며(다른 곳에서 실행된
 * 도구는 assets\를 엉뚱한 부모에 대해 해석한다고 그쪽 주석이 설명합니다), 그곳에서는 모든
 * 쓰기가 실패했습니다. 도구가 그렇게 말하고 0이 아닌 값으로 종료했다는 것만이, 이것이 조용히
 * 빈 디렉터리로 끝나지 않고 잡힌 버그가 된 이유입니다. */
static void asset_path(char *out, int cap, const char *rel) {
    char dir[MAX_PATH];
    int  n = plat_exe_dir(dir, sizeof(dir));
    int  i = txt_copy(out, cap, dir, n);
    txt_copy(out + i, cap - i, rel, -1);
}

/* --- how big, and over how much world ---------------------------------------
 *
 * ENGLISH
 * -------
 * PREVIEW_UNITS IS IN UV UNITS, AND A UV UNIT IS TWO METRES. Sector walls carry
 * LEVEL_UV = 0.5 uv per metre and a brush face at the editor's face scale of
 * 0.5 works out to 32/(0.5*128) = 0.5 as well, which is what the README means
 * by that scale being "the texel density the rest of the game already uses".
 *
 * The pair has to satisfy one equation, and it is the whole reason these two
 * numbers are not free:
 *
 *     metres the image DEPICTS  =  PREVIEW_UNITS / 0.5
 *     metres the editor DRAWS it over  =  PREVIEW_PX * 0.5 / 32
 *
 * At 256 and 2.0 both come to 4 metres, so a face wearing this preview in the
 * 3D view shows the pattern at the size the game will show it, and an author
 * judging whether a wall reads correctly is judging the right thing.
 *
 * PREVIEW_UNITS was 4.0 first, derived as though a UV unit were a metre. The
 * image then depicted 8 metres and the editor drew it over 4, so every pattern
 * appeared twice as fine in TrenchBroom as in play -- the one failure a preview
 * must not have, because it is invisible until you build the wall.
 *
 * 4 metres is also what the browser wants. Halving it to 128px over 1.0 keeps
 * the equation balanced but leaves pwindow's 1.43m glazing module barely more
 * than one window in the thumbnail, which in a list is a dark rectangle and
 * nothing else. 256 is the size at which the coarsest pattern here is still
 * recognisable at thumbnail size.
 *
 * 한국어
 * ------
 * *PREVIEW_UNITS는 UV 단위이며, 1 UV 단위는 2미터입니다.* 섹터 벽은 미터당 LEVEL_UV = 0.5
 * uv를 나르고, 에디터의 면 스케일 0.5에서 브러시 면도 32/(0.5*128) = 0.5로 같습니다. README가
 * 그 스케일을 두고 "게임의 나머지가 이미 쓰는 텍셀 밀도"라고 말하는 것이 이 뜻입니다.
 *
 * 이 두 값은 하나의 등식을 만족해야 하며, 그것이 이 숫자들이 자유롭지 않은 이유의 전부입니다.
 *
 *     이미지가 *담는* 미터    =  PREVIEW_UNITS / 0.5
 *     에디터가 *그리는* 미터  =  PREVIEW_PX * 0.5 / 32
 *
 * 256과 2.0에서 둘 다 4미터가 되므로, 3D 뷰에서 이 미리보기를 입은 면은 게임이 보여 줄 크기
 * 그대로 패턴을 보여 주고, 벽이 제대로 읽히는지 판단하는 작성자는 올바른 것을 판단합니다.
 *
 * PREVIEW_UNITS는 처음에 1 UV 단위를 1미터로 여기고 4.0으로 잡았습니다. 그러면 이미지는 8미터를
 * 담고 에디터는 그것을 4미터에 그리므로, 모든 패턴이 플레이에서보다 TrenchBroom에서 두 배 곱게
 * 보였습니다. 미리보기가 가져서는 안 될 단 하나의 결함인데, 벽을 세워 보기 전에는 보이지 않기
 * 때문입니다.
 *
 * 4미터는 브라우저가 원하는 크기이기도 합니다. 1.0 UV에 128px로 절반을 줄여도 등식은 유지되지만,
 * pwindow의 1.43m 유리 모듈이 썸네일에 창 하나를 겨우 넘길 만큼만 담겨 목록에서는 어두운 사각형
 * 하나가 됩니다. 256은 이곳에서 가장 성긴 패턴이 썸네일 크기에서도 알아볼 수 있는 크기입니다. */
#define PREVIEW_PX    256
#define PREVIEW_UNITS 2.0f

/* --- a minimal PNG writer ---------------------------------------------------
 * Lifted from tools/dithershot.c, and deliberately not shared with it: the
 * tools do not link against each other, and a fourth file existing only to hold
 * eighty lines two of them use would be a dependency where a copy is a copy.
 * tools/dithershot.c에서 가져왔으며 의도적으로 공유하지 않습니다. 도구들은 서로 링크되지
 * 않고, 둘이 쓰는 80줄을 담으려고 네 번째 파일을 두는 것은 사본으로 족한 자리에 의존성을
 * 만드는 일입니다. */

static unsigned crc_table[256];
static void crc_init(void) {
    for (unsigned n = 0; n < 256; n++) {
        unsigned c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static unsigned crc_of(const unsigned char *b, int n, unsigned c) {
    for (int i = 0; i < n; i++) c = crc_table[(c ^ b[i]) & 0xff] ^ (c >> 8);
    return c;
}
static void be32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}
static void chunk(FILE *f, const char *tag, const unsigned char *data, int len) {
    unsigned char hdr[4];
    be32(hdr, (unsigned)len);
    fwrite(hdr, 1, 4, f);
    fwrite(tag, 1, 4, f);
    if (len) fwrite(data, 1, (size_t)len, f);
    unsigned c = crc_of((const unsigned char *)tag, 4, 0xFFFFFFFFu);
    if (len) c = crc_of(data, len, c);
    be32(hdr, c ^ 0xFFFFFFFFu);
    fwrite(hdr, 1, 4, f);
}

/* Stored-deflate: no compression, just framed literal blocks. */
static int write_png(const char *path, const unsigned char *rgb, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    crc_init();

    static const unsigned char SIG[8] = {137,'P','N','G',13,10,26,10};
    fwrite(SIG, 1, 8, f);

    unsigned char ihdr[13];
    be32(ihdr, (unsigned)w); be32(ihdr + 4, (unsigned)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);

    int stride = w * 3 + 1;
    int raw_len = stride * h;
    unsigned char *raw = (unsigned char *)malloc((size_t)raw_len);
    if (!raw) { fclose(f); return 0; }
    for (int y = 0; y < h; y++) {
        unsigned char *row = raw + (size_t)y * stride;
        row[0] = 0;
        /* GL reads bottom-up; PNG is top-down. */
        memcpy(row + 1, rgb + (size_t)(h - 1 - y) * w * 3, (size_t)w * 3);
    }

    int max_block = 65535;
    int nblocks = (raw_len + max_block - 1) / max_block;
    int z_len = 2 + nblocks * 5 + raw_len + 4;
    unsigned char *z = (unsigned char *)malloc((size_t)z_len);
    if (!z) { free(raw); fclose(f); return 0; }

    int zp = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;
    for (int off = 0; off < raw_len; off += max_block) {
        int n = raw_len - off; if (n > max_block) n = max_block;
        z[zp++] = (off + n >= raw_len) ? 1 : 0;
        z[zp++] = (unsigned char)(n & 0xff);
        z[zp++] = (unsigned char)(n >> 8);
        z[zp++] = (unsigned char)(~n & 0xff);
        z[zp++] = (unsigned char)((~n >> 8) & 0xff);
        memcpy(z + zp, raw + off, (size_t)n);
        zp += n;
    }
    unsigned a = 1, b = 0;
    for (int i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32(z + zp, (b << 16) | a); zp += 4;

    chunk(f, "IDAT", z, zp);
    chunk(f, "IEND", 0, 0);

    free(z); free(raw); fclose(f);
    return 1;
}

/* --- which materials are recipes rather than images -------------------------
 *
 * Reads assets/textures.txt the way mapedit's material_names does, but carries
 * one extra bit per name: whether the block that follows it mentions `proc`.
 * A block ends where the next `t` begins, which is the same rule tex.c's
 * parse_recipe uses -- there is no explicit terminator in the format and this
 * must not invent one.
 *
 * 어느 재질이 이미지가 아니라 레시피인지 판별합니다. mapedit의 material_names와 같은 방식으로
 * assets/textures.txt를 읽되, 이름마다 한 비트를 더 나릅니다. 뒤따르는 블록이 `proc`를
 * 언급하는지 여부입니다. 블록은 다음 `t`가 시작하는 곳에서 끝나며, 이는 tex.c의
 * parse_recipe가 쓰는 규칙과 같습니다. 이 형식에는 명시적 종료 표시가 없고, 이곳에서 그것을
 * 새로 만들어 내서는 안 됩니다. */
#define MAX_NAMES 64

static int procedural_names(char out[][TEX_NAME_MAX], int max) {
    const char *p = data_text(DATA_RECIPES);
    int n = 0, cur = -1;
    if (!p) return 0;
    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;
        if (txt_is(t, len, "t")) {
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;
            /* Claim the slot now and release it again if no `proc` arrives, so
               a name is never half-written into the array.
               자리를 먼저 잡고 `proc`가 오지 않으면 되돌려 놓습니다. 이름이 배열에 반쯤
               쓰인 채로 남는 일이 없게 하기 위해서입니다. */
            cur = -1;
            if (n < max) {
                int i = 0;
                for (; i < len && i < TEX_NAME_MAX - 1; i++) out[n][i] = nm[i];
                out[n][i] = 0;
                cur = n;
            }
        } else if (cur >= 0 && txt_is(t, len, "proc")) {
            n = cur + 1;
            cur = -1;
        }
    }
    return n;
}

/* --- the swatch quad --------------------------------------------------------
 * The same screen-space quad mapedit's palette draws, sized to the whole
 * viewport and carrying UVs that run 0..PREVIEW_UNITS -- world UVs here are
 * metres, so that is literally how much wall the image depicts.
 * mapedit의 팔레트가 그리는 것과 같은 화면 공간 사각형이며, 뷰포트 전체 크기에 UV가
 * 0..PREVIEW_UNITS로 흐릅니다. 이곳의 월드 UV는 미터이므로 그 값이 곧 이미지가 담는
 * 벽의 넓이입니다. */
static void quad(MeshBuf *b, float sz, float uv) {
    v3 n = v3f(0, 0, 1);
    mb_vtx(b, v3f(0.0f, 0.0f, 0), n, 0.0f, 0.0f);
    mb_vtx(b, v3f(sz,   0.0f, 0), n, uv,   0.0f);
    mb_vtx(b, v3f(sz,   sz,   0), n, uv,   uv);
    mb_vtx(b, v3f(0.0f, 0.0f, 0), n, 0.0f, 0.0f);
    mb_vtx(b, v3f(sz,   sz,   0), n, uv,   uv);
    mb_vtx(b, v3f(0.0f, sz,   0), n, 0.0f, uv);
}

int main(void) {
    char names[MAX_NAMES][TEX_NAME_MAX];
    int  n = procedural_names(names, MAX_NAMES);
    if (!n) { printf("no procedural materials in assets\\textures.txt\n"); return 1; }

    HINSTANCE inst = GetModuleHandleA(0);
    if (!gl_bootstrap(inst)) { printf("gl_bootstrap FAILED\n"); return 1; }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = inst;
    wc.lpszClassName = "matdump";
    RegisterClassA(&wc);

    RECT r = { 0, 0, PREVIEW_PX, PREVIEW_PX };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND w = CreateWindowExA(0, "matdump", "matdump", WS_OVERLAPPEDWINDOW,
                             0, 0, r.right - r.left, r.bottom - r.top, 0, 0, inst, 0);
    HDC dc = GetDC(w);
    if (!gl_make_context(dc)) { printf("gl_make_context FAILED\n"); return 1; }

    rd_init();
    rd_use();
    glViewport(0, 0, PREVIEW_PX, PREVIEW_PX);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    MeshBuf mb;
    Mesh    mesh = {0};
    mb_init(&mb, 6);
    quad(&mb, (float)PREVIEW_PX, PREVIEW_UNITS);
    mesh_upload(&mesh, &mb, 0);

    /* Ortho straight onto the viewport, y down, which is what the swatch quad
       above is wound for. */
    rd_mvp(mat4_ortho(0.0f, (float)PREVIEW_PX, (float)PREVIEW_PX, 0.0f, -1.0f, 1.0f));
    rd_mode(RD_SWATCH);
    /* Pinned at zero rather than left alone. pLava is the one material that
       moves -- its crack threshold breathes on uTime -- so without this the
       preview would be whatever frame the clock happened to be showing, and
       two runs of a tool that writes files would differ for no reason a reader
       could see. An unset uniform is already zero; saying so is what makes the
       determinism a decision instead of an accident.
       그대로 두지 않고 0에 고정합니다. pLava는 이곳에서 움직이는 유일한 재질이며
       (균열 임계값이 uTime으로 호흡합니다) 이것이 없으면 미리보기는 시계가 마침
       가리키던 프레임이 되고, 파일을 쓰는 도구의 두 실행이 읽는 사람이 볼 수 없는
       이유로 달라집니다. 설정되지 않은 유니폼은 이미 0이지만, 그렇다고 말하는 것이
       그 결정성을 우연이 아니라 결정으로 만듭니다. */
    rd_time(0.0f);
    glActiveTexture(GL_TEXTURE0);

    unsigned char *px = (unsigned char *)malloc((size_t)PREVIEW_PX * PREVIEW_PX * 3);
    if (!px) { printf("out of memory\n"); return 1; }

    /* The .gitignore is written beside the previews and lists exactly the files
       this run produced. A pattern would have been shorter and wrong in one of
       two ways: `p*.png` silently swallows a future sprite that happens to
       start with p, and an exact list maintained by hand goes stale the moment
       a recipe is added. Generating it from the same sweep that writes the
       files is the only version of this that cannot disagree with itself.
       .gitignore를 미리보기 옆에 쓰며, 이번 실행이 만들어 낸 파일을 정확히 나열합니다.
       패턴이 더 짧았겠지만 두 가지 중 하나로 틀립니다. `p*.png`는 앞으로 p로 시작하는
       스프라이트를 조용히 삼키고, 손으로 관리하는 목록은 레시피가 하나 추가되는 순간
       낡습니다. 파일을 쓰는 그 훑기에서 함께 생성하는 것만이 스스로와 어긋날 수 없는
       형태입니다. */
    char ign_path[MAX_PATH];
    asset_path(ign_path, sizeof(ign_path), "assets\\sprites\\.gitignore");
    FILE *ign = fopen(ign_path, "wb");
    if (ign)
        fprintf(ign,
                "# Written by tools/matdump.c -- do not edit.\n"
                "#\n"
                "# Editor previews of the materials the fragment shader computes.\n"
                "# They exist so TrenchBroom's material browser can show them; the\n"
                "# game never reads them and bake.ps1 skips them, so nothing here\n"
                "# reaches the binary. Regenerate with:\n"
                "#\n"
                "#     .\\build.ps1 -Tool matdump\n"
                "#\n"
                "# tools/matdump.c가 작성합니다. 직접 편집하지 마십시오.\n"
                "# 프래그먼트 셰이더가 계산하는 재질의 에디터 미리보기입니다. TrenchBroom의\n"
                "# 재질 브라우저가 보여 줄 수 있도록 존재하며, 게임은 이 파일들을 결코 읽지\n"
                "# 않고 bake.ps1이 건너뛰므로 어느 것도 바이너리에 들어가지 않습니다.\n");

    int wrote = 0;
    for (int i = 0; i < n; i++) {
        Mat m = tex_mat(names[i]);
        if (!m.proc) {
            /* A name that parsed as procedural and did not resolve as one means
               the two readings of the same file disagree, which is worth saying
               out loud rather than skipping quietly.
               절차적으로 파싱된 이름이 절차적으로 해석되지 않았다면 같은 파일의 두 읽기가
               어긋난 것이며, 조용히 건너뛰기보다 소리 내어 말할 값어치가 있습니다. */
            printf("  %-16s SKIPPED -- recipe has `proc` but resolved to a texture\n",
                   names[i]);
            continue;
        }

        tex_use(&m);
        glClear(GL_COLOR_BUFFER_BIT);
        mesh_draw(&mesh);
        glFinish();
        glReadPixels(0, 0, PREVIEW_PX, PREVIEW_PX, GL_RGB, GL_UNSIGNED_BYTE, px);

        char rel[TEX_NAME_MAX + 32], path[MAX_PATH];
        wsprintfA(rel, "assets\\sprites\\%s.png", names[i]);
        asset_path(path, sizeof(path), rel);
        if (write_png(path, px, PREVIEW_PX, PREVIEW_PX)) {
            printf("  %-16s -> %s\n", names[i], path);
            if (ign) fprintf(ign, "%s.png\n", names[i]);
            wrote++;
        } else {
            printf("  %-16s could not write %s\n", names[i], path);
        }
    }
    if (ign) fclose(ign);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) printf("  GL error 0x%04x during the sweep\n", err);

    printf("\n%d of %d procedural materials written, %dx%d over %.0f units\n",
           wrote, n, PREVIEW_PX, PREVIEW_PX, (double)PREVIEW_UNITS);

    free(px);
    mb_free(&mb);
    return (wrote == n && err == GL_NO_ERROR) ? 0 : 1;
}
