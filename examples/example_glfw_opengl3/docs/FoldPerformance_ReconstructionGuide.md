# Fold Performance Reconstruction Guide

This note explains exactly how the desktop walk‑forward engine derives the per‑fold metrics that you now ingest via the Stage1 Drogon API. Use it as the canonical reference when reproducing fold tables and aggregate summaries in the Trading Control Panel or any other client.

The values appear in the JSON exported by `Stage1MetadataWriter` (see `metrics` and `thresholds` blocks) and in the QuestDB measurement containing predictions. This document bridges the two so you can implement identical calculations and binaries map cleanly.

---
## 1. Terminology

Each fold produces:

| Symbol              | Meaning                                                        |
|---------------------|----------------------------------------------------------------|
| `fold_number`       | Sequential integer (0‑based)                                   |
| `train_*_idx`       | Half‑open index range `[train_start_idx, train_end_idx)`        |
| `test_*_idx`        | Half‑open index range for the holdout window                    |
| `samples_train`     | Count of train rows actually used                              |
| `samples_test`      | Count of test rows actually scored                             |
| `n_signals`         | # of long entries placed for this fold                         |
| `n_short_signals`   | # of short entries                                             |
| `sum_wins` / `sum_losses` | Aggregate monetary P&L for profitable / losing long trades |
| `sum_short_wins` / `sum_short_losses` | Same, but only for shorts                      |
| `running_sum`       | Cumulative P&L for long signals through this fold              |
| `running_sum_short` | Cumulative P&L for short signals                                |
| `running_sum_dual`  | Cumulative P&L for long + short combined                        |
| `hit_rate`          | Long win ratio                                                 |
| `short_hit_rate`    | Short win ratio                                                |
| `profit_factor_train/test` | PF computed on train/test subsets (long only)          |
| `profit_factor_short_*` | PF for short side                                          |
| `thresholds.*`      | Signal thresholds emitted by the model (ROC, percentile, etc.) |

All numbers are consistent with the desktop UI; the frontend can re‑aggregate them to display the familiar tables.

---
## 2. Per‑fold Calculations

Given the set of executed trades for a fold:

```text
long_trades   = { trades with entry_condition = LONG }  (size = n_signals)
short_trades  = { trades with entry_condition = SHORT } (size = n_short_signals)
```

### 2.1 Hit Rate

```text
hit_rate       = wins_long / max(1, n_signals)
short_hit_rate = wins_short / max(1, n_short_signals)
```

where `wins_long` is the count of long trades with `pnl > 0`. The DIV/0 guard mirrors the desktop code (returns 0 when no trades were placed).

### 2.2 Profit Factor

For the long side:

```text
profit_factor_test = sum_wins / max(epsilon, sum_losses)
```

where `sum_wins` accumulates the positive P&L from long trades whose entry timestamp lies in the test slice, and `sum_losses` accumulates the absolute value of negative long P&L. If `sum_losses == 0`, we emit `999.0` when `sum_wins > 0`, otherwise `0`.

Short PF is computed the same way with `_short` fields. Dual PF uses `sum_wins + sum_short_wins` over `sum_losses + sum_short_losses`.

### 2.3 Running Sums

Inside the desktop loop (`SimulationEngine::RunSimulationThread`) we append the fold’s contribution to cumulative performance:

```cpp
running_sum       = previous_running_sum       + fold.signal_sum;
running_sum_short = previous_running_sum_short + fold.short_signal_sum;
running_sum_dual  = previous_running_sum_dual  + fold.signal_sum + fold.short_signal_sum;
```

These are what you see plotted in the UI and exported in the Stage1 payload. No extra scaling is performed; they are literal monetary P&L in the base currency of the OHLCV data.

### 2.4 Signal Statistics

```
signal_sum        = sum of monetary P&L for each long trade in this fold
short_signal_sum  = sum of monetary P&L for each short trade in this fold
signal_rate       = n_signals / samples_test
short_signal_rate = n_short_signals / samples_test
avg_return_on_signals        = signal_sum / max(1, n_signals)
median_return_on_signals     = median(long_trade_pnls)
std_return_on_signals        = stddev(long_trade_pnls)
avg_return_on_short_signals  = short_signal_sum / max(1, n_short_signals)
avg_predicted_return_on_signals = average model prediction for traded bars
```

