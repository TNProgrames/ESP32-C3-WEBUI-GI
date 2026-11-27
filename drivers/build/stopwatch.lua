local p = ui.panel("Stopwatch")
local running, t0, acc, laps = false, 0, 0, {}
local function fmt(ms)
  local s = ms // 1000
  return string.format("%02d:%02d.%d", s // 60, s % 60, (ms % 1000) // 100)
end
local function now() return running and acc + sys.millis() - t0 or acc end
p:label("t", "00:00.0")
p:label("lap", "-")
p:button("go", "Start / Stop", function()
  if running then acc = acc + (sys.millis() - t0) else t0 = sys.millis() end
  running = not running
end)
p:button("lp", "Lap", function()
  if not running then return end
  table.insert(laps, 1, fmt(now()))
  if #laps > 4 then table.remove(laps) end
  p:set("lap", table.concat(laps, "  "))
end)
p:button("rst", "Reset", function() running = false acc = 0 laps = {} p:set("t", fmt(0)) p:set("lap", "-") end)
timer.every(100, function() if running then p:set("t", fmt(now())) end end)
function on_stop() end
