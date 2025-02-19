/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <string.h>

#include <media/hardware/CryptoAPI.h>
#include <utils/String8.h>
#include <utils/Vector.h>
#include <utils/KeyedVector.h>

#ifndef USE_AES_TA
#include <openssl/aes.h>
#include <openssl/evp.h>
#else
extern "C" {
#include "aes_crypto.h"
}

/* Map between OP TEE TA and OpenSSL */
#define AES_BLOCK_SIZE CTR_AES_BLOCK_SIZE
#endif

namespace clearkeydrm {

using namespace android;

const uint8_t kBlockSize = AES_BLOCK_SIZE;
typedef uint8_t KeyId[kBlockSize];
typedef uint8_t Iv[kBlockSize];

#ifndef USE_AES_TA
static const size_t kBlockBitCount = kBlockSize * 8;
#endif

typedef android::CryptoPlugin::SubSample SubSample;

typedef android::KeyedVector<android::Vector<uint8_t>,
        android::Vector<uint8_t> > KeyMap;

class AesCtrDecryptorTest : public ::testing::Test {
  protected:
    typedef uint8_t Key[kBlockSize];

    status_t attemptDecrypt(const Key& key, const Iv& iv, const uint8_t* source,
                            uint8_t* destination, const SubSample* subSamples,
                            size_t numSubSamples, size_t* bytesDecryptedOut) {
        Vector<uint8_t> keyVector;
        keyVector.appendArray(key, kBlockSize);

//        AesCtrDecryptor decryptor;
//        return decryptor.decrypt(keyVector, iv, source, destination, subSamples,
//                                 numSubSamples, bytesDecryptedOut);
    uint32_t blockOffset = 0;
    uint8_t previousEncryptedCounter[kBlockSize];
    memset(previousEncryptedCounter, 0, kBlockSize);
    //android::Vector<uint8_t> *aes_key = &keyVector;
    size_t offset = 0;
    Iv opensslIv;

#ifndef USE_AES_TA
  AES_KEY aes_key;
  AES_set_encrypt_key(keyVector.array(), kBlockBitCount, &aes_key);
#endif

    memcpy(opensslIv, iv, sizeof(opensslIv));

    for (size_t i = 0; i < numSubSamples; ++i) {
        const SubSample& subSample = subSamples[i];

        if (subSample.mNumBytesOfClearData > 0) {
            memcpy(destination + offset, source + offset,
                    subSample.mNumBytesOfClearData);
            offset += subSample.mNumBytesOfClearData;
        }

        if (subSample.mNumBytesOfEncryptedData > 0) {
#ifndef USE_AES_TA
            AES_ctr128_encrypt(source + offset, destination + offset,
                    subSample.mNumBytesOfEncryptedData, &aes_key,
                    opensslIv, previousEncryptedCounter,
                    &blockOffset);
#else
            TEE_AES_ctr128_encrypt(source, destination,
                    subSample.mNumBytesOfEncryptedData, (const char*)keyVector.array(),
                    opensslIv, previousEncryptedCounter,
                    &blockOffset, offset, false);
#endif
            offset += subSample.mNumBytesOfEncryptedData;
        }
    }

    *bytesDecryptedOut = offset;
    return android::OK;

    }

    template <size_t totalSize>
    void attemptDecryptExpectingSuccess(const Key& key, const Iv& iv,
                                        const uint8_t* encrypted,
                                        const uint8_t* decrypted,
                                        const SubSample* subSamples,
                                        size_t numSubSamples) {
        uint8_t outputBuffer[totalSize] = {};
        size_t bytesDecrypted = 0;
        ASSERT_EQ(android::OK, attemptDecrypt(key, iv, encrypted, outputBuffer,
                                              subSamples, numSubSamples,
                                              &bytesDecrypted));
        EXPECT_EQ(totalSize, bytesDecrypted);
        EXPECT_EQ(0, memcmp(outputBuffer, decrypted, totalSize));
    }
};

TEST_F(AesCtrDecryptorTest, DecryptsContiguousEncryptedBlock) {
    const size_t kTotalSize = 64;
    const size_t kNumSubsamples = 1;

    // Test vectors from NIST-800-38A
    Key key = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };

    Iv iv = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };

    uint8_t encrypted[kTotalSize] = {
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
        0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
        0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff,
        0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff,
        0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5, 0xd3, 0x5e,
        0x5b, 0x4f, 0x09, 0x02, 0x0d, 0xb0, 0x3e, 0xab,
        0x1e, 0x03, 0x1d, 0xda, 0x2f, 0xbe, 0x03, 0xd1,
        0x79, 0x21, 0x70, 0xa0, 0xf3, 0x00, 0x9c, 0xee
    };

    uint8_t decrypted[kTotalSize] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
    };

    SubSample subSamples[kNumSubsamples] = {
        {0, 64}
    };
#ifdef USE_AES_TA
    TEE_crypto_init();
#endif
    attemptDecryptExpectingSuccess<kTotalSize>(key, iv, encrypted, decrypted,
                                               subSamples, kNumSubsamples);
