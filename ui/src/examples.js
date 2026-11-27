// أمثلة المختبر. كل مثال: سكريبت Lua، وصفحة HTML اختيارية تُحفظ على الجهاز قبل التشغيل.
const GAUGE_PAGE = `<!doctype html>
<meta charset="utf-8">
<style>
  :root { --fg:#eee; --mut:#999; --track:#333; }
  [data-theme="light"] { --fg:#111; --mut:#666; --track:#ddd; }
  body { margin:0; padding:8px; font-family:system-ui,sans-serif; text-align:center; color:var(--fg); }
  svg { width:100%; max-width:260px; }
  #n { font-size:34px; font-weight:bold; margin-top:-46px; }
  .ok { color:#10b981 } .hot { color:#ef4444 }
  input { width:80%; }
</style>
<svg viewBox="0 0 200 110">
  <path d="M20 100 A80 80 0 0 1 180 100" fill="none" stroke="var(--track)" stroke-width="16" stroke-linecap="round"/>
  <path id="arc" d="M20 100 A80 80 0 0 1 180 100" fill="none" stroke="#10b981" stroke-width="16" stroke-linecap="round" pathLength="100" stroke-dasharray="0 100"/>
</svg>
<div id="n">--</div>
<div style="color:var(--mut);font-size:13px">setpoint: <b id="sp">30</b> °C</div>
<input id="s" type="range" min="15" max="40" value="30">
<script>
  var sp = 30;
  esp.on("temp", function (v) {
    var pct = Math.max(0, Math.min(100, (v - 15) / 25 * 100));
    arc.setAttribute("stroke-dasharray", pct + " 100");
    n.textContent = v.toFixed(1) + "°";
    n.className = v > sp ? "hot" : "ok";
    arc.setAttribute("stroke", v > sp ? "#ef4444" : "#10b981");
  });
  esp.on("setpoint", function (v) { sp = v; s.value = v; document.getElementById("sp").textContent = v; });
  s.oninput = function () { document.getElementById("sp").textContent = s.value; };
  s.onchange = function () { sp = +s.value; esp.send("setpoint", sp); };
<\/script>`;

const CHART_PAGE = `<!doctype html>
<meta charset="utf-8">
<style>
  :root { --fg:#eee; --grid:#333; }
  [data-theme="light"] { --fg:#111; --grid:#ddd; }
  body { margin:0; padding:6px; font-family:system-ui,sans-serif; color:var(--fg); }
  canvas { width:100%; height:150px; display:block; }
  button { padding:6px 12px; border-radius:8px; border:0; background:#6366f1; color:#fff; font-weight:bold; }
</style>
<canvas id="c" width="400" height="150"></canvas>
<div style="display:flex;justify-content:space-between;align-items:center;margin-top:6px">
  <span>value: <b id="v">--</b></span><button id="b">pause</button>
</div>
<script>
  var data = [], paused = false, ctx = c.getContext("2d");
  function draw() {
    var w = c.width, h = c.height, css = getComputedStyle(document.documentElement);
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = esp.theme === "light" ? "#ddd" : "#333";
    for (var i = 1; i < 4; i++) { ctx.beginPath(); ctx.moveTo(0, h * i / 4); ctx.lineTo(w, h * i / 4); ctx.stroke(); }
    ctx.strokeStyle = "#6366f1"; ctx.lineWidth = 2; ctx.beginPath();
    data.forEach(function (y, i) { var x = i * w / 59, yy = h - y / 100 * h; i ? ctx.lineTo(x, yy) : ctx.moveTo(x, yy); });
    ctx.stroke();
  }
  esp.on("sample", function (v) {
    v_.textContent = v.toFixed(0);
    if (paused) return;
    data.push(v); if (data.length > 60) data.shift(); draw();
  });
  var v_ = document.getElementById("v");
  b.onclick = function () { paused = !paused; b.textContent = paused ? "resume" : "pause"; esp.send("paused", paused); };
  esp.onchange = draw;
<\/script>`;

