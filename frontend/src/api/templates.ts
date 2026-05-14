import { api } from './client';
import type { Template, TemplateInput, ValidationResult } from '../types';

export const TemplatesAPI = {
  list:     ()                                       => api.get<Template[]>('/api/templates'),
  get:      (id: number)                             => api.get<Template>(`/api/templates/${id}`),
  create:   (input: TemplateInput)                   => api.post<Template>('/api/templates', input),
  update:   (id: number, input: Partial<TemplateInput>) =>
                                                        api.patch<Template>(`/api/templates/${id}`, input),
  remove:   (id: number)                             => api.del<{ deleted: boolean }>(`/api/templates/${id}`),
  validate: (input: TemplateInput)                   => api.post<ValidationResult>('/api/templates/validate', input),
};
