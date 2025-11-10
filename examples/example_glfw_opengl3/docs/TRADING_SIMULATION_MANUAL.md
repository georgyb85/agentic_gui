# Trading Simulation Window - User Manual

## Table of Contents
1. [Overview and Motivation](#overview-and-motivation)
2. [Walk-Forward Testing Methodology](#walk-forward-testing-methodology)
3. [Feature Preprocessing and Transformations](#feature-preprocessing-and-transformations)
4. [Configuration Parameters](#configuration-parameters)
5. [Understanding Results](#understanding-results)
6. [Best Practices](#best-practices)

---

## Overview and Motivation

The Trading Simulation window implements a rigorous **walk-forward testing framework** for evaluating machine learning-based trading strategies. This testing methodology is critical for developing robust, production-ready trading systems that will perform well on unseen future data.

### Why Walk-Forward Testing?

Traditional backtesting approaches suffer from **look-ahead bias** and **overfitting**. Walk-forward testing addresses these issues by:

1. **Mimicking Real-World Trading**: Models are trained on historical data and tested on future data that was never seen during training, exactly as would occur in live trading.

2. **Handling Non-Stationarity**: Financial markets constantly evolve. Walk-forward testing reveals how well your model adapts to changing market dynamics by repeatedly retraining on recent data.

3. **Avoiding Survivorship Bias**: By testing sequentially through time, you capture the full spectrum of market regimes rather than cherry-picking favorable periods.

4. **Producing Unbiased Performance Estimates**: The out-of-sample (OOS) test results from walk-forward testing provide a realistic estimate of future performance, unlike in-sample optimization results.

### The Overlap Problem

A critical consideration in walk-forward testing is the **boundary effect** between training and test sets. When:
- **Indicators** look back over many bars (e.g., 50-day moving average)
- **Targets** look ahead multiple bars (e.g., 10-bar forward return)

Cases near the train/test boundary become highly correlated. A test case immediately following the training set will have nearly identical indicator values and overlapping target values with the last training case. This creates **optimistic bias** because the model was trained on nearly identical data.

**Solution**: The `train_test_gap` parameter creates a buffer zone of excluded bars between training and test sets, eliminating this correlation and producing honest test results.

---

## Walk-Forward Testing Methodology

### How Walk-Forward Works

The system divides your historical data into a sequence of **folds**, where each fold consists of:
- A **training period** (e.g., 10,000 bars) on which the model is trained
- A **gap period** (e.g., 9 bars) that is excluded to prevent overlap bias
- A **test period** (e.g., 200 bars) on which trained model performance is evaluated

The process:

1. **Fold 1**: Train on bars 6000-16000, test on bars 16010-16210
2. **Fold 2**: Train on bars 6200-16200, test on bars 16210-16410
3. **Fold 3**: Train on bars 6400-16400, test on bars 16410-16610
4. *Continue until end of dataset...*

After all folds complete, **all test results are pooled** to compute aggregate metrics (hit rate, profit factor, Sharpe ratio, etc.). These aggregated metrics represent an unbiased estimate of model performance.

### Understanding the Parameters

The walk-forward configuration consists of:

- **Train Size** (10000): Number of bars in each training period
- **Test Size** (200): Number of bars in each test period
- **Train-Test Gap** (9): Number of bars excluded between train and test to prevent overlap
- **Fold Step** (200): How many bars to advance between folds
  - `fold_step == test_size`: Non-overlapping test periods (recommended)
  - `fold_step < test_size`: Overlapping test periods (more data, but dependent results)
  - `fold_step > test_size`: Gaps between test periods (wastes data)
- **Initial Offset** (6000): How many bars to skip at the start of dataset
- **Start/End Fold**: Which folds to run (useful for resuming interrupted tests)

### Evaluating Non-Stationarity Robustness

A key insight: **vary the test size to evaluate robustness**.

- **Short test periods** (200 bars): Model is never tested far from its training period. Good performance here may not generalize.
- **Long test periods** (2000 bars): Model must maintain performance far into the future. If performance degrades with longer test periods, your model lacks robustness and will require frequent retraining in production.

A truly robust model will maintain acceptable performance across a range of test period lengths.

---

## Feature Preprocessing and Transformations

### Why Monotonic Transformations Matter

The system applies a **hyperbolic tangent (tanh) transformation** to the target variable before training. This is a **monotonic transformation**—it preserves the ordering of values while compressing their range.

#### Motivation for Tanh Transform

Financial returns exhibit:
1. **Heavy tails**: Extreme outliers (market crashes, flash crashes) can dominate least-squares loss functions
2. **Heteroskedasticity**: Volatility clusters—periods of high volatility followed by calm
3. **Non-normal distributions**: Return distributions are leptokurtic (fat tails, peaked center)

The tanh transformation addresses these issues:

```
transformed_value = tanh((value - mean) / std_dev * scaling_factor)
```

**Benefits**:
- **Outlier Robustness**: Extreme values are compressed to range [-1, 1], preventing them from dominating the loss function
- **Preserves Ordering**: Since tanh is monotonic, the relative ranking of returns is preserved—critical for trading decisions
- **Stable Gradients**: The bounded range prevents exploding gradients during training
- **Better Threshold Selection**: The compressed scale makes it easier to find meaningful decision thresholds

#### The Scaling Factor

The `tanh_scaling_factor` parameter (default: 0.001) controls how aggressively values are compressed:

- **Smaller values** (0.0001): More aggressive compression, higher outlier resistance, but may lose signal
- **Larger values** (0.01): Less compression, more sensitive to extreme values, riskier with noisy data

The default of 0.001 provides a good balance for most financial time series.

#### Inverse Transformation

After prediction, values are transformed back to the original scale:

```
original_value = (atanh(prediction) / scaling_factor) * std_dev + mean
```

This ensures trading decisions are made on actual expected returns, not transformed values.

### Alternative: Standardization

Setting `use_standardization = true` (and `use_tanh_transform = false`) applies simple z-score normalization:

```
standardized_value = (value - mean) / std_dev
```

This is less robust to outliers but may preserve more information for well-behaved distributions.

---

## Configuration Parameters

### Model Selection

**XGBoost** is the primary supported model—a gradient boosting framework that:
- Handles non-linear relationships
- Performs automatic feature selection
- Resists overfitting through regularization
- Scales well to large datasets

### XGBoost Hyperparameters

#### Learning Parameters

**Learning Rate** (`learning_rate`, default: 0.01)
- Controls the step size at each boosting iteration
- Lower values (0.001-0.01): More conservative, better generalization, longer training
- Higher values (0.1-0.3): Faster training, higher risk of overfitting
- **Recommendation**: Start with 0.01; decrease if overfitting, increase if underfitting

**Max Depth** (`max_depth`, default: 4)
- Maximum depth of each decision tree
- Range: 3-10 for most tasks
- Deeper trees capture more complex interactions but risk overfitting
- **Recommendation**: 3-6 for financial data (typically low signal-to-noise)

**Min Child Weight** (`min_child_weight`, default: 10.0)
- Minimum sum of instance weights needed in a leaf node
- Higher values prevent learning from rare, potentially spurious patterns
- Acts as regularization
- **Recommendation**: 10-50 for noisy financial data

#### Sampling Parameters

**Subsample** (`subsample`, default: 0.8)
- Fraction of training samples used for each tree
- Range: 0.5-1.0
- Lower values increase diversity and reduce overfitting
- **Recommendation**: 0.7-0.9

**Column Sample by Tree** (`colsample_bytree`, default: 0.7)
- Fraction of features sampled for each tree
- Prevents over-reliance on a few dominant features
- **Recommendation**: 0.6-0.9

#### Regularization

**Lambda** (`lambda`, default: 2.0)
- L2 regularization on leaf weights
- Higher values shrink weights toward zero, reducing overfitting
- **Recommendation**: 1.0-5.0 for financial data

#### Training Control

**Num Boost Round** (`num_boost_round`, default: 2000)
- Maximum number of boosting iterations
- Training will stop early if validation score doesn't improve

**Early Stopping Rounds** (`early_stopping_rounds`, default: 50)
- Stop training if validation metric doesn't improve for N consecutive rounds
- Prevents overfitting and saves computation
- **Recommendation**: 30-100 depending on dataset size

**Validation Split Ratio** (`val_split_ratio`, default: 0.8)
- Fraction of training data used for training; remainder used for validation
- Validation set determines early stopping point
- **Recommendation**: 0.7-0.9 (larger training sets for smaller datasets)

#### Technical Parameters

**Tree Method** (`tree_method`, default: "hist")
- Algorithm for constructing trees
- Options: "hist" (histogram-based, fast), "exact" (slower, more accurate)
- **Recommendation**: "hist" for datasets > 10,000 samples

**Objective** (`objective`, default: "reg:squarederror")
- Loss function being optimized
- Options:
  - `reg:squarederror`: Mean squared error (default)
  - `reg:quantileerror`: Quantile regression (see below)

**Device** (`device`, default: "cuda")
- Hardware acceleration
- Will automatically fall back to CPU if CUDA unavailable

### Feature Engineering

**Feature Columns**
- List of column names from your dataset to use as predictors
- Can include technical indicators, price ratios, volume metrics, etc.
- **Best Practice**: Start with 10-30 carefully chosen features; expand cautiously

**Target Column**
- The variable you're trying to predict (e.g., "return_10", "next_bar_return")
- Should represent actionable trading signal (forward return, trend indicator, etc.)

**Feature Schedule** (Advanced)
- Allows different feature sets for different data ranges
- Format: `"startRow-endRow: feature1, feature2, ..."`
- Use case: When you add new indicators over time or want to test feature evolution
- Example:
  ```
  0-10000: sma_20, rsi_14, volume_ratio
  10000-20000: sma_20, rsi_14, volume_ratio, bollinger_width, atr_14
  ```

### Data Transformation Options

**Use Tanh Transform** (`use_tanh_transform`, default: true)
- Apply hyperbolic tangent compression to target variable
- **Strongly recommended** for financial returns (handles outliers)

**Tanh Scaling Factor** (`tanh_scaling_factor`, default: 0.001)
- Controls compression strength
- Smaller = more aggressive compression

**Use Standardization** (`use_standardization`, default: false)
- Alternative to tanh: simple z-score normalization
- Set to true only if you understand your data has no significant outliers

**Random Seed** (`random_seed`, default: 43)
- Ensures reproducibility
- Change to test robustness across different random initializations

### Trading Threshold Configuration

**Threshold Method** (`threshold_method`, default: Percentile95)
- Determines how trading thresholds are calculated
- Options:
  - **Percentile95**: Use 95th percentile of predictions as long threshold, 5th as short threshold
  - **OptimalProfitFactor**: Search for thresholds that maximize profit factor on validation set

**Percentile95** (Traditional):
- Simple, stable, interpretable
- Takes the top 5% most bullish predictions as longs, bottom 5% as shorts
- No risk of overfitting threshold selection

**OptimalProfitFactor** (Adaptive):
- Searches validation predictions to find thresholds maximizing profit factor
- More adaptive to current market conditions
- Risk: Can overfit to validation period, especially with small datasets
- **Use only when**: Validation set is large (>1000 samples) and representative

### Performance Options

**Calculate Training Profit Factor** (`calculate_training_profit_factor`, default: true)
- Computes profit factor on training data (for comparison with test)
- Disable to save computation time if not needed

**Reuse Previous Model** (`reuse_previous_model`, default: false)
- **Model Caching**: If a fold fails to train (e.g., insufficient data), reuse the model from the previous successful fold
- Useful for robustness when some folds have data quality issues
- Disable if you want to see which folds genuinely fail

---

## Understanding Results

### Per-Fold Metrics

Each fold displays:

**Data Split**
- Train Start/End: Indices of training period
- Test Start/End: Indices of test period
- Samples: Number of bars in each set

**Model Training**
- Best Iteration: When early stopping occurred
- Best Score: Validation loss at early stopping
- Model Learned: Whether model improved from initialization

**Thresholds**
- Long Threshold (95th): Prediction value above which longs are taken
- Long Threshold (Optimal): Profit-factor-maximizing long threshold
- Short Threshold (5th/Optimal): Corresponding values for shorts

**Trading Performance**
- **Signals**: Number of trades taken
- **Hit Rate**: Fraction of profitable trades
- **Profit Factor**: Sum of wins / Sum of losses
  - PF > 1.0: Profitable
  - PF > 1.5: Good
  - PF > 2.0: Excellent
  - PF > 3.0: Extraordinary (verify not overfitting)
- **Avg Return**: Mean return per trade
- **Cumulative Sum**: Running total of all trade returns in this fold

### Aggregate Metrics

After all folds complete:

**Overall Hit Rate**
- Weighted average across all folds
- Should be > 50% for a useful model
- 55-60%: Good, 60-65%: Very good, >65%: Exceptional

**Overall Profit Factor**
- Computed on pooled trades from all test periods
- The single most important metric
- Must be > 1.0 for profitability
- Account for transaction costs (reduce by ~10-20% to be conservative)

**Return Statistics**
- Mean, median, std dev of returns on signals
- Use to estimate position sizing and risk management

### Performance Plots

The cumulative profit plot shows:
- **X-axis**: Fold number (time progression)
- **Y-axis**: Cumulative sum of returns

Look for:
- **Upward slope**: System is profitable
- **Consistency**: Steady growth across folds indicates robustness
- **Drawdowns**: How deep and how long? Can you tolerate them?
- **Regime changes**: Does performance degrade in later folds (non-stationarity)?

### Trade Modes

- **Long Only**: Only positive return predictions above threshold
- **Short Only**: Only negative return predictions below threshold
- **Dual**: Both longs and shorts (sum of both strategies)

Compare these to understand:
- Is edge primarily on long or short side?
- Does long/short balance change over time?
- Are shorts profitable enough to justify the additional complexity?

---

## Best Practices

### 1. Start Conservative

- Use default hyperparameters as baseline
- Start with 10-20 well-understood features
- Use percentile-based thresholds initially
- Enable tanh transform unless you have specific reason not to

### 2. Validate Robustness

**Vary test period length**: Run multiple walk-forwards with different `test_size`:
- 100 bars: Very optimistic
- 200-500 bars: Realistic for frequent retraining
- 1000+ bars: Stress test for model robustness

If performance degrades significantly with longer test periods, your model requires frequent retraining and may not generalize well.

**Cross-check with different time periods**: Run walk-forwards on different market regimes (bull markets, bear markets, high volatility, low volatility).

### 3. Beware of Overfitting

**Red flags**:
- Training profit factor >> Test profit factor (>2x difference is concerning)
- Hit rate > 70% (possible but rare; verify rigorously)
- Perfect or near-perfect early fold performance that degrades
- Performance highly sensitive to small hyperparameter changes

**Solutions**:
- Increase regularization (lambda, min_child_weight)
- Decrease model complexity (max_depth, num_boost_round)
- Reduce feature count
- Increase train_test_gap
- Use simpler threshold method

### 4. Feature Engineering

**Quality over quantity**:
- 10 thoughtful features often outperform 100 random indicators
- Prefer features with clear economic intuition
- Test feature importance via XGBoost's built-in importance metrics
- Remove highly correlated features

**Monotonic preprocessing**:
- Always apply monotonic transforms (tanh, log, rank) rather than arbitrary scaling
- Preserves the ranking relationships that drive trading decisions
- More robust to outliers and distribution shifts

### 5. Handle Market Regime Shifts

If later folds show degraded performance:
- Consider shorter training periods (model adapts faster)
- Try feature schedule to evolve indicators over time
- May indicate genuine market regime change requiring strategy revision

### 6. Transaction Costs

Always de-rate your results:
- Assume 10-20% reduction in profit factor for slippage and commissions
- High-frequency strategies (many signals) are more affected
- If profit factor after costs is still > 1.3, system may be viable

### 7. Model Caching Strategy

**When to enable** (`reuse_previous_model = true`):
- Data has occasional gaps or quality issues
- Some folds have insufficient recent data
- You want maximum robustness to individual fold failures

**When to disable**:
- You want to identify and fix data quality issues
- You need to know which periods truly have no edge
- You're in research/development phase

### 8. Validation Split Ratio

- Larger validation sets (val_split_ratio = 0.7): More robust early stopping, but less training data
- Smaller validation sets (val_split_ratio = 0.9): More training data, but noisier early stopping signal
- **Recommendation**: 0.8 is a good default; use 0.7 if you see wild variance in best_iteration across folds

### 9. Statistical Significance

**Important caveat**: Traditional statistical tests (t-tests, etc.) are **invalidated** by the serial correlation in financial returns.

The walk-forward framework partially addresses this by:
- Testing on truly out-of-sample data
- Pooling results across multiple independent folds
- Explicitly handling the overlap problem via train_test_gap

However, for formal significance testing, consider:
- Tapered block bootstrap
- Stationary bootstrap
- Permutation tests with appropriate blocking

Or simply rely on:
- Multiple walk-forwards across different time periods
- Profit factor well above 1.5 after transaction costs
- Consistency across different hyperparameter choices

### 10. Production Deployment

Before going live:
- Run full walk-forward on latest data
- Verify profit factor > 1.5 after costs
- Understand maximum historical drawdown
- Plan retraining schedule based on test period robustness
- Monitor leading indicators of model degradation (hit rate decline, prediction variance, etc.)
- Paper trade for at least 2-4 weeks
- Start with small position sizes and scale gradually

---

## Glossary

**Fold**: One complete train-test cycle in walk-forward validation

**In-Sample (IS)**: Training data; results are optimistically biased

**Out-of-Sample (OOS)**: Test data; results are unbiased estimates

**Hit Rate**: Percentage of profitable trades

**Profit Factor**: Gross profit / Gross loss (must be > 1.0 for profitability)

**Signal**: A trade triggered by prediction exceeding threshold

**Threshold**: Decision boundary for taking trades (e.g., 95th percentile of predictions)

**Non-Stationarity**: Market behavior changing over time, violating statistical assumptions

**Overlap**: Correlation between adjacent train/test samples due to lookback/lookahead in features/targets

**Monotonic Transform**: Transformation preserving value ordering (e.g., tanh, log, rank)

**Regularization**: Techniques to reduce overfitting (L2 penalty, min_child_weight, etc.)

**Early Stopping**: Halting training when validation performance stops improving

---

## Troubleshooting

**Problem**: All folds show "Model Learned Nothing"
- **Cause**: Learning rate too high, or target has no predictable signal
- **Solution**: Decrease learning_rate to 0.001, or verify target variable is meaningful

**Problem**: Training PF >> Test PF
- **Cause**: Overfitting
- **Solution**: Increase lambda, min_child_weight; decrease max_depth; reduce features

**Problem**: Very few signals (< 1% signal rate)
- **Cause**: Thresholds too extreme
- **Solution**: Try OptimalProfitFactor threshold method, or manually adjust percentile

**Problem**: Profit factor very high (> 5.0) but suspiciously consistent
- **Cause**: Possible data leakage or lookahead bias in features
- **Solution**: Audit feature construction; ensure no future information leaks into predictors

**Problem**: Performance degrades linearly across folds
- **Cause**: Genuine market regime change, or feature degradation
- **Solution**: Shorten training window, update feature set, or accept that edge has disappeared

**Problem**: CUDA errors or crashes
- **Cause**: GPU memory issues
- **Solution**: Set device = "cpu", reduce batch sizes, or close other GPU applications

---

## Summary

The Trading Simulation window provides a professional-grade framework for developing and validating machine learning trading strategies. By combining:

- **Walk-forward testing** that mimics real-world trading
- **Careful handling of the overlap problem** to prevent bias
- **Monotonic preprocessing** to handle heavy-tailed return distributions
- **Rigorous hyperparameter control** for model tuning
- **Comprehensive performance metrics** for evaluation

You can develop strategies that have a realistic chance of performing well in production.

Remember: **out-of-sample profit factor after transaction costs is the ultimate metric**. Everything else is diagnostic. If your OOS profit factor is consistently above 1.5 across multiple test periods and market regimes, you may have a tradeable system. If not, keep refining or accept that the edge doesn't exist.

Good luck, and trade carefully.
