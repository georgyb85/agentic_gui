#pragma once

#include <vector>
#include <memory>
#include <chrono>
#include <cstdlib>

constexpr int MAX_CLASSES = 32;
constexpr int MAX_THREADS = 24;

// Error constants
constexpr int ERROR_ESCAPE = 1;
constexpr int ERROR_SIMPLEX = 2;
constexpr int ERROR_THREAD = 3;
constexpr int ERROR_CUDA_MEMORY = 100;
constexpr int ERROR_INSUFFICIENT_MEMORY = 101;
constexpr int ERROR_CUDA_PARAMETER = 102;

// Global timing variables
extern int LFStimeTotal;
extern int LFStimeRealToBinary;
extern int LFStimeBetaCrit;
extern int LFStimeWeights;
extern int LFStimeCUDA;
extern int LFStimeCUDAdiff;
extern int LFStimeCUDAdist;
extern int LFStimeCUDAmindist;
extern int LFStimeCUDAterm;
extern int LFStimeCUDAtranspose;
extern int LFStimeCUDAsum;
extern int LFStimeCUDAgetweights;

// Quick sort functions
void qsortd(int first, int last, double* data);
void qsortds(int first, int last, double* data, double* slave);
void qsortdsi(int first, int last, double* data, int* slave);
void qsorti(int first, int last, int* data);
void qsortisd(int first, int last, int* data, double* slave);

// Random number generation functions
void RAND_LECUYER_seed(int iseed);
unsigned int RAND_LECUYER();
void RAND_KNUTH_seed(int iseed);
unsigned int RAND_KNUTH();
unsigned int RAND16_LECUYER();
unsigned int RAND16_KNUTH();
void RAND32_seed(unsigned int iseed);
unsigned int RAND32();
double unifrand();
double unifrand_fast();
double fast_unif(int* iparam);

class SingularValueDecomp {
public:
    SingularValueDecomp(int nrows, int ncols, int save_a = 0);
    ~SingularValueDecomp();
    void svdcmp();
    void backsub(double limit, double* soln);

    int ok;

    std::vector<double> a;
    std::vector<double> u;
    std::vector<double> w;
    std::vector<double> v;
    std::vector<double> b;

private:
    void bidiag(double* matrix);
    double bid1(int col, double* matrix, double scale);
    double bid2(int col, double* matrix, double scale);
    void right(double* matrix);
    void left(double* matrix);
    void cancel(int low, int high, double* matrix);
    void qr(int low, int high, double* matrix);
    void qr_mrot(int col, double sine, double cosine, double* matrix);
    void qr_vrot(int col, double sine, double cosine);

    int rows;
    int cols;
    std::vector<double> work;
    double norm;
};

class Simplex {
public:
    Simplex(int nv, int nc, int nle, int prn);
    ~Simplex();
    void set_objective(double* coefs);
    int set_constraints(double* values);
    int solve(int max_iters, double eps);
    void get_optimal_values(double* optval, double* values);
    int check_objective(double* coefs, double eps, double* error);
    int check_constraint(int which, double* constraints, double eps, double* error);
    int check_counters();
    void print_counters();

    int ok;

private:
    int find_pivot_column();
    int find_pivot_row(int pivot_col);
    void do_pivot(int row, int col, int phase1);
    int solve_simple(int max_iters, double eps);
    int solve_extended(int max_iters, double eps);
    void print_tableau(const char* msg);
    void print_optimal_vector(char* msg);

    int n_vars;
    int n_constraints;
    int n_less_eq;
    int n_gtr_eq;
    int nrows;
    int ncols;
    std::vector<double> tableau;
    std::vector<int> basics;
    int print;

    int p1_zero_exit;
    int p1_normal_exit;
    int p1_relaxed_exit;
    int p1_art_exit;
    int p1_art_in_basis;
    int p1_unbounded;
    int p1_no_feasible;
    int p1_too_many_its;
    int p1_cleanup_bad;
    int p2_normal_exit;
    int p2_relaxed_exit;
    int p2_unbounded;
    int p2_too_many_its;
};

class LFS {
public:
    LFS(int nc, int nv, int mk, int max_threads, double* x, int progress);
    ~LFS();

    int run(int iters, int n_rand, int n_beta, int irep, int mcpt_reps);
    int process_case(int i, int ithread);
    int* get_f();

    int ok;

private:
    int test_beta(int which_i, double beta, double eps_max, double* crit, int ithread);

    int n_cases;
    int n_vars;
    int n_classes;
    int max_kept;
    int n_rand;
    int n_beta;
    int max_threads;
    int progress;
    int n_per_class[MAX_CLASSES];

    std::vector<int> class_id;
    std::vector<double> cases;
    std::vector<double> weights;
    std::vector<double> delta;
    std::vector<double> f_real;
    std::vector<int> f_binary;
    std::vector<int> f_prior;
    std::vector<double> d_ijk;
    std::vector<int> nc_iwork;
    std::vector<int> best_binary;
    std::vector<int> best_fbin;
    std::vector<double> aa;
    std::vector<double> bb;
    std::vector<double> constraints;
    std::vector<std::unique_ptr<Simplex>> simplex1;
    std::vector<std::unique_ptr<Simplex>> simplex2;
};