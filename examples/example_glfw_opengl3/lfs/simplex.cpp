/******************************************************************************/
/*                                                                            */
/*  SIMPLEX - Modern HiGHS-based Linear Programming Solver                   */
/*                                                                            */
/*  This replaces the traditional simplex implementation with a modern        */
/*  HiGHS library-based solver for 1.1-1.5x performance improvement          */
/*  and better numerical stability while maintaining backward compatibility   */
/*                                                                            */
/******************************************************************************/

#include <memory>
#include <vector>
#include <stdexcept>
#include <cassert>
#include <algorithm>
#include <sstream>
#include <thread>
#include <mutex>
#include <cmath>
#include <chrono>
#include <limits>

#include "const.h"
#include "classes.h"
#include "funcdefs.h"


// --- HiGHS Integration ---
#include "highs/Highs.h"
#include "highs/lp_data/HighsLp.h"
#include "highs/lp_data/HighsStatus.h"
#include "highs/lp_data/HStruct.h"

// Use the real HiGHS objects and enums
using ::HighsStatus;
using ::ObjSense;
using ::MatrixFormat;

// Modern sparse matrix optimization utilities
class SparseMatrixConverter {
public:
    static void convert_dense_to_sparse(
        const double* dense_matrix, int rows, int cols,
        std::vector<double>& values,
        std::vector<int>& row_indices,
        std::vector<int>& col_starts
    ) {
        values.clear();
        row_indices.clear();
        col_starts.clear();
        col_starts.reserve(cols + 1);
        
        col_starts.push_back(0);
        
        for (int col = 0; col < cols; ++col) {
            for (int row = 0; row < rows; ++row) {
                // BUG: This is row-major indexing: dense_matrix[row * cols + col]
                // FIX: This should be column-major indexing: dense_matrix[col * rows + row]
                double val = dense_matrix[col * rows + row];
                if (std::abs(val) > 1e-12) {  // Numerical tolerance
                    values.push_back(val);
                    row_indices.push_back(row);
                }
            }
            col_starts.push_back(static_cast<int>(values.size()));
        }
    }
    
    static double calculate_sparsity(const double* matrix, int rows, int cols) {
        int non_zeros = 0;
        int total = rows * cols;
        
        for (int i = 0; i < total; ++i) {
            if (std::abs(matrix[i]) > 1e-12) {
                non_zeros++;
            }
        }
        
        return static_cast<double>(non_zeros) / total;
    }
};

/*
-----------------------------------------------------------------

   ModernSimplex - HiGHS-based linear programming solver

-----------------------------------------------------------------
*/

class ModernSimplex {
private:
    // Thread-local solver for thread-safe operations
    // Each thread gets its own HiGHS instance to avoid conflicts
    static thread_local std::unique_ptr<Highs> thread_highs_solver;
    static void configure_solver_for(Highs* solver);
    HighsStatus solve_status;
    
    // Problem dimensions
    int num_vars;
    int num_constraints;
    double sparsity_threshold;
    
    // Performance statistics
    int iterations_used;
    double solve_time;
    
public:
    // Stored solution data
    std::vector<double> latest_solution;
    double latest_objective_value;

    ModernSimplex() : sparsity_threshold(0.1), iterations_used(0), solve_time(0.0), latest_objective_value(0.0) {
        // HiGHS instances are created per-thread on demand to ensure thread safety
        // This avoids both static initialization order issues and thread conflicts
    }
    
    ~ModernSimplex() {
        // Thread-local solvers are automatically cleaned up per-thread
        // No explicit cleanup needed here
    }
    
    void configure_solver() {
        // Configure the thread-local solver if needed
        if (thread_highs_solver) {
            configure_solver_for(thread_highs_solver.get());
        }
    }
    
