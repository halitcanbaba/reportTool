import { api } from './client';
import type { ReadyMadeReport, ReadyMadeReportInput, ReadyMadeRunRequest } from '../types';

export const ReadyMadeAPI = {
  list:   ()                                                   => api.get<ReadyMadeReport[]>('/api/ready-made'),
  get:    (id: number)                                         => api.get<ReadyMadeReport>(`/api/ready-made/${id}`),
  create: (input: ReadyMadeReportInput)                        => api.post<ReadyMadeReport>('/api/ready-made', input),
  update: (id: number, input: Partial<ReadyMadeReportInput>)   => api.patch<ReadyMadeReport>(`/api/ready-made/${id}`, input),
  remove: (id: number)                                         => api.del<{ deleted: boolean }>(`/api/ready-made/${id}`),
  run:    (id: number, override?: ReadyMadeRunRequest)         =>
                                                                   api.post<{ job_id: number; status: string }>(`/api/ready-made/${id}/run`, override ?? {}),
};
