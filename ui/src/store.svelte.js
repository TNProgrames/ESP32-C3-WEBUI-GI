import { en } from './i18n.js';

const KEY = 'ui-prefs';
function load() {
  try { return JSON.parse(localStorage.getItem(KEY)) || {}; } catch { return {}; }
}
const saved = load();

export const ui = $state({ lang: saved.lang || 'ar', theme: saved.theme || 'dark' });

function apply() {
  document.documentElement.lang = ui.lang;
  document.documentElement.dir = ui.lang === 'ar' ? 'rtl' : 'ltr';
  document.documentElement.dataset.theme = ui.theme;
}
function persist() {
  apply();
  try { localStorage.setItem(KEY, JSON.stringify({ lang: ui.lang, theme: ui.theme })); } catch {}
}
export function toggleLang() { ui.lang = ui.lang === 'ar' ? 'en' : 'ar'; persist(); }
export function toggleTheme() { ui.theme = ui.theme === 'dark' ? 'light' : 'dark'; persist(); }

// يقرأ ui.lang فيتحدّث أي قالب يستعمله عند تغيير اللغة
export function t(s) { return ui.lang === 'en' ? (en[s] ?? s) : s; }

apply();
