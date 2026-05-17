import { api, downloadUrl, ApiError } from './client';
import type { ReportJob, RunReportRequest } from '../types';

//--- Backend response for POST /send-telegram. On success: { ok:true, chat_id }.
export type SendTelegramResult = { ok: true; chat_id: string };

//--- Per-call opts for sendTelegram. `kind` selects the routing on the server
//--- side. For 'document' / 'photo' provide blob + filename + mime; for 'text'
//--- provide a text string.
export type SendTelegramOpts =
  | { kind: 'document'; blob: Blob; filename: string; mime: string; chatId?: string; caption?: string }
  | { kind: 'photo';    blob: Blob; filename: string;                  chatId?: string; caption?: string }
  | { kind: 'text';     text: string;                                   chatId?: string };

export const ReportsAPI = {
  run:        (input: RunReportRequest)  => api.post<{ job_id: number; status: string }>('/api/reports/run', input),
  getJob:     (id: number)               => api.get<ReportJob>(`/api/reports/jobs/${id}`),
  listJobs:   (limit = 50)               => api.get<ReportJob[]>(`/api/reports/jobs?limit=${limit}`),
  removeJob:  (id: number)               => api.del<{ deleted: boolean }>(`/api/reports/jobs/${id}`),
  csvUrl:     (id: number)               => downloadUrl(`/api/reports/jobs/${id}/download.csv`),
  xlsxUrl:    (id: number)               => downloadUrl(`/api/reports/jobs/${id}/download.xlsx`),

  //--- Manual Telegram dispatch. Uses raw fetch because we send multipart and
  //--- the shared `api` wrapper JSON-stringifies everything.
  async sendTelegram(jobId: number, opts: SendTelegramOpts): Promise<SendTelegramResult> {
    const fd = new FormData();
    fd.append('kind', opts.kind);
    if (opts.chatId) fd.append('chat_id', opts.chatId);
    if ('caption' in opts && opts.caption) fd.append('caption', opts.caption);
    if (opts.kind === 'text') {
      fd.append('text', opts.text);
    } else {
      fd.append('file', opts.blob, opts.filename);
    }

    const res = await fetch(downloadUrl(`/api/reports/jobs/${jobId}/send-telegram`), {
      method: 'POST',
      credentials: 'include',
      body: fd,
    });
    const text = await res.text();
    let data: any = null;
    if (text) { try { data = JSON.parse(text); } catch { /* ignore */ } }
    if (!res.ok || !data?.ok) {
      const msg = data?.error ?? `HTTP ${res.status}`;
      throw new ApiError(res.status, msg, data?.code);
    }
    return data as SendTelegramResult;
  },
};
