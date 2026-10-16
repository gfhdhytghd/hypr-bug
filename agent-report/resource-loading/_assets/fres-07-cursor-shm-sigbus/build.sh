#!/usr/bin/env bash
# Compile repro.c. Deps: wayland-scanner, libwayland-client dev headers.
# xdg-shell comes from wayland-protocols; wlr-virtual-pointer xml is bundled here.
set -eu
cd "$(dirname "$0")"
XDG="$(pkg-config --variable=pkgdatadir wayland-protocols)/stable/xdg-shell/xdg-shell.xml"
VP="./wlr-virtual-pointer-unstable-v1.xml"
wayland-scanner client-header "$XDG" xdg-shell-client-protocol.h
wayland-scanner private-code  "$XDG" xdg-shell-protocol.c
wayland-scanner client-header "$VP"  virtual-pointer-client-protocol.h
wayland-scanner private-code  "$VP"  virtual-pointer-protocol.c
cc -O0 -g -Wall repro.c xdg-shell-protocol.c virtual-pointer-protocol.c -o repro -lwayland-client
echo "OK -> ./repro"
