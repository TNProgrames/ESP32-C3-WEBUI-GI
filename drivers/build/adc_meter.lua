local p = ui.panel("ADC")
for pin = 0, 3 do
  p:label("l" .. pin, "GPIO" .. pin .. ": --")
  p:progress("b" .. pin, "", 0)
end
timer.every(500, function()
  for pin = 0, 3 do
    local mv = adc.read_mv(pin)
    p:set("l" .. pin, string.format("GPIO%d: %d mV", pin, mv))
    p:set("b" .. pin, math.min(100, mv / 33))
  end
end)
function on_stop() end
