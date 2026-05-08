export function MonthPicker({ value, onChange }: { value: string; onChange: (v: string) => void }) {
  return (
    <div>
      <label className="label">Month</label>
      <input type="month" className="input" value={value} onChange={e => onChange(e.target.value)} />
    </div>
  );
}
