/******************************************************************************/
/*                                                                            */
/*  SIMPLEX LEGACY - Original tableau-based implementation                    */
/*                                                                            */
/******************************************************************************/

// Insert other needed system includes here
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>

#include "const.h"
#include "classes.h"
#include "funcdefs.h"

// Define memory macros for compatibility
#define MALLOC(n) malloc(n)
#define FREE(p) free(p)
#define MEMTEXT(msg) // Disable memory logging for now


/*
-----------------------------------------------------------------

   constructor and destructor

-----------------------------------------------------------------
*/

SimplexLegacy::SimplexLegacy (
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

   basics = (int *) MALLOC ( ncols * sizeof(int) ) ;

   // If any >= we keep original objective in extra row at bottom
   // It will play no active role in Phase 1 operations, but it will endure pivoting
   // right along with the rest of the tableau.  This leaves it all set up for Phase 2.

   if (n_gtr_eq)
      tableau = (double *) MALLOC ( (nrows+1) * ncols * sizeof(double) ) ;
   else
      tableau = (double *) MALLOC ( nrows * ncols * sizeof(double) ) ;

   if (basics == NULL  ||  tableau == NULL) {
      if (basics != NULL) {
         FREE ( basics ) ;
         basics = NULL ;
         }
      if (tableau != NULL) {
         FREE ( tableau ) ;
         tableau = NULL ;
         }
      ok = 0 ;
      return ;
      }
}

