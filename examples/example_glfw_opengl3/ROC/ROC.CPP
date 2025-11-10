/******************************************************************************/
/*                                                                            */
/*  ROC - Indicator-target relationship with threshold optimization and MCPT  */
/*                                                                            */
/******************************************************************************/

#include <windows.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <conio.h>
#include <assert.h>

#include "const.h"
#include "funcdefs.h"

#define MKTBUF 2048   /* Alloc for indicators in chunks of this many records */
                      /* This is not critical and can be any reasonable vlaue */

/*
   These are defined in MEM64.CPP
   This code is needed only if MALLOC maps to memalloc et cetera.
   If MALLOC maps to malloc (etc) then this is not needed.
*/

#if MEMDEBUG
extern int mem_keep_log ;      // Keep a log file?
extern char mem_file_name[] ;  // Log file name
extern __int64 mem_max_used ;  // Maximum memory ever in use
#endif


/*
--------------------------------------------------------------------------------

   Local subroutine computes and prints ROC table

--------------------------------------------------------------------------------
*/

// Set your bin definitions here

#if 1
static int nbins = 13 ;
static double bins[13] = { 0.01, 0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.95, 0.99 } ;
#else
static int nbins = 27 ;
static double bins[27] = { 0.01, 0.02 , 0.03 , 0.04 , 0.05, 0.06 , 0.07 , 0.08 , 0.09, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.91, 0.92, 0.93, 0.94, 0.95, 0.96 , 0.97 , 0.98 , 0.99 } ;
#endif

void print_ROC (
   int n ,
   double *signal_vals ,
   double *returns ,
   FILE *fp ,
   double *work_signal ,
   double *work_return
   )
{
   int i, k, ibin ;
   double win_above, win_below, lose_above, lose_below ;
   char msg[512], msg2[256] ;

/*
   Copy signals and returns to work areas and sort signal,
   simultaneously moving returns.
*/

   for (i=0 ; i<n ; i++) {
      work_signal[i] = signal_vals[i] ;
      work_return[i] = returns[i] ;
      }

   qsortds ( 0 , n-1 , work_signal , work_return ) ;

/*
   Compute and print table
*/

   fprintf ( fp , "\n\nProfit factor above and below various thresholds" ) ;
   fprintf ( fp , "\n\nThreshold  Frac Gtr/Eq  Long PF    Short PF      Frac Less  Short PF     Long PF" ) ;

   for (ibin=0 ; ibin<nbins ; ibin++) {
      k = (int) (bins[ibin] * (n + 1)) - 1 ;   // Unbiased estimate of location
      if (k < 0)                               // Prevent disaster
         k = 0 ;
      while (k > 0  &&  work_signal[k-1] == work_signal[k])  // Find start of tied block
         --k ;
      if (k == 0  ||  k == nbins-1)            // Ends are degenerate
         continue ;
      win_above = win_below = lose_above = lose_below = 0.0 ;

      for (i=0 ; i<k ; i++) {
         if (work_return[i] > 0.0)
            lose_below += work_return[i] ;
         else
            win_below -= work_return[i] ;
         }

      for (i=k ; i<n ; i++) {
         if (work_return[i] > 0.0)
            win_above += work_return[i] ;
         else
            lose_above -= work_return[i] ;
         }

      sprintf_s ( msg, "%8.3lf %10.3lf", work_signal[k], (double) (n-k) / (double) n ) ;

      if (lose_above > 0.0) {
         sprintf_s ( msg2, "%12.4lf", win_above / lose_above ) ;
         strcat_s ( msg , msg2 ) ;
         }
      else
         strcat_s ( msg , "       Inf  " ) ;

      if (win_above > 0.0) {
         sprintf_s ( msg2, "%12.4lf", lose_above / win_above ) ;
         strcat_s ( msg , msg2 ) ;
         }
      else
         strcat_s ( msg , "       Inf  " ) ;


      sprintf_s ( msg2, "%13.3lf", (double) k / (double) n ) ;
      strcat_s ( msg , msg2 ) ;

      if (lose_below > 0.0) {
         sprintf_s ( msg2, "%12.4lf", win_below / lose_below ) ;
         strcat_s ( msg , msg2 ) ;
         }
      else
         strcat_s ( msg , "       Inf  " ) ;

      if (win_below > 0.0) {
         sprintf_s ( msg2, "%12.4lf", lose_below / win_below ) ;
         strcat_s ( msg , msg2 ) ;
         }
      else
         strcat_s ( msg , "       Inf  " ) ;

      fprintf ( fp , "\n%s", msg ) ;
      }
}


