-- يتطلب درايفر ina219. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local p = ui.panel("INA219")
p:page("ui/power_meter.html")
local dev
local function ensure()
  if dev then return dev end
  local d, err = drv.open("ina219", {shunt_mohm = 100})
  if not d then p:emit("err", tostring(err)) return nil end
  dev = d
  return dev
end
local mwh, last = 0, nil
p:on("reset", function() mwh = 0 end)
timer.every(1000, function()
  local s = ensure()
  if not s then return end
  local v, i, w = s:read()
  if not v then p:emit("err", tostring(i)) return end
  p:emit("err", "")
  mwh = mwh + w / 3600
  p:emit("d", {v / 1000, i, w, mwh})
end)
function on_stop() if dev then dev:close() end end
