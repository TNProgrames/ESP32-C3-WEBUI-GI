-- يتطلب درايفر bh1750. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local p = ui.panel("BH1750")
p:page("ui/light_meter.html")
local dev
local function ensure()
  if dev then return dev end
  local d, err = drv.open("bh1750", {})
  if not d then p:emit("err", tostring(err)) return nil end
  dev = d
  return dev
end
timer.every(500, function()
  local s = ensure()
  if not s then return end
  local lx = s:read()
  if not lx then p:emit("err", "read") return end
  p:emit("lux", lx / 10)
end)
function on_stop() if dev then dev:close() end end
