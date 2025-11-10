# Laptop Agent Task Brief (Stage 1.3)

## Context
- Host: local development laptop (this Dear ImGui workspace).
- Role: curate and validate the QuestDB/Postgres data footprints that fuel the new Stage 1.3 backend controllers.
- Connected services: QuestDB/Postgres on `45.85.147.236`, Drogon backend prototype at `39.114.73.97`.

## Primary Goals
1. Supply production-like indicator, walkforward, and simulation datasets that exercise every Stage 1.3 endpoint.
2. Capture validation evidence (QuestDB queries, Postgres snapshots, ILP samples) so backend/frontend agents can debug without desktop access.
3. Keep documentation and manifests current as schemas evolve.

## Detailed Tasks
- **Dataset Provisioning**
  - Run export flows in Dear ImGui (`TimeSeriesWindow::ExportToQuestDB`) for at least two symbols/intervals and record manifest entries in `docs/GUI_EXPORT_WORKFLOW.md` (dataset slug, measurement, row count, timestamp span).
  - Backfill walkforward metadata into Postgres via the laptop CLI or SQL scripts; ensure `walkforward_runs`, `walkforward_folds`, and `simulation_runs` each have seeded rows.
  - When creating new exports, archive the raw CSV/Arrow inputs under `docs/fixtures/stage1_3/` with a README describing provenance.
- **Validation Artifacts**
  - Capture QuestDB verification queries (`SELECT COUNT(*), MIN(timestamp_unix), MAX(timestamp_unix) FROM …`) and store them in `docs/fixtures/stage1_3/questdb_checks.sql`.
  - Dump representative Postgres rows (`COPY (SELECT … LIMIT 20) TO STDOUT WITH CSV`) into `docs/fixtures/stage1_3/postgres_samples.csv`.
  - Record ILP line samples for each measurement in `docs/fixtures/stage1_3/ilp_samples.txt`, highlighting required tags/fields.
- **Backend Hand-off Package**
  - Generate curl scripts in `docs/fixtures/stage1_3/backend_probe.sh` that hit the Drogon mock endpoints and compare responses against the captured datasets (use jq assertions).
  - Update `STAGE1_ALIGNMENT_RESULTS.md` if new fields/tags are introduced during export.
  - Log any anomalies (e.g., QuestDB rejects rows, Postgres type mismatches) in `docs/fixtures/stage1_3/KNOWN_ISSUES.md` with reproduction notes.

## Deliverables to Attach
- Updated export manifest in `docs/GUI_EXPORT_WORKFLOW.md`.
- Fixture bundle under `docs/fixtures/stage1_3/` containing CSV/Arrow sources, SQL checks, ILP samples, curl probes, and issue log.
- Notation in `STAGE1_ALIGNMENT_RESULTS.md` for any schema adjustments triggered by new datasets.

## Verification Checklist
- `[ ]` Each QuestDB measurement referenced by Stage 1.3 endpoints has at least one fresh export logged in the manifest.
- `[ ]` Postgres tables contain seeded rows covering all JSON fields consumed by Drogon controllers.
- `[ ]` Curl probe script succeeds against the latest backend build using the supplied fixtures.
- `[ ]` All fixtures and documentation reference the exact dataset slugs/tag combinations used during export.
