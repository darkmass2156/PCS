#!/bin/bash
# Arduino-IDE-equivalent command-line build for "Arduino Mega or Mega 2560" (ArduinoCore-avr platform.txt flags,
# LTO, --gc-sections), used to check every sketch compiles and links without the IDE. Not needed for uploading.
# Needs avr-gcc 7.3 + avr-libc (Ubuntu: apt install gcc-avr avr-libc binutils-avr, same compiler as the IDE).
# usage: tools/avr_build/build.sh <sketch dir>      e.g. tools/avr_build/build.sh firmware/controller
#        tools/avr_build/build.sh all               every sketch: firmware/*, tests/NN_*
# Sketch warnings are on (-Wall -Wextra), core/libraries are compiled with -w like the IDE.
# Dependencies are cloned once into tools/avr_build/.cache (git-ignored).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
B=$HERE/.cache
mkdir -p "$B"
for r in arduino/ArduinoCore-avr adafruit/Adafruit-ST7735-Library adafruit/Adafruit-GFX-Library adafruit/Adafruit_BusIO; do
  [ -d "$B/$(basename $r)" ] || git clone -q --depth 1 https://github.com/$r "$B/$(basename $r)"
done
if [ "$1" = all ]; then
  ROOT=$(cd "$HERE/../.." && pwd); rc=0
  for s in "$ROOT"/firmware/*/ "$ROOT"/tests/[0-9]*/; do
    printf '%-24s ' "$(basename "$s")"
    if out=$("$0" "${s%/}" 2>&1); then echo "$out" | awk '/Program|Data/ {printf "%s %s %s  ", $1, $2, $4} END {print ""}'
      echo "$out" | grep "warning:" | grep -v "missing initializer" || true
    else echo FAILED; echo "$out" | grep -E "error|warning" | head -20; rc=1; fi
  done
  exit $rc
fi
SK=$(cd "$1" && pwd); NAME=$(basename "$SK"); OUT=$B/out/$NAME; rm -rf "$OUT"; mkdir -p "$OUT"
CORE=$B/ArduinoCore-avr
MCU="-mmcu=atmega2560 -DF_CPU=16000000L -DARDUINO=10819 -DARDUINO_AVR_MEGA2560 -DARDUINO_ARCH_AVR"
INC="-I$CORE/cores/arduino -I$CORE/variants/mega -I$CORE/libraries/SPI/src -I$CORE/libraries/Wire/src -I$CORE/libraries/EEPROM/src -I$B/Adafruit-GFX-Library -I$B/Adafruit_BusIO -I$B/Adafruit-ST7735-Library -I$SK"
CF="-c -g -Os -std=gnu11 -ffunction-sections -fdata-sections -flto -fno-fat-lto-objects $MCU"
XF="-c -g -Os -std=gnu++11 -fpermissive -fno-exceptions -ffunction-sections -fdata-sections -fno-threadsafe-statics -Wno-error=narrowing -flto $MCU"
# core + libraries (cached), compiled with -w like the IDE
if [ ! -f $B/out/libs.a ]; then
  mkdir -p $B/out/libs; cd $B/out/libs
  for f in $CORE/cores/arduino/*.c $CORE/libraries/Wire/src/utility/*.c; do avr-gcc $CF -w $INC "$f" -o "$(basename $f).o"; done
  for f in $CORE/cores/arduino/*.cpp $CORE/libraries/SPI/src/*.cpp $CORE/libraries/Wire/src/*.cpp \
           $B/Adafruit-GFX-Library/*.cpp $B/Adafruit_BusIO/*.cpp $B/Adafruit-ST7735-Library/*.cpp; do
    avr-g++ $XF -w $INC "$f" -o "$(basename $f).o"; done
  for f in $CORE/cores/arduino/*.S; do avr-gcc -c -g -x assembler-with-cpp -flto $MCU $INC "$f" -o "$(basename $f).o"; done
  avr-gcc-ar rcs $B/out/libs.a *.o
fi
# sketch: .ino -> .cpp with Arduino.h first (the IDE also adds prototypes; all our sketches define before use)
cd "$OUT"
{ echo '#include <Arduino.h>'; echo "#line 1 \"$SK/$NAME.ino\""; cat "$SK/$NAME.ino"; } > "$NAME.ino.cpp"
avr-g++ $XF -Wall -Wextra -Wno-unused-parameter $INC "$NAME.ino.cpp" -o "$NAME.ino.o"
for f in "$SK"/*.cpp; do [ -e "$f" ] || continue; avr-g++ $XF -Wall -Wextra -Wno-unused-parameter $INC "$f" -o "$(basename $f).o"; done
avr-gcc -w -Os -g -flto -fuse-linker-plugin -Wl,--gc-sections,--relax -mmcu=atmega2560 -o "$NAME.elf" *.o $B/out/libs.a -lm
avr-size -C --mcu=atmega2560 "$NAME.elf" | grep -E "Program|Data"
