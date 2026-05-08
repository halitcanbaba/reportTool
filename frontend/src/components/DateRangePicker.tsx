export function DateRangePicker({
  from, to, onFromChange, onToChange,
}: {
  from: string; to: string;
  onFromChange: (v: string) => void;
  onToChange:   (v: string) => void;
}) {
  return (
    <div className="grid grid-cols-2 gap-3">
      <div>
        <label className="label">Date from</label>
        <input type="date" className="input" value={from} onChange={e => onFromChange(e.target.value)} />
      </div>
      <div>
        <label className="label">Date to</label>
        <input type="date" className="input" value={to} onChange={e => onToChange(e.target.value)} />
      </div>
    </div>
  );
}
