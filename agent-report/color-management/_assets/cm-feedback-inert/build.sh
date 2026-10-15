#!/usr/bin/env bash
# Compile repro.c: generate the Wayland protocol stubs locally and link.
# Deps: wayland-scanner, libwayland-client dev headers (pkg-config wayland-client).
set -eu
cd "$(dirname "$0")"
WPROT="$(pkg-config --variable=pkgdatadir wayland-protocols)"
CM="$WPROT/staging/color-management/color-management-v1.xml"
[[ -f "$CM" ]] || { echo "color-management-v1.xml not found (need wayland-protocols >= 1.49): $CM"; exit 1; }
wayland-scanner client-header "$CM" color-management-v1-client-protocol.h
wayland-scanner private-code  "$CM" color-management-v1-protocol.c
cc -O0 -g -Wall repro.c color-management-v1-protocol.c -o repro -lwayland-client
echo "OK -> ./repro"
