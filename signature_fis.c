#include "signature_fis.h"
#include "hashing_util.h"
#include "lowmc.h"
#include "mpc.h"
#include "mpc_lowmc.h"
#include "randomness.h"
#include "timing.h"

unsigned fis_compute_sig_size(unsigned m, unsigned n, unsigned r, unsigned k) {
  unsigned first_view_size = k;
  unsigned full_view_size  = n;
  unsigned int_view_size   = 3 * m;
  // views for mpc_and in sbox, intial view and last view
  unsigned views = r * int_view_size + first_view_size + full_view_size;
  // commitment and r and seed
  unsigned int commitment = 8 * (COMMITMENT_LENGTH + 2 * (COMMITMENT_RAND_LENGTH + PRNG_KEYSIZE));
  unsigned int challenge  = (FIS_NUM_ROUNDS + 3) / 4;

  return (FIS_NUM_ROUNDS * (commitment + views) + full_view_size + challenge + 7) / 8;
}

unsigned char* fis_sig_to_char_array(public_parameters_t* pp, fis_signature_t* sig, unsigned* len) {
  return proof_to_char_array(pp->lowmc, sig->proof, len, true);
}

fis_signature_t* fis_sig_from_char_array(public_parameters_t* pp, unsigned char* data) {
  unsigned len         = 0;
  fis_signature_t* sig = malloc(sizeof(fis_signature_t));
  sig->proof           = proof_from_char_array(pp->lowmc, 0, data, &len, true);
  return sig;
}

bool fis_create_key(public_parameters_t* pp, fis_private_key_t* private_key,
                    fis_public_key_t* public_key) {
  TIME_FUNCTION;

  START_TIMING;
  private_key->k = lowmc_keygen(pp->lowmc);
  END_TIMING(timing_and_size->gen.keygen);

  if (!private_key->k) {
    return false;
  }

  mzd_t* p = mzd_local_init(1, pp->lowmc->n);
  if (!p) {
    return false;
  }

  START_TIMING;
  public_key->pk = lowmc_call(pp->lowmc, private_key->k, p);
  END_TIMING(timing_and_size->gen.pubkey);

  mzd_local_free(p);

  return public_key->pk != NULL;
}

void fis_destroy_key(fis_private_key_t* private_key, fis_public_key_t* public_key) {
  lowmc_key_free(private_key->k);
  private_key->k = NULL;

  mzd_local_free(public_key->pk);
  public_key->pk = NULL;
}

