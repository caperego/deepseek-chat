#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ -z "${QMAKE:-}" ]; then
    if command -v qmake6 >/dev/null 2>&1; then
        QMAKE=qmake6
    elif command -v qmake >/dev/null 2>&1; then
        QMAKE=qmake
    else
        echo "Erro: qmake/qmake6 não encontrado no PATH." >&2
        echo "Linux: sudo pacman -S qt6-base qt6-declarative qt6-multimedia qt6-speech qt6-svg qt6-webengine" >&2
        echo "macOS: brew install qt" >&2
        exit 1
    fi
fi

MAKE_BIN="${MAKE_BIN:-make}"
if ! command -v "$MAKE_BIN" >/dev/null 2>&1; then
    MAKE_BIN="$(command -v gmake || command -v make)"
fi

mkdir -p build
cd build

echo "==> qmake ($QMAKE)"
"$QMAKE" ../deepseek-chat.pro

echo "==> compilando"
"$MAKE_BIN" -j"$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu )"

echo
if [ -d "$SCRIPT_DIR/bin/deepseek-chat.app" ]; then
    echo "App gerado em: $SCRIPT_DIR/bin/deepseek-chat.app"
    echo "Para distribuir no macOS: macdeployqt bin/deepseek-chat.app"
else
    echo "Binário gerado em: $SCRIPT_DIR/bin/deepseek-chat"
fi
echo "Execute com: $SCRIPT_DIR/bin/deepseek-chat"
