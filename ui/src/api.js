// طبقة الاتصال بالـ API. الرمز يُحفظ في sessionStorage ويُرسل في الترويسة User_Token
export const session = {
  get token() { return sessionStorage.getItem('token') || ''; },
  get role() { return sessionStorage.getItem('role') || ''; },
  set(token, role) { sessionStorage.setItem('token', token); sessionStorage.setItem('role', role); },
  clear() { sessionStorage.removeItem('token'); sessionStorage.removeItem('role'); }
};

export class ApiError extends Error {
  constructor(status) { super('HTTP ' + status); this.status = status; }
}

export async function api(path, body) {
  const opts = { headers: { User_Token: session.token, User_Role: session.role } };
  if (body !== undefined) {
    opts.method = 'POST';
    opts.headers['Content-Type'] = 'application/json';
    opts.body = JSON.stringify(body);
  }
  const res = await fetch(path, opts);
  if (res.status === 401 && !path.startsWith('/login') && !path.startsWith('/settings/get_stat') && !path.startsWith('/settings/scan_wifi')) {
    session.clear();
    location.reload();
  }
  if (!res.ok) throw new ApiError(res.status);
  const text = await res.text();
  return text ? JSON.parse(text) : null;
}

// إرسال نص خام (مثل سكريبت Lua) مع الرمز. لا نستعمل text/plain: المكتبة تعتبر الجسم الذي يحوي = نموذجًا (form) فلا يصل للمعالج (خطأ 501)
export async function apiRaw(path, text = '') {
  const res = await fetch(path, { method: 'POST', headers: { User_Token: session.token, User_Role: session.role, 'Content-Type': 'application/octet-stream' }, body: text });
  if (res.status === 401) { session.clear(); location.reload(); }
  if (!res.ok) throw new ApiError(res.status);
  const s = await res.text();
  return s ? JSON.parse(s) : null;
}

export function authHeaders() { return { User_Token: session.token, User_Role: session.role }; }
