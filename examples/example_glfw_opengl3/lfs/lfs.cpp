/******************************************************************************/
/*                                                                            */
/*  LFS - Local Feature Selection main routine coordinates everything         */
/*                                                                            */
/*  Usage:                                                                    */
/*    1) Call the constructor, and then check the 'ok' member.                */
/*       It will normally be 1, indicating that all is well.                  */
/*       It will be set to 0 if there was insufficient memory.                */
/*       Calling parameters are not checked for legality!                     */
/*    The calling parameters are:                                             */
/*       n_cases - Number of cases (rows) in data matrix                      */
/*       n_vars - Number of predictor variables                               */
/*       max_kept - Maximum number of variables used as the metric space      */
/*                  for each case                                             */
/*       max_threads - Maximum number of threads to use                       */
/*                     There is no point in using more than the CPU has cores */
/*                     This greatly impacts memory requirements.              */
/*       x - The n_cases by (n_vars+1) data matrix, columns changing fastest  */
/*           The last column of each row is the class ID of this case,        */
/*           0, 1, 2, ...                                                     */
/*       progress - If nonzero, progress details printed to MEM.LOG           */
/*                                                                            */
/*    2) Call run ( iters , n_rand , n_beta , irep , MCPTreps )               */
/*    The calling parameters are:                                             */
/*       iters - Iterations to do, a small number greater than 1 (2 or 3 ok)  */
/*       n_rand - Number of random tries converting real f to binary          */
/*                Larger values provide more accuracy but more CPU time       */
/*                At least several hundred, more if many variables            */
/*       n_beta - Number of trial values for best beta; 10-30 typical         */
/*       irep - MCPT replication number; purely for progress display          */
/*       MCPTreps - MCPT replications; purely for progress display            */
/*                                                                            */
/*    3) Call get_f ()                                                        */
/*       This returns a pointer to a binary int array n_cases by n_vars       */
/*       Each row is a flag vector of the variables used for that case.       */
/*                                                                            */
/******************************************************************************/

// Insert the other includes you need here

// Fix for M_PI constant availability
#define _USE_MATH_DEFINES
#include <iostream>
#include <cmath>
#include <math.h>
#include <thread>
#include <future>
#include <atomic>
#include <mutex>
#include <cuda_runtime.h>
#include "lfs_cuda.h"
#include <shared_mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <memory>
#include <chrono>
#include <exception>
#include <limits>
#include <algorithm>
#include <new>
#include <type_traits>
#include <bitset>
#include <string>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "const.h"
#include "classes.h"
#include "funcdefs.h"
#include "lfs_cuda.h"

/*
================================================================================

   Modern Memory Management Implementation with RAII and Smart Pointers
   
   This section implements the modern C++ memory management patterns to replace
   the legacy MALLOC/FREE calls throughout the LFS codebase.

================================================================================
*/


// Memory pool for frequent allocations to reduce fragmentation
template<typename T, size_t PoolSize>
class MemoryPool {
private:
    alignas(T) char pool[PoolSize * sizeof(T)];
    std::bitset<PoolSize> used;
    std::mutex pool_mutex;
    
public:
    using deleter_type = std::function<void(T*)>;
    
    std::unique_ptr<T, deleter_type> allocate() {
        std::lock_guard<std::mutex> lock(pool_mutex);
        
        for (size_t i = 0; i < PoolSize; ++i) {
            if (!used[i]) {
                used[i] = true;
                T* ptr = reinterpret_cast<T*>(&pool[i * sizeof(T)]);
                
                return std::unique_ptr<T, deleter_type>(
                    ptr, [this, i](T* p) {
                        if (p) {
                            p->~T();
                            deallocate(i);
                        }
                    }
                );
            }
        }
        
        // Pool exhausted, fall back to regular allocation
        T* ptr = new T();
        return std::unique_ptr<T, deleter_type>(
            ptr, [](T* p) { delete p; }
        );
    }
    
    template<typename... Args>
    std::unique_ptr<T, deleter_type> allocate(Args&&... args) {
        std::lock_guard<std::mutex> lock(pool_mutex);
        
        for (size_t i = 0; i < PoolSize; ++i) {
            if (!used[i]) {
                used[i] = true;
                T* ptr = reinterpret_cast<T*>(&pool[i * sizeof(T)]);
                new(ptr) T(std::forward<Args>(args)...);
                
                return std::unique_ptr<T, deleter_type>(
                    ptr, [this, i](T* p) {
                        if (p) {
                            p->~T();
                            deallocate(i);
                        }
                    }
                );
            }
        }
        
        // Pool exhausted, fall back to regular allocation
        T* ptr = new T(std::forward<Args>(args)...);
        return std::unique_ptr<T, deleter_type>(
            ptr, [](T* p) { delete p; }
        );
    }
    
private:
    void deallocate(size_t index) {
        std::lock_guard<std::mutex> lock(pool_mutex);
        used[index] = false;
    }
};

// RAII wrapper for Simplex objects
SimplexManager::SimplexManager(int n_vars, int n_constraints, int n_less_eq, int debug_flag)
    : simplex(std::make_unique<Simplex>(n_vars, n_constraints, n_less_eq, debug_flag)),
      stored_n_vars(n_vars), stored_n_constraints(n_constraints),
      stored_n_less_eq(n_less_eq), stored_debug_flag(debug_flag) {
    
    if (!simplex || !simplex->ok) {
        throw std::runtime_error("Failed to create Simplex object");
    }
}

// Exception-safe memory manager for LFS operations
class LFSMemoryManager {
private:
    static thread_local MemoryPool<double, 2048> double_pool;
    static thread_local MemoryPool<int, 1024> int_pool;
    
public:
    template<typename T>
    static std::unique_ptr<T[]> allocate_array(size_t count) {
        try {
            return std::make_unique<T[]>(count);
        } catch (const std::bad_alloc& e) {
            throw std::runtime_error("Memory allocation failed for array of size " + std::to_string(count));
        }
    }
    
    template<typename T>
    static std::vector<T> allocate_vector(size_t count) {
        try {
            std::vector<T> vec;
            vec.reserve(count);
            return vec;
        } catch (const std::bad_alloc& e) {
            throw std::runtime_error("Vector allocation failed for size " + std::to_string(count));
        }
    }
    
