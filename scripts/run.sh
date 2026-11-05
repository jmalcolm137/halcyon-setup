#!/usr/bin/env bash
# Run Halcyon Setup with the environment libgui-wayland needs.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIBGUI="${LIBGUI:-$PROJECT_ROOT/../libgui-wayland}"
SERENITY="${SERENITY:-$LIBGUI/serenity}"
LAGOM="${LAGOM:-$LIBGUI/build/lagom}"
BIN="${BIN:-$PROJECT_ROOT/build/halcyon-setup}"

if [[ ! -x "$BIN" ]]; then
    echo "error: $BIN does not exist; run 'make' first" >&2
    exit 1
fi

export SERENITY_RES_ROOT="${SERENITY_RES_ROOT:-$SERENITY/Base/res}"
export HALCYON_SETUP_MAP="${HALCYON_SETUP_MAP:-$PROJECT_ROOT/res/world-map.png}"
export HALCYON_SETUP_ICON="${HALCYON_SETUP_ICON:-$PROJECT_ROOT/res/icons/hicolor/32x32/apps/halcyon-setup.png}"
export LD_LIBRARY_PATH="$LAGOM/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if [[ "$(id -u)" != 0 && "${HALCYON_SETUP_DEMO:-0}" != 1 && " $* " != *" --demo "* ]]; then
    if command -v doas >/dev/null 2>&1; then
        exec doas env WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
            XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" \
            SERENITY_RES_ROOT="$SERENITY_RES_ROOT" \
            HALCYON_SETUP_MAP="$HALCYON_SETUP_MAP" \
            HALCYON_SETUP_ICON="$HALCYON_SETUP_ICON" \
            LD_LIBRARY_PATH="$LD_LIBRARY_PATH" "$BIN" "$@"
    elif command -v sudo >/dev/null 2>&1; then
        exec sudo env WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
            XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" \
            SERENITY_RES_ROOT="$SERENITY_RES_ROOT" \
            HALCYON_SETUP_MAP="$HALCYON_SETUP_MAP" \
            HALCYON_SETUP_ICON="$HALCYON_SETUP_ICON" \
            LD_LIBRARY_PATH="$LD_LIBRARY_PATH" "$BIN" "$@"
    fi
    echo "warning: not running as root; starting in demo mode" >&2
    exec "$BIN" --demo "$@"
fi

exec "$BIN" "$@"