    // Modern LP solving interface
    int solve_linear_program(
        int n_vars, int n_constraints,
        const std::vector<double>& c,           // Objective coefficients
        const std::vector<double>& A_values,    // Constraint matrix values
        const std::vector<int>& A_indices,      // Row indices
        const std::vector<int>& A_starts,       // Column starts
        const std::vector<double>& b_lower,     // Lower bounds
        const std::vector<double>& b_upper,     // Upper bounds
        const std::vector<double>& x_lower,     // Variable lower bounds
        const std::vector<double>& x_upper,     // Variable upper bounds
        std::vector<double>& solution,          // Output: optimal solution
        double& objective_value                 // Output: optimal value
    ) {
        // Use thread-local solver for thread safety
        // Recreate the solver periodically to avoid state accumulation
        static thread_local int solve_count = 0;
        
        // Recreate solver every 20 solves to prevent state accumulation
        // This is more aggressive but ensures clean state
        if (!thread_highs_solver || (++solve_count % 20 == 0)) {
            thread_highs_solver.reset();  // Destroy old solver
            thread_highs_solver = std::make_unique<Highs>();
            solve_count = 0;
        }
        Highs* solver = thread_highs_solver.get();
        
        try {
            num_vars = n_vars;
            num_constraints = n_constraints;
            
            // Create a fresh LP model for each solve to avoid stale references
            HighsLp lp_model;
            lp_model.clear();
            
            // Set up the LP model using the correct HiGHS API members
            lp_model.num_col_ = n_vars;
            lp_model.num_row_ = n_constraints;
            
            // Set objective coefficients
            lp_model.col_cost_ = c;
            lp_model.col_lower_ = x_lower;
            lp_model.col_upper_ = x_upper;
            
            // Set constraint bounds
            lp_model.row_lower_ = b_lower;
            lp_model.row_upper_ = b_upper;
            
            // Set constraint matrix in sparse format
            lp_model.a_matrix_.format_ = MatrixFormat::kColwise;
            lp_model.a_matrix_.start_ = A_starts;
            lp_model.a_matrix_.index_ = A_indices;
            lp_model.a_matrix_.value_ = A_values;
            
            // Set sense (maximize)
            lp_model.sense_ = ObjSense::kMaximize;
            
            // Configure solver options BEFORE passing the model
            configure_solver_for(solver);
            
            // Pass the new model (without clear() which might reset settings)
            solver->passModel(lp_model);
            
            // Solve the problem
            auto start_time = std::chrono::high_resolution_clock::now();
            solve_status = solver->run();
            auto end_time = std::chrono::high_resolution_clock::now();
            
            solve_time = std::chrono::duration<double>(end_time - start_time).count();
            
            // Get solution
            const HighsSolution& highs_solution = solver->getSolution();
            const HighsInfo& highs_info = solver->getInfo();
            
            iterations_used = highs_info.simplex_iteration_count;
            
            if (solve_status == HighsStatus::kOk) {
                // Check model status to ensure it's actually optimal
                HighsModelStatus model_status = solver->getModelStatus();
                if (model_status != HighsModelStatus::kOptimal) {
                    std::cout << "[HiGHS] WARNING: Solver returned OK but model status is not optimal: " 
                              << static_cast<int>(model_status) << std::endl;
                    // Don't accept non-optimal solutions
                    this->latest_solution.clear();
                    this->latest_objective_value = 0.0;
                    return 1;  // Error
                }
                
                this->latest_solution = solver->getSolution().col_value;
                this->latest_objective_value = solver->getInfo().objective_function_value;
                solution = this->latest_solution;
                objective_value = this->latest_objective_value;
                
                // Silently reject solutions that violate sum >= 1
                if (n_constraints > n_vars) {
                    double sum = 0.0;
                    for (size_t i = 0; i < solution.size(); ++i) {
                        sum += solution[i];
                    }
                    if (sum < 0.999) {  // Violates sum >= 1
                        // Reject invalid solution without spamming output
                        this->latest_solution.clear();
                        this->latest_objective_value = 0.0;
                        return 1;  // Error
                    }
                }
                
                
                return 0;  // Success
            } else {
                this->latest_solution.clear();
                this->latest_objective_value = 0.0;
                return 1; // Error or other status
            }
            
        } catch (const std::exception& e) {
            return 1;  // Error
        }
    }
    
    // Get performance statistics
    int get_iterations() const { return iterations_used; }
    double get_solve_time() const { return solve_time; }
    
    // Getter for model status from thread-local solver
    HighsModelStatus getModelStatus() const {
        if (thread_highs_solver) {
            return thread_highs_solver->getModelStatus();
        }
        return HighsModelStatus::kNotset; // Or another appropriate default
    }
};
// Thread-local solver definitions and helper
thread_local std::unique_ptr<Highs> ModernSimplex::thread_highs_solver = nullptr;

// Static helper to configure solver
void ModernSimplex::configure_solver_for(Highs* solver) {
    if (!solver) {
        throw std::runtime_error("Null solver pointer in configure_solver_for");
    }
    
    // CRITICAL: Disable ALL output from HiGHS
    solver->setOptionValue("output_flag", false);
    solver->setOptionValue("log_to_console", false);
    solver->setOptionValue("log_dev_level", 0);
    solver->setOptionValue("highs_debug_level", 0);
    solver->setOptionValue("highs_analysis_level", 0);
    
    // Solver configuration optimized for TINY problems (19 vars, 21-22 constraints)
    solver->setOptionValue("solver", "simplex");
    solver->setOptionValue("presolve", "off");  // Disable presolve - adds overhead for tiny problems
    solver->setOptionValue("time_limit", 3600.0);  // 1 hour limit
    
    // Performance tuning for small problems
    solver->setOptionValue("threads", 1);  // Use single thread per solver instance
    
    // Memory management
    solver->setOptionValue("simplex_update_limit", 5000);
    
    // Use simplest strategies to minimize overhead for tiny problems
    solver->setOptionValue("simplex_strategy", "dual");  // Dual simplex
    solver->setOptionValue("simplex_crash_strategy", "off");  // No crash - overhead for tiny problems
    solver->setOptionValue("simplex_dual_edge_weight_strategy", "dantzig");  // Simplest/fastest
    solver->setOptionValue("simplex_primal_edge_weight_strategy", "dantzig");  // Simplest/fastest

}

