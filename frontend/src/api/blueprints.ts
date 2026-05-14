import { api } from './client';
import type { FormulaBlueprint, FormulaBlueprintInput } from '../types';

export const BlueprintsAPI = {
  list:   ()                                                 => api.get<FormulaBlueprint[]>('/api/blueprints'),
  get:    (id: number)                                       => api.get<FormulaBlueprint>(`/api/blueprints/${id}`),
  create: (input: FormulaBlueprintInput)                     => api.post<FormulaBlueprint>('/api/blueprints', input),
  update: (id: number, input: Partial<FormulaBlueprintInput>)=> api.patch<FormulaBlueprint>(`/api/blueprints/${id}`, input),
  remove: (id: number)                                       => api.del<{ deleted: boolean }>(`/api/blueprints/${id}`),
};
