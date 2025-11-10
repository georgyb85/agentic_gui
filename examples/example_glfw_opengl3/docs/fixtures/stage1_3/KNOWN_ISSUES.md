# Stage 1.3 Known Issues

## 2024-04-05 – QuestDB ILP tag typo
- **Symptom:** initial export used `granularity=240m` tag instead of `4h` for dataset `btc_4h_v2`.
- **Impact:** backend filters relying on `granularity=4h` returned empty arrays.
- **Resolution:** re-exported dataset with the correct tag and added guard in export manifest.

## 2024-04-05 – Postgres JSON quoting
- **Symptom:** manual `COPY` into `walkforward_runs` failed because JSON fields were not escaped.
- **Impact:** prevented seeding of Stage 1.3 fixtures.
- **Resolution:** regenerated CSV via `psql \copy` with proper JSONB quoting (see `postgres_samples.csv`).
