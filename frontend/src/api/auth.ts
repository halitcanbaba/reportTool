import { api } from './client';
import type { AppUser, LoginRequest, SetupRequest, SetupStatus } from '../types';

export const AuthAPI = {
  setupStatus:    ()                         => api.get<SetupStatus>('/api/auth/setup-status'),
  setup:          (body: SetupRequest)       => api.post<{ user: AppUser }>('/api/auth/setup', body),
  login:          (body: LoginRequest)       => api.post<{ user: AppUser }>('/api/auth/login', body),
  logout:         ()                         => api.post<{}>('/api/auth/logout', {}),
  me:             ()                         => api.get<{ user: AppUser }>('/api/auth/me'),
  changePassword: (old_password: string, new_password: string) =>
                                                 api.post<{ ok: boolean }>('/api/auth/change-password',
                                                                            { old_password, new_password }),
};
