import { Navigate } from 'react-router-dom';
import type { ReactNode } from 'react';
import { useAuth } from '../contexts/AuthContext';

export function RequireAdmin({ children }: { children: ReactNode }) {
  const { user } = useAuth();
  if (!user) return <Navigate to="/login" replace />;
  if (user.role !== 'admin') {
    return (
      <div className="card p-6 border-amber-200 bg-amber-50 text-amber-900">
        <div className="font-semibold mb-1">Admin access required</div>
        <div className="text-sm">Your account ({user.username} · {user.role}) does not have permission to view this page.</div>
      </div>
    );
  }
  return <>{children}</>;
}
