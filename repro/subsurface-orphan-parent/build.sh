#!/usr/bin/env bash
# Compile repro.c. Deps: libwayland-client dev headers (pkg-config wayland-client).
set -eu
cd "$(dirname "$0")"
cc -O0 -g -Wall repro.c -o repro $(pkg-config --cflags --libs wayland-client)
echo "OK -> ./repro"