#ifdef USE_AES_TA
    TEE_crypto_close();
#endif
}

TEST_F(AesCtrDecryptorTest, DecryptsAlignedBifurcatedEncryptedBlock) {
    const size_t kTotalSize = 64;
    const size_t kNumSubsamples = 2;

    // Test vectors from NIST-800-38A
    Key key = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };

    Iv iv = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };

    uint8_t encrypted[kTotalSize] = {
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
        0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
        0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff,
        0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff,
        0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5, 0xd3, 0x5e,
        0x5b, 0x4f, 0x09, 0x02, 0x0d, 0xb0, 0x3e, 0xab,
        0x1e, 0x03, 0x1d, 0xda, 0x2f, 0xbe, 0x03, 0xd1,
        0x79, 0x21, 0x70, 0xa0, 0xf3, 0x00, 0x9c, 0xee
    };

    uint8_t decrypted[kTotalSize] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
    };

    SubSample subSamples[kNumSubsamples] = {
        {0, 32},
        {0, 32}
    };
#ifdef USE_AES_TA
    TEE_crypto_init();
#endif
    attemptDecryptExpectingSuccess<kTotalSize>(key, iv, encrypted, decrypted,
                                             subSamples, kNumSubsamples);
#ifdef USE_AES_TA  
    TEE_crypto_close();
#endif
}

TEST_F(AesCtrDecryptorTest, DecryptsUnalignedBifurcatedEncryptedBlock) {
    const size_t kTotalSize = 64;
    const size_t kNumSubsamples = 2;

    // Test vectors from NIST-800-38A
    Key key = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };

    Iv iv = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };

    uint8_t encrypted[kTotalSize] = {
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
        0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
        0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff,
        0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff,
        0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5, 0xd3, 0x5e,
        0x5b, 0x4f, 0x09, 0x02, 0x0d, 0xb0, 0x3e, 0xab,
        0x1e, 0x03, 0x1d, 0xda, 0x2f, 0xbe, 0x03, 0xd1,
        0x79, 0x21, 0x70, 0xa0, 0xf3, 0x00, 0x9c, 0xee
    };

    uint8_t decrypted[kTotalSize] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
    };

    SubSample subSamples[kNumSubsamples] = {
        {0, 29},
        {0, 35}
    };
#ifdef USE_AES_TA
    TEE_crypto_init();
#endif
    attemptDecryptExpectingSuccess<kTotalSize>(key, iv, encrypted, decrypted,
                                               subSamples, kNumSubsamples);
#ifdef USE_AES_TA
    TEE_crypto_close();
#endif
}

TEST_F(AesCtrDecryptorTest, DecryptsOneMixedSubSample) {
    const size_t kTotalSize = 72;
    const size_t kNumSubsamples = 1;

    // Based on test vectors from NIST-800-38A
    Key key = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };

    Iv iv = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };

    uint8_t encrypted[kTotalSize] = {
        // 8 clear bytes
        0xf0, 0x13, 0xca, 0xc7, 0x00, 0x64, 0x0b, 0xbb,
        // 64 encrypted bytes
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
        0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
        0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff,
        0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff,
        0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5, 0xd3, 0x5e,
        0x5b, 0x4f, 0x09, 0x02, 0x0d, 0xb0, 0x3e, 0xab,
        0x1e, 0x03, 0x1d, 0xda, 0x2f, 0xbe, 0x03, 0xd1,
        0x79, 0x21, 0x70, 0xa0, 0xf3, 0x00, 0x9c, 0xee
    };

    uint8_t decrypted[kTotalSize] = {
        0xf0, 0x13, 0xca, 0xc7, 0x00, 0x64, 0x0b, 0xbb,
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
    };

    SubSample subSamples[kNumSubsamples] = {
        {8, 64}
    };
#ifdef USE_AES_TA
    TEE_crypto_init();
#endif
    attemptDecryptExpectingSuccess<kTotalSize>(key, iv, encrypted, decrypted,
                                               subSamples, kNumSubsamples);
#ifdef USE_AES_TA
    TEE_crypto_close();
#endif
}

TEST_F(AesCtrDecryptorTest, DecryptsAlignedMixedSubSamples) {
    const size_t kTotalSize = 80;
    const size_t kNumSubsamples = 2;

    // Based on test vectors from NIST-800-38A
    Key key = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };

    Iv iv = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };

    uint8_t encrypted[kTotalSize] = {
        // 8 clear bytes
        0xf0, 0x13, 0xca, 0xc7, 0x00, 0x64, 0x0b, 0xbb,
        // 32 encrypted bytes
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
        0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
        0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff,
        0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff,
        // 8 clear bytes
        0x94, 0xba, 0x88, 0x2e, 0x0e, 0x12, 0x11, 0x55,
        // 32 encrypted bytes
        0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5, 0xd3, 0x5e,
        0x5b, 0x4f, 0x09, 0x02, 0x0d, 0xb0, 0x3e, 0xab,
        0x1e, 0x03, 0x1d, 0xda, 0x2f, 0xbe, 0x03, 0xd1,
        0x79, 0x21, 0x70, 0xa0, 0xf3, 0x00, 0x9c, 0xee
    };

    uint8_t decrypted[kTotalSize] = {
        0xf0, 0x13, 0xca, 0xc7, 0x00, 0x64, 0x0b, 0xbb,
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x94, 0xba, 0x88, 0x2e, 0x0e, 0x12, 0x11, 0x55,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
    };

    SubSample subSamples[kNumSubsamples] = {
        {8, 32},
        {8, 32}
    };
