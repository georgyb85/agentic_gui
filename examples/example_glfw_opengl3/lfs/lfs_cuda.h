#pragma once

// CUDA API function declarations for LFS
// These functions are implemented in lfs_cuda.cu
// Note: These match the declarations in funcdefs.h

// Initialize CUDA resources
int lfs_cuda_init(int n_cases, int n_vars, double* cases, char* error_msg);

// Cleanup CUDA resources
void lfs_cuda_cleanup();

// Set class IDs for cases
int lfs_cuda_classes(int* class_id, char* error_msg);

// Set prior flags
int lfs_cuda_flags(int* f_prior, char* error_msg);

// CUDA weight computation functions - called in sequence to compute weights
int lfs_cuda_diff(int which_i);
int lfs_cuda_dist();
int lfs_cuda_mindist(int which_i);
int lfs_cuda_term(int iclass);
int lfs_cuda_transpose();
int lfs_cuda_sum();
int lfs_cuda_get_weights(double* weights, char* error_msg);