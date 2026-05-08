import type { JobStatus } from '../types';

const cls: Record<JobStatus, string> = {
  queued:    'bg-ink-100 text-ink-700',
  running:   'bg-blue-100 text-blue-800',
  completed: 'bg-emerald-100 text-emerald-800',
  failed:    'bg-red-100 text-red-800',
};

export function StatusPill({ status }: { status: JobStatus }) {
  return <span className={`pill ${cls[status]}`}>{status}</span>;
}
