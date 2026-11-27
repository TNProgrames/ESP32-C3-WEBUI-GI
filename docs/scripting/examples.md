# أمثلة

كل أمثلة القسم الأول تستعمل **فقط ما هو منفَّذ حاليًا** ويمكن لصقها في تبويب **🧪 مختبر Lua**. الأرجل المستعملة (4 و5 و6 و7 و8 و10 وغيرها) غيّرها بحسب لوحتك. القسم الثاني يحتاج وحدات لم تُنفَّذ بعد.

> الأمثلة العملية الجاهزة في قائمة "أمثلة..." داخل المختبر: Hello وBlink وTimer وADC وPWM وأدوات جاهزة وعداد حرارة وصفحة الرسم البياني.

---

# الجزء 1: يعمل الآن

## أساسيات

### 1) Hello وحالة الذاكرة
```lua
print("hello", 1 + 2, 10 / 4)
print("free heap", sys.heap(), "uptime ms", sys.millis())
```

### 2) نصائح Lua: format وجداول وترتيب
```lua
local t = { 5, 2, 9, 1 }
table.sort(t)
print(table.concat(t, ","))                       --> 1,2,5,9
print(string.format("%5.2f | %04d | %s", 3.14159, 42, "ok"))
print(math.random(1, 6), math.floor(7.8), math.max(3, 9, 4))
```

### 3) التقاط الأخطاء بـ pcall
```lua
local ok, err = pcall(function() gpio.write(15, 1) end)   -- رجل ممنوعة
print(ok, err)   --> false   ... pin 15 not allowed
```

## GPIO

### 4) ليد يومض
```lua
gpio.mode(8, "out")
local on = false
timer.every(500, function()
  on = not on
  gpio.write(8, on)
end)
```

### 5) ليدان بإيقاعين مختلفين
```lua
gpio.mode(8, "out"); gpio.mode(10, "out")
local a, b = false, false
timer.every(300,  function() a = not a; gpio.write(8,  a) end)
timer.every(1000, function() b = not b; gpio.write(10, b) end)
```

### 6) زر يبدّل ليد (قراءة دورية مع كشف الحافة)
```lua
gpio.mode(4, "pullup")      -- الزر بين الرجل 4 والأرضي
gpio.mode(8, "out")
local last, on = 1, false
timer.every(20, function()
  local v = gpio.read(4)
  if v == 0 and last == 1 then        -- لحظة الضغط فقط
    on = not on
    gpio.write(8, on)
    print("toggle", on)
  end
  last = v
end)
```

### 7) إشارة مرور بتسلسل مؤقتات
```lua
local pins = { 5, 6, 7 }                -- أحمر، أصفر، أخضر
for _, p in ipairs(pins) do gpio.mode(p, "out") end
local function show(i) for k, p in ipairs(pins) do gpio.write(p, k == i) end end

local seq = { {1, 3000}, {3, 3000}, {2, 1000} }   -- {الضوء, المدة ms}
local i = 0
local function step()
  i = i % #seq + 1
  show(seq[i][1])
  timer.after(seq[i][2], step)
end
step()
```

### 8) نمط sleep داخل coroutine (تجريبي)
```lua
local function sleep(ms)
  local co = coroutine.running()
  timer.after(ms, function() coroutine.resume(co) end)
  coroutine.yield()
end
gpio.mode(8, "out")
coroutine.wrap(function()
  while true do
    gpio.write(8, 1); sleep(100)
    gpio.write(8, 0); sleep(900)
  end
end)()
```

## ADC و PWM

### 9) فولتميتر بمتوسط 8 قراءات (GPIO0)
```lua
local function avg(pin, n)
  local s = 0
  for i = 1, n do s = s + adc.mv(pin) end
  return s / n
end
timer.every(1000, function()
  print(string.format("A0 = %.0f mV", avg(0, 8)))
end)
```

### 10) مقسّم جهد (يقرأ حتى 6.6V بمقاومتين متساويتين)
```lua
timer.every(1000, function()
  local v = adc.mv(1) * 2 / 1000
  print(string.format("battery %.2f V", v))
end)
```

### 11) إضاءة "تنفّس"
```lua
local d, dir = 0, 5
timer.every(30, function()
  d = d + dir
  if d >= 100 or d <= 0 then dir = -dir end
  pwm.set(8, 1000, d)                 -- pin, freq, duty%
end)
```

