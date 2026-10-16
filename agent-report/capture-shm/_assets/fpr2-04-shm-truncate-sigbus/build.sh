#!/usr/bin/env bash
# Compile repro.c. Deps: libwayland-client dev headers. Uses only core wl_shm.
set -eu
cd "$(dirname "$0")"
cc -O0 -g -Wall repro.c -o repro -lwayland-client
echo "OK -> ./repro"
