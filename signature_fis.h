#ifndef SIGNATURE_FIS_H
#define SIGNATURE_FIS_H

#include "signature_common.h"

typedef struct {
  // pk = E_k(0)
  mzd_t* pk;
} fis_public_key_t;

typedef struct { lowmc_key_t* k; } fis_private_key_t;

typedef struct { proof_t* proof; } fis_signature_t;

unsigned fis_compute_sig_size(unsigned m, unsigned n, unsigned r, unsigned k);

unsigned char* fis_sig_to_char_array(public_parameters_t* pp, fis_signature_t* sig, unsigned* len);

fis_signature_t* fis_sig_from_char_array(public_parameters_t* pp, unsigned char* data);

bool fis_create_key(public_parameters_t* pp, fis_private_key_t* private_key,
                    fis_public_key_t* public_key);

void fis_destroy_key(fis_private_key_t* private_key, fis_public_key_t* public_key);

fis_signature_t* fis_sign(public_parameters_t* pp, fis_private_key_t* private_key,
                          const uint8_t* msg, size_t msglen);

int fis_verify(public_parameters_t* pp, fis_public_key_t* public_key, const uint8_t* msg,
               size_t msglen, fis_signature_t* sig);

void fis_free_signature(public_parameters_t* pp, fis_signature_t* signature);

#endif