### 12) تحريك سيرفو 0–180° (إشارة 50Hz)
```lua
local angle, dir = 0, 3
timer.every(40, function()
  angle = angle + dir
  if angle >= 180 or angle <= 0 then dir = -dir end
  local duty = 2.5 + angle / 180 * 10      -- نبضة 0.5–2.5ms من 20ms
  pwm.set(5, 50, duty)
end)
```

### 13) منظّم حرارة: مروحة PWM حسب حساس LM35 (GPIO0)
```lua
timer.every(1000, function()
  local t = adc.mv(0) / 10                    -- 10mV لكل درجة
  local duty = math.max(0, math.min(100, (t - 25) * 10))
  pwm.set(5, 25000, duty)
  print(string.format("%.1f C -> fan %d%%", t, duty))
end)
```

## اللوحات (طبقة الأدوات الجاهزة)

### 14) مفتاح وشريط وزر
```lua
local p = ui.panel("Control")
gpio.mode(8, "out")
local count = 0

p:label("st", "LED: OFF")
p:switch("led", "LED (GPIO8)", function(on)
  gpio.write(8, on)
  p:set("st", on and "LED: ON" or "LED: OFF")
end)
p:slider("br", "Brightness (GPIO10)", 0, 100, function(v) pwm.set(10, 1000, v) end, 0)
p:button("inc", "Count +1", function()
  count = count + 1
  p:set("cnt", "pressed " .. count .. " times")
end)
p:label("cnt", "pressed 0 times")
```

### 15) ساعة إيقاف
```lua
local p = ui.panel("Stopwatch")
local running, t0, acc = false, 0, 0
local function fmt(ms)
  local s = ms // 1000
  return string.format("%02d:%02d.%d", s // 60, s % 60, (ms % 1000) // 100)
end
p:label("t", "00:00.0")
p:button("go", "Start / Stop", function()
  if running then acc = acc + (sys.millis() - t0) else t0 = sys.millis() end
  running = not running
end)
p:button("rst", "Reset", function() running = false; acc = 0; p:set("t", fmt(0)) end)
timer.every(100, function()
  if running then p:set("t", fmt(acc + sys.millis() - t0)) end
end)
```

### 16) لوحة قياس: حرارة + شريط + تنبيه (GPIO0)
```lua
local p = ui.panel("Thermometer")
local limit = 30
p:label("t", "-- C")
p:progress("bar", "Temperature (0-50C)", 0)
p:slider("lim", "Alarm limit", 20, 45, function(v) limit = v end, limit)
p:label("alarm", "OK")
gpio.mode(8, "out")

timer.every(1000, function()
  local t = adc.mv(0) / 10
  p:set("t", string.format("%.1f C", t))
  p:set("bar", t * 2)
  local hot = t > limit
  p:set("alarm", hot and "ALARM" or "OK")
  gpio.write(8, hot)
end)
```

### 17) تحكم مروحة: آلي/يدوي
```lua
local p = ui.panel("Fan")
local auto, manual = true, 0
p:label("t", "-- C")
p:switch("auto", "Auto (by temperature)", function(v) auto = v end, true)
p:slider("man", "Manual %", 0, 100, function(v) manual = v end, 0)
p:progress("duty", "Fan %", 0)

timer.every(1000, function()
  local t = adc.mv(0) / 10
  local d = auto and math.max(0, math.min(100, (t - 25) * 10)) or manual
  pwm.set(5, 25000, d)
  p:set("t", string.format("%.1f C", t))
  p:set("duty", math.floor(d))
end)
```

### 18) نرد وإدخال نص
```lua
local p = ui.panel("Toys")
p:label("r", "press roll")
p:button("roll", "Roll", function() p:set("r", "dice: " .. math.random(1, 6)) end)
p:input("msg", "Say something", function(s) print("you said:", s) end, "hi")
```

### 22) زر بحدث تغيّر (on_change) مع تجاهل الارتداد
```lua
gpio.mode(4, "in_pullup")
gpio.mode(8, "out")
gpio.on_change(4, function(v)
  if v == 0 then                 -- 0 = مضغوط
    gpio.toggle(8)
    print("led =", gpio.read(8))
  end
end, 30)                         -- 30ms تجاهل ارتداد (الافتراضي 20)
```

### 23) ضغطة قصيرة وضغطة طويلة
```lua
gpio.mode(4, "in_pullup")
local t0
gpio.on_change(4, function(v)
  if v == 0 then t0 = sys.millis()
  else
    local held = sys.millis() - t0
    print(held > 800 and "long press" or "short press", held, "ms")
  end
end)
```

