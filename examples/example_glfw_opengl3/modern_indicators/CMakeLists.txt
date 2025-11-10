cmake_minimum_required(VERSION 3.20)
project(tssb_modern_indicators LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tssb_modern_indicators
    src/IndicatorEngine.cpp
    src/IndicatorId.cpp
    src/MathUtils.cpp
    src/SingleIndicatorLibrary.cpp
    src/MultiIndicatorLibrary.cpp
    src/IndicatorConfig.cpp
    src/TaskExecutor.cpp
    src/helpers/InformationTheory.cpp
    src/helpers/Fti.cpp
    src/helpers/Janus.cpp
    src/helpers/WaveletHelpers.cpp
    src/validation/DataParsers.cpp)

target_include_directories(tssb_modern_indicators
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(tssb_modern_indicators PUBLIC cxx_std_20)

add_executable(indicator_verify
    tools/verify_indicators.cpp)

target_link_libraries(indicator_verify
    PRIVATE
        tssb_modern_indicators)

add_executable(validate_wavelets
    tools/validate_wavelets.cpp)

target_link_libraries(validate_wavelets
    PRIVATE
        tssb_modern_indicators)

add_executable(test_ma_diff
    tools/test_ma_diff.cpp)

target_link_libraries(test_ma_diff
    PRIVATE
        tssb_modern_indicators)

add_executable(test_compression
    tools/test_compression.cpp)

target_link_libraries(test_compression
    PRIVATE
        tssb_modern_indicators)

add_executable(test_dynamic_lookback
    tools/test_dynamic_lookback.cpp)

target_link_libraries(test_dynamic_lookback
    PRIVATE
        tssb_modern_indicators)

add_executable(verify_alignment
    tools/verify_alignment.cpp)

target_link_libraries(verify_alignment
    PRIVATE
        tssb_modern_indicators)

add_executable(test_growing_lookback
    tools/test_growing_lookback.cpp)

target_link_libraries(test_growing_lookback
    PRIVATE
        tssb_modern_indicators)

add_executable(diagnose_compression
    tools/diagnose_compression.cpp)

target_link_libraries(diagnose_compression
    PRIVATE
        tssb_modern_indicators)

add_executable(test_csv_relative_lookback
    tools/test_csv_relative_lookback.cpp)

target_link_libraries(test_csv_relative_lookback
    PRIVATE
        tssb_modern_indicators)

add_executable(check_zeros
    tools/check_zeros.cpp)

target_link_libraries(check_zeros
    PRIVATE
        tssb_modern_indicators)

add_executable(test_quartile_methods
    tools/test_quartile_methods.cpp)

target_link_libraries(test_quartile_methods
    PRIVATE
        tssb_modern_indicators)

add_executable(test_full_history
    tools/test_full_history.cpp)

target_link_libraries(test_full_history
    PRIVATE
        tssb_modern_indicators)

add_executable(test_periodic_update
    tools/test_periodic_update.cpp)

target_link_libraries(test_periodic_update
    PRIVATE
        tssb_modern_indicators)

add_executable(test_c_values
    tools/test_c_values.cpp)

target_link_libraries(test_c_values
    PRIVATE
        tssb_modern_indicators)

add_executable(test_rsi
    tools/test_rsi.cpp)

target_link_libraries(test_rsi
    PRIVATE
        tssb_modern_indicators)

add_executable(test_detrended_rsi
    tools/test_detrended_rsi.cpp)

target_link_libraries(test_detrended_rsi
    PRIVATE
        tssb_modern_indicators)

add_executable(test_correct_ma_diff
    tools/test_correct_ma_diff.cpp)

target_link_libraries(test_correct_ma_diff
    PRIVATE
        tssb_modern_indicators)

add_executable(validate_all_indicators
    tools/validate_all_indicators.cpp)

target_link_libraries(validate_all_indicators
    PRIVATE
        tssb_modern_indicators)

add_executable(test_all_implemented
    tools/test_all_implemented.cpp)

target_link_libraries(test_all_implemented
    PRIVATE
        tssb_modern_indicators)

add_executable(diagnose_trend
    tools/diagnose_trend.cpp)

target_link_libraries(diagnose_trend
    PRIVATE
        tssb_modern_indicators)

add_executable(test_trend_params
    tools/test_trend_params.cpp)

target_link_libraries(test_trend_params
    PRIVATE
        tssb_modern_indicators)

add_executable(test_all_trend
    tools/test_all_trend.cpp)

target_link_libraries(test_all_trend
    PRIVATE
        tssb_modern_indicators)

add_executable(test_trend_c_values
    tools/test_trend_c_values.cpp)

target_link_libraries(test_trend_c_values
    PRIVATE
        tssb_modern_indicators)

add_executable(test_cubic_c_values
    tools/test_cubic_c_values.cpp)

target_link_libraries(test_cubic_c_values
    PRIVATE
        tssb_modern_indicators)

add_executable(diagnose_single_bar
    tools/diagnose_single_bar.cpp)

target_link_libraries(diagnose_single_bar
    PRIVATE
        tssb_modern_indicators)

add_executable(replicate_tssb_trend
    tools/replicate_tssb_trend.cpp)

target_link_libraries(replicate_tssb_trend
    PRIVATE
        tssb_modern_indicators)

add_executable(test_swapped_params
    tools/test_swapped_params.cpp)

target_link_libraries(test_swapped_params
    PRIVATE
        tssb_modern_indicators)

add_executable(find_correct_params
    tools/find_correct_params.cpp)

target_link_libraries(find_correct_params
    PRIVATE
        tssb_modern_indicators)

add_executable(find_all_trend_params
    tools/find_all_trend_params.cpp)

target_link_libraries(find_all_trend_params
    PRIVATE
        tssb_modern_indicators)

add_executable(validate_trend_with_found_params
    tools/validate_trend_with_found_params.cpp)

target_link_libraries(validate_trend_with_found_params
    PRIVATE
        tssb_modern_indicators)

add_executable(test_params_multiple_bars
    tools/test_params_multiple_bars.cpp)

target_link_libraries(test_params_multiple_bars
    PRIVATE
        tssb_modern_indicators)

add_executable(test_trend_btc25_3 tools/test_trend_btc25_3.cpp)
target_link_libraries(test_trend_btc25_3 PRIVATE tssb_modern_indicators)

add_executable(diagnose_trend_detailed tools/diagnose_trend_detailed.cpp)
target_link_libraries(diagnose_trend_detailed PRIVATE tssb_modern_indicators)

add_executable(test_rsq_formula tools/test_rsq_formula.cpp)
target_link_libraries(test_rsq_formula PRIVATE tssb_modern_indicators)

add_executable(test_all_formulas tools/test_all_formulas.cpp)
target_link_libraries(test_all_formulas PRIVATE tssb_modern_indicators)

add_executable(find_divergence tools/find_divergence.cpp)
target_link_libraries(find_divergence PRIVATE tssb_modern_indicators)

add_executable(check_bar_100 tools/check_bar_100.cpp)
target_link_libraries(check_bar_100 PRIVATE tssb_modern_indicators)

add_executable(verify_exact_alignment tools/verify_exact_alignment.cpp)
target_link_libraries(verify_exact_alignment PRIVATE tssb_modern_indicators)

add_executable(diagnose_adx tools/diagnose_adx.cpp)
target_link_libraries(diagnose_adx PRIVATE tssb_modern_indicators)

add_executable(check_adx_scaling tools/check_adx_scaling.cpp)
target_link_libraries(check_adx_scaling PRIVATE tssb_modern_indicators)

add_executable(verify_working_indicators tools/verify_working_indicators.cpp)
target_link_libraries(verify_working_indicators PRIVATE tssb_modern_indicators)

add_executable(diagnose_adx_detail tools/diagnose_adx_detail.cpp)
target_link_libraries(diagnose_adx_detail PRIVATE tssb_modern_indicators)

add_executable(trace_adx_decay tools/trace_adx_decay.cpp)
target_link_libraries(trace_adx_decay PRIVATE tssb_modern_indicators)

add_executable(test_adx_exact_tssb tools/test_adx_exact_tssb.cpp)
target_link_libraries(test_adx_exact_tssb PRIVATE tssb_modern_indicators)

add_executable(export_computed_indicators tools/export_computed_indicators.cpp)
target_link_libraries(export_computed_indicators PRIVATE tssb_modern_indicators)

add_executable(test_adx_variations tools/test_adx_variations.cpp)
target_link_libraries(test_adx_variations PRIVATE tssb_modern_indicators)

add_executable(test_smoothing_factors tools/test_smoothing_factors.cpp)
target_link_libraries(test_smoothing_factors PRIVATE tssb_modern_indicators)

add_executable(test_all_adx_variants tools/test_all_adx_variants.cpp)
target_link_libraries(test_all_adx_variants PRIVATE tssb_modern_indicators)

add_executable(test_adx tools/test_adx.cpp)
target_link_libraries(test_adx PRIVATE tssb_modern_indicators)

add_executable(test_adx_both_methods tools/test_adx_both_methods.cpp)
target_link_libraries(test_adx_both_methods PRIVATE tssb_modern_indicators)

add_executable(test_trend_variants tools/test_trend_variants.cpp)
target_link_libraries(test_trend_variants PRIVATE tssb_modern_indicators)

add_executable(test_trend_comprehensive tools/test_trend_comprehensive.cpp)
target_link_libraries(test_trend_comprehensive PRIVATE tssb_modern_indicators)

add_executable(test_trend_final tools/test_trend_final.cpp)
target_link_libraries(test_trend_final PRIVATE tssb_modern_indicators)

add_executable(test_bollinger_cmma tools/test_bollinger_cmma.cpp)
target_link_libraries(test_bollinger_cmma PRIVATE tssb_modern_indicators)

add_executable(test_cmma_compression tools/test_cmma_compression.cpp)
target_link_libraries(test_cmma_compression PRIVATE tssb_modern_indicators)

add_executable(export_cmma_indicators tools/export_cmma_indicators.cpp)
target_link_libraries(export_cmma_indicators PRIVATE tssb_modern_indicators)

add_executable(diagnose_cmma_detail tools/diagnose_cmma_detail.cpp)
target_link_libraries(diagnose_cmma_detail PRIVATE tssb_modern_indicators)

add_executable(test_cmma_denom_variants tools/test_cmma_denom_variants.cpp)
target_link_libraries(test_cmma_denom_variants PRIVATE tssb_modern_indicators)

add_executable(test_all_cmma_variants tools/test_all_cmma_variants.cpp)
target_link_libraries(test_all_cmma_variants PRIVATE tssb_modern_indicators)

add_executable(test_cmma_scaling_fine tools/test_cmma_scaling_fine.cpp)
target_link_libraries(test_cmma_scaling_fine PRIVATE tssb_modern_indicators)

add_executable(verify_cmma_formula tools/verify_cmma_formula.cpp)
target_link_libraries(verify_cmma_formula PRIVATE tssb_modern_indicators)

add_executable(test_cmma_c_pattern tools/test_cmma_c_pattern.cpp)
target_link_libraries(test_cmma_c_pattern PRIVATE tssb_modern_indicators)

add_executable(test_cmma_postscale_linear tools/test_cmma_postscale_linear.cpp)
target_link_libraries(test_cmma_postscale_linear PRIVATE tssb_modern_indicators)

add_executable(final_cmma_diagnostic tools/final_cmma_diagnostic.cpp)
target_link_libraries(final_cmma_diagnostic PRIVATE tssb_modern_indicators)

add_executable(test_cmma_compression_constant tools/test_cmma_compression_constant.cpp)
target_link_libraries(test_cmma_compression_constant PRIVATE tssb_modern_indicators)

add_executable(test_cmma_exact_book tools/test_cmma_exact_book.cpp)
target_link_libraries(test_cmma_exact_book PRIVATE tssb_modern_indicators)

add_executable(test_cmma_formula_with_constant tools/test_cmma_formula_with_constant.cpp)
target_link_libraries(test_cmma_formula_with_constant PRIVATE tssb_modern_indicators)

add_executable(test_cmma_optimal_c_per_lookback tools/test_cmma_optimal_c_per_lookback.cpp)
target_link_libraries(test_cmma_optimal_c_per_lookback PRIVATE tssb_modern_indicators)

add_executable(diagnose_cmma_zscore tools/diagnose_cmma_zscore.cpp)
target_link_libraries(diagnose_cmma_zscore PRIVATE tssb_modern_indicators)

add_executable(verify_global_constant tools/verify_global_constant.cpp)
target_link_libraries(verify_global_constant PRIVATE tssb_modern_indicators)

add_executable(test_cmma_both_versions tools/test_cmma_both_versions.cpp)
target_link_libraries(test_cmma_both_versions PRIVATE tssb_modern_indicators)

add_executable(test_volume_indicators tools/test_volume_indicators.cpp)
target_link_libraries(test_volume_indicators PRIVATE tssb_modern_indicators)

add_executable(diagnose_volume_momentum tools/diagnose_volume_momentum.cpp)
target_link_libraries(diagnose_volume_momentum PRIVATE tssb_modern_indicators)

add_executable(test_volume_momentum_both_modes tools/test_volume_momentum_both_modes.cpp)
target_link_libraries(test_volume_momentum_both_modes PRIVATE tssb_modern_indicators)

add_executable(test_hit_or_miss tools/test_hit_or_miss.cpp)
target_link_libraries(test_hit_or_miss PRIVATE tssb_modern_indicators)

add_executable(trace_hit_or_miss tools/trace_hit_or_miss.cpp)
target_link_libraries(trace_hit_or_miss PRIVATE tssb_modern_indicators)

add_executable(export_hit_or_miss tools/export_hit_or_miss.cpp)
target_link_libraries(export_hit_or_miss PRIVATE tssb_modern_indicators)

add_executable(test_hit_or_miss_both_orders tools/test_hit_or_miss_both_orders.cpp)
target_link_libraries(test_hit_or_miss_both_orders PRIVATE tssb_modern_indicators)

add_executable(test_aroon tools/test_aroon.cpp)
target_link_libraries(test_aroon PRIVATE tssb_modern_indicators)

add_executable(test_fti tools/test_fti.cpp)
target_link_libraries(test_fti PRIVATE tssb_modern_indicators)

add_executable(test_wavelets tools/test_wavelets.cpp)
target_link_libraries(test_wavelets PRIVATE tssb_modern_indicators)

add_executable(find_wavelet_compression tools/find_wavelet_compression.cpp)
target_link_libraries(find_wavelet_compression PRIVATE tssb_modern_indicators)

add_executable(test_scaling_vs_normalization tools/test_scaling_vs_normalization.cpp)
target_link_libraries(test_scaling_vs_normalization PRIVATE tssb_modern_indicators)

add_executable(diagnose_wavelet_raw tools/diagnose_wavelet_raw.cpp)
target_link_libraries(diagnose_wavelet_raw PRIVATE tssb_modern_indicators)

add_executable(test_dtr_rsi tools/test_dtr_rsi.cpp)
target_link_libraries(test_dtr_rsi PRIVATE tssb_modern_indicators)

add_executable(test_missing_indicators tools/test_missing_indicators.cpp)
target_link_libraries(test_missing_indicators PRIVATE tssb_modern_indicators)

add_executable(validate_new_indicators tools/validate_new_indicators.cpp)
target_link_libraries(validate_new_indicators PRIVATE tssb_modern_indicators)

add_executable(compute_indicators tools/compute_indicators.cpp)
target_link_libraries(compute_indicators PRIVATE tssb_modern_indicators)

add_executable(validate_pco tools/validate_pco.cpp)
target_link_libraries(validate_pco PRIVATE tssb_modern_indicators)
