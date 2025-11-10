/******************************************************************************/
/*                                                                            */
/*  LFS_BETA - Local Feature Selection evaluates a trial beta                 */
/*                                                                            */
/*  Normally returns 0; returns ERROR_SIMPLEX if Simplex error                */
/*                                                                            */
/******************************************************************************/

// Insert the other includes you need here

#include <math.h>
#include <vector>
#include <algorithm>
#include <utility>

#include "const.h"
#include "classes.h"
#include "funcdefs.h"

// HiGHS headers for solver status validation
#include "highs/Highs.h"
#include "highs/lp_data/HighsLp.h"
#include "highs/lp_data/HighsStatus.h"
#include "highs/lp_data/HStruct.h"

// Error constants
#define ERROR_SIMPLEX 3

// Helper function to convert HiGHS model status to string for logging
const char* highs_status_to_string(HighsModelStatus status) {
    switch (status) {
        case HighsModelStatus::kNotset: return "NOT_SET";
        case HighsModelStatus::kLoadError: return "LOAD_ERROR";
        case HighsModelStatus::kModelError: return "MODEL_ERROR";
        case HighsModelStatus::kPresolveError: return "PRESOLVE_ERROR";
        case HighsModelStatus::kSolveError: return "SOLVE_ERROR";
        case HighsModelStatus::kPostsolveError: return "POSTSOLVE_ERROR";
        case HighsModelStatus::kModelEmpty: return "MODEL_EMPTY";
        case HighsModelStatus::kOptimal: return "OPTIMAL";
        case HighsModelStatus::kInfeasible: return "INFEASIBLE";
        case HighsModelStatus::kUnboundedOrInfeasible: return "UNBOUNDED_OR_INFEASIBLE";
        case HighsModelStatus::kUnbounded: return "UNBOUNDED";
        case HighsModelStatus::kObjectiveBound: return "OBJECTIVE_BOUND";
        case HighsModelStatus::kObjectiveTarget: return "OBJECTIVE_TARGET";
        case HighsModelStatus::kTimeLimit: return "TIME_LIMIT";
        case HighsModelStatus::kIterationLimit: return "ITERATION_LIMIT";
        case HighsModelStatus::kUnknown: return "UNKNOWN";
        default: return "UNKNOWN_STATUS";
    }
}

