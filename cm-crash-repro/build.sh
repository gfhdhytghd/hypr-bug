#!/usr/bin/env bash
# 编译 repro.c：现场生成 wayland 协议桩并链接。
# 依赖：wayland-scanner、libwayland-client 开发头文件（pkg-config wayland-client）。
set -eu
cd "$(dirname "$0")"

WPROT="$(pkg-config --variable=pkgdatadir wayland-protocols)"
CM="$WPROT/staging/color-management/color-management-v1.xml"
XDG="$WPROT/stable/xdg-shell/xdg-shell.xml"
[[ -f "$CM" ]] || { echo "找不到 color-management-v1.xml（需要 wayland-protocols ≥ 1.49）：$CM"; exit 1; }

wayland-scanner client-header "$CM"  color-management-v1-client-protocol.h
wayland-scanner private-code  "$CM"  color-management-v1-protocol.c
wayland-scanner client-header "$XDG" xdg-shell-client-protocol.h
wayland-scanner private-code  "$XDG" xdg-shell-protocol.c

cc -O0 -g -Wall repro.c color-management-v1-protocol.c xdg-shell-protocol.c -o repro -lwayland-client
echo "OK -> ./repro"
