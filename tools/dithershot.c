/* dithershot -- render the real level through the post pass and write the
 * result to a PNG, so the look can be JUDGED rather than argued about.
 *
 * The duotone stage is a visual decision, and a table of chroma numbers is a
 * poor way to make one. This draws an actual level with the actual materials,
 * lighting and dither, reads the window back, and writes a file you can open.
 *
 * DUOTONE is a compile-time constant, so this captures whatever the current
 * build is set to. Build it once per setting to compare:
 *
 *     .\build.ps1 -Tool dithershot          (writes build\dither_<n>.png)
 *
 * The PNG writer is deliberately minimal -- stored (uncompressed) deflate
 * blocks and a hand-rolled CRC. A real encoder would be a dependency, and this
 * is a dev tool whose output is looked at once. Files are large; that is fine.
 *
 * 듀오톤은 시각적 판단의 문제이며, 채도 수치 표는 그 판단에 적합한 도구가 아닙니다. 이
 * 도구는 실제 레벨을 실제 재질·조명·디더로 그린 뒤 화면을 읽어 파일로 저장합니다.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl.h"
#include "wgl.h"
#include "post.h"
#include "render.h"
#include "level.h"
#include "model.h"
#include "tex.h"
#include "player.h"
#include "fx.h"
#include "pools.h"

/* The pools this file drives. Owned here the way a ::World owns its own; the
   five modules that used to keep these in file-scope arrays hand them back now.
   See src/pools.h.
   이 파일이 구동하는 풀입니다. ::World가 자기 것을 소유하듯 이곳에서 소유합니다. 이것을
   파일 스코프 배열에 담고 있던 다섯 모듈이 이제 그것을 돌려줍니다. src/pools.h를
   참조하십시오. */
static Pools g_pools;


#define SHOT_W 1280
#define SHOT_H 720

/* --- a minimal PNG writer ------------------------------------------------ */

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

    /* raw = per-row filter byte 0 + RGB triples */
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

    /* zlib stream: 2-byte header, stored blocks, adler32 */
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

/* --- the capture --------------------------------------------------------- */

