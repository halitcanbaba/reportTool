import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { TemplatesAPI } from '../api/templates';
import type { Template } from '../types';
import { fmtDateTime } from '../utils/format';
import { copyName } from '../lib/duplicate';
import { FolderedCard, type FolderedCol } from '../components/FolderedCard';
import { IconButton, IconTemplate, IconPlay, IconEdit, IconDuplicate, IconDelete } from '../components/icons';
import { FoldersAPI } from '../api/folders';
import type { Folder } from '../types';
import { useNavigate } from 'react-router-dom';

export function TemplateListPage() {
  const nav = useNavigate();
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

  //--- Duplicate every item inside a folder into a brand-new folder copy.
  //--- Sequential to keep error paths tractable; runtime is fine for the
  //--- expected handful-to-few-dozen items per folder.
  const duplicateFolder = async (folder: Folder, rows: Template[]) => {
    const folders = await FoldersAPI.list('template');
    const dup = await FoldersAPI.create({
      entity_type: 'template',
      name: copyName(folder.name, folders.map(f => f.name)),
    });
    const existingNames = items.map(i => i.name);
    for (const r of rows) {
      const full = await TemplatesAPI.get(r.id);
      const { id: _id, created_at: _c, updated_at: _u, folder_id: _f, ...rest } = full as any;
      void _id; void _c; void _u; void _f;
      const created = await TemplatesAPI.create({ ...rest, name: copyName(r.name, existingNames) });
      await FoldersAPI.move('template', (created as any).id, dup.id);
      existingNames.push((created as any).name ?? '');
    }
    reload();
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
      sortValue: t => t.default_top_n,
      render: t => <InlineNumberCell value={t.default_top_n}
                                     onSave={async (v) => {
                                       const full = await TemplatesAPI.get(t.id);
                                       const { id: _id, created_at: _c, updated_at: _u, ...rest } = full;
                                       void _id; void _c; void _u;
                                       await TemplatesAPI.update(t.id, { ...rest, default_top_n: Math.max(0, v) });
                                       reload();
                                     }} /> },
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
          onMoved={reload}
          columns={columns}
          onDuplicateFolder={duplicateFolder}
          rowActions={t => (
            <span className="inline-flex items-center gap-0.5">
              <IconButton title="Run"       onClick={() => nav(`/templates/${t.id}/run`)}><IconPlay /></IconButton>
              <IconButton title="Edit"      onClick={() => nav(`/templates/${t.id}/edit`)}><IconEdit /></IconButton>
              <IconButton title="Duplicate" onClick={() => onDuplicate(t)}><IconDuplicate /></IconButton>
              <IconButton title="Delete"    danger onClick={() => onDelete(t.id, t.name)}><IconDelete /></IconButton>
            </span>
          )}
        />
      )}
    </div>
  );
}

//--- Tiny inline numeric cell. Reads as `tabular-nums` text; on click becomes
//--- an <input>; commits on blur/Enter. Drag listener on the row is suppressed
//--- by stopPropagation so editing doesn't kick off a drag.
function InlineNumberCell({ value, onSave }: { value: number; onSave: (v: number) => Promise<void> }) {
  const [editing, setEditing] = useState(false);
  const [text, setText] = useState(String(value));
  const commit = async () => {
    setEditing(false);
    const n = Math.max(0, Math.floor(Number(text) || 0));
    if (n === value) return;
    try { await onSave(n); }
    catch (e: any) { alert(e?.message ?? 'save failed'); setText(String(value)); }
  };
  if (!editing) {
    return (
      <span className="tabular-nums cursor-text hover:bg-ink-50 rounded px-1 -mx-1"
            title="Click to edit"
            onPointerDown={e => e.stopPropagation()}
            onClick={() => { setText(String(value)); setEditing(true); }}>
        {value}
      </span>
    );
  }
  return (
    <input className="w-20 text-right text-xs border border-ink-300 rounded px-1 py-0.5 tabular-nums"
           type="number" min={0} autoFocus
           value={text}
           onPointerDown={e => e.stopPropagation()}
           onChange={e => setText(e.target.value)}
           onBlur={commit}
           onKeyDown={e => {
             if (e.key === 'Enter')  (e.target as HTMLInputElement).blur();
             if (e.key === 'Escape') { setEditing(false); setText(String(value)); }
           }} />
  );
}
