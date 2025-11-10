/******************************************************************************/
/*                                                                            */
/*  CLASSES.H - All class and struct definitions are here                     */
/*                                                                            */
/******************************************************************************/

#include <memory>
#include <vector>
#include <string>
#include <new>
#include <type_traits>
#include <mutex>
#include <cstdlib>  // For std::free instead of malloc.h
#include <cassert>  // For bounds checking assertions

/*
--------------------------------------------------------------------------------

   SingularValueDecomp - Singular Value Decomposition

--------------------------------------------------------------------------------
*/

class SingularValueDecomp {

public:

   SingularValueDecomp ( int nrows , int ncols , int save_a=0 ) ;
   ~SingularValueDecomp () ;
   void svdcmp () ;
   void backsub ( double limit , double *soln ) ;

   int ok ;         // Was everything legal and allocs successful?

/*
   These are made public to allow access if desired.
   Normally, only 'a' (the design matrix) and 'b' (the right-hand-side)
   are written by the user.  If 'save_a' is nonzero, 'a' is kept intact.
*/

   double *a ;      // nrows by ncols input of design, output of U
   double *u ;      // unless save_a nonzero, in which case U output in 'u'
   double *w ;      // Unsorted ncols vector of singular values
   double *v ;      // Ncols by ncols output of 'v'
   double *b ;      // Nrows right-hand-side for backsub


private:

   void bidiag ( double *matrix ) ;
   double bid1 ( int col , double *matrix , double scale ) ;
   double bid2 ( int col , double *matrix , double scale ) ;
   void right ( double *matrix ) ;
   void left ( double *matrix ) ;
   void cancel ( int low , int high , double *matrix ) ;
   void qr ( int low , int high , double *matrix ) ;
   void qr_mrot ( int col , double sine , double cosine , double *matrix ) ;
   void qr_vrot ( int col , double sine , double cosine ) ;

   int rows ;       // Nrows preserved here
   int cols ;       // And ncols
   double *work ;   // Scratch vector ncols long
   double norm ;    // Norm of 'a' matrix
} ;


/*
--------------------------------------------------------------------------------

   Simplex

--------------------------------------------------------------------------------
*/

// Forward declaration for the modern solver class
class ModernSimplex;
// Forward declaration for the legacy solver class
class SimplexLegacy;

class Simplex {

public:
   Simplex ( int nv , int nc , int nle , int prn ) ;
   ~Simplex () ;
   void set_objective ( double *coefs ) ;
   int set_constraints ( double *values ) ;
   int solve ( int max_iters , double eps ) ;
   void get_optimal_values ( double *optval , double *values ) ;
   int check_objective ( double *coefs , double eps , double *error ) ;
   int check_constraint ( int which , double *constraints , double eps , double *error ) ;
   int check_counters () ;
   void print_counters () ;
   void set_slack_variables();

   int ok ;
   std::string status;

private:
   void reconstruct_tableau_from_solution(int n_vars, int n_constraints);
   int solve_simple ( int max_iters , double eps ) ;
   int solve_extended ( int max_iters , double eps ) ;
   void print_tableau ( const char *msg ) ;
   void print_optimal_vector ( const char *msg ) ;
   
   // HiGHS-based modern solver
   std::unique_ptr<ModernSimplex> modern_solver;
   
   // Legacy tableau-based solver
   std::unique_ptr<SimplexLegacy> legacy_solver;
   
   // Data for the sparse matrix representation, with a lifetime tied to the Simplex object
   std::vector<double> sparse_A_values;
   std::vector<int> sparse_A_indices;
   std::vector<int> sparse_A_starts;

