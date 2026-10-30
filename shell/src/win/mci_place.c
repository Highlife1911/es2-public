/* The portable parts of the own movie player (mci_player.h): the movie's
 * path and its rectangle in the parent's client area. */
#include "mci_player.h"

#include <string.h>

int es2_mci_player_path(const char *cd_root, int index, int language, char *out, size_t capacity) {
    char name[64];
    const char *tail = name;
    size_t root, n;
    if (!cd_root || !out || es2_shell_movie_path(index, language, name, sizeof name) != 0) return -1;
    /* Original: 0x40d9e1, strcpy(buffer, CD root), strcat(buffer, path + 2
     * when path[1] is ':') */
    if (name[0] && name[1] == ':') tail = name + 2;
    root = strlen(cd_root);
    n = strlen(tail);
    if (root + n >= capacity) return -1;
    memcpy(out, cd_root, root);
    memcpy(out + root, tail, n + 1);
    return 0;
}

static int floor_scale(int64_t value, int64_t size, int64_t full) {
    int64_t n = value * size, q = n / full;
    if (n % full != 0 && n < 0) q--;
    return (int)q;
}

es2_mci_rect es2_mci_player_place(const es2_mci_rect *frame, const es2_shell_movie_rect *rect) {
    es2_mci_rect f = {0, 0, 640, 480}, out = {0, 0, 0, 0};
    if (!rect) return out;
    if (frame && frame->width > 0 && frame->height > 0) f = *frame;
    int x0 = f.x + floor_scale(rect->x, f.width, 640);
    int y0 = f.y + floor_scale(rect->y, f.height, 480);
    int x1 = f.x + floor_scale((int64_t)rect->x + rect->width, f.width, 640);
    int y1 = f.y + floor_scale((int64_t)rect->y + rect->height, f.height, 480);
    out.x = x0;
    out.y = y0;
    out.width = x1 - x0;
    out.height = y1 - y0;
    return out;
}
