import type { FieldCatalog, FieldDef } from '../types';
import { FieldsAPI } from '../api/fields';
import { DepositFiltersAPI } from '../api/depositFilters';

//--- Names of the bucket-aware aggregator fields registered in the C++
//--- catalog (category "K"). For each unique bucket key found across all
//--- saved DepositFilters we synthesize one virtual entry per agg, with
//--- a label that surfaces the bucket so users can drag it straight onto a
//--- formula slot — no manual bucket typing.
const BUCKET_FIELD_NAMES = ['sum_deposit_amount', 'sum_deposit_abs', 'count_deposits'] as const;

const AGG_PREFIX: Record<string, string> = {
  sum_deposit_amount: 'Σ',
  sum_deposit_abs:    'Σ |…|',
  count_deposits:     '#',
};

//--- Loads /api/fields/catalog and /api/deposit-filters in parallel, then
//--- replaces the three generic K-category fields with per-bucket virtual
//--- entries. The chip's `bucket` is pre-filled at drop time via the new
//--- FieldDef.default_bucket marker.
export async function loadCatalogWithBuckets(): Promise<FieldCatalog> {
  const [catalog, depositFilters] = await Promise.all([
    FieldsAPI.catalog(),
    DepositFiltersAPI.list().catch(() => []),
  ]);

  //--- Union of (key, first-seen-label) across all saved DepositFilters.
  const bucketLabel = new Map<string, string>();
  for (const f of depositFilters) {
    for (const b of f.buckets) {
      if (!bucketLabel.has(b.key)) bucketLabel.set(b.key, b.label || b.key);
    }
  }

  //--- No buckets yet → keep the generic 3 entries so the user sees the
  //--- feature exists and can discover the bucket flow on the chip itself.
  if (bucketLabel.size === 0) return catalog;

  //--- Strip the generic 3, replace with per-bucket virtuals.
  const baseFields = new Map<string, FieldDef>();
  for (const name of BUCKET_FIELD_NAMES) {
    const f = catalog.fields.find(x => x.name === name);
    if (f) baseFields.set(name, f);
  }
  const fields = catalog.fields.filter(f => !BUCKET_FIELD_NAMES.includes(f.name as typeof BUCKET_FIELD_NAMES[number]));

  for (const [key, label] of bucketLabel) {
    for (const name of BUCKET_FIELD_NAMES) {
      const base = baseFields.get(name);
      if (!base) continue;
      fields.push({
        ...base,
        label: `${AGG_PREFIX[name] ?? name}  ${label}  (${key})`,
        default_bucket: key,
      });
    }
  }

  return { ...catalog, fields };
}
