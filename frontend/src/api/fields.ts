import { api } from './client';
import type { FieldCatalog } from '../types';

let _cache: FieldCatalog | null = null;

export const FieldsAPI = {
  catalog: async (): Promise<FieldCatalog> => {
    if (_cache) return _cache;
    _cache = await api.get<FieldCatalog>('/api/reports/fields');
    return _cache;
  },
  invalidate: () => { _cache = null; },
};
