export function validateCapacityRun(run, { concurrency, sizesMiB, rounds }) {
  const errors = [];
  if (run?.status !== 'PASS') errors.push(`status:${run?.status || 'missing'}`);
  if (run?.config?.mode !== 'fixed') errors.push(`mode:${run?.config?.mode || 'missing'}/fixed`);
  if (run?.config?.fixedConcurrency !== concurrency) {
    errors.push(`fixed_concurrency:${run?.config?.fixedConcurrency || 'missing'}/${concurrency}`);
  }
  if (run?.config?.rounds !== rounds) errors.push(`configured_rounds:${run?.config?.rounds || 'missing'}/${rounds}`);
  for (const sizeMiB of sizesMiB) {
    const logicalBytes = sizeMiB * 1024 * 1024;
    const rows = (run?.results || []).filter(row => row.logicalBytes === logicalBytes);
    if (rows.length !== rounds) errors.push(`round_count:${sizeMiB}:${rows.length}/${rounds}`);
    const seen = new Set(rows.map(row => row.round));
    for (let round = 1; round <= rounds; round++) {
      if (!seen.has(round)) errors.push(`missing_round:${sizeMiB}:${round}`);
    }
  }
  return errors;
}
