#include "hashing_util.h"
#include "io.h"
#include "lowmc.h"
#include "lowmc_pars.h"
#include "mpc.h"
#include "mpc_lowmc.h"
#include "multithreading.h"
#include "mzd_additional.h"
#include "randomness.h"
#include "signature_fis.h"
#include "timing.h"

#include <inttypes.h>
#include <stdint.h>
#include <time.h>

#ifndef VERBOSE
static void print_timings(timing_and_size_t* timings, unsigned int iter, unsigned int numt) {
  for (unsigned i = 0; i < iter; i++) {
    for (unsigned j = 0; j < numt; j++) {
      printf("%" PRIu64, timings[i].data[j]);
      if (j < numt - 1)
        printf(",");
    }
    printf("\n");
  }
}
#else
static void print_detailed_timings(timing_and_size_t* timings, unsigned int iter) {
  for (unsigned int i = 0; i != iter; ++i, ++timings) {
    printf("Setup:\n");
    printf("LowMC setup                   %6" PRIu64 "\n", timings->gen.lowmc_init);
    printf("LowMC key generation          %6" PRIu64 "\n", timings->gen.keygen);
    printf("Public key computation        %6" PRIu64 "\n", timings->gen.pubkey);
    printf("\n");
    printf("Prove:\n");
    printf("MPC randomess generation      %6" PRIu64 "\n", timings->sign.rand);
    printf("MPC secret sharing            %6" PRIu64 "\n", timings->sign.secret_sharing);
    printf("MPC LowMC encryption          %6" PRIu64 "\n", timings->sign.lowmc_enc);
    printf("Hashing views                 %6" PRIu64 "\n", timings->sign.views);
    printf("Generating challenge          %6" PRIu64 "\n", timings->sign.challenge);
    printf("\n");
    printf("Verify:\n");
    printf("Recomputing challenge         %6" PRIu64 "\n", timings->verify.challenge);
    printf("Verifying output shares       %6" PRIu64 "\n", timings->verify.output_shares);
    printf("Comparing output views        %6" PRIu64 "\n", timings->verify.output_views);
    printf("Verifying views               %6" PRIu64 "\n", timings->verify.verify);
    printf("\n");
  }
}

#endif

static void parse_args(int params[5], int argc, char** argv) {
  if (argc != 6) {
    printf("Usage ./mpc_lowmc [Number of SBoxes] [Blocksize] [Rounds] [Keysize] [Numiter]\n");
    exit(-1);
  }
  params[0] = atoi(argv[1]);
  params[1] = atoi(argv[2]);
  params[2] = atoi(argv[3]);
  params[3] = atoi(argv[4]);
  params[4] = atoi(argv[5]);

  if (params[0] * 3 > params[1]) {
    printf("Number of S-boxes * 3 exceeds block size!");
    exit(-1);
  }
}

static void fis_sign_verify(int args[5]) {
  static const uint8_t m[] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                              17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

  timing_and_size_t* timings_fis = calloc(args[4], sizeof(timing_and_size_t));

  for (int i = 0; i != args[4]; ++i) {
    timing_and_size = &timings_fis[i];

    public_parameters_t pp;
    fis_private_key_t private_key;
    fis_public_key_t public_key;

    if (!create_instance(&pp, args[0], args[1], args[2], args[3])) {
      printf("Failed to create LowMC instance.\n");
      break;
    }

    if (!fis_create_key(&pp, &private_key, &public_key)) {
      printf("Failed to create keys.\n");
      destroy_instance(&pp);
      break;
    }

    fis_signature_t* sig = fis_sign(&pp, &private_key, m, sizeof(m));
    if (sig) {
      unsigned len        = 0;
      unsigned char* data = fis_sig_to_char_array(&pp, sig, &len);
      timing_and_size->size =
          fis_compute_sig_size(pp.lowmc->m, pp.lowmc->n, pp.lowmc->r, pp.lowmc->k);
      fis_free_signature(&pp, sig);
      sig = fis_sig_from_char_array(&pp, data);
      free(data);

      if (fis_verify(&pp, &public_key, m, sizeof(m), sig)) {
        printf("fis_verify: failed\n");
      }

      fis_free_signature(&pp, sig);
    } else {
      printf("fis_sign: failed\n");
    }

    destroy_instance(&pp);
    fis_destroy_key(&private_key, &public_key);
  }

#ifndef VERBOSE
  print_timings(timings_fis, args[4], 13);
#else
  printf("Fish Signature:\n\n");
  print_detailed_timings(timings_fis, args[4]);
#endif

  free(timings_fis);
}

int main(int argc, char** argv) {
  init_rand_bytes();
  init_EVP();
  openmp_thread_setup();

  int args[5];
  parse_args(args, argc, argv);

  fis_sign_verify(args);

  openmp_thread_cleanup();
  cleanup_EVP();
  deinit_rand_bytes();

  return 0;
}
