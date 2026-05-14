// Empty API_BASE = same-origin (nginx reverse-proxies /api/* to Windows backend).
// Set VITE_API_BASE=http://host:5151 only if you want the browser to call
// the backend directly (requires backend CORS + bypass of Chrome PNA).
const API_BASE = (import.meta.env.VITE_API_BASE as string | undefined) ?? '';

export class ApiError extends Error {
  constructor(public status: number, message: string, public code?: string) {
    super(message);
  }
}

//--- Optional 401 handler registered by AuthContext. When a request returns
//--- 401, we still throw ApiError(401) so caller error-handlers can run, but
//--- we also invoke this hook so the app can flip global auth state and
//--- redirect to /login.
type UnauthorizedHandler = () => void;
let unauthorizedHandler: UnauthorizedHandler | null = null;
export function setUnauthorizedHandler(fn: UnauthorizedHandler | null) {
  unauthorizedHandler = fn;
}

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const res = await fetch(`${API_BASE}${path}`, {
    method,
    credentials: 'include',                       // send/receive session cookie
    headers: body !== undefined ? { 'Content-Type': 'application/json' } : undefined,
    body: body !== undefined ? JSON.stringify(body) : undefined,
  });
  const text = await res.text();
  let data: any = null;
  if (text) {
    try { data = JSON.parse(text); } catch { /* ignore */ }
  }
  if (!res.ok) {
    const msg = data?.error ?? `HTTP ${res.status}`;
    //--- Notify global handler on 401 so the app can transition to login state,
    //--- but only if the request itself isn't one of the auth endpoints (we
    //--- don't want LoginPage's wrong-password to trigger a redirect loop).
    if (res.status === 401 && unauthorizedHandler && !path.startsWith('/api/auth/')) {
      try { unauthorizedHandler(); } catch { /* ignore */ }
    }
    throw new ApiError(res.status, msg, data?.code);
  }
  return data as T;
}

export const api = {
  base: API_BASE || '(same-origin)',
  get:   <T>(p: string)             => request<T>('GET',    p),
  post:  <T>(p: string, body: any)  => request<T>('POST',   p, body),
  put:   <T>(p: string, body: any)  => request<T>('PUT',    p, body),
  patch: <T>(p: string, body: any)  => request<T>('PATCH',  p, body),
  del:   <T>(p: string)             => request<T>('DELETE', p),
};

export const downloadUrl = (path: string) => `${API_BASE}${path}`;
