//+------------------------------------------------------------------+
//| FolderedCard.tsx — tree-table list with in-header search + sort,  |
//| inline folder rename, drag-drop entities between folders. One     |
//| reusable component used by Template/Schedule/Blueprint/Ready-Made/|
//| AccountFilter list pages.                                         |
//+------------------------------------------------------------------+
import { useCallback, useEffect, useMemo, useState, type Dispatch, type ReactNode, type SetStateAction } from 'react';
import { DndContext, DragOverlay, useDraggable, useDroppable, PointerSensor, useSensor, useSensors,
         closestCenter, type DragEndEvent, type DragStartEvent } from '@dnd-kit/core';
import { FoldersAPI } from '../api/folders';
import { useAuth } from '../contexts/AuthContext';
import type { Folder, FolderEntityType } from '../types';
import { IconFolder, IconDuplicate } from './icons';

export type FolderedCol<T> = {
  key: string;
  header: ReactNode;                    // shown as the input placeholder when string
  align?: 'left' | 'right' | 'center';
  width?: string;
  searchable?: boolean;
  searchValue?: (row: T) => string;
  sortable?: boolean;                   // default true
  sortValue?: (row: T) => string | number;
  render: (row: T, idx: number) => ReactNode;
};

type Props<T> = {
  entityType: FolderEntityType;
  rows: T[];
  rowKey: (row: T) => number;
  folderIdOf: (row: T) => number | null;
  onMoved?: () => void;
  columns: FolderedCol<T>[];
  rowClassName?: (row: T) => string;
  rowActions?: (row: T) => ReactNode;
  emptyText?: string;
  //--- Optional: per-page "duplicate this entire folder" handler. When set,
  //--- a copy button appears beside the folder's delete icon. The page knows
  //--- how to clone each entity, so the orchestration lives there.
  onDuplicateFolder?: (folder: Folder, rows: T[]) => Promise<void>;
};

const UNFILED = 'unfiled';
type SortState = { col: string | null; dir: 'asc' | 'desc' | null };