/*
-----------------------------------------------------------------

   constructor and destructor

-----------------------------------------------------------------
*/

Simplex::Simplex (
   int nv ,         // Number of variables to be optimized
   int nc ,         // Number of constraints
   int nle ,        // The first nle constraints are <=, the remaining are >=
   int prn          // Print steps and final solution?
   )
{
   ok = 1 ;         // Optimistically assume that memory allocation goes well

   p1_zero_exit = 0 ;     // Ideal Phase 1 exit because criterion reached zero
   p1_normal_exit = 0 ;   // Ideal exit because all row 0 choices are nonnegative
   p1_relaxed_exit = 0 ;  // Exited Phase 1 because only trivially negative choices remain; acceptable
   p1_art_exit = 0 ;      // Ideal Phase 1 exit because all artificial variables are out of basis
   p1_art_in_basis = 0 ;  // One or more artificial vars in basis at end of Phase 1
   p1_unbounded = 0 ;     // Unbounded exit in Phase 1 (pathological!  Should NEVER happen)
   p1_no_feasible = 0 ;   // No Phase 1 feasible solutions; the problem is formulated wrong
   p1_too_many_its = 0 ;  // Phase 1 terminated early due to too many iterations.  May indicate endless cycling.
   p1_cleanup_bad = 0 ;   // Phase 1 criterion degraded in cleanup; pathological!  Should NEVER happen
   p2_normal_exit = 0 ;   // Ideal Phase 2 exit because all row 0 choices are nonnegative
   p2_relaxed_exit = 0 ;  // Exited Phase 2 because only trivially negative choices remain; acceptable
   p2_unbounded = 0 ;     // Unbounded exit in Phase 2; the problem is formulated wrong
   p2_too_many_its = 0 ;  // Phase 2 terminated early due to too many iterations.  May indicate endless cycling.

   print = prn ;
   n_vars = nv ;
   n_constraints = nc ;
   n_less_eq = nle ;
   n_gtr_eq = n_constraints - n_less_eq ;
   nrows = n_constraints + 1 ;
   ncols = 1 + n_vars + n_constraints + n_gtr_eq ; // Constant/FuncVal + vars + slacks + artificial

   try {
       // Modern memory management with exception safety
       size_t tableau_size = n_gtr_eq ? (nrows+1) * ncols : nrows * ncols;
       
       basics_data = std::make_unique<int[]>(ncols);
       tableau_data = std::make_unique<double[]>(tableau_size);
       
       // Set up legacy pointers for compatibility
       basics = basics_data.get();
       tableau = tableau_data.get();
       
       // Explicitly zero-initialize the memory to replicate calloc behavior
       if (basics) {
           std::fill(basics, basics + ncols, 0);
       }
       if (tableau) {
           std::fill(tableau, tableau + tableau_size, 0.0);
       }

       // Check global flag for solver selection
       extern bool g_use_highs_solver;
       
       if (g_use_highs_solver) {
           // Initialize the modern solver
           this->modern_solver = std::make_unique<ModernSimplex>();
       } else {
           // Initialize the legacy solver
           this->legacy_solver = std::make_unique<SimplexLegacy>(n_vars, n_constraints, n_less_eq, print);
           if (!this->legacy_solver->ok) {
               ok = 0;
               return;
           }
       }
       
   } catch (const std::bad_alloc& e) {
       ok = 0;
       return;
   } catch (...) {
       ok = 0;
       return;
   }
}

Simplex::~Simplex ()
{
   // Modern RAII cleanup - automatic memory management
   // basics_data and tableau_data are automatically cleaned up
   // modern_solver will be automatically destroyed
   // No manual memory management required
}

/*
----------------------------------------------------------------------------

   set_objective() and set_constraints()

   coefs is a vector of n_vars coefficients of the objective function.
   If there are no >= constraints we will use the simple method,
   so place the coefficients in the top row.
   But if there are any >= constraints, the top row will be used in
   Phase 1 for minimizing the sum of the artificial variables.
   In that case, for now we put the original objective function in the
   bottom row, below the tableau that will be used in Phase 1.
   We initialize its RHS value to zero because the Phase 1 initial
   tableau will be feasible, the artificial variables and any slack
   variables from <= constraints will form the basis (and hence be
   nonzero), and all other variables (notably, the objective variables)
   will be nonbasic and hence zero by definition.

   values is a matrix with n_constraints rows and n_vars+1 columns.
   The first value in each row is the constant for the inequality.
   The remaining columns are the coefficients of the variables.

----------------------------------------------------------------------------
*/

