import { api } from './client';
import type { TelegramSettings, TelegramSettingsInput } from '../types';

export const SettingsAPI = {
  telegramGet:  ()                              => api.get<TelegramSettings>('/api/settings/telegram'),
  telegramPut:  (input: TelegramSettingsInput)  => api.put<{ ok: boolean }>('/api/settings/telegram', input),
  telegramTest: (chat_id?: string)              => api.post<{ ok: boolean; chat_id: string }>(
                                                       '/api/settings/telegram/test',
                                                       chat_id ? { chat_id } : {}),
};
