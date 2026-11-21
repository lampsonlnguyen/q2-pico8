pico-8 cartridge // http://www.pico-8.com
version 41
__lua__
-- original test cartridge, released under CC0-1.0
-- plays sfx 0 (a sustained sine note) from _init so the native smoke
-- test can verify the FAKE-08 synth produces non-silent audio through
-- the Q2 host audio path, not just that frames render.
ticks=0
function _init()
  sfx(0)
end
function _update()
  ticks=ticks+1
end
function _draw()
  cls(1)
  circfill(64,64,5,8)
  print("q2 pico8 audio test",15,15,7)
end
__sfx__
001000001807018070180701807018070180701807018070180701807018070180701807018070180701807018070180701807018070180701807018070180701807018070180701807018070180701807018070
