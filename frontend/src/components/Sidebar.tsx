import { Link, NavLink, useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
//--- Read the build's version from package.json so the sidebar label stays in
//--- sync with the CI's auto-bump (deploy.yml rewrites package.json's version
//--- on every main push, then the next build picks it up here automatically).
import pkg from '../../package.json';

const baseItems = [
  { to: '/managers',        label: 'Managers' },
  { to: '/account-filters', label: 'Account Filters' },
  { to: '/deposit-filters', label: 'Deposit Filters' },
  { to: '/blueprints',      label: 'Blueprints' },
  { to: '/templates',       label: 'Templates' },
  { to: '/ready-made',      label: 'Ready-made' },
  { to: '/schedules',       label: 'Scheduler' },
  { to: '/history',         label: 'History' },
];

export function Sidebar() {
  const { user, logout } = useAuth();
  const nav = useNavigate();

  const items = user?.role === 'admin'
    ? [...baseItems, { to: '/users', label: 'Users' }]
    : baseItems;

  const onLogout = async () => {
    await logout();
    nav('/login', { replace: true });
  };

  return (
    <aside className="w-60 shrink-0 bg-ink-900 text-ink-50 flex flex-col">
      <div className="px-6 py-6 border-b border-ink-800">
        <div className="font-mono text-lg tracking-tight">MT5 ReportTool</div>
        <div className="text-xs text-ink-400 mt-1">v{pkg.version}</div>
      </div>

      <nav className="flex-1 py-4">
        {items.map(it => (
          <NavLink
            key={it.to}
            to={it.to}
            className={({ isActive }) =>
              `block px-6 py-2 text-sm font-medium transition-colors ${
                isActive ? 'bg-ink-800 text-white' : 'text-ink-300 hover:text-white hover:bg-ink-800'
              }`
            }
          >
            {it.label}
          </NavLink>
        ))}
      </nav>

      <div className="px-6 py-4 border-t border-ink-800 space-y-2">
        {user ? (
          <>
            <div className="flex items-center gap-2">
              <div className="font-mono text-sm text-ink-100 truncate" title={user.username}>{user.username}</div>
              <span className={`text-[10px] uppercase tracking-wide px-1.5 py-0.5 rounded border ${
                user.role === 'admin'
                  ? 'border-blue-700 text-blue-200 bg-blue-900/40'
                  : 'border-ink-700 text-ink-300 bg-ink-800/60'
              }`}>{user.role}</span>
            </div>
            <div className="flex items-center gap-3 text-xs">
              <Link to="/account/password" className="text-ink-400 hover:text-white">Change password</Link>
              <button onClick={onLogout} className="text-ink-400 hover:text-white">Logout</button>
            </div>
          </>
        ) : (
          <div className="text-xs text-ink-500">Not signed in</div>
        )}
      </div>
    </aside>
  );
}