static proof_t* fis_prove(mpc_lowmc_t* lowmc, lowmc_key_t* lowmc_key, mzd_t* p, const uint8_t* m,
                          unsigned m_len) {
  TIME_FUNCTION;

  const unsigned int view_count = lowmc->r + 2;

  unsigned char r[FIS_NUM_ROUNDS][3][COMMITMENT_RAND_LENGTH];
  unsigned char keys[FIS_NUM_ROUNDS][3][16];
  unsigned char secret_sharing_key[16];

  // Generating keys
  START_TIMING;
  if (rand_bytes((unsigned char*)keys, sizeof(keys)) != 1 ||
      rand_bytes((unsigned char*)r, sizeof(r)) != 1 ||
      rand_bytes(secret_sharing_key, sizeof(secret_sharing_key)) != 1) {
    return 0;
  }
  END_TIMING(timing_and_size->sign.rand);

  START_TIMING;
  view_t* views[FIS_NUM_ROUNDS];
  init_view(lowmc, views);

  mzd_shared_t s[FIS_NUM_ROUNDS];
  for (unsigned int i = 0; i < FIS_NUM_ROUNDS; ++i) {
    mzd_shared_init(&s[i], lowmc_key);
    mzd_shared_share_from_keys(&s[i], keys[i]);
  }
  END_TIMING(timing_and_size->sign.secret_sharing);

  START_TIMING;
  mzd_t** c_mpc[FIS_NUM_ROUNDS];

#ifdef WITH_OPENMP
  mzd_t** rvecs[FIS_NUM_ROUNDS][3];
#else
  mzd_t** rvec[SC_PROOF];
  for (unsigned int i = 0; i < SC_PROOF; ++i) {
    rvec[i] = malloc(sizeof(mzd_t*) * lowmc->r);
    mzd_local_init_multiple_ex(rvec[i], lowmc->r, 1, lowmc->n, false);
  }
#endif

#pragma omp parallel for
  for (unsigned int i = 0; i < FIS_NUM_ROUNDS; ++i) {
#ifdef WITH_OPENMP
    mzd_t*** rvec = rvecs[i];
    rvec[0]       = mzd_init_random_vectors_from_seed(keys[i][0], lowmc->n, lowmc->r);
    rvec[1]       = mzd_init_random_vectors_from_seed(keys[i][1], lowmc->n, lowmc->r);
    rvec[2]       = mzd_init_random_vectors_from_seed(keys[i][2], lowmc->n, lowmc->r);
#else
    for (unsigned int j = 0; j < SC_PROOF; ++j) {
      mzd_randomize_multiple_from_seed(rvec[j], lowmc->r, keys[i][j]);
    }
#endif
    c_mpc[i] = mpc_lowmc_call(lowmc, &s[i], p, views[i], rvec);
  }
  END_TIMING(timing_and_size->sign.lowmc_enc);

  START_TIMING;
  unsigned char hashes[FIS_NUM_ROUNDS][3][COMMITMENT_LENGTH];
#pragma omp parallel for
  for (unsigned int i = 0; i < FIS_NUM_ROUNDS; ++i) {
    H(keys[i][0], c_mpc[i], views[i], 0, view_count, r[i][0], hashes[i][0]);
    H(keys[i][1], c_mpc[i], views[i], 1, view_count, r[i][1], hashes[i][1]);
    H(keys[i][2], c_mpc[i], views[i], 2, view_count, r[i][2], hashes[i][2]);
  }
  END_TIMING(timing_and_size->sign.views);

  START_TIMING;
  unsigned char ch[FIS_NUM_ROUNDS];
  fis_H3(hashes, m, m_len, ch);

  proof_t* proof = create_proof(NULL, lowmc, hashes, ch, r, keys, views);

  for (unsigned int j = 0; j < FIS_NUM_ROUNDS; ++j) {
    mzd_shared_clear(&s[j]);
#ifdef WITH_OPENMP
    for (unsigned int i = 0; i < SC_PROOF; ++i) {
      mzd_local_free_multiple(rvecs[j][i]);
      free(rvecs[j][i]);
    }
#endif
    mpc_free(c_mpc[j], 3);
  }

#ifndef WITH_OPENMP
  for (unsigned int i = 0; i < SC_PROOF; ++i) {
    mzd_local_free_multiple(rvec[i]);
    free(rvec[i]);
  }
#endif

  free_view(lowmc, views);
  END_TIMING(timing_and_size->sign.challenge);

  return proof;
}

