#!/usr/bin/env bash
# Compile repro.c. Deps: wayland-scanner, libwayland-client dev headers.
set -eu
cd "$(dirname "$0")"
XDG="$(pkg-config --variable=pkgdatadir wayland-protocols)/stable/xdg-shell/xdg-shell.xml"
wayland-scanner client-header "$XDG" xdg-shell-client-protocol.h
wayland-scanner private-code  "$XDG" xdg-shell-protocol.c
cc -O0 -g -Wall repro.c xdg-shell-protocol.c -o repro -lwayland-client
echo "OK -> ./repro"
