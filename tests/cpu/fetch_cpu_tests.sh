#!/usr/bin/env sh
# Downloads per-opcode test files from SingleStepTests (nes6502 set) into this folder.
# Each file is ~3 MB, so only the opcodes you've implemented are fetched by default.
# Pass opcodes as arguments to fetch others:   sh fetch_cpu_tests.sh 20 60 48
set -e
cd "$(dirname "$0")"
BASE="https://raw.githubusercontent.com/SingleStepTests/65x02/main/nes6502/v1"

# Part 1: loads, stores, transfers, flags, inc/dec, JMP, NOP
PART1="a9 a5 b5 ad bd b9 a1 b1 a2 a6 b6 ae be a0 a4 b4 ac bc
85 95 8d 9d 99 81 91 86 96 8e 84 94 8c
aa a8 8a 98 ba 9a 18 38 58 78 d8 f8 b8
e8 c8 ca 88 e6 f6 ee fe c6 d6 ce de 4c 6c ea"

# Part 2: stack, subroutines/interrupts, branches, arithmetic, compares, logic, shifts
PART2="48 08 68 28 20 60 40 00
10 30 50 70 90 b0 d0 f0
69 65 75 6d 7d 79 61 71 e9 e5 f5 ed fd f9 e1 f1
c9 c5 d5 cd dd d9 c1 d1 e0 e4 ec c0 c4 cc
29 25 35 2d 3d 39 21 31 09 05 15 0d 1d 19 01 11
49 45 55 4d 5d 59 41 51 24 2c
0a 06 16 0e 1e 4a 46 56 4e 5e 2a 26 36 2e 3e 6a 66 76 6e 7e"

OPS="${*:-$PART1 $PART2}"
for op in $OPS; do
  if [ ! -f "$op.json" ]; then
    curl -fsSL -o "$op.json" "$BASE/$op.json"
    printf '.'
  fi
done
echo " done"
