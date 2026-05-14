import { Navigate, useLocation } from 'react-router-dom';
import type { ReactNode } from 'react';
import { useAuth } from '../contexts/AuthContext';

export function RequireAuth({ children }: { children: ReactNode }) {
  const { user, loading, needsSetup } = useAuth();
  const location = useLocation();

  if (loading) {
    return (
      <div className="h-screen flex items-center justify-center text-sm text-ink-400">
        Loading…
      </div>
    );
  }
  if (needsSetup) return <Navigate to="/setup" replace />;
  if (!user)      return <Navigate to="/login" replace state={{ from: location.pathname }} />;
  return <>{children}</>;
}
