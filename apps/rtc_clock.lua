-- يتطلب درايفر ds3231. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local p = ui.panel("DS3231")
p:page("ui/rtc_clock.html")
local dev
local function ensure()
  if dev then return dev end
  local d, err = drv.open("ds3231", {})
  if not d then p:emit("err", tostring(err)) return nil end
  dev = d
  return dev
end
-- الصفحة ترسل "YYYY-MM-DDTHH:MM:SS" لضبط الوقت
p:on("sync", function(v)
  local rtc = ensure()
  if not rtc then return end
  local y, mo, d, h, mi, s = tostring(v):match("^(%d+)-(%d+)-(%d+)T(%d+):(%d+):(%d+)$")
  if not y then return end
  local ok, e = rtc:set(tonumber(y), tonumber(mo), tonumber(d), tonumber(h), tonumber(mi), tonumber(s))
  p:emit("err", (ok == nil and e) and tostring(e) or "")
end)
local n = 0
timer.every(1000, function()
  local rtc = ensure()
  if not rtc then return end
  local y, mo, d, h, mi, s = rtc:read()
  if not y then p:emit("err", tostring(mo)) return end
  p:emit("t", string.format("%02d:%02d:%02d", h, mi, s))
  p:emit("d", string.format("%04d-%02d-%02d", y, mo, d))
  n = n + 1
  if n % 10 == 1 then local c = rtc:temp(); if c then p:emit("c", c / 100) end end
end)
function on_stop() if dev then dev:close() end end
