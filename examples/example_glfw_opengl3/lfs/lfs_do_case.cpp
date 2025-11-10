/******************************************************************************/
/*                                                                            */
/*  LFS_DO_CASE - Local Feature Selection processes a single case             */
/*                                                                            */
/*  Normally returns 0; returns ERROR_SIMPLEX if Simplex error                */
/*                                                                            */
/******************************************************************************/

// Insert other includes that you need here

#include <math.h>
#include <cmath>
#include <stdio.h>
#include <algorithm>
#include <vector>
#include <utility>

#include "const.h"
#include "classes.h"
#include "funcdefs.h"
#include "lfs_cuda.h"

// External declarations for compatibility
extern int cuda_enable;  // Use the global cuda_enable from lfs console.cpp
extern int LFStimeWeights;
extern int LFStimeCUDA, LFStimeCUDAdiff, LFStimeCUDAdist, LFStimeCUDAmindist;
extern int LFStimeCUDAterm, LFStimeCUDAtranspose, LFStimeCUDAsum, LFStimeCUDAgetweights;

// Global mutex to serialize CUDA calls (CUDA kernels are not thread-safe)
#include <mutex>
// #include <cuda_runtime.h>  // Commented out - CUDA not available in this build
static std::mutex cuda_mutex;

// Forward declaration - implementation is in LFS.CPP
//extern int timeGetTime();

// Error constants
#define ERROR_SIMPLEX 3

#define VERIFY_WEIGHTS 0

