export function DownloadButtons({ csvUrl, xlsxUrl }: { csvUrl?: string; xlsxUrl?: string }) {
  return (
    <div className="flex gap-2">
      {csvUrl && <a href={csvUrl} className="btn-secondary text-xs" download>Download CSV</a>}
      {xlsxUrl && <a href={xlsxUrl} className="btn-secondary text-xs" download>Download XLSX</a>}
    </div>
  );
}
