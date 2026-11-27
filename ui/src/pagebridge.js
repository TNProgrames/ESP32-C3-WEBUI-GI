// يبني مستند srcdoc للصفحة المخصصة: سياسة CSP تمنع الشبكة + جسر esp للتواصل مع اللوحة الأم.
// الإطار يُنشأ بـ sandbox="allow-scripts" فقط (بلا allow-same-origin) فلا يصل إلى الرمز ولا إلى واجهة الجهاز.
const CSP = '<meta http-equiv="Content-Security-Policy" content="default-src \'none\'; script-src \'unsafe-inline\'; style-src \'unsafe-inline\'; img-src data: blob:; font-src data:; media-src data: blob:">';

const BRIDGE = '<script>(function(){var H={},L={};' +
  'window.esp={theme:"dark",lang:"ar",' +
  'on:function(n,f){H[n]=f;if(n in L){try{f(L[n])}catch(e){}}},' +
  'send:function(n,v){parent.postMessage({esp:1,k:"send",n:String(n),v:v===undefined?null:v},"*")},' +
  'height:function(h){parent.postMessage({esp:1,k:"h",h:+h},"*")}};' +
  'addEventListener("message",function(e){var d=e.data;if(!d||!d.esp)return;' +
  'if(d.k==="init"){esp.theme=d.theme;esp.lang=d.lang;var r=document.documentElement;' +
  'r.setAttribute("data-theme",d.theme);r.lang=d.lang;r.dir=d.lang==="ar"?"rtl":"ltr";' +
  'if(esp.onchange)try{esp.onchange()}catch(x){}}' +
  'else if(d.k==="emit"){L[d.n]=d.v;if(H[d.n])try{H[d.n](d.v)}catch(x){}}});' +
  'parent.postMessage({esp:1,k:"hello"},"*");})();<\/script>';

export function buildSrcdoc(html) {
  const m = html.match(/^\s*<!doctype[^>]*>/i);
  const pre = m ? m[0] : '';
  const rest = m ? html.slice(m[0].length) : html;
  return pre + CSP + BRIDGE + rest;
}
