local PINS = {4, 5, 6, 7}
local p = ui.panel("GPIO")
for _, n in ipairs(PINS) do
  gpio.mode(n, "out")
  local on = storage.get("g" .. n) == "1"
  gpio.write(n, on)
  p:switch("g" .. n, "GPIO" .. n, function(v)
    gpio.write(n, v)
    storage.set("g" .. n, v and "1" or "0")
  end, on)
end
function on_stop() for _, n in ipairs(PINS) do gpio.write(n, false) end end