export function FolderedCard<T>(props: Props<T>) {
  const { user } = useAuth();
  const isAdmin = user?.role === 'admin';

  const [folders, setFolders] = useState<Folder[]>([]);
  const [searches, setSearches] = useState<Record<string, string>>({});
  const [sort, setSort] = useState<SortState>({ col: null, dir: null });
  const [collapsed, setCollapsed] = useState<Record<string, boolean>>({});
  const [items, setItems] = useState<T[]>(props.rows);
  useEffect(() => { setItems(props.rows); }, [props.rows]);

  const [creating, setCreating] = useState(false);
  const [newName, setNewName] = useState('');
  const [renameId, setRenameId] = useState<number | null>(null);
  const [renameValue, setRenameValue] = useState('');
  const [duplicatingId, setDuplicatingId] = useState<number | null>(null);

  const sensors = useSensors(useSensor(PointerSensor, { activationConstraint: { distance: 3 } }));
  const [draggingLabel, setDraggingLabel] = useState<string | null>(null);
  //--- When a folder is being dragged, render the per-folder insertion lines
  //--- and the root drop zone. Stays null during entity-row drags so the UI
  //--- doesn't change for the common case.
  const [draggingFolderId, setDraggingFolderId] = useState<number | null>(null);

  const onDragStart = useCallback((e: DragStartEvent) => {
    const aid = String(e.active.id);
    if (aid.startsWith('folder-drag:')) {
      const fid = Number(aid.slice('folder-drag:'.length));
      const f = folders.find(ff => ff.id === fid);
      setDraggingFolderId(fid);
      setDraggingLabel(f ? f.name : 'Folder');
      return;
    }
    if (!aid.startsWith('row:')) return;
    const rowId = Number(aid.slice('row:'.length));
    const r = props.rows.find(x => props.rowKey(x) === rowId);
    if (!r) { setDraggingLabel('Row'); return; }
    //--- Take the first searchable column's value as the drag-overlay label.
    const col = props.columns.find(c => c.searchable) ?? props.columns[0];
    const sv = col?.searchValue ? col.searchValue(r) : stringifyRender(col?.render(r, 0));
    setDraggingLabel(sv || 'Row');
  }, [props.rows, props.rowKey, props.columns, folders]);

  //--- Set of folder ids that are descendants of the currently-dragged folder.
  //--- Used both client-side (don't highlight) and to skip the API call when
  //--- the user drops into a forbidden target.
  const forbiddenDropFolders = useMemo(() => {
    if (draggingFolderId == null) return new Set<number>();
    const desc = new Set<number>([draggingFolderId]);
    let grew = true;
    while (grew) {
      grew = false;
      for (const f of folders) {
        if (f.parent_id != null && desc.has(f.parent_id) && !desc.has(f.id)) {
          desc.add(f.id); grew = true;
        }
      }
    }
    return desc;
  }, [draggingFolderId, folders]);

  const reloadFolders = () =>
    FoldersAPI.list(props.entityType).then(fs => {
      setFolders(fs);
      //--- Folders default to collapsed on first sighting. We only set the
      //--- map for folder ids we haven't seen yet, so a user-toggled state
      //--- survives a folder list refresh (e.g. after create / rename / move).
      setCollapsed(prev => {
        const next = { ...prev };
        let changed = false;
        for (const f of fs) {
          if (!(String(f.id) in next)) { next[String(f.id)] = true; changed = true; }
        }
        return changed ? next : prev;
      });
    }).catch(() => {});

  useEffect(() => { reloadFolders(); /* eslint-disable-next-line react-hooks/exhaustive-deps */ }, [props.entityType]);

  //--- Build a folder tree from the flat list returned by the API. Each node
  //--- carries its entity rows + child folder nodes (children share the parent's
  //--- entity_type, validated server-side). Top-level rows (no folder) render
  //--- ABOVE all folder trees in the table.
  type FolderNode = {
    folder: Folder;
    rows: T[];          // post-search/sort
    raw:  T[];          // pre-filter (used for duplicate + count)
    children: FolderNode[];
  };
  const { topLevelRows, folderTree } = useMemo(() => {
    const byFolder = new Map<number | null, T[]>();
    byFolder.set(null, []);
    for (const f of folders) byFolder.set(f.id, []);
    for (const r of items) {
      const fid = props.folderIdOf(r);
      const bucket = byFolder.get(fid) ?? byFolder.get(null)!;
      bucket.push(r);
    }
    const matches = (r: T) => {
      for (const c of props.columns) {
        if (!c.searchable) continue;
        const q = (searches[c.key] ?? '').trim().toLowerCase();
        if (!q) continue;
        const sv = c.searchValue ? c.searchValue(r) : stringifyRender(c.render(r, 0));
        if (!sv.toLowerCase().includes(q)) return false;
      }
      return true;
    };
    const cmp = (a: T, b: T) => {
      if (!sort.col || !sort.dir) return 0;
      const col = props.columns.find(c => c.key === sort.col);
      if (!col || col.sortable === false) return 0;
      const av = col.sortValue ? col.sortValue(a)
        : col.searchValue ? col.searchValue(a)
        : stringifyRender(col.render(a, 0));
      const bv = col.sortValue ? col.sortValue(b)
        : col.searchValue ? col.searchValue(b)
        : stringifyRender(col.render(b, 0));
      if (typeof av === 'number' && typeof bv === 'number') {
        return sort.dir === 'asc' ? av - bv : bv - av;
      }
      const aS = String(av).toLowerCase();
      const bS = String(bv).toLowerCase();
      const r = aS < bS ? -1 : aS > bS ? 1 : 0;
      return sort.dir === 'asc' ? r : -r;
    };
    const unfiledAll = byFolder.get(null) ?? [];
    const top = [...unfiledAll.filter(matches)].sort(cmp);

    //--- Index children by parent. `folders` is already API-sorted (sort_order,id)
    //--- so child arrays inherit that order.
    const childrenOf = new Map<number | null, Folder[]>();
    childrenOf.set(null, []);
    for (const f of folders) {
      const key = f.parent_id ?? null;
      if (!childrenOf.has(key)) childrenOf.set(key, []);
      childrenOf.get(key)!.push(f);
      if (!childrenOf.has(f.id)) childrenOf.set(f.id, []);
    }
    const buildNode = (f: Folder): FolderNode => {
      const all = byFolder.get(f.id) ?? [];
      return {
        folder: f,
        rows: [...all.filter(matches)].sort(cmp),
        raw: all,
        children: (childrenOf.get(f.id) ?? []).map(buildNode),
      };
    };
    const tree = (childrenOf.get(null) ?? []).map(buildNode);
    return { topLevelRows: top, folderTree: tree };
  }, [folders, items, searches, sort, props]);

  const cycleSort = (key: string) => {
    setSort(s => {
      if (s.col !== key) return { col: key, dir: 'asc' };
      if (s.dir === 'asc') return { col: key, dir: 'desc' };
      return { col: null, dir: null };
    });
  };

  const onDragEnd = async (e: DragEndEvent) => {
    const activeId = String(e.active.id);
    const wasFolderDrag = activeId.startsWith('folder-drag:');
    setDraggingLabel(null);
    setDraggingFolderId(null);
    if (!e.over) return;
    const overId = String(e.over.id);

    //--- Folder repositioning ---------------------------------------
    if (wasFolderDrag) {
      const folderId = Number(activeId.slice('folder-drag:'.length));
      let parent_id: number | null | undefined;
      let before_id: number | null | undefined;

      if (overId === `folder:${UNFILED}` || overId === 'folder-root') {
        parent_id = null;
      } else if (overId.startsWith('folder-into:')) {
        const target = Number(overId.slice('folder-into:'.length));
        if (forbiddenDropFolders.has(target)) return;     // cycle guard
        if (target === folderId) return;
        parent_id = target;
      } else if (overId.startsWith('folder-before:')) {
        const target = Number(overId.slice('folder-before:'.length));
        if (target === folderId) return;
        const targetFolder = folders.find(f => f.id === target);
        if (!targetFolder) return;
        if (forbiddenDropFolders.has(target)) return;
        parent_id = targetFolder.parent_id;
        before_id = target;
      } else {
        return;
      }
      try {
        await FoldersAPI.moveFolder(folderId, { parent_id, before_id });
        reloadFolders();
      } catch (err: any) {
        alert(err?.message ?? 'folder move failed');
      }
      return;
    }

    //--- Entity row move into a folder ------------------------------
    if (!activeId.startsWith('row:')) return;
    const rowId = Number(activeId.slice('row:'.length));

    //--- Resolve target folder from whichever droppable was hovered:
    //---   "folder:<id|unfiled>"   → that folder
    //---   "folder-into:<id>"      → that folder (folder body, same target as entity drop)
    //---   "rowdrop:<rowKey>"      → the hovered row's current folder
    let targetFolderId: number | null = 0 as unknown as null;  // placeholder
    let targetResolved = false;
    if (overId.startsWith('folder:')) {
      const t = overId.slice('folder:'.length);
      targetFolderId = t === UNFILED ? null : Number(t);
      targetResolved = true;
    } else if (overId.startsWith('folder-into:')) {
      targetFolderId = Number(overId.slice('folder-into:'.length));
      targetResolved = true;
    } else if (overId.startsWith('rowdrop:')) {
      const overRowId = Number(overId.slice('rowdrop:'.length));
      if (overRowId !== rowId) {
        const overRow = items.find(r => props.rowKey(r) === overRowId);
        if (overRow) {
          targetFolderId = props.folderIdOf(overRow);
          targetResolved = true;
        }
      }
    }
    if (!targetResolved) return;

    const current = items.find(r => props.rowKey(r) === rowId);
    if (current && props.folderIdOf(current) === targetFolderId) return; // same folder

    const before = items;
    setItems(items.map(r =>
      props.rowKey(r) === rowId ? ({ ...(r as any), folder_id: targetFolderId } as T) : r));
    try {
      await FoldersAPI.move(props.entityType, rowId, targetFolderId);
      reloadFolders();
      props.onMoved?.();
    } catch (err: any) {
      setItems(before);
      alert(err?.message ?? 'move failed');
    }
  };

  const createFolder = async () => {
    const name = newName.trim();
    if (!name) { setCreating(false); return; }
    try {
      await FoldersAPI.create({ entity_type: props.entityType, name });
      setNewName(''); setCreating(false);
      reloadFolders();
    } catch (err: any) {
      alert(err?.message ?? 'create failed');
    }
  };

  const renameFolder = async (id: number) => {
    const name = renameValue.trim();
    setRenameId(null);
    if (!name) return;
    try { await FoldersAPI.update(id, { name }); reloadFolders(); }
    catch (err: any) { alert(err?.message ?? 'rename failed'); }
  };

  const deleteFolder = async (id: number, name: string) => {
    if (!window.confirm(`Delete folder "${name}"? Its items become Unfiled.`)) return;
    try { await FoldersAPI.remove(id); reloadFolders(); }
    catch (err: any) { alert(err?.message ?? 'delete failed'); }
  };

  const colCount = props.columns.length + (props.rowActions ? 1 : 0);

  return (
    <DndContext sensors={sensors}
                collisionDetection={closestCenter}
                onDragStart={onDragStart}
                onDragEnd={onDragEnd}
                onDragCancel={() => setDraggingLabel(null)}>
      <div className="card overflow-hidden">
        {/* Thin toolbar */}
        <div className="flex items-center justify-end p-2 border-b border-ink-100">
          {isAdmin && (creating ? (
            <span className="inline-flex items-center gap-1">
              <input className="input text-xs"
                     style={{ width: 160 }}
                     autoFocus
                     placeholder="folder name"
                     value={newName}
                     onChange={e => setNewName(e.target.value)}
                     onKeyDown={e => { if (e.key === 'Enter') createFolder(); if (e.key === 'Escape') { setCreating(false); setNewName(''); } }} />
              <button type="button" className="text-[11px] text-ink-500 hover:text-ink-900 px-1"
                      onClick={() => { setCreating(false); setNewName(''); }}>esc</button>
            </span>
          ) : (
            <button type="button"
                    className="text-xs px-2 py-1 rounded text-ink-600 hover:text-ink-900 hover:bg-ink-100"
                    onClick={() => setCreating(true)}>+ New folder</button>
          ))}
        </div>

        {/* Table */}
        <div className="overflow-auto">
          <table className="min-w-full text-sm">
            <thead className="bg-ink-50 border-b border-ink-100">
              <tr>
                {props.columns.map(c => (
                  <HeaderCell key={c.key}
                              col={c}
                              search={searches[c.key] ?? ''}
                              onSearchChange={v => setSearches(s => ({ ...s, [c.key]: v }))}
                              sortKey={sort.col}
                              sortDir={sort.dir}
                              onCycleSort={() => cycleSort(c.key)} />
                ))}
                {props.rowActions && <th className="px-3 py-2" />}
              </tr>
            </thead>

            <tbody>
              {/* Top-level (no folder) items — no header, no indent.
                  Drop a foldered row onto a top-level row to move it out of
                  its folder. While ANY drag is active, surface the ungroup /
                  root drop zone so unfile (entity) and un-nest (folder) both
                  have a target. */}
              {topLevelRows.map((row, i) => (
                <EntityRow<T>
                  key={'top:' + props.rowKey(row)}
                  row={row}
                  idx={i}
                  rowKey={props.rowKey}
                  columns={props.columns}
                  rowActions={props.rowActions}
                  rowClassName={props.rowClassName}
                  draggable={isAdmin}
                  topLevel
                />
              ))}
              {isAdmin && draggingLabel != null && (
                <UngroupDropRow colCount={colCount}
                                label={draggingFolderId != null
                                  ? 'Drop here to move folder to top level'
                                  : 'Drop here to remove from folder'} />
              )}

              {/* Folder tree — each top-level node recursively renders its
                  subfolders and entity rows, indented per depth level. */}
              {folderTree.map(node => (
                <FolderSubtree<T>
                  key={node.folder.id}
                  node={node}
                  depth={0}
                  collapsed={collapsed}
                  setCollapsed={setCollapsed}
                  isAdmin={isAdmin}
                  renameId={renameId}
                  renameValue={renameValue}
                  setRenameId={setRenameId}
                  setRenameValue={setRenameValue}
                  renameFolder={renameFolder}
                  deleteFolder={deleteFolder}
                  onDuplicateFolder={props.onDuplicateFolder}
                  duplicatingId={duplicatingId}
                  setDuplicatingId={setDuplicatingId}
                  reloadFolders={reloadFolders}
                  colCount={colCount}
                  columns={props.columns}
                  rowKey={props.rowKey}
                  rowActions={props.rowActions}
                  rowClassName={props.rowClassName}
                  draggingFolderId={draggingFolderId}
                  forbiddenDropFolders={forbiddenDropFolders}
                />
              ))}
              {items.length === 0 && (
                <tr><td className="px-3 py-12 text-center text-ink-400" colSpan={colCount}>
                  {props.emptyText ?? 'No rows.'}
                </td></tr>
              )}
            </tbody>
          </table>
        </div>
      </div>

      {/* Visual feedback while dragging — a small floating pill with the row's
          first searchable column value. Keeps the drag affordance obvious even
          when the source <tr> stays in place. */}
      <DragOverlay>
        {draggingLabel != null && (
          <div className="inline-flex items-center gap-1.5 bg-ink-900 text-white text-xs px-2.5 py-1 rounded shadow-lg">
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor"
                 strokeWidth="2" strokeLinecap="round" aria-hidden>
              <circle cx="9"  cy="6"  r="1" />
              <circle cx="15" cy="6"  r="1" />
              <circle cx="9"  cy="12" r="1" />
              <circle cx="15" cy="12" r="1" />
              <circle cx="9"  cy="18" r="1" />
              <circle cx="15" cy="18" r="1" />
            </svg>
            <span className="truncate" style={{ maxWidth: 240 }}>{draggingLabel}</span>
          </div>
        )}
      </DragOverlay>
    </DndContext>
  );
}

