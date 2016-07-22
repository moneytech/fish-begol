#include "lowmc_pars.h"
#include "randomness.h"
#include "mpc.h"
#include "mzd_additional.h"
#include "m4ri/m4ri.h"

mzd_t *mzd_sample_lmatrix(rci_t n) {
  mzd_t *A = mzd_init(n,n);
  mzd_t *B = mzd_init(n,n);
  do {
    for(rci_t i=0; i<n; i++) {
      for(rci_t j=0; j<n; j++)
        mzd_write_bit(A, n-i-1, n-j-1, getrandbit());
      //mzd_xor_bits(A, n-i-1, n-i-1, 1, 1);
    }
    mzd_copy(B, A);
  } while(mzd_echelonize(A, 0) != n);
  mzd_free(A);
  return B;
};

mzd_t *mzd_sample_kmatrix(rci_t n, rci_t k) {
  mzd_t *A = mzd_init(n, k);
  mzd_t *B = mzd_init(n, k);

  rci_t r = (n<k) ? n : k;

  do {
    for(rci_t i=0; i<n; i++) {
      for(rci_t j=0; j<k; j++)
        mzd_write_bit(A, n-i-1, k-j-1, getrandbit());
      mzd_xor_bits(A, n-i-1, (k+i+1)%k, 1, 1);
    }
    mzd_copy(B, A);
  } while(mzd_echelonize(A, 0) != r);
  mzd_free(A);
  return B;
};

lowmc_t *lowmc_init(size_t m, size_t n, size_t r, size_t k) {
  lowmc_t *lowmc = (lowmc_t*)malloc(sizeof(lowmc_t));
  lowmc->m = m;
  lowmc->n = n;
  lowmc->r = r;
  lowmc->k = k;

  lowmc->LMatrix = (mzd_t**)calloc(sizeof(mzd_t*),r);
  for(int i=0; i<r; i++)
    lowmc->LMatrix[i] = mzd_sample_lmatrix(n);

  lowmc->Constants = (mzd_t**)calloc(sizeof(mzd_t*),r);
  for(int i=0; i<r; i++) {
    lowmc->Constants[i] = mzd_init_random_vector(n);
  }
  lowmc->KMatrix = (mzd_t**)calloc(sizeof(mzd_t*), r+1);
  for(int i=0; i<r+1; i++) {
    lowmc->KMatrix[i] = mzd_sample_kmatrix(n, k);
  }

  lowmc->key = (mzd_t**)malloc(sizeof(mzd_t*));
  lowmc->key[0] = mzd_init_random_vector(k);
  
  lowmc->secret_shared = 0;
  return lowmc;
}

void lowmc_free(lowmc_t *lowmc) {
  for(int i=0; i<lowmc->r; i++) {
    mzd_free(lowmc->Constants[i]);
    mzd_free(lowmc->KMatrix[i]);
    mzd_free(lowmc->LMatrix[i]);
  }
  mzd_free(lowmc->KMatrix[lowmc->r]);
  free(lowmc->Constants);
  free(lowmc->LMatrix);
  free(lowmc->KMatrix);

  mzd_free(lowmc->key[0]);
  if(lowmc->secret_shared) {
    mzd_free(lowmc->key[1]);
    mzd_free(lowmc->key[2]);
  }
  
  free(lowmc);
}


void lowmc_secret_share(lowmc_t *lowmc) {
  lowmc->key = (mzd_t**)realloc(lowmc->key, 3 * sizeof(mzd_t*));
  
  lowmc->key[1] = mzd_init_random_vector(lowmc->k);
  lowmc->key[2] = mzd_init_random_vector(lowmc->k);
  
  mzd_add(lowmc->key[0], lowmc->key[0], lowmc->key[1]);
  mzd_add(lowmc->key[0], lowmc->key[0], lowmc->key[2]);

  lowmc->secret_shared = 1;
}
