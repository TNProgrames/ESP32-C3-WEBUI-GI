-- OLED SSD1306 (I2C): نص وإطار وزمن التشغيل.
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 400000)
local d, err = drv.open("ssd1306", {})
if not d then print("OLED:", err) return end
timer.every(1000, function()
  d:clear()
  d:rect(0, 0, 128, 64, true, false)
  d:text(8, 8, "ESP32-C3")
  d:text(8, 24, "up " .. sys.uptime() .. "s")
  d:show()
end)
function on_stop() d:clear() d:show() d:close() end
