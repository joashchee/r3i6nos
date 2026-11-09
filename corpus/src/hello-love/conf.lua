-- r3i6nos test corpus: a 640 x 480 window, like every app on the R36S.
function love.conf(t)
  t.identity = "r3i6nos-test-corpus"
  t.window.title = "r3i6nos test corpus"
  t.window.width, t.window.height = 640, 480
end
