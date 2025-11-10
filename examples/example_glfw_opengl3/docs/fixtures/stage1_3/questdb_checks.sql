-- QuestDB validation queries for Stage 1.3 datasets.
-- Run via http://45.85.147.236:9000/exec or the QuestDB console.

-- Indicator dataset btc25_1
SELECT dataset, source, granularity, COUNT(*) AS rows,
       MIN(timestamp_unix) AS first_unix, MAX(timestamp_unix) AS last_unix
FROM indicator_bars
WHERE dataset = 'btc25_1'
  AND source = 'chronosflow'
GROUP BY dataset, source, granularity;

-- Indicator dataset eth25_1
SELECT dataset, source, granularity, COUNT(*) AS rows,
       MIN(timestamp_unix) AS first_unix, MAX(timestamp_unix) AS last_unix
FROM indicator_bars
WHERE dataset = 'eth25_1'
  AND source = 'chronosflow'
GROUP BY dataset, source, granularity;

-- Walkforward predictions linked to btc25_run1
SELECT run_id, fold, dataset, target, COUNT(*) AS predictions
FROM walkforward_predictions
WHERE run_id = 'btc25_run1'
GROUP BY run_id, fold, dataset, target
ORDER BY fold;

-- Trading simulation traces linked to btc25_run1
SELECT simulation_id, run_id, mode, COUNT(*) AS trades,
       SUM(pnl) AS total_pnl, SUM(return_pct) AS total_return
FROM trading_sim_traces
WHERE simulation_id = 'btc25_sim1'
GROUP BY simulation_id, run_id, mode;