SimplexLegacy::~SimplexLegacy ()
{
   if (tableau != NULL)
      FREE ( tableau ) ;
   if (basics != NULL)
      FREE ( basics ) ;
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

void SimplexLegacy::set_objective ( double *coefs )
{
   int i ;
   double *tptr ;

   // A two-phase problem reset n_gtr_eq and ncols at the end of phase 1,
   // so fix them now in case this is a subsequent problem using the
   // same Simplex object.

   n_gtr_eq = n_constraints - n_less_eq ;
   ncols = 1 + n_vars + n_constraints + n_gtr_eq ; // Constant/FuncVal + vars + slacks + artificial

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

int SimplexLegacy::set_constraints ( double *values )
{
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

void SimplexLegacy::set_slack_variables()
{
   // This method is needed for interface compatibility with the modern solver
   // The legacy solver initializes slack variables differently in solve_simple/solve_extended
   // For now, this is a no-op since the slack variables are initialized in the solve methods
   // But we need this method to exist so the code compiles
}

void SimplexLegacy::print_tableau ( const char *mes) {
   static int counter=0;
   int i, j;
   char msg[256] ;

   sprintf_s ( msg, "%d. Tableau %s:", ++counter, mes);
   MEMTEXT ( msg ) ;
   MEMTEXT ( "----------------------------------------------------------" ) ;

   sprintf_s ( msg, "col    b[i] " ) ;
   MEMTEXT ( msg ) ;

   for (i=1 ; i<ncols ; i++) {
      sprintf ( msg, "    x%-2d ", i ) ;
      MEMTEXT ( msg ) ;
      }

   for (i=0 ; i<nrows ; i++) {
      if (i==0) {
         if (n_gtr_eq)
            MEMTEXT ( "Ph1 " ) ;
         else
            MEMTEXT ( "Obj " ) ;
         }
      else {
         sprintf ( msg, "b%-2d ", i ) ;
         MEMTEXT ( msg ) ;
         }
      for(j=0;j<ncols; j++) {
         sprintf ( msg, " %7.3lf", tableau[i*ncols+j] ) ;
         MEMTEXT ( msg ) ;
         }
      }

   if (n_gtr_eq) {
      MEMTEXT ( "Obj " ) ;
      for(j=0;j<ncols; j++) {
         sprintf ( msg, " %7.3lf", tableau[nrows*ncols+j] ) ;
         MEMTEXT ( msg ) ;
         }
      }

   MEMTEXT ( "----------------------------------------------------------" ) ;
}


/*
----------------------------------------------------------------------------

   find_pivot_col()

   This uses the 'traditional' method for finding the pivot column,
   which is the variable that will be entering the basis.
   The pivot column is the column whose top row entry in the tableau
   is the smallest (most negative).

   If none is negative we have achieved convergence to the optimum.

   This traditional method may cause endless cycling if the problem
   specification is degenerate.

----------------------------------------------------------------------------
*/

int SimplexLegacy::find_pivot_column ( int phase , double eps ) {
   int j, pivot_col = 1 ;
   double lowest = tableau[1] ;
   char msg[256] ;
   
   // Phase and eps parameters are not used in this implementation but kept for interface compatibility

   for(j=2; j<ncols; j++) {
      if (tableau[j] < lowest) {
         lowest = tableau[j];
         pivot_col = j;
         }
      }

   if (print) {
      sprintf ( msg, "Entering variable is x%d with top row value = %.5le", pivot_col, lowest ) ;
      MEMTEXT ( msg ) ;
      }

   if (lowest >= 0.0 )
      return -1; // All non-negative columns in row[0], this is optimal.

   // Note that the calling routine will (if it's smart!) be a little more strict.
   // If this routine returns a valid negative column, the calling routine will
   // make sure that it's not just trivially negative, but actually <= -eps.

   return pivot_col;
}


/*
----------------------------------------------------------------------------

   find_pivot_row()

   This uses the 'traditional' method for finding the pivot row,
   which determines the variable that will be leaving the basis.

   The leaving variable is the member (column) of the basis
   whose '1' is in this row.

   The pivot row is the row that has the following property:
   Consider the set of all rows whose pivot column entry in the
   tableau is positive.  For each such row, compute the b (leftmost column
   in the tableau) for that row, divided by the pivot column entry for that row.
   The pivot row is then the row having the smallest such ratio.

   If no row of the pivot column has a positive entry,
   the objective function is unbounded.

   This traditional method may cause endless cycling if the problem
   specification is degenerate.

   Note that I have slightly modified this routine by being a little more
   strict about 'greater than zero'.  In fact, I demand that the pivot
   be greater than 1.e-10.  Otherwise, pivots that are theoretically zero
   but trivially positive due to fpt error may end up as a divisor and
   lead to crazy results.  Of course, in extremely rare cases this change
   may cause unfounded accusations of the problem being unbounded.
   But this is a small price to pay for the greatly increased stability.

----------------------------------------------------------------------------
*/

int SimplexLegacy::find_pivot_row ( int pivot_col ) {
   int i, pivot_row = -1 ;
   double pivot, ratio, min_ratio ;
   char msg[256] ;

   if (print) {
      sprintf ( msg, "Ratios A[row_i,0]/A[row_i,%d] = [",pivot_col) ;
      MEMTEXT ( msg ) ;
      }

   for (i=1 ; i<nrows ; i++) {
      pivot = tableau[i*ncols+pivot_col] ;
      ratio = tableau[i*ncols+0] / pivot ;  // Precompute only to allow printing; may be undefined if pivot==0
      if (print) {
         sprintf ( msg, "%6.4lf ", ratio ) ;       // Don't be alarmed if this prints INF; denom may be zero
         MEMTEXT ( msg ) ;
         }
      if (pivot > 1.e-10  &&  (pivot_row == -1  ||  ratio < min_ratio) )  {
         min_ratio = ratio ;
         pivot_row = i ;
         }
      }

   if (print)
      MEMTEXT ( "]" ) ;

   if (pivot_row == -1) {
      if (print)
         MEMTEXT ( "No valid pivot row found" ) ;
      return -1 ; // Unbounded.
      }

   if (print) {
      sprintf ( msg, "Leaving row %d   Pivot is A[%d,%d]=%.4le, min positive ratio=%.5lf.",
             pivot_row, pivot_row, pivot_col, tableau[pivot_row*ncols+pivot_col], min_ratio ) ;
      MEMTEXT ( msg ) ;
      }

   return pivot_row;
}


/*
----------------------------------------------------------------------------

   do_pivot()

   Given the pivot row and column, this performs the pivot,
   replacing a current basis variable (the leaving variable)
   with a formerly non-basis variable.

   If we are in a phase 1 operation (phase1 nonzero), simultaneously update
   the original objective function that is stored in the extra row
   at the bottom of the tableau.

   If we have finished Phase 1 operation but are cleaning artificial
   variables out of the basis (phase1=2) then do not assert that the pivot
   is positive.  This has nothing to do with the algorithm; it is
   purely diagnostic.

----------------------------------------------------------------------------
*/

void SimplexLegacy::do_pivot ( int row , int col , int phase1 ) {
   int i, j, nr ;
   double pivot;

   pivot = tableau[row*ncols+col];
   assert ( phase1 == 2  ||  pivot > 0.0 ) ;
   assert ( fabs ( pivot ) > 1.e-12 ) ;

   for(j=0;j<ncols;j++)
      tableau[row*ncols+j] /= pivot ;

   assert ( fabs(tableau[row*ncols+col] - 1.0) < 1.e-8 ) ;
   tableau[row*ncols+col] = 1.0 ;  // Make sure no fpt error

   nr = phase1 ? nrows+1 : nrows ;    // The original objective is in extra row at bottom during Phase 1

   for(i=0; i<nr; i++) { // for each remaining row i do
      double multiplier = tableau[i*ncols+col];
      if (i == row)
         continue;
      for(j=0; j<ncols; j++)
         tableau[i*ncols+j] -= multiplier * tableau[row*ncols+j] ;
      assert ( fabs(tableau[i*ncols+col]) < 1.e-8 ) ;
      tableau[i*ncols+col] = 0.0 ;  // Make sure no fpt error
      }
}


/*
----------------------------------------------------------------------------

   print_optimal_vector()

----------------------------------------------------------------------------
*/

void SimplexLegacy::print_optimal_vector ( const char *msg ) {
   int j, k ;
   char msg2[256] ;

   sprintf_s ( msg2, "%s at ", msg ) ;
   MEMTEXT ( msg2 ) ;
   for (j=1 ; j<ncols ; j++) {
      k = basics[j] ;
      if (k != -1)
         sprintf_s ( msg2, "x%d=%.3lf, ", j, tableau[k*ncols+0] );
      else
         sprintf_s ( msg2, "x%d=0, ", j) ;
      MEMTEXT ( msg2 ) ;
      }
} 


/*
----------------------------------------------------------------------------

   get_optimal_values ()

----------------------------------------------------------------------------
*/

void SimplexLegacy::get_optimal_values (
   double *optval ,  // Returns optimal value of objective function
   double *values    // Returns n_vars vector of variable values
   )
{
   int ivar, k ;

   *optval = tableau[0] ;

   for (ivar=0 ; ivar<n_vars ; ivar++) {   // For each variable
      k = basics[ivar+1] ;
      if (k == -1)              // If it's not in the basis
         values[ivar] = 0.0 ;
      else
         values[ivar] = tableau[k*ncols+0] ;
      }
}


/*
----------------------------------------------------------------------------

   check_objective() and check_constraint()

----------------------------------------------------------------------------
*/

int SimplexLegacy::check_objective ( double *coefs , double eps , double *error )
{
   int ivar, k ;
   double sum ;

   sum = 0.0 ;
   for (ivar=0 ; ivar<n_vars ; ivar++) {   // For each variable
      k = basics[ivar+1] ;
      if (k != -1)    // Nonbasic variables (k=-1) are zero by definition
         sum += coefs[ivar] * tableau[k*ncols+0] ;
      }


   *error = fabs ( sum - tableau[0] ) ;
   if (*error < eps)
      return 0 ;
   return 1 ;
}

int SimplexLegacy::check_constraint ( int which , double *constraints , double eps , double *error )
{
   int ivar, k ;
   double sum, *cptr ;

   cptr = constraints + which * (n_vars+1) ;  // Point to this row in the constraint matrix

   sum = 0.0 ;
   for (ivar=0 ; ivar<n_vars ; ivar++) {   // For each variable
      k = basics[ivar+1] ;
      if (k != -1)    // Nonbasic variables (k=-1) are zero by definition
         sum += cptr[ivar+1] * tableau[k*ncols+0] ;
      }

   *error = fabs ( sum - cptr[0] ) ;

   if (which < n_less_eq) {
      if (sum - cptr[0] > eps)
         return 1 ;
      else
         return 0 ;
      }

   else {
      if (cptr[0] - sum > eps)
         return 1 ;
      else
         return 0 ;
      }
}


/*
----------------------------------------------------------------------------

   check_counters()

----------------------------------------------------------------------------
*/

int SimplexLegacy::check_counters ()
{
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

void SimplexLegacy::print_counters ()
{
   char msg[256] ;

   if (n_less_eq < n_constraints) {
      sprintf_s ( msg , "Phase 1 normal exit = %d", p1_normal_exit ) ;
      MEMTEXT ( msg ) ;
      sprintf_s ( msg , "Phase 1 zero exit = %d", p1_zero_exit ) ;
      MEMTEXT ( msg ) ;
      sprintf_s ( msg , "Phase 1 relaxed exit = %d", p1_relaxed_exit )  ;
      MEMTEXT ( msg ) ;
      sprintf_s ( msg , "Phase 1 exit due to all artificial vars out of basis = %d", p1_art_exit )  ;
      MEMTEXT ( msg ) ;
      sprintf_s ( msg , "Phase 1 too many iterations = %d", p1_too_many_its )  ;
      MEMTEXT ( msg ) ;
      sprintf_s ( msg , "Phase 1 unbounded = %d", p1_unbounded )  ;
      MEMTEXT ( msg ) ;
      sprintf_s ( msg , "Phase 1 no feasible solutions = %d", p1_no_feasible )  ;
      MEMTEXT ( msg ) ;
      sprintf_s ( msg , "Phase 1 artificial variables in basis = %d", p1_art_in_basis )  ;
      MEMTEXT ( msg ) ;
      sprintf_s ( msg , "Phase 1 final cleanup criterion changed = %d", p1_cleanup_bad )  ;
      MEMTEXT ( msg ) ;
      }

   sprintf_s ( msg , "Phase 2 normal exit = %d", p2_normal_exit ) ;
   MEMTEXT ( msg ) ;
   sprintf_s ( msg , "Phase 2 relaxed exit = %d", p2_relaxed_exit )  ;
   MEMTEXT ( msg ) ;
   sprintf_s ( msg , "Phase 2 too many iterations = %d", p2_too_many_its )  ;
   MEMTEXT ( msg ) ;
   sprintf_s ( msg , "Phase 2 unbounded = %d", p2_unbounded )  ;
   MEMTEXT ( msg ) ;
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

int SimplexLegacy::solve ( int max_iters , double eps )
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

   solve_simple ()

   This solves the simple problem:
      There are n_vars variables in vector 'x' for which we are to find optimal values.
      A basic constraint is that all x's are nonnegative.
      The coefficients of x are in the n_vars vector c.
      We are to maximize cx subject to n_constraints constraints, all of which are <=:
      Ax <= b
      'A' has n_constraints rows and n_vars columns, and 'b' is an n_constraints vector
      containing all nonnegative numbers.

   The n_constraints+1 by 1+n_vars+n_constraints internal tableau has the following
   structure at the start:
      [0,0] is the value of the objective function, cx.
      The next n_vars elements of the top row are -c, and the rest are 0
      The remaining n_constraints rows have the b values as the first column.
      For each row after the first, the n_vars columns after 'b' contain that row of 'A'.
      The n_constraints square submatrix that remains as the lower-right submatrix
      of the tableau is initialized to an identity matrix.

   Returns:
      0 - Normal return, optimum found
      1 - Function is unbounded
      2 - Too many iterations without convergence

--------------------------------------------------------------------------------------
*/

int SimplexLegacy::solve_simple ( int max_iters , double eps )
{
   int irow, icol, pivot_col, pivot_row, leaving_var, loop, ret_val ;
   char msg[256] ;

/*
   Zero [0,0] which is where we keep the value of the objective function
   Append the slack variables, an identity matrix
   Zero the slack area of the first row
   Initialize the flag vector for basic variables
*/

   tableau[0] = 0.0 ;   // Will be value of objective function
   for (irow=0 ; irow<nrows ; irow++) {
      for (icol=1 ; icol<=n_constraints ; icol++) {
         if (irow == icol) {
            tableau[irow*ncols+n_vars+icol] = 1.0 ;
            basics[n_vars+icol] = icol ;
            }
         else
            tableau[irow*ncols+n_vars+icol] = 0.0 ;
         }
      }

   for (icol=1 ; icol<=n_vars ; icol++)     // Basics[0] is ignored
      basics[icol] = -1 ;                   // Variables start out nonbasic
   
   if (print) {
      print_tableau ( "padded with slack variables" ) ;
      MEMTEXT ( "Basics flags:" ) ;
      for (icol=1 ; icol<ncols ; icol++) {
         sprintf ( msg, " %2d", basics[icol] ) ;
         MEMTEXT ( msg ) ;
         }
      }

/*
   This is the main loop
*/

   for (loop=0 ; loop<max_iters ; loop++ ) {

      // Find the variable that is entering the basis (the column)

      pivot_col = find_pivot_column ( 2, eps ) ;  // Entering variable, phase 2
      if (pivot_col < 0 ) {               // Ideal and standard indication of convergence
         if (print) {
            sprintf ( msg, "\nFound optimal value=A[0,0]=%3.2lf (no negatives in row 0).\n", tableau[0* ncols+0]);
            print_optimal_vector ( "Optimal vector" ) ;
            }
         ++p2_normal_exit ;
         ret_val = 0 ;
         break ;
         }

      // This is a slightly less strict convergence indicator to alleviate problems from fpt errors.
      // If a supposedly valid pivot column was found (a negative number in the top row) but it is
      // only trivially negative, we call it converged, even though it may be not quite there.
      // Allowing the iterations to continue with a tiny value here can introduce horrid fpt issues.

      if (tableau[pivot_col] > -eps) {      // Call it optimum with relaxed condition to avoid tiny pivot
         if (print) {
            sprintf ( msg, "Found relaxed optimal value=A[0,0]=%.4le (no negatives in row 0).", tableau[0* ncols+0]);
            MEMTEXT ( msg ) ;
            print_optimal_vector ( "Optimal vector" ) ;
            }
         ++p2_relaxed_exit ;
         ret_val = 0 ;
         break ;
         }

      // Find the variable that is leaving the basis (the column that has '1' in this row)

      pivot_row = find_pivot_row ( pivot_col ) ;  // Column that has 1 in this row is leaving variable
      if (pivot_row < 0) {
         if (print)
            MEMTEXT ( "unbounded (no pivot_row).");
         ++p2_unbounded ;
         ret_val = 1 ;
         break ;
         }

      // Update our record of which variables are in the basis, and which row has the '1'

      for (icol=1 ; icol<ncols ; icol++) {
         if (basics[icol] == pivot_row) {
            leaving_var = icol ;
            break ;
            }
         }

      if (print) {
         sprintf ( msg, "  Leaving variable is x%d", leaving_var ) ;
         MEMTEXT ( msg ) ;
         }

      assert ( pivot_col != leaving_var ) ;  // The entering and leaving variables must not be the same!
      basics[pivot_col] = pivot_row ;        // Identify the row with the '1' for this newly entered basis var
      basics[leaving_var] = -1 ;             // Flag that the leaving variable is no longer in the basis

      // Do the pivot

      do_pivot ( pivot_row, pivot_col , 0 ) ;

      if (print) {
         print_tableau ( "After pivoting" ) ;
         MEMTEXT ( "Basics flags:" ) ;
         for (icol=1 ; icol<ncols ; icol++) {
            sprintf ( msg, " %2d", basics[icol] ) ;
            MEMTEXT ( msg ) ;
            }
         MEMTEXT ( "" ) ;
         print_optimal_vector ( "Basic feasible solution" ) ;
         }

      } // For loop

   if (loop == max_iters) {
      ++p2_too_many_its ;
      ret_val = 2 ;
      }

   return ret_val ;
}


/*
--------------------------------------------------------------------------------------

   solve_extended ()

   This solves the more extended problem, involving both <= and >= constraints.
      There are n_vars variables in vector 'x' for which we are to find optimal values.
      The coefficients of x are in the n_vars vector c.
      We are to maximize cx subject to n_constraints constraints, the first n_less_eq
      of which are <=, and the remaining n_gtr_eq are >=.
      Ax (<= or >=) b
      'A' has n_constraints rows and n_vars columns, and 'b' is an n_constraints vector
      of all non-negative numbers.

   The n_constraints+2 by 1+n_vars+n_constraints+n_gtr_eq tableau has the following
   structure at the start of phase 1:
      [0,0] is the value of the Phase 1 objective function: sum of all artificial vars,
      written in terms of the non-basis variables.
      The remaining elements of the top row are those coefficients, with 0 for the basis
      variables.  (The basis variables are the artificial variables and the slack variables
      for the <= constraints.)
      The next n_constraints rows have the b values as the first column.
      For each row after the first, the n_vars columns after 'b' contain that row of 'A'.
      Then the next n_constraints columns are the slack coefficients, forming an
      n_constraints square diagonal submatrix with +1 for <= constraints and -1 for >=.
      For the first n_less_eq rows after the first, the remaining columns are zero.
      For the remaining rows, the final n_gtr_eq columns of these rows form an n_gtr_eq
      square identity submatrix.
      The very last ('extra') row is the original objective function, starting with 0,
      then -c, then all zeros.  As Phase 1 progresses, we adjust it as well as the main tableau.

   After the artificial variables have all been driven to zero by the Phase 1 simplex
   algorithm, if any artificial variables are still in the basis we must drive them out,
   even if it means using negative pivots.

   Phase 1 is complete when the sum of artificial variables is zero and no artificial
   variables remain in the basis.  Then we compress out the now useless artificial
   variable columns, as we have a feasible starting solution for Phase 2.
   (If we cannot drive their sum to zero, there is no feasible solution due to conflicting
   constraints.)

   When all is good, we move the now modified original objective function coefficients
   from the bottom row to the top row and finish up by doing the ordinary simplex algorithm.

   Returns:
      0 - Normal return, optimum found
      1 - Function is unbounded
      2 - Too many iterations without convergence
      3 - Conflicting constraints prevent a feasible solution
      4 - Constraint matrix is not full rank (there is redundancy)

--------------------------------------------------------------------------------------
*/

int SimplexLegacy::solve_extended ( int max_iters , double eps )
{
   int i, irow, icol, pivot_col, pivot_row, leaving_var, loop, ret_val, flag ;
   double max_pivot, prior_crit, *srcptr, *destptr ;
   char msg[256] ;

   if (print)
      MEMTEXT ( "Starting Phase 1" ) ;

/*
   We begin with Phase 1, which minimizes the sum of all artificial variables
   Skip the first row for the moment.  Fill in the slack and artificial columns
   of the constraint rows.  To do this:
      Append the slack variables, an identity matrix (with -1 for >= constraints)
      Append the artificial variables, which exist only for >= constraints.
      This is also an identity matrix, all +1 on the diagonal.
*/

   for (irow=1 ; irow<nrows ; irow++) {             // For all constraint rows
      for (icol=1 ; icol<=n_constraints ; icol++) { // Slack variables
         if (irow == icol)
            tableau[irow*ncols+n_vars+icol] = (irow <= n_less_eq) ? 1.0 : -1.0 ;
         else
            tableau[irow*ncols+n_vars+icol] = 0.0 ;
         }
      for (icol=1 ; icol<=n_gtr_eq ; icol++) {      // Artificial variables
         if (irow == icol + n_less_eq)
            tableau[irow*ncols+n_vars+n_constraints+icol] = 1.0 ;
         else
            tableau[irow*ncols+n_vars+n_constraints+icol] = 0.0 ;
         }
      }

/*
   Now initialize the first row.
   Put the coefficients of the Phase 1 objective function into the first row.
   These coefficients are zero for the basis columns (slack vars for <= constraints,
   and all artificial variables (which handle the >= constraints)).
   For the non-basis columns, these are the negative sum (because we are minimizing)
   of the coefficients in the >= constraint rows.  This effectively writes the
   sum of the artificial variables in terms of the non-basis variables.
   Also initialize the flag vector for basic variables.  Basics[0] is ignored.
*/

   tableau[0] = 0.0 ;
   for (i=1 ; i<=n_gtr_eq ; i++)  // Value of Phase 1 objective function
      tableau[0] -= tableau[(n_less_eq+i)*ncols+0] ;

   for (icol=1 ; icol<ncols ; icol++) {
      if (icol <= n_vars) {   // Objective variables are nonbasic
         flag = 1 ;           // Flag this fact if so
         basics[icol] = -1 ;
         }
      else if (icol <= n_vars + n_less_eq) {     // <= slack cols are basic
         flag = 0 ;
         basics[icol] = icol - n_vars ;
         }
      else if (icol <= n_vars + n_constraints) { // >= slack cols are nonbasic
         flag = 1 ;
         basics[icol] = -1 ;
         }
      else {                  // Artificial var cols are basic
         flag = 0 ;
         basics[icol] = icol - n_vars - n_constraints + n_less_eq ;
         }
      tableau[icol] = 0.0 ;
      if (flag) {             // Is this a nonbasic column?
         for (i=1 ; i<=n_gtr_eq ; i++)
            tableau[icol] -= tableau[(n_less_eq+i)*ncols+icol] ;
         }
      }

   if (print) {
      print_tableau ( "padded with slack variables" ) ;
      MEMTEXT ( "Basics flags:" ) ;
      for (icol=1 ; icol<ncols ; icol++) {
         sprintf ( msg, " %2d", basics[icol] ) ;
         MEMTEXT ( msg ) ;
         }
      }

/*
   This is the Phase 1 main loop
*/

   for (loop=0 ; loop<max_iters ; loop++ ) {

      // Find the variable that is entering the basis (the column)

      pivot_col = find_pivot_column ( 1, eps ) ;    // Entering variable, phase 1

      if (pivot_col < 0 ) {                 // Have we reached the optimum?
         if (print) {
            sprintf ( msg, "Found optimal value=A[0,0]=%.4le (no negatives in row 0).", tableau[0* ncols+0] ) ;
            MEMTEXT ( msg ) ;
            print_optimal_vector ( "Optimal vector" ) ;
            }
         ++p1_normal_exit ;
         ret_val = 0 ;
         break ;
         }

      // This is a slightly less strict convergence indicator to alleviate problems from fpt errors.
      // If a supposedly valid pivot column was found (a negative number in the top row) but it is
      // only trivially negative, we call it converged, even though it may be not quite there.
      // Allowing the iterations to continue with a tiny value here can introduce horrid fpt issues.

      if (tableau[pivot_col] > -eps) {      // Call it optimum with relaxed condition to avoid tiny pivot
         if (print) {
            sprintf ( msg, "Found relaxed optimal value=A[0,0]=%.4le (no negatives in row 0).", tableau[0* ncols+0]);
            MEMTEXT ( msg ) ;
            print_optimal_vector ( "Optimal vector" ) ;
            }
         ++p1_relaxed_exit ;
         ret_val = 0 ;
         break ;
         }

      // Find the variable that is leaving the basis (the column that has '1' in this row)

      pivot_row = find_pivot_row ( pivot_col ) ;  // Column that has 1 in this row is leaving variable
      if (pivot_row < 0) {   // This is a pathological situation that, theoretically, should never happen
         if (print)
            MEMTEXT ( "ERROR... Phase 1 is Unbounded (no pivot_row)." ) ;
         ++p1_unbounded ;
         ret_val = 1 ;
         break ;
         }

      // Update our record of which variables are in the basis, and which row has the '1'

      for (icol=1 ; icol<ncols ; icol++) {
         if (basics[icol] == pivot_row) {
            leaving_var = icol ;
            break ;
            }
         }

      if (print) {
         sprintf ( msg, "  Leaving variable is x%d  Pivot=%.4le", leaving_var, tableau[pivot_row*ncols+pivot_col] ) ;
         MEMTEXT ( msg ) ;
         }

      assert ( pivot_col != leaving_var ) ;  // The entering and leaving variables must not be the same!
      basics[pivot_col] = pivot_row ;        // Identify the row with the '1' for this newly entered basis var
      basics[leaving_var] = -1 ;             // Flag that the leaving variable is no longer in the basis

      // Do the pivot

      do_pivot ( pivot_row, pivot_col , 1 ) ;

      if (print) {
         print_tableau ( "After pivoting" ) ;
         sprintf ( msg, "Basics flags:" ) ;
         MEMTEXT ( msg ) ;
         for (icol=1 ; icol<ncols ; icol++) {
            sprintf ( msg, " %2d", basics[icol] ) ;
            MEMTEXT ( msg ) ;
            }
         MEMTEXT ( "" ) ;
         print_optimal_vector ( "Basic feasible solution" ) ;
         }

      // We are finished if the criterion has reached zero or if all artificial variables
      // are out of the basis.  These are both ideal exits.  If we exited above, from no
      // negatives in the top row, it's likely that we have not reached zero, and hence
      // we have no feasible Phase 1 vector.

      if (tableau[0] >= 0.0) {
         if (print)
            MEMTEXT ( "\nPhase 1 success due to crit=0" ) ;
         assert ( fabs ( tableau[0] ) < 1.e-6 ) ;
         ++p1_zero_exit ;
         ret_val = 0 ;
         break ;
         }

      for (icol=n_vars+n_constraints+1 ; icol<ncols ; icol++) {  // Check all artificial variables
         if (basics[icol] != -1)      // If this is basic
            break ;
         }

      if (icol == ncols) {   // True if all artificial variables are nonbasic
         if (print) {
            sprintf ( msg, "Phase 1 success due to all artificial variables out of basis (crit=%.4lf)", tableau[0] ) ;
            MEMTEXT ( msg ) ;
            }
         assert ( fabs ( tableau[0] ) < 1.e-8 ) ;
         ++p1_art_exit ;
         ret_val = 0 ;
         break ;             // In which case we are done with this Phase 1 cleanup
         }

      } // For loop

   if (loop == max_iters) {
      if (print)
         MEMTEXT ( "ERROR... Too many Phase 1 iterations" ) ;
      ++p1_too_many_its ;
      return 2 ;
      }

   if (ret_val)         // We may have had an error (such as unbounded)
      return ret_val ;

/*
   At this point the Phase 1 optimization has converged to an optimum for the sum of the artificial variables.
   Since we are maximizing, we converge upward from a negative sum.
   The optimum had better be zero!  If is is not zero, then a careless user has provided a set of
   constraints that are incompatible, and there is no feasible solution.
*/

   if (tableau[0] < -eps) {
      if (print)
         MEMTEXT ( "\nERROR... no feasible Phase 1 solution" ) ;
      ++p1_no_feasible ;
      return 3 ;
      }

/*
   If we are lucky, all artificial variable have left the basis.  We need that to be the case
   so that we are able to just drop those columns when we switch over to the original objective
   function for phase 2.  If one or more artificial variables are still in the basis we now
   drive them out.  We do this by pivoting with a basic artificial variable as the leaving variable.
   The entering variable can be any non-artificial variable having a nonzero (not necessarily positive!)
   pivot for the row corresponding to this basic artificial variable.  We know that as long as
   the pivot is nonzero we are adding a nonbasic variable, because the other basic variables
   will all have zero in this row.  So we don't have to confirm that the entering variable is nonbasic.
*/

   for (;;) {   // Loop until all artificial variables are cleared out of the basis

      for (icol=n_vars+n_constraints+1 ; icol<ncols ; icol++) {  // Check all artificial variables
         if (basics[icol] != -1)      // If this is basic
            break ;
         }

      if (icol == ncols) {   // True if all artificial variables are nonbasic
         ret_val = 0 ;
         break ;             // In which case we are done with this Phase 1 cleanup
         }

      //  Uh oh.  Column icol is basic, and it corresponds to row basics[icol].  It's gotta go.

      ++p1_art_in_basis ;
      leaving_var = icol ;
      pivot_row = basics[icol] ;

      // Theoretically, we can let the entering variable be any column that has a nonzero entry
      // for this row (the pivot).  But for best stability we choose the pivot that has
      // maximum absolute value.  Note that because the artificial variable at leaving_var has
      // been optimized down to zero, this pivot operation will neither introduce infeasibility
      // nor change the Phase 1 criterion value.

      pivot_col = -1 ;
      max_pivot = 0.0 ;
      for (i=1 ; i<=n_vars+n_constraints ; i++) {   // This is everything except the artificial variables
         if (fabs(tableau[pivot_row*ncols+i]) > max_pivot) {
            max_pivot = fabs(tableau[pivot_row*ncols+i]) ;
            pivot_col = i ;
            }
         }

      if (print) {
         sprintf ( msg, "Phase 1 cleanup; entering variable x%d, pivot row %d (%.4le), leaving variable x%d",
                    pivot_col, pivot_row, max_pivot, leaving_var ) ;
         MEMTEXT ( msg ) ;
         }

      assert ( basics[pivot_col] == -1 ) ;                  // Entering variable must be nonbasic
      assert ( fabs(tableau[pivot_row*ncols+0]) < 1.e-8 ) ; // Leaving artificial variable must be zero

      // If we failed to find a nonzero pivot, the careless user has provided a set of
      // constraints that are not full rank.  We could legitimately continue by simply
      // removing this redundant row, but I think it's better to halt and inform the user.

      if (max_pivot < 1.e-10) {
         if (print)
            MEMTEXT ( "ERROR... Constraint matrix is not full rank.  (It is redundant.)" ) ;
         ret_val = 4 ;
         break ;             // In which case we are done with this Phase 1 cleanup
         }

      // Update our record of which variables are basic and their '1' row
      // then do the pivot

      assert ( pivot_col != leaving_var ) ;  // The entering and leaving variables must not be the same!
      basics[pivot_col] = pivot_row ;        // Identify the row with the '1' for this newly entered basis var
      basics[leaving_var] = -1 ;             // Flag that the leaving variable is no longer in the basis

      prior_crit = tableau[0] ;
      do_pivot ( pivot_row , pivot_col , 2 ) ;
      if (fabs(tableau[0] - prior_crit) > 1.e-8)   // This operation should not change the criterion!
         ++p1_cleanup_bad ;

      if (print) {
         print_tableau ( "After cleanup pivoting" ) ;
         MEMTEXT ( "\nBasics flags:" ) ;
         for (icol=1 ; icol<ncols ; icol++) {
            sprintf ( msg, " %2d", basics[icol] ) ;
            MEMTEXT ( msg ) ;
            }
         MEMTEXT ( "" ) ;
         print_optimal_vector ( "Basic feasible solution" ) ;
         }

      } // Endless cleanup loop

   if (ret_val) {
      if (print)
         MEMTEXT ( "\nAborting due to Phase 1 ERROR" ) ;
      return ret_val ;
      }

/*
   When we get here, we have a feasible solution with all artificial variables out of the basis
   and hence zero by definition.  We have to do two bits of bookkeeping to get ready for Phase 2.

   Recall that when we set up for Phase 1, we stored that actual objective function in an extra
   row at the bottom of the tableau.  Every time we did a pivot we also updated this extra row.
   So now the first of two preparatory things we need to do is copy this extra row up to the
   top row, where it will serve as the objective function for Phase 2.
*/

   if (print)
      MEMTEXT ( "Phase 1 successfully completed.  Moving to Phase 2." ) ;

   srcptr = tableau + nrows * ncols ;   // Point to the extra row at the bottom
   for (icol=0 ; icol<=n_vars+n_constraints ; icol++)
      tableau[icol] = srcptr[icol] ;

/*
   The second preparatory thing we must do is remove all artificial variable columns,
   which are at the far right side of the tableau.
   The next code is trivially inefficient in that it needlessly copies the first few values
   onto themselves.
*/

   srcptr = destptr = tableau ;
   for (irow=0 ; irow<nrows ; irow++) {
      for (icol=0 ; icol<=n_vars+n_constraints ; icol++)
         *destptr++ = *srcptr++ ;
      srcptr += n_gtr_eq ;   // Skip the artificial variables
      }

   ncols -= n_gtr_eq ; // We no longer have artificial variables
   n_gtr_eq = 0 ;

   if (print) {
      print_tableau ( "Ready to start Phase 2" ) ;
      MEMTEXT ( "\nBasics flags:" ) ;
      for (icol=1 ; icol<ncols ; icol++) {
         sprintf ( msg, " %2d", basics[icol] ) ;
         MEMTEXT ( msg ) ;
         }
      MEMTEXT ( "" ) ;
      print_optimal_vector ( "Basic feasible solution" ) ;
      }

/*
   This is the Phase 2 main loop
*/

   for (loop=0 ; loop<max_iters ; loop++ ) {

      // Find the variable that is entering the basis (the column)

      pivot_col = find_pivot_column ( 2, eps ) ;    // Entering variable, phase 2
      if (pivot_col < 0 ) {
         if (print) {
            sprintf ( msg, "Found optimal value=A[0,0]=%3.2lf (no negatives in row 0).\n", tableau[0]);
            MEMTEXT ( msg ) ;
            print_optimal_vector ( "Optimal vector" ) ;
            }
         ++p2_normal_exit ;
         ret_val = 0 ;
         break ;
         }

      // This is a slightly less strict convergence indicator to alleviate problems from fpt errors.
      // If a supposedly valid pivot column was found (a negative number in the top row) but it is
      // only trivially negative, we call it converged, even though it may be not quite there.
      // Allowing the iterations to continue with a tiny value here can introduce horrid fpt issues.

      if (tableau[pivot_col] > -eps) {      // Call it optimum with relaxed condition to avoid tiny pivot
         if (print) {
            sprintf ( msg, "Found relaxed optimal value=A[0,0]=%.4le (no negatives in row 0).", tableau[0]);
            print_optimal_vector ( "Optimal vector" ) ;
            }
         ++p2_relaxed_exit ;
         ret_val = 0 ;
         break ;
         }

      // Find the variable that is leaving the basis (the column that has '1' in this row)

      pivot_row = find_pivot_row ( pivot_col ) ;  // Column that has 1 in this row is leaving variable
      if (pivot_row < 0) {
         if (print)
            MEMTEXT ( "unbounded (no pivot_row).\n" ) ;
         ++p2_unbounded ;
         ret_val = 1 ;
         break ;
         }

      // Update our record of which variables are in the basis, and which row has the '1'

      for (icol=1 ; icol<ncols ; icol++) {
         if (basics[icol] == pivot_row) {
            leaving_var = icol ;
            break ;
            }
         }

      if (print) {
         sprintf ( msg, "  Leaving variable is x%d", leaving_var ) ;
         MEMTEXT ( msg ) ;
         }

      assert ( pivot_col != leaving_var ) ;  // The entering and leaving variables must not be the same!
      basics[pivot_col] = pivot_row ;        // Identify the row with the '1' for this newly entered basis var
      basics[leaving_var] = -1 ;             // Flag that the leaving variable is no longer in the basis

      // Do the pivot

      do_pivot ( pivot_row, pivot_col , 0 ) ;

      if (print) {
         print_tableau ( "After pivoting" ) ;
         MEMTEXT ( "Basics flags:" ) ;
         for (icol=1 ; icol<ncols ; icol++) {
            sprintf ( msg, " %2d", basics[icol] ) ;
            MEMTEXT ( msg ) ;
            }
         MEMTEXT ( "" ) ;
         print_optimal_vector ( "Basic feasible solution" ) ;
         }

      } // For loop

   if (loop == max_iters) {
      ++p2_too_many_its ;
      ret_val = 2 ;
      }

   return ret_val ;
}
