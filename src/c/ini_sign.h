#pragma once

#include <ovbase.h>

struct gcmz_ini_reader;

/**
 * @brief Cryptographic signature size constants
 */
enum {
  gcmz_sign_public_key_size = 32, ///< Public key size in bytes
  gcmz_sign_secret_key_size = 32, ///< Secret key size in bytes
  gcmz_sign_signature_size = 64,  ///< Signature size in bytes
};

/**
 * @brief Generate a cryptographic signature for INI file contents
 *
 * Creates a digital signature for the provided INI reader data
 * using the specified secret key. The signature can later be verified
 * with the corresponding public key to ensure data integrity.
 *
 * @param reader INI reader containing the data to be signed
 * @param secret_key Secret key for signature generation
 * @param signature [out] Output buffer for the generated signature
 * @param err [out] Error information on failure
 * @return true on successful signature generation, false on failure
 */
bool gcmz_sign(struct gcmz_ini_reader const *reader,
               uint8_t const secret_key[gcmz_sign_secret_key_size],
               uint8_t signature[gcmz_sign_signature_size],
               struct ov_error *err);

/**
 * @brief Verify a cryptographic signature against INI file contents
 *
 * Validates a digital signature against the provided INI reader
 * data using the specified public key. This ensures the data has not been
 * tampered with and originates from the holder of the corresponding secret key.
 *
 * @param reader INI reader containing the data to be verified
 * @param public_key Public key for signature verification
 * @param err [out] Error information on failure
 * @return true if signature is valid, false if invalid or on error
 */
bool gcmz_sign_verify(struct gcmz_ini_reader const *reader,
                      uint8_t const public_key[gcmz_sign_public_key_size],
                      struct ov_error *err);
