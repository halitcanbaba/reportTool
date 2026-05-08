import { useEffect, useState, useCallback } from 'react';
import { ManagersAPI } from '../api/managers';
import type { Manager } from '../types';

export function useManagers() {
  const [items, setItems] = useState<Manager[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const reload = useCallback(async () => {
    setLoading(true);
    try { setItems(await ManagersAPI.list()); setError(null); }
    catch (e: any) { setError(e.message ?? 'failed to load managers'); }
    finally { setLoading(false); }
  }, []);

  useEffect(() => { reload(); }, [reload]);

  return { items, loading, error, reload };
}