   int n_vars ;           // Number of variables to be optimized
   int n_constraints ;    // Number of constraints
   int n_less_eq ;        // The first n_less_eq constraints are <=, the remaining are >=
   int n_gtr_eq ;         // The last n_constraints-n_less_eq constraints are >=
   int nrows ;            // Number of rows in tableau (n_constraints + 1)
                          // If n_gtr_eq>0, the tableau will actually have nrows+1 rows, with the extra
                          // row at the bottom used to hold and modify the objective during Phase 1.
   int ncols ;            // Number of columns in tableau (1 + n_vars + n_constraints + n_gtr_eq)
   // Modern memory-managed data with RAII
   std::unique_ptr<double[]> tableau_data ;  // RAII-managed tableau memory
   std::unique_ptr<int[]> basics_data ;      // RAII-managed basics memory
   
   // Legacy pointer interfaces for compatibility
   double *tableau ;      // Points to tableau_data.get() - top row is objective, left column is RHS of constraints
   int *basics ;          // Points to basics_data.get() - ncols vector: basics[i] tells which row in column i is 1; -1 if col i not basic
   int print ;            // Print steps and final solution?
   // These counters are only to inform user of performance stats
   int p1_zero_exit ;     // Ideal exit because criterion reached zero
   int p1_normal_exit ;   // Ideal exit because all row 0 choices are nonnegative
   int p1_relaxed_exit ;  // Exited Phase 1 because only trivially negative choices remain; acceptable
   int p1_art_exit ;      // Exited Phase 1 because all artificial variables are out of basis
   int p1_art_in_basis ;  // One or more artificial vars in basis at end of Phase 1
   int p1_unbounded ;     // Unbounded exit in Phase 1 (pathological!)
   int p1_no_feasible ;   // No Phase 1 feasible solutions
   int p1_too_many_its ;  // Phase 1 terminated early due to too many iterations
   int p1_cleanup_bad ;   // Phase 1 criterion degraded in cleanup; pathological!
   int p2_normal_exit ;   // Ideal exit because all row 0 choices are nonnegative
   int p2_relaxed_exit ;  // Exited Phase 2 because only trivially negative choices remain; acceptable
   int p2_unbounded ;     // Unbounded exit in Phase 2
   int p2_too_many_its ;  // Phase 2 terminated early due to too many iterations
} ;


/*
--------------------------------------------------------------------------------

   SimplexLegacy - Original tableau-based simplex solver

--------------------------------------------------------------------------------
*/

class SimplexLegacy {

public:
   SimplexLegacy ( int nv , int nc , int nle , int prn ) ;
   ~SimplexLegacy () ;
   void set_objective ( double *coefs ) ;
   int set_constraints ( double *values ) ;
   int solve ( int max_iters , double eps ) ;
   void get_optimal_values ( double *optval , double *values ) ;
   int check_objective ( double *coefs , double eps , double *error ) ;
   int check_constraint ( int which , double *constraints , double eps , double *error ) ;
   int check_counters () ;
   void print_counters () ;
   void set_slack_variables() ;  // Added for compatibility with modern interface

   int ok ;
   
   // These counters are public to allow Simplex wrapper to access them
   int p1_zero_exit ;     // Ideal exit because criterion reached zero
   int p1_normal_exit ;   // Ideal exit because all row 0 choices are nonnegative
   int p1_relaxed_exit ;  // Exited Phase 1 because only trivially negative choices remain; acceptable
   int p1_art_exit ;      // Exited Phase 1 because all artificial variables are out of basis
   int p1_art_in_basis ;  // One or more artificial vars in basis at end of Phase 1
   int p1_unbounded ;     // Unbounded exit in Phase 1 (pathological!)
   int p1_no_feasible ;   // No Phase 1 feasible solutions
   int p1_too_many_its ;  // Phase 1 terminated early due to too many iterations
   int p1_cleanup_bad ;   // Phase 1 criterion degraded in cleanup; pathological!
   int p2_normal_exit ;   // Ideal exit because all row 0 choices are nonnegative
   int p2_relaxed_exit ;  // Exited Phase 2 because only trivially negative choices remain; acceptable
   int p2_unbounded ;     // Unbounded exit in Phase 2
   int p2_too_many_its ;  // Phase 2 terminated early due to too many iterations

private:
   int solve_1 () ;
   int solve_simple ( int max_iters , double eps ) ;
   int solve_extended ( int max_iters , double eps ) ;
   int find_pivot_column ( int phase , double eps ) ;
   int find_pivot_row ( int pivot_col ) ;
   void do_pivot ( int pivot_row , int pivot_col , int phase1 ) ;
   void print_tableau ( const char *msg ) ;
   void print_optimal_vector ( const char *msg ) ;

