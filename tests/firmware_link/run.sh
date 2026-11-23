#!/bin/sh
# Builds and runs test_link.cpp, with ArduinoJson from the firmware build (pio run first).
cd "$(dirname "$0")" || exit 1
SRC=../../firmware/src
JSON=../../firmware/.pio/libdeps/e32r24p/ArduinoJson/src
g++ -std=c++17 -Wall -I$SRC -I$JSON test_link.cpp $SRC/link_frame.cpp $SRC/link_model.cpp $SRC/exchange.cpp \
  -o /tmp/test_firmware_link && /tmp/test_firmware_link