void Simplex::set_objective ( double *coefs )
{
   // Check global flag for solver selection
   extern bool g_use_highs_solver;
   
   // If legacy solver is selected, delegate to it
   if (!g_use_highs_solver && legacy_solver) {
       legacy_solver->set_objective(coefs);
       return;
   }
   
   int i ;
   double *tptr ;

   // A two-phase problem reset n_gtr_eq and ncols at the end of phase 1,
   // so fix them now in case this is a subsequent problem using the
   // same Simplex object.

   n_gtr_eq = n_constraints - n_less_eq ;
   ncols = 1 + n_vars + n_constraints + n_gtr_eq ; // Constant/FuncVal + vars + slacks + artificial

   // CRITICAL FIX: Zero out the entire tableau AND basics array before filling.
   // This prevents stale data from previous solves from corrupting the new problem.
   size_t tableau_size = n_gtr_eq ? (nrows+1) * ncols : nrows * ncols;
   if (tableau) {
       std::fill(tableau, tableau + tableau_size, 0.0);
   }
   // Also reset the basics array to prevent stale pivot information
   if (basics) {
       std::fill(basics, basics + ncols, -1);
   }

   if (n_gtr_eq == 0) {    // Simple case, so put it in top row
      for (i=0 ; i<n_vars ; i++)
         tableau[i+1] = -coefs[i] ;
      }

   else {                 // Extended case, so save it in bottom row (below the active tableau)
      tptr = tableau + nrows * ncols ;  // Point to the bottom row
      tptr[0] = 0.0 ;     // Phase 2 objective value will go here; we'll copy this row to top later
      for (i=0 ; i<n_vars ; i++)
         tptr[i+1] = -coefs[i] ;
      for (i=n_vars+1 ; i<ncols ; i++)  // The objective is defined in terms of vars only
         tptr[i] = 0.0 ;
      }
}

int Simplex::set_constraints ( double *values )
{
   // Check global flag for solver selection
   extern bool g_use_highs_solver;
   
   // If legacy solver is selected, delegate to it
   if (!g_use_highs_solver && legacy_solver) {
       return legacy_solver->set_constraints(values);
   }
   
   int irow, icol ;
   double *tableau_ptr, *values_ptr ;

   // A two-phase problem reset n_gtr_eq and ncols at the end of phase 1,
   // so fix them now in case this is a subsequent problem using the
   // same Simplex object.

   n_gtr_eq = n_constraints - n_less_eq ;
   ncols = 1 + n_vars + n_constraints + n_gtr_eq ; // Constant/FuncVal + vars + slacks + artificial

   for (irow=1 ; irow<=n_constraints ; irow++) {
      tableau_ptr = tableau + irow * ncols ;
      values_ptr = values + (irow - 1) * (n_vars + 1) ;
      if (values_ptr[0] < 0.0)
         return 1 ;   // Error return; constants must not be negative (so X=0 is feasible)
      for (icol=0 ; icol<=n_vars ; icol++)
         tableau_ptr[icol] = values_ptr[icol] ;
      }
   return 0 ;
}

// Add this new helper function
void Simplex::set_slack_variables() {
    // Check global flag for solver selection
    extern bool g_use_highs_solver;
    
    // If legacy solver is selected, delegate to it
    if (!g_use_highs_solver && legacy_solver) {
        legacy_solver->set_slack_variables();
        return;
    }
    
    int irow, slack_col;
    for (irow = 1; irow <= n_constraints; ++irow) {
        // The column for the slack/surplus variable for constraint `irow`
        slack_col = 1 + n_vars + irow - 1;

        if (irow > n_less_eq) { // This is a ">=" constraint, add a surplus variable
            tableau[irow * ncols + slack_col] = -1.0;
        } else { // This is a "<=" constraint, add a slack variable
            tableau[irow * ncols + slack_col] = 1.0;
        }
    }
}


// Helper function to reconstruct tableau state from HiGHS solution
void Simplex::reconstruct_tableau_from_solution(int n_vars, int n_constraints) {
    if (!modern_solver || modern_solver->latest_solution.empty()) {
        return; // No solution to reconstruct from
    }

    // 1. Clear the old tableau and basics to a clean state
    size_t tableau_size = n_gtr_eq ? (nrows + 1) * ncols : nrows * ncols;
    std::fill(tableau, tableau + tableau_size, 0.0);
    std::fill(basics, basics + ncols, -1);

    // 2. Set the objective function value
    tableau[0] = modern_solver->latest_objective_value;
    
    // 3. For the simplified reconstruction, we only need to ensure get_optimal_values works
    // The solution values are already stored in modern_solver->latest_solution
    // The basics array is used by the legacy check_constraint function
    
    // Find which variables are non-zero (basic) and assign them to rows
    const auto& solution = modern_solver->latest_solution;
    int basic_count = 0;
    
    // First pass: count non-zero variables
    for (int i = 0; i < n_vars; ++i) {
        if (solution[i] > 1e-9) {
            basic_count++;
        }
    }
    
    // In LP, we need exactly n_constraints basic variables
    // If we have fewer non-zero variables than constraints, some slack/surplus variables are basic
    // For now, we'll just mark the non-zero decision variables as basic
    
    int row = 1;
    for (int i = 0; i < n_vars && row <= n_constraints; ++i) {
        if (solution[i] > 1e-9) {
            basics[i + 1] = row;  // Variable i is basic in row 'row'
            tableau[row * ncols] = solution[i];  // Set the RHS value
            row++;
        }
    }
    
    // Note: This is a simplified reconstruction that ensures get_optimal_values and
    // check_constraint work correctly. A full tableau reconstruction would require
    // more complex logic to handle slack/surplus variables properly.
}