   int n_vars ;           // Number of variables to be optimized
   int n_constraints ;    // Number of constraints
   int n_less_eq ;        // The first n_less_eq constraints are <=, the remaining are >=
   int n_gtr_eq ;         // The last n_constraints-n_less_eq constraints are >=
   int nrows ;            // Number of rows in tableau (n_constraints + 1)
                          // If n_gtr_eq>0, the tableau will actually have nrows+1 rows, with the extra
                          // row at the bottom used to hold and modify the objective during Phase 1.
   int ncols ;            // Number of columns in tableau (1 + n_vars + n_constraints + n_gtr_eq)
   double *tableau ;      // Top row is objective, left column is RHS of constraints
   int *basics ;          // Ncols vector: basics[i] tells which row in column i is 1; -1 if col i not basic
   int print ;            // Print steps and final solution?
} ;


/*
--------------------------------------------------------------------------------

   LFS

--------------------------------------------------------------------------------
*/

// Custom aligned allocator for performance-critical data
template<typename T, size_t Alignment = 32>
class LFSAlignedAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    template<typename U>
    struct rebind {
        using other = LFSAlignedAllocator<U, Alignment>;
    };
    
    LFSAlignedAllocator() noexcept = default;
    
    template<typename U>
    LFSAlignedAllocator(const LFSAlignedAllocator<U, Alignment>&) noexcept {}
    
    pointer allocate(size_type n) {
        if (n == 0) return nullptr;
        
        size_type bytes = n * sizeof(T);
        void* ptr = nullptr;
        
        // Use aligned allocation for better performance with RAII safety
        #ifdef _WIN32
            ptr = _aligned_malloc(bytes, Alignment);
        #else
            if (posix_memalign(&ptr, Alignment, bytes) != 0) {
                ptr = nullptr;
            }
        #endif
        
        if (!ptr) {
            throw std::bad_alloc();
        }
        
        return static_cast<pointer>(ptr);
    }
    
    void deallocate(pointer p, size_type) noexcept {
        if (p) {
            #ifdef _WIN32
                _aligned_free(p);
            #else
                std::free(p);  // Use std::free instead of raw free
            #endif
        }
    }
    
    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        new(p) U(std::forward<Args>(args)...);
    }
    
    template<typename U>
    void destroy(U* p) {
        p->~U();
    }
    
    bool operator==(const LFSAlignedAllocator&) const noexcept { return true; }
    bool operator!=(const LFSAlignedAllocator&) const noexcept { return false; }
};

template<typename T>
using LFSAlignedVector = std::vector<T, LFSAlignedAllocator<T, 32>>;

// Safe array access utilities with bounds checking
template<typename T>
class SafeArrayAccess {
public:
    static T& safe_access(T* array, size_t index, size_t max_size, const char* context = "array") {
        assert(array != nullptr && "Array pointer is null");
        assert(index < max_size && "Array index out of bounds");
        if (index >= max_size) {
            throw std::out_of_range(std::string("Index ") + std::to_string(index) +
                                  " out of bounds for " + context + " (size: " + std::to_string(max_size) + ")");
        }
        return array[index];
    }
    
    static const T& safe_access(const T* array, size_t index, size_t max_size, const char* context = "array") {
        assert(array != nullptr && "Array pointer is null");
        assert(index < max_size && "Array index out of bounds");
        if (index >= max_size) {
            throw std::out_of_range(std::string("Index ") + std::to_string(index) +
                                  " out of bounds for " + context + " (size: " + std::to_string(max_size) + ")");
        }
        return array[index];
    }
    
