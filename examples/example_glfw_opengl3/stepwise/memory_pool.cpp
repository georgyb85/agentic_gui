#include "memory_pool.h"
#include <stdexcept>
#include <algorithm>

// Initialize static members
MatrixMemoryPool* GlobalMemoryPool::instance_ = nullptr;
ThreadLocalMemoryPool* GlobalMemoryPool::thread_local_instance_ = nullptr;
bool GlobalMemoryPool::initialized_ = false;

MatrixMemoryPool::MatrixMemoryPool(size_t max_rows, size_t max_cols) {
    // Allocate primary buffer for main matrices
    primary_size_ = max_rows * max_cols;
    primary_buffer_.resize(primary_size_);
    
    // Secondary buffer for intermediate results
    secondary_size_ = max_rows * max_cols / 2;
    secondary_buffer_.resize(secondary_size_);
    
    // Workspace for temporary operations
    workspace_size_ = max_rows * 50;  // Enough for vectors and small matrices
    workspace_buffer_.resize(workspace_size_);
    
    // Initialize with zeros for safety
    std::fill(primary_buffer_.begin(), primary_buffer_.end(), 0.0);
    std::fill(secondary_buffer_.begin(), secondary_buffer_.end(), 0.0);
    std::fill(workspace_buffer_.begin(), workspace_buffer_.end(), 0.0);
}

Eigen::Map<Eigen::MatrixXd> MatrixMemoryPool::get_matrix(size_t rows, size_t cols) const {
    size_t required = rows * cols;
    
    // Try primary buffer first
    if (primary_pos_ + required <= primary_size_) {
        double* ptr = const_cast<double*>(&primary_buffer_[primary_pos_]);
        primary_pos_ += required;
        return Eigen::Map<Eigen::MatrixXd>(ptr, rows, cols);
    }
    
    // Fallback to secondary buffer
    if (secondary_pos_ + required <= secondary_size_) {
        double* ptr = const_cast<double*>(&secondary_buffer_[secondary_pos_]);
        secondary_pos_ += required;
        return Eigen::Map<Eigen::MatrixXd>(ptr, rows, cols);
    }
    
    throw std::runtime_error("Memory pool exhausted. Reset or increase pool size.");
}

Eigen::Map<Eigen::VectorXd> MatrixMemoryPool::get_vector(size_t size) const {
    // Vectors are just single-column matrices
    if (primary_pos_ + size <= primary_size_) {
        double* ptr = const_cast<double*>(&primary_buffer_[primary_pos_]);
        primary_pos_ += size;
        return Eigen::Map<Eigen::VectorXd>(ptr, size);
    }
    
    if (secondary_pos_ + size <= secondary_size_) {
        double* ptr = const_cast<double*>(&secondary_buffer_[secondary_pos_]);
        secondary_pos_ += size;
        return Eigen::Map<Eigen::VectorXd>(ptr, size);
    }
    
    throw std::runtime_error("Memory pool exhausted for vector allocation.");
}

double* MatrixMemoryPool::get_raw_memory(size_t size) const {
    if (primary_pos_ + size <= primary_size_) {
        double* ptr = const_cast<double*>(&primary_buffer_[primary_pos_]);
        primary_pos_ += size;
        return ptr;
    }
    
    if (secondary_pos_ + size <= secondary_size_) {
        double* ptr = const_cast<double*>(&secondary_buffer_[secondary_pos_]);
        secondary_pos_ += size;
        return ptr;
    }
    
    throw std::runtime_error("Memory pool exhausted for raw allocation.");
}

Eigen::Map<Eigen::MatrixXd> MatrixMemoryPool::get_workspace_matrix(size_t rows, size_t cols) const {
    size_t required = rows * cols;
    if (workspace_pos_ + required <= workspace_size_) {
        double* ptr = const_cast<double*>(&workspace_buffer_[workspace_pos_]);
        workspace_pos_ += required;
        return Eigen::Map<Eigen::MatrixXd>(ptr, rows, cols);
    }
    
    throw std::runtime_error("Workspace memory exhausted.");
}

Eigen::Map<Eigen::VectorXd> MatrixMemoryPool::get_workspace_vector(size_t size) const {
    if (workspace_pos_ + size <= workspace_size_) {
        double* ptr = const_cast<double*>(&workspace_buffer_[workspace_pos_]);
        workspace_pos_ += size;
        return Eigen::Map<Eigen::VectorXd>(ptr, size);
    }
    
    throw std::runtime_error("Workspace memory exhausted.");
}

void MatrixMemoryPool::ensure_capacity(size_t required_size) {
    if (required_size > primary_size_) {
        primary_size_ = required_size * 1.5;  // Add some headroom
        primary_buffer_.resize(primary_size_);
        primary_pos_ = 0;
    }
}

// ThreadLocalMemoryPool implementation
ThreadLocalMemoryPool::ThreadLocalMemoryPool(size_t max_rows, size_t max_cols) {
    #ifdef _OPENMP
    int num_threads = omp_get_max_threads();
    thread_pools_.reserve(MAX_THREADS);
    for (int i = 0; i < std::min(num_threads, MAX_THREADS); ++i) {
        thread_pools_.push_back(new MatrixMemoryPool(max_rows, max_cols));
    }
    #else
    single_pool_ = new MatrixMemoryPool(max_rows, max_cols);
    #endif
}

ThreadLocalMemoryPool::~ThreadLocalMemoryPool() {
    #ifdef _OPENMP
    for (auto* pool : thread_pools_) {
        delete pool;
    }
    thread_pools_.clear();
    #else
    delete single_pool_;
    single_pool_ = nullptr;
    #endif
}

MatrixMemoryPool& ThreadLocalMemoryPool::get_pool() const {
    #ifdef _OPENMP
    int thread_id = omp_get_thread_num();
    if (thread_id < thread_pools_.size()) {
        return *thread_pools_[thread_id];
    }
    // Fallback to first pool if thread ID exceeds our array
    return *thread_pools_[0];
    #else
    return *single_pool_;
    #endif
}

void ThreadLocalMemoryPool::reset_all() const {
    #ifdef _OPENMP
    for (auto* pool : thread_pools_) {
        pool->reset();
    }
    #else
    single_pool_->reset();
    #endif
}

// GlobalMemoryPool implementation
MatrixMemoryPool& GlobalMemoryPool::get_main_pool() {
    if (!instance_) {
        instance_ = new MatrixMemoryPool();
    }
    return *instance_;
}

ThreadLocalMemoryPool& GlobalMemoryPool::get_thread_pools() {
    if (!thread_local_instance_) {
        thread_local_instance_ = new ThreadLocalMemoryPool();
    }
    return *thread_local_instance_;
}

void GlobalMemoryPool::initialize(size_t max_rows, size_t max_cols) {
    if (!initialized_) {
        instance_ = new MatrixMemoryPool(max_rows, max_cols);
        thread_local_instance_ = new ThreadLocalMemoryPool(max_rows / 10, max_cols);
        initialized_ = true;
    }
}

void GlobalMemoryPool::cleanup() {
    // DO NOT USE - causes crashes at program exit due to static destruction order
    // Memory will be reclaimed by OS at program termination
}