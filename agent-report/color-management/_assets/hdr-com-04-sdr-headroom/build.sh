#!/usr/bin/env bash
# Compile cminfo.c (read-only color-management info probe).
# Deps: wayland-scanner, libwayland-client dev headers, wayland-protocols >= 1.49
# (for staging/color-management/color-management-v1.xml).
set -eu
cd "$(dirname "$0")"
CM="$(pkg-config --variable=pkgdatadir wayland-protocols)/staging/color-management/color-management-v1.xml"
wayland-scanner client-header "$CM" color-management-v1-client-protocol.h
wayland-scanner private-code  "$CM" color-management-v1-protocol.c
cc -O0 -g -Wall cminfo.c color-management-v1-protocol.c -o cminfo -lwayland-client
echo "OK -> ./cminfo [output-name]   (default eDP-1)"
