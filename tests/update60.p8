pico-8 cartridge // http://www.pico-8.com
version 41
__lua__
-- original test cartridge, released under CC0-1.0
-- 60 FPS variant of demo.p8: _update60 runs every GameLoop tick, so a
-- new frame is drawn every tick (~16.7 ms). Plays sfx 0 like audio.p8
-- so the audio path is exercised at 60 FPS too.
x=64
ticks=0
function _init()
  sfx(0)
end
function _update60()
  ticks=ticks+1
  x=(x+2)%128
end
function _draw()
  cls(1)
  circfill(x,64,5,8)
  print("q2 pico8 60fps test",15,15,7)
end
__sfx__
001000001807018070180701807018070180701807018070180701807018070180701807018070180701807018070180701807018070180701807018070180701807018070180701807018070180701807018070
