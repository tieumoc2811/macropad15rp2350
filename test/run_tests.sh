#!/bin/sh
# Chay: sh test/run_tests.sh   (can gcc; node de doi chieu ban JS cua trang web)
set -e
cd "$(dirname "$0")"
mkdir -p build
# 1) trich NGUYEN VAN ham mc_ascii tu firmware (de test dung ma that, khong phai ban sao)
awk '/^static uint16_t mc_ascii\(/{f=1} f{print} f&&/^}/{exit}' ../firmware/main.c > build/mc_ascii.inc
test -s build/mc_ascii.inc || { echo "khong trich duoc mc_ascii"; exit 1; }
# 2) test don vi + fuzz cho morse.h
gcc -std=gnu11 -Wall -Wextra -Wno-unused-function -O1 -fsanitize=address,undefined -o build/morse_test morse_test.c
./build/morse_test
# 3) mo phong END-TO-END: nhung chinh firmware/main.c vao khung gia (stub/ thay Pico SDK + TinyUSB), bam phim vat ly, giai ma bao cao HID nhu may tinh
gcc -std=gnu11 -w -O1 -fsanitize=address,undefined -Istub -I../firmware -o build/fw_sim fw_sim.c
./build/fw_sim
# 4) doi chieu bang ma + bo cuc layer 3 + thuat toan giua firmware (C) va trang thai web (JS)
if command -v node >/dev/null 2>&1; then node cross_check.js; else echo "(bo qua doi chieu JS: khong co node)"; fi
