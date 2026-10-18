#!/bin/sh
# Run inside the existing, licensed ARM/QNX cross-build container, offline.
# Usage: sh tools/build.sh /local/tuning.conf
set -eu
cd /work
. "$1"
CC=/gcc-out/bin/arm-unknown-nto-qnx6.5.0eabi-gcc
READELF=readelf
mkdir -p .build/common .build/790 .build/791 validation
base='-std=gnu99 -O2 -Wall -Wextra -mno-unaligned-access -static-libgcc'
common='-DCNS2_ALTSCREEN_PASSIVE_V2=1 -DCNS2_ALTSCREEN_AES_V3=1 -DCNS2_ALTSCREEN_LIVE_V4=1 -DCNS2_ALTSCREEN_CHACHA_V5=1 -DCNS2_ALTSCREEN_LIFECYCLE_V7=1 -DCNS2_ALTSCREEN_REFRESH=1 -DCNS2_ALTSCREEN_SHOWUI_V9=1 -DCNS2_ALTSCREEN_PERSISTENT_LOG=1 -DCNS2_AIRPLAY_RUNTIME_BASE=0x78060000U'
build_video() {
    model=$1 fps=$2 period=$3 width=$4 height=$5 pw=$6 ph=$7 sx=$8 sy=$9
    shift 9
    sw=$1 sh=$2
    case "$period" in ''|*[!0-9]*) exit 2;; esac
    [ "$period" -gt 0 ] && [ "$period" -le 120 ]
    "$CC" $base -fPIC -shared $common \
        -DCNS2_DISPLAY_FPS="$fps" -DCNS2_STREAM_RECOVERY_PERIOD="$period" \
        -DCNS2_DISPLAY_WIDTH="$width" -DCNS2_DISPLAY_HEIGHT="$height" \
        -DCNS2_PHYSICAL_WIDTH="$pw" -DCNS2_PHYSICAL_HEIGHT="$ph" \
        -DCNS2_SAFE_X="$sx" -DCNS2_NAV_X="$sx" -DCNS2_SAFE_Y="$sy" \
        -DCNS2_SAFE_WIDTH="$sw" -DCNS2_SAFE_HEIGHT="$sh" \
        source/producer/cns2_carplay_altscreen_declare_v1.c \
        source/producer/u64_divmod.c source/producer/aeabi_uldivmod.S \
        -o ".build/$model/libcns2-carplay-altscreen-live-v9-showui-persistlog.so" -lc
}
build_video 790 30 "$RECOVERY_790" 1010 376 202 75 202 16 606 344
build_video 791 40 "$RECOVERY_791" 800 480 160 96 160 20 480 440
"$CC" $base -fPIC -shared -Wno-unused-function -Wno-unused-variable \
    source/most/most-pull.c source/producer/u64_divmod.c \
    source/producer/aeabi_uldivmod.S source/most/signed-divmod.c source/most/aeabi-ldivmod.S \
    -o .build/common/libcns2-most-pull-r43.so -lc
"$CC" $base -fPIC -shared \
    '-DCNS2_AUTOLOAD_LIBRARY="/ramdisk/.cns2-v43/libcns2-carplay-altscreen-live-v9-showui-persistlog.so"' \
    '-DCNS2_AUTOLOAD_SYMBOL="arm_declare_v1"' \
    '-DCNS2_AUTOLOAD_LOG="/ramdisk/.cns2-v43-state/carplay-autoload.log"' \
    source/carplay-autoload.c -o .build/common/libcns2-autoload-carplay-v9.so -lc
"$CC" $base -Isource/vendor source/install-tool.c source/serial_reader.c source/vendor/monocypher.c \
    source/producer/u64_divmod.c source/producer/aeabi_uldivmod.S \
    -o .build/common/cns2-tool -lsocket -lc
for file in .build/common/* .build/790/* .build/791/*; do
    "$READELF" -h "$file" | grep -q 'Machine:.*ARM'
    "$READELF" -Ws "$file" > validation/symbols.tmp
    if grep 'UND.*__aeabi_uldivmod' validation/symbols.tmp; then exit 3; fi
done
echo 'ARM/QNX build passed for both independent profiles'
