/*
 * Copyright 2016 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include <openssl/evp.h>

#include "crypto/s2n_cipher.h"
#include "crypto/s2n_openssl.h"

#include "tls/s2n_crypto.h"

#include "utils/s2n_safety.h"
#include "utils/s2n_blob.h"

/* EVP for ChaCha20-Poly1305 added in Openssl 1.1.0. See: https://www.openssl.org/news/cl110.txt .
 * LibreSSL supports the cipher, but the interface is different from Openssl's. We should define a
 * separate s2n_cipher struct for the LibreSSL version.
 */
#if ((S2N_OPENSSL_VERSION_AT_LEAST(1,1,0)) && (!defined LIBRESSL_VERSION_NUMBER))
#define S2N_CHACHA20_POLY1305_AVAILABLE
#endif

static uint8_t s2n_aead_chacha20_poly1305_available(void)
{
#ifdef S2N_CHACHA20_POLY1305_AVAILABLE
    return 1;
#else
    return 0;
#endif
}

static int s2n_aead_chacha20_poly1305_encrypt(struct s2n_session_key *key, struct s2n_blob *iv, struct s2n_blob *aad, struct s2n_blob *in, struct s2n_blob *out)
{
#ifdef S2N_CHACHA20_POLY1305_AVAILABLE
    gte_check(in->size, S2N_TLS_CHACHA20_POLY1305_TAG_LEN);
    gte_check(out->size, in->size);
    eq_check(iv->size, S2N_TLS_CHACHA20_POLY1305_IV_LEN);

    /* Initialize the IV */
    if (EVP_EncryptInit_ex(key->evp_cipher_ctx, NULL, NULL, NULL, iv->data) != 1) {
        S2N_ERROR(S2N_ERR_KEY_INIT);
    }

    /* Adjust our buffer pointers to account for the explicit IV and TAG lengths */
    int in_len = in->size - S2N_TLS_CHACHA20_POLY1305_TAG_LEN;
    uint8_t *tag_data = out->data + out->size - S2N_TLS_CHACHA20_POLY1305_TAG_LEN;

    int out_len;
    /* Specify the AAD */
    if (EVP_EncryptUpdate(key->evp_cipher_ctx, NULL, &out_len, aad->data, aad->size) != 1) {
        S2N_ERROR(S2N_ERR_ENCRYPT);
    }

    /* Encrypt the data */
    if (EVP_EncryptUpdate(key->evp_cipher_ctx, out->data, &out_len, in->data, in_len) != 1) {
        S2N_ERROR(S2N_ERR_ENCRYPT);
    }

    /* Finalize */
    if (EVP_EncryptFinal_ex(key->evp_cipher_ctx, out->data, &out_len) != 1) {
        S2N_ERROR(S2N_ERR_ENCRYPT);
    }

    /* write the tag */
    if (EVP_CIPHER_CTX_ctrl(key->evp_cipher_ctx, EVP_CTRL_AEAD_GET_TAG, S2N_TLS_CHACHA20_POLY1305_TAG_LEN, tag_data) != 1) {
        S2N_ERROR(S2N_ERR_ENCRYPT);
    }

    return 0;
#else
    S2N_ERROR(S2N_ERR_ENCRYPT);
#endif
}

static int s2n_aead_chacha20_poly1305_decrypt(struct s2n_session_key *key, struct s2n_blob *iv, struct s2n_blob *aad, struct s2n_blob *in, struct s2n_blob *out)
{
#ifdef S2N_CHACHA20_POLY1305_AVAILABLE
    gte_check(in->size, S2N_TLS_CHACHA20_POLY1305_TAG_LEN);
    gte_check(out->size, in->size);
    eq_check(iv->size, S2N_TLS_CHACHA20_POLY1305_IV_LEN);

    /* Initialize the IV */
    if (EVP_DecryptInit_ex(key->evp_cipher_ctx, NULL, NULL, NULL, iv->data) != 1) {
        S2N_ERROR(S2N_ERR_KEY_INIT);
    }

    /* Adjust our buffer pointers to account for the explicit IV and TAG lengths */
    int in_len = in->size - S2N_TLS_CHACHA20_POLY1305_TAG_LEN;
    uint8_t *tag_data = in->data + in->size - S2N_TLS_CHACHA20_POLY1305_TAG_LEN;

    /* Set the TAG */
    if (EVP_CIPHER_CTX_ctrl(key->evp_cipher_ctx, EVP_CTRL_GCM_SET_TAG, S2N_TLS_CHACHA20_POLY1305_TAG_LEN, tag_data) == 0) {
        S2N_ERROR(S2N_ERR_DECRYPT);
    }

    int out_len;
    /* Specify the AAD */
    if (EVP_DecryptUpdate(key->evp_cipher_ctx, NULL, &out_len, aad->data, aad->size) != 1) {
        S2N_ERROR(S2N_ERR_DECRYPT);
    }

    /* Decrypt the data */
    if (EVP_DecryptUpdate(key->evp_cipher_ctx, out->data, &out_len, in->data, in_len) != 1) {
        S2N_ERROR(S2N_ERR_DECRYPT);
    }

    /* Verify the tag */
    if (EVP_DecryptFinal_ex(key->evp_cipher_ctx, out->data, &out_len) != 1) {
        S2N_ERROR(S2N_ERR_DECRYPT);
    }

    return 0;
#else
    S2N_ERROR(S2N_ERR_DECRYPT);
#endif
}

