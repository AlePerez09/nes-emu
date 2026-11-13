#!/usr/bin/env sh
# Downloads freely distributed test ROMs into this folder (gitignored, so the repo stays ROM-free).
#   nestest.nes + nestest.log   CPU trace test
#   ppu/*.nes                   PPU, sprite and timing tests (results shown on screen) and demos
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
  sprite_hit_tests_2005.10.05/01.basics.nes \
  sprite_hit_tests_2005.10.05/02.alignment.nes \
  sprite_hit_tests_2005.10.05/03.corners.nes \
  sprite_hit_tests_2005.10.05/04.flip.nes \
  sprite_hit_tests_2005.10.05/05.left_clip.nes \
  sprite_hit_tests_2005.10.05/06.right_edge.nes \
  sprite_hit_tests_2005.10.05/07.screen_bottom.nes \
  sprite_hit_tests_2005.10.05/08.double_height.nes \
  sprite_hit_tests_2005.10.05/09.timing_basics.nes \
  sprite_hit_tests_2005.10.05/10.timing_order.nes \
  sprite_hit_tests_2005.10.05/11.edge_timing.nes \
  sprite_overflow_tests/1.Basics.nes \
  sprite_overflow_tests/2.Details.nes \
  sprite_overflow_tests/4.Obscure.nes \
  sprite_overflow_tests/5.Emulator.nes \
  blargg_ppu_tests_2005.09.15b/sprite_ram.nes \
  blargg_ppu_tests_2005.09.15b/palette_ram.nes \
  blargg_ppu_tests_2005.09.15b/vram_access.nes \
  oam_read/oam_read.nes \
  other/SimpleParallaxDemo.nes \
  other/manhole.nes \
  other/snow.nes \
  full_palette/full_palette.nes
do
  curl -fsSL -o "ppu/$(basename "$path")" "$BASE/$path"
  printf '.'
done
echo " done"