int main(int argc, char **argv) {
    const char *level_name = (argc > 1) ? argv[1] : "arena";

    HINSTANCE inst = GetModuleHandleA(0);
    if (!gl_bootstrap(inst)) { printf("gl_bootstrap FAILED\n"); return 1; }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = inst;
    wc.lpszClassName = "dithershot";
    RegisterClassA(&wc);

    RECT r = { 0, 0, SHOT_W, SHOT_H };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND w = CreateWindowExA(0, "dithershot", "dithershot", WS_OVERLAPPEDWINDOW,
                             0, 0, r.right - r.left, r.bottom - r.top, 0, 0, inst, 0);
    HDC dc = GetDC(w);
    if (!gl_make_context(dc)) { printf("gl_make_context FAILED\n"); return 1; }

    rd_init();
    int scale = SHOT_H / POST_HEIGHT; if (scale < 1) scale = 1;
    if (!post_init(SHOT_W / scale, SHOT_H / scale)) {
        printf("post_init FAILED\n"); return 1;
    }

    static Level lv;
    if (!level_load(level_name, &lv)) { printf("no level '%s'\n", level_name); return 1; }

    /* `-door <0..100>` opens every door in the level that far before the
     * geometry is built, and `-yaw <degrees>` turns the camera off the spawn
     * heading. Both exist for the same question, which nothing in this project
     * could previously answer: WHAT DOES AN OPEN DOOR LOOK LIKE?
     *
     * That stopped being rhetorical when the light bake landed. A level's
     * lamps are compiled into its vertices at load, and the vertices a door
     * did not move keep the light they were given -- so a door opening onto a
     * lit room no longer brightens the room behind it. That is Quake's
     * behaviour and it is a deliberate trade, but "deliberate" is an argument
     * and an argument is not an observation. A dark doorway reads as a fault
     * to whoever walks up to it, and the only way to find out whether this one
     * does is to look at it.
     *
     * The doors are moved exactly as door.c's apply() moves them, which is
     * copied logic and worth saying so: this tool has no DoorState, no touch
     * test and no keys, and reproducing those to take a photograph would be
     * building the door system twice.
     *
     * `-door <0..100>`은 지오메트리를 만들기 전에 레벨의 모든 문을 그만큼 열고,
     * `-yaw <도>`는 카메라를 스폰 방향에서 돌립니다. 둘 다 같은 질문을 위해 있으며, 이
     * 프로젝트의 어떤 것도 이전에는 답할 수 없던 질문입니다. *열린 문은 어떻게 보이는가?*
     *
     * 조명 베이크가 들어오면서 그것은 수사적 질문이기를 그쳤습니다. 레벨의 등은 로드 시
     * 정점에 구워지고, 문이 움직이지 않은 정점은 받았던 빛을 유지합니다. 따라서 밝은 방으로
     * 열리는 문이 더 이상 뒤쪽 방을 밝히지 않습니다. 이는 Quake의 동작이며 의도된 거래이지만,
     * "의도"는 논증이고 논증은 관찰이 아닙니다. 어두운 문간은 그 앞에 선 사람에게 결함으로
     * 읽히며, 이 문간이 그런지 알아내는 유일한 방법은 보는 것입니다.
     *
     * 문은 door.c의 apply()가 움직이는 것과 정확히 같게 움직입니다. 복제된 논리이며 그렇다고
     * 말해 둘 가치가 있습니다. 이 도구에는 DoorState도, 접촉 판정도, 열쇠도 없고, 사진 한 장을
     * 찍기 위해 그것을 재현하는 것은 문 시스템을 두 번 만드는 일입니다. */
    float door_t = 0.0f;
    for (int i = 2; i + 1 < argc; i++)
        if (argv[i][0] == '-' && argv[i][1] == 'd' && argv[i][2] == 'o')
            door_t = (float)atoi(argv[i + 1]) / 100.0f;

    if (door_t > 0.0f) {
        if (door_t > 1.0f) door_t = 1.0f;
        int nd = lv.n_doors > LVL_MAX_DOORS ? LVL_MAX_DOORS : lv.n_doors;
        for (int i = 0; i < nd; i++) {
            const DoorDef *d = &lv.doors[i];
            if (d->sector < 0 || d->sector >= lv.n_sectors) continue;
            Sector *s = &lv.sectors[d->sector];
            switch (d->axis) {
            case DOOR_UP:   s->ceil  = (short)(s->ceil  + d->amount * door_t); break;
            case DOOR_DOWN: s->floor = (short)(s->floor - d->amount * door_t); break;
            case DOOR_X:
            case DOOR_Z: {
                int off = (d->axis == DOOR_X) ? 0 : 1;
                for (int k = 0; k < s->n; k++)
                    s->pts[k*2 + off] = (short)(s->pts[k*2 + off] + d->amount * door_t);
                level_bounds(s);
                break;
            }
            default: break;
            }
        }
        level_grid_build(&lv);
        printf("  %d door(s) opened to %.0f%%\n", nd, door_t * 100.0f);
    }

    MeshBuf  mb;  mb_init(&mb, 16384);
    Mesh     mesh = {0};
    MdlRange ranges[LVL_MAX_RANGES];
    Mat      mats[LVL_MAX_RANGES];
    int nr = level_geometry(&mb, &lv, ranges, LVL_MAX_RANGES);
    mesh_upload(&mesh, &mb, 0);
    for (int i = 0; i < nr; i++) mats[i] = tex_mat(ranges[i].mat);

    /* Stand where the level says the player spawns, looking along its yaw, so
       the shot frames whatever the author pointed the start at. */
    Player p = {0};
    float yaw = player_spawn(&p, &lv);

    /* `-yaw <degrees>` turns off the spawn heading. The author points a start
       at whatever the level opens with, which is rarely the door somebody
       needs to photograph.
       `-yaw <도>`는 스폰 방향에서 돌립니다. 제작자는 시작 지점을 레벨이 열리는 광경 쪽으로
       두며, 그것이 누군가 촬영해야 하는 문인 경우는 드뭅니다. */
    for (int i = 2; i + 1 < argc; i++)
        if (argv[i][0] == '-' && argv[i][1] == 'y')
            yaw += (float)atoi(argv[i + 1]) * 0.0174533f;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.05f, 0.06f, 0.09f, 1.0f);

    float aspect = post_begin();
    if (aspect <= 0.0f) aspect = (float)SHOT_W / (float)SHOT_H;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* `-death <0..1>` renders the death collapse at a point along its curve,
       so the pose can be looked at without dying in the game at the right
       moment with a screenshot key ready. The easing and the three constants
       are the ones main.c uses, not a copy -- a preview that drifted from the
       thing it previews is worse than no preview.
       `-death <0..1>`은 사망 시 쓰러짐을 곡선상의 한 지점에서 렌더링하므로, 게임에서
       정확한 순간에 죽으며 스크린샷 키를 준비하지 않고도 자세를 확인할 수 있습니다.
       이징과 세 상수는 main.c가 쓰는 것이며 사본이 아닙니다. 미리보기 대상과 어긋난
       미리보기는 없느니만 못합니다. */
    /* `-de`, not `-d`: `-door` begins with the same letter and used to match
       this test too, so asking for an open door also asked for the death
       collapse and the shot came back rolled 35 degrees. A one-letter flag
       test is fine until the second flag starting with that letter arrives.
       `-d`가 아니라 `-de`입니다. `-door`가 같은 글자로 시작해서 이 검사에도 걸렸고, 그래서
       문을 열어 달라는 요청이 쓰러짐 연출까지 함께 요청하여 촬영이 35도 기울어진 채로
       돌아왔습니다. 한 글자짜리 플래그 검사는 그 글자로 시작하는 두 번째 플래그가 나타나기
       전까지만 괜찮습니다. */
    float death_k = 0.0f;
    for (int i = 2; i + 1 < argc; i++)
        if (argv[i][0] == '-' && argv[i][1] == 'd' && argv[i][2] == 'e')
            death_k = (float)atoi(argv[i + 1]) / 100.0f;

    v3    eye_pos   = p.pos;
    float cam_pitch = 0.0f, cam_roll = 0.0f;
    if (death_k > 0.0f) {
        if (death_k > 1.0f) death_k = 1.0f;
        float e = 1.0f - (1.0f - death_k) * (1.0f - death_k);
        eye_pos.y -= DEATH_DROP  * e;
        cam_pitch -= DEATH_PITCH * e;
        cam_roll   = DEATH_ROLL  * e;
        printf("  death pose %.0f%%: drop %.2fm  pitch %.0f deg  roll %.0f deg\n",
               death_k * 100.0f, DEATH_DROP * e,
               DEATH_PITCH * e * 57.2958f, DEATH_ROLL * e * 57.2958f);
    }

    mat4 proj = mat4_perspective(1.5708f, aspect, 0.05f, 200.0f);
    mat4 view = mat4_fps_view_roll(eye_pos, yaw, cam_pitch, cam_roll);
    mat4 vp   = mat4_mul(proj, view);

    rd_mode(RD_WORLD);
    rd_mvp(vp);
    rd_eye(eye_pos);

    /* `-time <seconds>` advances the clock animated materials run against, so
       the lava's flow can be captured at a chosen moment. Without this every
       shot is taken at t=0 and the animation is untestable -- a still frame of
       a moving surface proves only that it renders, not that it moves.
       `-time <초>`는 애니메이션 재질이 사용하는 시계를 진행시켜 용암의 흐름을 원하는
       순간에 담을 수 있게 합니다. 이것이 없으면 모든 촬영이 t=0에서 이루어져 애니메이션을
       검증할 수 없습니다. 움직이는 표면의 정지 화면은 그것이 렌더링된다는 것만 증명할 뿐
       움직인다는 것은 증명하지 못합니다. */
    {
        float shot_time = 0.0f;
        for (int i = 2; i + 1 < argc; i++)
            if (argv[i][0] == '-' && argv[i][1] == 't')
                shot_time = (float)atoi(argv[i + 1]) / 100.0f;
        rd_time(shot_time);
        if (shot_time > 0.0f) printf("  clock at %.2f s\n", shot_time);
    }
    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < nr; i++) {
        tex_use(&mats[i]);
        mesh_draw_range(&mesh, ranges[i].first, ranges[i].count);
    }

    /* Effects, if a second argument named one. Spawned a few metres ahead and
       stepped forward a little so the burst is caught mid-flight rather than
       all at its origin -- a particle system photographed on frame zero looks
       identical whatever its velocities are.
       두 번째 인자로 이펙트를 지정하면 함께 그립니다. 몇 미터 앞에 생성한 뒤 조금
       진행시켜, 전부 원점에 모여 있는 것이 아니라 비행 중인 모습을 담습니다. 0번째
       프레임에서 촬영한 파티클 시스템은 속도가 어떻든 똑같아 보입니다. */
    /* `-death` is a camera flag, not an effect name. Without this test the
       flag itself would be spawned as an effect, find nothing, and the
       output would be written as fx_-d.png -- which is exactly what it did.
       `-death`는 이펙트 이름이 아니라 카메라 플래그입니다. 이 검사가 없으면 플래그
       자체가 이펙트로 생성 시도되어 아무것도 찾지 못하고, 출력이 fx_-d.png로
       기록됩니다. 실제로 그렇게 동작했습니다. */
    int has_fx = (argc > 2 && argv[2][0] != '-');
    if (has_fx) {
        float cy = cosf(yaw), sy = sinf(yaw);
        v3 fwd = v3f(-sy, 0.0f, -cy);
        v3 at  = v3add(p.pos, v3scale(fwd, 4.0f));

        /* How far into the burst to photograph, in frames. A particle system
           is a shape that changes, so which instant is captured decides what
           the picture shows: frame 8 is still mid-expansion and reads denser
           than the effect ever looks in motion. An optional third argument
           picks the moment.
           폭발의 어느 시점을 촬영할지를 프레임 단위로 지정합니다. 파티클 시스템은 변화하는
           형태이므로 어느 순간을 담느냐가 사진의 내용을 결정합니다. 8프레임은 아직 퍼지는
           중이라 실제 움직일 때보다 조밀해 보입니다. 선택적 세 번째 인자로 시점을
           고릅니다. */
        int at_frame = (argc > 3) ? atoi(argv[3]) : 14;

        fx_spawn(&g_pools, argv[2], at, v3f(0.0f, 1.0f, 0.0f));
        for (int i = 0; i < at_frame; i++) fx_update(&g_pools, 1.0f / 60.0f);

        v3 cam_right = v3f(cy, 0.0f, -sy);
        v3 cam_up    = v3f(0.0f, 1.0f, 0.0f);
        fx_draw(&g_pools, vp, cam_right, cam_up);
        printf("  spawned '%s': %d particles live\n", argv[2], fx_live_count(&g_pools));
    }

    post_end(SHOT_W, SHOT_H);

    glFinish();
    unsigned char *px = (unsigned char *)malloc((size_t)SHOT_W * SHOT_H * 3);
    if (!px) { printf("out of memory\n"); return 1; }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, SHOT_W, SHOT_H, GL_RGB, GL_UNSIGNED_BYTE, px);

    /* Written next to the EXE rather than into the working directory, which is
       the repo root when build.ps1 launches a tool -- a capture sweep would
       otherwise scatter multi-megabyte PNGs through the source tree.
       작업 디렉터리가 아니라 EXE 옆에 씁니다. build.ps1이 도구를 실행할 때 작업
       디렉터리는 저장소 루트이며, 그대로 두면 캡처 스윕이 수 메가바이트짜리 PNG를 소스
       트리 곳곳에 흩뿌리게 됩니다. */
    char path[MAX_PATH];
    int d100 = (int)(POST_DUOTONE * 100.0 + 0.5);
    {
        DWORD n = GetModuleFileNameA(0, path, MAX_PATH);
        while (n > 0 && path[n - 1] != '\\') n--;
        if (has_fx)            wsprintfA(path + n, "fx_%s.png", argv[2]);
        else if (door_t > 0.0f) wsprintfA(path + n, "door_%s_%03d.png",
                                          level_name, (int)(door_t * 100.0f + 0.5f));
        else                    wsprintfA(path + n, "dither_duotone_%03d.png", d100);
    }

    if (write_png(path, px, SHOT_W, SHOT_H))
        printf("wrote %s   (level '%s', DUOTONE %d/100)\n", path, level_name, d100);
    else
        printf("could not write %s\n", path);

    /* Report what actually reached the screen, as a cross-check on the file. */
    {
        double ch = 0; int n = 0;
        for (int i = 0; i < SHOT_W * SHOT_H; i++) {
            int R = px[i*3], G = px[i*3+1], B = px[i*3+2];
            int mx = R > G ? (R > B ? R : B) : (G > B ? G : B);
            int mn = R < G ? (R < B ? R : B) : (G < B ? G : B);
            ch += (mx - mn); n++;
        }
        printf("  mean chroma across the frame: %.2f / 255\n", ch / n);
    }

    free(px);
    mb_free(&mb);
    post_shutdown();
    return 0;
}