//--- HeaderCell: an input (placeholder = column name) + sort toggle.
//--- Non-searchable columns render the input disabled so layout stays even.
function HeaderCell<T>({ col, search, onSearchChange, sortKey, sortDir, onCycleSort }: {
  col: FolderedCol<T>;
  search: string;
  onSearchChange: (v: string) => void;
  sortKey: string | null;
  sortDir: 'asc' | 'desc' | null;
  onCycleSort: () => void;
}) {
  const placeholder = typeof col.header === 'string' ? col.header : col.key;
  const sortIcon = sortKey === col.key
    ? sortDir === 'asc' ? '↑' : '↓'
    : '⇅';
  const alignClass = col.align === 'right' ? 'text-right'
                   : col.align === 'center' ? 'text-center' : 'text-left';
  const canSort = col.sortable !== false;
  return (
    <th style={col.width ? { width: col.width } : undefined}
        className={`px-2 py-1.5 ${alignClass} font-medium text-ink-600 uppercase text-xs tracking-wide`}>
      <div className={'flex items-center gap-1 ' + (col.align === 'right' ? 'flex-row-reverse' : '')}>
        <input
          className="flex-1 min-w-0 bg-transparent border-0 outline-none px-1 py-0.5 text-[11px] uppercase tracking-wide font-medium text-ink-700 placeholder-ink-500 focus:bg-white focus:rounded"
          placeholder={placeholder}
          value={search}
          disabled={!col.searchable}
          onChange={e => onSearchChange(e.target.value)} />
        {canSort && (
          <button type="button"
                  onClick={onCycleSort}
                  title={sortKey === col.key ? `Sorted ${sortDir}` : 'Sort'}
                  className={
                    'text-[11px] px-1 select-none rounded hover:bg-ink-100 ' +
                    (sortKey === col.key ? 'text-ink-900' : 'text-ink-400')
                  }>
            {sortIcon}
          </button>
        )}
      </div>
    </th>
  );
}

