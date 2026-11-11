#!/usr/bin/env bash
# Headless smoke test: run the wizard as a native Wayland client under
# libgui-wayland's headless compositor and capture one frame per page.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$PROJECT_ROOT/build/halcyon-setup"
LIBGUI="${LIBGUI:-$PROJECT_ROOT/../libgui-wayland}"
HC="${HC:-$LIBGUI/build/xlib-wayland/headless-compositor}"
SERENITY="${SERENITY:-$LIBGUI/serenity}"
LAGOM="${LAGOM:-$LIBGUI/build/lagom}"

if [[ ! -x "$BIN" ]]; then
    echo "error: $BIN is missing; run 'make' first" >&2
    exit 1
fi
if [[ ! -x "$HC" ]]; then
    echo "skip: headless compositor not found at $HC" >&2
    exit 0
fi

TMPDIR="$(mktemp -d "${TMPDIR:-/tmp}/halcyon-setup-ui.XXXXXX")"
chmod 700 "$TMPDIR"
trap 'rm -rf "$TMPDIR"' EXIT

run_page() {
    local page="$1"
    local runtime="$TMPDIR/$page"
    mkdir -p "$runtime"

    cat > "$runtime/input.txt" <<'EOF'
sleep 2500
screenshot
sleep 300
EOF

    XDG_RUNTIME_DIR="$runtime" "$HC" \
        --socket "halcyon-$page" --size 900x700 --timeout 4 \
        --output "$runtime/frame.png" --input "$runtime/input.txt" \
        > "$runtime/compositor.log" 2>&1 &
    local hc_pid=$!

    for _ in $(seq 1 100); do
        [[ -e "$runtime/halcyon-$page" ]] && break
        sleep 0.05
    done

    set +e
    env XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="halcyon-$page" \
        SERENITY_RES_ROOT="$SERENITY/Base/res" \
        HALCYON_SETUP_MAP="$PROJECT_ROOT/res/world-map.png" \
        HALCYON_SETUP_ICON="$PROJECT_ROOT/res/icons/hicolor/32x32/apps/halcyon-setup.png" \
        LD_LIBRARY_PATH="$LAGOM/lib" \
        HALCYON_SETUP_TEST_PAGE="$page" \
        timeout 8 "$BIN" --demo > "$runtime/app.log" 2>&1
    local status=$?
    set -e

    wait "$hc_pid" 2>/dev/null || true

    if [[ $status -ge 128 ]]; then
        echo "FAIL: $page crashed (status $status)" >&2
        cat "$runtime/app.log" >&2
        return 1
    fi
    if ! grep -q "presented window" "$runtime/app.log"; then
        echo "FAIL: $page never presented a window" >&2
        cat "$runtime/app.log" >&2
        return 1
    fi
    if [[ ! -s "$runtime/frame.png" ]]; then
        echo "FAIL: $page produced no frame" >&2
        cat "$runtime/compositor.log" >&2
        return 1
    fi
    echo "  $page: window presented and frame captured"
}

for page in welcome disk mounts timezone accounts options summary finished; do
    run_page "$page"
done

echo "UI smoke test passed"
