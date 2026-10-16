/* F-PROTO-02 repro: destroy popup-role content xdg_surface, then grab the popup.
 * Expected: Hyprland null deref in popup grab path (XDGShell.cpp:975). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

static struct wl_compositor *compositor;
static struct xdg_wm_base *wm_base;
static struct wl_seat *seat;

static void rm_global(void *d, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t ver) {
    if (!strcmp(iface, "wl_compositor"))
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, "xdg_wm_base"))
        wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 2);
    else if (!strcmp(iface, "wl_seat"))
        seat = wl_registry_bind(r, name, &wl_seat_interface, 2);
}
static void rm_global_remove(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener registry_listener = {
    rm_global, rm_global_remove
};

static void wm_base_ping(void *d, struct xdg_wm_base *w, uint32_t serial) {
    xdg_wm_base_pong(w, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = { wm_base_ping };

static void xdg_configure(void *d, struct xdg_surface *s, uint32_t serial) {}
static const struct xdg_surface_listener xdg_surface_listener = { xdg_configure };

int main(void) {
    struct wl_display *disp = wl_display_connect(NULL);
    if (!disp) { fprintf(stderr, "connect failed\n"); return 1; }
    struct wl_registry *reg = wl_display_get_registry(disp);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(disp);
    if (!compositor || !wm_base || !seat) {
        fprintf(stderr, "bind failed comp=%p xdg=%p seat=%p\n",
                (void*)compositor, (void*)wm_base, (void*)seat);
        return 1;
    }
    xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);

    /* Parent xdg_surface with a real role (toplevel). */
    struct wl_surface *ps = wl_compositor_create_surface(compositor);
    struct xdg_surface *px = xdg_wm_base_get_xdg_surface(wm_base, ps);
    xdg_surface_add_listener(px, &xdg_surface_listener, NULL);
    struct xdg_toplevel *pt = xdg_surface_get_toplevel(px);
    xdg_surface_set_window_geometry(px, 0, 0, 100, 100);
    wl_surface_commit(ps);
    wl_display_roundtrip(disp);

    /* Positioner. */
    struct xdg_positioner *posn = xdg_wm_base_create_positioner(wm_base);
    xdg_positioner_set_size(posn, 64, 64);
    xdg_positioner_set_anchor_rect(posn, 0, 0, 100, 100);
    xdg_positioner_set_anchor(posn, XDG_POSITIONER_ANCHOR_TOP_LEFT);
    xdg_positioner_set_gravity(posn, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);

    /* Popup-role content xdg_surface. */
    struct wl_surface *cs = wl_compositor_create_surface(compositor);
    struct xdg_surface *cx = xdg_wm_base_get_xdg_surface(wm_base, cs);
    xdg_surface_add_listener(cx, &xdg_surface_listener, NULL);
    struct xdg_popup *popup = xdg_surface_get_popup(cx, px, posn);
    xdg_surface_set_window_geometry(cx, 0, 0, 64, 64);
    wl_surface_commit(cs);
    wl_display_roundtrip(disp);

    /* Destroy popup content (xdg_surface + wl_surface) while popup role lives. */
    xdg_surface_destroy(cx);
    wl_surface_destroy(cs);
    wl_display_roundtrip(disp);

    fprintf(stderr, "content destroyed, issuing grab...\n");
    fflush(stderr);

    /* Grab the surviving popup: compositor must not deref expired content. */
    xdg_popup_grab(popup, seat, 12345);
    wl_display_flush(disp);
    fprintf(stderr, "grab flushed, waiting for reply...\n");
    fflush(stderr);

    int rc = wl_display_roundtrip(disp);
    fprintf(stderr, "roundtrip returned %d (display error: %s)\n", rc,
            wl_display_get_error(disp) ? "yes" : "no");
    return 0;
}
