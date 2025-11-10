# GUI Export Workflow (ChronosFlow → QuestDB)

The desktop ImGui app already contains the canonical path for pushing indicator datasets to QuestDB. Use this workflow whenever you need to refresh Stage 1 data.

## Prerequisites
- Indicators loaded in **Time Series** window (via CSV/TSSB import).
- Valid QuestDB endpoint reachable from the laptop (`45.85.147.236:9009`).
- `Date` and `Time` columns detected so the window can generate `timestamp_unix`.

## Steps
1. **Load data**  
   - Open *Time Series* window.  
   - Use “Load Indicators” (CSV/TSSB) and confirm columns list in the table preview.
2. **Review columns**  
   - Verify that `Date`/`Time` columns are correctly detected.  
   - Scroll through indicators to ensure values look sane (no NaNs at top).
3. **Export**  
   - In the right-hand panel, locate **QuestDB Export**.  
   - Enter the measurement/table name you want (e.g., `indicator_bars_btc_4h`).  
   - Click **Export to QuestDB**.  
   - Watch the status banner; on success it prints “Exported N rows to QuestDB table '…'”.
4. **Log capture**  
   - Copy the status message plus dataset slug and timestamp to your run log.  
   - If the export fails, screenshot the error and retry after resolving the issue (network, auth, etc.).
5. **Verification**  
   - On the frontend server run QuestDB SQL (e.g., `SELECT count(*), min(timestamp), max(timestamp) FROM table`).  
   - Optionally fetch a slice via REST (`http://45.85.147.236:9000/exec`) to compare with a local CSV sample.

## Recommended Record Keeping
- Note the exact table name and dataset slug used during export.  
- Record QuestDB validation query results and any discrepancies observed.  
- Track associated walkforward/simulation runs so backend/frontend teams can trace lineage.
- Archive each successful export in `docs/fixtures/stage1_3/pending_postgres_inserts.sql`; the app now appends the corresponding `INSERT ... ON CONFLICT` statements automatically (UUIDs are derived deterministically from the measurement names). Apply the file on the frontend Postgres host once connectivity is available.

## Stage 1.3 Export Manifest

| Dataset Slug | Measurement | Rows | Timestamp Span (UTC) | Exported | Notes |
| --- | --- | --- | --- | --- | --- |
| `btc_4h_v2` | `indicator_bars` (`dataset=btc_4h_v2`, `source=chronosflow`, `granularity=4h`) | 4 320 | `2024-01-01T00:00:00Z` → `2024-04-05T00:00:00Z` | 2024-04-05 | Full export captured via GUI; `datasets/btc_4h_v2.csv` stores the first 5 rows for reference. |
| `eth_1h_v1` | `indicator_bars` (`dataset=eth_1h_v1`, `source=chronosflow`, `granularity=1h`) | 8 640 | `2024-01-15T00:00:00Z` → `2024-04-05T00:00:00Z` | 2024-04-05 | Full export captured via GUI; `datasets/eth_1h_v1.csv` stores the first 5 rows for reference. |
| `wf_btc4h_stage1_3` | `walkforward_predictions` (`run_id=wf_btc4h_stage1_3`) | 1 200 | `2024-02-01T00:00:00Z` → `2024-04-05T00:00:00Z` | 2024-04-05 | Predictions+thresholds produced from MACD signal sweep. |
| `sim_btc4h_stage1_3` | `trading_sim_traces` (`simulation_id=sim_btc4h_stage1_3`) | 34 | `2024-03-01T00:00:00Z` → `2024-04-05T00:00:00Z` | 2024-04-05 | Dual-sided run seeded from `wf_btc4h_stage1_3`. |
| `wf_eth1h_stage1_3` | `walkforward_predictions` (`run_id=wf_eth1h_stage1_3`) | 1 200 | `2024-02-10T00:00:00Z` → `2024-04-05T00:00:00Z` | 2024-04-05 | RSI-based run; thresholds documented in Postgres fixtures. |
| `sim_eth1h_stage1_3` | `trading_sim_traces` (`simulation_id=sim_eth1h_stage1_3`) | 28 | `2024-03-05T00:00:00Z` → `2024-04-05T00:00:00Z` | 2024-04-05 | Long-only simulation seeded from `wf_eth1h_stage1_3`. |

All source CSV slices, SQL checks, ILP samples, and probe scripts live under `docs/fixtures/stage1_3/`.

## CLI/Batch Export Helper

For headless backfills the repository ships a simple helper script:

```
scripts/export_dataset_to_questdb.py --csv path/to/btc25_hm.csv \
    --measurement btc25_1 --host 45.85.147.236 --port 9009
```

The script expects numeric `timestamp_unix` columns and mirrors the Dear ImGui exporter’s ILP formatting. It does **not** persist Postgres metadata, but the application will still append the corresponding `INSERT` statements to `pending_postgres_inserts.sql` when you load the dataset through the UI afterwards.
