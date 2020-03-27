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

#ifndef _DP_PERFECTSHADOW_H_
#define _DP_PERFECTSHADOW_H_

#include "signature.h"
#include "abstract_shadow.h"

#include <stdint.h>
#include <map>
#include <iostream>


namespace __dp
{

    class PerfectShadow : public Shadow
    {
    public:
        PerfectShadow(int slotSize, int size, int numHash)
            : PerfectShadow() {}

        PerfectShadow()
        {
            sigRead = new std::map<int64_t, sigElement>();
            sigWrite = new std::map<int64_t, sigElement>();
        }

        ~PerfectShadow()
        {
            delete sigRead;
            delete sigWrite;
        }

        inline sigElement testInRead(int64_t memAddr)
        {
            return (*sigRead)[memAddr];
        }

        inline sigElement testInWrite(int64_t memAddr)
        {
            return (*sigWrite)[memAddr];
        }

        inline sigElement insertToRead(int64_t memAddr, sigElement value)
        {
            sigElement oldValue = testInRead(memAddr);
            (*sigRead)[memAddr] = value;
            return oldValue;
        }

        inline sigElement insertToWrite(int64_t memAddr, sigElement value)
        {
            sigElement oldValue = testInWrite(memAddr);
            (*sigWrite)[memAddr] = value;
            return oldValue;
        }

        inline void updateInRead(int64_t memAddr, sigElement newValue)
        {
            (*sigRead)[memAddr] = newValue;
        }

        inline void updateInWrite(int64_t memAddr, sigElement newValue)
        {
            (*sigWrite)[memAddr] = newValue;
        }

        inline void removeFromRead(int64_t memAddr)
        {
            (*sigRead)[memAddr] = 0;
        }

        inline void removeFromWrite(int64_t memAddr)
        {
            (*sigWrite)[memAddr] = 0;
        }

    private:
        std::map <int64_t, sigElement> *sigRead;
        std::map <int64_t, sigElement> *sigWrite;
    };

} // namespace __dp
#endif