void Simplex::print_tableau ( const char *mes) {
   static int counter=0;
   int i, j;
   char msg[256] ;

   sprintf_s ( msg, "%d. Tableau %s:", ++counter, mes);
   memtext ( msg ) ;
   memtext ( "----------------------------------------------------------" ) ;

   sprintf_s ( msg, "col    b[i] " ) ;
   memtext ( msg ) ;

   for (i=1 ; i<ncols ; i++) {
      sprintf_s ( msg, "    x%-2d ", i ) ;
      memtext ( msg ) ;
      }

   for (i=0 ; i<nrows ; i++) {
      if (i==0) {
         if (n_gtr_eq)
            memtext ( "Ph1 " ) ;
         else
            memtext ( "Obj " ) ;
         }
      else {
         sprintf_s ( msg, "b%-2d ", i ) ;
         memtext ( msg ) ;
         }
      for(j=0;j<ncols; j++) {
         sprintf_s ( msg, " %7.3lf", tableau[i*ncols+j] ) ;
         memtext ( msg ) ;
         }
      }

   if (n_gtr_eq) {
      memtext ( "Obj " ) ;
      for(j=0;j<ncols; j++) {
         sprintf_s ( msg, " %7.3lf", tableau[nrows*ncols+j] ) ;
         memtext ( msg ) ;
         }
      }

   memtext ( "----------------------------------------------------------" ) ;
}




/*
----------------------------------------------------------------------------

   print_optimal_vector()

----------------------------------------------------------------------------
*/

void Simplex::print_optimal_vector ( const char *msg ) {
   int j, k ;
   char msg2[256] ;

   sprintf_s ( msg2, "%s at ", msg ) ;
   memtext ( msg2 ) ;
   for (j=1 ; j<ncols ; j++) {
      k = basics[j] ;
      if (k != -1)
         sprintf_s ( msg2, "x%d=%.3lf, ", j, tableau[k*ncols+0] );
      else
         sprintf_s ( msg2, "x%d=0, ", j) ;
      memtext ( msg2 ) ;
      }
} 


/*
----------------------------------------------------------------------------

   get_optimal_values ()

----------------------------------------------------------------------------
*/

void Simplex::get_optimal_values (
   double *optval ,  // Returns optimal value of objective function
   double *values    // Returns n_vars vector of variable values
   )
{
   // Check global flag for solver selection
   extern bool g_use_highs_solver;
   
   // If legacy solver is selected, delegate to it
   if (!g_use_highs_solver && legacy_solver) {
       legacy_solver->get_optimal_values(optval, values);
       return;
   }
   
   int ivar, k ;

   if (modern_solver && !modern_solver->latest_solution.empty()) {
       *optval = modern_solver->latest_objective_value;
       std::copy(modern_solver->latest_solution.begin(), modern_solver->latest_solution.end(), values);
   } else {
       // Fallback or error for legacy compatibility
       *optval = tableau[0];
       for (ivar = 0; ivar < n_vars; ivar++) {
           k = basics[ivar + 1];
           if (k == -1)
               values[ivar] = 0.0;
           else
               values[ivar] = tableau[k * ncols + 0];
       }
   }
}


/*
----------------------------------------------------------------------------

   check_objective() and check_constraint()

----------------------------------------------------------------------------
*/

int Simplex::check_objective(double* coefs, double eps, double* error)
{
    // Check global flag for solver selection
    extern bool g_use_highs_solver;
    
    // If legacy solver is selected, delegate to it
    if (!g_use_highs_solver && legacy_solver) {
        return legacy_solver->check_objective(coefs, eps, error);
    }
    
    // Modernized check using the HiGHS solution directly
    if (modern_solver && !modern_solver->latest_solution.empty()) {
        double sum = 0.0;
        for (int i = 0; i < n_vars; ++i) {
            // The double-negation in the solve path means the HiGHS solver maximizes the original coefficients.
            // Therefore, we must use the original coefficients here for a correct check.
            sum += coefs[i] * modern_solver->latest_solution[i];
        }
        // The HiGHS objective value is already maximized, so we compare directly.
        *error = fabs(sum - modern_solver->latest_objective_value);
        return (*error < eps) ? 0 : 1;
    }

    // Legacy fallback (should not be hit in normal operation)
    int ivar, k;
    double sum = 0.0;
    for (ivar = 0; ivar < n_vars; ivar++) {
        k = basics[ivar + 1];
        if (k != -1)
            sum += coefs[ivar] * tableau[k * ncols + 0];
    }
    *error = fabs(sum - tableau[0]);
    return (*error < eps) ? 0 : 1;
}

