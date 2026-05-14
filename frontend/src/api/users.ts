import { api } from './client';
import type { AppUser, UserCreateInput, UserPatchInput } from '../types';

export const UsersAPI = {
  list:          ()                                   => api.get<AppUser[]>('/api/users'),
  get:           (id: number)                         => api.get<AppUser>(`/api/users/${id}`),
  create:        (input: UserCreateInput)             => api.post<AppUser>('/api/users', input),
  patch:         (id: number, input: UserPatchInput)  => api.patch<AppUser>(`/api/users/${id}`, input),
  resetPassword: (id: number, new_password: string)   => api.patch<{ ok: boolean }>(`/api/users/${id}/password`, { new_password }),
  remove:        (id: number)                         => api.del<{ deleted: boolean }>(`/api/users/${id}`),
};