    // Safe 2D array access with bounds checking
    static T& safe_2d_access(T* array, size_t row, size_t col, size_t num_cols, size_t total_size, const char* context = "2D array") {
        size_t index = row * num_cols + col;
        return safe_access(array, index, total_size, context);
    }
    
    static const T& safe_2d_access(const T* array, size_t row, size_t col, size_t num_cols, size_t total_size, const char* context = "2D array") {
        size_t index = row * num_cols + col;
        return safe_access(array, index, total_size, context);
    }
};

// Modern memory pool for performance-critical allocations
template<typename T, size_t PoolSize = 1024>
class MemoryPool;

// RAII wrapper for legacy Simplex objects
class SimplexManager {
public:
    SimplexManager(int n_vars, int n_constraints, int n_less_eq, int debug_flag);
    ~SimplexManager() = default;
    
    // Move semantics for efficient transfers
    SimplexManager(SimplexManager&&) = default;
    SimplexManager& operator=(SimplexManager&&) = default;
    
    // Delete copy operations to prevent accidental copies
    SimplexManager(const SimplexManager&) = delete;
    SimplexManager& operator=(const SimplexManager&) = delete;
    
    Simplex* get() const { return simplex.get(); }
    Simplex* operator->() const { return simplex.get(); }
    bool is_valid() const { return simplex && simplex->ok; }
    
    // Reset method to prevent state contamination between optimizations
    void reset() {
        if (simplex) {
            // Force complete reinitialization by recreating the Simplex instance
            // This prevents state contamination that could lead to binary solutions
            // instead of fractional values
            simplex.reset(new Simplex(stored_n_vars, stored_n_constraints, stored_n_less_eq, stored_debug_flag));
        }
    }
    
private:
    std::unique_ptr<Simplex> simplex;
    // Store constructor parameters for reset functionality
    int stored_n_vars;
    int stored_n_constraints;
    int stored_n_less_eq;
    int stored_debug_flag;
};

// Thread-safe work area manager for simplex operations
class ThreadSafeWorkAreaManager {
private:
    std::mutex work_area_mutex;
    std::vector<bool> work_area_in_use;
    int max_work_areas;
    
public:
    ThreadSafeWorkAreaManager(int max_areas) : max_work_areas(max_areas) {
        work_area_in_use.resize(max_areas, false);
    }
    
    int acquire_work_area() {
        std::lock_guard<std::mutex> lock(work_area_mutex);
        for (int i = 0; i < max_work_areas; ++i) {
            if (!work_area_in_use[i]) {
                work_area_in_use[i] = true;
                return i;
            }
        }
        return -1; // No available work area
    }
    
    void release_work_area(int area_id) {
        if (area_id >= 0 && area_id < max_work_areas) {
            std::lock_guard<std::mutex> lock(work_area_mutex);
            work_area_in_use[area_id] = false;
        }
    }
    
    bool is_valid_area(int area_id) const {
        return area_id >= 0 && area_id < max_work_areas;
    }
    
    int get_max_work_areas() const {
        return max_work_areas;
    }
};

// Modern LFS class with RAII memory management
class LFS {

public:
   LFS ( int nc , int nv , int mk , int max_threads , double *x , int progress ) ;
   ~LFS ();  // Custom destructor for external resource cleanup

   int run ( int iters , int n_rand , int n_beta , int irep , int mcpt_reps ) ;
   int process_case ( int i , int ithread, int iter ) ;  // This is the threaded routine; easiest to just make it public
   int process_case_with_weights ( int i , int ithread, int iter, double* precomputed_weights ) ;  // Version that uses pre-computed weights
   int *get_f () ;
   void set_deterministic_mode(bool enable) { deterministic_mode = enable; }

   int ok ;

private:

   void compute_weights ( int which_i, double* weights_ptr, double* delta_ptr, double* d_ijk_ptr, const int* f_prior_ptr ) ;
   int test_beta ( int which_i, double beta, double eps_max, double* crit, int ithread,
                   double* aa_ptr, int* best_binary_ptr, double* constr_ptr, double* d_ijk_ptr,
                   int* nc_iwork_ptr, double* weight_ptr, double* f_real_ptr, double* delta_ptr_for_case);
   
   // Basic configuration
   int n_cases ;          // Number of cases
   int n_vars ;           // Number of variables
   int n_classes ;        // Number of classes (computed when cases initially processed)
   int max_kept ;         // Max number of variables to keep
   int n_rand ;           // Number of Monte-Carlo trials for converting real f to binary f
   int n_beta ;           // Number of trial beta values for finding best beta
   int max_threads ;      // Maximum number of threads
   int progress ;         // Print progress reports?
   bool deterministic_mode ; // Force deterministic execution (single-threaded, no CUDA)
   int n_per_class[MAX_CLASSES] ; // Number of cases in each class
   
   // CUDA synchronization - only one thread can use CUDA at a time
   std::mutex cuda_mutex;
   
   // Modern memory-managed data structures with RAII
   std::vector<int> class_id_data ;           // Class ID (origin 0) for each case - RAII managed
   std::vector<double> cases_data ;           // Cases data - RAII managed
   LFSAlignedVector<double> weights_data ;       // Weight array with aligned memory for performance
   LFSAlignedVector<double> delta_data ;         // Delta differences - aligned for SIMD operations
   std::vector<double> f_real_data ;          // Real-valued feature array
   std::vector<int> f_binary_data ;           // Binary feature array
   std::vector<int> f_prior_data ;            // Prior iteration binary feature array
   std::vector<double> d_ijk_data ;           // Work area for weight computation
   std::vector<int> nc_iwork_data ;           // Work area used in LFS_BETA.CPP
   
   // Thread-local work areas using modern containers
   std::vector<std::vector<int>> best_binary_data ;     // Per-thread work areas
   std::vector<std::vector<int>> best_fbin_data ;       // Per-thread work areas
   std::vector<std::vector<double>> aa_data ;           // Per-thread 'a' terms
   std::vector<std::vector<double>> bb_data ;           // Per-thread 'b' terms
   std::vector<std::vector<double>> constraints_data ;  // Per-thread constraint matrices
   
   
   // Modern Simplex management with RAII and thread-safe access
   std::vector<SimplexManager> simplex1_managers ;  // RAII-managed Simplex objects for inter-class
   std::vector<SimplexManager> simplex2_managers ;  // RAII-managed Simplex objects for intra-class
   Simplex *simplex1[MAX_THREADS] ;  // Legacy interface - points to managed objects
   Simplex *simplex2[MAX_THREADS] ;  // Legacy interface - points to managed objects
   
   // Thread-safe work area management
   std::unique_ptr<ThreadSafeWorkAreaManager> work_area_manager;
   std::mutex simplex_access_mutex;  // Protects simplex object access
   
   // Pipeline infrastructure for CPU/GPU overlap
   std::vector<std::vector<double>> per_thread_weights;  // Per-thread weight buffers for pipelining
   
   // Thread-safe accessors for simplex objects
   Simplex* get_simplex1_safe(int thread_id);
   Simplex* get_simplex2_safe(int thread_id);
   
   // Exception-safe initialization helpers
   void initialize_memory_pools();
   void setup_legacy_pointers();
   void validate_memory_allocation();
   void cleanup_external_resources();
} ;


/*
--------------------------------------------------------------------------------

   HMM

--------------------------------------------------------------------------------
*/

class HMM {  // Hidden Markov Model

public:
   HMM ( int ncases , int nvars , int nstates ) ;
   ~HMM () ;
   int estimate ( int print , FILE *file_print , int n_init , int max_iters , double *dataset ) ;
   double get_params ( double **init_probs , double **transition ,
                       double **means , double **covars , double **state_probs ) ;

