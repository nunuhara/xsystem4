#!/usr/bin/env bash
# Linux Mesa/GLES integration runner; see README.md for dependencies.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=${1:-"$ROOT/../build/parts-gles-test"}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
: "${SDL2_INCLUDE:?Set SDL2_INCLUDE to the directory containing SDL.h}"
: "${CGLM_INCLUDE:?Set CGLM_INCLUDE to the directory containing cglm/cglm.h}"
: "${GLES_INCLUDE:=/usr/include}"
"${CC:-cc}" -std=gnu11 -O0 -g -ffunction-sections -fdata-sections -include string.h \
    -DUSE_GLES "-DXSYS4_DATA_DIR=\"$ROOT\"" \
    -I"$SDL2_INCLUDE" -I"$CGLM_INCLUDE" -I"$GLES_INCLUDE" \
    -I"$ROOT/include" -I"$ROOT/subprojects/libsys4/include" \
    "$ROOT/test/native/gl/renderer_test.c" \
    "$ROOT/test/native/gl/construction_wrapper.c" \
    "$ROOT/test/native/gl/render_wrapper.c" \
    "$ROOT/test/native/gl/flat_wrapper.c" \
    "$ROOT/test/native/gl/renderer3d_wrapper.c" \
    "$ROOT/test/native/gl/api_wrapper.c" \
    "$ROOT/test/native/gl/activity_wrapper.c" \
    "$ROOT/subprojects/libsys4/src/ex.c" \
    "$ROOT/subprojects/libsys4/src/mt19937int.c" \
    "$ROOT/src/parts/parts.c" "$ROOT/src/draw.c" "$ROOT/subprojects/libsys4/src/hashtable.c" \
    -Wl,--gc-sections -Wl,--wrap=parts_get -Wl,--wrap=glDrawElements \
    -Wl,--wrap=parts_build_construction_process -Wl,--wrap=SDL_GL_SwapWindow -Wl,--wrap=SDL_GL_CreateContext \
    -l:libSDL2-2.0.so.0 -l:libGLESv2.so.2 -lm -o "$OUT/renderer-test"
cd "$OUT"
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 ./renderer-test
