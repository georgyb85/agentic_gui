// Test different XGBoost cache settings
void testXGBoostCacheSettings(BoosterHandle booster) {
    // Disable XGBoost's internal caching
    XGBoosterSetParam(booster, "cache_opt", "0");  // Disable cache optimization
    
    // For GPU, control memory usage
    XGBoosterSetParam(booster, "gpu_page_size", "0");  // Disable GPU paging
    
    // Alternative: Use external memory mode
    XGBoosterSetParam(booster, "max_bin", "256");  // Reduce memory usage
    XGBoosterSetParam(booster, "tree_method", "approx");  // Use approximate algorithm
    
    // For DMatrix creation, use these flags:
    // Set missing value to NaN to avoid scanning
    float missing = std::numeric_limits<float>::quiet_NaN();
    
    // Create DMatrix with specific cache settings
    // Unfortunately, C API doesn't expose all cache controls
}