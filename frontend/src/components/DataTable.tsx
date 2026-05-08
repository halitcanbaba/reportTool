import { ReactNode } from 'react';

export type Column<T> = {
  key: string;
  header: ReactNode;
  align?: 'left' | 'right' | 'center';
  width?: string;
  render: (row: T, idx: number) => ReactNode;
};

export function DataTable<T>({ columns, rows, emptyText = 'No data' }: {
  columns: Column<T>[];
  rows: T[];
  emptyText?: string;
}) {
  return (
    <div className="card overflow-hidden">
      <div className="overflow-x-auto">
        <table className="min-w-full text-sm">
          <thead className="bg-ink-50 border-b border-ink-100 sticky top-0">
            <tr>
              {columns.map(c => (
                <th
                  key={c.key}
                  style={{ width: c.width }}
                  className={`px-4 py-3 font-medium text-ink-600 uppercase text-xs tracking-wide ${
                    c.align === 'right'  ? 'text-right'  :
                    c.align === 'center' ? 'text-center' : 'text-left'
                  }`}
                >
                  {c.header}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {rows.length === 0 ? (
              <tr><td colSpan={columns.length} className="px-4 py-12 text-center text-ink-400">{emptyText}</td></tr>
            ) : rows.map((row, idx) => (
              <tr key={idx} className="border-b border-ink-50 last:border-0 hover:bg-ink-50/50">
                {columns.map(c => (
                  <td
                    key={c.key}
                    className={`px-4 py-3 ${
                      c.align === 'right'  ? 'text-right tabular-nums' :
                      c.align === 'center' ? 'text-center'             : 'text-left'
                    }`}
                  >
                    {c.render(row, idx)}
                  </td>
                ))}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
