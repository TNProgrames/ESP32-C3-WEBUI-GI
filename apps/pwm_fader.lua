local PIN, freq, duty = 5, 1000, 0
local p = ui.panel("PWM")
p:label("st", "GPIO5: 0% @1000Hz")
local function apply()
  pwm.set(PIN, freq, duty)
  p:set("st", string.format("GPIO%d: %d%% @%dHz", PIN, duty, freq))
end
p:slider("d", "Duty %", 0, 100, function(v) duty = math.floor(v) apply() end, 0)
p:slider("f", "Freq Hz (100-20000)", 100, 20000, function(v) freq = math.floor(v) apply() end, 1000)
function on_stop() pwm.set(PIN, 1000, 0) end