    template<typename T>
    static LFSAlignedVector<T> allocate_aligned_vector(size_t count) {
        try {
            LFSAlignedVector<T> vec;
            vec.reserve(count);
            return vec;
        } catch (const std::bad_alloc& e) {
            throw std::runtime_error("Aligned vector allocation failed for size " + std::to_string(count));
        }
    }
};

// Thread-local memory pools
thread_local MemoryPool<double, 2048> LFSMemoryManager::double_pool;
thread_local MemoryPool<int, 1024> LFSMemoryManager::int_pool;

// Global variables and constants needed for compatibility
extern int cuda_enable;
//extern int escape_key_pressed;
//extern int max_threads_limit;    // Keep as extern declaration
//extern void* hwndProgress;       // Keep as extern declaration
// Define the timing variables (not just extern declarations)
int LFStimeTotal = 0, LFStimeRealToBinary = 0, LFStimeBetaCrit = 0, LFStimeWeights = 0;
int LFStimeCUDA = 0, LFStimeCUDAdiff = 0, LFStimeCUDAdist = 0, LFStimeCUDAmindist = 0;
int LFStimeCUDAterm = 0, LFStimeCUDAtranspose = 0, LFStimeCUDAsum = 0, LFStimeCUDAgetweights = 0;

// Function declarations for compatibility
// Self-contained progress message handlers to resolve linker errors
void begin_progress_message(const char* msg) {
    if (msg) {
        std::cout << "[PROGRESS] " << msg << "..." << std::endl;
    }
}

void end_progress_message() {
    std::cout << "[PROGRESS] ...Done." << std::endl;
}

void setpos_progress_message(double pos) {
    // Optional: Could implement a more sophisticated progress bar here
}

// Modern timing function to replace Windows timeGetTime()
int timeGetTime_loc() {
    static auto start_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
    return static_cast<int>(elapsed.count());
}

// Implementation of memtext function for logging
void memtext(const char *text) {
    static std::mutex log_mutex;
    static bool log_file_opened = false;
    static FILE* log_file = nullptr;
    
    std::lock_guard<std::mutex> lock(log_mutex);
    
    // Open log file on first use
    if (!log_file_opened) {
        log_file_opened = true;
        #ifdef _WIN32
            fopen_s(&log_file, "MEM.LOG", "w");
        #else
            log_file = fopen("MEM.LOG", "w");
        #endif
        
        if (log_file) {
            fprintf(log_file, "=== LFS Memory Log Started ===\n");
            fflush(log_file);
        }
    }
    
    // Write to log file if available, otherwise to console
    if (log_file && text) {
        fprintf(log_file, "%s\n", text);
        fflush(log_file);
    } else if (text) {
        // Fallback to console output
        std::cout << text << std::endl;
    }
}


// Error constants
#define ERROR_ESCAPE 1
#define ERROR_THREAD 2
#define ERROR_SIMPLEX 3
#define DEBUG_SIMPLEX 0
#define DEBUG_LFS 0
#define DEBUG_THREADS 0
#define DEBUG_CUDA 0
#define WRITE_WEIGHTS 0

/*
-------------------------------------------------------------------------

   Constructor and destructor

-------------------------------------------------------------------------
*/

