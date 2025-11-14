# Stage1 Backend REST API Documentation

**Version:** 2025-11-10
**Base URL:** `http://localhost:8081` or `https://localhost:8444`

## Table of Contents

1. [Authentication](#authentication)
2. [Health Endpoints](#health-endpoints)
3. [Dataset Endpoints](#dataset-endpoints)
4. [Run Endpoints](#run-endpoints)
5. [Simulation Endpoints](#simulation-endpoints)
6. [QuestDB Endpoints](#questdb-endpoints)
7. [Job Endpoints](#job-endpoints)
8. [Error Responses](#error-responses)

---

## Authentication

**Current Status:** Authentication is disabled (`enable_auth: false` in config).

All endpoints currently accept requests without authentication tokens. When authentication is enabled in future versions, requests will require the `X-Stage1-Token` header.

---

## Health Endpoints

### GET /healthz
Simple liveness check.

**Response:**
```json
{"status": "ok"}
```

### GET /readyz
Readiness check.

**Response:**
```json
{"status": "ready"}
```

### GET /api/health
Detailed health information.

**Response:**
```json
{
  "status": "healthy",
  "timestamp": "2025-11-10T01:30:00Z",
  "uptime": 3600
}
```

---

## Dataset Endpoints

### GET /api/datasets
List all datasets with pagination.

**Query Parameters:**
- `limit` (optional, default: 50): Maximum number of results
- `offset` (optional, default: 0): Pagination offset

**Response:**
```json
{
  "datasets": [
    {
      "dataset_id": "uuid",
      "dataset_slug": "btc_1h",
      "symbol": "BTC/USD",
      "granularity": "1h",
      "source": "exchange_name",
      "ohlcv_measurement": "btc_1h_ohlcv",
      "indicator_measurement": "btc_1h_indicators",
      "ohlcv_row_count": 100000,
      "indicator_row_count": 100000,
      "ohlcv_first_ts": 1609459200000000,
      "ohlcv_last_ts": 1730764800000000,
      "indicator_first_ts": 1609459200000000,
      "indicator_last_ts": 1730764800000000,
      "metadata": {}
    }
  ],
  "count": 1
}
```

### GET /api/datasets/{id}
Get a specific dataset by ID.

**Response:** Single dataset object (same structure as list item above), or 404 if not found.

### POST /api/datasets
Create or update a dataset.

**Request Body:**
```json
{
  "dataset_id": "uuid",
  "dataset_slug": "btc_1h",
  "symbol": "BTC/USD",
  "granularity": "1h",
  "source": "exchange_name",
  "ohlcv_measurement": "btc_1h_ohlcv",
  "indicator_measurement": "btc_1h_indicators"
}
```

**Response (201 Created):**
```json
{
  "success": true,
  "message": "Dataset created successfully"
}
```

### GET /api/datasets/{id}/runs
List all walkforward runs for a specific dataset.

**Query Parameters:**
- `limit` (optional, default: 50)
- `offset` (optional, default: 0)

**Response:**
```json
{
  "runs": [
    {
      "run_id": "uuid",
      "dataset_id": "uuid",
      "fold_count": 5,
      "features": ["feature1", "feature2"],
      "thresholds": [0.5, 0.6, 0.7],
      "created_at": "2025-11-10T01:30:00Z"
    }
  ],
  "count": 1
}
```

### GET /api/datasets/{id}/indicators
Return raw indicator rows for a dataset (the GPU/XGBoost services and the walkforward UI rely on this endpoint).

**Query Parameters:**
- `start_timestamp` / `end_timestamp` (optional, ms since epoch) – inclusive window filters.
- `start_index` / `end_index` (optional, zero-based) – fetch by raw bar index. When supplied, the server translates these into the proper `LIMIT/OFFSET`. `end_index` is exclusive.
- `limit` (optional, default: 5000, max: 200000) – maximum number of rows (applied after `end_index` trimming).
- `desc` (optional, default: `false`) – sort direction.
- `timestamp_column` (optional) – override the timestamp column used in the `WHERE` clause (defaults to probing `timestamp`, `timestamp_unix`, etc.).
- `columns` (optional) – comma‑separated column whitelist (e.g., feature + target set).

**Response:**
```json
{
  "measurement": "btc_1h_indicators",
  "count": 650,
  "rows": [
    {
      "timestamp_ms": 1732831200000,
      "rsi_14": 55.1,
      "pcv_10_20": -4.25,
      "TGT_115": -0.0012
    }
  ],
  "questdb": { "... full QuestDB payload ..." }
}
```

Each row now includes a canonical `timestamp_ms` column emitted by the ingestion pipeline, so clients no longer need to reconstruct epochs from `Date`/`Time`.
Rows returned from this endpoint are inner-joined with the dataset's OHLCV measurement, ensuring that every indicator bar has a corresponding OHLCV bar.

### GET /api/datasets/{id}/manifest
Return the dataset manifest describing the canonical timestamp range, bar interval, lookback, and measurements. This is generated at export time and stored in the dataset metadata JSON.

**Response:**
```json
{
  "version": 1,
  "dataset_id": "4931fd4d-487b-451f-9b27-8e9eedd03e68",
  "dataset_slug": "t_ind6",
  "granularity": "1h",
  "source": "laptop_imgui",
  "ohlcv_measurement": "t_ind6_ohlcv",
  "indicator_measurement": "t_ind6_ind6",
  "bar_interval_ms": 3600000,
  "lookback_rows": 48,
  "first_ohlcv_timestamp_ms": 1688169600000,
  "last_ohlcv_timestamp_ms": 1735185600000,
  "first_indicator_timestamp_ms": 1688186880000,
  "last_indicator_timestamp_ms": 1735185600000,
  "ohlcv_rows": 679,
  "indicator_rows": 631,
  "exported_at": "2025-11-12T00:05:18Z"
}
```

### GET /api/datasets/{id}/index_map
Convert raw row indices into timestamps without fetching every indicator column.

**Query Parameters:**
- `start_idx` (required): starting index (inclusive).
- `end_idx` (required): ending index (exclusive).
- `type` (optional, default `indicator`): `indicator` or `ohlcv`.

**Response:**
```json
{
  "measurement": "t_ind6",
  "start_index": 520,
  "end_index": 530,
  "rows": [
    { "index": 520, "timestamp_ms": 1734699600000 },
    { "index": 521, "timestamp_ms": 1734703200000 }
  ]
}
```

---

## Run Endpoints

### GET /api/runs
List all walkforward runs.

**Query Parameters:**
- `dataset_id` (optional): Filter by dataset ID
- `limit` (optional, default: 50)
- `offset` (optional, default: 0)

**Response:**
```json
{
  "runs": [
    {
      "run_id": "uuid",
      "dataset_id": "uuid",
      "fold_count": 5,
      "features": ["feature1", "feature2"],
      "thresholds": [0.5, 0.6, 0.7],
      "created_at": "2025-11-10T01:30:00Z"
    }
  ],
  "count": 1
}
```

### GET /api/runs/{id}
Get a specific run by ID.

**Response:** Single run object (matches Stage1MetadataReader expectations).

### POST /api/runs
Create a new walkforward run.

**Request Body:**
```json
{
  "run_id": "uuid",
  "dataset_id": "uuid",
  "fold_count": 5,
  "features": ["feature1", "feature2"],
  "thresholds": [0.5, 0.6, 0.7]
}
```

**Response (201 Created):**
```json
{
  "success": true,
  "message": "Run created successfully"
}
```

### GET /api/runs/{id}/predictions
Stream predictions for a run as CSV or JSON.

**Query Parameters:**
- `format` (optional, default: "csv"): Output format ("csv" or "json")

**Response (CSV):**
```csv
timestamp,bar_index,fold_number,prediction,target,threshold_0,threshold_1
1609459200000000,0,1,0.65,1,0.5,0.6
```

**Response (JSON):**
```json
{
  "dataset": [
    [1609459200000000, 0, 1, 0.65, 1, 0.5, 0.6]
  ]
}
```

**Note:** Predictions are stored in QuestDB measurement `wf_{run_id}`.

---

## Simulation Endpoints

### GET /api/simulations
List all simulation runs.

**Query Parameters:**
- `run_id` (optional): Filter by walkforward run ID
- `limit` (optional, default: 50)
- `offset` (optional, default: 0)

**Response:**
```json
{
  "simulations": [
    {
      "simulation_id": "uuid",
      "run_id": "uuid",
      "initial_capital": 10000,
      "parameters": {},
      "results": {},
      "created_at": "2025-11-10T01:30:00Z"
    }
  ],
  "count": 1
}
```

### GET /api/simulations/{id}
Get a specific simulation by ID.

### POST /api/simulations
Create a new simulation run.

**Request Body:**
```json
{
  "simulation_id": "uuid",
  "run_id": "uuid",
  "initial_capital": 10000,
  "parameters": {}
}
```

### GET /api/simulations/{id}/trades
Get all trades for a simulation.

**Query Parameters:**
- `limit` (optional, default: 1000)
- `offset` (optional, default: 0)

**Response:**
```json
{
  "trades": [
    {
      "trade_id": "uuid",
      "simulation_id": "uuid",
      "bar_timestamp": 1609459200000000,
      "side": "long",
      "size": 1.0,
      "entry_price": 29000.0,
      "exit_price": 29500.0,
      "pnl": 500.0,
      "return_pct": 1.72,
      "metadata": {}
    }
  ],
  "count": 1
}
```

---

## QuestDB Endpoints

### POST /api/questdb/query
Execute SQL query against QuestDB.

**Request Body:**
```json
{
  "sql": "SELECT * FROM btc_1h_ohlcv LIMIT 10"
}
```

**Response:**
```json
{
  "query": "SELECT * FROM btc_1h_ohlcv LIMIT 10",
  "columns": ["timestamp", "open", "high", "low", "close", "volume"],
  "dataset": [
    [1609459200000000, 29000, 29500, 28900, 29400, 1000]
  ],
  "count": 1
}
```

### POST /api/questdb/export
Export query results as CSV.

**Request Body:**
```json
{
  "sql": "SELECT * FROM btc_1h_ohlcv LIMIT 10"
}
```

**Response:**
```csv
timestamp,open,high,low,close,volume
1609459200000000,29000,29500,28900,29400,1000
```

**Content-Type:** `text/csv`
**Content-Disposition:** `attachment; filename="export.csv"`

### GET /api/questdb/measurements
List all QuestDB tables (measurements).

**Query Parameters:**
- `prefix` (optional): Filter tables by prefix

**Response:**
```json
{
  "measurements": [
    {"name": "btc_1h_ohlcv"},
    {"name": "btc_1h_indicators"}
  ],
  "count": 2
}
```

### GET /api/questdb/measurement/{name}
Get metadata for a specific measurement.

**Response:**
```json
{
  "name": "btc_1h_ohlcv",
  "row_count": 100000,
  "first_ts": 1609459200000000,
  "last_ts": 1730764800000000
}
```

### POST /api/questdb/import
Direct import (currently returns 501 Not Implemented).

**Note:** Use `/api/questdb/import/async` instead for large imports.

### POST /api/questdb/import/async
Asynchronously import CSV data to QuestDB via ILP.

**Request Body (JSON):**
```json
{
  "measurement": "my_table",
  "data": "timestamp_unix,value1,value2\n1609459200,100,200\n1609459260,101,201",
  "filename": "data.csv"
}
```

**Required Fields:**
- `measurement`: Target QuestDB table name
- `data`: CSV data as string (first row must be header)

**CSV Requirements:**
- Must contain `timestamp_unix` or `timestamp` column
- Timestamp should be in seconds, milliseconds, or microseconds (auto-detected)
- All other columns are imported as fields

**Response (202 Accepted):**
```json
{
  "job_id": "uuid",
  "status": "PENDING",
  "message": "Import job created. Poll /api/jobs/{job_id} for status.",
  "poll_url": "/api/jobs/uuid"
}
```

**Processing:**
1. CSV data is saved to `/opt/stage1_server/uploads/import_{timestamp}.csv`
2. Async job is created with type `questdb_import`
3. Worker thread claims job and processes it
4. CSV rows are converted to ILP format and sent to QuestDB via TCP (port 9009)
5. Progress is updated during processing
6. Temp file is deleted on completion

---

## Job Endpoints

### GET /api/jobs
List all jobs.

**Query Parameters:**
- `job_type` (optional): Filter by job type (e.g., "questdb_import")
- `limit` (optional, default: 50)
- `offset` (optional, default: 0)

**Response:**
```json
{
  "jobs": [
    {
      "job_id": "uuid",
      "job_type": "questdb_import",
      "status": "COMPLETED",
      "payload": "{\"measurement\":\"my_table\",\"file_path\":\"/opt/stage1_server/uploads/import_123.csv\"}",
      "progress": 1000,
      "total": 1000,
      "result": "{\"success\":true,\"message\":\"CSV imported successfully\"}",
      "error": null,
      "created_at": "2025-11-10T01:30:00",
      "updated_at": "2025-11-10T01:30:30",
      "started_at": "2025-11-10T01:30:01",
      "completed_at": "2025-11-10T01:30:30"
    }
  ],
  "count": 1
}
```

### GET /api/jobs/{id}
Get job status by ID.

**Response:** Single job object (same structure as list item above).

**Job Status Values:**
- `PENDING`: Job created, waiting for worker
- `RUNNING`: Job is being processed
- `COMPLETED`: Job finished successfully
- `FAILED`: Job failed (see `error` field)
- `CANCELLED`: Job was cancelled

**Polling Best Practices:**
1. Poll every 1-2 seconds while status is PENDING or RUNNING
2. Stop polling once status is COMPLETED, FAILED, or CANCELLED
3. Check `progress` and `total` fields for progress indication

---

## Error Responses

All error responses follow this format:

```json
{
  "error": "Error message description"
}
```

**Common HTTP Status Codes:**
- `200 OK`: Request successful
- `201 Created`: Resource created successfully
- `202 Accepted`: Async operation accepted (job created)
- `400 Bad Request`: Invalid request (missing fields, invalid JSON)
- `404 Not Found`: Resource not found
- `500 Internal Server Error`: Server error
- `501 Not Implemented`: Feature not yet implemented
- `503 Service Unavailable`: Service temporarily unavailable

**Example Error:**
```json
{
  "error": "Missing 'measurement' parameter"
}
```

---

## Desktop Integration Notes

### Dataset Manager Integration
- Use `GET /api/datasets` to list all datasets
- Use `GET /api/datasets/{id}/runs` to get runs for a specific dataset
- Dataset schema matches Stage1 desktop expectations with all required fields

### Run Results Integration
- Use `GET /api/runs/{id}` to get run metadata
- Use `GET /api/runs/{id}/predictions?format=csv` to download predictions
- Predictions CSV columns: `timestamp,bar_index,fold_number,prediction,target,threshold_0,threshold_1,...`

### Simulation Results Integration
- Use `GET /api/simulations/{id}` to get simulation metadata
- Use `GET /api/simulations/{id}/trades` to get all trades
- Trade data includes entry/exit prices, PnL, and return percentages

### Async Import Workflow
1. Prepare CSV data with required columns (including `timestamp_unix`)
2. POST to `/api/questdb/import/async` with JSON body
3. Receive job ID in response
4. Poll `GET /api/jobs/{job_id}` for status updates
5. Check `progress` field for import progress
6. Wait for status to become `COMPLETED` or `FAILED`

---

## Configuration

**PostgreSQL Connection:**
- Host: Configured via environment or config.json
- Database: `stage1_trading`
- Connection pool: 5 connections

**QuestDB Connection:**
- HTTP API: Port 9000 (queries, exports)
- ILP (InfluxDB Line Protocol): Port 9009 (imports)
- Timeout: 30 seconds

**Server:**
- HTTP: Port 8081
- HTTPS: Port 8444
- CORS: Enabled for `agenticresearch.info` and `localhost:3000`

**Workers:**
- 2 worker threads by default
- Continuously poll for PENDING jobs
- Execute jobs asynchronously
- Support job types: `questdb_import` (more types can be added)

---

## Implementation Status

✅ **Fully Implemented:**
- Health endpoints
- Dataset CRUD + listing runs
- Run CRUD + predictions streaming
- Simulation CRUD + trades listing
- QuestDB query/export
- QuestDB async import via ILP
- Job management with worker pool
- Atomic job claiming
- Progress tracking

⚠️ **Limitations:**
- Authentication disabled (auth framework exists but not enforced)
- No rate limiting
- No WebSocket/SSE support for real-time updates
- Direct import endpoint returns 501 (use async endpoint)

🔜 **Future Enhancements:**
- Enable authentication
- Add rate limiting
- WebSocket support for job status updates
- Monitoring dashboard
- OpenAPI/Swagger specification
