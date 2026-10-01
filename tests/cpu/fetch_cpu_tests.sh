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

OPS="${*:-$PART1}"
for op in $OPS; do
  if [ ! -f "$op.json" ]; then
    curl -fsSL -o "$op.json" "$BASE/$op.json"
    printf '.'
  fi
done
echo " done"
