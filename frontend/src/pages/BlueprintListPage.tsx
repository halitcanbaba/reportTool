import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { BlueprintsAPI } from '../api/blueprints';
import { astToText } from '../lib/exprChips';
import { fmtDateTime } from '../utils/format';
import { copyName } from '../lib/duplicate';
import type { FormulaBlueprint } from '../types';
import { useNavigate } from 'react-router-dom';
import { FolderedCard, type FolderedCol } from '../components/FolderedCard';
import { IconButton, IconBlueprint, IconEdit, IconDuplicate, IconDelete } from '../components/icons';
import { FoldersAPI } from '../api/folders';
import type { Folder } from '../types';

function safePreview(b: FormulaBlueprint): string {
  try { return b.expr ? astToText(b.expr) : ''; }
  catch { return '(unrenderable)'; }
}

export function BlueprintListPage() {
  const nav = useNavigate();
  const [items, setItems] = useState<FormulaBlueprint[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const reload = async () => {
    setLoading(true);
    try { setItems(await BlueprintsAPI.list()); setError(null); }
    catch (e: any) { setError(e.message ?? 'failed'); }
    finally { setLoading(false); }
  };
  useEffect(() => { reload(); }, []);

  const onDelete = async (id: number, name: string) => {
    if (!confirm(`Delete blueprint "${name}"?`)) return;
    await BlueprintsAPI.remove(id);
    reload();
  };

  const onDuplicate = async (b: FormulaBlueprint) => {
    try {
      const full = await BlueprintsAPI.get(b.id);
      const { id: _id, created_at: _c, updated_at: _u, ...rest } = full;
      void _id; void _c; void _u;
      await BlueprintsAPI.create({ ...rest, name: copyName(b.name, items.map(i => i.name)) });
      reload();
    } catch (e: any) {
      alert(e.message ?? 'duplicate failed');
    }
  };

  const duplicateFolder = async (folder: Folder, rows: FormulaBlueprint[]) => {
    const folders = await FoldersAPI.list('blueprint');
    const dup = await FoldersAPI.create({
      entity_type: 'blueprint',
      name: copyName(folder.name, folders.map(f => f.name)),
    });
    const existing = items.map(i => i.name);
    for (const r of rows) {
      const full = await BlueprintsAPI.get(r.id);
      const { id: _id, created_at: _c, updated_at: _u, folder_id: _f, ...rest } = full as any;
      void _id; void _c; void _u; void _f;
      const created = await BlueprintsAPI.create({ ...rest, name: copyName(r.name, existing) });
      await FoldersAPI.move('blueprint', (created as any).id, dup.id);
      existing.push((created as any).name ?? '');
    }
    reload();
  };

  const columns: FolderedCol<FormulaBlueprint>[] = [
    {
      key: 'name', header: 'Name', searchable: true,
      searchValue: b => `${b.name} ${b.description ?? ''}`,
      render: b => (
        <div className="flex items-start gap-2">
          <IconBlueprint className="text-ink-500 shrink-0 mt-0.5" />
          <div>
            <div className="font-medium">{b.name}</div>
            {b.description && <div className="text-xs text-ink-500">{b.description}</div>}
          </div>
        </div>
      ),
    },
    {
      key: 'date_params', header: 'Date params',
      searchValue: b => b.date_params.join(', '),
      render: b => <span className="font-mono text-xs">{b.date_params.join(', ') || <span className="text-ink-400">—</span>}</span>,
    },
    {
      key: 'formula', header: 'Formula', searchable: true,
      searchValue: b => safePreview(b),
      render: b => {
        const preview = safePreview(b);
        return (
          <div className="font-mono text-xs text-ink-700 truncate" style={{ maxWidth: 400 }} title={preview}>
            {preview}
          </div>
        );
      },
    },
    {
      key: 'updated', header: 'Updated',
      searchValue: b => fmtDateTime(b.updated_at),
      render: b => <span className="text-xs text-ink-500">{fmtDateTime(b.updated_at)}</span>,
    },
  ];

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-semibold text-ink-900">Formula Blueprints</h1>
          <p className="text-sm text-ink-500 mt-1">Reusable formula building blocks for fast template design.</p>
        </div>
        <Link to="/blueprints/new" className="btn-primary">+ New blueprint</Link>
      </div>

      {error && <div className="card p-4 mb-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}
      {loading && <div className="text-ink-400 text-sm">Loading…</div>}

      {!loading && items.length === 0 && (
        <div className="card p-12 text-center">
          <div className="text-ink-400 mb-4">No blueprints yet.</div>
          <Link to="/blueprints/new" className="btn-primary">Save your first blueprint</Link>
        </div>
      )}

      {!loading && items.length > 0 && (
        <FolderedCard<FormulaBlueprint>
          entityType="blueprint"
          rows={items}
          rowKey={b => b.id}
          folderIdOf={b => b.folder_id ?? null}
          columns={columns}
          onDuplicateFolder={duplicateFolder}
          rowActions={b => (
            <span className="inline-flex items-center gap-0.5">
              <IconButton title="Edit"      onClick={() => nav(`/blueprints/${b.id}/edit`)}><IconEdit /></IconButton>
              <IconButton title="Duplicate" onClick={() => onDuplicate(b)}><IconDuplicate /></IconButton>
              <IconButton title="Delete"    danger onClick={() => onDelete(b.id, b.name)}><IconDelete /></IconButton>
            </span>
          )}
        />
      )}
    </div>
  );
}
