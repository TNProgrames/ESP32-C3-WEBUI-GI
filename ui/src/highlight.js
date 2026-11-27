// تلوين الكود بلا مكتبات: كل دالة تعيد HTML آمنًا (مُهرَّبًا) فيه <span class="tk-..."> فقط.
const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const span = (c, t) => `<span class="tk-${c}">${esc(t)}</span>`;

// يمسح النص بتعبير نمطي عام (g) ويلوّن كل تطابق بحسب cls(نص, موضع, المصدر)
function scan(src, re, cls) {
  let out = '', last = 0, m;
  re.lastIndex = 0;
  while ((m = re.exec(src))) {
    if (m[0] === '') { re.lastIndex++; continue; }
    if (m.index > last) out += esc(src.slice(last, m.index));
    const c = cls(m[0], m.index, src);
    out += c ? span(c, m[0]) : esc(m[0]);
    last = m.index + m[0].length;
  }
  return out + esc(src.slice(last));
}

// ---------- Lua ----------
const LUA_KW = new Set('and break do else elseif end for function goto if in local not or repeat return then until while'.split(' '));
const LUA_CONST = new Set(['true', 'false', 'nil']);
const LUA_LIB = new Set(['gpio', 'adc', 'pwm', 'timer', 'sys', 'ui', 'math', 'string', 'table', 'utf8', 'coroutine']);
const LUA_BUILTIN = new Set(['print', 'pcall', 'error', 'assert', 'ipairs', 'pairs', 'next', 'select', 'tonumber', 'tostring', 'type', 'setmetatable', 'getmetatable', 'rawget', 'rawset', 'rawequal', 'rawlen', 'collectgarbage', 'xpcall']);
const LUA_RE = /--\[(=*)\[[\s\S]*?(?:\]\1\]|$)|--[^\n]*|\[(=*)\[[\s\S]*?(?:\]\2\]|$)|"(?:\\[\s\S]|[^"\\\n])*"?|'(?:\\[\s\S]|[^'\\\n])*'?|\b0[xX][0-9a-fA-F]+\b|\b\d+\.?\d*(?:[eE][+-]?\d+)?|\.\d+|[A-Za-z_]\w*|\.\.\.?|[=~<>]=|[+\-*\/%^#<>=]|::/g;

export function highlightLua(src) {
  return scan(src, LUA_RE, (t, i, s) => {
    const c = t[0];
    if (t.startsWith('--')) return 'c';
    if (c === '"' || c === "'" || (c === '[' && /^\[=*\[/.test(t))) return 's';
    if (/^[0-9.]/.test(c)) return /^\.\.\.?$/.test(t) ? 'o' : 'n';
    if (/^[A-Za-z_]/.test(c)) {
      const prev = s[i - 1], nextCh = s.slice(i + t.length).match(/^\s*(\(|"|'|\{)/);
      if (prev === '.' || prev === ':') return nextCh ? 'fn' : 'p';          // حقل أو دالة بعد . أو :
      if (LUA_KW.has(t)) return 'k';
      if (LUA_CONST.has(t)) return 'n';
      if (LUA_LIB.has(t)) return 'l';
      if (LUA_BUILTIN.has(t)) return 'b';
      return nextCh ? 'fn' : null;
    }
    return 'o';
  });
}

// ---------- JavaScript ----------
const JS_KW = new Set('var let const function return if else for while do break continue switch case default new delete typeof instanceof in of this class extends try catch finally throw async await yield import export from'.split(' '));
const JS_CONST = new Set(['true', 'false', 'null', 'undefined', 'NaN', 'Infinity']);
const JS_RE = /\/\/[^\n]*|\/\*[\s\S]*?(?:\*\/|$)|"(?:\\[\s\S]|[^"\\\n])*"?|'(?:\\[\s\S]|[^'\\\n])*'?|`(?:\\[\s\S]|[^`\\])*`?|\b0[xX][0-9a-fA-F]+\b|\b\d+\.?\d*(?:[eE][+-]?\d+)?|[A-Za-z_$][\w$]*|[=!<>]=?=?|&&|\|\||[+\-*\/%?:]/g;

export function highlightJs(src) {
  return scan(src, JS_RE, (t, i, s) => {
    const c = t[0];
    if (t.startsWith('//') || t.startsWith('/*')) return 'c';
    if (c === '"' || c === "'" || c === '`') return 's';
    if (/^[0-9]/.test(c)) return 'n';
    if (/^[A-Za-z_$]/.test(c)) {
      const prev = s[i - 1], call = /^\s*\(/.test(s.slice(i + t.length, i + t.length + 12));
      if (prev === '.') return call ? 'fn' : 'p';
      if (JS_KW.has(t)) return 'k';
      if (JS_CONST.has(t)) return 'n';
      if (t === 'esp' || t === 'document' || t === 'window' || t === 'Math' || t === 'JSON') return 'l';
      return call ? 'fn' : null;
    }
    return 'o';
  });
}

// ---------- CSS ----------
const CSS_RE = /\/\*[\s\S]*?(?:\*\/|$)|"[^"\n]*"?|'[^'\n]*'?|#[0-9a-fA-F]{3,8}\b|-?[A-Za-z_][\w-]*(?=\s*:)|\b\d+\.?\d*(?:px|em|rem|%|s|ms|deg|vh|vw)?|[{}:;,]/g;

export function highlightCss(src) {
  return scan(src, CSS_RE, (t) => {
    if (t.startsWith('/*')) return 'c';
    if (t[0] === '"' || t[0] === "'") return 's';
    if (t[0] === '#' || /^\d/.test(t)) return 'n';
    if (/^[{}:;,]$/.test(t)) return 'o';
    return 'p';
  });
}

// ---------- HTML (مع <script> و<style> مضمَّنة) ----------
const TAG_RE = /<\/?[A-Za-z!][\w:-]*|"[^"]*"?|'[^']*'?|[\w:@.-]+(?=\s*=)|\/?>|=/g;
function tagHtml(t) {
  return scan(t, TAG_RE, (x, i) => {
    if (x[0] === '<') return 't';
    if (x[0] === '"' || x[0] === "'") return 's';
    if (x === '>' || x === '/>') return 't';
    if (x === '=') return 'o';
    return 'a';
  });
}
const HTML_RE = /<!--[\s\S]*?(?:-->|$)|<(script|style)\b[^>]*>[\s\S]*?(?:<\/\1\s*>|$)|<\/?[A-Za-z!][^>]*>?/gi;

export function highlightHtml(src) {
  let out = '', last = 0, m;
  HTML_RE.lastIndex = 0;
  while ((m = HTML_RE.exec(src))) {
    if (m.index > last) out += esc(src.slice(last, m.index));
    const t = m[0];
    if (t.startsWith('<!--')) out += span('c', t);
    else if (m[1]) {
      const open = t.match(/^<[^>]*>/)[0];
      const close = t.match(/<\/[a-z]+\s*>$/i);
      const inner = t.slice(open.length, close ? t.length - close[0].length : t.length);
      out += tagHtml(open) + (m[1].toLowerCase() === 'script' ? highlightJs(inner) : highlightCss(inner)) + (close ? tagHtml(close[0]) : '');
    } else out += tagHtml(t);
    last = m.index + t.length;
  }
  return out + esc(src.slice(last));
}

export function highlight(src, lang) {
  return lang === 'html' ? highlightHtml(src) : highlightLua(src);
}
