import { api } from './client';
import type { ScheduleEntry, ScheduleEntryInput } from '../types';

export const SchedulesAPI = {
  list:    ()                                                  => api.get<ScheduleEntry[]>('/api/schedules'),
  get:     (id: number)                                        => api.get<ScheduleEntry>(`/api/schedules/${id}`),
  create:  (input: ScheduleEntryInput)                         => api.post<ScheduleEntry>('/api/schedules', input),
  update:  (id: number, input: Partial<ScheduleEntryInput>)    => api.patch<ScheduleEntry>(`/api/schedules/${id}`, input),
  remove:  (id: number)                                        => api.del<{ deleted: boolean }>(`/api/schedules/${id}`),
  runNow:  (id: number)                                        => api.post<{ queued_for: number }>(`/api/schedules/${id}/run-now`, {}),
};