//--- Recursive subtree: folder row + nested subfolders + entity rows. Each
//--- nesting level adds left-padding to the first cell so the tree is visible.
//--- The folder row is itself draggable (admin only) and acts as a drop target
//--- — both for entity rows (move into folder) and for other folders (nest).
type FolderSubtreeProps<T> = {
  node: { folder: Folder; rows: T[]; raw: T[]; children: any[] };
  depth: number;
  collapsed: Record<string, boolean>;
  setCollapsed: Dispatch<SetStateAction<Record<string, boolean>>>;
  isAdmin: boolean;
  renameId: number | null;
  renameValue: string;
  setRenameId: (id: number | null) => void;
  setRenameValue: (v: string) => void;
  renameFolder: (id: number) => void | Promise<void>;
  deleteFolder: (id: number, name: string) => void | Promise<void>;
  onDuplicateFolder?: (folder: Folder, rows: T[]) => Promise<void>;
  duplicatingId: number | null;
  setDuplicatingId: (id: number | null) => void;
  reloadFolders: () => void;
  colCount: number;
  columns: FolderedCol<T>[];
  rowKey: (row: T) => number;
  rowActions?: (row: T) => ReactNode;
  rowClassName?: (row: T) => string;
  draggingFolderId: number | null;
  forbiddenDropFolders: Set<number>;
};
function FolderSubtree<T>(p: FolderSubtreeProps<T>) {
  const f = p.node.folder;
  const folded = !!p.collapsed[String(f.id)];
  const childCount = p.node.children.reduce((n: number, c: any) => n + 1 + countDescendants(c), 0);
  const totalItems = (function totalOf(n: any): number {
    return n.raw.length + n.children.reduce((s: number, c: any) => s + totalOf(c), 0);
  })(p.node);
  const renaming = p.renameId === f.id;
  const renderingDuringFolderDrag = p.draggingFolderId != null;
  const isSelf = p.draggingFolderId === f.id;
  const isForbidden = p.forbiddenDropFolders.has(f.id);

  //--- Draggable handle for the folder row itself (admin only).
  const drag = useDraggable({ id: `folder-drag:${f.id}` });
  //--- Drop target for "nest inside this folder" — reused by entity row drops too.
  const dropInto = useDroppable({ id: `folder-into:${f.id}` });
  //--- Insertion line above the folder (for sibling reorder). Rendered only
  //--- while a folder drag is active to keep the layout calm otherwise.
  const dropBefore = useDroppable({ id: `folder-before:${f.id}` });

  const indentPx = 8 + p.depth * 16;
  const dimWhileDragging = isSelf ? 'opacity-50 ' : '';
  const intoHighlight = (renderingDuringFolderDrag && !isForbidden && dropInto.isOver)
    ? 'ring-1 ring-ink-400 bg-ink-100/70 '
    : (!renderingDuringFolderDrag && dropInto.isOver)
      ? 'ring-1 ring-ink-400 bg-ink-100/70 '
      : '';

  const setRowRef = (el: HTMLTableRowElement | null) => {
    drag.setNodeRef(el);
    dropInto.setNodeRef(el);
  };

  return (
    <>
      {/* Insertion line — only while folder drag is active and target ≠ self */}
      {renderingDuringFolderDrag && !isSelf && !isForbidden && (
        <tr ref={dropBefore.setNodeRef}
            className={
              'h-1 ' +
              (dropBefore.isOver ? 'bg-ink-700' : 'bg-transparent')
            }>
          <td colSpan={p.colCount} className="p-0 leading-none" />
        </tr>
      )}

      {/* Folder header — draggable + droppable */}
      <tr ref={p.isAdmin ? setRowRef : dropInto.setNodeRef}
          {...(p.isAdmin ? drag.attributes : {})}
          {...(p.isAdmin && !renaming ? drag.listeners : {})}
          className={
            'border-b border-ink-100 bg-ink-50 group ' +
            (p.isAdmin ? 'cursor-grab active:cursor-grabbing ' : '') +
            dimWhileDragging + intoHighlight
          }>
        <td colSpan={p.colCount} className="py-1.5" style={{ paddingLeft: indentPx, paddingRight: 8 }}>
          <div className="flex items-center gap-1.5">
            <button type="button"
                    onClick={() => p.setCollapsed(c => ({ ...c, [String(f.id)]: !folded }))}
                    onPointerDown={e => e.stopPropagation()}
                    className="w-5 text-center text-ink-500 hover:text-ink-900 select-none"
                    aria-label={folded ? 'expand' : 'collapse'}>
              {folded ? '▸' : '▾'}
            </button>
            <IconFolder className="text-ink-500 shrink-0" />
            {renaming ? (
              <input className="input text-xs"
                     style={{ width: 220 }}
                     autoFocus
                     onPointerDown={e => e.stopPropagation()}
                     value={p.renameValue}
                     onChange={e => p.setRenameValue(e.target.value)}
                     onKeyDown={e => {
                       if (e.key === 'Enter')  p.renameFolder(f.id);
                       if (e.key === 'Escape') p.setRenameId(null);
                     }}
                     onBlur={() => p.renameFolder(f.id)} />
            ) : (
              <span
                className={'text-sm font-medium text-ink-800 ' + (p.isAdmin ? 'cursor-text hover:underline decoration-dotted underline-offset-2' : '')}
                onPointerDown={e => e.stopPropagation()}
                onClick={p.isAdmin ? () => { p.setRenameId(f.id); p.setRenameValue(f.name); } : undefined}
                title={p.isAdmin ? 'Click to rename · drag to move' : undefined}>
                {f.name}
              </span>
            )}
            <span className="text-xs text-ink-500 ml-1">
              ({totalItems}{childCount > 0 ? ` · ${childCount} folder${childCount === 1 ? '' : 's'}` : ''})
            </span>
            {p.duplicatingId === f.id && (
              <span className="ml-auto text-[11px] text-ink-500 italic">duplicating…</span>
            )}
            {p.isAdmin && p.duplicatingId !== f.id && (
              <span className="ml-auto inline-flex items-center gap-0.5 opacity-0 group-hover:opacity-100"
                    onPointerDown={e => e.stopPropagation()}>
                {p.onDuplicateFolder && (
                  <button type="button"
                          className="p-1 text-ink-500 hover:text-ink-900 hover:bg-ink-200 rounded"
                          title="Duplicate folder + all items"
                          onClick={async () => {
                            p.setDuplicatingId(f.id);
                            try { await p.onDuplicateFolder!(f, p.node.raw); p.reloadFolders(); }
                            catch (err: any) { alert(err?.message ?? 'duplicate failed'); }
                            finally { p.setDuplicatingId(null); }
                          }}>
                    <IconDuplicate />
                  </button>
                )}
                <button type="button"
                        className="p-1 text-ink-400 hover:text-red-600 hover:bg-red-50 rounded text-sm leading-none"
                        title="Delete folder"
                        onClick={() => p.deleteFolder(f.id, f.name)}>×</button>
              </span>
            )}
          </div>
        </td>
      </tr>

      {!folded && p.node.children.map((child: any) => (
        <FolderSubtree<T> key={child.folder.id}
                          {...p}
                          node={child}
                          depth={p.depth + 1} />
      ))}

      {!folded && p.node.rows.map((row: T, i: number) => (
        <EntityRow<T>
          key={p.rowKey(row)}
          row={row}
          idx={i}
          rowKey={p.rowKey}
          columns={p.columns}
          rowActions={p.rowActions}
          rowClassName={p.rowClassName}
          draggable={p.isAdmin}
          indentPx={indentPx + 24}
        />
      ))}
      {!folded && p.node.rows.length === 0 && p.node.children.length === 0 && (
        <tr>
          <td colSpan={p.colCount}
              className="py-3 text-[11px] text-ink-400 italic"
              style={{ paddingLeft: indentPx + 24 }}>
            Drop a row or folder here to add it.
          </td>
        </tr>
      )}
    </>
  );
}

