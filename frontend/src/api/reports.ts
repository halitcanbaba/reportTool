import { api, downloadUrl } from './client';
import type { ReportJob } from '../types';

export type TopWinnerInput = {
  manager_id: number;
  date_from: string;     // YYYY-MM-DD
  date_to:   string;
  top_n?:    number;
};

export type SummaryInput = {
  manager_id: number;
  month?:     string;    // YYYY-MM
  date_from?: string;
  date_to?:   string;
};

export const ReportsAPI = {
  runTopWinner: (input: TopWinnerInput) => api.post<{ job_id: number; status: string }>('/api/reports/top-winner', input),
  runSummary:   (input: SummaryInput)   => api.post<{ job_id: number; status: string }>('/api/reports/summary',   input),
  getJob:       (id: number)            => api.get<ReportJob>(`/api/reports/jobs/${id}`),
  listJobs:     (limit = 50)            => api.get<ReportJob[]>(`/api/reports/jobs?limit=${limit}`),
  removeJob:    (id: number)            => api.del<{ deleted: boolean }>(`/api/reports/jobs/${id}`),
  csvUrl:       (id: number)            => downloadUrl(`/api/reports/jobs/${id}/download.csv`),
  xlsxUrl:      (id: number)            => downloadUrl(`/api/reports/jobs/${id}/download.xlsx`),
};