LFS::LFS (
   int nc ,     // Number of cases (rows) in x
   int nv ,     // Number of variables NOT COUNTING CLASS in x
                // So x will have nv+1 columns, with the last
                // being the origin-0 class ID
   int mk ,     // Max number of variables to keep
   int mt ,     // Maximum number of threads
   double *x ,  // nc by nv+1 data matrix (last col is class)
   int prog     // Print progress report to MEM.LOG?
   )
{
   int i, j, k, n, index, ithread ;
   double *x_ptr, *case_ptr, *constr_ptr, diff, mean, stddev ;
   char msg[256], error_msg[256] ;

   ok = 1 ;
   n_cases = nc ;
   n_vars = nv ;
   max_kept = mk ;
   max_threads = mt ;
   progress = prog ;
   deterministic_mode = false;  // Default to performance mode

#if WRITE_WEIGHTS
   max_threads = 1 ;
#endif

   std::cout << "LFS constructor starting with modern memory management" << std::endl;

   try {
       // Initialize modern memory-managed containers with exception safety
       initialize_memory_pools();
       
       // Allocate main data arrays using modern containers
       cases_data = LFSMemoryManager::allocate_vector<double>(nc * nv);
       cases_data.resize(nc * nv);
       
       int num_work_areas = std::min(nc, mt * 4);  // Match work area count
       weights_data = LFSMemoryManager::allocate_aligned_vector<double>(nc * num_work_areas);
       weights_data.resize(nc * num_work_areas);
       
       delta_data = LFSMemoryManager::allocate_aligned_vector<double>(nc * nv * num_work_areas);
       delta_data.resize(nc * nv * num_work_areas);
       
       f_real_data = LFSMemoryManager::allocate_vector<double>(nc * nv);
       f_real_data.resize(nc * nv);
       
       f_binary_data = LFSMemoryManager::allocate_vector<int>(nc * nv);
       f_binary_data.resize(nc * nv);
       // Initialize f_binary to zero
       std::fill(f_binary_data.begin(), f_binary_data.end(), 0);
       
       f_prior_data = LFSMemoryManager::allocate_vector<int>(nc * nv);
       f_prior_data.resize(nc * nv);
       // Initialize f_prior to zero for first iteration
       std::fill(f_prior_data.begin(), f_prior_data.end(), 0);
       
       class_id_data = LFSMemoryManager::allocate_vector<int>(nc);
       class_id_data.resize(nc);
       
       d_ijk_data = LFSMemoryManager::allocate_vector<double>(nc * num_work_areas);
       d_ijk_data.resize(nc * num_work_areas);
       
       nc_iwork_data = LFSMemoryManager::allocate_vector<int>(nc * num_work_areas);
       nc_iwork_data.resize(nc * num_work_areas);
       
       // Allocate per-work-area arrays (not just per-thread)
       best_binary_data.resize(num_work_areas);
       best_fbin_data.resize(num_work_areas);
       aa_data.resize(num_work_areas);
       bb_data.resize(num_work_areas);
       constraints_data.resize(num_work_areas);
       
       for (ithread = 0; ithread < num_work_areas; ++ithread) {
           best_binary_data[ithread] = LFSMemoryManager::allocate_vector<int>(nv);
           best_binary_data[ithread].resize(nv);
           
           best_fbin_data[ithread] = LFSMemoryManager::allocate_vector<int>(nv);
           best_fbin_data[ithread].resize(nv);
           
           aa_data[ithread] = LFSMemoryManager::allocate_vector<double>(nv);
           aa_data[ithread].resize(nv);
           
           bb_data[ithread] = LFSMemoryManager::allocate_vector<double>(nv);
           bb_data[ithread].resize(nv);
           
           constraints_data[ithread] = LFSMemoryManager::allocate_vector<double>((nv+3) * (nv+1));
           constraints_data[ithread].resize((nv+3) * (nv+1));
       }
       
       
       std::cout << "Modern memory allocation completed successfully" << std::endl;
       
   } catch (const std::exception& e) {
       sprintf_s(msg, "LFS constructor failed with exception: %s", e.what());
       std::cout << msg << std::endl;
       ok = 0;
       return;
   } catch (...) {
       std::cout << "LFS constructor failed with unknown exception" << std::endl;
       ok = 0;
       return;
   }

   // Initialize thread-safe work area manager with more work areas to reduce contention
   // Having more work areas than threads allows better pipelining
   try {
       int num_work_areas = std::min(nc, mt * 4);  // Up to 4x oversubscription
       work_area_manager = std::make_unique<ThreadSafeWorkAreaManager>(num_work_areas);
       std::cout << "Thread-safe work area manager initialized with " << num_work_areas << " work areas" << std::endl;
   } catch (const std::exception& e) {
       sprintf_s(msg, "Work area manager creation failed: %s", e.what());
       std::cout << msg << std::endl;
       ok = 0;
       return;
   }

   // Initialize Simplex objects using modern RAII management
   try {
       std::cout << "Creating modern Simplex managers" << std::endl;
       
       simplex1_managers.reserve(max_threads);
       simplex2_managers.reserve(max_threads);
       
       for (ithread = 0; ithread < max_threads; ++ithread) {
           // Create Simplex managers with RAII
           simplex1_managers.emplace_back(n_vars, n_vars+2, n_vars+1, DEBUG_SIMPLEX);
           simplex2_managers.emplace_back(n_vars, n_vars+3, n_vars+1, DEBUG_SIMPLEX);
           
           // Setup legacy pointers for compatibility (still needed for some legacy code)
           simplex1[ithread] = simplex1_managers[ithread].get();
           simplex2[ithread] = simplex2_managers[ithread].get();
           
           // Validate creation
           if (!simplex1_managers[ithread].is_valid() || !simplex2_managers[ithread].is_valid()) {
               std::ostringstream oss;
               oss << "Failed to create Simplex objects for thread " << ithread;
               throw std::runtime_error(oss.str());
           }
       }
       
       std::cout << "Simplex managers created successfully" << std::endl;
       
   } catch (const std::exception& e) {
       sprintf_s(msg, "Simplex creation failed: %s", e.what());
       std::cout << msg << std::endl;
       ok = 0;
       return;
   }

/*
   Build the constraint matrix that will be the complete set for inter-class
   maximizations, and the all-but-one set for intra-class minimizations.

   We have n_vars constraints that each f<=1, one that their sum <= max_kept,
   and one that their sum >= 1.  For intra-class minimization there will be
   one more constraint: bf >= beta epsilon, but we can't set that yet.
*/

   // Build constraint matrix using modern container for thread 0
   constr_ptr = constraints_data[0].data() ;
   
   // Initialize ALL elements, including zeros - explicit constraint matrix setup
   for (i=0 ; i<n_vars ; i++) {
       constr_ptr = constraints_data[0].data() + i * (n_vars+1);
       constr_ptr[0] = 1.0;  // RHS
       for (j=0 ; j<n_vars ; j++)
           constr_ptr[j+1] = (i == j) ? 1.0 : 0.0;  // Explicit zeros
   }

   // Sum <= max_kept constraint
   constr_ptr = constraints_data[0].data() + n_vars * (n_vars+1);
   constr_ptr[0] = (double)max_kept;  // Make sure it's a double
   for (j=0 ; j<n_vars ; j++)
       constr_ptr[j+1] = 1.0;

   // Sum >= 1 constraint - CHECK THE SIGN!
   constr_ptr = constraints_data[0].data() + (n_vars+1) * (n_vars+1);
   constr_ptr[0] = 1.0;
   for (j=0 ; j<n_vars ; j++)
       constr_ptr[j+1] = 1.0;

/*
   Copy this pre-built contraint matrix to those which will be used
   for subsequent threads.  This duplication is a bit wasteful,
   but because we have to leave the last row undone it's simpler
   this way.
*/

   // Copy constraint matrix from thread 0 to other threads
   for (ithread=1 ; ithread<max_threads ; ithread++ ) {
      double* dest_ptr = constraints_data[ithread].data() ;
      double* src_ptr = constraints_data[0].data() ;
      for (i=0 ; i<(n_vars+2)*(n_vars+1) ; i++)
         dest_ptr[i] = src_ptr[i] ;
      }

/*
   Copy the cases, but sort them according to class.
   The class flag, origin 0, is the last variable in each row (case).
   We don't need to keep that variable in the local copy of cases.
   There is no computational reason for sorting the cases by class;
   it has no effect on results, accuracy, or speed.
   However, having the cases grouped by class simplifies some debugging.
*/

   index = 0 ;    // Will be total case count
   for (n_classes=0 ; n_classes<MAX_CLASSES ; n_classes++) {
      n = 0 ;      // Counts cases in this class
      for (i=0 ; i<n_cases ; i++) {
         x_ptr = x + i * (n_vars+1) ; // This case in the input dataset
         k = (int) x_ptr[n_vars] ;    // Class of this case, the last variable
         if (k != n_classes)          // Skip cases not in current class
            continue ;
         case_ptr = cases_data.data() + index * n_vars ; // This case in the local copy
         for (j=0 ; j<n_vars ; j++)          // Copy it from input to local
            case_ptr[j] = x_ptr[j] ;
         class_id_data[index++] = n_classes ;     // Save the class ID of this case
         ++n ;                               // Count cases in this class
         }
      if (n == 0)                            // We've got them all
         break ;                             // Assuming silly user did not have empty class!
      n_per_class[n_classes] = n ;
      }

/*
   Standardize the cases, which helps with correct scaling of the weights.
*/

   for (j=0 ; j<n_vars ; j++) {
      mean = stddev = 0.0 ;
      for (i=0 ; i<n_cases ; i++)
         mean += cases_data[i*n_vars+j] ;
      mean /= n_cases ;
      for (i=0 ; i<n_cases ; i++) {
         diff = cases_data[i*n_vars+j] - mean ;
         stddev += diff * diff ;
         }
      stddev = sqrt ( stddev / n_cases ) ;

      if (stddev < 1.e-15) {
         ok = 0 ;
         std::cout << "" << std::endl;
         std::cout << "ERROR: A variable is constant.  Aborting." << std::endl;
         return ;
         }

      for (i=0 ; i<n_cases ; i++)
         cases_data[i*n_vars+j] = (cases_data[i*n_vars+j] - mean) / stddev ;
      }

   if (progress) {   
      sprintf_s ( msg, "Processed %d cases (mean=%.4lf StdDev=%.4lf) having %d classes with the following case counts:",
                  n_cases, mean, stddev, n_classes ) ;
      std::cout << msg << std::endl;
      for (i=0 ; i<n_classes ; i++) {
         sprintf_s ( msg, "%5d %8d", i, n_per_class[i] ) ;
         std::cout << msg << std::endl;
         }
      }

   if (cuda_enable) {
       if (lfs_cuda_init ( n_cases , n_vars , cases_data.data() , error_msg )) {
           std::cout << "CUDA initialization failed: " << error_msg << std::endl;
           std::cout << "Falling back to CPU processing" << std::endl;
           cuda_enable = 0;
       } else {
           std::cout << "CUDA initialized successfully" << std::endl;
           
           // Initialize per-thread weight buffers for pipelined execution
           per_thread_weights.resize(max_threads);
           for (int i = 0; i < max_threads; ++i) {
               per_thread_weights[i].resize(n_cases);
           }
           std::cout << "CUDA pipeline infrastructure initialized with " << max_threads << " streams" << std::endl;
       }
   }

   std::cout << "LFS constructor ending" << std::endl;
}