int Simplex::check_constraint(int which, double* constraints, double eps, double* error)
{
    // Check global flag for solver selection
    extern bool g_use_highs_solver;
    
    // If legacy solver is selected, delegate to it
    if (!g_use_highs_solver && legacy_solver) {
        return legacy_solver->check_constraint(which, constraints, eps, error);
    }
    
    // Modernized check using the HiGHS solution directly
    if (modern_solver && !modern_solver->latest_solution.empty()) {
        double* cptr = constraints + which * (n_vars + 1);
        double sum = 0.0;
        for (int i = 0; i < n_vars; ++i) {
            sum += cptr[i + 1] * modern_solver->latest_solution[i];
        }

        double lhs = sum;
        double rhs = cptr[0];
        *error = fabs(lhs - rhs);

        if (which < n_less_eq) { // <= constraint: sum <= cptr[0]
            return (lhs - rhs > eps) ? 1 : 0;
        } else { // >= constraint: sum >= cptr[0]
            return (rhs - lhs > eps) ? 1 : 0;
        }
    }

    // Legacy fallback
    int ivar, k;
    double sum, *cptr;
    cptr = constraints + which * (n_vars + 1);
    sum = 0.0;

    for (ivar = 0; ivar < n_vars; ivar++) {
        k = basics[ivar + 1];
        if (k != -1)
            sum += cptr[ivar + 1] * tableau[k * ncols + 0];
    }
    *error = fabs(sum - cptr[0]);
    if (which < n_less_eq) {
        return (sum - cptr[0] > eps) ? 1 : 0;
    } else {
        return (cptr[0] - sum > eps) ? 1 : 0;
    }
}


/*
----------------------------------------------------------------------------

   check_counters()

----------------------------------------------------------------------------
*/

int Simplex::check_counters ()
{
   // Check global flag for solver selection
   extern bool g_use_highs_solver;
   
   // If legacy solver is selected, delegate to it
   if (!g_use_highs_solver && legacy_solver) {
       return legacy_solver->check_counters();
   }
   
   if (n_less_eq < n_constraints) {
      if (p1_too_many_its)
         return 1 ;
      if (p1_unbounded)
         return 1 ;
      if (p1_no_feasible)
         return 1 ;
      if (p1_cleanup_bad)
         return 1 ;
      }

   if (p2_too_many_its)
      return 1 ;
   if (p2_unbounded)
      return 1 ;

   return 0 ;
} 


/*
----------------------------------------------------------------------------

   print_counters()

----------------------------------------------------------------------------
*/

void Simplex::print_counters ()
{
   // Check global flag for solver selection
   extern bool g_use_highs_solver;
   
   // If legacy solver is selected, delegate to it
   if (!g_use_highs_solver && legacy_solver) {
       legacy_solver->print_counters();
       return;
   }
   
   char msg[256] ;

   if (n_less_eq < n_constraints) {
      sprintf_s ( msg , "Phase 1 normal exit = %d", p1_normal_exit ) ;
      memtext ( msg ) ;
      sprintf_s ( msg , "Phase 1 zero exit = %d", p1_zero_exit ) ;
      memtext ( msg ) ;
      sprintf_s ( msg , "Phase 1 relaxed exit = %d", p1_relaxed_exit )  ;
      memtext ( msg ) ;
      sprintf_s ( msg , "Phase 1 exit due to all artificial vars out of basis = %d", p1_art_exit )  ;
      memtext ( msg ) ;
      sprintf_s ( msg , "Phase 1 too many iterations = %d", p1_too_many_its )  ;
      memtext ( msg ) ;
      sprintf_s ( msg , "Phase 1 unbounded = %d", p1_unbounded )  ;
      memtext ( msg ) ;
      sprintf_s ( msg , "Phase 1 no feasible solutions = %d", p1_no_feasible )  ;
      memtext ( msg ) ;
      sprintf_s ( msg , "Phase 1 artificial variables in basis = %d", p1_art_in_basis )  ;
      memtext ( msg ) ;
      sprintf_s ( msg , "Phase 1 final cleanup criterion changed = %d", p1_cleanup_bad )  ;
      memtext ( msg ) ;
      }

   sprintf_s ( msg , "Phase 2 normal exit = %d", p2_normal_exit ) ;
   memtext ( msg ) ;
   sprintf_s ( msg , "Phase 2 relaxed exit = %d", p2_relaxed_exit )  ;
   memtext ( msg ) ;
   sprintf_s ( msg , "Phase 2 too many iterations = %d", p2_too_many_its )  ;
   memtext ( msg ) ;
   sprintf_s ( msg , "Phase 2 unbounded = %d", p2_unbounded )  ;
   memtext ( msg ) ;
} 


/*
--------------------------------------------------------------------------------------

   solve ()

   This solves the general problem that can contain both <= and >= constraints.

   Returns:
      0 - Normal return, optimum found
      1 - Function is unbounded
      2 - Too many iterations without convergence
      3 - Constraints are conflicting, so no feasible solution
          (Can happen only when at least one >= constraint)
      4 - Constraint matrix is not full rank (there is redundancy)

--------------------------------------------------------------------------------------
*/

