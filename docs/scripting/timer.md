# timer — المؤقتات

> **✅ منفَّذ في v0.4**: `timer.every` و`timer.after` و`timer.cancel`. المؤقتات حتى 8 لكل سكريبت، والأدنى 10ms لـ `every`.

المؤقتات هي أساس عمل السكريبت، فلا يوجد `sleep` ولا حلقات انتظار.

## `timer.every(ms, fn) → id`
يستدعي `fn()` كل `ms` مللي ثانية (الأدنى 10).

## `timer.after(ms, fn) → id`
يستدعيها مرة واحدة بعد `ms`.

## `timer.cancel(id)`
يلغي مؤقتًا.

```lua
local n = 0
local id
id = timer.every(500, function()
  n = n + 1
  gpio.toggle(8)
  if n >= 10 then timer.cancel(id) end
end)
```

## ملاحظات
- الحد **8 مؤقتات** لكل سكريبت.
- أي استدعاء يتجاوز **200 مللي ثانية** يُقطع بخطأ (watchdog). اجعل كل استدعاء قصيرًا.
- المؤقتات تُلغى تلقائيًا عند إيقاف السكريبت.
