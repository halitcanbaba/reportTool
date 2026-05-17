import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { AccountFiltersAPI } from '../api/accountFilters';
import type { AccountFilter } from '../types';
import { fmtDateTime } from '../utils/format';
import { collectPredicateFields } from '../lib/blueprintInsert';
import { copyName } from '../lib/duplicate';
import { FolderedCard, type FolderedCol } from '../components/FolderedCard';
import { IconFilter } from '../components/icons';

export function AccountFilterListPage() {
  const [items, setItems] = useState<AccountFilter[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const reload = async () => {
    setLoading(true);
    try { setItems(await AccountFiltersAPI.list()); setError(null); }
    catch (e: any) { setError(e.message ?? 'failed'); }
    finally { setLoading(false); }
  };
  useEffect(() => { reload(); }, []);

  const onDelete = async (id: number, name: string) => {
    if (!confirm(`Delete account filter "${name}"?`)) return;
    await AccountFiltersAPI.remove(id);
    reload();
  };

  const onDuplicate = async (f: AccountFilter) => {
    try {
      const full = await AccountFiltersAPI.get(f.id);
      const { id: _id, created_at: _c, updated_at: _u, ...rest } = full;
      void _id; void _c; void _u;
      await AccountFiltersAPI.create({ ...rest, name: copyName(f.name, items.map(i => i.name)) });
      reload();
    } catch (e: any) {
      alert(e.message ?? 'duplicate failed');
    }
  };

  const columns: FolderedCol<AccountFilter>[] = [
    { key: 'name', header: 'Name', searchable: true,
      searchValue: f => `${f.name} ${f.description ?? ''}`,
      render: f => (
        <span className="inline-flex items-center gap-2">
          <IconFilter className="text-ink-500 shrink-0" />
          <span className="font-medium">{f.name}</span>
        </span>
      ) },
    { key: 'masks', header: 'Group Masks', searchable: true,
      searchValue: f => f.group_masks.join(', '),
      render: f => <span className="font-mono text-xs">{f.group_masks.join(', ') || <span className="text-ink-400">—</span>}</span> },
    { key: 'regex', header: 'Regex',
      searchValue: f => f.group_regex,
      render: f => <span className="font-mono text-xs">{f.group_regex || <span className="text-ink-400">—</span>}</span> },
    { key: 'login_range', header: 'Login range', align: 'right',
      searchValue: f => `${f.login_min ?? ''} ${f.login_max ?? ''}`,
      render: f => <span className="font-mono text-xs tabular-nums">{f.login_min ?? '—'} … {f.login_max ?? '—'}</span> },
    { key: 'extras', header: 'Extra filters',
      searchValue: f => collectPredicateFields(f.user_predicate ?? null).join(' '),
      render: f => {
        const extras = collectPredicateFields(f.user_predicate ?? null);
        return extras.length === 0
          ? <span className="text-ink-400 text-xs">—</span>
          : (
            <span className="flex flex-wrap gap-1">
              {extras.map(name => (
                <span key={name}
                      className="inline-block px-1.5 py-0.5 rounded bg-emerald-50 text-emerald-800 border border-emerald-200 font-mono text-[11px]">
                  {name}
                </span>
              ))}
            </span>
          );
      } },
    { key: 'updated', header: 'Updated',
      searchValue: f => fmtDateTime(f.updated_at),
      render: f => <span className="text-xs text-ink-500">{fmtDateTime(f.updated_at)}</span> },
  ];

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-semibold text-ink-900">Account Filters</h1>
          <p className="text-sm text-ink-500 mt-1">Reusable filter presets for group masks, regex, login range.</p>
        </div>
        <Link to="/account-filters/new" className="btn-primary">+ New filter</Link>
      </div>

      {error && <div className="card p-4 mb-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}
      {loading && <div className="text-ink-400 text-sm">Loading…</div>}

      {!loading && items.length === 0 && (
        <div className="card p-12 text-center">
          <div className="text-ink-400 mb-4">No account filters yet.</div>
          <Link to="/account-filters/new" className="btn-primary">Add the first filter</Link>
        </div>
      )}

      {!loading && items.length > 0 && (
        <FolderedCard<AccountFilter>
          entityType="account_filter"
          rows={items}
          rowKey={f => f.id}
          folderIdOf={f => f.folder_id ?? null}
          columns={columns}
          rowActions={f => (
            <>
              <Link to={`/account-filters/${f.id}/edit`} className="btn-secondary text-xs px-2 py-1 mr-2">Edit</Link>
              <button onClick={() => onDuplicate(f)} className="btn-secondary text-xs px-2 py-1 mr-2">Duplicate</button>
              <button onClick={() => onDelete(f.id, f.name)} className="btn-secondary text-xs px-2 py-1 text-red-600 hover:bg-red-50">Delete</button>
            </>
          )}
        />
      )}
    </div>
  );
}