int LFS::process_case (
   int which_i ,     // Index of the current case
   int ithread,      // Thread number (lets us access the right work areas)
   int iter          // The main loop iteration number
)
{
   // FIXED: Ensure thread ID is within valid bounds
   int safe_thread_id = ithread;
   if (safe_thread_id < 0 || safe_thread_id >= max_threads) {
       // Clamp to valid range to prevent crashes
       safe_thread_id = 0;
       printf("\n\nWARNING... Thread ID %d out of range, using thread 0", ithread);
   }
   
   // Phase 1 Safety Enhancement: Bounds checking for thread index
   if (safe_thread_id >= MAX_THREADS) {
      printf("\n\nERROR... Invalid thread index %d (must be 0-%d)", safe_thread_id, MAX_THREADS-1);
      return ERROR_SIMPLEX;
   }
   
   // Modern architecture: Verify SimplexManager objects are properly initialized
   if (safe_thread_id >= static_cast<int>(simplex1_managers.size()) || !simplex1_managers[safe_thread_id].is_valid()) {
      printf("\n\nERROR... Simplex1 manager not initialized for thread %d", safe_thread_id);
      return ERROR_SIMPLEX;
   }

   int i, j, ivar, this_class, *iptr, *best_binary_ptr, *best_fbin_ptr, time ;
   double *dptr1, *dptr2, *delta_ptr, *constr_ptr, *aa_ptr, *bb_ptr, *temp_ptr, *weights_ptr, wt, term, eps_max, crit, best_crit ;
#if (DEBUG_LFS > 1)  ||  DEBUG_CUDA  ||  VERIFY_WEIGHTS
   char msg[256] ;
#endif

   this_class = class_id_data[which_i] ;
   
   // FIXED: Use safe thread ID with bounds checking
   if (safe_thread_id >= static_cast<int>(aa_data.size()) ||
       safe_thread_id >= static_cast<int>(bb_data.size()) ||
       safe_thread_id >= static_cast<int>(constraints_data.size()) ||
       safe_thread_id >= static_cast<int>(best_binary_data.size()) ||
       safe_thread_id >= static_cast<int>(best_fbin_data.size())) {
       printf("\n\nERROR... Work area data not available for thread %d", safe_thread_id);
       return ERROR_SIMPLEX;
   }
   
   // Use legacy pointer interface (which now points to modern container data)
   // The modern memory management is transparent to this code
   aa_ptr = aa_data[safe_thread_id].data() ;
   bb_ptr = bb_data[safe_thread_id].data() ;
   constr_ptr = constraints_data[safe_thread_id].data() ;
   best_binary_ptr = best_binary_data[safe_thread_id].data() ;
   best_fbin_ptr = best_fbin_data[safe_thread_id].data() ;
   
   // FIXED: Use safe thread ID with bounds checking
   if (safe_thread_id * n_cases >= static_cast<int>(weights_data.size())) {
       printf("\n\nERROR... Weights data out of bounds for thread %d", safe_thread_id);
       return ERROR_SIMPLEX;
   }
   weights_ptr = weights_data.data() + safe_thread_id * n_cases ;

#if DEBUG_LFS > 1
   sprintf_s ( msg, "Processing case %d, class %d", which_i, this_class ) ;
   MEMTEXT ( msg ) ;
#endif

/*
   Compute delta, the difference between this case and each other case
*/

   // DEFINE the base delta pointer for this thread's workspace here
   double* thread_delta_base_ptr = delta_data.data() + static_cast<size_t>(safe_thread_id) * n_cases * n_vars;

   // The calculation loop remains the same, but we use the base pointer
   dptr1 = cases_data.data() + which_i * n_vars;
   for (j = 0; j < n_cases; j++) {
       dptr2 = cases_data.data() + j * n_vars;
       // Calculate the specific delta for case j
       delta_ptr = thread_delta_base_ptr + j * n_vars; // Note: this local delta_ptr is fine
       for (ivar = 0; ivar < n_vars; ivar++)
           delta_ptr[ivar] = dptr1[ivar] - dptr2[ivar];
   }


/*
   Compute the weights
   We use a little trick here.  On the first iteration, f_prior will be all zero.
   On subsequent iterations, it is guaranteed that for each case, at least 1 of them
   will be 1.  So on the first iteration, all weights will be 1.0.
   Weight computation is terribly slow, so there's no need to compute them
   on the first iteration.  We could just pass the iteration number as a parameter,
   but we can avoid this parameter passing by examining the first case's flags.
   It they are all zero, it's the first iteration and we can skip weight computation.
*/

#if VERIFY_WEIGHTS
   for (i=0 ; i<n_cases ; i++)   // MUST be one thread!
      f_real[i] = weights[i] ;   // Borrow this; nothing is in it now.
#endif

    if (iter > 0) { // This is a subsequent iteration, so compute weights.
        if (VERIFY_WEIGHTS || !cuda_enable) { // If verifying or CUDA is off, use CPU
            time = timeGetTime_loc() ;
            compute_weights ( which_i, weights_ptr, delta_data.data() + safe_thread_id * n_cases * n_vars, d_ijk_data.data() + safe_thread_id * n_cases, f_prior_data.data() ) ;
            LFStimeWeights += timeGetTime_loc() - time ;
        }
        else { // CUDA is enabled, use the GPU path
            // No mutex needed - we're processing sequentially when CUDA is enabled
            
            char error_msg[256];
            int time_cuda, time_subset, iclass;
            iclass = class_id_data[which_i];
            time_cuda = timeGetTime_loc();
            
            time_subset = timeGetTime_loc();
            lfs_cuda_diff(which_i);
            LFStimeCUDAdiff += timeGetTime_loc() - time_subset;
            
            time_subset = timeGetTime_loc();
            lfs_cuda_dist();
            LFStimeCUDAdist += timeGetTime_loc() - time_subset;
            
            time_subset = timeGetTime_loc();
            lfs_cuda_mindist(which_i);
            LFStimeCUDAmindist += timeGetTime_loc() - time_subset;
            
            time_subset = timeGetTime_loc();
            lfs_cuda_term(iclass);
            LFStimeCUDAterm += timeGetTime_loc() - time_subset;
            
            time_subset = timeGetTime_loc();
            lfs_cuda_transpose();
            LFStimeCUDAtranspose += timeGetTime_loc() - time_subset;
            
            time_subset = timeGetTime_loc();
            lfs_cuda_sum();
            LFStimeCUDAsum += timeGetTime_loc() - time_subset;
            
            time_subset = timeGetTime_loc();
            lfs_cuda_get_weights(weights_ptr, error_msg);
            LFStimeCUDAgetweights += timeGetTime_loc() - time_subset;
            
            // Synchronize ONCE after all kernels complete - not after each kernel!
            // cudaDeviceSynchronize();  // Commented out - CUDA not available

            LFStimeCUDA += timeGetTime_loc() - time_cuda;
        }
    } else { // In first iteration, so all weights are 1.0
        for (j=0 ; j<n_cases ; j++)
            weights_ptr[j] = 1.0 ;
    }

#if VERIFY_WEIGHTS
   if (ivar < n_vars) {
      for (i=0 ; i<n_cases ; i++) {  // MUST be one thread!
         if (fabs ( f_real[i] - weights[i] ) > 1.e-5) {
            sprintf_s ( msg , "ERROR... Weights disagree which_i=%d  jcase=%d  %.5lf vs %.5lf)",
                        which_i, i, f_real[i], weights[i] ) ;
            MEMTEXT ( msg ) ;
            }
         }
      }
#endif


/*
   Compute the 'a' vector  and the 'b' vector
   Actually, we compute -a because when we use it later we
   will be minimizing using a maximization algorithm.
   Note that we must exclude case j==which_i from the summation,
   but delta is zero for that case so including it here is
   fine, and we avoid taking the time to test for j==which_i.
*/

   for (j=0 ; j<n_vars ; j++)
      aa_ptr[j] = bb_ptr[j] = 0.0 ;

   // Get the base pointer for this thread's entire delta matrix

   for (j=0 ; j<n_cases ; j++) {
       if (j == which_i)
           continue ;

       // THIS IS THE CRITICAL FIX:
       // Calculate the pointer to the specific delta vector for case 'j'
       // within this thread's dedicated memory block.
       delta_ptr = thread_delta_base_ptr + j * n_vars;

       wt = weights_ptr[j] ;
       if (class_id_data[j] == this_class) {
          for (ivar=0 ; ivar<n_vars ; ivar++) {
             term = delta_ptr[ivar] ;
             aa_ptr[ivar] -= wt * term * term ;
          }
       }
       else {
          for (ivar=0 ; ivar<n_vars ; ivar++) {
             term = delta_ptr[ivar] ;
             bb_ptr[ivar] += wt * term * term ;
          }
       }
   } // For j



   // Debug the objective coefficients
   if (which_i < 5 && iter == 0) {  // Debug first 5 cases in first iteration
       printf("\n=== DEBUG Case %d ===\n", which_i);
       printf("This class: %d\n", this_class);

       // Check aa (intra-class) coefficients
       double aa_sum = 0.0;
       int aa_nonzero = 0;  // FIXED: Should be int, not double
       for (j = 0; j < n_vars; j++) {
           if (fabs(aa_ptr[j]) > 1e-10) aa_nonzero++;
           aa_sum += aa_ptr[j];
       }
       printf("aa (intra): sum=%.6f, nonzero=%d/%d\n", aa_sum, aa_nonzero, n_vars);

       // Check bb (inter-class) coefficients  
       double bb_sum = 0.0;
       int bb_nonzero = 0;  // FIXED: Should be int, not double
       for (j = 0; j < n_vars; j++) {
           if (fabs(bb_ptr[j]) > 1e-10) bb_nonzero++;
           bb_sum += bb_ptr[j];
       }
       printf("bb (inter): sum=%.6f, nonzero=%d/%d\n", bb_sum, bb_nonzero, n_vars);
   }




#if DEBUG_LFS > 1
   MEMTEXT ( "Weights:" ) ;
   for (i=0 ; i<n_cases ; i++) {
      sprintf_s ( msg, " %.4lf", weights_ptr[i] ) ;
      MEMTEXT ( msg ) ;
      }
   MEMTEXT ( "a:" ) ;
   for (i=0 ; i<n_vars ; i++) {
      sprintf_s ( msg, " %.4lf", aa_ptr[i] ) ;
      MEMTEXT ( msg ) ;
      }
   MEMTEXT ( "b:" ) ;
   for (i=0 ; i<n_vars ; i++) {
      sprintf_s ( msg, "%.4lf", bb_ptr[i] ) ;
      MEMTEXT ( msg ) ;
      }
#endif


/*
   Solve the linear programming problem to get the maximum feasible inter-class separation
   We have n_vars constraints that each f<=1, one that their sum <= max_kept,
   and one that their sum >= 1.  The constraints were computed in the constructor.
*/

#if DEBUG_LFS > 1
   MEMTEXT ( "Starting inter-class simplex" ) ;
#endif

   // Reset simplex1 to prevent state contamination between optimizations
   simplex1_managers[safe_thread_id].reset();

   // FIXED: Use safe simplex access
   simplex1_managers[safe_thread_id]->set_objective ( bb_ptr ) ;
   simplex1_managers[safe_thread_id]->set_constraints ( constr_ptr ) ;  // These never change, but simplex 2 does change
   
   // Only call set_slack_variables for HiGHS solver
   // Legacy solver initializes slack variables internally in solve_simple/solve_extended
   extern bool g_use_highs_solver;
   if (g_use_highs_solver) {
       simplex1_managers[safe_thread_id]->set_slack_variables();
   }
   
   int solve_status = simplex1_managers[safe_thread_id]->solve ( 10 * n_vars + 1000 , 1.e-8 ) ;
   if (solve_status != 0) {
       // Solver failed - likely numerical issues or infeasible problem
       return ERROR_SIMPLEX;
   }
   simplex1_managers[safe_thread_id]->get_optimal_values ( &eps_max , f_real_data.data() + which_i * n_vars ) ; // Don't need f_real

   // Then, right after getting eps_max from simplex1, add:
   if (which_i < 5 && iter == 0) {
       printf("Inter-class optimization: eps_max=%.6f\n", eps_max);
   }

   // The next 3 blocks just error check the Simplex maximization

   if (simplex1_managers[safe_thread_id]->check_objective ( bb_ptr , 1.e-8 , &term )) {
      printf ( "\n\nERROR... Simplex maximization of inter-class error failed (objective error=%lf)", term ) ;
      simplex1_managers[safe_thread_id]->print_counters () ;
      return ERROR_SIMPLEX ;
      }

   for (i=0 ; i<n_vars+2 ; i++) {
      if (simplex1_managers[safe_thread_id]->check_constraint ( i , constr_ptr , 1.e-8 , &term )) {
         printf ( "\n\nERROR... Simplex maximization of inter-class error failed (constraint %d error=%lf)", i, term ) ;
         simplex1_managers[safe_thread_id]->print_counters () ;
         return ERROR_SIMPLEX ;
         }
      }

   if (simplex1_managers[safe_thread_id]->check_counters ()) {
      printf ( "\n\nERROR... Simplex maximization of inter-class error failed (counter)" ) ;
      simplex1_managers[safe_thread_id]->print_counters () ;
      return ERROR_SIMPLEX ;
      }

#if DEBUG_LFS > 2
   MEMTEXT ( "Inter-class counters" ) ;
   simplex1_managers[safe_thread_id]->print_counters () ;
#endif

/*
   Test various beta values for minimizing the intra-class separation.
   The inter-class separation b f > beta eps will be an additional constraint
   that will have to be set in test_beta.  But the coefficients are the same
   for all betas, so we might as well set them now and just change the limit
   when we change beta in test_beta().
*/

   // Make a thread-safe copy of the constraints to avoid race conditions.
   std::vector<double> thread_constraints_copy = constraints_data[safe_thread_id];
   double* thread_constr_ptr = thread_constraints_copy.data();

   // Now, set the coefficients for the final constraint on the thread-safe copy.
   // This constraint is for the inter-class separation: b'f >= beta * eps_max
   double* last_row_ptr = thread_constr_ptr + (n_vars + 2) * (n_vars + 1);
   for (j = 0; j < n_vars; j++) {
       last_row_ptr[j + 1] = bb_ptr[j];
   }
   
   best_crit = -1.e60 ;
 
   //debug:
   if (which_i < 5) {
       // Check bb_ptr sum to understand feasibility
       double bb_sum = 0.0;
       for (j = 0; j < n_vars; j++) {
           bb_sum += bb_ptr[j];
       }
       printf("\nCase %d: bb_sum=%.3f, eps_max=%.3f\n", which_i, bb_sum, eps_max);
       printf("If all f=1, max possible bf = %.3f\n", bb_sum);
       printf("For beta=0.5, need bf >= %.3f\n", 0.5 * eps_max);
 
       // Check the constraint matrix setup
       // Use thread_constr_ptr for the check to ensure consistency
       double* last_row = thread_constr_ptr + (n_vars + 2) * (n_vars + 1);
       double coef_sum = 0.0;
       for (j = 1; j <= n_vars; j++) {
           coef_sum += last_row[j];
       }
       printf("Constraint coefficients sum = %.3f (should equal bb_sum)\n", coef_sum);
   }
 
   int successful_betas = 0;  // Track how many betas succeeded
   
   for (i=1 ; i<=n_beta ; i++) {  // Trial values of beta
      // CRITICAL: Pass the thread-specific constraint pointer to test_beta
      int beta_status = test_beta ( which_i, (double)i / (n_beta + 1), eps_max, &crit, safe_thread_id,
                  aa_ptr, best_binary_ptr, thread_constr_ptr, d_ijk_data.data() + safe_thread_id * n_cases,
                  nc_iwork_data.data() + safe_thread_id * n_cases, weights_ptr, f_real_data.data(),
                  thread_delta_base_ptr );
      
      // CRITICAL FIX: Only consider beta values that returned successful status (0)
      // This ensures only feasible, optimal solutions are used for best criterion selection
      if (beta_status == 0) {
         successful_betas++;
         if (crit > best_crit) {
            best_crit = crit ;
            for (ivar=0 ; ivar<n_vars ; ivar++)              // Copy optimal f for this beta
               best_fbin_ptr[ivar] = best_binary_ptr[ivar] ; // to best f across all betas
            }
         } else {
            // Log failed beta for debugging
            if (which_i < 5) {
               printf("  Beta %d/%d (%.3f) failed with status %d - skipping\n",
                      i, n_beta, (double)i / (n_beta + 1), beta_status);
            }
         }
      }
   
   // ROBUST ERROR HANDLING: Provide safe fallback when all beta values fail
   if (successful_betas == 0) {
      printf("WARNING: All beta values failed for case %d - using emergency fallback\n", which_i);
      
      // Emergency fallback: Use a minimal selection based on largest aa coefficients
      // This ensures algorithm stability even when no optimal solutions are found
      for (ivar=0 ; ivar<n_vars ; ivar++)
         best_fbin_ptr[ivar] = 0 ;
         
      // Select a few variables with largest (most negative) aa values for minimal feature set
      std::vector<std::pair<double, int>> aa_indexed;
      for (ivar = 0; ivar < n_vars; ivar++) {
         aa_indexed.push_back(std::make_pair(aa_ptr[ivar], ivar));
      }
      std::sort(aa_indexed.begin(), aa_indexed.end());  // Sort ascending (most negative first)
      
      // Select top few variables (at least 1, at most 10% of variables)
      int n_emergency = std::max(1, std::min(static_cast<int>(0.1 * n_vars), 5));
      for (ivar = 0; ivar < n_emergency && ivar < static_cast<int>(aa_indexed.size()); ivar++) {
         best_fbin_ptr[aa_indexed[ivar].second] = 1;
      }
      
      printf("  Emergency fallback selected %d variables\n", n_emergency);
   } else {
      if (which_i < 5 || which_i % 100 == 0 || which_i == n_cases - 1) {
         printf("  [Progress] Case %d/%d: Successfully processed %d/%d beta values\n",
                which_i + 1, n_cases, successful_betas, n_beta);
      }
   }

   // We have the best beta's binary f.  Save it.

   iptr = f_binary_data.data() + which_i * n_vars ;
   for (ivar=0 ; ivar<n_vars ; ivar++)
      iptr[ivar] = best_fbin_ptr[ivar] ;

   return 0 ;
}


