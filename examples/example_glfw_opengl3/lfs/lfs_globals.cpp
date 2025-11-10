// Global variables required by LFS framework
// These are referenced by various LFS modules

// Data storage
double* database = nullptr;
int n_cases = 0;
int n_vars = 0;

// CUDA configuration
int cuda_present = 1;  // CUDA is available
int cuda_enable = 1;   // Enable CUDA by default

// Threading
int max_threads_limit = 20;

// Progress reporting (not used in GUI version)
void* hwndProgress = nullptr;

// Solver selection
bool g_use_highs_solver = false;

// Timing variables
int LFStimeTotal = 0;
int LFStimeRealToBinary = 0;
int LFStimeBetaCrit = 0;
int LFStimeWeights = 0;
int LFStimeCUDA = 0;
int LFStimeCUDAdiff = 0;
int LFStimeCUDAdist = 0;
int LFStimeCUDAmindist = 0;
int LFStimeCUDAterm = 0;
int LFStimeCUDAtranspose = 0;
int LFStimeCUDAsum = 0;
int LFStimeCUDAgetweights = 0;