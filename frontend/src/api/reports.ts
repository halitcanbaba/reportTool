import { api, downloadUrl } from './client';
import type { ReportJob, RunReportRequest } from '../types';

export const ReportsAPI = {
  run:        (input: RunReportRequest)  => api.post<{ job_id: number; status: string }>('/api/reports/run', input),
  getJob:     (id: number)               => api.get<ReportJob>(`/api/reports/jobs/${id}`),
  listJobs:   (limit = 50)               => api.get<ReportJob[]>(`/api/reports/jobs?limit=${limit}`),
  removeJob:  (id: number)               => api.del<{ deleted: boolean }>(`/api/reports/jobs/${id}`),
  csvUrl:     (id: number)               => downloadUrl(`/api/reports/jobs/${id}/download.csv`),
  xlsxUrl:    (id: number)               => downloadUrl(`/api/reports/jobs/${id}/download.xlsx`),
};
