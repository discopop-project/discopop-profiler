/*
 * This file is part of the DiscoPoP software (http://www.discopop.tu-darmstadt.de)
 *
 * Copyright (c) 2019, Technische Universitaet Darmstadt, Germany
 *
 * This software may be modified and distributed under the terms of
 * a BSD-style license.  See the LICENSE file in the package base
 * directory for details.
 *
 */

#ifndef _DP_SIGNATURE_H_
#define _DP_SIGNATURE_H_

#include <stdint.h>
#include <assert.h>

#include <iostream>
using namespace std;
typedef  int32_t sigElement;

namespace __dp
{
    class Signature
    {
    public:
        Signature(int32_t slotSize, int32_t size, int32_t numOfHash = 1) :
            sigSlotSize(slotSize), numSlot(size), numHash(numOfHash)
        {
            assert((slotSize % 8 == 0) && (slotSize <= 32 && slotSize >= 8) && "slotSize must be byte aligned!");
            sigSlotSizeInByte = sigSlotSize / 8;
            sigSizeInByte = sigSlotSizeInByte * numSlot;

            sigarray = new uint8_t[sigSizeInByte];
            for (int i = 0; i < sigSizeInByte; ++i)
            {
                sigarray[i] = 0;
            }

            insertedElem = 0;
            conflictElem = 0;
        }

        ~Signature()
        {
            delete [] sigarray;
        }

        sigElement insert(int64_t elem, sigElement value);
        void update(int64_t elem, sigElement newValue);
        void remove(int64_t elem);
        sigElement membershipCheck(int64_t elem);
        bool intersect(Signature &other);
        double expectedFalsePositiveRate();

    private:
        int32_t sigSlotSize;        // in bits
        int32_t numSlot;
        int32_t numHash;            // 1 by default
        int32_t sigSlotSizeInByte;  // slot size in byte
        int32_t sigSizeInByte;      // total size in byte
        uint8_t *sigarray;

        int32_t insertedElem;
        int32_t conflictElem;

        uint32_t hash(int64_t value)
        {
            //cout << "hash: " << std::hex << value << ", " << std::dec << numSlot << endl;
            return (uint32_t)((value >> 8) + value) % numSlot;
        }
    };
} // namespace __dp
#endif