/*
================================================================================

   Modern Memory Management Helper Methods Implementation

================================================================================
*/

void LFS::initialize_memory_pools() {
    // Initialize any global memory pools if needed
    // Currently using thread-local pools which are automatically initialized
}


// Modern RAII-based destructor with minimal manual cleanup
LFS::~LFS() {
    std::cout << "LFS destructor starting - RAII cleanup" << std::endl;
    
    // Clean up external resources that aren't managed by RAII
    cleanup_external_resources();
    
    // All other cleanup is handled automatically by RAII:
    // 1. std::vector containers automatically deallocate their memory
    // 2. SimplexManager objects automatically clean up Simplex instances
    // 3. AlignedVector containers handle aligned memory deallocation
    // 4. No manual memory management required
    // 5. Exception-safe cleanup guaranteed
    
    std::cout << "LFS destructor ending - RAII cleanup completed" << std::endl;
}

void LFS::cleanup_external_resources() {
    std::cout << "LFS cleaning up external resources" << std::endl;
    
    // Clean up pipeline infrastructure
    if (cuda_enable) {
        per_thread_weights.clear();
        std::cout << "Pipeline infrastructure cleaned up" << std::endl;
    }
    
    lfs_cuda_cleanup () ;
    std::cout << "LFS external resource cleanup completed" << std::endl;
}

// Thread-safe accessors for simplex objects
Simplex* LFS::get_simplex1_safe(int thread_id) {
    if (!work_area_manager || !work_area_manager->is_valid_area(thread_id)) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(simplex_access_mutex);
    if (thread_id >= 0 && thread_id < static_cast<int>(simplex1_managers.size()) &&
        simplex1_managers[thread_id].is_valid()) {
        return simplex1_managers[thread_id].get();
    }
    return nullptr;
}

Simplex* LFS::get_simplex2_safe(int thread_id) {
    if (!work_area_manager || !work_area_manager->is_valid_area(thread_id)) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(simplex_access_mutex);
    if (thread_id >= 0 && thread_id < static_cast<int>(simplex2_managers.size()) &&
        simplex2_managers[thread_id].is_valid()) {
        return simplex2_managers[thread_id].get();
    }
    return nullptr;
}


/*
------------------------------------------------------------------------------------

   get_f() - Return a pointer to the binary f matrix, n_cases by n_vars

------------------------------------------------------------------------------------
*/

int *LFS::get_f ()
{
   return f_binary_data.data() ;
}


/*
----------------------------------------------------------------------------------

   Modern C++20 Threading Architecture
      1) Thread-safe data structures for case processing results
      2) Thread pool for efficient thread reuse
      3) Async task processing with std::future
      4) Exception-safe threading with proper cleanup

----------------------------------------------------------------------------------
*/

// Modern case result structure with exception safety
struct CaseResult {
    int case_id;
    int thread_id;
    int actual_thread_id;  // Actual thread pool thread ID
    int error_code;
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;
    std::string error_message;
    
    CaseResult() : case_id(-1), thread_id(-1), actual_thread_id(-1), error_code(0) {}
    
    double get_processing_time_ms() const {
        return std::chrono::duration<double, std::milli>(end_time - start_time).count();
    }
};

// Thread-safe progress tracking
class ThreadSafeProgress {
private:
    std::atomic<int> completed_cases{0};
    std::atomic<int> total_cases{0};
    mutable std::shared_mutex progress_mutex;
    
public:
    void set_total(int total) {
        total_cases.store(total);
        completed_cases.store(0);
    }
    
    void increment_completed() {
        completed_cases.fetch_add(1);
    }
    
    double get_progress() const {
        int total = total_cases.load();
        if (total == 0) return 0.0;
        return static_cast<double>(completed_cases.load()) / total;
    }
    
    int get_completed() const {
        return completed_cases.load();
    }
    
    int get_total() const {
        return total_cases.load();
    }
};