The per‑fold JSON already contains these values, so for frontend display you can read them directly and optionally recompute to verify.

---
## 3. Aggregation to Summary Metrics

The aggregate metrics stored in `summary_metrics` and displayed in the UI are simple accumulations across all folds:

```text
total_long_signals  = Σ fold.n_signals
total_short_signals = Σ fold.n_short_signals
total_signals       = total_long_signals + total_short_signals
pf_long  = compute_pf(Σ fold.sum_wins, Σ fold.sum_losses)
pf_short = compute_pf(Σ fold.sum_short_wins, Σ fold.sum_short_losses)
pf_dual  = compute_pf(Σ (fold.sum_wins + fold.sum_short_wins),
                      Σ (fold.sum_losses + fold.sum_short_losses))
running_sum_long  = last fold.running_sum (already cumulative)
running_sum_short = last fold.running_sum_short
running_sum_dual  = last fold.running_sum_dual
hit_rate_long   = Σ (fold.hit_rate * fold.n_signals) / max(1, total_long_signals)
hit_rate_short  = Σ (fold.short_hit_rate * fold.n_short_signals) / max(1, total_short_signals)
hit_rate_overall = (Σ fold.signal_wins + Σ fold.short_signal_wins) /
                   max(1, total_signals)
```

`compute_pf` uses the same epsilon logic as the per‑fold metrics.

These formulas exactly match the implementation in `SummaryMetricsToJson()` (see `simulation/SimulationWindowNew.cpp:143-205`). Client code can simply parse `summary_metrics` from the Stage1 payload; however, if you re‑aggregate from the fold list you must mirror the guards (division by zero, PF ≥ 999 caps, etc.).

---
## 4. Pseudocode Outline

The walk-forward loop looks like this:

```pseudo
running_long  = 0
running_short = 0
running_dual  = 0
agg = init_aggregates()

for each fold in folds:
    trades = simulate_fold(fold)
    metrics = compute_metrics(trades)

    running_long  += metrics.signal_sum
    running_short += metrics.short_signal_sum
    running_dual  += metrics.signal_sum + metrics.short_signal_sum

    emit fold JSON with:
        n_signals, n_short_signals, sum_wins, sum_losses, ...
        running_sum       = running_long
        running_sum_short = running_short
        running_sum_dual  = running_dual

    agg.sum_wins        += metrics.sum_wins
    agg.sum_losses      += metrics.sum_losses
    agg.sum_short_wins  += metrics.sum_short_wins
    agg.sum_short_losses+= metrics.sum_short_losses
    agg.long_signals    += metrics.n_signals
    agg.short_signals   += metrics.n_short_signals
    agg.weighted_long_hits += metrics.hit_rate * metrics.n_signals
    ... etc ...

summary_metrics = serialize_aggregates(agg, running_long, running_short, running_dual)
```

`simulate_fold` executes the strategy on the test window using the trained model, placing entries whenever predictions cross the configured thresholds. `compute_metrics` is pure bookkeeping on the resulting trade list.

---
## 5. Reproducing in the Frontend

1. **Fold Table** – Read directly from the `folds` array returned by `/api/runs/{id}`. All columns shown in the desktop UI already exist (`n_signals`, `pf_train`, `running_sum`, etc.).
2. **Performance Chart** – Plot `fold_number` on the x-axis vs. `running_sum`, `running_sum_short`, `running_sum_dual` (or reconstruct cumulatives via the formula above).
3. **Summary Widget** – Use the `summary_metrics` object or recompute with the aggregation formulas.
4. **Totals (Signals, Hit Rates, Return)** – The Stage1 payload exposes the same numbers, so you can display them verbatim.

If you need to validate the backend numbers against the desktop UI, run the same walk-forward locally, save the run, and compare `summary_metrics` to the desktop’s Performance Summary panel—they should match down to the last cent.

---
## 6. Reference

- Simulation engine core: `simulation/SimulationEngine.cpp`
- Result widget aggregation: `simulation/ui/SimulationResultsWidget_v2.cpp`
- Stage1 export: `stage1_metadata_writer.cpp`
- Example fold JSON in SQL spool: `docs/fixtures/stage1_3/pending_postgres_inserts.sql`

This guide should give the frontend team everything needed to parse, verify, and display walk-forward fold statistics consistently with the desktop workflow.
