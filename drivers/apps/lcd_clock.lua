-- شاشة LCD1602: تعرض زمن التشغيل. عدّل الأرجل إن لزم.
local SDA, SCL, ADDR = 8, 9, 0x27
i2c.begin(SDA, SCL, 100000)
local lcd, err = drv.open("lcd1602", {addr = ADDR})
if not lcd then print("LCD:", err) return end
lcd:clear()
timer.every(1000, function()
  lcd:text(0, 0, "ESP32-C3")
  lcd:text(1, 0, "up " .. sys.uptime() .. "s   ")
end)
function on_stop() lcd:clear() end
