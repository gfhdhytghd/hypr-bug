#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include "color-management-v1-client-protocol.h"

/*
 * Client for bug: wp_color_management_surface_feedback_v1 never becomes inert
 * when its wl_surface is destroyed (ColorManagement.cpp:460 derefs an expired
 * WP on the next output image-description change → main-thread SIGSEGV).
 *
 * Trigger: run this, then in another terminal change the output image
 * description, e.g.  hyprctl keyword monitor <output>,add,cm,hdr
 * (anything that changes the description tuple works: cm, sdr_eotf,
 *  bitdepth with cm=auto, or max/min_luminance while in hdr).
 *
 * Baseline: Hyprland cm/tonemap-ref-scaling @ 30e438818, render:cm_enabled=1.
 */

static struct wl_display *dpy;
static struct wl_compositor *compo;
static struct wp_color_manager_v1 *cmgr;

static void reg_global(void *d, struct wl_registry *r, uint32_t n, const char *iface, uint32_t ver) {
    (void)d;
    if (!strcmp(iface, "wl_compositor"))
        compo = wl_registry_bind(r, n, &wl_compositor_interface, 4);
    else if (!strcmp(iface, "wp_color_manager_v1"))
        cmgr = wl_registry_bind(r, n, &wp_color_manager_v1_interface, ver < 1 ? 1 : ver);
}
static const struct wl_registry_listener reg_li = { .global = reg_global };

int main(void) {
    if (!(dpy = wl_display_connect(NULL))) { fprintf(stderr, "cannot connect to Wayland display\n"); return 2; }
    struct wl_registry *r = wl_display_get_registry(dpy);
    wl_registry_add_listener(r, &reg_li, NULL);
    wl_display_roundtrip(dpy);
    if (!cmgr || !compo) { fprintf(stderr, "wl_compositor / wp_color_manager_v1 not advertised (render:cm_enabled?)\n"); return 2; }

    struct wl_surface *s = wl_compositor_create_surface(compo);
    struct wp_color_management_surface_feedback_v1 *fb = wp_color_manager_v1_get_surface_feedback(cmgr, s);
    (void)fb;
    wl_display_roundtrip(dpy);

    wl_surface_destroy(s); /* surface dies; feedback must become inert per spec — it doesn't */
    printf("[*] surface destroyed, feedback still parked in the server-side list\n");
    printf("[*] in another terminal run, e.g.:\n"
           "    hyprctl keyword monitor <output>,add,cm,hdr\n"
           "    (also works: ,add,sdr_eotf,gamma22 | ,add,bitdepth,10 with cm=auto | under hdr: ,add,max_luminance,600)\n");
    printf("[*] press Enter when done…\n");
    fflush(stdout);
    getchar();

    printf("[*] waiting for the unguarded broadcast crash (ColorManagement.cpp:460)…\n");
    fflush(stdout);
    while (wl_display_dispatch(dpy) != -1)
        ;
    printf("[!] connection dropped — verify hyprland died with SIGSEGV (onPreferredChanged at stack top)\n");
    return 0;
}