export const examples = {
  'Hello': { code: `print("hello from Lua", 1 + 2, 10 / 4)\nprint("heap", sys.heap())` },
  'Blink (GPIO8)': { code: `-- يومّض LED على GPIO8 كل 500ms\ngpio.mode(8, "out")\nlocal on = false\ntimer.every(500, function()\n  on = not on\n  gpio.write(8, on)\n  print("led", on)\nend)` },
  'Timer': { code: `local n = 0\nlocal id\nid = timer.every(1000, function()\n  n = n + 1\n  print("tick", n)\n  if n == 5 then timer.cancel(id) print("done") end\nend)` },
  'ADC (GPIO0)': { code: `timer.every(1000, function()\n  print("adc", adc.read(0), adc.mv(0) .. " mV")\nend)` },
  'PWM (GPIO10)': { code: `local d = 0\ntimer.every(50, function()\n  d = (d + 5) % 105\n  pwm.set(10, 1000, d)\nend)` },
  'Button: on_change': { code:
`-- زر على GPIO4 (بين الرجل والأرضي) يبدّل ليد GPIO8
gpio.mode(4, "in_pullup")
gpio.mode(8, "out")
gpio.on_change(4, function(v)
  if v == 0 then          -- 0 = مضغوط
    gpio.toggle(8)
    print("pressed, led =", gpio.read(8))
  end
end, 30)                  -- تجاهل الارتداد 30ms` },
  'Safe stop: on_stop': { code:
`-- on_stop تُستدعى عند الإيقاف أو الاستبدال لإعادة الأرجل لحالة آمنة
gpio.mode(8, "out")
pwm.set(10, 1000, 50)
timer.every(500, function() gpio.toggle(8) end)

function on_stop()
  gpio.write(8, 0)
  pwm.stop(10)
  print("stopped safely")
end` },
  'PWM: duty & stop': { code:
`pwm.set(10, 1000, 0)           -- pin, freq, duty%
local d, dir = 0, 10
timer.every(100, function()
  d = d + dir
  if d >= 100 or d <= 0 then dir = -dir end
  pwm.duty(10, d)               -- يغيّر النسبة فقط
end)
timer.after(10000, function() pwm.stop(10) print("pwm stopped") end)` },
  'System info': { code:
`print("firmware", sys.version())
log("uptime", sys.uptime(), "s  heap", sys.heap())
adc.atten(0, "11db")           -- مدى القياس الأوسع
timer.every(2000, function()
  print(string.format("A0 = %d mV, up %ds", adc.read_mv(0), sys.uptime()))
end)` },
  'Storage: boot counter': { code:
`-- القيم تبقى بعد إعادة التشغيل (ملف على الجهاز)
local boots = storage.get("boots", 0) + 1
storage.set("boots", boots)
print("this script started", boots, "times")
storage.set("last", sys.uptime())` },
  'Storage: saved settings': { code:
`-- شريط إعداد يتذكّر قيمته بعد الإقلاع
local p = ui.panel("Saved settings")
local limit = storage.get("limit", 30)
p:label("info", "limit = " .. limit)
p:slider("lim", "Alarm limit", 20, 45, function(v)
  limit = v
  storage.set("limit", v)
  p:set("info", "limit = " .. v)
end, limit)` },
  'Bus: sender': { code:
`-- يُرسل قيمًا لأي سكريبت مشترك (شغّل مثال Bus: receiver أيضًا)
local n = 0
timer.every(1000, function()
  n = n + 1
  bus.publish("temp", 25 + n % 5)
  bus.publish("status", { ok = true, n = n })
end)` },
  'Bus: receiver': { code:
`local p = ui.panel("Receiver")
p:label("t", "--")
p:label("s", "--")
bus.subscribe("temp", function(v) p:set("t", "temp: " .. v) end)
bus.subscribe("status", function(st)
  p:set("s", "n=" .. st.n .. " ok=" .. tostring(st.ok))
end)` },
  'I2C: scan': { code:
`-- يبحث عن أجهزة I2C (SDA=GPIO6, SCL=GPIO7؛ غيّرهما حسب لوحتك)
i2c.begin(6, 7, 100000)
local found = i2c.scan()
if #found == 0 then print("no I2C devices (check wiring and pull-ups)") end
for _, a in ipairs(found) do
  print(string.format("found 0x%02X", a))
end` },
  'I2C: scan panel': { code:
`local p = ui.panel("I2C scanner")
i2c.begin(6, 7)
p:label("r", "press Scan")
p:button("scan", "Scan", function()
  local t = {}
  for _, a in ipairs(i2c.scan()) do t[#t + 1] = string.format("0x%02X", a) end
  p:set("r", #t > 0 and table.concat(t, " ") or "none found")
end)` },
  'I2C: read register': { code:
`-- مثال: قراءة سجل هوية (BME280 عنوان 0x76: السجل 0xD0 يعيد 0x60)
i2c.begin(6, 7)
local id, err = i2c.read_reg(0x76, 0xD0)
if id then print(string.format("chip id = 0x%02X", id)) else print("read failed:", err) end
local ok = i2c.write(0x76, { 0xF4, 0x27 })      -- كتابة بايتات
print("write ok:", ok)
local two = i2c.write_read(0x76, { 0xFA }, 2)   -- أرسل ثم اقرأ بايتين
if two then print(two[1], two[2]) end` },
  'Library: util (save as lib)': { lib: true, libName: 'util', code:
`-- مكتبة: احفظها باسم util ثم استعملها بـ require("util")
local M = {}
function M.clamp(x, lo, hi) return math.max(lo, math.min(hi, x)) end
function M.map(x, a, b, c, d) return c + (x - a) * (d - c) / (b - a) end
function M.round(x, n) local m = 10 ^ (n or 0) return math.floor(x * m + 0.5) / m end
return M` },
  'Use library: util': { code:
`local util = require("util")      -- /scripts/lib/util.lua
print(util.clamp(150, 0, 100), util.map(512, 0, 1023, 0, 3.3), util.round(3.14159, 2))` },
  'Panel: أدوات جاهزة': { code:
`-- لوحة بأدوات جاهزة: تظهر في تبويب اللوحات
local p = ui.panel("Control")
gpio.mode(8, "out")
local count = 0

p:label("st", "LED: OFF")
p:switch("led", "LED (GPIO8)", function(on)
  gpio.write(8, on)
  p:set("st", on and "LED: ON" or "LED: OFF")
end)
p:slider("br", "Brightness (GPIO10 PWM)", 0, 100, function(v)
  pwm.set(10, 1000, v)
end, 0)
p:button("inc", "Count +1", function()
  count = count + 1
  p:set("cnt", "pressed " .. count .. " times")
end)
p:label("cnt", "pressed 0 times")
p:progress("up", "Uptime (s mod 100)", 0)
p:input("msg", "Message", function(s) print("msg:", s) end, "hi")

timer.every(1000, function()
  p:set("up", (sys.millis() // 1000) % 100)
end)` },
  'Page: عداد حرارة (iframe)': { code:
`-- صفحة HTML مخصصة داخل إطار معزول
local p = ui.panel("Gauge")
p:page("ui/gauge.html")
local setpoint = 30
p:on("setpoint", function(v)
  setpoint = v
  print("setpoint =", v)
end)
local t = 0
timer.every(500, function()
  t = t + 0.2
  local temp = 25 + 8 * math.sin(t)   -- حرارة تجريبية
  p:emit("temp", temp)
end)`, page: { path: '/ui/gauge.html', html: GAUGE_PAGE } },
  'Page: رسم بياني (canvas)': { code:
`local p = ui.panel("Chart")
p:page("ui/chart.html")
local paused = false
p:on("paused", function(v) paused = v print("paused", v) end)
local x = 0
timer.every(200, function()
  x = x + 0.15
  p:emit("sample", 50 + 40 * math.sin(x) * math.cos(x / 3))
end)`, page: { path: '/ui/chart.html', html: CHART_PAGE } },
  'Driver: lcd1602 (drv.open)': { code:
`-- يتطلب درايفر lcd1602 مثبّتًا (يوضع في /drivers). SDA=GPIO8، SCL=GPIO9
i2c.begin(8, 9, 100000)
local lcd, err = drv.open("lcd1602", {addr = 0x27})
if not lcd then print("LCD:", err) return end
lcd:clear()
timer.every(1000, function()
  lcd:text(0, 0, "Hello ESP32-C3")
  lcd:text(1, 0, "up " .. sys.uptime() .. "s   ")
end)
function on_stop() lcd:clear() end` },
  'Error test (حلقة لا نهائية)': { code: `-- يجب أن يوقفها الحارس بعد 200ms\nwhile true do end` }
};
