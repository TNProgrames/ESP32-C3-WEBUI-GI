-- يتطلب درايفر ads1115. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 400000)
local p = ui.panel("ADS1115")
p:page("ui/adc_scope.html")
local dev
local function ensure()
  if dev then return dev end
  local d, err = drv.open("ads1115", {gain = 1})
  if not d then p:emit("err", tostring(err)) return nil end
  dev = d
  return dev
end
local ch, gain = 0, 1
p:on("ch", function(v) if v == 0 or v == 1 or v == 2 or v == 3 then ch = v end end)
p:on("g", function(v)
  if type(v) == "number" and v >= 0 and v <= 5 then gain = math.floor(v); if dev then dev:gain(gain) end end
end)
timer.every(200, function()
  local adc = ensure()
  if not adc then return end
  local mv = adc:read(ch)
  if not mv then p:emit("err", "read") return end
  p:emit("v", mv)
end)
function on_stop() if dev then dev:close() end end