/*
--------------------------------------------------------------------------------

   Local subroutine finds long and short thresholds that maximize profit factor

--------------------------------------------------------------------------------
*/

void opt_thresh (
   int n ,                // Number of indicator/return pairs
   int min_kept ,         // Must keep (trade) at least this many cases
   int flip_sign ,        // If nonzero, flip sign of indicator
   double *signal_vals ,  // Indicators
   double *returns ,      // Associated returns
   double *pf_all ,       // Profit factor of entire dataset
   double *high_thresh ,  // Upper threshold, for long trades
   double *pf_high ,      // Profit factor >= threshold
   double *low_thresh ,   // Lower threshold, for short trades
   double *pf_low ,       // Profit factor < threshold
   double *work_signal ,  // Work area n long
   double *work_return    // Work area n long
   )
{
   int i, best_low_index, best_high_index ;
   double win_above, win_below, lose_above, lose_below, best_high_pf, best_low_pf ;

   if (min_kept < 1)
      min_kept = 1 ;

/*
   Copy signals and returns to work areas and sort signal,
   simultaneously moving returns.
*/

   for (i=0 ; i<n ; i++) {
      work_signal[i] = flip_sign ? (-signal_vals[i]) : signal_vals[i] ;
      work_return[i] = returns[i] ;
      }

   qsortds ( 0 , n-1 , work_signal , work_return ) ;

/*
   Initialize by computing 'above' win/loss for complete set
*/

   win_above = win_below = lose_above = lose_below = 0.0 ;

   for (i=0 ; i<n ; i++) {
      if (work_return[i] > 0.0)
         win_above += work_return[i] ;
      else
         lose_above -= work_return[i] ;
      }

   *pf_all = best_high_pf = win_above / (lose_above + 1.e-30) ;
   best_high_index = 0 ;   // Complete set is implied by threshold at smallest value

/*
   Search all possible thresholds
*/

   best_low_pf = -1.0 ;
   best_low_index = n-1 ;     // This should never be necessary
                              // but it avoids a crash if pathological condition

   for (i=0 ; i<n-1 ; i++) {  // Indicator[i+1] is a candidate threshold

      // Remove this case from high set
      if (work_return[i] > 0.0)
         win_above -= work_return[i] ;
      else
         lose_above += work_return[i] ;

      // Add this case to low set
      if (work_return[i] > 0.0)
         lose_below += work_return[i] ;
      else
         win_below -= work_return[i] ;

      // This trial threshold at i+1 is legitimate only if it is a new value.
      // Subsequent values in a tied block are not real thresholds.
      if (work_signal[i+1] == work_signal[i])
         continue ;

      // If we have enough in the 'above' set, see about updating the best high threshold
      if (n-i-1 >= min_kept) {
         if (win_above / (lose_above + 1.e-30) > best_high_pf) {
            best_high_pf = win_above / (lose_above + 1.e-30) ;
            best_high_index = i+1 ;
            }
         }

      // If we have enough in the 'below' set, see about updating the best low threshold
      if (i+1 >= min_kept) {
         if (win_below / (lose_below + 1.e-30) > best_low_pf) {
            best_low_pf = win_below / (lose_below + 1.e-30) ;
            best_low_index = i+1 ;
            }
         }
#if 0
      char msg[256] ;
      sprintf_s ( msg , "Thr=%7.3lf  WA=%6.2lf  LA=%6.2lf  WB=%6.2lf  LB=%6.2lf  PFA=%6.2lf  PFB=%6.2lf",
                  work_signal[i+1], win_above, lose_above, win_below, lose_below, win_above / (lose_above + 1.e-30), win_below / (lose_below + 1.e-30) ) ;
      MEMTEXT ( msg ) ;
#endif
      } // For all trial thresholds

   *high_thresh = work_signal[best_high_index] ;
   *low_thresh = work_signal[best_low_index] ;
   *pf_high = best_high_pf ;
   *pf_low = best_low_pf ;
}