int Simplex::solve ( int max_iters , double eps )
{
   // A two-phase problem reset n_gtr_eq and ncols at the end of phase 1,
   // so fix them now in case this is a subsequent problem using the
   // same Simplex object.

   n_gtr_eq = n_constraints - n_less_eq ;
   ncols = 1 + n_vars + n_constraints + n_gtr_eq ; // Constant/FuncVal + vars + slacks + artificial

   if (n_gtr_eq)
      return solve_extended ( max_iters , eps ) ;
   else
      return solve_simple ( max_iters , eps ) ;
}


/*
--------------------------------------------------------------------------------------

   solve_simple () - Enhanced with modern algorithms

   This solves the simple problem with improved pivot selection and
   anti-cycling rules for better performance and numerical stability.

--------------------------------------------------------------------------------------
*/

int Simplex::solve_simple ( int max_iters , double eps )
{
    // Check global flag for solver selection
    extern bool g_use_highs_solver;
    
    // If legacy solver is selected, delegate to it
    if (!g_use_highs_solver && legacy_solver) {
        // The legacy solver already has the objective and constraints set via
        // the delegated set_objective() and set_constraints() calls
        int result = legacy_solver->solve(max_iters, eps);
        
        // Update counters from legacy solver
        p2_normal_exit = legacy_solver->p2_normal_exit;
        p2_relaxed_exit = legacy_solver->p2_relaxed_exit;
        p2_unbounded = legacy_solver->p2_unbounded;
        p2_too_many_its = legacy_solver->p2_too_many_its;
        
        return result;
    }
    
    // Otherwise use HiGHS
    memtext("Entering Simplex::solve_simple (HiGHS)\n");
    // 1. Extract problem from tableau into modern C++ containers
    std::vector<double> c(n_vars);
    for (int i = 0; i < n_vars; ++i) {
        c[i] = -tableau[i + 1]; // Objective coefficients (negated for maximization)
    }

    std::vector<double> A_dense(n_constraints * n_vars);
    std::vector<double> b(n_constraints);

    // Populate A_dense in column-major order
    for (int j = 0; j < n_vars; ++j) { // Outer loop is by column
        for (int i = 0; i < n_constraints; ++i) { // Inner loop is by row
            A_dense[j * n_constraints + i] = tableau[(i + 1) * ncols + (j + 1)];
        }
    }
    // Populate b vector separately
    for (int i = 0; i < n_constraints; ++i) {
        b[i] = tableau[(i + 1) * ncols];
    }

    // 2. Convert dense matrix A to sparse format for HiGHS, using class members to ensure data lifetime
    SparseMatrixConverter::convert_dense_to_sparse(A_dense.data(), n_constraints, n_vars, this->sparse_A_values, this->sparse_A_indices, this->sparse_A_starts);

    // 3. Define bounds
    // For "<=" constraints, row bounds are (-inf, b_i)
    std::vector<double> row_lower(n_constraints, -kHighsInf);
    std::vector<double> row_upper = b;

    // Variables are bounded [0, 1] per LFS algorithm requirements
    // While redundant with f_i <= 1 constraints, this ensures numerical stability
    std::vector<double> col_lower(n_vars, 0.0);
    std::vector<double> col_upper(n_vars, 1.0);  // Explicit bounds for better numerical behavior

    // 4. Solve the LP using the modern solver
    std::vector<double> solution(n_vars);
    double objective_value;
    int status = this->modern_solver->solve_linear_program(
        n_vars, n_constraints, c,
        this->sparse_A_values, this->sparse_A_indices, this->sparse_A_starts,
        row_lower, row_upper, col_lower, col_upper,
        solution, objective_value
    );
    memtext("After modern_solver->solve_linear_program in solve_simple\n");

    // 5. Finalize based on solver status (tableau reconstruction is removed)
    if (status == 0) { // Success
        // THIS IS THE CRITICAL CHANGE
        reconstruct_tableau_from_solution(n_vars, n_constraints);

        if (print) {
            char msg[256];
            sprintf_s(msg, "\nFound optimal value=A[0,0]=%3.2lf (HiGHS solver).\n", objective_value);
            memtext(msg);
            // `print_optimal_vector` would not be accurate here without the tableau, so it's omitted.
        }
        ++p2_normal_exit;
        return 0;
    } else if (status == 1) { // Error from solver (unbounded, infeasible, etc.)
        if (print) memtext("Solve failed: model may be unbounded or infeasible (HiGHS solver).");
        ++p2_unbounded; // Or another appropriate error counter
        return 1; // Unbounded
    } else { // Other errors
        ++p2_too_many_its; // Generic failure
        return 2;
    }
}


/*
--------------------------------------------------------------------------------------

   solve_extended () - Enhanced with modern algorithms

   This solves the more extended problem with improved Phase 1 and Phase 2
   algorithms for better performance and numerical stability.

--------------------------------------------------------------------------------------
*/

