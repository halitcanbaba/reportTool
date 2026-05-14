import { createContext, useCallback, useContext, useEffect, useState, type ReactNode } from 'react';
import { AuthAPI } from '../api/auth';
import { setUnauthorizedHandler } from '../api/client';
import type { AppUser } from '../types';

type AuthState = {
  user:        AppUser | null;
  loading:     boolean;
  needsSetup:  boolean;
  login:       (username: string, password: string) => Promise<void>;
  logout:      () => Promise<void>;
  setupAdmin:  (username: string, password: string) => Promise<void>;
  refresh:     () => Promise<void>;
};

const AuthContext = createContext<AuthState | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser]             = useState<AppUser | null>(null);
  const [loading, setLoading]       = useState(true);
  const [needsSetup, setNeedsSetup] = useState(false);

  //--- Resolves to (user logged in?, system needs first-admin setup?).
  //--- 401 on /me is normal when no session exists yet.
  const refresh = useCallback(async () => {
    try {
      const status = await AuthAPI.setupStatus();
      if (status.needs_setup) {
        setNeedsSetup(true);
        setUser(null);
        return;
      }
      setNeedsSetup(false);
      try {
        const { user } = await AuthAPI.me();
        setUser(user);
      } catch {
        setUser(null);
      }
    } catch {
      //--- Network/server unavailable: leave loading on so the UI can show
      //--- an error; for now treat as not-logged-in.
      setUser(null);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    //--- Initial check.
    refresh();
    //--- Register the global 401 hook so background requests can transition us
    //--- back to login when the session expires server-side.
    setUnauthorizedHandler(() => { setUser(null); });
    return () => setUnauthorizedHandler(null);
  }, [refresh]);

  const login = async (username: string, password: string) => {
    const { user } = await AuthAPI.login({ username, password });
    setUser(user);
    setNeedsSetup(false);
  };

  const logout = async () => {
    try { await AuthAPI.logout(); } catch { /* ignore — still clear local */ }
    setUser(null);
  };

  const setupAdmin = async (username: string, password: string) => {
    const { user } = await AuthAPI.setup({ username, password });
    setUser(user);
    setNeedsSetup(false);
  };

  return (
    <AuthContext.Provider value={{ user, loading, needsSetup, login, logout, setupAdmin, refresh }}>
      {children}
    </AuthContext.Provider>
  );
}

export function useAuth(): AuthState {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error('useAuth must be used within <AuthProvider>');
  return ctx;
}
