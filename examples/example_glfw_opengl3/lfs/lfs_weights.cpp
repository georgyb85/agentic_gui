/******************************************************************************/
/*                                                                            */
/*  LFS_WEIGHTS - Compute weights for LFS algorithm                           */
/*                                                                            */
/******************************************************************************/

// Insert other includes you need here

#include <math.h>
#include <iostream>
#include <iomanip>

#include "const.h"
#include "classes.h"
#include "funcdefs.h"

void LFS::compute_weights ( int which_i, double* weights_ptr, double* delta_ptr, double* d_ijk_ptr, const int* f_prior_ptr )
{
   int j, k, ivar, this_class;
   const int* fk_ptr;
   double term, sum, min_same, min_different;

/*
   Compute the weights
   At first glance it would seem that the way to compute the weights would be to
   loop over j, computing the weights one at a time.
   But because of the nature of the d_ijk terms, it's actually better to zero
   all of the weights and then loop over k, cumulating the weights for each k.
*/

   this_class = class_id_data[which_i] ;

   for (j=0 ; j<n_cases ; j++)
      weights_ptr[j] = 0.0 ;

   for (k=0 ; k<n_cases ; k++) {       // Summation loop that builds all weights one k at a time
      fk_ptr = f_prior_ptr + k * n_vars ;  // Point to f(k) from the prior iteration

       // Compute d_ijk for all j with this fixed which_i and k
       // While we're at it, examine all elements of d_ijk and
       // compute two minimums across all j:
       //   1) Those for which the class of j is the same as that of which_i
       //   2) Those in a different class

       min_same = min_different = 1.e60 ;
       for (j=0 ; j<n_cases ; j++) {
          double* current_delta_ptr = delta_ptr + j * n_vars;
          sum = 0.0 ;
          for (ivar=0 ; ivar<n_vars ; ivar++) {       // Compute norm under metric space k
             if (fk_ptr[ivar])                        // This will be false for most variables
                 sum += current_delta_ptr[ivar] * current_delta_ptr[ivar] ; // Cumulate (squared) norm
             }
          term = sqrt ( sum ) ;  // This is the norm
          
          
          d_ijk_ptr[j] = term ;  // Save it to use soon
 
          if (class_id_data[j] == this_class) {
             if (term < min_same  &&  j != which_i)  // Don't count distance to itself!
                min_same = term ;
             }
          else {
             if (term < min_different)
                min_different = term ;
             }
          } // For j, computing d_ijk and the two mins

       // We now have everything we need to compute a term in the sum over k.
       // Cumulate this sum for every weight.
       // Note that we will never use weight[which_i], so we don't need to compute it.
       // But doing so is faster than checking for j==which_i.

       for (j=0 ; j<n_cases ; j++) {    // For every weight, add in this k term
          if (class_id_data[j] == this_class)
              term = d_ijk_ptr[j] - min_same ;
           else
              term = d_ijk_ptr[j] - min_different ;
           weights_ptr[j] += exp ( -term ) ;
           }
       }  // For k

    // The sum over k is computed.  Divide by N to get average.

    for (j=0 ; j<n_cases ; j++)     // For every weight, add in this k term
        weights_ptr[j] /= n_cases ;
}
