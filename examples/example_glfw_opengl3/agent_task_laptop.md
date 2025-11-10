# Laptop Agent Task Brief (Stage 1.1–1.2)

## Context
- Host: local development laptop (current repo checkout).
- Purpose: curate historical datasets and documentation that flow into QuestDB/Postgres for Stage 1 deliverables.
- Remote services: QuestDB/Postgres at `45.85.147.236`, Drogon backend at `39.114.73.97`.

## Primary Goals
1. Use the existing Dear ImGui workflow to export indicator datasets to QuestDB following the schema in `STAGE1_ALIGNMENT_RESULTS.md`.
2. Maintain manual validation logs (row counts, min/max timestamps) for every export so backend/frontend agents can trace lineage.
3. Keep documentation current when schema conventions evolve.

## Detailed Tasks
- **Manual Export**
  - Follow the steps in `docs/GUI_EXPORT_WORKFLOW.md` when pushing datasets from the Time Series window.
  - Record dataset slug, measurement name, and QuestDB validation queries (row count, min/max timestamp) in your run log.
- **Schema Validation**
  - Spot-check exported data with lightweight SELECT statements; confirm tags (`dataset`, `source`, `granularity`) match the agreed contract.
- **Documentation**
  - Update `STAGE1_ALIGNMENT_RESULTS.md` whenever conventions change.
  - Note any issues or required follow-ups for backend/frontend agents.

## Deliverables to Attach
- Export/validation logs (QuestDB queries, timestamps, dataset slug).
- Updated documentation referencing any schema changes.

## Verification Checklist
- `[ ]` Export recorded in log with dataset slug, table name, validation query output.
- `[ ]` Spot-check confirms tag fields (`dataset`, `source`, `granularity`) and timestamps align with source data.
- `[ ]` Documentation links back to `STAGE1_ALIGNMENT_RESULTS.md` for traceability.
