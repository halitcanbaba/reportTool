//--- Build a unique "Copy of …" name that doesn't collide with existing rows.
//--- "X" → "X (copy)" → "X (copy 2)" → "X (copy 3)" …
export function copyName(base: string, existing: Iterable<string>): string {
  const set = new Set(existing);
  const candidate = `${base} (copy)`;
  if (!set.has(candidate)) return candidate;
  let n = 2;
  while (set.has(`${base} (copy ${n})`)) n++;
  return `${base} (copy ${n})`;
}
