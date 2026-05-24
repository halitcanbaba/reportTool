import { api } from './client';
import type { DepositFilter, DepositFilterInput, Predicate } from '../types';

//--- Preview payload: lets the editor preview in-flight buckets without
//--- saving first. Buckets in the body take precedence; the saved filter's
//--- buckets are not auto-loaded.
export type DepositFilterPreviewRequest = {
  manager_id?:        number;
  account_filter_id?: number | null;
  group_masks?:       string[];
  group_regex?:       string;
  login_min?:         number | null;
  login_max?:         number | null;
  date_from:          string;
  date_to:            string;
  buckets:            { key: string; label: string; predicate: Predicate }[];
  offset?:            number;
  limit?:             number;
};

export type DepositFilterPreviewRow = {
  time:            number;
  login:           number;
  action:          number;
  action_label:    string;
  profit:          number;
  comment:         string;
  matched_buckets: string[];
};

export type DepositFilterPreviewResult = {
  total_count: number;
  offset:      number;
  limit:       number;
  rows:        DepositFilterPreviewRow[];
  buckets:     { key: string; label: string; matched_count: number }[];
};

async function previewCsv(input: DepositFilterPreviewRequest): Promise<Blob> {
  const base = (import.meta.env.VITE_API_BASE as string | undefined) ?? '';
  const res = await fetch(`${base}/api/deposit-filters/preview.csv`, {
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

export const DepositFiltersAPI = {
  list:    ()                                       => api.get<DepositFilter[]>('/api/deposit-filters'),
  get:     (id: number)                             => api.get<DepositFilter>(`/api/deposit-filters/${id}`),
  create:  (input: DepositFilterInput)              => api.post<DepositFilter>('/api/deposit-filters', input),
  update:  (id: number, input: Partial<DepositFilterInput>) =>
                                                       api.patch<DepositFilter>(`/api/deposit-filters/${id}`, input),
  remove:  (id: number)                             => api.del<{ deleted: boolean }>(`/api/deposit-filters/${id}`),
  preview: (input: DepositFilterPreviewRequest)     =>
                                                       api.post<DepositFilterPreviewResult>('/api/deposit-filters/preview', input),
  previewCsv,
};