/*
   process_case_with_weights - Version that uses pre-computed CUDA weights
   This matches the legacy architecture where CUDA computes weights on main thread
   before spawning worker threads
*/
int LFS::process_case_with_weights (
   int which_i ,              // Index of the current case
   int ithread,               // Thread number (lets us access the right work areas)
   int iter,                  // The main loop iteration number
   double* precomputed_weights // Pre-computed weights from CUDA (computed on main thread)
)
{
   // FIXED: Ensure thread ID is within valid bounds
   int safe_thread_id = ithread;
   if (safe_thread_id < 0 || safe_thread_id >= max_threads) {
       // Clamp to valid range to prevent crashes
       safe_thread_id = 0;
       printf("\n\nWARNING... Thread ID %d out of range, using thread 0", ithread);
   }
   
   // Phase 1 Safety Enhancement: Bounds checking for thread index
   if (safe_thread_id >= MAX_THREADS) {
      printf("\n\nERROR... Invalid thread index %d (must be 0-%d)", safe_thread_id, MAX_THREADS-1);
      return ERROR_SIMPLEX;
   }
   
   // Modern architecture: Verify SimplexManager objects are properly initialized
   if (safe_thread_id >= static_cast<int>(simplex1_managers.size()) || !simplex1_managers[safe_thread_id].is_valid()) {
      printf("\n\nERROR... Simplex1 manager not initialized for thread %d", safe_thread_id);
      return ERROR_SIMPLEX;
   }

   int i, j, ivar, this_class, *iptr, *best_binary_ptr, *best_fbin_ptr;
   double *dptr1, *dptr2, *delta_ptr, *constr_ptr, *aa_ptr, *bb_ptr, *weights_ptr, wt, term, eps_max, crit, best_crit ;

   this_class = class_id_data[which_i] ;
   
   // FIXED: Use safe thread ID with bounds checking
   if (safe_thread_id >= static_cast<int>(aa_data.size()) ||
       safe_thread_id >= static_cast<int>(bb_data.size()) ||
       safe_thread_id >= static_cast<int>(constraints_data.size()) ||
       safe_thread_id >= static_cast<int>(best_binary_data.size()) ||
       safe_thread_id >= static_cast<int>(best_fbin_data.size())) {
       printf("\n\nERROR... Work area data not available for thread %d", safe_thread_id);
       return ERROR_SIMPLEX;
   }
   
   // Use legacy pointer interface (which now points to modern container data)
   aa_ptr = aa_data[safe_thread_id].data() ;
   bb_ptr = bb_data[safe_thread_id].data() ;
   constr_ptr = constraints_data[safe_thread_id].data() ;
   best_binary_ptr = best_binary_data[safe_thread_id].data() ;
   best_fbin_ptr = best_fbin_data[safe_thread_id].data() ;
   
   // KEY DIFFERENCE: Use pre-computed weights instead of computing them
   weights_ptr = precomputed_weights;  // Use the weights computed by CUDA on main thread

/*
   Compute delta, the difference between this case and each other case
*/

   // DEFINE the base delta pointer for this thread's workspace here
   double* thread_delta_base_ptr = delta_data.data() + static_cast<size_t>(safe_thread_id) * n_cases * n_vars;

   // The calculation loop remains the same, but we use the base pointer
   dptr1 = cases_data.data() + which_i * n_vars;
   for (j = 0; j < n_cases; j++) {
       dptr2 = cases_data.data() + j * n_vars;
       // Calculate the specific delta for case j
       delta_ptr = thread_delta_base_ptr + j * n_vars;
       for (ivar = 0; ivar < n_vars; ivar++)
           delta_ptr[ivar] = dptr1[ivar] - dptr2[ivar];
   }

/*
   NOTE: Weight computation is SKIPPED here because weights are pre-computed
   This is the key architectural difference - weights were computed by CUDA
   on the main thread before this function was called
*/

/*
   Compute the 'a' vector  and the 'b' vector
   Actually, we compute -a because when we use it later we
   will be minimizing using a maximization algorithm.
*/

   for (j=0 ; j<n_vars ; j++)
      aa_ptr[j] = bb_ptr[j] = 0.0 ;

   // Get the base pointer for this thread's entire delta matrix
   for (j=0 ; j<n_cases ; j++) {
       if (j == which_i)
           continue ;

       // Calculate the pointer to the specific delta vector for case 'j'
       delta_ptr = thread_delta_base_ptr + j * n_vars;

       wt = weights_ptr[j] ;
       if (class_id_data[j] == this_class) {
          for (ivar=0 ; ivar<n_vars ; ivar++) {
             term = delta_ptr[ivar] ;
             aa_ptr[ivar] -= wt * term * term ;
          }
       }
       else {
          for (ivar=0 ; ivar<n_vars ; ivar++) {
             term = delta_ptr[ivar] ;
             bb_ptr[ivar] += wt * term * term ;
          }
       }
   } // For j


/*
   Solve the linear programming problem to get the maximum feasible inter-class separation
*/

   // Reset simplex1 to prevent state contamination between optimizations
   simplex1_managers[safe_thread_id].reset();


   // FIXED: Use safe simplex access
   simplex1_managers[safe_thread_id]->set_objective ( bb_ptr ) ;
   simplex1_managers[safe_thread_id]->set_constraints ( constr_ptr ) ;
   
   // Only call set_slack_variables for HiGHS solver
   // Legacy solver initializes slack variables internally in solve_simple/solve_extended
   extern bool g_use_highs_solver;
   if (g_use_highs_solver) {
       simplex1_managers[safe_thread_id]->set_slack_variables();
   }
   
   int solve_status = simplex1_managers[safe_thread_id]->solve ( 10 * n_vars + 1000 , 1.e-8 ) ;
   if (solve_status != 0) {
       // Solver failed - likely numerical issues or infeasible problem
       return ERROR_SIMPLEX;
   }
   simplex1_managers[safe_thread_id]->get_optimal_values ( &eps_max , f_real_data.data() + which_i * n_vars ) ;

   // Error check the Simplex maximization
   if (simplex1_managers[safe_thread_id]->check_objective ( bb_ptr , 1.e-8 , &term )) {
      printf ( "\n\nERROR... Simplex maximization of inter-class error failed (objective error=%lf)", term ) ;
      simplex1_managers[safe_thread_id]->print_counters () ;
      return ERROR_SIMPLEX ;
      }

   for (i=0 ; i<n_vars+2 ; i++) {
      if (simplex1_managers[safe_thread_id]->check_constraint ( i , constr_ptr , 1.e-8 , &term )) {
         printf ( "\n\nERROR... Simplex maximization of inter-class error failed (constraint %d error=%lf)", i, term ) ;
         simplex1_managers[safe_thread_id]->print_counters () ;
         return ERROR_SIMPLEX ;
         }
      }

   if (simplex1_managers[safe_thread_id]->check_counters ()) {
      printf ( "\n\nERROR... Simplex maximization of inter-class error failed (counter)" ) ;
      simplex1_managers[safe_thread_id]->print_counters () ;
      return ERROR_SIMPLEX ;
      }

/*
   Test various beta values for minimizing the intra-class separation.
*/

   // Make a thread-safe copy of the constraints to avoid race conditions.
   std::vector<double> thread_constraints_copy = constraints_data[safe_thread_id];
   double* thread_constr_ptr = thread_constraints_copy.data();

   // Set the coefficients for the final constraint
   double* last_row_ptr = thread_constr_ptr + (n_vars + 2) * (n_vars + 1);
   for (j = 0; j < n_vars; j++) {
       last_row_ptr[j + 1] = bb_ptr[j];
   }
   
   best_crit = -1.e60 ;
   int successful_betas = 0;
   
   for (i=1 ; i<=n_beta ; i++) {  // Trial values of beta
      // CRITICAL: Pass the thread-specific constraint pointer to test_beta
      int beta_status = test_beta ( which_i, (double)i / (n_beta + 1), eps_max, &crit, safe_thread_id,
                  aa_ptr, best_binary_ptr, thread_constr_ptr, d_ijk_data.data() + safe_thread_id * n_cases,
                  nc_iwork_data.data() + safe_thread_id * n_cases, weights_ptr, f_real_data.data(),
                  thread_delta_base_ptr );
      
      // Only consider beta values that returned successful status
      if (beta_status == 0) {
         successful_betas++;
         if (crit > best_crit) {
            best_crit = crit ;
            for (ivar=0 ; ivar<n_vars ; ivar++)
               best_fbin_ptr[ivar] = best_binary_ptr[ivar] ;
            }
         }
      }
   
   // ROBUST ERROR HANDLING: Provide safe fallback when all beta values fail
   if (successful_betas == 0) {
      printf("WARNING: All beta values failed for case %d - using emergency fallback\n", which_i);
      
      // Emergency fallback: Use a minimal selection based on largest aa coefficients
      for (ivar=0 ; ivar<n_vars ; ivar++)
         best_fbin_ptr[ivar] = 0 ;
         
      // Select a few variables with largest (most negative) aa values
      std::vector<std::pair<double, int>> aa_indexed;
      for (ivar = 0; ivar < n_vars; ivar++) {
         aa_indexed.push_back(std::make_pair(aa_ptr[ivar], ivar));
      }
      std::sort(aa_indexed.begin(), aa_indexed.end());  // Sort ascending (most negative first)
      
      // Select top few variables
      int n_emergency = std::max(1, std::min(static_cast<int>(0.1 * n_vars), 5));
      for (ivar = 0; ivar < n_emergency && ivar < static_cast<int>(aa_indexed.size()); ivar++) {
         best_fbin_ptr[aa_indexed[ivar].second] = 1;
      }
   }

   // We have the best beta's binary f.  Save it.
   iptr = f_binary_data.data() + which_i * n_vars ;
   for (ivar=0 ; ivar<n_vars ; ivar++)
      iptr[ivar] = best_fbin_ptr[ivar] ;

   return 0 ;
}
