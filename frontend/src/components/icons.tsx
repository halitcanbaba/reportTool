//+------------------------------------------------------------------+
//| icons.tsx — tiny monochrome SVG icon set used on list pages and    |
//| folder tree headers. All icons use `currentColor` so the caller    |
//| can tint via Tailwind text-* classes (e.g. text-ink-500).          |
//+------------------------------------------------------------------+
import type { SVGProps } from 'react';

type IconProps = SVGProps<SVGSVGElement> & { size?: number };

function base({ size = 16, ...rest }: IconProps) {
  return {
    width: size, height: size,
    viewBox: '0 0 24 24',
    fill: 'none',
    stroke: 'currentColor',
    strokeWidth: 1.6,
    strokeLinecap: 'round' as const,
    strokeLinejoin: 'round' as const,
    'aria-hidden': true,
    ...rest,
  };
}

export function IconFolder(p: IconProps) {
  return (
    <svg {...base(p)}>
      <path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7z" />
    </svg>
  );
}

//--- Document with two body lines — read as "report / template".
export function IconTemplate(p: IconProps) {
  return (
    <svg {...base(p)}>
      <path d="M7 3h7l4 4v13a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z" />
      <path d="M14 3v4h4" />
      <path d="M9 13h6M9 17h4" />
    </svg>
  );
}

//--- Stacked rectangles — read as "block / building piece".
export function IconBlueprint(p: IconProps) {
  return (
    <svg {...base(p)}>
      <rect x="3" y="3"  width="8" height="8" rx="1" />
      <rect x="13" y="3" width="8" height="5" rx="1" />
      <rect x="13" y="10" width="8" height="11" rx="1" />
      <rect x="3"  y="13" width="8" height="8" rx="1" />
    </svg>
  );
}

//--- Clock — schedule / cron.
export function IconSchedule(p: IconProps) {
  return (
    <svg {...base(p)}>
      <circle cx="12" cy="12" r="9" />
      <path d="M12 7v5l3 2" />
    </svg>
  );
}

//--- Box — pre-packaged ready-made report.
export function IconReadyMade(p: IconProps) {
  return (
    <svg {...base(p)}>
      <path d="M3 7l9-4 9 4-9 4-9-4z" />
      <path d="M3 7v10l9 4 9-4V7" />
      <path d="M12 11v10" />
    </svg>
  );
}

//--- Funnel — filter.
export function IconFilter(p: IconProps) {
  return (
    <svg {...base(p)}>
      <path d="M3 5h18l-7 9v6l-4-2v-4z" />
    </svg>
  );
}

//--- Action icons used in row action columns ----------------------

//--- Play triangle — Run.
export function IconPlay(p: IconProps) {
  return (
    <svg {...base(p)} fill="currentColor" stroke="none">
      <path d="M7 4.5v15l13-7.5z" />
    </svg>
  );
}

//--- Triangle + gear — Run with overrides.
export function IconRunWith(p: IconProps) {
  return (
    <svg {...base(p)}>
      <path d="M5 4.5v15l11-7.5z" fill="currentColor" stroke="none" />
      <circle cx="19" cy="19" r="3" />
      <path d="M19 16.5v-1M19 22.5v-1M16.5 19h-1M22.5 19h-1" />
    </svg>
  );
}

//--- Pencil — Edit.
export function IconEdit(p: IconProps) {
  return (
    <svg {...base(p)}>
      <path d="M4 20h4l11-11-4-4L4 16z" />
      <path d="M14 5l4 4" />
    </svg>
  );
}

//--- Two overlapping rectangles — Duplicate / Copy.
export function IconDuplicate(p: IconProps) {
  return (
    <svg {...base(p)}>
      <rect x="8"  y="8"  width="12" height="12" rx="1.5" />
      <path d="M4 16V5a1 1 0 0 1 1-1h11" />
    </svg>
  );
}

//--- Same shape, exposed under the "copy" name for folder-copy callers.
export const IconCopy = IconDuplicate;

//--- Trash can — Delete.
export function IconDelete(p: IconProps) {
  return (
    <svg {...base(p)}>
      <path d="M4 7h16" />
      <path d="M9 7V5a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v2" />
      <path d="M6 7l1 12a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-12" />
      <path d="M10 11v6M14 11v6" />
    </svg>
  );
}

//+------------------------------------------------------------------+
//| Small square icon button used in list-page row actions.          |
//| Tooltip via native `title` attribute on hover.                   |
//+------------------------------------------------------------------+
import type { ButtonHTMLAttributes, ReactNode } from 'react';

export function IconButton({ title, danger = false, className = '', children, ...rest }: {
  title: string;
  danger?: boolean;
  className?: string;
  children: ReactNode;
} & Omit<ButtonHTMLAttributes<HTMLButtonElement>, 'title' | 'children'>) {
  const base =
    'inline-flex items-center justify-center w-7 h-7 rounded transition-colors ' +
    'text-ink-600 hover:bg-ink-100 disabled:opacity-40 disabled:cursor-not-allowed ';
  const tone = danger ? 'hover:text-red-600 hover:bg-red-50 ' : 'hover:text-ink-900 ';
  return (
    <button type="button" title={title} aria-label={title}
            className={base + tone + className}
            {...rest}>
      {children}
    </button>
  );
}
