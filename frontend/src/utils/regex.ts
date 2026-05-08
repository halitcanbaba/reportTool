export function isValidRegex(pattern: string): { ok: boolean; error?: string } {
  if (!pattern) return { ok: true };
  try { new RegExp(pattern, 'i'); return { ok: true }; }
  catch (e: any) { return { ok: false, error: e.message }; }
}

export function testRegex(pattern: string, sample: string): boolean {
  try { return new RegExp(pattern, 'i').test(sample); }
  catch { return false; }
}