### 24) إيقاف آمن بـ on_stop
```lua
gpio.mode(8, "out")
pwm.set(10, 1000, 50)
timer.every(500, function() gpio.toggle(8) end)

function on_stop()               -- تُستدعى عند الإيقاف أو استبدال السكريبت
  gpio.write(8, 0)
  pwm.stop(10)
  print("stopped safely")
end
```

### 25) تغيير نسبة PWM فقط وإيقافه لاحقًا
```lua
pwm.set(10, 1000, 0)             -- pin, freq, duty%
local d, dir = 0, 10
timer.every(100, function()
  d = d + dir
  if d >= 100 or d <= 0 then dir = -dir end
  pwm.duty(10, d)
end)
timer.after(10000, function() pwm.stop(10) end)
```

### 26) معلومات النظام ومدى قياس ADC
```lua
print("firmware", sys.version())
adc.atten(0, "11db")             -- 0db أدق لكن مداه أضيق (حتى ~0.95V)
timer.every(2000, function()
  log(string.format("A0 = %d mV  up %ds  heap %d", adc.read_mv(0), sys.uptime(), sys.heap()))
end)
```

### 27) عدّاد إقلاعات محفوظ
```lua
local boots = storage.get("boots", 0) + 1
storage.set("boots", boots)
print("this script started", boots, "times")
```
شغّله، ثم أعد تشغيل السكريبت أو الجهاز: يزيد العدّاد (وفعّل «تشغيل تلقائي» ليعمل بعد الإقلاع).

### 28) إعداد يتذكّره الجهاز: شريط حد التنبيه
```lua
local p = ui.panel("Saved settings")
local limit = storage.get("limit", 30)
p:label("info", "limit = " .. limit)
p:slider("lim", "Alarm limit", 20, 45, function(v)
  limit = v
  storage.set("limit", v)            -- تُدمج الكتابة وتُحفظ بعد 3 ثوانٍ
  p:set("info", "limit = " .. v)
end, limit)
```

### 29) حفظ سجل صغير (آخر 5 قراءات) في storage
```lua
local function push(v)
  for i = 1, 4 do storage.set("r" .. i, storage.get("r" .. (i + 1), 0)) end
  storage.set("r5", v)
end
timer.every(5000, function()
  push(adc.read_mv(0))
  print(storage.get("r1"), storage.get("r2"), storage.get("r3"), storage.get("r4"), storage.get("r5"))
end)
```

### 30) سكريبتان يتبادلان البيانات بـ bus
**المرسل** (`sender`):
```lua
local n = 0
timer.every(1000, function()
  n = n + 1
  bus.publish("temp", 25 + n % 5)               -- رقم
  bus.publish("status", { ok = true, n = n })   -- جدول بسيط
end)
```
**المستقبل** (`receiver`):
```lua
local p = ui.panel("Receiver")
p:label("t", "--")
p:label("s", "--")
bus.subscribe("temp", function(v) p:set("t", "temp: " .. v) end)
bus.subscribe("status", function(st)
  p:set("s", "n=" .. st.n .. " ok=" .. tostring(st.ok))
end)
```
شغّل كلًّا بالمختبر باسمه (اسمان مختلفان)، وانظر لوحة Receiver في تبويب اللوحات.

### 31) فصل القراءة عن العرض: سكريبت للحساس وآخر للوحة
```lua
-- sensor: يقرأ ويبثّ فقط
timer.every(1000, function() bus.publish("temp", adc.read_mv(0) / 10) end)
```
```lua
-- display: يستقبل ويعرض ويقرّر التنبيه
local p = ui.panel("Display")
p:label("v", "--")
gpio.mode(8, "out")
bus.subscribe("temp", function(t)
  p:set("v", string.format("%.1f C", t))
  gpio.write(8, t > 30)
end)
```
يمكن إيقاف سكريبت العرض وتعديله دون لمس الحساس، وتجرّب المستقبل من الطرفية: `pub temp 35`.

### 32) البحث عن أجهزة I2C
```lua
i2c.begin(6, 7, 100000)              -- SDA=6, SCL=7 (غيّرهما حسب لوحتك)
local found = i2c.scan()
if #found == 0 then print("no I2C devices (check wiring and pull-ups)") end
for _, a in ipairs(found) do print(string.format("found 0x%02X", a)) end
```

### 33) ماسح I2C بزر في لوحة
```lua
local p = ui.panel("I2C scanner")
i2c.begin(6, 7)
p:label("r", "press Scan")
p:button("scan", "Scan", function()
  local t = {}
  for _, a in ipairs(i2c.scan()) do t[#t + 1] = string.format("0x%02X", a) end
  p:set("r", #t > 0 and table.concat(t, " ") or "none found")
end)
```

