import { Fragment } from 'react';
import { Link } from 'react-router-dom';

export type Crumb = {
  label: string;
  to?: string;       // undefined → render as the active (last) crumb
};

//--- Path-style navigation rendered above page titles. Each crumb except
//--- the last is a Link back to its section root; the last crumb is the
//--- current page (non-clickable, emphasized).
export function Breadcrumbs({ items }: { items: Crumb[] }) {
  return (
    <nav className="flex items-center gap-1.5 text-xs mb-2" aria-label="Breadcrumb">
      {items.map((c, i) => {
        const isLast = i === items.length - 1;
        return (
          <Fragment key={i}>
            {i > 0 && <span className="text-ink-300 select-none">/</span>}
            {c.to && !isLast ? (
              <Link
                to={c.to}
                className="text-ink-500 hover:text-ink-900 hover:underline transition-colors"
              >
                {c.label}
              </Link>
            ) : (
              <span className={isLast ? 'text-ink-700 font-medium' : 'text-ink-500'}>
                {c.label}
              </span>
            )}
          </Fragment>
        );
      })}
    </nav>
  );
}
