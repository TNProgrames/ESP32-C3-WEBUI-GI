-- محطة طقس: sht31 (حرارة/رطوبة) + bh1750 (ضوء). الأرجل: SDA=8, SCL=9
-- (الحد: درايفران مفتوحان لكل سكريبت)
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local p = ui.panel("Weather")
p:page("ui/weather_station.html")
local devs, cfg = {}, {sht31 = {}, bh1750 = {}}
local function get(name)
  if devs[name] then return devs[name] end
  local d = drv.open(name, cfg[name])
  if d then devs[name] = d end
  return d
end
timer.every(2000, function()
  local out = {}
  local s = get("sht31")
  if s then
    local t, h = s:read()
    if t then out.t, out.h = t / 100, h / 100 end
  end
  local b = get("bh1750")
  if b then
    local lux = b:read()
    if lux then out.l = lux / 10 end
  end
  p:emit("w", out)
  p:emit("err", (s and b) and "" or (s and "bh1750 ✗" or "sht31 ✗"))
end)
function on_stop()
  for _, d in pairs(devs) do d:close() end
end