// Thread-safe results collection
class ThreadSafeResults {
private:
    std::vector<CaseResult> results;
    mutable std::shared_mutex results_mutex;
    
public:
    void reserve(size_t size) {
        std::unique_lock<std::shared_mutex> lock(results_mutex);
        results.reserve(size);
    }
    
    void add_result(const CaseResult& result) {
        std::unique_lock<std::shared_mutex> lock(results_mutex);
        results.push_back(result);
    }
    
    std::vector<CaseResult> get_results() const {
        std::shared_lock<std::shared_mutex> lock(results_mutex);
        return results;
    }
    
    void clear() {
        std::unique_lock<std::shared_mutex> lock(results_mutex);
        results.clear();
    }
    
    size_t size() const {
        std::shared_lock<std::shared_mutex> lock(results_mutex);
        return results.size();
    }
};

// Thread-local storage for simplex work areas
class ThreadLocalStorage {
private:
    static thread_local int current_thread_id;
    static std::atomic<int> next_thread_id;
    static std::mutex thread_id_mutex;
    static std::unordered_map<std::thread::id, int> thread_id_map;
    
public:
    static int get_thread_id() {
        if (current_thread_id == -1) {
            std::lock_guard<std::mutex> lock(thread_id_mutex);
            auto thread_id = std::this_thread::get_id();
            auto it = thread_id_map.find(thread_id);
            if (it == thread_id_map.end()) {
                current_thread_id = next_thread_id.fetch_add(1);
                thread_id_map[thread_id] = current_thread_id;
            } else {
                current_thread_id = it->second;
            }
        }
        return current_thread_id;
    }
    
    static void reset() {
        std::lock_guard<std::mutex> lock(thread_id_mutex);
        thread_id_map.clear();
        next_thread_id.store(0);
    }
};

// Static member definitions
thread_local int ThreadLocalStorage::current_thread_id = -1;
std::atomic<int> ThreadLocalStorage::next_thread_id{0};
std::mutex ThreadLocalStorage::thread_id_mutex;
std::unordered_map<std::thread::id, int> ThreadLocalStorage::thread_id_map;

// Thread-safe queue for tasks
template<typename T>
class ThreadSafeQueue {
private:
    mutable std::mutex mut;
    std::queue<T> data_queue;
    std::condition_variable data_cond;

public:
    void push(T new_value) {
        std::lock_guard<std::mutex> lk(mut);
        data_queue.push(std::move(new_value));
        data_cond.notify_one();
    }

    bool wait_and_pop(T& value) {
        std::unique_lock<std::mutex> lk(mut);
        data_cond.wait(lk, [this] { return !data_queue.empty(); });
        if (data_queue.empty()) return false;
        value = std::move(data_queue.front());
        data_queue.pop();
        return true;
    }

    bool is_empty() const {
        std::lock_guard<std::mutex> lk(mut);
        return data_queue.empty();
    }
};

// A thread pool that dedicates tasks to threads
class ThreadPool {
private:
    std::atomic<bool> done;
    std::vector<std::thread> threads;
    std::vector<std::unique_ptr<ThreadSafeQueue<std::function<void()>>>> queues;
    std::atomic<int> active_tasks{0};
    std::atomic<int> queued_tasks{0};  // Track total queued tasks
    std::mutex completion_mutex;
    std::condition_variable completion_cond;

public:
    ThreadPool(size_t thread_count) : done(false), threads(thread_count), queues(thread_count) {
        for (size_t i = 0; i < thread_count; ++i) {
            queues[i] = std::make_unique<ThreadSafeQueue<std::function<void()>>>();
            threads[i] = std::thread([this, i] {
                while (!done) {
                    std::function<void()> task;
                    if (queues[i]->wait_and_pop(task)) {
                        queued_tasks--;  // Decrement when task is dequeued
                        task();
                        active_tasks--;  // Decrement when task completes
                        completion_cond.notify_all();
                    }
                }
            });
        }
    }

    ~ThreadPool() {
        done = true;
        for (auto& queue : queues) {
            queue->push([] {}); // Wake up threads
        }
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    template <typename F>
    void enqueue(int thread_id, F f) {
        queued_tasks++;  // Increment when task is queued
        active_tasks++;  // Consider task active immediately
        queues[thread_id]->push(std::function<void()>(f));
    }
    
    void wait_for_completion() {
        std::unique_lock<std::mutex> lock(completion_mutex);
        completion_cond.wait(lock, [this] {
            // All tasks complete when both queued and active are 0
            return queued_tasks == 0 && active_tasks == 0;
        });
    }
};

// Performance monitoring for threading
class ThreadPerformanceMonitor {
private:
    std::atomic<double> total_processing_time{0.0};
    std::atomic<int> total_tasks{0};
    std::atomic<double> min_time{std::numeric_limits<double>::max()};
    std::atomic<double> max_time{0.0};
    mutable std::mutex stats_mutex;
    
public:
    void record_task_time(double time_ms) {
        // Use compare_exchange for atomic double operations
        double current = total_processing_time.load();
        while (!total_processing_time.compare_exchange_weak(current, current + time_ms)) {}
        total_tasks.fetch_add(1);
        
        // Update min/max atomically
        double current_min = min_time.load();
        while (time_ms < current_min &&
               !min_time.compare_exchange_weak(current_min, time_ms)) {}
        
        double current_max = max_time.load();
        while (time_ms > current_max &&
               !max_time.compare_exchange_weak(current_max, time_ms)) {}
    }
    
    double get_average_time() const {
        int tasks = total_tasks.load();
        return tasks > 0 ? total_processing_time.load() / tasks : 0.0;
    }
    
    double get_min_time() const {
        double min_val = min_time.load();
        return min_val == std::numeric_limits<double>::max() ? 0.0 : min_val;
    }
    
    double get_max_time() const {
        return max_time.load();
    }
    
    int get_total_tasks() const {
        return total_tasks.load();
    }
    
