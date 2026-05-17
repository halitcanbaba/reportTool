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
