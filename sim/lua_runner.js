// Lightweight Lua 5.4 subset compiler & ESP32-C3 hardware/driver simulator
// Supports the full scripting API used by examples.js and Store apps.

const MULTI = Symbol('lua_multi');
export function luaMulti(...vals) {
  const arr = [...vals];
  arr[MULTI] = true;
  return arr;
}
function unwrap(v) {
  return v && typeof v === 'object' && v[MULTI] ? v[0] : v;
}
function unpack(v) {
  if (v && typeof v === 'object' && v[MULTI]) return v;
  return [v];
}
function truthy(v) {
  const u = unwrap(v);
  return u !== false && u !== null && u !== undefined;
}
function luaAnd(a, bFn) {
  return truthy(a) ? bFn() : unwrap(a);
}
function luaOr(a, bFn) {
  return truthy(a) ? unwrap(a) : bFn();
}
function luaLen(v) {
  const u = unwrap(v);
  if (typeof u === 'string') return u.length;
  if (Array.isArray(u)) return u.length;
  if (u && typeof u === 'object') return Object.keys(u).length;
  return 0;
}

// printf-style formatter for Lua string.format
function sprintf(fmt, ...args) {
  let i = 0;
  return String(fmt).replace(/%([+-0 #]*)(\d*)(?:\.(\d+))?([diuxXfsc%])/g, (_, flags, width, prec, spec) => {
    if (spec === '%') return '%';
    const raw = unwrap(args[i++]);
    let s = '';
    if (spec === 's') {
      s = raw === undefined || raw === null ? 'nil' : String(raw);
    } else if (spec === 'c') {
      s = String.fromCharCode(Number(raw) || 0);
    } else if (spec === 'd' || spec === 'i' || spec === 'u') {
      const n = Math.trunc(Number(raw) || 0);
      s = spec === 'u' ? String(n >>> 0) : String(n);
      if (prec !== undefined) s = s.padStart(Number(prec), '0');
    } else if (spec === 'x' || spec === 'X') {
      let n = (Math.trunc(Number(raw) || 0) >>> 0).toString(16);
      if (spec === 'X') n = n.toUpperCase();
      if (prec !== undefined) n = n.padStart(Number(prec), '0');
      s = n;
    } else if (spec === 'f') {
      const p = prec !== undefined ? Number(prec) : 6;
      s = (Number(raw) || 0).toFixed(p);
    }
    const w = Number(width) || 0;
    if (w > s.length) {
      const pad = flags.includes('0') ? '0' : ' ';
      s = flags.includes('-') ? s.padEnd(w, ' ') : s.padStart(w, pad);
    }
    return s;
  });
}

// Basic Lua pattern matcher for common patterns like "^(%d+)-(%d+)-..."
function luaMatch(str, pat) {
  let reStr = String(pat)
    .replace(/%d/g, '\\d')
    .replace(/%a/g, '[A-Za-z]')
    .replace(/%w/g, '[A-Za-z0-9]')
    .replace(/%s/g, '\\s')
    .replace(/%\./g, '\\.');
  const re = new RegExp(reStr);
  const m = String(str).match(re);
  if (!m) return luaMulti(null);
  if (m.length === 1) return m[0];
  return luaMulti(...m.slice(1));
}

// Tokenizer for Lua source
function tokenize(src) {
  const tokens = [];
  let i = 0;
  let line = 1;
  const n = src.length;
  const kw = new Set([
    'and', 'break', 'do', 'else', 'elseif', 'end', 'false', 'for', 'function',
    'if', 'in', 'local', 'nil', 'not', 'or', 'repeat', 'return', 'then', 'true', 'until', 'while'
  ]);

  while (i < n) {
    const ch = src[i];
    if (ch === '\n') { line++; i++; continue; }
    if (/\s/.test(ch)) { i++; continue; }
    // Comments
    if (ch === '-' && src[i + 1] === '-') {
      i += 2;
      if (src[i] === '[' && src[i + 1] === '[') {
        i += 2;
        while (i < n && !(src[i] === ']' && src[i + 1] === ']')) {
          if (src[i] === '\n') line++;
          i++;
        }
        i += 2;
      } else {
        while (i < n && src[i] !== '\n') i++;
      }
      continue;
    }
    // Strings
    if (ch === '"' || ch === "'") {
      const q = ch;
      const startLine = line;
      i++;
      let val = '';
      while (i < n && src[i] !== q) {
        if (src[i] === '\\') {
          i++;
          const e = src[i++];
          if (e === 'n') val += '\n';
          else if (e === 'r') val += '\r';
          else if (e === 't') val += '\t';
          else val += e || '';
        } else {
          if (src[i] === '\n') line++;
          val += src[i++];
        }
      }
      i++;
      tokens.push({ type: 'str', value: val, line: startLine });
      continue;
    }
    // Numbers
    if (/\d/.test(ch) || (ch === '.' && /\d/.test(src[i + 1]))) {
      const start = i;
      if (ch === '0' && (src[i + 1] === 'x' || src[i + 1] === 'X')) {
        i += 2;
        while (i < n && /[0-9a-fA-F]/.test(src[i])) i++;
        tokens.push({ type: 'num', value: parseInt(src.slice(start, i), 16), line });
      } else {
        while (i < n && /[\d.]/.test(src[i])) i++;
        if (src[i] === 'e' || src[i] === 'E') {
          i++;
          if (src[i] === '+' || src[i] === '-') i++;
          while (i < n && /\d/.test(src[i])) i++;
        }
        tokens.push({ type: 'num', value: Number(src.slice(start, i)), line });
      }
      continue;
    }
    // Identifiers / Keywords
    if (/[A-Za-z_]/.test(ch)) {
      const start = i;
      while (i < n && /[A-Za-z0-9_]/.test(src[i])) i++;
      const word = src.slice(start, i);
      tokens.push({ type: kw.has(word) ? 'kw' : 'id', value: word, line });
      continue;
    }
    // Multi-char operators
    const two = src.slice(i, i + 2);
    if (['..', '//', '==', '~=', '<=', '>='].includes(two)) {
      tokens.push({ type: 'op', value: two, line });
      i += 2;
      continue;
    }
    // Single-char punctuation/operators
    tokens.push({ type: 'op', value: ch, line });
    i++;
  }
  tokens.push({ type: 'eof', value: '', line });
  return tokens;
}

// Compile Lua tokens into JS async-free body
export function compileLua(src) {
  const toks = tokenize(src);
  let pos = 0;
  let guardId = 0;

  const peek = (o = 0) => toks[pos + o] || toks[toks.length - 1];
  const next = () => toks[pos++];
  const is = (type, val) => peek().type === type && (val === undefined || peek().value === val);
  const eat = (type, val) => {
    if (is(type, val)) return next();
    const t = peek();
    throw new Error(`:${t.line}: unexpected '${t.value || 'EOF'}' (expected '${val || type}')`);
  };
  const tryEat = (type, val) => (is(type, val) ? next() : null);

  function safeId(name) {
    const reserved = new Set(['var', 'let', 'const', 'function', 'return', 'if', 'else', 'for', 'while', 'do', 'break', 'switch', 'case', 'default', 'try', 'catch', 'finally', 'throw', 'new', 'delete', 'typeof', 'void', 'in', 'of', 'class', 'extends', 'import', 'export', 'await', 'async', 'yield', 'true', 'false', 'null', 'undefined', 'this', 'super', 'arguments', 'eval']);
    return reserved.has(name) ? `$lua_${name}` : name;
  }

  function parseBlock(stopWords) {
    const stmts = [];
    while (!is('eof') && !(peek().type === 'kw' && stopWords.includes(peek().value))) {
      tryEat('op', ';');
      if (is('eof') || (peek().type === 'kw' && stopWords.includes(peek().value))) break;
      stmts.push(parseStmt());
      tryEat('op', ';');
    }
    return stmts.join('\n');
  }

  function parseParams() {
    eat('op', '(');
    const params = [];
    while (!is('op', ')')) {
      if (is('op', '.')) { // ...
        next(); eat('op', '.'); eat('op', '.');
        break;
      }
      const id = eat('id').value;
      params.push(safeId(id));
      if (!tryEat('op', ',')) break;
    }
    eat('op', ')');
    return params;
  }

  function parseStmt() {
    const t = peek();
    if (is('kw', 'local')) {
      next();
      if (tryEat('kw', 'function')) {
        const name = safeId(eat('id').value);
        const params = parseParams();
        const body = parseBlock(['end']);
        eat('kw', 'end');
        return `let ${name} = function(${params.join(',')}) {\n${body}\n};`;
      }
      const names = [safeId(eat('id').value)];
      while (tryEat('op', ',')) names.push(safeId(eat('id').value));
      if (tryEat('op', '=')) {
        const exprs = parseExprList();
        if (names.length === 1 && exprs.length === 1) {
          return `let ${names[0]} = __unwrap(${exprs[0]});`;
        }
        if (exprs.length === 1) {
          return `let [${names.join(',')}] = __unpack(${exprs[0]});`;
        }
        return `let [${names.join(',')}] = [${exprs.map((e) => `__unwrap(${e})`).join(',')}];`;
      }
      return `let ${names.join(', ')};`;
    }
    if (is('kw', 'function')) {
      next();
      let name = safeId(eat('id').value);
      let isMethod = false;
      while (is('op', '.') || is('op', ':')) {
        const sep = next().value;
        const part = eat('id').value;
        if (sep === ':') { isMethod = true; name += `.${part}`; break; }
        name += `.${part}`;
      }
      const params = parseParams();
      if (isMethod) params.unshift('self');
      const body = parseBlock(['end']);
      eat('kw', 'end');
      if (name === 'on_stop' && !isMethod) {
        return `on_stop(function(${params.join(',')}) {\n${body}\n});`;
      }
      return `${name} = function(${params.join(',')}) {\n${body}\n};`;
    }
    if (is('kw', 'if')) {
      next();
      let out = `if (__truthy(${parseExpr()})) {\n`;
      eat('kw', 'then');
      out += parseBlock(['elseif', 'else', 'end']) + '\n}';
      while (tryEat('kw', 'elseif')) {
        out += ` else if (__truthy(${parseExpr()})) {\n`;
        eat('kw', 'then');
        out += parseBlock(['elseif', 'else', 'end']) + '\n}';
      }
      if (tryEat('kw', 'else')) {
        out += ` else {\n${parseBlock(['end'])}\n}`;
      }
      eat('kw', 'end');
      return out;
    }
    if (is('kw', 'while')) {
      const wLine = t.line;
      next();
      const cond = parseExpr();
      eat('kw', 'do');
      const g = `__g${++guardId}`;
      const body = parseBlock(['end']);
      eat('kw', 'end');
      return `for (let ${g}=0; __truthy(${cond}); ) { if (++${g} > 50000) throw new Error(":${wLine}: instruction limit exceeded (watchdog)");\n${body}\n}`;
    }
    if (is('kw', 'repeat')) {
      const rLine = t.line;
      next();
      const g = `__g${++guardId}`;
      const body = parseBlock(['until']);
      eat('kw', 'until');
      const cond = parseExpr();
      return `for (let ${g}=0; ; ) { if (++${g} > 50000) throw new Error(":${rLine}: instruction limit exceeded (watchdog)");\n${body}\nif (__truthy(${cond})) break; }`;
    }
    if (is('kw', 'for')) {
      const fLine = t.line;
      next();
      const v1 = safeId(eat('id').value);
      if (tryEat('op', '=')) {
        const start = `__unwrap(${parseExpr()})`;
        eat('op', ',');
        const stop = `__unwrap(${parseExpr()})`;
        const step = tryEat('op', ',') ? `__unwrap(${parseExpr()})` : '1';
        eat('kw', 'do');
        const g = `__g${++guardId}`;
        const body = parseBlock(['end']);
        eat('kw', 'end');
        return `{ const __st=${step}, __en=${stop}; let ${g}=0; for (let ${v1}=${start}; __st>=0 ? ${v1}<=__en : ${v1}>=__en; ${v1}+=__st) { if (++${g} > 50000) throw new Error(":${fLine}: instruction limit exceeded");\n${body}\n} }`;
      }
      const vars = [v1];
      while (tryEat('op', ',')) vars.push(safeId(eat('id').value));
      eat('kw', 'in');
      const iterExpr = parseExpr();
      eat('kw', 'do');
      const g = `__g${++guardId}`;
      const body = parseBlock(['end']);
      eat('kw', 'end');
      return `{ let ${g}=0; for (const [${vars.join(',')}] of __iter(${iterExpr})) { if (++${g} > 50000) throw new Error(":${fLine}: instruction limit exceeded");\n${body}\n} }`;
    }
    if (is('kw', 'do')) {
      next();
      const body = parseBlock(['end']);
      eat('kw', 'end');
      return `{\n${body}\n}`;
    }
    if (is('kw', 'return')) {
      next();
      if (is('eof') || is('op', ';') || (peek().type === 'kw' && ['end', 'else', 'elseif', 'until'].includes(peek().value))) {
        return 'return;';
      }
      const exprs = parseExprList();
      if (exprs.length === 1) return `return ${exprs[0]};`;
      return `return __multi(${exprs.map((e) => `__unwrap(${e})`).join(',')});`;
    }
    if (is('kw', 'break')) {
      next();
      return 'break;';
    }
    // Expression statement (call or assignment)
    const lhs = parseSuffixedInfo();
    if (is('op', '=')) {
      next();
      const rhs = parseExprList();
      if (rhs.length === 1) return lhs.assign(`__unwrap(${rhs[0]})`) + ';';
      const tmps = rhs.map((_, i) => `__m${++guardId}`);
      return `{ const [${tmps.join(',')}] = [${rhs.map((e) => `__unwrap(${e})`).join(',')}]; ${lhs.assign(tmps[0])}; }`;
    }
    if (is('op', ',')) {
      const targets = [lhs];
      while (tryEat('op', ',')) targets.push(parseSuffixedInfo());
      eat('op', '=');
      const rhs = parseExprList();
      const tmps = targets.map(() => `__m${++guardId}`);
      const rhsCode = rhs.length === 1
        ? `__unpack(${rhs[0]})`
        : `[${rhs.map((e) => `__unwrap(${e})`).join(',')}]`;
      const assigns = targets.map((t, i) => t.assign(tmps[i]) + ';').join(' ');
      return `{ const [${tmps.join(',')}] = ${rhsCode}; ${assigns} }`;
    }
    return `${lhs.expr};`;
  }

  function parseExprList() {
    const list = [parseExpr()];
    while (tryEat('op', ',')) list.push(parseExpr());
    return list;
  }

  function parseExpr() { return parseOr(); }
  function parseOr() {
    let e = parseAnd();
    while (tryEat('kw', 'or')) { const r = parseAnd(); e = `__or(${e}, () => ${r})`; }
    return e;
  }
  function parseAnd() {
    let e = parseCmp();
    while (tryEat('kw', 'and')) { const r = parseCmp(); e = `__and(${e}, () => ${r})`; }
    return e;
  }
  function parseCmp() {
    let e = parseConcat();
    while (is('op') && ['==', '~=', '<', '>', '<=', '>='].includes(peek().value)) {
      const op = next().value;
      const r = parseConcat();
      const jop = op === '~=' ? '!==' : op === '==' ? '===' : op;
      e = `(__unwrap(${e}) ${jop} __unwrap(${r}))`;
    }
    return e;
  }
  function parseConcat() {
    let e = parseAdd();
    while (tryEat('op', '..')) {
      const r = parseAdd();
      e = `(String(__unwrap(${e}) ?? '') + String(__unwrap(${r}) ?? ''))`;
    }
    return e;
  }
  function parseAdd() {
    let e = parseMul();
    while (is('op') && (peek().value === '+' || peek().value === '-')) {
      const op = next().value;
      const r = parseMul();
      e = `(Number(__unwrap(${e})) ${op} Number(__unwrap(${r})))`;
    }
    return e;
  }
  function parseMul() {
    let e = parseUnary();
    while (is('op') && ['*', '/', '//', '%'].includes(peek().value)) {
      const op = next().value;
      const r = parseUnary();
      if (op === '//') e = `Math.floor(Number(__unwrap(${e})) / Number(__unwrap(${r})))`;
      else e = `(Number(__unwrap(${e})) ${op} Number(__unwrap(${r})))`;
    }
    return e;
  }
  function parseUnary() {
    if (tryEat('kw', 'not')) return `(!__truthy(${parseUnary()}))`;
    if (tryEat('op', '-')) return `(-Number(__unwrap(${parseUnary()})))`;
    if (tryEat('op', '#')) return `__len(${parseUnary()})`;
    return parsePow();
  }
  function parsePow() {
    let e = parsePrimary();
    if (tryEat('op', '^')) {
      const r = parseUnary();
      e = `Math.pow(Number(__unwrap(${e})), Number(__unwrap(${r})))`;
    }
    return e;
  }

  function parsePrimary() {
    if (is('kw', 'nil')) { next(); return 'null'; }
    if (is('kw', 'true')) { next(); return 'true'; }
    if (is('kw', 'false')) { next(); return 'false'; }
    if (is('num')) return JSON.stringify(next().value);
    if (is('str')) return JSON.stringify(next().value);
    if (is('kw', 'function')) {
      next();
      const params = parseParams();
      const body = parseBlock(['end']);
      eat('kw', 'end');
      return `(function(${params.join(',')}) {\n${body}\n})`;
    }
    if (is('op', '{')) return parseTable();
    return parseSuffixed();
  }

  function parseTable() {
    eat('op', '{');
    const items = [];
    let isArr = true;
    while (!is('op', '}')) {
      if (is('op', '[')) {
        isArr = false;
        next();
        const k = parseExpr();
        eat('op', ']');
        eat('op', '=');
        const v = parseExpr();
        items.push({ k, v });
      } else if (peek().type === 'id' && peek(1).type === 'op' && peek(1).value === '=') {
        isArr = false;
        const k = JSON.stringify(next().value);
        next();
        const v = parseExpr();
        items.push({ k, v });
      } else {
        const v = parseExpr();
        items.push({ k: null, v });
      }
      if (!tryEat('op', ',') && !tryEat('op', ';')) break;
    }
    eat('op', '}');
    if (isArr) {
      return `[${items.map((x) => `__unwrap(${x.v})`).join(',')}]`;
    }
    let idx = 1;
    const props = items.map((x) => {
      const key = x.k !== null ? `[__unwrap(${x.k})]` : `[${idx++}]`;
      return `${key}: __unwrap(${x.v})`;
    });
    return `{${props.join(',')}}`;
  }

  function parseArgs() {
    if (tryEat('op', '(')) {
      const args = [];
      while (!is('op', ')')) {
        args.push(parseExpr());
        if (!tryEat('op', ',')) break;
      }
      eat('op', ')');
      return args;
    }
    if (is('str')) return [JSON.stringify(next().value)];
    if (is('op', '{')) return [parseTable()];
    const t = peek();
    throw new Error(`:${t.line}: expected function arguments near '${t.value}'`);
  }

  function parseSuffixedInfo() {
    let expr;
    let assignTarget = null;
    if (tryEat('op', '(')) {
      expr = `(${parseExpr()})`;
      eat('op', ')');
    } else if (is('id')) {
      expr = safeId(next().value);
      assignTarget = { type: 'expr', expr };
    } else {
      const t = peek();
      throw new Error(`:${t.line}: unexpected symbol '${t.value}'`);
    }

    while (true) {
      if (tryEat('op', '.')) {
        const prop = eat('id').value;
        expr = `${expr}.${prop}`;
        assignTarget = { type: 'expr', expr };
      } else if (tryEat('op', '[')) {
        const base = expr;
        const idx = `__unwrap(${parseExpr()})`;
        eat('op', ']');
        expr = `__idx(${base}, ${idx})`;
        assignTarget = { type: 'idx', base, idx };
      } else if (tryEat('op', ':')) {
        const method = eat('id').value;
        const args = parseArgs();
        expr = `__callMethod(${expr}, ${JSON.stringify(method)}, [${args.map((a) => `__unwrap(${a})`).join(',')}])`;
        assignTarget = null;
      } else if (is('op', '(') || is('str') || is('op', '{')) {
        const args = parseArgs();
        expr = `${expr}(${args.map((a) => `__unwrap(${a})`).join(',')})`;
        assignTarget = null;
      } else {
        break;
      }
    }
    return {
      expr,
      assign(valCode) {
        if (assignTarget?.type === 'idx') {
          return `__setIdx(${assignTarget.base}, ${assignTarget.idx}, ${valCode})`;
        }
        return `${expr} = ${valCode}`;
      }
    };
  }

  function parseSuffixed() {
    return parseSuffixedInfo().expr;
  }

  const body = parseBlock([]);
  return body;
}

// Helper for 1-based array indexing vs object key lookup
function luaIdx(obj, k) {
  if (Array.isArray(obj) && typeof k === 'number') return obj[k - 1];
  return obj?.[k];
}
function luaSetIdx(obj, k, v) {
  if (Array.isArray(obj) && typeof k === 'number' && k >= 1) obj[k - 1] = v;
  else if (obj && typeof obj === 'object') obj[k] = v;
  return v;
}
function normalizeLuaValue(val, seen = new WeakSet()) {
  if (val === null || val === undefined || typeof val !== 'object') return val;
  if (seen.has(val)) return null;
  seen.add(val);
  if (Array.isArray(val)) {
    const keys = Object.keys(val);
    const hasNamedKeys = keys.some((k) => !/^\d+$/.test(k));
    if (hasNamedKeys) {
      const obj = {};
      for (let i = 0; i < val.length; i++) {
        if (val[i] !== undefined) obj[i + 1] = normalizeLuaValue(val[i], seen);
      }
      for (const k of keys) {
        if (!/^\d+$/.test(k)) obj[k] = normalizeLuaValue(val[k], seen);
      }
      return obj;
    }
    return val.map((item) => normalizeLuaValue(item, seen));
  }
  const out = {};
  for (const [k, v] of Object.entries(val)) out[k] = normalizeLuaValue(v, seen);
  return out;
}
function luaCallMethod(obj, method, args) {
  if (typeof obj === 'string' && method === 'match') {
    return luaMatch(obj, args[0]);
  }
  if (obj && typeof obj[method] === 'function') {
    return obj[method](...args);
  }
  throw new Error(`attempt to call method '${method}' (a nil value)`);
}
function luaIter(x) {
  return x || [];
}

export function createLuaScriptRuntime({
  name,
  code,
  log,
  uiApi,
  hardware,
  requireLib,
  openDriver,
  storageApi,
  busApi,
  onError
}) {
  const timers = new Set();
  let stopped = false;
  let nextTimerId = 1;
  const timerMap = new Map();
  let onStopCb = null;

  const env = {
    __unwrap: unwrap,
    __unpack: unpack,
    __truthy: truthy,
    __and: luaAnd,
    __or: luaOr,
    __len: luaLen,
    __multi: luaMulti,
    __idx: luaIdx,
    __setIdx: luaSetIdx,
    __callMethod: luaCallMethod,
    __iter: luaIter,
    on_stop(fn) {
      if (typeof fn === 'function') onStopCb = fn;
    },
    print(...args) {
      log(`[${name}] ${args.map((a) => (a === null || a === undefined ? 'nil' : typeof a === 'object' ? JSON.stringify(normalizeLuaValue(a)) : String(a))).join('\t')}\n`);
    },
    log(...args) {
      env.print(...args);
    },
    tostring(v) {
      const u = unwrap(v);
      return u === null || u === undefined ? 'nil' : String(u);
    },
    tonumber(v) {
      const n = Number(unwrap(v));
      return Number.isNaN(n) ? null : n;
    },
    type(v) {
      const u = unwrap(v);
      if (u === null || u === undefined) return 'nil';
      if (typeof u === 'boolean') return 'boolean';
      if (typeof u === 'number') return 'number';
      if (typeof u === 'string') return 'string';
      if (typeof u === 'function') return 'function';
      return 'table';
    },
    ipairs(arr) {
      if (!Array.isArray(arr)) return [];
      return arr.map((v, i) => [i + 1, v]);
    },
    pairs(obj) {
      if (!obj || typeof obj !== 'object') return [];
      if (Array.isArray(obj)) return obj.map((v, i) => [i + 1, v]);
      return Object.entries(obj);
    },
    select(idx, ...args) {
      const flat = args.flatMap((a) => unpack(a));
      if (idx === '#') return flat.length;
      const i = Number(idx);
      if (i < 1) return luaMulti();
      return luaMulti(...flat.slice(i - 1));
    },
    require(libName) {
      return requireLib(String(libName), env);
    },
    math: {
      abs: Math.abs,
      floor: Math.floor,
      ceil: Math.ceil,
      round: Math.round,
      min: Math.min,
      max: Math.max,
      sin: Math.sin,
      cos: Math.cos,
      tan: Math.tan,
      atan2: Math.atan2,
      sqrt: Math.sqrt,
      pow: Math.pow,
      log: Math.log,
      exp: Math.exp,
      pi: Math.PI,
      random: (a, b) => {
        if (a === undefined) return Math.random();
        if (b === undefined) return Math.floor(Math.random() * a) + 1;
        return Math.floor(Math.random() * (b - a + 1)) + a;
      }
    },
    string: {
      format: sprintf,
      len: (s) => String(s ?? '').length,
      sub: (s, i, j) => String(s ?? '').slice(i - 1, j),
      upper: (s) => String(s ?? '').toUpperCase(),
      lower: (s) => String(s ?? '').toLowerCase(),
      match: luaMatch
    },
    table: {
      insert(arr, posOrVal, val) {
        if (!Array.isArray(arr)) return;
        if (val === undefined) arr.push(posOrVal);
        else arr.splice(posOrVal - 1, 0, val);
      },
      remove(arr, pos) {
        if (!Array.isArray(arr)) return null;
        if (pos === undefined) return arr.pop();
        return arr.splice(pos - 1, 1)[0];
      },
      concat(arr, sep = '') {
        return Array.isArray(arr) ? arr.join(sep) : '';
      }
    },
    sys: {
      heap: () => Math.max(85000, Math.round(180000 - Math.random() * 4000)),
      uptime: () => hardware.uptimeSec(),
      millis: () => Date.now() - hardware.bootAt,
      version: () => hardware.version,
      temp: () => 40.5 + Math.sin(Date.now() / 5000) * 1.4
    },
    gpio: {
      mode(pin, mode) { hardware.gpioMode(pin, mode); },
      write(pin, v) { hardware.gpioWrite(pin, v ? 1 : 0); },
      read(pin) { return hardware.gpioRead(pin); },
      toggle(pin) { return hardware.gpioToggle(pin); },
      on_change(pin, cb) { hardware.gpioOnChange(name, pin, cb); }
    },
    adc: {
      atten() {},
      read(pin) { return Math.round(1400 + 800 * Math.sin(Date.now() / 1800 + pin)); },
      mv(pin) { return Math.round(1150 + 650 * Math.sin(Date.now() / 1800 + pin)); },
      read_mv(pin) { return Math.round(1150 + 650 * Math.sin(Date.now() / 1800 + pin)); }
    },
    pwm: {
      set(pin, freq, duty) { hardware.pwmSet(pin, freq, duty); },
      duty(pin, duty) { hardware.pwmDuty(pin, duty); },
      stop(pin) { hardware.pwmStop(pin); }
    },
    timer: {
      every(ms, fn) {
        if (stopped) return 0;
        const id = nextTimerId++;
        const h = setInterval(() => {
          if (stopped) return;
          try { fn(); } catch (e) { fail(e); }
        }, Math.max(50, Number(ms) || 100));
        timers.add(h);
        timerMap.set(id, { type: 'interval', h });
        return id;
      },
      after(ms, fn) {
        if (stopped) return 0;
        const id = nextTimerId++;
        const h = setTimeout(() => {
          timers.delete(h);
          timerMap.delete(id);
          if (stopped) return;
          try { fn(); } catch (e) { fail(e); }
        }, Math.max(10, Number(ms) || 100));
        timers.add(h);
        timerMap.set(id, { type: 'timeout', h });
        return id;
      },
      cancel(id) {
        const entry = timerMap.get(id);
        if (!entry) return;
        if (entry.type === 'interval') clearInterval(entry.h);
        else clearTimeout(entry.h);
        timers.delete(entry.h);
        timerMap.delete(id);
      }
    },
    storage: storageApi(name),
    bus: busApi(name),
    i2c: {
      begin(sda, scl, freq) { hardware.i2cBegin(sda, scl, freq); },
      stop() {},
      scan() { return [0x27, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x68, 0x76]; },
      write() { return true; },
      read(_addr, n) { return new Array(n || 1).fill(0); },
      write_read(_addr, _tx, n) { return new Array(n || 1).fill(0); },
      write_reg() { return true; },
      read_reg(addr, reg) {
        if (addr === 0x76 && reg === 0xD0) return 0x60;
        return 0;
      }
    },
    ui: {
      panel(title) {
        const p = uiApi.panel(title);
        if (!p) return p;
        return {
          ...p,
          set(wid, val) { return p.set(wid, normalizeLuaValue(val)); },
          emit(evtName, val) { return p.emit(evtName, normalizeLuaValue(val)); }
        };
      }
    },
    drv: {
      open(drvName, cfg) {
        return openDriver(drvName, cfg);
      }
    }
  };

  function fail(e) {
    stop(true);
    onError(e?.message || String(e));
  }

  function stop(skipCallback = false) {
    if (stopped) return;
    stopped = true;
    for (const h of timers) {
      clearInterval(h);
      clearTimeout(h);
    }
    timers.clear();
    timerMap.clear();
    if (!skipCallback) {
      try {
        if (typeof env.on_stop === 'function' && env.on_stop !== onStopRegister) {
          env.on_stop();
        } else if (typeof onStopCb === 'function') {
          onStopCb();
        }
      } catch {}
    }
  }

  // Keep reference to default on_stop register so `function on_stop() ... end` overrides env.on_stop cleanly
  const onStopRegister = env.on_stop;

  function start() {
    try {
      const jsBody = compileLua(code);
      const keys = Object.keys(env);
      const fn = new Function(...keys, jsBody);
      fn(...keys.map((k) => env[k]));
    } catch (e) {
      fail(e);
    }
  }

  return { start, stop, env };
}
