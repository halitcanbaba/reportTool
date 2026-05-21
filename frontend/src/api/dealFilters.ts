import { api } from './client';
import type { DealFilter, DealFilterInput, Predicate } from '../types';

export type DealFilterPreviewRequest = {
  manager_id?:        number;
  account_filter_id?: number | null;
  group_masks?:       string[];
  group_regex?:       string;
  login_min?:         number | null;
  login_max?:         number | null;
  date_from:          string;
  date_to:            string;
  //--- Candidate predicate being designed; rows are tagged matched=true when
  //--- it evaluates true against them. Omit to preview "all cash-flow deals
  //--- in window" without any matching.
  predicate?:         Predicate | null;
  offset?:            number;
  limit?:             number;
};

export type DealFilterPreviewRow = {
  time:         number;       // unix sec UTC
  login:        number;
  action:       number;       // raw IMTDeal enum value
  action_label: string;       // e.g. "DEAL_BALANCE"
  profit:       number;
  comment:      string;
  matched:      boolean;
};

export type DealFilterPreviewResult = {
  total_count:   number;
  matched_count: number;
  offset:        number;
  limit:         number;
  rows:          DealFilterPreviewRow[];
};

//--- /preview.csv returns a text/csv stream; can't go through the JSON client.
async function previewCsv(input: DealFilterPreviewRequest): Promise<Blob> {
  const base = (import.meta.env.VITE_API_BASE as string | undefined) ?? '';
  const res = await fetch(`${base}/api/deal-filters/preview.csv`, {
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

export const DealFiltersAPI = {
  list:       ()                                  => api.get<DealFilter[]>('/api/deal-filters'),
  get:        (id: number)                        => api.get<DealFilter>(`/api/deal-filters/${id}`),
  create:     (input: DealFilterInput)            => api.post<DealFilter>('/api/deal-filters', input),
  update:     (id: number, input: Partial<DealFilterInput>) =>
                                                     api.patch<DealFilter>(`/api/deal-filters/${id}`, input),
  remove:     (id: number)                        => api.del<{ deleted: boolean }>(`/api/deal-filters/${id}`),
  preview:    (input: DealFilterPreviewRequest)   =>
                                                     api.post<DealFilterPreviewResult>('/api/deal-filters/preview', input),
  previewCsv,
};