static int s2n_aead_chacha20_poly1305_set_encryption_key(struct s2n_session_key *key, struct s2n_blob *in)
{
#ifdef S2N_CHACHA20_POLY1305_AVAILABLE
    eq_check(in->size, S2N_TLS_CHACHA20_POLY1305_KEY_LEN);

    if (EVP_DecryptInit_ex(key->evp_cipher_ctx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1) {
        S2N_ERROR(S2N_ERR_KEY_INIT);
    }

    EVP_CIPHER_CTX_ctrl(key->evp_cipher_ctx, EVP_CTRL_AEAD_SET_IVLEN, S2N_TLS_CHACHA20_POLY1305_IV_LEN, NULL);

    if (EVP_DecryptInit_ex(key->evp_cipher_ctx, NULL, NULL, in->data, NULL) != 1) {
        S2N_ERROR(S2N_ERR_KEY_INIT);
    }

    return 0;
#else
    S2N_ERROR(S2N_ERR_KEY_INIT);
#endif
}

static int s2n_aead_chacha20_poly1305_set_decryption_key(struct s2n_session_key *key, struct s2n_blob *in)
{
#ifdef S2N_CHACHA20_POLY1305_AVAILABLE
    eq_check(in->size, S2N_TLS_CHACHA20_POLY1305_KEY_LEN);

    if (EVP_DecryptInit_ex(key->evp_cipher_ctx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1) {
        S2N_ERROR(S2N_ERR_KEY_INIT);
    }

    EVP_CIPHER_CTX_ctrl(key->evp_cipher_ctx, EVP_CTRL_AEAD_SET_IVLEN, S2N_TLS_CHACHA20_POLY1305_IV_LEN, NULL);

    if (EVP_DecryptInit_ex(key->evp_cipher_ctx, NULL, NULL, in->data, NULL) != 1) {
        S2N_ERROR(S2N_ERR_KEY_INIT);
    }

    return 0;
#else
    S2N_ERROR(S2N_ERR_KEY_INIT);
#endif
}

static int s2n_aead_chacha20_poly1305_init(struct s2n_session_key *key)
{
    EVP_CIPHER_CTX_init(key->evp_cipher_ctx);

    return 0;
}

static int s2n_aead_chacha20_poly1305_destroy_key(struct s2n_session_key *key)
{
    EVP_CIPHER_CTX_cleanup(key->evp_cipher_ctx);

    return 0;
}

struct s2n_cipher s2n_chacha20_poly1305 = {
    .key_material_size = S2N_TLS_CHACHA20_POLY1305_KEY_LEN,
    .type = S2N_AEAD,
    .io.aead = {
                .record_iv_size = S2N_TLS_CHACHA20_POLY1305_EXPLICIT_IV_LEN,
                .fixed_iv_size = S2N_TLS_CHACHA20_POLY1305_FIXED_IV_LEN,
                .tag_size = S2N_TLS_CHACHA20_POLY1305_TAG_LEN,
                .decrypt = s2n_aead_chacha20_poly1305_decrypt,
                .encrypt = s2n_aead_chacha20_poly1305_encrypt},
    .is_available = s2n_aead_chacha20_poly1305_available,
    .init = s2n_aead_chacha20_poly1305_init,
    .set_encryption_key = s2n_aead_chacha20_poly1305_set_encryption_key,
    .set_decryption_key = s2n_aead_chacha20_poly1305_set_decryption_key,
    .destroy_key = s2n_aead_chacha20_poly1305_destroy_key,
};
