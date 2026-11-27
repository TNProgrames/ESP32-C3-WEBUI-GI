-- يتطلب درايفر aht20. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local p = ui.panel("AHT20")
p:page("ui/aht_dash.html")
local dev
local function ensure()
  if dev then return dev end
  local d, err = drv.open("aht20", {})
  if not d then p:emit("err", tostring(err)) return nil end
  dev = d
  return dev
end
local iv, tick = 2, 0
p:on("iv", function(v) if type(v) == "number" and v >= 1 and v <= 60 then iv = math.floor(v) end end)
timer.every(1000, function()
  tick = tick + 1
  if tick % iv ~= 0 then return end
  local s = ensure()
  if not s then return end
  local t, h = s:read()
  if not t then p:emit("err", tostring(h)) return end
  p:emit("t", t / 100)
  p:emit("h", h / 100)
end)
function on_stop() if dev then dev:close() end end