/*
--------------------------------------------------------------------------------

   Local subroutine performs MCPT of optimal threshold

--------------------------------------------------------------------------------
*/

void opt_MCPT (
   int n ,                // Number of indicator/return pairs
   int min_kept ,         // Must keep (trade) at least this many cases
   int flip_sign ,        // If nonzero, flip sign of indicator
   int nreps ,            // Number of replications, including non-permuted
   double *signal_vals ,  // Indicators
   double *returns ,      // Associated returns
   double *pf_all ,       // Profit factor of entire dataset
   double *high_thresh ,  // Upper threshold, for long trades
   double *pf_high ,      // Profit factor >= threshold
   double *low_thresh ,   // Lower threshold, for short trades
   double *pf_low ,       // Profit factor < threshold
   double *pval_long ,    // p-value for long trades
   double *pval_short ,   // p-value for short trades
   double *pval_best ,    // p-value for best-side trades
   double *work_signal ,  // Work area n long
   double *work_return ,  // Work area n long
   double *work_permute   // Work area n long
   )
{
   int i, j, irep, long_count, short_count, best_count ;
   double dtemp, pf_long, pf_short, long_thresh, short_thresh, best_pf, original_best_pf ;

   for (i=0 ; i<n ; i++)
      work_permute[i] = returns[i] ;

   for (irep=0 ; irep<nreps ; irep++) {

      if (irep) {   // Shuffle replication?
         i = n ;
         while (i > 1) {  // While at least 2 to shuffle
            j = (int) (unifrand_fast() * i) ;
            if (j >= i)
               j = i - 1 ;
            dtemp = work_permute[--i] ;
            work_permute[i] = work_permute[j] ;
            work_permute[j] = dtemp ;
            }
         }

      opt_thresh ( n , min_kept , flip_sign , signal_vals , work_permute ,
                   pf_all , &long_thresh , &pf_long , &short_thresh , &pf_short ,
                   work_signal , work_return ) ;

      best_pf = (pf_long > pf_short) ? pf_long : pf_short ;

      if (irep == 0) {
         *high_thresh = long_thresh ;
         *pf_high = pf_long ;
         *low_thresh = short_thresh ;
         *pf_low = pf_short ;
         original_best_pf = best_pf ;
         long_count = short_count = best_count = 1 ;
         }

      else {
         if (pf_long >= *pf_high)
            ++long_count ;
         if (pf_short >= *pf_low)
            ++short_count ;
         if (best_pf >= original_best_pf)
            ++best_count ;
         }
      } // For irep

   *pval_long = (double) long_count / (double) nreps ;
   *pval_short = (double) short_count / (double) nreps ;
   *pval_best = (double) best_count / (double) nreps ;
}


/*
--------------------------------------------------------------------------------

   Main routine

--------------------------------------------------------------------------------
*/