### 34) قراءة سجلات حساس وكتابتها
```lua
i2c.begin(6, 7)
local id, err = i2c.read_reg(0x76, 0xD0)         -- رقم واحد (BME280 يعيد 0x60)
if id then print(string.format("chip id = 0x%02X", id)) else print("read failed:", err) end
print("write ok:", i2c.write(0x76, { 0xF4, 0x27 }))
local two = i2c.write_read(0x76, { 0xFA }, 2)    -- أرسل ثم اقرأ بايتين
if two then print(two[1], two[2]) end
```

### 35) مكتبة بسيطة وطريقة استعمالها (require)
**احفظ كمكتبة باسم `util`** (فعّل «حفظ كمكتبة» في المختبر):
```lua
local M = {}
function M.clamp(x, lo, hi) return math.max(lo, math.min(hi, x)) end
function M.map(x, a, b, c, d) return c + (x - a) * (d - c) / (b - a) end
function M.round(x, n) local m = 10 ^ (n or 0) return math.floor(x * m + 0.5) / m end
return M
```
**ثم في أي سكريبت:**
```lua
local util = require("util")
print(util.clamp(150, 0, 100), util.map(512, 0, 1023, 0, 3.3), util.round(3.14159, 2))
```

### 36) درايفر I2C صغير بنفسك: حساس بسجلين (هيكل)
**مكتبة `mydev`:**
```lua
local M = {}
M.__index = M
function M.new(addr)
  local self = setmetatable({ addr = addr }, M)
  assert(i2c.write_reg(addr, 0x00, 0x01), "device not responding")   -- تهيئة
  return self
end
function M:read()
  local b = i2c.read_reg(self.addr, 0x10, 2)
  if not b then return nil end
  return (b[1] << 8) | b[2]
end
return M
```
**السكريبت:**
```lua
i2c.begin(6, 7)
local dev = require("mydev").new(0x40)
timer.every(1000, function() print(dev:read()) end)
```

## صفحات مخصصة (طبقة iframe)

الصفحة ملف HTML على الجهاز (`/ui/<name>.html`). في المختبر: فعّل "صفحة HTML مخصصة" واكتب المسار والمحتوى، فتُحفظ قبل التشغيل. داخل الصفحة الكائن `esp` (انظر [ui.md](ui.md)).

### 19) متحكم RGB: منتقي لون يقود ثلاث قنوات PWM
**Lua** (`rgb`):
```lua
local p = ui.panel("RGB")
p:page("ui/rgb.html")
local pins = { r = 5, g = 6, b = 7 }
for k, pin in pairs(pins) do
  p:on(k, function(v) pwm.set(pin, 1000, v) end)   -- v من 0 إلى 100
end
```
**الصفحة** `/ui/rgb.html`:
```html
<!doctype html><meta charset="utf-8">
<style>
  body{margin:0;padding:12px;text-align:center;font-family:system-ui;color:#999}
  input{width:120px;height:120px;border:0;background:none}
  [data-theme=light] body{color:#333}
</style>
<input type="color" id="c" value="#ff8800">
<div id="v">pick a color</div>
<script>
  c.onchange = function () {
    var h = c.value, k = ["r","g","b"];
    for (var i = 0; i < 3; i++)
      esp.send(k[i], Math.round(parseInt(h.substr(1 + 2*i, 2), 16) / 2.55));
    v.textContent = h;
  };
</script>
```

### 20) شاشة زمن التشغيل الكبيرة
**Lua**:
```lua
local p = ui.panel("Clock")
p:page("ui/clock.html")
timer.every(1000, function() p:emit("up", sys.millis() // 1000) end)
```
**الصفحة** `/ui/clock.html` (تتبع السمة واللغة):
```html
<!doctype html><meta charset="utf-8">
<style>
  body{margin:0;padding:10px;text-align:center;font-family:system-ui;color:#eee}
  [data-theme=light] body{color:#111}
  #t{font-size:44px;font-weight:bold;font-variant-numeric:tabular-nums}
</style>
<div id="t">--:--:--</div>
<div id="l" style="color:#888"></div>
<script>
  function pad(n){return (n<10?"0":"")+n}
  esp.on("up", function (s) {
    t.textContent = pad(Math.floor(s/3600)) + ":" + pad(Math.floor(s/60)%60) + ":" + pad(s%60);
  });
  function lbl(){ l.textContent = esp.lang === "ar" ? "زمن التشغيل" : "Uptime"; }
  esp.onchange = lbl; lbl();
</script>
```

