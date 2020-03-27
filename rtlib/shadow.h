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

#ifndef _DP_SHADOW_H_
#define _DP_SHADOW_H_

#include "signature.h"
#include "abstract_shadow.h"

#include <stdint.h>
#include <iostream>

using namespace std;

namespace __dp
{

    class ShadowMemory : public Shadow
    {
    public:
        ShadowMemory(int slotSize, int size, int numHash)
        {
            sigRead = new Signature(slotSize, size, numHash);
            sigWrite = new Signature(slotSize, size, numHash);
        }

        ~ShadowMemory()
        {
            delete sigRead;
            delete sigWrite;
        }

        inline sigElement testInRead(int64_t memAddr)
        {
            return sigRead->membershipCheck(memAddr);
        }

        inline sigElement testInWrite(int64_t memAddr)
        {
            return sigWrite->membershipCheck(memAddr);
        }

        inline sigElement insertToRead(int64_t memAddr, sigElement value)
        {
            return sigRead->insert(memAddr, value);
        }

        inline sigElement insertToWrite(int64_t memAddr, sigElement value)
        {
            return sigWrite->insert(memAddr, value);
        }

        inline void updateInRead(int64_t memAddr, sigElement newValue)
        {
            sigRead->update(memAddr, newValue);
        }

        inline void updateInWrite(int64_t memAddr, sigElement newValue)
        {
            sigWrite->update(memAddr, newValue);
        }

        inline void removeFromRead(int64_t memAddr)
        {
            sigRead->remove(memAddr);
        }

        inline void removeFromWrite(int64_t memAddr)
        {
            sigWrite->remove(memAddr);
        }

    private:
        Signature *sigRead;
        Signature *sigWrite;
    };

} // namespace __dp
#endif
