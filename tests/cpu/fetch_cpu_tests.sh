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

# Part 3: unofficial opcodes (stable ones only)
PART3="1a 3a 5a 7a da fa 80 82 89 c2 e2 04 44 64 14 34 54 74 d4 f4 0c 1c 3c 5c 7c dc fc
a7 b7 af bf a3 b3 87 97 8f 83 eb
c7 d7 cf df db c3 d3 e7 f7 ef ff fb e3 f3
07 17 0f 1f 1b 03 13 27 37 2f 3f 3b 23 33
47 57 4f 5f 5b 43 53 67 77 6f 7f 7b 63 73
0b 2b 4b 6b cb"

OPS="${*:-$PART1 $PART2 $PART3}"
for op in $OPS; do
  if [ ! -f "$op.json" ]; then
    curl -fsSL -o "$op.json" "$BASE/$op.json"
    printf '.'
  fi
done
echo " done"
