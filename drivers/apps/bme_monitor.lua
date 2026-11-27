-- يتطلب درايفر bme280. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local p = ui.panel("BME280")
p:page("ui/bme_monitor.html")
local dev
local function ensure()
  if dev then return dev end
  local d, err = drv.open("bme280", {})
  if not d then p:emit("err", tostring(err)) return nil end
  dev = d
  return dev
end
timer.every(3000, function()
  local s = ensure()
  if not s then return end
  local t, pr, h = s:read()
  if not t then p:emit("err", tostring(pr)) return end
  p:emit("t", t / 100)
  p:emit("p", pr / 100)
  p:emit("h", h / 100)
end)
function on_stop() if dev then dev:close() end end
