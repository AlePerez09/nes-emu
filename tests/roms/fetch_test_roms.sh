#!/usr/bin/env sh
# Downloads freely distributed test ROMs into this folder (gitignored, so the repo stays ROM-free).
#   nestest.nes + nestest.log   CPU trace test
#   ppu/*.nes                   PPU timing tests and demos that report results on screen
set -e
cd "$(dirname "$0")"
BASE="https://raw.githubusercontent.com/christopherpow/nes-test-roms/master"

curl -fsSL -o nestest.nes "$BASE/other/nestest.nes"
curl -fsSL -o nestest.log "$BASE/other/nestest.log"

mkdir -p ppu
for path in \
  branch_timing_tests/1.Branch_Basics.nes \
  branch_timing_tests/2.Backward_Branch.nes \
  branch_timing_tests/3.Forward_Branch.nes \
  cpu_timing_test6/cpu_timing_test.nes \
  vbl_nmi_timing/1.frame_basics.nes \
  vbl_nmi_timing/4.vbl_clear_timing.nes \
  ppu_vbl_nmi/rom_singles/01-vbl_basics.nes \
  ppu_vbl_nmi/rom_singles/03-vbl_clear_time.nes \
  ppu_vbl_nmi/rom_singles/04-nmi_control.nes \
  ppu_vbl_nmi/rom_singles/09-even_odd_frames.nes \
  other/SimpleParallaxDemo.nes \
  full_palette/full_palette.nes
do
  curl -fsSL -o "ppu/$(basename "$path")" "$BASE/$path"
  printf '.'
done
echo " done"