### 21) صفحة تجمع الاتجاهين: لوحة أزرار تتحكم وتعرض الحالة
**Lua**:
```lua
local p = ui.panel("Pad")
p:page("ui/pad.html")
gpio.mode(8, "out")
local on = false
p:on("toggle", function()
  on = not on
  gpio.write(8, on)
  p:emit("state", on)          -- تأكيد من الجهاز: القيمة المعروضة هي الحقيقية
end)
p:emit("state", on)
```
**الصفحة** `/ui/pad.html`:
```html
<!doctype html><meta charset="utf-8">
<style>
  body{margin:0;padding:10px;text-align:center;font-family:system-ui}
  button{font-size:20px;padding:14px 28px;border:0;border-radius:14px;color:#fff;background:#6366f1}
  .on{background:#10b981}
</style>
<button id="b">LED</button>
<script>
  b.onclick = function () { esp.send("toggle", true); };
  esp.on("state", function (s) { b.className = s ? "on" : ""; b.textContent = "LED " + (s ? "ON" : "OFF"); });
</script>
```

## أفكار لتجرّبها مع تعديل بسيط
- **أضواء متتابعة (Knight Rider):** مصفوفة أرجل ومؤشر يتحرك ذهابًا وإيابًا بـ `timer.every(80, ...)`.
- **مؤقت مطبخ:** لوحة فيها `input` للدقائق وزر بدء، و`timer.after` يشغّل ليدًا عند الانتهاء.
- **مقياس إضاءة:** LDR على GPIO1 مع مقسّم، وشريط تقدم في لوحة، ويُشغّل ليلًا ليدًا بـ PWM.
- **ملاحظة:** مع سكريبتات متعددة، اجعل لكل واحد لوحته، فلا تتشارك الأسماء بين السكريبتات.

---

# الجزء 2: مخطَّط (يحتاج وحدات لم تُنفَّذ بعد)

> هذه تصاميم للاتفاق على الشكل قبل التنفيذ، وقد تتغيّر الأسماء.

## شاشة حروف LCD1602 عبر I2C (درايفر موقّع `lcd1602`)
```lua
i2c.begin(8, 9, 100000)
local lcd, err = drv.open("lcd1602", {addr = 0x27})
if not lcd then print("LCD:", err) return end
lcd:clear()
timer.every(1000, function()
  lcd:text(0, 0, "Hello ESP32-C3")
  lcd:text(1, 0, "up " .. sys.uptime() .. "s   ")
end)
function on_stop() lcd:clear() end
```

## شاشة OLED وحساس BME280 (درايفران موقّعان، المرحلة 5)
```lua
i2c.begin(8, 9, 400000)
local oled = drv.open("ssd1306", {addr = 0x3C, w = 128, h = 64})
local bme  = drv.open("bme280", {addr = 0x76})
local p = ui.panel("Weather")
p:label("t", "--")
timer.every(2000, function()
  local t, pr, h = bme:read()          -- أعداد صحيحة: t بمئات الدرجة، pr بالباسكال، h بالألف من %
  if not t then return end
  p:set("t", string.format("%.1f C  %d hPa", t / 100, pr // 100))
  oled:clear()
  oled:text(0, 0, string.format("%.1f C", t / 100), 3)
  oled:show()                          -- الإطار داخل الدرايفر نفسه
end)
```

## شريط ليد WS2812 (يُنفَّذ في الفيرموير عبر RMT)
```lua
--@pins 5
--@needs rmt
local strip = ws2812.new(5, 8)           -- 8 ليدات
local hue = 0
timer.every(30, function()
  for i = 1, 8 do strip:set(i, ws2812.hsv((hue + i * 30) % 360, 1, 0.3)) end
  strip:show()
  hue = hue + 5
end)
```

## حساس DHT22 (توقيت دقيق يُنفَّذ في الفيرموير)
```lua
--@pins 4
local dht = require("dht").new(4, "dht22")
timer.every(3000, function()
  local t, h = dht:read()
  if t then log(string.format("%.1f C  %.0f %%", t, h)) end
end)
```

## مستقبل أشعة تحت الحمراء (IR)
```lua
--@pins 3
--@needs rmt
ir.receive(3, function(proto, addr, cmd)
  log(proto, addr, cmd)
  if cmd == 0x45 then gpio.write(8, 1) end
end)
```

## قراءة GPS على UART
```lua
--@pins 20,21
--@needs uart
uart.begin(1, 9600, 20, 21)
uart.on_line(1, function(line)
  if line:sub(1, 6) == "$GPGGA" then log(line) end
end)
```