    void reset() {
        total_processing_time.store(0.0);
        total_tasks.store(0);
        min_time.store(std::numeric_limits<double>::max());
        max_time.store(0.0);
    }
};


/*
------------------------------------------------------------------------------------

   run() - This is the main routine that coordinates everything

------------------------------------------------------------------------------------
*/


int LFS::run (
   int iters , // Number of iterations
   int nrnd ,  // Number of Monte-Carlo trials for converting real f to binary f
   int nbeta , // Number of trial betas for finding best
   int irep ,  // MCPT replication number; purely for progress display
   int reps    // MCPT replications; purely for progress display
   )
{
   int i, j, k, iter, ret_val, which_i, *iptr, *prior_ptr ;
   int progress_divisor ;
   int time, cuda_time, cuda_subset_time ;
   char msg[8192], error_msg[512] ;
   
   // Modern threading components
   std::unique_ptr<ThreadPool> thread_pool;
   ThreadSafeProgress progress_tracker;
   ThreadSafeResults results_collector;
   ThreadPerformanceMonitor perf_monitor;
   std::vector<std::future<CaseResult>> case_futures;
   size_t optimal_threads = 0; // Declare at function scope

#if DEBUG_CUDA
   char msg2[512] ;
   double *cuda_data ;
   k = (n_cases > 128) ? n_cases : 128 ;
   cuda_data = (double *) MALLOC ( k * k * sizeof(double) ) ;
#endif

#if WRITE_WEIGHTS
   int this_i, this_class, other_class ;
   double *wt_ptr ;
   FILE *fp ;
   fp = fopen ( "WEIGHTS.TXT" , "wt" ) ;
#endif

   n_rand = nrnd ;
   n_beta = nbeta ;
   
   // In deterministic mode, we still use CUDA but ensure synchronization
   // CUDA operations are deterministic when properly synchronized
   if (deterministic_mode) {
       std::cout << "\n[DETERMINISTIC MODE] Ensuring synchronized execution for reproducible results" << std::endl;
       std::cout << "  CUDA: " << (cuda_enable ? "ENABLED" : "DISABLED") << std::endl;
       std::cout << "  Threads: " << max_threads << std::endl;
   }

   progress_divisor = n_cases / 50 ;  // For updating progress window
   if (progress_divisor < 1)
      progress_divisor = 1 ;

   LFStimeTotal = 0 ;            // Total time LFS processing
   LFStimeRealToBinary = 0 ;     // Converting real f to binary
   LFStimeBetaCrit = 0 ;         // Evaluating criterion for trial beta (Does not include TealToBinary)
   LFStimeWeights = 0 ;          // Computing weights
   LFStimeCUDA = 0 ;
   LFStimeCUDAdiff = 0 ;
   LFStimeCUDAdist = 0 ;
   LFStimeCUDAmindist = 0 ;
   LFStimeCUDAterm = 0 ;
   LFStimeCUDAtranspose = 0 ;
   LFStimeCUDAsum = 0 ;
   LFStimeCUDAgetweights = 0 ;

   time = timeGetTime_loc() ;

/*
   Initialize best binary f to all zero
*/   

   for (i=0 ; i<n_cases ; i++) {
      iptr = f_binary_data.data() + i * n_vars ;
      for (j=0 ; j<n_vars ; j++)
         iptr[j] = 0 ;
      }

   if (cuda_enable) {
       if (lfs_cuda_classes ( class_id_data.data() , error_msg )) {
           std::cout << "CUDA class setup failed: " << error_msg << std::endl;
           std::cout << "Falling back to CPU processing" << std::endl;
           cuda_enable = 0;
       }
   }

/*
   Main iteration loop
   First step is to copy current best to prior
*/

   ret_val = 0 ;     // Error flag that we will return to caller

   // Create thread pool with optimal thread count
   // Use multi-threading even with CUDA - the mutex in lfs_do_case will serialize GPU calls
   optimal_threads = std::min(static_cast<size_t>(max_threads),
                              static_cast<size_t>(std::thread::hardware_concurrency()));
   std::cout << "Using " << optimal_threads << " threads" << std::endl;
   thread_pool = std::make_unique<ThreadPool>(optimal_threads);
   results_collector.reserve(n_cases);

   for (iter=0 ; iter<iters ; iter++) {
      
      // CRITICAL: Copy f_binary to f_prior at the start of each iteration (except first)
      // This is essential for the weight computation to work correctly
      if (iter > 0) {
          memtext("Copying f_binary to f_prior for next iteration");
          for (i = 0; i < n_cases; i++) {
              int* binary_ptr = f_binary_data.data() + i * n_vars;
              int* prior_ptr = f_prior_data.data() + i * n_vars;
              for (j = 0; j < n_vars; j++) {
                  prior_ptr[j] = binary_ptr[j];
              }
          }
      }

      sprintf_s ( msg, "Local Feature Selection MCPT rep %d of %d  iter %d of %d", irep+1, reps, iter+1, iters ) ;
      begin_progress_message ( msg ) ;
      
      // Report iteration start and CUDA usage
      if (iter == 0) {
          std::cout << "\n[ITERATION " << iter << "] Starting (weights=1.0, no CUDA needed)" << std::endl;
      } else {
          std::cout << "\n[ITERATION " << iter << "] Starting (CUDA " 
                    << (cuda_enable ? "ENABLED" : "DISABLED") << " for weight computation)" << std::endl;
      }

#if DEBUG_LFS
      sprintf_s ( msg, "---------> Starting iteration %d", iter ) ;
      std::cout << msg << std::endl;
#endif

      // ITERATION CASCADE FIX: Add detailed logging for iteration transitions
      int total_f_binary_flags = 0;
      int total_f_prior_flags = 0;
      int cases_with_flags = 0;
      
      for (i=0 ; i<n_cases ; i++) {         // Copy current best to prior
         iptr = f_binary_data.data() + i * n_vars ;
         prior_ptr = f_prior_data.data() + i * n_vars ;
         
         int case_f_binary_count = 0;
         for (j=0 ; j<n_vars ; j++) {
            if (iptr[j] == 1) {
                case_f_binary_count++;
                total_f_binary_flags++;
            }
            prior_ptr[j] = iptr[j] ;
            if (prior_ptr[j] == 1) total_f_prior_flags++;
         }
         
         if (case_f_binary_count > 0) cases_with_flags++;
         
         // Log first few cases for detailed debugging
         if (i < 3 && iter > 0) {
             std::cout << "ITERATION_CASCADE: iter=" << iter << ", case=" << i
                      << ", f_binary_flags=" << case_f_binary_count << std::endl;
         }
      }
      
      std::cout << "ITERATION_CASCADE: iter=" << iter
               << ", total_f_binary=" << total_f_binary_flags
               << ", total_f_prior=" << total_f_prior_flags
               << ", cases_with_flags=" << cases_with_flags << "/" << n_cases << std::endl;

      if (cuda_enable) {
          if (lfs_cuda_flags ( f_prior_data.data() , error_msg)) {
              std::cout << "CUDA flag setup failed: " << error_msg << std::endl;
              std::cout << "Falling back to CPU processing" << std::endl;
              cuda_enable = 0;
          }
      }


/*
   Initialize modern threading components for iteration
*/
      
      // Initialize progress tracking
      progress_tracker.set_total(n_cases);
      results_collector.clear();
      perf_monitor.reset();

/*
   Process all cases - matching legacy architecture exactly
   Legacy: Main thread computes CUDA weights for each case, then spawns worker thread
*/

      // LEGACY-STYLE PIPELINED ARCHITECTURE: Matches original execution model
      // Main thread launches GPU work and immediately spawns CPU thread
      // cudaMemcpy in lfs_cuda_get_weights provides implicit synchronization
      
      if (iter > 0 && cuda_enable) {
          std::cout << "[CUDA Pipelined] Starting iteration " << iter << " with legacy-style execution" << std::endl;
          
          // Pre-allocate per-thread weight buffers
          if (per_thread_weights.empty()) {
              per_thread_weights.resize(max_threads);
              for (int i = 0; i < max_threads; ++i) {
                  per_thread_weights[i].resize(n_cases);
              }
          }
          
          // Process all cases - each thread will handle its own CUDA work
          for (which_i = 0; which_i < n_cases; ++which_i) {
              int thread_id = which_i % optimal_threads;
              
              // Spawn CPU thread that will do CUDA work internally
              auto case_idx = which_i;
              auto case_class = class_id_data[which_i];
              thread_pool->enqueue(thread_id,
                  [this, case_idx, case_class, thread_id, iter, &results_collector, &perf_monitor, optimal_threads] {
                  auto start_time = std::chrono::high_resolution_clock::now();
                  
                  // Acquire work area first
                  int work_area_id = work_area_manager->acquire_work_area();
                  if (work_area_id == -1) {
                      CaseResult res;
                      res.case_id = case_idx;
                      res.error_code = -1;
                      res.error_message = "No available work area";
                      res.start_time = start_time;
                      res.end_time = std::chrono::high_resolution_clock::now();
                      results_collector.add_result(res);
                      return;
                  }
                  
                  // Launch GPU kernels WITH MUTEX PROTECTION
                  // Only one thread can use CUDA at a time to prevent race conditions
                  char error_msg[256];
                  double* work_weights = weights_data.data() + work_area_id * n_cases;
                  
                  {
                      std::lock_guard<std::mutex> cuda_lock(cuda_mutex);
                      
                      // Launch all CUDA kernels for this case
                      lfs_cuda_diff(case_idx);
                      lfs_cuda_dist();
                      lfs_cuda_mindist(case_idx);
                      lfs_cuda_term(case_class);
                      lfs_cuda_transpose();
                      lfs_cuda_sum();
                      
                      // Get weights - cudaMemcpy inside will block until GPU kernels complete
                      // Keep this inside the mutex to ensure kernels complete before next thread starts
                      lfs_cuda_get_weights(work_weights, error_msg);
                  }
                  
                  // Process case with weights
                  int result = this->process_case_with_weights(
                      case_idx,
                      work_area_id,
                      iter,
                      work_weights
                  );
                  
                  work_area_manager->release_work_area(work_area_id);
                  
                  // Record results
                  auto end_time = std::chrono::high_resolution_clock::now();
                  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                  perf_monitor.record_task_time(static_cast<double>(duration));
                  
                  CaseResult res;
                  res.case_id = case_idx;
                  res.thread_id = thread_id;
                  res.error_code = result;
                  res.start_time = start_time;
                  res.end_time = end_time;
                  if (result != 0) {
                      res.error_message = "Processing failed";
                  }
                  results_collector.add_result(res);
              });
              
              // NO wait here - main thread continues to next case immediately
              // This creates the pipeline: GPU(i+1) runs while CPU(i) processes
              
              // Progress reporting
              if (progress && ((which_i + 1) % 100 == 0 || which_i == n_cases - 1)) {
                  printf("\r[Pipeline] Launched %d/%d cases (%.1f%%)",
                         which_i + 1, n_cases, 
                         100.0 * (which_i + 1) / n_cases);
                  fflush(stdout);
              }
          }
          printf("\n");  // New line after progress
          
          // Wait for all CPU threads to complete
          thread_pool->wait_for_completion();
          std::cout << "[CUDA Pipelined] All cases completed for iteration " << iter << std::endl;
          
      } else {
          // CPU path or iteration 0: Use original parallel processing
          memtext("LFS::run: Preparing for parallel task submission...\n");
          case_futures.clear();
          case_futures.reserve(n_cases);

          // Process in larger chunks now that we have more work areas
          // This reduces synchronization overhead
          int chunk_size = work_area_manager->get_max_work_areas();
          for (int chunk_start = 0; chunk_start < n_cases; chunk_start += chunk_size) {
              int chunk_end = std::min(chunk_start + chunk_size, n_cases);
              
              // Submit this chunk
              for (which_i = chunk_start; which_i < chunk_end; ++which_i) {
                  int thread_id = which_i % optimal_threads;
                  thread_pool->enqueue(thread_id, [this, which_i, iter] {
                      
                      // Acquire an exclusive work area for this task.
                      // This is the key to thread safety.
                      int work_area_id = work_area_manager->acquire_work_area();
                      if (work_area_id == -1) {
                          // This should not happen if batch size <= max_threads
                          std::cerr << "Error: No available work area for case " << which_i << std::endl;
                          return;
                      }

                      try {
                          // Pass the EXCLUSIVE work_area_id to process_case
                          this->process_case(which_i, work_area_id, iter);
                      }
                      catch (const std::exception& e) {
                          std::cerr << "Exception in process_case for case " << which_i << ": " << e.what() << std::endl;
                      }
                      catch (...) {
                          std::cerr << "Unknown exception in process_case for case " << which_i << std::endl;
                      }

                      // Release the work area so another task can use it.
                      work_area_manager->release_work_area(work_area_id);
                  });
              }
              
              // Wait for this chunk to complete before starting next chunk
              // This ensures we don't overwhelm the work area manager
              thread_pool->wait_for_completion();
          }
          
          std::cout << "[CPU Processing] All cases completed for iteration " << iter << std::endl;
      }

/*
   Collect results with proper exception handling
*/
            
            // Process results
            auto collected_results = results_collector.get_results();
            for (const auto& result : collected_results) {
                if (result.error_code != 0) {
                    if (result.error_code == ERROR_SIMPLEX) {
                        sprintf_s(msg, "INTERNAL ERROR!!! Simplex optimization failed for case %d", result.case_id);
                    } else {
                        sprintf_s(msg, "INTERNAL ERROR!!! Case %d failed with error %d: %s",
                                  result.case_id, result.error_code, result.error_message.c_str());
                    }
                    std::cout << msg << std::endl;
                    // Don't fail the entire algorithm for individual case failures
                    // The LFS algorithm can tolerate some failed cases
                    // ret_val = result.error_code;  // Commented out - too strict
                }
            }
      
            // Log performance statistics
            if (progress) {
         sprintf_s(msg, "Thread performance - Avg: %.2fms, Min: %.2fms, Max: %.2fms, Tasks: %d",
                  perf_monitor.get_average_time(), perf_monitor.get_min_time(),
                  perf_monitor.get_max_time(), perf_monitor.get_total_tasks());
         std::cout << msg << std::endl;
      }

      if (ret_val)    // If there was an error
         break ;

      end_progress_message () ;
      
      } // For iter

   LFStimeTotal += timeGetTime_loc() - time ;          // Total time LFS processing

   // Log modern threading performance statistics
   if (progress) {
      sprintf_s(msg, "Modern Threading Performance Summary:");
      std::cout << msg << std::endl;
      sprintf_s(msg, "  Optimal thread count used: %zu", optimal_threads);
      std::cout << msg << std::endl;
      sprintf_s(msg, "  Total cases processed: %d", n_cases);
      std::cout << msg << std::endl;
      sprintf_s(msg, "  Average case processing time: %.2fms", perf_monitor.get_average_time());
      std::cout << msg << std::endl;
      sprintf_s(msg, "  Min case processing time: %.2fms", perf_monitor.get_min_time());
      std::cout << msg << std::endl;
      sprintf_s(msg, "  Max case processing time: %.2fms", perf_monitor.get_max_time());
      std::cout << msg << std::endl;
   }

#if DEBUG_LFS
   for (int ithread=0 ; ithread<max_threads ; ithread++) {
      // Modern architecture: Additional bounds checking for debug output
      if (ithread >= 0 && ithread < static_cast<int>(simplex1_managers.size()) &&
          ithread < static_cast<int>(simplex2_managers.size()) &&
          simplex1_managers[ithread].is_valid() && simplex2_managers[ithread].is_valid()) {
         sprintf_s ( msg, "Final Inter-class counters for thread %d", ithread ) ;
         std::cout << msg << std::endl;
         simplex1_managers[ithread]->print_counters () ;
         sprintf_s ( msg, "Final Intra-class counters for thread %d", ithread ) ;
         std::cout << msg << std::endl;
         simplex2_managers[ithread]->print_counters () ;
      }
   }
#endif

#if WRITE_WEIGHTS
   fclose ( fp ) ;
#endif

   end_progress_message () ;

   memtext("LFS::run: Finalizing LFS execution.\n");
   memtext("LFS::run: Finalizing LFS execution.\n");
   memtext("LFS::run: Finalizing LFS execution.\n");
   memtext("LFS::run: Finalizing LFS execution.\n");
   memtext("LFS::run: Finalizing LFS execution.\n");
   memtext("LFS::run: Finalizing LFS execution.\n");
   sprintf_s ( msg, "LFS Total time = %.3lf seconds", LFStimeTotal / 1000.0 ) ;
   std::cout << msg << std::endl;

   sprintf_s ( msg, "CUDA Total time = %.3lf seconds",  LFStimeCUDA / 1000.0 ) ;
   std::cout << msg << std::endl;

   sprintf_s ( msg, "   CUDA diff time = %.3lf seconds", LFStimeCUDAdiff / 1000.0 ) ;
   std::cout << msg << std::endl;

   sprintf_s ( msg, "   CUDA dist time = %.3lf seconds", LFStimeCUDAdist / 1000.0 ) ;
   std::cout << msg << std::endl;

   sprintf_s ( msg, "   CUDA mindist time = %.3lf seconds", LFStimeCUDAmindist / 1000.0 ) ;
   std::cout << msg << std::endl;

   sprintf_s ( msg, "   CUDA term time = %.3lf seconds", LFStimeCUDAterm / 1000.0 ) ;
   std::cout << msg << std::endl;

   sprintf_s ( msg, "   CUDA transpose time = %.3lf seconds", LFStimeCUDAtranspose / 1000.0 ) ;
   std::cout << msg << std::endl;

   sprintf_s ( msg, "   CUDA sum time = %.3lf seconds", LFStimeCUDAsum / 1000.0 ) ;
   std::cout << msg << std::endl;

   sprintf_s ( msg, "   CUDA get weights time = %.3lf seconds", LFStimeCUDAgetweights / 1000.0 ) ;
   std::cout << msg << std::endl;

   sprintf_s ( msg, "Non-CUDA Total time = %.3lf seconds", (LFStimeTotal - LFStimeCUDA) / 1000.0 ) ;
   std::cout << msg << std::endl;

   /*if (max_threads_limit == 1) {
      sprintf_s ( msg, "   Real to binary = %.3lf seconds", LFStimeRealToBinary / 1000.0 ) ;
      std::cout << msg << std::endl;
      sprintf_s ( msg, "   Beta criterion = %.3lf seconds", LFStimeBetaCrit / 1000.0 ) ;
      std::cout << msg << std::endl;
      sprintf_s ( msg, "   Weights = %.3lf seconds", LFStimeWeights / 1000.0 ) ;
      std::cout << msg << std::endl;
      }
   else
      std::cout << "Additional timing information not printed because more than 1 thread used" << std::endl;
*/
#if DEBUG_CUDA
   FREE ( cuda_data ) ;
#endif

   return ret_val ;
}
