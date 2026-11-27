-- يتطلب درايفر sht31. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local p = ui.panel("SHT31")
p:page("ui/sht_monitor.html")
local dev
local function ensure()
  if dev then return dev end
  local d, err = drv.open("sht31", {})
  if not d then p:emit("err", tostring(err)) return nil end
  dev = d
  return dev
end
timer.every(2000, function()
  local s = ensure()
  if not s then return end
  local t, h = s:read()
  if not t then p:emit("err", tostring(h)) return end
  p:emit("err", "")
  p:emit("d", {t / 100, h / 100})
end)
function on_stop() if dev then dev:close() end end
