-- يتطلب درايفر pcf8574. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local p = ui.panel("PCF8574")
p:page("ui/io_expander.html")
local dev
local function ensure()
  if dev then return dev end
  local d, err = drv.open("pcf8574", {addr = 0x20})
  if not d then p:emit("err", tostring(err)) return nil end
  dev = d
  return dev
end
local function push_state()
  local s = ensure()
  if not s then return end
  local v = s:read()
  if v then p:emit("st", v) p:emit("err", "") end
end
p:on("tog", function(n)
  local s = ensure()
  if s and type(n) == "number" and n >= 0 and n <= 7 then s:toggle(math.floor(n)) push_state() end
end)
timer.every(1000, push_state)
function on_stop() if dev then dev:close() end end
