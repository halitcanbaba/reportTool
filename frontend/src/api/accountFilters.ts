import { api } from './client';
import type { AccountFilter, AccountFilterInput, Predicate } from '../types';

export type AccountFilterPreviewRequest = {
  manager_id:   number;
  group_masks:  string[];
  group_regex:  string;
  login_min:    number | null;
  login_max:    number | null;
  user_predicate?: Predicate | null;
};

export type AccountFilterPreviewResult = {
  matched_count: number;
  sample_logins: {
    login: number;
    group: string;
    name: string;
    extra?: Record<string, string>;
  }[];
  sample_groups: string[];
  extra_fields?: string[];
};

export const AccountFiltersAPI = {
  list:    ()                                       => api.get<AccountFilter[]>('/api/account-filters'),
  get:     (id: number)                             => api.get<AccountFilter>(`/api/account-filters/${id}`),
  create:  (input: AccountFilterInput)              => api.post<AccountFilter>('/api/account-filters', input),
  update:  (id: number, input: Partial<AccountFilterInput>) =>
                                                       api.patch<AccountFilter>(`/api/account-filters/${id}`, input),
  remove:  (id: number)                             => api.del<{ deleted: boolean }>(`/api/account-filters/${id}`),
  preview: (input: AccountFilterPreviewRequest)     =>
                                                       api.post<AccountFilterPreviewResult>('/api/account-filters/preview', input),
};
