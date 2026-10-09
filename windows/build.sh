#!/bin/sh
# Cross-compile MatrixRain.scr with MinGW-w64 (Ubuntu: apt install mingw-w64).
set -e
cd "$(dirname "$0")"
x86_64-w64-mingw32-windres resources.rc -O coff -o resources.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -municode -mwindows -static -s \
  -o MatrixRain.scr main.cpp resources.o \
  -lcomctl32 -lbcrypt -lgdi32 -luser32 -ladvapi32 -lshell32 -lwinmm
rm -f resources.o
ls -l MatrixRain.scr
