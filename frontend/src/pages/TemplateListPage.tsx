import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { TemplatesAPI } from '../api/templates';
import type { Template } from '../types';
import { fmtDateTime } from '../utils/format';
import { copyName } from '../lib/duplicate';
import { FolderedCard, type FolderedCol } from '../components/FolderedCard';
import { IconTemplate } from '../components/icons';

export function TemplateListPage() {
  const [items, setItems] = useState<Template[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const reload = async () => {
    setLoading(true);
    try { setItems(await TemplatesAPI.list()); setError(null); }
    catch (e: any) { setError(e.message ?? 'failed'); }
    finally { setLoading(false); }
  };
  useEffect(() => { reload(); }, []);

  const onDelete = async (id: number, name: string) => {
    if (!confirm(`Delete template "${name}"? Jobs that reference it will block the delete.`)) return;
    try { await TemplatesAPI.remove(id); reload(); }
    catch (e: any) { alert(e.message ?? 'delete failed'); }
  };

  const onDuplicate = async (t: Template) => {
    try {
      const full = await TemplatesAPI.get(t.id);
      const { id: _id, created_at: _c, updated_at: _u, ...rest } = full;
      void _id; void _c; void _u;
      await TemplatesAPI.create({ ...rest, name: copyName(t.name, items.map(i => i.name)) });
      reload();
    } catch (e: any) {
      alert(e.message ?? 'duplicate failed');
    }
  };

  const columns: FolderedCol<Template>[] = [
    {
      key: 'name', header: 'Name', searchable: true,
      searchValue: t => `${t.name} ${t.description ?? ''}`,
      render: t => (
        <div className="flex items-start gap-2">
          <IconTemplate className="text-ink-500 shrink-0 mt-0.5" />
          <div>
            <div className="font-medium">{t.name}</div>
            {t.description && <div className="text-xs text-ink-500">{t.description}</div>}
          </div>
        </div>
      ),
    },
    {
      key: 'date_params', header: 'Date Params',
      searchValue: t => t.date_params.join(', '),
      render: t => <span className="font-mono text-xs">{t.date_params.join(', ') || <span className="text-ink-400">none</span>}</span>,
    },
    { key: 'cols', header: 'Columns', align: 'right',
      render: t => <span className="tabular-nums">{t.columns.length}</span> },
    { key: 'top_n', header: 'Top N', align: 'right',
      render: t => <span className="tabular-nums">{t.default_top_n}</span> },
    { key: 'updated', header: 'Updated', searchable: true,
      searchValue: t => fmtDateTime(t.updated_at),
      render: t => <span className="text-xs text-ink-500">{fmtDateTime(t.updated_at)}</span> },
  ];

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-semibold text-ink-900">Report Templates</h1>
          <p className="text-sm text-ink-500 mt-1">Design once, run with different date ranges and account filters.</p>
        </div>
        <Link to="/templates/new" className="btn-primary">+ New template</Link>
      </div>

      {error && <div className="card p-4 mb-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}
      {loading && <div className="text-ink-400 text-sm">Loading…</div>}

      {!loading && items.length === 0 && (
        <div className="card p-12 text-center">
          <div className="text-ink-400 mb-4">No templates yet.</div>
          <Link to="/templates/new" className="btn-primary">Design the first template</Link>
        </div>
      )}

      {!loading && items.length > 0 && (
        <FolderedCard<Template>
          entityType="template"
          rows={items}
          rowKey={t => t.id}
          folderIdOf={t => t.folder_id ?? null}
          columns={columns}
          rowActions={t => (
            <>
              <Link to={`/templates/${t.id}/run`} className="btn-primary text-xs px-2 py-1 mr-2">Run</Link>
              <Link to={`/templates/${t.id}/edit`} className="btn-secondary text-xs px-2 py-1 mr-2">Edit</Link>
              <button onClick={() => onDuplicate(t)} className="btn-secondary text-xs px-2 py-1 mr-2">Duplicate</button>
              <button onClick={() => onDelete(t.id, t.name)} className="btn-secondary text-xs px-2 py-1 text-red-600 hover:bg-red-50">Delete</button>
            </>
          )}
        />
      )}
    </div>
  );
}