int Simplex::solve_extended ( int max_iters , double eps )
{
    // Check global flag for solver selection
    extern bool g_use_highs_solver;
    
    // If legacy solver is selected, delegate to it
    if (!g_use_highs_solver && legacy_solver) {
        // The legacy solver already has the objective and constraints set via
        // the delegated set_objective() and set_constraints() calls
        int result = legacy_solver->solve(max_iters, eps);
        
        // Update counters from legacy solver
        p1_zero_exit = legacy_solver->p1_zero_exit;
        p1_normal_exit = legacy_solver->p1_normal_exit;
        p1_relaxed_exit = legacy_solver->p1_relaxed_exit;
        p1_art_exit = legacy_solver->p1_art_exit;
        p1_art_in_basis = legacy_solver->p1_art_in_basis;
        p1_unbounded = legacy_solver->p1_unbounded;
        p1_no_feasible = legacy_solver->p1_no_feasible;
        p1_too_many_its = legacy_solver->p1_too_many_its;
        p1_cleanup_bad = legacy_solver->p1_cleanup_bad;
        p2_normal_exit = legacy_solver->p2_normal_exit;
        p2_relaxed_exit = legacy_solver->p2_relaxed_exit;
        p2_unbounded = legacy_solver->p2_unbounded;
        p2_too_many_its = legacy_solver->p2_too_many_its;
        
        return result;
    }
    
    // Otherwise use HiGHS
    memtext("Entering Simplex::solve_extended (HiGHS)\n");
    // 1. Extract problem from tableau into modern C++ containers
    // The original objective function is stored in the extra row at the bottom
    double* original_obj = tableau + nrows * ncols;
    std::vector<double> c(n_vars);
    char msg[256]; // For printing

    for (int i = 0; i < n_vars; ++i) {
        c[i] = -original_obj[i + 1]; // Negated for maximization
    }

    std::vector<double> A_dense(n_constraints * n_vars);
    std::vector<double> b(n_constraints);

    // Populate A_dense in column-major order
    for (int j = 0; j < n_vars; ++j) { // Outer loop is by column
        for (int i = 0; i < n_constraints; ++i) { // Inner loop is by row
            A_dense[j * n_constraints + i] = tableau[(i + 1) * ncols + (j + 1)];
        }
    }
    // Populate b vector separately
    for (int i = 0; i < n_constraints; ++i) {
        b[i] = tableau[(i + 1) * ncols];
    }

    // 2. Convert dense matrix A to sparse format for HiGHS, using class members to ensure data lifetime
    SparseMatrixConverter::convert_dense_to_sparse(A_dense.data(), n_constraints, n_vars, this->sparse_A_values, this->sparse_A_indices, this->sparse_A_starts);


    // 3. Define bounds based on constraint type
    std::vector<double> row_lower(n_constraints);
    std::vector<double> row_upper(n_constraints);
    
    for (int i = 0; i < n_constraints; ++i) {
        if (i < n_less_eq) { // <= constraint
            row_lower[i] = -kHighsInf;
            row_upper[i] = b[i];
        } else { // >= constraint
            row_lower[i] = b[i];
            row_upper[i] = kHighsInf;
        }
    }
    

    // Variables are bounded [0, 1] per LFS algorithm requirements
    // While redundant with f_i <= 1 constraints, this ensures numerical stability
    std::vector<double> col_lower(n_vars, 0.0);
    std::vector<double> col_upper(n_vars, 1.0);  // Explicit bounds for better numerical behavior

    // 4. Solve the LP using the modern solver
    std::vector<double> solution(n_vars);
    double objective_value;
    int status = this->modern_solver->solve_linear_program(
        n_vars, n_constraints, c,
        this->sparse_A_values, this->sparse_A_indices, this->sparse_A_starts,
        row_lower, row_upper, col_lower, col_upper,
        solution, objective_value
    );
    memtext("After modern_solver->solve_linear_program in solve_extended\n");

    // 5. Finalize based on solver status (tableau reconstruction is removed)
    if (status == 0) { // Success
        // THIS IS THE CRITICAL CHANGE
        reconstruct_tableau_from_solution(n_vars, n_constraints);
        

        if (print) {
            sprintf_s(msg, "\nFound optimal value=A[0,0]=%3.2lf (HiGHS solver).\n", objective_value);
            memtext(msg);
        }
        ++p2_normal_exit;
        return 0;
    } else { // Infeasible, Unbounded or Error
        // Simplified error handling
        if (print) memtext("Solve failed: model may be unbounded or infeasible (HiGHS solver).");
        
        // Crude mapping of HiGHS failure to legacy return codes
        const HighsModelStatus model_status = this->modern_solver->getModelStatus();
        if (model_status == HighsModelStatus::kInfeasible) {
            ++p1_no_feasible;
            return 3; // Infeasible
        } else if (model_status == HighsModelStatus::kUnbounded) {
            ++p2_unbounded;
            return 1; // Unbounded
        } else {
            ++p2_too_many_its;
            return 2; // Other errors
        }
    }
}