int LFS::test_beta ( int which_i, double beta, double eps_max, double* crit, int ithread,
                   double* aa_ptr, int* best_binary_ptr, double* constr_ptr, double* d_ijk_ptr,
                   int* nc_iwork_ptr, double* weight_ptr, double* f_real_ptr, double* delta_ptr_for_case)
{
    //debug:
    if (which_i < 5) {
        printf("\n--- test_beta Case %d, beta=%.3f, eps_max=%.3f, beta*eps_max=%.3f ---\n",
            which_i, beta, eps_max, beta * eps_max);
    }


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
   if (safe_thread_id >= static_cast<int>(simplex2_managers.size()) || !simplex2_managers[safe_thread_id].is_valid()) {
      printf("\n\nERROR... Simplex2 manager not initialized for thread %d", safe_thread_id);
      return ERROR_SIMPLEX;
   }

   int j, k, n, ivar, irand, iseed, *fb_ptr, this_class, time ;
   double dtemp, sum, best_func, error, val, rank ;
#if DEBUG_LFS
   char msg[256] ;
#endif

   this_class = class_id_data[which_i] ;


/*
   Minimize the intra-class separation

   Solve the linear programming problem to get the minimum intra-class separation
   for for the specified beta in [0,1].
   We have n_vars constraints that each f<=1, one that their sum <= max_kept,
   and one that their sum >= 1.  These were computed in the constructor and
   comprised everything needed for simplex1.
   But there is now one more for simplex2: their inter-class separation (bf) is at least
   beta times the feasible limit found in the prior optimization.
   The limit as well as bf change for every case, so we have to fill in that last row.
   But the coefficients are the same for all betas, so we set them in the caller.
   All we have to set here is the constant.
*/

#if DEBUG_LFS
   sprintf_s ( msg, "Starting intra-class simplex for beta=%.4lf", beta ) ;
   MEMTEXT ( msg ) ;
#endif

   // Final constraint: b dot f >= beta * eps_max
   // ADD A SMALL STABILITY EPSILON HERE
   double stability_epsilon = 1.0e-9;
   constr_ptr[(n_vars+2)*(n_vars+1)] = beta * eps_max + stability_epsilon;

   // Reset simplex2 to prevent state contamination between optimizations
   simplex2_managers[safe_thread_id].reset();

   // FIXED: Use safe simplex access
   simplex2_managers[safe_thread_id]->set_objective ( aa_ptr ) ;
   simplex2_managers[safe_thread_id]->set_constraints ( constr_ptr ) ;
   
   // Only call set_slack_variables for HiGHS solver
   // Legacy solver initializes slack variables internally in solve_simple/solve_extended
   extern bool g_use_highs_solver;
   if (g_use_highs_solver) {
       simplex2_managers[safe_thread_id]->set_slack_variables();
   }
   
   int solve_result = simplex2_managers[safe_thread_id]->solve ( 10 * n_vars + 1000 , 1.e-8 ) ;
   
   // CRITICAL FIX: Check solver return value BEFORE proceeding with get_optimal_values
   // This prevents the algorithm from using garbage data when the solver fails
   
   // The solve() method returns 0 for success, non-zero for failure
   if (solve_result != 0) {
       // Solver failed - this can happen for infeasible problems
       // This is not necessarily an error - some beta values may create infeasible constraints
       // Return a very bad criterion value to ensure this beta is not selected
       *crit = -1.0e60;
       return 0;  // Return 0 (not ERROR_SIMPLEX) since this is an expected possibility
   }
   
   // If we reach here, the solver found an optimal solution - safe to proceed
   double* fr_ptr = f_real_ptr + which_i * n_vars ;

   simplex2_managers[safe_thread_id]->get_optimal_values ( &dtemp , fr_ptr ) ;
 
    // Debug constraint satisfaction
    if (which_i < 5) {
        double bf_value = 0.0;
        for (int i = 0; i < n_vars; i++) {
            bf_value += constr_ptr[(n_vars+2)*(n_vars+1) + i + 1] * fr_ptr[i];
        }
        printf("  After simplex2: bf_value=%.6f, constraint=%.6f (bf >= beta*eps_max)\n",
               bf_value, beta * eps_max);
    }


   // debug:
   if (which_i < 5) {
       printf("  Intra-class optimization value: %.6f\n", dtemp);
       double fr_sum = 0.0, fr_max = 0.0;
       int fr_nonzero = 0;
       for (int i = 0; i < n_vars; i++) {
           fr_sum += fr_ptr[i];
           if (fr_ptr[i] > 1e-6) {
               fr_nonzero++;
               if (fr_ptr[i] > fr_max) fr_max = fr_ptr[i];
           }
       }
       printf("  Real f: sum=%.6f, max=%.6f, nonzero=%d/%d\n",
           fr_sum, fr_max, fr_nonzero, n_vars);

       // If all zeros, let's check the constraint value
       if (fr_nonzero == 0) {
           printf("  WARNING: All real f values are zero/near-zero!\n");
           printf("  Constraint limit was: beta*eps_max = %.3f\n", beta * eps_max);
       }
   }

   // The next 3 blocks are just error checking the simplex optimization

   if (simplex2_managers[safe_thread_id]->check_objective ( aa_ptr , 1.e-8 , &error )) {
      printf ( "\n\nERROR... Simplex minimization of intra-class error failed (objective error=%lf)", error ) ;
      simplex2_managers[safe_thread_id]->print_counters () ;
      return ERROR_SIMPLEX ;
      }

   for (j=0 ; j<n_vars+3 ; j++) {
      if (simplex2_managers[safe_thread_id]->check_constraint ( j , constr_ptr , 1.e-8 , &error )) {
         printf ( "\n\nERROR... Simplex minimization of intra-class error failed (constraint %d error=%lf)", j, error ) ;
         simplex2_managers[safe_thread_id]->print_counters () ;
         return ERROR_SIMPLEX ;
         }
      }

   if (simplex2_managers[safe_thread_id]->check_counters ()) {
      printf ( "\n\nERROR... Simplex minimization of intra-class error failed (counters)" ) ;
      simplex2_managers[safe_thread_id]->print_counters () ;
      return ERROR_SIMPLEX ;
      }

#if DEBUG_LFS > 2
   sprintf_s ( msg, "Intra-class counters" ) ;
   MEMTEXT ( msg ) ;
   simplex2_managers[ithread]->print_counters () ;
#endif


/*
   Convert the real-valued optimal f to binary optimal f

   At this point we have (for this which_i and beta) the REAL values of the
   optimal variable selectors.  But we need binary values.  So use a Monte-Carlo
   method to find the best binary vector.  To do this, repeat n_rand times:
   For each variable, set the binary flag to 1 with probability equal to the
   real value of f for that variable.  If the binary vector satisfies all of
   the constraints of the simplex minimization just done, compute its value
   for the objective function.  Whichever such randomly generated binary
   vector has the minimum objective function is chosen as the binary vector.
   We don't have to worry about this looping insanely long, because the
   only relevant constraint is the number of '1' flags, and that is satisfied
   with high enough probability to avoid massive failures.
*/

    time = timeGetTime_loc();
    // MATCH LEGACY: fb_ptr points to f_binary storage for this case
    // Legacy uses this as trial storage, then copies best to best_binary
    fb_ptr = f_binary_data.data() + which_i * n_vars;
    
    best_func = -1.e60;
    iseed = which_i + 1;
    
    // Match legacy loop structure exactly
    for (irand = 0; irand < n_rand; irand++) {
        n = 0;
        for (ivar = 0; ivar < n_vars; ivar++) {
            if (fast_unif(&iseed) < fr_ptr[ivar]) {
                fb_ptr[ivar] = 1;
                ++n;
            } else {
                fb_ptr[ivar] = 0;
            }
        }
        
        if (n == 0 || n > max_kept) {
            --irand;  // Match legacy: decrement counter to retry
            continue;
        }
        
        // Evaluate objective (recall aa is actually -a for maximization)
        sum = 0.0;
        for (ivar = 0; ivar < n_vars; ivar++)
            sum += aa_ptr[ivar] * fb_ptr[ivar];
            
        if (sum > best_func) {
            best_func = sum;
            for (ivar = 0; ivar < n_vars; ivar++)
                best_binary_ptr[ivar] = fb_ptr[ivar];
        }
    }
    
    LFStimeRealToBinary += timeGetTime_loc() - time;

/*
   Evaluate the performance quality of this trial beta

   We now have a binary representation of the flag vector that is likely
   close to optimal, if not exactly optimal.  Evaluate its quality.
   Here is the philosophy:  If this f(i) is good and we examine cases near
   x(i) per the basis defined by f(i), those cases that are in class y(i)
   should be closer to x(i) (in the f(i) basis) than those not in y(i).
   So we compute the distances, and use a measure of how well this ordering occurs.

   We borrow d_ijk as a work area for the distances
*/

   time = timeGetTime_loc() ;

   for (j=0 ; j<n_cases ; j++) {
      // USE the passed-in base pointer to calculate the pointer for the current case j
      double* current_delta_ptr = delta_ptr_for_case + j * n_vars;
      sum = 0.0 ;
      for (ivar=0 ; ivar<n_vars ; ivar++) {
         if (best_binary_ptr[ivar])
            sum += current_delta_ptr[ivar] * current_delta_ptr[ivar] ; // Use the new pointer here
         }
      d_ijk_ptr[j] = sum ;
      nc_iwork_ptr[j] = j ;
      }

   qsortdsi ( 0 , n_cases-1 , d_ijk_ptr , nc_iwork_ptr ) ;  // Sort ascending, simultaneously moving index

   // Convert sorted distances to ranks.
   // IMPORTANT: Legacy code has a bug here - uses 'n' instead of 'n_cases'
   // We replicate this bug for exact compatibility

   for (j=0 ; j<n_cases ; ) {
      val = d_ijk_ptr[j] ;
      for (k=j+1 ; k<n ; k++) {  // LEGACY BUG: uses n instead of n_cases
         if (d_ijk_ptr[k] > val)
            break ;
         }
      rank = 0.5 * ((double) j + (double) k + 1.0) ;
      while (j < k)
         d_ijk_ptr[j++] = rank ;
      } // For each case in sorted distance array

   // Sum criterion

   *crit = 0.0 ;
   for (j=0 ; j<n_cases ; j++) {
      k = nc_iwork_ptr[j] ;               // Original index of this sorted case
      if (k == which_i)                   // No sense scoring a case with itself!
         continue ;
      if (class_id_data[k] == this_class)
         *crit -= d_ijk_ptr[j] * weight_ptr[k] ;
      else
         *crit += d_ijk_ptr[j] * weight_ptr[k] ;
      }

   LFStimeBetaCrit += timeGetTime_loc() - time ;

#if DEBUG_LFS > 3
   sprintf_s ( msg, "Class and rank" ) ;
   MEMTEXT ( msg ) ;
   for (j=1 ; j<n_cases ; j++) {
      sprintf_s ( msg, "%2d %8.1lf", class_id_data[nc_iwork_ptr[j]], d_ijk_ptr[j] ) ;
      MEMTEXT ( msg ) ;
      }
#endif

#if DEBUG_LFS
   sprintf_s ( "   Crit=%.1lf", *crit ) ;
   MEMTEXT ( msg ) ;
#endif

   return 0 ;
}
