// Empty API_BASE = same-origin (nginx reverse-proxies /api/* to Windows backend).
// Set VITE_API_BASE=http://host:5151 only if you want the browser to call
// the backend directly (requires backend CORS + bypass of Chrome PNA).
const API_BASE = (import.meta.env.VITE_API_BASE as string | undefined) ?? '';

export class ApiError extends Error {
  constructor(public status: number, message: string, public code?: string) {
    super(message);
  }
}

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const res = await fetch(`${API_BASE}${path}`, {
    method,
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
