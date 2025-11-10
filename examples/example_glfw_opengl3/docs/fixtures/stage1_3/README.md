# Stage 1.3 Dataset Fixtures

This directory collects the reference materials produced from the laptop workflow to support backend and frontend development for Stage 1.3.

## Contents
- `datasets/` – truncated CSV slices exported via the Time Series window before pushing to QuestDB (full datasets reside in QuestDB/Postgres; these samples cover the first few rows for reference).
- `questdb_checks.sql` – verification queries executed against QuestDB after each export.
- `postgres_samples.csv` – sampled rows captured from Postgres tables seeded for Stage 1.3.
- `ilp_samples.txt` – representative ILP lines sent during exports.
- `backend_probe.sh` – curl-based smoke checks used to validate Drogon endpoints against the seeded datasets.
- `KNOWN_ISSUES.md` – running log of anomalies or follow-ups encountered during Stage 1.3 preparation.
- `validation_logs/` – QuestDB/psql outputs captured after each export (populate this directory once live exports complete).
- `pending_postgres_inserts.sql` – append-only spool of INSERT statements written automatically by the Dear ImGui app after each export/run/save (UUIDs are derived deterministically from the measurement names so referential integrity is preserved across runs).

Each artifact ties back to the dataset slugs recorded in the export manifest inside `docs/GUI_EXPORT_WORKFLOW.md`.

## Dataset Provenance Checklist

For every new dataset exported from Dear ImGui:
- Note the source files (e.g., `btc25.txt`, `btc25 hm.csv`) and any lookback padding required before indicators become valid.
- Record the chosen QuestDB measurement name (e.g., `btc25_1`) and the generated column schema snapshot.
- Capture validation outputs (row counts, min/max timestamps) in `validation_logs/`.
- Append the dataset slug and export timestamp to `docs/GUI_EXPORT_WORKFLOW.md`.
- Apply `pending_postgres_inserts.sql` on the frontend Postgres host after a session to mirror the captured metadata.
- UUIDs in the inserts are deterministic (derived from the measurement names) so re-running on the same dataset/run/simulation will produce idempotent upserts.
