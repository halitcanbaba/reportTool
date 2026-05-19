import { api } from './client';
import type { Folder, FolderEntityType } from '../types';

export const FoldersAPI = {
  list:   (entityType: FolderEntityType) =>
    api.get<Folder[]>(`/api/folders?entity_type=${entityType}`),
  create: (input: { entity_type: FolderEntityType; name: string; sort_order?: number }) =>
    api.post<Folder>('/api/folders', input),
  update: (id: number, patch: { name?: string; sort_order?: number; parent_id?: number | null }) =>
    api.patch<Folder>(`/api/folders/${id}`, patch),
  remove: (id: number) =>
    api.del<{ deleted: boolean }>(`/api/folders/${id}`),
  //--- Move an entity row to a folder. folder_id=null → Unfiled.
  //--- When `before` is supplied, the entity is also positioned among the
  //--- target level's mixed siblings (folder + entity intermix supported).
  move:   (entity_type: FolderEntityType, entity_id: number, folder_id: number | null,
           before?: { kind: 'folder' | 'entity'; id: number } | null) =>
    api.patch<{ ok: true }>('/api/folders/move', {
      entity_type, entity_id, folder_id,
      ...(before ? { before_id: before.id, before_kind: before.kind } : {}),
    }),
  //--- Reposition a folder itself: change parent (null = top level) and/or
  //--- place before a sibling (folder or entity; null/undefined = append).
  moveFolder: (id: number, patch: {
    parent_id: number | null;
    before_id?: number | null;
    before_kind?: 'folder' | 'entity';
  }) =>
    api.patch<Folder>(`/api/folders/${id}/move`, patch),
};
