import { useEffect, useState } from 'react';
import { AccountFiltersAPI } from '../api/accountFilters';
import type { AccountFilter } from '../types';

type Props = {
  value: number | null;
  managerId?: number | null;     // when provided, prioritizes filters bound to this manager
  onChange: (id: number | null) => void;
};

export function AccountFilterPicker({ value, managerId, onChange }: Props) {
  const [items, setItems] = useState<AccountFilter[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    AccountFiltersAPI.list().then(rs => {
      const generic = rs.filter(f => !f.manager_id);
      const bound = rs.filter(f => managerId != null && f.manager_id === managerId);
      setItems([...bound, ...generic]);
      setLoading(false);
    });
  }, [managerId]);

  if (loading) return <select className="input" disabled><option>Loading…</option></select>;

  return (
    <select className="input"
            value={value ?? ''}
            onChange={e => onChange(e.target.value ? Number(e.target.value) : null)}>
      <option value="">— manager defaults —</option>
      {items.map(f => (
        <option key={f.id} value={f.id}>
          {f.name}{f.manager_id ? ' (bound)' : ''}
        </option>
      ))}
    </select>
  );
}
