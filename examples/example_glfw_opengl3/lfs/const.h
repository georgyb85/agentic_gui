/******************************************************************************/
/*                                                                            */
/* CONST.H - System and program constants and limitations                     */
/*                                                                            */
/******************************************************************************/

/*
   Constants
*/

#if ! defined ( MAXPOSNUM )
#define MAXPOSNUM 2147483647
#endif
#if ! defined ( MAXNEGNUM )
#define MAXNEGNUM -2147483647 /* Actually is -2147483648 */
#endif
#if ! defined ( PI )
#define PI 3.141592653589793
#endif

/*
   These are program limitations.
*/

#define MAX_NAME_LENGTH 15       /* Characters in a variable name */
#define MAX_VARS 3072            /* Total number of variables */
#define MAX_CLASSES 32           /* For LFS only */
#define MAX_STATES 8             /* HMM_MEM only */
#define MAX_MUTINF_BINS 10       /* Maximum bins for discrete mutual information */
#define MAX_THREADS 24           /* Maximum number of threads active at the same time (Windows 7 limit for Wait is 64) */
