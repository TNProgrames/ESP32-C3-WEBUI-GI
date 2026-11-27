-- يتطلب درايفر pca9685. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local p = ui.panel("PCA9685")
p:page("ui/servo_ctrl.html")
local dev
local function ensure()
  if dev then return dev end
  local d, err = drv.open("pca9685", {freq = 50})
  if not d then p:emit("err", tostring(err)) return nil end
  dev = d
  return dev
end
p:on("servo", function(v)
  local s = ensure()
  if not s or type(v) ~= "table" then return end
  local e = select(2, s:servo(math.floor(v[1] or 0), math.floor(v[2] or 90)))
  if e then p:emit("err", tostring(e)) else p:emit("err", "") end
end)
timer.after(300, ensure)
function on_stop() if dev then for c = 0, 3 do dev:off(c) end dev:close() end end
