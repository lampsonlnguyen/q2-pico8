pico-8 cartridge // http://www.pico-8.com
version 41
__lua__
-- original test cartridge, released under CC0-1.0
x=64
ticks=0
function _init()
  x=64
  ticks=0
end
function _update()
  ticks=ticks+1
  x=(x+1)%128
end
function _draw()
  cls(1)
  circfill(x,64,5,8)
  print("q2 pico8 test",15,15,7)
end
