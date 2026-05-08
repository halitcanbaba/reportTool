import { api } from './client';
import type { Manager, ManagerInput } from '../types';

export const ManagersAPI = {
  list:    ()                                       => api.get<Manager[]>('/api/managers'),
  get:     (id: number)                             => api.get<Manager>(`/api/managers/${id}`),
  create:  (input: ManagerInput)                    => api.post<Manager>('/api/managers', input),
  update:  (id: number, input: Partial<ManagerInput>)=> api.patch<Manager>(`/api/managers/${id}`, input),
  remove:  (id: number)                             => api.del<{ deleted: boolean }>(`/api/managers/${id}`),
  test:    (id: number)                             => api.post<{ connected: boolean; users_sample?: number; error?: string }>(`/api/managers/${id}/test`, {}),
};
