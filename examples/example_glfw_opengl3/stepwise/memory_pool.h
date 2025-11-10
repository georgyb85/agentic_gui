#pragma once

#include <vector>
#include <memory>
#include <Eigen/Dense>
#ifdef _OPENMP
#include <omp.h>
#endif

// Memory pool for efficient matrix allocation and reuse
// Eliminates repeated allocations in hot loops
class MatrixMemoryPool {
private:
    // Pre-allocated memory buffers
    std::vector<double> primary_buffer_;
    std::vector<double> secondary_buffer_;
    std::vector<double> workspace_buffer_;
    
    // Current allocation positions
    mutable size_t primary_pos_ = 0;
    mutable size_t secondary_pos_ = 0;
    mutable size_t workspace_pos_ = 0;
    
    // Buffer sizes
    size_t primary_size_;
    size_t secondary_size_;
    size_t workspace_size_;
    
public:
    // Constructor with estimated sizes
    MatrixMemoryPool(size_t max_rows = 100000, size_t max_cols = 250);
    
    // Reset allocation positions (call before each major operation)
    void reset() const {
        primary_pos_ = 0;
        secondary_pos_ = 0;
        workspace_pos_ = 0;
    }
    
    // Get a mapped matrix from the pool
    Eigen::Map<Eigen::MatrixXd> get_matrix(size_t rows, size_t cols) const;
    
    // Get a mapped vector from the pool
    Eigen::Map<Eigen::VectorXd> get_vector(size_t size) const;
    
    // Get raw memory for custom use
    double* get_raw_memory(size_t size) const;
    
    // Get workspace memory (separate pool for temporary operations)
    Eigen::Map<Eigen::MatrixXd> get_workspace_matrix(size_t rows, size_t cols) const;
    Eigen::Map<Eigen::VectorXd> get_workspace_vector(size_t size) const;
    
    // Check if pool can accommodate requested size
    bool can_accommodate(size_t rows, size_t cols) const {
        return (rows * cols) <= (primary_size_ - primary_pos_);
    }
    
    // Resize pool if needed (expensive, avoid in hot loops)
    void ensure_capacity(size_t required_size);
};

// Thread-local memory pool for parallel regions
class ThreadLocalMemoryPool {
private:
    // Each thread gets its own pool
    #ifdef _OPENMP
    static constexpr int MAX_THREADS = 128;
    std::vector<MatrixMemoryPool*> thread_pools_;
    #else
    MatrixMemoryPool* single_pool_;
    #endif
    
public:
    ThreadLocalMemoryPool(size_t max_rows = 10000, size_t max_cols = 250);
    ~ThreadLocalMemoryPool();
    
    // Get pool for current thread
    MatrixMemoryPool& get_pool() const;
    
    // Reset all pools
    void reset_all() const;
};

// Global singleton for application-wide memory pool
class GlobalMemoryPool {
private:
    static MatrixMemoryPool* instance_;
    static ThreadLocalMemoryPool* thread_local_instance_;
    static bool initialized_;
    
public:
    static MatrixMemoryPool& get_main_pool();
    static ThreadLocalMemoryPool& get_thread_pools();
    
    // Initialize with custom sizes
    static void initialize(size_t max_rows = 100000, size_t max_cols = 250);
    
    // Check if initialized
    static bool is_initialized() { return initialized_; }
    
    // Clean up (DO NOT CALL - causes crashes at program exit)
    static void cleanup();
};