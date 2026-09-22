#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
# Mesa EGL surfaceless executes the GLES shader and restores real driver state.
# This is a host regression, not a Switch hardware result.
mesa=${KISAKU_MESA_PREFIX:-/opt/homebrew/opt/mesa}
font=${1:-local/fonts/arshanghaisonggbpro_lt.otf}
mkdir -p build
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -Iruntime -I"$mesa/include" \
  tests/present_gles_test.c -o build/present-gles-test
build/present-gles-test
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -Iruntime -I"$mesa/include" \
  tests/present_gles_render_test.c runtime/font.c $(pkg-config --cflags --libs freetype2) \
  -L"$mesa/lib" -lEGL -lGLESv2 -o build/present-gles-render-test
EGL_PLATFORM=surfaceless build/present-gles-render-test "$font"

# Exercise the actual SDL GLES backend and original game menu assets.
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -Iruntime $(pkg-config --cflags sdl2) \
    runtime/lzss.c runtime/ai6arc.c runtime/rmt.c runtime/akb.c runtime/vm.c runtime/mov.c runtime/ax.c runtime/mam.c runtime/video.c runtime/flags.c runtime/gallery.c runtime/control_store.c runtime/save_slot.c runtime/scene.c runtime/scene_view.c runtime/title.c runtime/flag_dialog.c runtime/scene_history.c runtime/text_encoding.c runtime/text_layout.c runtime/font.c runtime/read_flags.c runtime/voice_worker.c runtime/image_worker.c runtime/present_filter.c runtime/bootstrap.c runtime/switch_hos.c runtime/image_sdl.c tests/present_sdl_test.c \
    -lSDL2_test $(pkg-config --libs sdl2) $(pkg-config --cflags --libs libavformat libavcodec libswscale libswresample freetype2) -I"$mesa/include" -L"$mesa/lib" -lGLESv2 -o build/present-sdl-test

saves=$(mktemp -d "${TMPDIR:-/tmp}/kisaku-gles.XXXXXX")
trap 'rm -rf "$saves"' EXIT HUP INT TERM
SDL_VIDEODRIVER=offscreen SDL_RENDER_DRIVER=opengles2 \
SDL_VIDEO_EGL_DRIVER="$mesa/lib/libEGL.dylib" \
SDL_OPENGL_LIBRARY="$mesa/lib/libGLESv2.dylib" EGL_PLATFORM=surfaceless \
build/present-sdl-test "${2:-鬼作}" "$saves" "$font"
