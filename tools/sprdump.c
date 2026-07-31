/* sprdump -- write the monster sprite atlas to a PPM for eyeballing, with no
   GL context. Rows are monster types, columns are animation frames. */
#include <stdio.h>
#include "sprite.h"

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "sprites.ppm";
    if (!sprite_dump_ppm(path)) { printf("could not write %s\n", path); return 1; }
    printf("wrote %s\n", path);
    return 0;
}