static int fis_proof_verify(mpc_lowmc_t const* lowmc, mzd_t const* p, mzd_t const* c,
                            proof_t const* prf, const uint8_t* m, unsigned m_len) {
  TIME_FUNCTION;

  const unsigned int view_count      = lowmc->r + 2;
  const unsigned int last_view_index = lowmc->r + 1;

#ifdef WITH_OPENMP
  mzd_t* ycs[FIS_NUM_ROUNDS] = {NULL};
  mzd_local_init_multiple(ycs, FIS_NUM_ROUNDS, 1, lowmc->n);
#else
  mzd_t* yc    = mzd_local_init(1, lowmc->n);
#endif

  START_TIMING;
  unsigned char ch[FIS_NUM_ROUNDS];
  unsigned char hash[FIS_NUM_ROUNDS][2][COMMITMENT_LENGTH];

#ifndef WITH_OPENMP
  mzd_t** rv[SC_VERIFY];
  for (unsigned int i = 0; i < SC_VERIFY; ++i) {
    rv[i] = malloc(sizeof(mzd_t*) * lowmc->r);
    mzd_local_init_multiple_ex(rv[i], lowmc->r, 1, lowmc->n, false);
  }
#endif

#pragma omp parallel for
  for (unsigned int i = 0; i < FIS_NUM_ROUNDS; ++i) {
    unsigned int a_i = getChAt(prf->ch, i);
    unsigned int b_i = (a_i + 1) % 3;
    unsigned int c_i = (a_i + 2) % 3;

#ifdef WITH_OPENMP
    mzd_t** rv[2];
    rv[0] = mzd_init_random_vectors_from_seed(prf->keys[i][0], lowmc->n, lowmc->r);
    rv[1] = mzd_init_random_vectors_from_seed(prf->keys[i][1], lowmc->n, lowmc->r);
#else
    for (unsigned int j = 0; j < SC_VERIFY; ++j) {
      mzd_randomize_multiple_from_seed(rv[j], lowmc->r, prf->keys[i][j]);
    }
#endif

    for (unsigned int j = 1; j < view_count - 1; ++j) {
      mzd_local_clear(prf->views[i][j].s[0]);
    }

    mpc_lowmc_verify_keys(lowmc, p, prf->views[i], rv, a_i, prf->keys[i]);

    mzd_t* ys[3];
    ys[a_i] = prf->views[i][last_view_index].s[0];
    ys[b_i] = prf->views[i][last_view_index].s[1];
    ys[c_i] = (mzd_t*)c;

#ifdef WITH_OPENMP
    mzd_t* yc = ycs[i];
#endif
    ys[c_i] = mpc_reconstruct_from_share(yc, ys);

    H(prf->keys[i][0], ys, prf->views[i], 0, view_count, prf->r[i][0], hash[i][0]);
    H(prf->keys[i][1], ys, prf->views[i], 1, view_count, prf->r[i][1], hash[i][1]);

#ifdef WITH_OPENMP
    mzd_local_free_multiple(rv[1]);
    free(rv[1]);
    mzd_local_free_multiple(rv[0]);
    free(rv[0]);
#endif
  }
  fis_H3_verify(hash, prf->hashes, prf->ch, m, m_len, ch);

#ifdef WITH_OPENMP
  mzd_local_free_multiple(ycs);
#else
  for (unsigned int i = 0; i < SC_VERIFY; ++i) {
    mzd_local_free_multiple(rv[i]);
    free(rv[i]);
  }
  mzd_local_free(yc);
#endif

  unsigned char ch_collapsed[(FIS_NUM_ROUNDS + 3) / 4] = {0};
  for (unsigned int i = 0; i < FIS_NUM_ROUNDS; ++i) {
    const unsigned int idx   = i / 4;
    const unsigned int shift = (i % 4) << 1;

    ch_collapsed[idx] |= ch[i] << shift;
  }

  const int success_status =
      memcmp(ch_collapsed, prf->ch, ((FIS_NUM_ROUNDS + 3) / 4) * sizeof(unsigned char));
  END_TIMING(timing_and_size->verify.verify);

  START_TIMING;

  return success_status;
}

fis_signature_t* fis_sign(public_parameters_t* pp, fis_private_key_t* private_key,
                          const uint8_t* msg, size_t msglen) {
  fis_signature_t* sig = malloc(sizeof(fis_signature_t));
  mzd_t* p             = mzd_local_init(1, pp->lowmc->n);
  sig->proof           = fis_prove(pp->lowmc, private_key->k, p, msg, msglen);
  mzd_local_free(p);
  return sig;
}

int fis_verify(public_parameters_t* pp, fis_public_key_t* public_key, const uint8_t* msg,
               size_t msglen, fis_signature_t* sig) {
  mzd_t* p = mzd_local_init(1, pp->lowmc->n);
  int res  = fis_proof_verify(pp->lowmc, p, public_key->pk, sig->proof, msg, msglen);
  mzd_local_free(p);
  return res;
}

void fis_free_signature(public_parameters_t* pp, fis_signature_t* signature) {
  free_proof(pp->lowmc, signature->proof);
  free(signature);
}
