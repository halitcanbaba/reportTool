import { useEffect, useState } from 'react';
import { ReportsAPI } from '../api/reports';
import type { ReportJob } from '../types';

export function useReportJob(id: number | null) {
  const [job, setJob] = useState<ReportJob | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (id == null) return;
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | null = null;

    const tick = async () => {
      try {
        const j = await ReportsAPI.getJob(id);
        if (cancelled) return;
        setJob(j);
        if (j.status === 'queued' || j.status === 'running') {
          timer = setTimeout(tick, 2000);
        }
      } catch (e: any) {
        if (!cancelled) setError(e.message ?? 'job poll failed');
      }
    };

    tick();
    return () => { cancelled = true; if (timer) clearTimeout(timer); };
  }, [id]);

  return { job, error };
}
