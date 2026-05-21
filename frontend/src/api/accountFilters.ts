import { api } from './client';
import type { AccountFilter, AccountFilterInput, Predicate } from '../types';

export type AccountFilterPreviewRequest = {
  manager_id:   number;
  group_masks:  string[];
  group_regex:  string;
  login_min:    number | null;
  login_max:    number | null;
  user_predicate?: Predicate | null;
  //--- Pagination. Backend clamps limit to 500; omit both to keep the
  //--- legacy "first 25" sample.
  offset?:      number;
  limit?:       number;
};

export type AccountFilterPreviewResult = {
  matched_count: number;
  offset?:       number;
  limit?:        number;
  sample_logins: {
    login: number;
    group: string;
    name: string;
    extra?: Record<string, string>;
  }[];
  sample_groups: string[];
  extra_fields?: string[];
};

//--- /preview.csv returns text/csv directly. We use a raw fetch instead of
//--- api.post because the response isn't JSON.
async function previewCsv(input: AccountFilterPreviewRequest): Promise<Blob> {
  const base = (import.meta.env.VITE_API_BASE as string | undefined) ?? '';
  const res = await fetch(`${base}/api/account-filters/preview.csv`, {
    method: 'POST',
    credentials: 'include',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(input),
  });
  if (!res.ok) {
    const text = await res.text();
    let msg = `HTTP ${res.status}`;
    try { msg = JSON.parse(text)?.error ?? msg; } catch { /* ignore */ }
    throw new Error(msg);
  }
  return await res.blob();
}

export const AccountFiltersAPI = {
  list:       ()                                       => api.get<AccountFilter[]>('/api/account-filters'),
  get:        (id: number)                             => api.get<AccountFilter>(`/api/account-filters/${id}`),
  create:     (input: AccountFilterInput)              => api.post<AccountFilter>('/api/account-filters', input),
  update:     (id: number, input: Partial<AccountFilterInput>) =>
                                                          api.patch<AccountFilter>(`/api/account-filters/${id}`, input),
  remove:     (id: number)                             => api.del<{ deleted: boolean }>(`/api/account-filters/${id}`),
  preview:    (input: AccountFilterPreviewRequest)     =>
                                                          api.post<AccountFilterPreviewResult>('/api/account-filters/preview', input),
  previewCsv,
};
