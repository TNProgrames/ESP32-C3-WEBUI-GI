# uart — المنفذ التسلسلي

> **🕒 مخطَّط (غير منفَّذ بعد)** — تصميم مقترح، وقد يتغيّر قبل التنفيذ.

يستعمل UART1 (UART0 محجوز للسجل والتفليش).

## `uart.begin(rx, tx, baud)`
## `uart.write(data)`
يرسل نصًا أو جدول بايتات.
## `uart.read([n]) → string`
يقرأ ما وصل (حتى `n` بايت).
## `uart.on_data(fn)`
يستدعي `fn(data)` عند وصول بيانات.

```lua
--@pins 20,21
uart.begin(20, 21, 9600)
uart.on_data(function(d) log("وصل: " .. d) end)
uart.write("AT\r\n")
```