   int ok ;

private:
   void find_mean_covar () ;
   void find_densities ( double *these_means ) ;
   double forward ( double *these_transitions ) ;
   double backward () ;
   void initialize ( FILE *fp , int n_trials ) ;
   void update_mean_covar () ;
   void print_params ( int print , FILE *file_print , int iter , double forward_prob , double backward_prob ) ;

   int ncases ;          // Number of cases in dataset
   int nvars ;           // Number of variables in each case
   int nstates ;         // Number of states
   double *data ;        // Ncases * nvars dataset; case changes slowest
   double init_probs[MAX_STATES] ;  // Nstates vector of probability that first case is in each state
   double transition[MAX_STATES*MAX_STATES] ; // Transition probability matrix; Aij=prob(i-->j)
   double *means ;       // Nstates * nvars means; state changes slowest
   double *trial_means ; // Nstates * nvars means; state changes slowest (work)
   double *best_means ;  // Nstates * nvars means; state changes slowest (work)
   double *covars ;      // Nstates * nvars * nvars covariances; state changes slowest
   double *init_covar ;  // Nvars * nvars initial data covariance for initialization only
   double *best_covar ;  // Nvars * nvars best initial data covariance for initialization only
   double *densities ;   // Nstates * ncases probability densities; state changes slowest (work)
   double *inverse ;     // Work vector nvars * nvars long (work)
   double *alpha ;       // Ncases * nstates result of forward pass; state changes fastest (work)
   double *beta ;        // Ncases * nstates result of backward pass; state changes fastest (work)
   double *state_probs ; // Ncases * nstates probabilities of state; state changes fastest
   double likelihood ;   // Most recent (and final) log likelihood
   double *rwork ;       // Work vector nvars*nvars + 2*nvars long (work)
   int *iwork ;          // Work vector nvars long (work)
   double trans_work1[MAX_STATES*MAX_STATES] ;      // Transition work matrix
   double trans_work2[MAX_STATES*MAX_STATES] ;      // Transition work matrix
   double trial_transition[MAX_STATES*MAX_STATES] ; // Transition work matrix
   double best_transition[MAX_STATES*MAX_STATES] ;  // Transition work matrix
} ;


/*
--------------------------------------------------------------------------------

   NomOrd - Nominal-to-ordinal conversion

--------------------------------------------------------------------------------
*/

class NomOrd {

public:
   NomOrd ( int ncases , int npred , double *preds , double *gate ) ;
   ~NomOrd () ;
   void train ( double *target ) ;
   void mcpt ( int type , int reps , double *target , int *pred_index ) ;
   int create () ;
   void print_counts ( int *pred_indices ) ;
   void print_ranks ( int *pred_indices ) ;

   int ok ;

private:
   int ncases ;            // Number of cases in training set
   int npred ;             // Number of predictors
   int nclasses ;          // Number of classes; equals npred if npred > 1
   int *class_id ;         // Ncases vector of class IDs
   int *gate ;             // -1, 0, 1 if trinary gate, NULL if no gate
   int *class_counts ;     // Number of cases in each class
   int gate_counts[3] ;    // Number of cases in each gate bin
   int *bin_counts ;       // Number of cases in each class/gate bin
   double *temp_target ;   // Work area
   double *target_work ;   // Work area
   double *ranks ;         // Work area
   int *indices ;          // Work area
   double *mean_ranks ;    // Work area
   double median ;         // Work area
   // The following are for MCPT
   double *orig_gate ;     // nclasses vector of rep 0 abs (gate+ - gate-)
   double orig_max_gate ;  // Max across classes of above
   double orig_class[2] ;  // Rep 0 max class difference for gate- and gate+
   double orig_max_class ; // Max of above
   int *count_gate ;
   int count_max_gate ;
   int count_class[2] ;
   int count_max_class ;
} ;