#ifdef USE_AES_TA
    TEE_crypto_init();
#endif
    attemptDecryptExpectingSuccess<kTotalSize>(key, iv, encrypted, decrypted,
                                               subSamples, kNumSubsamples);
#ifdef USE_AES_TA
    TEE_crypto_close();
#endif
}

TEST_F(AesCtrDecryptorTest, DecryptsUnalignedMixedSubSamples) {
    const size_t kTotalSize = 80;
    const size_t kNumSubsamples = 2;

    // Based on test vectors from NIST-800-38A
    Key key = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };

    Iv iv = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };

    uint8_t encrypted[kTotalSize] = {
        // 8 clear bytes
        0xf0, 0x13, 0xca, 0xc7, 0x00, 0x64, 0x0b, 0xbb,
        // 30 encrypted bytes
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
        0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
        0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff,
        0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff,
        // 8 clear bytes
        0x94, 0xba, 0x88, 0x2e, 0x0e, 0x12, 0x11, 0x55,
        // 34 encrypted bytes
        0xfd, 0xff, 0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5,
        0xd3, 0x5e, 0x5b, 0x4f, 0x09, 0x02, 0x0d, 0xb0,
        0x3e, 0xab, 0x1e, 0x03, 0x1d, 0xda, 0x2f, 0xbe,
        0x03, 0xd1, 0x79, 0x21, 0x70, 0xa0, 0xf3, 0x00,
        0x9c, 0xee
    };

    uint8_t decrypted[kTotalSize] = {
        0xf0, 0x13, 0xca, 0xc7, 0x00, 0x64, 0x0b, 0xbb,
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x94, 0xba,
        0x88, 0x2e, 0x0e, 0x12, 0x11, 0x55, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
    };

    SubSample subSamples[kNumSubsamples] = {
        {8, 30},
        {8, 34}
    };
#ifdef USE_AES_TA
    TEE_crypto_init();
#endif
    attemptDecryptExpectingSuccess<kTotalSize>(key, iv, encrypted, decrypted,
                                               subSamples, kNumSubsamples);
#ifdef USE_AES_TA
    TEE_crypto_close();
#endif
}

TEST_F(AesCtrDecryptorTest, DecryptsComplexMixedSubSamples) {
    const size_t kTotalSize = 72;
    const size_t kNumSubsamples = 6;

    // Based on test vectors from NIST-800-38A
    Key key = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };

    Iv iv = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };

    uint8_t encrypted[kTotalSize] = {
        // 4 clear bytes
        0xf0, 0x13, 0xca, 0xc7,
        // 1 encrypted bytes
        0x87,
        // 9 encrypted bytes
        0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26, 0x1b,
        0xef,
        // 11 clear bytes
        0x81, 0x4f, 0x24, 0x87, 0x0e, 0xde, 0xba, 0xad,
        0x11, 0x9b, 0x46,
        // 20 encrypted bytes
        0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
        0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff,
        0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff,
        // 8 clear bytes
        0x94, 0xba, 0x88, 0x2e, 0x0e, 0x12, 0x11, 0x55,
        // 3 clear bytes
        0x10, 0xf5, 0x22,
        // 14 encrypted bytes
        0xfd, 0xff, 0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5,
        0xd3, 0x5e, 0x5b, 0x4f, 0x09, 0x02,
        // 2 clear bytes
        0x02, 0x01
    };

    uint8_t decrypted[kTotalSize] = {
        0xf0, 0x13, 0xca, 0xc7, 0x6b, 0xc1, 0xbe, 0xe2,
        0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x81, 0x4f,
        0x24, 0x87, 0x0e, 0xde, 0xba, 0xad, 0x11, 0x9b,
        0x46, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a, 0xae,
        0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e,
        0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x94, 0xba, 0x88,
        0x2e, 0x0e, 0x12, 0x11, 0x55, 0x10, 0xf5, 0x22,
        0x8e, 0x51, 0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c,
        0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19, 0x02, 0x01
    };

    SubSample subSamples[kNumSubsamples] = {
        {4, 1},
        {0, 9},
        {11, 20},
        {8, 0},
        {3, 14},
        {2, 0}
    };
#ifdef USE_AES_TA
    TEE_crypto_init();
#endif
    attemptDecryptExpectingSuccess<kTotalSize>(key, iv, encrypted, decrypted,
                                               subSamples, kNumSubsamples);
#ifdef USE_AES_TA
    TEE_crypto_close();
#endif
}

}  // namespace clearkeydrm
