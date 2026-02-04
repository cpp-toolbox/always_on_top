#!/bin/bash
windres resource.rc -o resource.o
g++ -o always_on_top.exe main.cpp resource.o -mwindows -luser32 -lshell32 -lgdi32 -ldwmapi -DUNICODE -D_UNICODE -static -static-libgcc -static-libstdc++
