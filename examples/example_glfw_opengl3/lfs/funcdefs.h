#pragma once

#include <cstdio>
#include <cstdint>

// Main LFS function
extern int lfs_do_case(int max_kept, int iterations, int n_rand, int n_beta, int* class_ids, void* progress_callback);
extern int* lfs_get_results();  // Get the feature selection results

extern void audit ( const char *text ) ;
extern int do_lfs ( int npred , int *predictors , int target , int target_bins, int mcpt_type , int mcpt_reps ,
                    int max_kept , int iters , int nrand , int nbeta ) ;
extern int evec_rs ( double *mat_in , int n , int find_vec , double *vect , double *eval , double *workv ) ;
extern double fast_unif ( int *iparam ) ;
extern int hmm_exec ( int nDim , int nstates , int npred , int *preds , int target ,
                      int mcpt_type , int mcpt_reps , int max_printed ) ;
extern double hmm_estimate ( int print , FILE *file_print , int max_iters , int ncases , int nvars , int nstates ,
                             double *data , double *init_probs , double *transition , double *means , double *covars ,
                             double *densities , double *inverse , double *alpha , double *beta , double *state_probs ,
                             double *rwork , int *iwork , double *trans_work1 , double *trans_work2 ) ;
extern void hmm_find_mean_covar ( int ncases , int nvars , int nstates ,
                                  double *data , double *means , double *covars , double *init_covar ) ;
extern void hmm_initialize ( FILE *fp , int n_trials , int ncases , int nvars , int nstates ,
                             double *data , double *init_probs , double *means ,
                             double *covars , double *init_covar , double *best_covar ,
                             double *transition , double *trial_means , double *trial_transition ,
                             double *best_means , double *best_transition , double *densities ,
                             double *inverse , double *alpha , double *rwork , int *iwork ) ;
extern int hmm_mem ( int npred , int *preds , int n_states , int n_init , int max_iters , int mcpt_reps ) ;
extern int invert ( int n , double *x , double *xinv , double *det , double *rwork , int *iwork ) ;
extern int lfs_cuda_classes ( int *classes , char *error_msg ) ;
extern void lfs_cuda_cleanup () ;
extern int lfs_cuda_diff ( int icase ) ;
extern int lfs_cuda_dist () ;
extern int lfs_cuda_get_class ( double *class_ids ) ;
extern int lfs_cuda_get_data ( double *data ) ;
extern int lfs_cuda_get_diff ( double *diff ) ;
extern int lfs_cuda_get_dist ( double *dist ) ;
extern int lfs_cuda_get_flags ( double *flags ) ;
extern int lfs_cuda_get_mindist ( double *same , double *diff ) ;
extern int lfs_cuda_get_trans ( double *trans ) ;
extern int lfs_cuda_get_weights ( double *weights , char *error_msg ) ;
extern int lfs_cuda_init ( int n_cases , int n_vars , double *data  , char *error_msg ) ;
extern int lfs_cuda_flags ( int *flags , char *error_msg ) ;
extern int lfs_cuda_mindist ( int which_i ) ;
extern int lfs_cuda_sum () ;
extern int lfs_cuda_term ( int iclass ) ;
extern int lfs_cuda_transpose () ;
extern void memtext ( const char *text ) ;
extern double mv_normal ( int nv , double *x , double *means , double *inv_covar , double det , double *work ) ;
extern int nom_ord ( int npred , int *preds , int gate , int target ,
                     int mcpt_type , int mcpt_reps ) ;
extern void partition ( int n , double *data , int *npart , double *bnds , int *bins ) ;
extern void qsortd ( int first , int last , double *data ) ;
extern void qsortds ( int first , int last , double *data , double *slave ) ;
extern void qsortdsi ( int first , int last , double *data , int *slave ) ;
extern void qsorti ( int first , int last , int *data ) ;
extern void qsortisd ( int first , int last , int *data , double *slave ) ;
extern int stepwise ( int npred , int *preds , int target , int nkept , int nfolds ,
                      int minpred , int maxpred , int mcpt_type , int mcpt_reps ) ;
extern double unifrand () ;
extern double unifrand_fast () ;  /* NOW thread safe !!! */
//extern int user_pressed_escape () ;
extern double U_test ( int n1 , double *x1 , int n2 , double *x2 , int *iwork , double *work , double *z ) ;

/* Timing functions - Cross-platform using std::chrono */
extern int timeGetTime_loc();

/* Timing variables for performance benchmarking */
extern int LFStimeTotal, LFStimeRealToBinary, LFStimeBetaCrit, LFStimeWeights;
extern int LFStimeCUDA, LFStimeCUDAdiff, LFStimeCUDAdist, LFStimeCUDAmindist;
extern int LFStimeCUDAterm, LFStimeCUDAtranspose, LFStimeCUDAsum, LFStimeCUDAgetweights;

/* Modern thread-safe RNG functions */
extern unsigned int RAND32 () ;
extern void RAND32_seed ( unsigned int iseed ) ;
extern double rand_uniform () ;
extern int rand_int ( int min , int max ) ;
extern double rand_normal () ;
extern double rand_normal ( double mean , double stddev ) ;
extern double rand_fast () ;
extern int rand_fast_int ( int min , int max ) ;
extern void rand_seed ( uint64_t seed ) ;
extern uint64_t rand_raw () ;

/* Legacy generator compatibility functions */
extern unsigned int RAND_LECUYER () ;
extern void RAND_LECUYER_seed ( int iseed ) ;
extern unsigned int RAND_KNUTH () ;
extern void RAND_KNUTH_seed ( int iseed ) ;
extern unsigned int RAND16_LECUYER () ;
extern unsigned int RAND16_KNUTH () ;
