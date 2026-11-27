-- يتطلب درايفر mpu6050. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local p = ui.panel("MPU6050")
p:page("ui/tilt_level.html")
local dev
local function ensure()
  if dev then return dev end
  local d, err = drv.open("mpu6050", {})
  if not d then p:emit("err", tostring(err)) return nil end
  dev = d
  return dev
end
timer.every(100, function()
  local s = ensure()
  if not s then return end
  local ax, ay, az = s:read()
  if not ax then p:emit("err", tostring(ay)) return end
  p:emit("a", {ax, ay, az})
end)
function on_stop() if dev then dev:close() end end