int main (
   int argc ,    // Number of command line arguments (includes prog name)
   char *argv[]  // Arguments (prog name is argv[0])
   )
{
   int i, j, nsignals, nprices, ncases, bufcnt, *mkt_date, *signal_date ;
   double *open, *high, *low, *close, *signal, *signal_vals, *returns ;
   double *work_signal, *work_return, *work_permute, pf_all, high_thresh, low_thresh, pf_high, pf_low ;
   double pval_long, pval_short, pval_best ;
   char line[256], MarketName[4096], SignalName[4096], *cptr ;
   FILE *fp, *fp_log ;
   SYSTEMTIME systime ;

   mkt_date = signal_date = NULL ;
   open = high = low = close = signal = NULL ;
   fp = fp_log = NULL ;

/*
   Process command line parameters
*/

#if 1
   if (argc != 3) {
      printf ( "\nUsage: SINGLE  MarketName  SignalName" ) ;
      printf ( "\n  MarketName - name of market file (YYYYMMDD Open High Low Close)" ) ;
      printf ( "\n  SignalName - name of signal file" ) ;
      exit ( 1 ) ;
      }

   strcpy_s ( MarketName , argv[1] ) ;
   strcpy_s ( SignalName , argv[2] ) ;
#else
   strcpy_s ( MarketName , "E:\\MarketDataAssorted\\INDEXES\\$OEX.TXT" ) ; // For diagnostics
   strcpy_s ( SignalName , "SIGNAL.TXT" ) ;
#endif

/*
   Memory checking stuff for MEM64.CPP safe memory allocation.
   This code is needed only if MALLOC maps to memalloc et cetera.
   If MALLOC maps to malloc (etc) then this is not needed.
*/

#if MEMDEBUG
   mem_max_used = 0 ;
   mem_keep_log = 1 ;
   _fullpath ( mem_file_name , "MEM.LOG" , 256 ) ;
   if (fopen_s ( &fp , mem_file_name , "wt" )) {
      mem_file_name[0] = 0 ;
      mem_keep_log = 0 ;
      }
   else {
      GetLocalTime ( &systime ) ;
      sprintf_s ( line , "ROC memory log %s  %d/%d/%02d  %d:%02d:%02d" ,
         mem_file_name,
         systime.wMonth, systime.wDay, systime.wYear%100,
         systime.wHour, systime.wMinute, systime.wSecond ) ;
      fprintf ( fp , "%s", line ) ;
      fclose ( fp ) ;
      }
#endif


/*
-------------------------------------------------------------------------------

   Allocate initial memory

-------------------------------------------------------------------------------
*/

   mkt_date = (int *) MALLOC ( MKTBUF * sizeof(int) ) ;
   signal_date = (int *) MALLOC ( MKTBUF * sizeof(int) ) ;
   open = (double *) MALLOC ( MKTBUF * sizeof(double) ) ;
   high = (double *) MALLOC ( MKTBUF * sizeof(double) ) ;
   low = (double *) MALLOC ( MKTBUF * sizeof(double) ) ;
   close = (double *) MALLOC ( MKTBUF * sizeof(double) ) ;
   signal = (double *) MALLOC ( MKTBUF * sizeof(double) ) ;
   signal_vals = (double *) MALLOC ( MKTBUF * sizeof(double) ) ;
   returns = (double *) MALLOC ( MKTBUF * sizeof(double) ) ;
   work_signal = (double *) MALLOC ( MKTBUF * sizeof(double) ) ;
   work_return = (double *) MALLOC ( MKTBUF * sizeof(double) ) ;
   work_permute = (double *) MALLOC ( MKTBUF * sizeof(double) ) ;
   if (mkt_date == NULL  ||  signal_date == NULL  ||  open == NULL  ||  high == NULL  ||  low == NULL
    || close == NULL  ||  signal == NULL  ||  signal_vals == NULL  ||  returns == NULL
    || work_signal == NULL  ||  work_return == NULL  ||  work_permute == NULL) {
      printf ( "\n\nInsufficient memory" ) ;
      fclose ( fp ) ;
      goto FINISH ;
      }


   if (fopen_s ( &fp_log , "ROC.LOG" , "wt" )) {
      printf ( "\nERROR... Cannot open log file ROC.LOG" ) ;
      exit ( 1 ) ;
      }

/*
-------------------------------------------------------------------------------

   Read market prices

-------------------------------------------------------------------------------
*/

   if (fopen_s ( &fp, MarketName , "rt" )) {
      printf ( "\n\nCannot open market history file %s", MarketName ) ;
      fprintf ( fp_log , "\n\nCannot open market history file %s", MarketName ) ;
      exit ( 1 ) ;
      }

   bufcnt = MKTBUF ;  // There are this many record slots available now

   printf ( "\nReading market file..." ) ;

   nprices = 0 ;    // Counts lines (market prices) read
   for (;;) {

      if (feof ( fp )                          // If end of file
       || (fgets ( line , 256 , fp ) == NULL)  // Or unable to read line
       || (strlen ( line ) < 2))               // Or empty line
         break ;                               // We are done reading price history

      if (ferror ( fp )) {                     // If an error reading file
         printf ( "\nError reading line %d of file %s", nprices+1, MarketName ) ;
         fprintf ( fp_log , "\nError reading line %d of file %s", nprices+1, MarketName ) ;
         goto FINISH ;
         }

      if (! bufcnt) {  // Allocate a new memory block if needed
         mkt_date = (int *) REALLOC ( mkt_date , (nprices+MKTBUF) * sizeof(int) ) ;
         open = (double *) REALLOC ( open , (nprices+MKTBUF) * sizeof(double) ) ;
         high = (double *) REALLOC ( high , (nprices+MKTBUF) * sizeof(double) ) ;
         low = (double *) REALLOC ( low , (nprices+MKTBUF) * sizeof(double) ) ;
         close = (double *) REALLOC ( close , (nprices+MKTBUF) * sizeof(double) ) ;
         if (mkt_date == NULL  ||  open == NULL  ||  high == NULL  ||  low == NULL  ||  close == NULL) {
            printf ( "\n\nInsufficient memory reading market history file %s  Press any key...", MarketName ) ;
            fprintf ( fp_log , "\n\nInsufficient memory reading market history file %s  Press any key...", MarketName ) ;
            goto FINISH ;
            } // If insufficient memory
         bufcnt = MKTBUF ;  // There are this many new record slots available now
         } // If allocating new block

      // Parse the date and do a crude sanity check

      for (i=0 ; i<8 ; i++) {
         if ((line[i] < '0')  ||  (line[i] > '9')) {
            printf ( "\nInvalid date reading line %d of file %s", nprices+1, MarketName ) ;
            fprintf ( fp_log , "\nInvalid date reading line %d of file %s", nprices+1, MarketName ) ;
            goto FINISH ;
            }
         }
      mkt_date[nprices] = atoi ( line ) ;

      // Parse the open

      cptr = line + 9 ;  // Price is in this column or beyond
                         // (Next loop allows price to start past this)

      while (*cptr == ' '  ||  *cptr == '\t'  ||  *cptr == ',')  // Delimiters
         ++cptr ;  // Move up to the price

      open[nprices] = atof ( cptr ) ;
      if (open[nprices] > 0.0)                     // Always true, but avoid disaster
         open[nprices] = open[nprices] ;

      while (*cptr  &&  *cptr != ' '  &&  *cptr != ','  &&  *cptr != '\t')
         ++cptr ;  // Pass the price; stop at delimiter

      // Parse the high

      while (*cptr == ' '  ||  *cptr == '\t'  ||  *cptr == ',')  // Delimiters
         ++cptr ;  // Move up to the price

      high[nprices] = atof ( cptr ) ;
      if (high[nprices] > 0.0)                     // Always true, but avoid disaster
         high[nprices] = high[nprices] ;

      while (*cptr  &&  *cptr != ' '  &&  *cptr != ','  &&  *cptr != '\t')
         ++cptr ;  // Pass the price; stop at delimiter

      // Parse the low

      while (*cptr == ' '  ||  *cptr == '\t'  ||  *cptr == ',')  // Delimiters
         ++cptr ;  // Move up to the price

      low[nprices] = atof ( cptr ) ;
      if (low[nprices] > 0.0)                     // Always true, but avoid disaster
         low[nprices] = low[nprices] ;

      while (*cptr  &&  *cptr != ' '  &&  *cptr != ','  &&  *cptr != '\t')
         ++cptr ;  // Pass the price; stop at delimiter

      // Parse the close

      while (*cptr == ' '  ||  *cptr == '\t'  ||  *cptr == ',')  // Delimiters
         ++cptr ;  // Move up to the price

      close[nprices] = atof ( cptr ) ;
      if (close[nprices] > 0.0)                     // Always true, but avoid disaster
         close[nprices] = close[nprices] ;

      while (*cptr  &&  *cptr != ' '  &&  *cptr != ','  &&  *cptr != '\t')
         ++cptr ;  // Pass the price; stop at delimiter

      if (low[nprices] > open[nprices]  ||  low[nprices] > close[nprices]  ||
          high[nprices] < open[nprices]  ||  high[nprices] < close[nprices]) {
         printf ( "\nInvalid open/high/low/close reading line %d of file %s", nprices+1, MarketName ) ;
         fprintf ( fp_log , "\nInvalid open/high/low/close reading line %d of file %s", nprices+1, MarketName ) ;
         goto FINISH ;
         }

      ++nprices  ;
      --bufcnt ;           // One less slot remains

      } // For all lines

   fclose ( fp ) ;
   fp = NULL ;

   printf ( "\nMarket price history read; %d cases from %d to %d", nprices, mkt_date[0], mkt_date[nprices-1] ) ;
   fprintf ( fp_log , "\nMarket price history read; %d cases from %d to %d", nprices, mkt_date[0], mkt_date[nprices-1] ) ;


/*
-------------------------------------------------------------------------------

   Read the signal file

-------------------------------------------------------------------------------
*/

   if (fopen_s ( &fp, SignalName , "rt" )) {
      printf ( "\n\nCannot open signal file %s", SignalName ) ;
      exit ( 1 ) ;
      }

   bufcnt = MKTBUF ;  // There are this many record slots available now

   printf ( "\nReading signal file..." ) ;

   nsignals = 0 ;    // Counts lines (signals) read
   for (;;) {

      if (feof ( fp )                          // If end of file
       || (fgets ( line , 256 , fp ) == NULL)  // Or unable to read line
       || (strlen ( line ) < 2))               // Or empty line
         break ;                               // We are done reading signal file

      if (ferror ( fp )) {                     // If an error reading file
         printf ( "\nError reading line %d of file %s", nprices+1, SignalName ) ;
         fprintf ( fp_log , "\nError reading line %d of file %s", nprices+1, SignalName ) ;
         goto FINISH ;
         }

      if (! bufcnt) {  // Allocate a new memory block if needed
         signal_date = (int *) REALLOC ( signal_date , (nsignals+MKTBUF) * sizeof(int) ) ;
         signal = (double *) REALLOC ( signal , (nsignals+MKTBUF) * sizeof(double) ) ;
         signal_vals = (double *) REALLOC ( signal_vals , (nsignals+MKTBUF) * sizeof(double) ) ;
         returns = (double *) REALLOC ( returns , (nsignals+MKTBUF) * sizeof(double) ) ;
         work_signal = (double *) REALLOC ( work_signal , (nsignals+MKTBUF) * sizeof(double) ) ;
         work_return = (double *) REALLOC ( work_return , (nsignals+MKTBUF) * sizeof(double) ) ;
         work_permute = (double *) REALLOC ( work_permute , (nsignals+MKTBUF) * sizeof(double) ) ;
         if (signal_date == NULL  ||  signal == NULL  ||  signal_vals == NULL  ||  returns == NULL
          || work_signal == NULL  ||  work_return == NULL  ||  work_permute == NULL) {
            printf ( "\n\nInsufficient memory reading signal file %s  Press any key...", SignalName ) ;
            fprintf ( fp_log , "\n\nInsufficient memory reading signal file %s  Press any key...", SignalName ) ;
            goto FINISH ;
            } // If insufficient memory
         bufcnt = MKTBUF ;  // There are this many new record slots available now
         } // If allocating new block

      // Parse the date and do a crude sanity check

      for (i=0 ; i<8 ; i++) {
         if ((line[i] < '0')  ||  (line[i] > '9')) {
            printf ( "\nInvalid date reading line %d of file %s", nprices+1, MarketName ) ;
            fprintf ( fp , "\nInvalid date reading line %d of file %s", nprices+1, MarketName ) ;
            goto FINISH ;
            }
         }
      signal_date[nsignals] = atoi ( line ) ;

      // Parse the signal

      cptr = line + 9 ;  // Price is in this column or beyond
                         // (Next loop allows price to start past this)

      while (*cptr == ' '  ||  *cptr == '\t'  ||  *cptr == ',')  // Delimiters
         ++cptr ;  // Move up to the signal

      signal[nsignals] = atof ( cptr ) ;

      while (*cptr  &&  *cptr != ' '  &&  *cptr != ','  &&  *cptr != '\t')
         ++cptr ;  // Pass the price; stop at delimiter; not needed

      ++nsignals  ;
      --bufcnt ;           // One less slot remains

      } // For all lines

   fclose ( fp ) ;
   fp = NULL ;

   printf ( "\nSignal file read; %d cases from %d to %d", nsignals, signal_date[0], signal_date[nsignals-1] ) ;
   fprintf ( fp_log , "\nSignal file read; %d cases from %d to %d", nsignals, signal_date[0], signal_date[nsignals-1] ) ;


/*
-------------------------------------------------------------------------------

   We have market price history and signals.

   Pass through all signals.  For each, find (if possible) a market record with the same date.
   When we have such a pair, copy the signal to signal_vals and place the log ratio next day
   market return in returns.  This return is the close of the signal day to the next record's close.
   A more conservative approach would open the trade at the next day's open, but the purpose
   of this demonstration is not precisely accurate returns, but ROC, optimal threshold, and MCPT studies.
   These require only reasonably fair and, more importantly, true OOS returns.

-------------------------------------------------------------------------------
*/

   ncases = 0 ;   // Number of signal/return pairs counf
   j = 0 ;        // Index in market prices of current record

   for (i=0 ; i<nsignals ; i++) {          // Check each signal
      if (signal_date[i] < mkt_date[j])    // Signal has to catch up to market?
         continue ;
      while (j < nprices-2  &&  mkt_date[j] < signal_date[i]) // Bring market up to this signal
         ++j ;
      if (mkt_date[j] == signal_date[i]) { // We've got a matchup
         signal_vals[ncases] = signal[i] ;
         returns[ncases] = log ( close[j+1] / close[j] ) ;
         ++ncases ;
         } // Processing a match
      if (j >= nprices-2)                   // Can't compute any more returns
         break ;
      } // For all signals

   printf ( "\n%d signal/return pairs found", ncases ) ;
   fprintf ( fp_log , "\n%d signal/return pairs found", ncases ) ;


/*
-------------------------------------------------------------------------------

   Call subroutines to do the main operations

-------------------------------------------------------------------------------
*/

   print_ROC ( ncases , signal_vals , returns , fp_log , work_signal , work_return ) ;

   opt_thresh ( ncases , ncases/100 , 1 , signal_vals , returns ,
                &pf_all , &high_thresh , &pf_high , &low_thresh , &pf_low ,
                work_signal , work_return ) ;

   if (pf_all > 1.e20)
      fprintf ( fp_log, "\n\nGrand profit factor = infinite" ) ;
   else
      fprintf ( fp_log, "\n\nGrand profit factor = %.3lf", pf_all ) ;

   fprintf ( fp_log, "\nUpper (long) threshold = %.4lf", high_thresh ) ;
   if (pf_high > 1.e20)
      fprintf ( fp_log, "  profit factor = infinite" ) ;
   else
      fprintf ( fp_log, "  profit factor = %.3lf", pf_high ) ;

   fprintf ( fp_log, "\nLower (short) threshold = %.4lf", low_thresh ) ;
   if (pf_low > 1.e20)
      fprintf ( fp_log, "  profit factor = infinite" ) ;
   else
      fprintf ( fp_log, "  profit factor = %.3lf", pf_low ) ;

   opt_MCPT ( ncases , ncases/100 , 1 , 1000 , signal_vals , returns ,
              &pf_all , &high_thresh , &pf_high , &low_thresh , &pf_low ,
              &pval_long , &pval_short , &pval_best ,
              work_signal , work_return , work_permute ) ;

    fprintf ( fp_log, "\n\nP-values:  Long=%.3lf  Short=%.3lf  Best=%.3lf",
              pval_long, pval_short, pval_best ) ;
FINISH:
   printf ( "\n\nPress any key..." ) ;
   _getch () ;  // Wait for user to press a key

   if (fp_log != NULL)
      fclose ( fp_log ) ;
   if (fp != NULL)
      fclose ( fp ) ;
   if (mkt_date != NULL)
      FREE ( mkt_date ) ;
   if (signal_date != NULL)
      FREE ( signal_date ) ;
   if (open != NULL)
      FREE ( open ) ;
   if (high != NULL)
      FREE ( high ) ;
   if (low != NULL)
      FREE ( low ) ;
   if (close != NULL)
      FREE ( close ) ;
   if (signal != NULL)
      FREE ( signal ) ;
   if (signal_vals != NULL)
      FREE ( signal_vals ) ;
   if (returns != NULL)
      FREE ( returns ) ;
   if (work_signal != NULL)
      FREE ( work_signal ) ;
   if (work_return != NULL)
      FREE ( work_return ) ;
   if (work_permute != NULL)
      FREE ( work_permute ) ;

#if MEMDEBUG
   memclose () ;
#endif

   exit ( 0 ) ;
}
