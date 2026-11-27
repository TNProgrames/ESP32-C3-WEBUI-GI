// المفتاح = النص العربي، والقيمة = الترجمة الإنجليزية. أي نص بلا ترجمة يظهر بالعربية.
export const en = {
  // App
  'مدير': 'Admin', 'مستخدم': 'User', 'خروج': 'Logout',
  'الحالة': 'Status', 'الواي فاي': 'WiFi', 'الاتصال': 'Connection', 'كلمات المرور': 'Passwords',
  'نسخ احتياطي': 'Backup', 'الطرفية': 'Terminal', 'تحديث': 'Update',
  // Drivers
  'درايفرات': 'Drivers',
  'تم الحفظ — يُعاد تشغيل الجهاز لتطبيق الاسم المحلي...': 'Saved — the device is restarting to apply the local name...',
  'يبقى لأن تطبيقًا آخر يستعمله': 'Kept because another app uses it',
  'بحث عن تطبيق أو درايفر': 'Search apps or drivers', 'الكل': 'All', 'حساسات': 'Sensors', 'شاشات': 'Displays', 'وقت': 'Time', 'أدوات': 'Tools', 'لا نتائج': 'No results', 'تطبيق': 'apps', 'صفحة': 'page',
  'مثبّت': 'Installed', 'صفحة + canvas': 'Page + canvas', 'درايفر': 'driver', 'حذف التطبيق': 'Delete app', 'تم الحذف': 'Deleted', 'تعذر الحذف': 'Could not delete', 'الدرايفرات': 'Drivers', 'الإعدادات': 'Settings',

  'الاسم المحلي: أحرف إنجليزية صغيرة وأرقام وشرطة فقط، دون شرطة في الأول أو الآخر': 'Local name: lowercase letters, digits and hyphen only, no leading/trailing hyphen',
  'متجر التطبيقات': 'App Store', 'التطبيقات': 'Apps', 'التطبيق سكريبت Lua جاهز مع الدرايفر الذي يحتاجه؛ يُثبَّت الاثنان معًا.': 'An app is a ready Lua script plus the driver it needs; both are installed together.',
  'لا توجد تطبيقات (اضغط جلب)': 'No apps (press Fetch)', 'تم تثبيت التطبيق': 'App installed', 'تم التشغيل': 'Started', 'تعذر التشغيل': 'Could not start', 'تعديل': 'Edit', 'تعذر فتح الملف': 'Could not open file',
  'وضع تشغيل الدرايفرات': 'Driver execution mode', 'تلقائي (المعالج إن أمكن وإلا المفسّر)': 'Auto (CPU if possible, else interpreter)', 'المفسّر VM (معزول، أبطأ)': 'Interpreter VM (sandboxed, slower)', 'المعالج (سريع، يتطلب ذاكرة تنفيذ)': 'CPU (fast, needs executable memory)',
  'ذاكرة التنفيذ متاحة على هذا الجهاز': 'Executable memory is available on this device', 'ذاكرة التنفيذ غير متاحة: سيعمل المفسّر': 'Executable memory unavailable: the interpreter will be used', 'تم تغيير الوضع وأُعيد تحميل الدرايفرات': 'Mode changed and drivers reloaded', 'تعذر تغيير الوضع': 'Could not change mode', 'المعالج': 'CPU',
  'الشبكات المحفوظة (تُجرَّب بالترتيب والأقوى أولاً؛ إن فشلت كلها يدخل وضع الاسترداد)': 'Saved networks (tried strongest first; if all fail the device enters recovery mode)', 'اسم الشبكة': 'Network name', 'كلمة المرور': 'Password', 'إضافة شبكة': 'Add network', 'أعلى': 'Up',

  'درايفرات موقّعة تُحمَّل عند الإقلاع. سكريبت Lua يستعملها بـ drv.open(...) ولا يلمس العتاد مباشرة.': 'Signed drivers loaded at boot. Lua scripts use them with drv.open(...) and never touch the hardware directly.',
  'لا توجد درايفرات مثبّتة': 'No drivers installed', 'مفتوح': 'in use', 'رفع ملف درايفر': 'Upload a driver file',
  'تثبيت': 'Install', 'تم التثبيت': 'Installed', 'تعذر التثبيت': 'Install failed', 'حذف الدرايفر': 'Delete driver', 'تم الحذف': 'Deleted',
  'المتجر': 'Store', 'جلب': 'Fetch', 'الفهرس فارغ': 'The index is empty', 'مثبّت': 'Installed',
  'تعذر جلب الفهرس (يلزم اتصال بالإنترنت)': 'Could not fetch the index (internet required)', 'تعذر التنزيل': 'Download failed',
  'اختبار': 'Test', 'معامل': 'Argument', 'اختياري': 'optional', 'فتح في المختبر': 'Open in Lab',
  // Lab
  'مختبر Lua': 'Lua Lab',
  'اكتب سكريبت Lua وشغّله على الجهاز مباشرة. المتاح: gpio, adc, pwm, timer, sys, ui, print.': 'Write a Lua script and run it on the device. Available: gpio, adc, pwm, timer, sys, ui, print.',
  'اللوحات': 'Panels', 'لا توجد لوحات الآن': 'No panels right now',
  'اللوحات التي تنشئها السكريبتات بـ ui.panel(...). شغّل مثالًا من تبويب المختبر لتظهر هنا.': 'Panels created by scripts with ui.panel(...). Run an example from the Lab tab to see them here.',
  'صفحة HTML مخصصة (تُحفظ على الجهاز قبل التشغيل)': 'Custom HTML page (saved to the device before running)',
  'مسار الصفحة يجب أن يكون /ui/<اسم>.html': 'Page path must be /ui/<name>.html',
  'تعذر حفظ الصفحة': 'Could not save the page', 'تعذر تحميل الصفحة': 'Could not load the page',
  'اسم السكريبت': 'Script name', 'أمثلة...': 'Examples...', 'تشغيل': 'Run', 'إيقاف الكل': 'Stop all',
  'تعذر التشغيل (تحقق من الاسم أو الحجم)': 'Cannot run (check name or size)',
  // Lab (v0.5)
  'حفظ وتشغيل': 'Save & run', 'السكريبتات المحفوظة': 'Saved scripts', 'تنظيف': 'Clear',
  'لا توجد سكريبتات محفوظة بعد. «حفظ وتشغيل» يخزّن الملف على الجهاز.': 'No saved scripts yet. "Save & run" stores the file on the device.',
  'اسم السكريبت: حروف إنجليزية وأرقام و _ و - فقط (حتى 23)': 'Script name: English letters, digits, _ and - only (max 23)',
  'السكريبت فارغ': 'The script is empty', 'تعذر الإيقاف': 'Could not stop', 'تعذر التشغيل': 'Could not run', 'تعذر الحفظ': 'Could not save',
  'تعذر الحذف': 'Could not delete', 'تعذر فتح الملف': 'Could not open the file', 'حذف السكريبت': 'Delete script',
  'تشغيل': 'Run', 'إيقاف': 'Stop', 'تعديل': 'Edit', 'حذف': 'Delete',
  'تشغيل تلقائي عند إقلاع الجهاز': 'Run automatically at device boot',
  // Lab: libraries
  'حفظ كمكتبة (require) بدل التشغيل': 'Save as library (require) instead of running', 'حفظ المكتبة': 'Save library', 'حُفظت المكتبة': 'Library saved',
  'المكتبات': 'Libraries', 'حذف المكتبة': 'Delete library',
  'لا توجد مكتبات. فعّل «حفظ كمكتبة» واحفظ ملفًا ثم استعمله بـ require.': 'No libraries. Enable "Save as library", save a file, then use it with require.',
  // Login
  'تسجيل الدخول': 'Sign in', 'المستخدم': 'User', 'كلمة المرور': 'Password', 'دخول': 'Sign in',
  'اسم المستخدم أو كلمة المرور غير صحيحة': 'Wrong username or password',
  'تعذر الاتصال بالجهاز': 'Cannot reach the device',
  'محاولات كثيرة، انتظر 30 ثانية': 'Too many attempts, wait 30 seconds',
  'نسيت كلمة المرور؟': 'Forgot password?',
  'إعادة ضبط كلمة المرور': 'Reset password',
  'سيُعاد ضبط كلمات المرور إلى الافتراضية. بعد الموافقة اضغط زر الجهاز الفيزيائي. هل تريد المتابعة؟':
    'Passwords will be reset to the defaults. After confirming, press the physical button on the device. Continue?',
  'اضغط زر الجهاز (GPIO {n}) خلال {s} ثانية': 'Press the device button (GPIO {n}) within {s} seconds',
  'تمت إعادة الضبط. كلمات المرور الافتراضية:': 'Reset done. Default passwords:',
  'انتهت المهلة، أعد المحاولة': 'Timed out, try again',
  'تعذر بدء إعادة الضبط (تحقق من رقم PIN في الإعدادات)': 'Could not start the reset (check the PIN number in settings)',
  // Dashboard
  'استخدام الموارد': 'Resource usage', 'الذاكرة (RAM)': 'Memory (RAM)', 'التخزين': 'Storage',
  'الشبكة': 'Network', 'الإصدار': 'Version', 'مدة التشغيل': 'Uptime', 'حرارة المعالج': 'CPU temp', 'لا توجد أماكن كافية للدرايفرات': 'Not enough driver slots', 'التطبيق': 'App', 'يحتاج درايفرات جديدة': 'needs new drivers', 'الأماكن الكلية': 'Total slots', 'المستعملة': 'Used', 'المتبقية': 'Free', 'المطلوبة': 'Needed', 'اختر للحذف': 'Pick to delete', 'يستعمله': 'used by', 'احذف وأكمل التثبيت': 'Delete & continue install', 'إلغاء': 'Cancel', 'مؤقت': 'Timer', 'رجل': 'Pin', 'إقلاع عادي': 'Normal boot', 'حدّد مؤقتًا أو رجل استيقاظ': 'Set a timer or a wake pin', 'سيدخل الجهاز النوم العميق وينقطع الاتصال. متابعة؟': 'The device will deep-sleep and disconnect. Continue?', 'الجهاز في النوم العميق. يستيقظ بالمؤقت أو رجل الاستيقاظ أو زر RESET.': 'Device is in deep sleep. It wakes by timer, wake pin or RESET.', 'تعذّر': 'Failed', 'سبب آخر إقلاع': 'Last boot reason', 'النوم العميق': 'Deep sleep', 'مؤقت (ثانية، 0 = بلا)': 'Timer (s, 0 = none)', 'مستوى الاستيقاظ': 'Wake level', 'عالٍ': 'High', 'منخفض': 'Low', 'أرجل الاستيقاظ': 'Wake pins', 'نوم عميق الآن': 'Deep sleep now', 'الإشارة': 'Signal',
  'لا توجد بيانات (قد يكون الواي فاي قيد الضبط)': 'No data (WiFi may be reconfiguring)',
  'إعادة تشغيل الجهاز': 'Restart device', 'هل تريد إعادة تشغيل الجهاز؟': 'Restart the device?',
  'يُعاد تشغيل الجهاز، سجّل الدخول من جديد...': 'Restarting, sign in again...',
  // Wifi
  'إعدادات الواي فاي': 'WiFi settings', 'الوضع': 'Mode',
  'AP (نقطة وصول)': 'AP (access point)', 'STA (اتصال بشبكة)': 'STA (join a network)',
  'اسم الشبكة (STA)': 'Network name (STA)', 'مسح': 'Scan', 'كلمة مرور الشبكة': 'Network password',
  'اسم نقطة الوصول (AP)': 'Access point name (AP)',
  'كلمة مرور نقطة الوصول (8 أحرف على الأقل)': 'Access point password (min 8 chars)',
  'حفظ وتطبيق': 'Save & apply',
  'تم الحفظ. سيُعاد ضبط الواي فاي وقد ينقطع الاتصال لحظات.': 'Saved. WiFi is being reconfigured; the connection may drop briefly.',
  'فشل الحفظ': 'Save failed',
  // Comm
  'المضيف المحلي': 'Local host', 'مضيف الإنترنت': 'Internet host',
  'رقم PIN لإعادة الضبط': 'Reset PIN (GPIO number)', 'حفظ': 'Save', 'تم الحفظ': 'Saved',
  // Passwords
  'كلمة مرور admin': 'Admin password', 'كلمة مرور user': 'User password',
  'كلمة مرور صفحة الطوارئ (/em)': 'Emergency page password (/em)',
  '4 أحرف على الأقل (اتركها فارغة للإبقاء)': 'Min 4 chars (leave blank to keep)',
  'كلمة مرور الطوارئ بين 4 و 31 حرفًا': 'Emergency password must be 4–31 characters',
  'أدخل كلمة مرور واحدة على الأقل': 'Enter at least one password',
  'كلمة مرور جديدة (اتركها فارغة للإبقاء)': 'New password (leave blank to keep)',
  'تُخزَّن كلمات المرور معمّاة ولا يمكن استرجاعها؛ اترك الحقل فارغًا للإبقاء على كلمة المرور الحالية.':
    'Passwords are stored hashed and cannot be viewed; leave a field blank to keep the current password.',
  // Backup
  'نسخة احتياطية': 'Backup',
  'تحفظ الإعدادات (الواي فاي والشبكات المحفوظة، الاتصال، PIN) وبيانات التحقق في ملف JSON واحد.':
    'Saves settings (WiFi, saved networks, connection, PIN) and hashed credentials in one JSON file.',
  'تنزيل النسخة الاحتياطية': 'Download backup',
  'تنبيه: الملف يحتوي إعدادات الشبكات وبيانات التحقق المعمّاة، فاحفظه في مكان آمن.':
    'Note: the file contains network settings and hashed credentials, keep it in a safe place.',
  'استعادة': 'Restore', 'ملف النسخة الاحتياطية (.json)': 'Backup file (.json)',
  'استعادة وإعادة التشغيل': 'Restore & restart',
  'تم تنزيل النسخة الاحتياطية': 'Backup downloaded', 'فشل التنزيل: ': 'Download failed: ',
  'ستُستبدل الإعدادات وكلمات المرور الحالية بمحتوى الملف ويُعاد تشغيل الجهاز. متابعة؟': 'Current settings and passwords will be replaced and the device restarted. Continue?',
  'تمت الاستعادة. يُعاد تشغيل الجهاز، سجّل الدخول من جديد...': 'Restored. Restarting, sign in again...',
  'الملف ليس JSON صالحًا': 'The file is not valid JSON', 'فشلت الاستعادة (تحقق من الملف)': 'Restore failed (check the file)',
  // Ota
  'تحديث البرنامج (firmware)': 'Firmware update', 'الملف firmware-*.bin': 'File firmware-*.bin',
  'تحديث الواجهة (littlefs)': 'Web UI update (littlefs)', 'الملف littlefs-*.bin': 'File littlefs-*.bin',
  'رفع وتحديث': 'Upload & update',
  'تحذير: هذا يمسح الإعدادات وكلمات المرور المحفوظة ويعيدها للافتراضي.': 'Warning: this wipes the saved settings and passwords and restores the defaults.',
  'تحديث الواجهة يستبدل نظام الملفات بالكامل ويمسح الإعدادات وكلمات المرور. هل تريد المتابعة؟': 'Updating the UI replaces the whole filesystem and wipes settings and passwords. Continue?',
  'سيُحدَّث البرنامج ثم يُعاد تشغيل الجهاز. هل تريد المتابعة؟': 'The firmware will be updated and the device restarted. Continue?',
  'تم التحديث. يُعاد تشغيل الجهاز، وستحتاج إلى تسجيل الدخول من جديد...': 'Updated. The device is restarting, you will need to sign in again...',
  'فشل التحديث: ': 'Update failed: ', 'انقطع الاتصال أثناء الرفع': 'Connection lost during upload',
  // Terminal
  'عرض سجل الجهاز (Serial)': 'Live device log (Serial)', 'اكتب help لعرض الأوامر': 'Type help to list commands',
  'أمر...': 'Command...', 'إرسال': 'Send', 'مسح الشاشة': 'Clear'
};
