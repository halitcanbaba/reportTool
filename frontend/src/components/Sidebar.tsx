import { NavLink } from 'react-router-dom';

const items = [
  { to: '/managers', label: 'Managers' },
  { to: '/reports',  label: 'Reports'  },
  { to: '/history',  label: 'History'  },
];

export function Sidebar() {
  return (
    <aside className="w-60 shrink-0 bg-ink-900 text-ink-50 flex flex-col">
      <div className="px-6 py-6 border-b border-ink-800">
        <div className="font-mono text-lg tracking-tight">MT5 ReportTool</div>
        <div className="text-xs text-ink-400 mt-1">v1.0.0</div>
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
      <div className="px-6 py-4 text-xs text-ink-500 border-t border-ink-800">
        backend → {import.meta.env.VITE_API_BASE || '(nginx proxy)'}
      </div>
    </aside>
  );
}
