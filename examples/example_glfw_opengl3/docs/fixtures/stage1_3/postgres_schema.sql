-- Stage 1 QuestDB/Postgres schema for walkforward + simulation metadata.
-- Run against the Postgres instance on 45.85.147.236 (or a staging clone).

CREATE TABLE IF NOT EXISTS indicator_datasets (
    dataset_id         UUID PRIMARY KEY,
    symbol             TEXT NOT NULL,
    granularity        TEXT NOT NULL,
    source             TEXT NOT NULL,
    questdb_tag        TEXT NOT NULL UNIQUE,
    row_count          BIGINT NOT NULL CHECK (row_count >= 0),
    first_bar_ts       TIMESTAMPTZ NOT NULL,
    last_bar_ts        TIMESTAMPTZ NOT NULL,
    created_at         TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_indicator_datasets_symbol_granularity
    ON indicator_datasets (symbol, granularity);

CREATE TABLE IF NOT EXISTS walkforward_runs (
    run_id               UUID PRIMARY KEY,
    dataset_id           UUID NOT NULL REFERENCES indicator_datasets(dataset_id) ON DELETE CASCADE,
    prediction_measurement TEXT NOT NULL,
    target_column        TEXT NOT NULL,
    feature_columns      JSONB NOT NULL,
    hyperparameters      JSONB NOT NULL,
    walk_config          JSONB NOT NULL,
    status               TEXT NOT NULL,
    requested_by         TEXT,
    started_at           TIMESTAMPTZ,
    completed_at         TIMESTAMPTZ,
    duration_ms          BIGINT,
    summary_metrics      JSONB NOT NULL,
    created_at           TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_walkforward_runs_dataset_created
    ON walkforward_runs (dataset_id, created_at DESC);

CREATE TABLE IF NOT EXISTS walkforward_folds (
    run_id             UUID NOT NULL REFERENCES walkforward_runs(run_id) ON DELETE CASCADE,
    fold_number        INTEGER NOT NULL,
    train_start_idx    BIGINT NOT NULL,
    train_end_idx      BIGINT NOT NULL,
    test_start_idx     BIGINT NOT NULL,
    test_end_idx       BIGINT NOT NULL,
    samples_train      BIGINT NOT NULL,
    samples_test       BIGINT NOT NULL,
    best_iteration     INTEGER,
    best_score         DOUBLE PRECISION,
    thresholds         JSONB NOT NULL,
    metrics            JSONB NOT NULL,
    PRIMARY KEY (run_id, fold_number)
);

CREATE INDEX IF NOT EXISTS idx_walkforward_folds_run
    ON walkforward_folds (run_id);

CREATE TABLE IF NOT EXISTS simulation_runs (
    simulation_id        UUID PRIMARY KEY,
    run_id               UUID NOT NULL REFERENCES walkforward_runs(run_id) ON DELETE CASCADE,
    dataset_id           UUID NOT NULL REFERENCES indicator_datasets(dataset_id) ON DELETE CASCADE,
    input_run_measurement TEXT NOT NULL,
    questdb_namespace    TEXT NOT NULL,
    mode                 TEXT NOT NULL,
    config               JSONB NOT NULL,
    status               TEXT NOT NULL,
    started_at           TIMESTAMPTZ,
    completed_at         TIMESTAMPTZ,
    summary_metrics      JSONB NOT NULL,
    created_at           TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_simulation_runs_run_created
    ON simulation_runs (run_id, created_at DESC);

CREATE TABLE IF NOT EXISTS simulation_trades (
    trade_id           UUID PRIMARY KEY,
    simulation_id      UUID NOT NULL REFERENCES simulation_runs(simulation_id) ON DELETE CASCADE,
    bar_timestamp      TIMESTAMPTZ NOT NULL,
    side               TEXT NOT NULL,
    size               DOUBLE PRECISION NOT NULL,
    entry_price        DOUBLE PRECISION NOT NULL,
    exit_price         DOUBLE PRECISION,
    pnl                DOUBLE PRECISION,
    return_pct         DOUBLE PRECISION,
    metadata           JSONB NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_simulation_trades_sim_ts
    ON simulation_trades (simulation_id, bar_timestamp);

CREATE TABLE IF NOT EXISTS simulation_trade_buckets (
    simulation_id     UUID NOT NULL REFERENCES simulation_runs(simulation_id) ON DELETE CASCADE,
    side              TEXT NOT NULL,
    trade_count       BIGINT NOT NULL,
    win_count         BIGINT NOT NULL,
    profit_factor     DOUBLE PRECISION,
    avg_return_pct    DOUBLE PRECISION,
    max_drawdown_pct  DOUBLE PRECISION,
    notes             TEXT,
    PRIMARY KEY (simulation_id, side)
);

CREATE INDEX IF NOT EXISTS idx_simulation_trade_buckets_sim
    ON simulation_trade_buckets (simulation_id);