function countDescendants(node: any): number {
  return node.children.reduce((s: number, c: any) => s + 1 + countDescendants(c), 0);
}

//--- Drop target shown above all folders during any drag. For folder drags it
//--- moves the dragged folder to top level; for entity-row drags it unfiles
//--- the row. Both resolve to `folder_id = null` / `parent_id = null`.
function UngroupDropRow({ colCount, label }: { colCount: number; label: string }) {
  const { setNodeRef, isOver } = useDroppable({ id: `folder:${UNFILED}` });
  return (
    <tr ref={setNodeRef}
        className={'border-b border-dashed ' +
                   (isOver ? 'border-ink-500 bg-ink-100/70' : 'border-ink-200 bg-ink-50/40')}>
      <td colSpan={colCount} className="px-3 py-2 text-center text-[11px] text-ink-500 italic">
        {label}
      </td>
    </tr>
  );
}

//--- Single entity row. The first cell gets a left pad so the tree nesting
//--- is visually obvious. Each row is both draggable (source) and droppable
//--- (target) — droppable so a user dropping onto a sibling row counts as a
//--- drop on the parent folder, which makes the drag-drop hit area generous.
function EntityRow<T>({ row, idx, rowKey, columns, rowActions, rowClassName, draggable, topLevel = false, indentPx }: {
  row: T;
  idx: number;
  rowKey: (row: T) => number;
  columns: FolderedCol<T>[];
  rowActions?: (row: T) => ReactNode;
  rowClassName?: (row: T) => string;
  draggable: boolean;
  topLevel?: boolean;            // top-level row (no folder) renders without left indent
  indentPx?: number;             // nested-folder children pass per-depth indent
}) {
  const dragId = `row:${rowKey(row)}`;
  const dropId = `rowdrop:${rowKey(row)}`;
  const { setNodeRef: setDragRef, attributes, listeners, isDragging } = useDraggable({ id: dragId });
  const { setNodeRef: setDropRef } = useDroppable({ id: dropId });
  //--- Combine ref so the same <tr> is both drag source and drop target.
  const setRef = (el: HTMLTableRowElement | null) => { setDragRef(el); setDropRef(el); };
  const extra = rowClassName ? rowClassName(row) : '';
  const firstCellClass = topLevel ? 'px-3' : (indentPx != null ? 'pr-3' : 'pl-8 pr-3');
  const firstCellStyle = indentPx != null && !topLevel ? { paddingLeft: indentPx } : undefined;
  return (
    <tr ref={draggable ? setRef : setDropRef}
        {...(draggable ? attributes : {})}
        {...(draggable ? listeners : {})}
        className={
          'border-b border-ink-50 last:border-0 ' +
          (draggable ? 'cursor-grab active:cursor-grabbing ' : '') +
          (isDragging ? 'opacity-50 ' : '') + extra
        }>
      {columns.map((c, k) => (
        <td key={c.key}
            style={k === 0 ? firstCellStyle : undefined}
            className={`${k === 0 ? firstCellClass : 'px-3'} py-2 text-${c.align ?? 'left'}`}>
          {c.render(row, idx)}
        </td>
      ))}
      {rowActions && (
        //--- onPointerDown stopPropagation keeps action buttons clickable; the
        //--- drag listener (attached to <tr>) would otherwise swallow clicks.
        <td className="px-3 py-2 text-right whitespace-nowrap"
            onPointerDown={e => e.stopPropagation()}>
          {rowActions(row)}
        </td>
      )}
    </tr>
  );
}

//--- Best-effort string coercion for `render()` output when the caller didn't
//--- provide a `searchValue` / `sortValue`. JSX renders into objects; we bail
//--- to empty so the column simply doesn't match for search and sorts equal.
function stringifyRender(v: unknown): string {
  if (v == null) return '';
  if (typeof v === 'string') return v;
  if (typeof v === 'number' || typeof v === 'boolean') return String(v);
  return '';
}
