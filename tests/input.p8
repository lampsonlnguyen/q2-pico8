pico-8 cartridge // http://www.pico-8.com
version 41
__lua__
-- original test cartridge, released under CC0-1.0
-- input-response cart for the host Q2 simulation: the circle moves
-- with btn() (0=left 1=right 2=up 3=down), O (4) changes colour and
-- X (5) resets it to the centre. Used to prove the Q2 wheel/buttons/
-- touch path actually reaches the cart, not just that frames render.
px=64
py=64
col=8
function _init()
  px=64
  py=64
  col=8
end
function _update()
  if btn(0) then px=px-2 end
  if btn(1) then px=px+2 end
  if btn(2) then py=py-2 end
  if btn(3) then py=py+2 end
  if btnp(4) then col=col%15+1 end
  if btnp(5) then px=64 py=64 end
  px=mid(0,px,127)
  py=mid(0,py,127)
end
function _draw()
  cls(1)
  circfill(px,py,5,col)
  print("q2 pico8 input test",15,15,7)
end
