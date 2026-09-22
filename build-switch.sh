#!/bin/sh
set -eu
cd "$(dirname "$0")"
python3 tools/native_media_tables.py "${KISAKU_EXE:-鬼作/AI6WIN.exe}" build/media_tables.h
python3 tools/native_bowling_tables.py "${KISAKU_EXE:-鬼作/AI6WIN.exe}" build/bowling_tables.h
dkp=${DEVKITPRO:-/opt/devkitpro}
mkdir -p build-switch
PKG_CONFIG_LIBDIR="$dkp/portlibs/switch/lib/pkgconfig"
export PKG_CONFIG_LIBDIR
"$dkp/devkitA64/bin/aarch64-none-elf-gcc" -std=c11 -O2 -Wall -Wextra -Werror -Iruntime \
  -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ \
  -I"$dkp/libnx/include" -L"$dkp/libnx/lib" -specs="$dkp/libnx/switch.specs" \
  runtime/lzss.c runtime/ai6arc.c runtime/rmt.c runtime/akb.c runtime/vm.c runtime/mov.c runtime/ax.c runtime/mam.c runtime/video.c runtime/flags.c runtime/gallery.c runtime/control_store.c runtime/save_slot.c runtime/scene.c runtime/scene_view.c runtime/title.c runtime/flag_dialog.c runtime/scene_history.c runtime/text_encoding.c runtime/text_layout.c runtime/font.c runtime/read_flags.c runtime/voice_worker.c runtime/image_worker.c runtime/bootstrap.c runtime/switch_hos.c tools/bootstrap_probe.c \
  $("$dkp/tools/bin/pkg-config" --cflags --libs libavformat libavcodec libswscale libswresample freetype2) -lpthread -lnx -o build-switch/kisaku-bootstrap.elf
"$dkp/tools/bin/nacptool" --create 'KISAKU Bootstrap Diagnostic' 'kisaku port project' '0.3.0' build-switch/kisaku-bootstrap.nacp
"$dkp/tools/bin/elf2nro" build-switch/kisaku-bootstrap.elf build-switch/kisaku-bootstrap.nro --nacp=build-switch/kisaku-bootstrap.nacp
"$dkp/devkitA64/bin/aarch64-none-elf-gcc" -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter \
  -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ \
  -I"$dkp/libnx/include" -L"$dkp/libnx/lib" -specs="$dkp/libnx/switch.specs" \
  runtime/lzss.c runtime/ai6arc.c runtime/probe.c -lpthread -lnx -o build-switch/kisaku-diagnostic.elf
"$dkp/tools/bin/nacptool" --create 'KISAKU AI6WIN Diagnostic' 'kisaku port project' '0.1.0' build-switch/kisaku-diagnostic.nacp
"$dkp/tools/bin/elf2nro" build-switch/kisaku-diagnostic.elf build-switch/kisaku-diagnostic.nro --nacp=build-switch/kisaku-diagnostic.nacp
PKG_CONFIG_LIBDIR="$dkp/portlibs/switch/lib/pkgconfig"
export PKG_CONFIG_LIBDIR
"$dkp/devkitA64/bin/aarch64-none-elf-gcc" -std=c11 -O2 -Wall -Wextra -Werror -Iruntime \
  $($dkp/tools/bin/pkg-config --cflags sdl2) -fPIE -specs="$dkp/libnx/switch.specs" \
  runtime/lzss.c runtime/ai6arc.c runtime/rmt.c runtime/akb.c runtime/image_sdl.c tools/image_viewer.c \
  -L"$dkp/portlibs/switch/lib" -lSDL2_test $($dkp/tools/bin/pkg-config --libs sdl2) -lm \
  -o build-switch/kisaku-image-viewer.elf
"$dkp/tools/bin/nacptool" --create 'KISAKU Resource Viewer' 'kisaku port project' '0.2.0' build-switch/kisaku-image-viewer.nacp
"$dkp/tools/bin/elf2nro" build-switch/kisaku-image-viewer.elf build-switch/kisaku-image-viewer.nro --nacp=build-switch/kisaku-image-viewer.nacp

"$dkp/devkitA64/bin/aarch64-none-elf-gcc" -std=c11 -O2 -Wall -Wextra -Werror -Iruntime \
  $("$dkp/tools/bin/pkg-config" --cflags sdl2) -fPIE -specs="$dkp/libnx/switch.specs" \
  runtime/lzss.c runtime/ai6arc.c runtime/rmt.c runtime/akb.c runtime/vm.c runtime/mov.c runtime/ax.c runtime/mam.c runtime/video.c runtime/flags.c runtime/gallery.c runtime/control_store.c runtime/save_slot.c runtime/scene.c runtime/scene_view.c runtime/title.c runtime/flag_dialog.c runtime/scene_history.c runtime/text_encoding.c runtime/text_layout.c runtime/font.c runtime/read_flags.c runtime/voice_worker.c runtime/image_worker.c runtime/present_filter.c runtime/bootstrap.c runtime/switch_hos.c runtime/image_sdl.c tools/runtime_viewer.c \
  -L"$dkp/portlibs/switch/lib" -lSDL2_test $("$dkp/tools/bin/pkg-config" --libs sdl2) $("$dkp/tools/bin/pkg-config" --cflags --libs glesv2) -lm \
  $("$dkp/tools/bin/pkg-config" --cflags --libs libavformat libavcodec libswscale libswresample freetype2) -lpthread -lnx -o build-switch/kisaku-runtime.elf
"$dkp/tools/bin/nacptool" --create 'Kisaku Port Preview' 'kisaku-switch contributors' '0.1.0' build-switch/kisaku.nacp
# Original-game artwork stays local, outside the source repository.
set --
if [ -f icon.png ]; then set -- --icon=icon.png
elif [ -f kisaku-icon.jpg ]; then set -- --icon=kisaku-icon.jpg
elif [ -f local/kisaku-icon.jpg ]; then set -- --icon=local/kisaku-icon.jpg
fi
"$dkp/tools/bin/elf2nro" build-switch/kisaku-runtime.elf build-switch/kisaku.nro --nacp=build-switch/kisaku.nacp "$@"

# Keep the earlier preview filename byte-identical to the main entry point.
cp build-switch/kisaku.nro build-switch/kisaku-preview.nro
