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

#ifndef _DP_IFUNCTIONS_H_
#define _DP_IFUNCTIONS_H_

//#define SKIP_DUP_INSTR 1

#include <algorithm>
#include <cstdlib>
#include <pthread.h>
#include <queue>
#include <set>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <string.h>
#include <regex>
#include "DPUtils.h"

namespace __dp
{

    /******* Data structures *******/

    typedef enum
    {
        RAW,
        WAR,
        WAW,
        INIT
    }
    depType;

    struct AccessInfo
    {
        AccessInfo(bool isRead, LID lid, char *var, ADDR addr, bool skip = false)
            : isRead(isRead), lid(lid), var(var), addr(addr), skip(skip) {}
        AccessInfo() : lid(0) {}

        bool isRead;
        bool skip;
        LID lid;
        char *var;
        ADDR addr;
    };

    // For runtime dependency merging
    struct Dep
    {
        Dep(depType T, LID dep, char *var) : type(T), depOn(dep), var(var) {}

        depType type;
        LID depOn;
        char *var;
    };

    struct compDep
    {
        bool operator()(const Dep &a, const Dep &b)
        {
            if (a.type < b.type)
            {
                return true;
            }
            else if (a.type == b.type && a.depOn < b.depOn)
            {
                return true;
            }
            // comparison between string is very time-consuming. So just compare variable names
            // according to address (we only need to distinguish them)
            else if (a.type == b.type && a.depOn == b.depOn && ((size_t)a.var < (size_t)b.var))
            {
                return true;
            }

            return false;
        }
    };

    typedef std::set<Dep, compDep> depSet;
    typedef std::unordered_map<LID, depSet *> depMap;
    typedef std::unordered_map<std::string, std::set<std::string>> stringDepMap;

    // For loop tracking
    struct LoopTableEntry
    {
        LoopTableEntry(int32_t l, int32_t id, int32_t c, LID b) : funcLevel(l), loopID(id), count(c), begin(b) {}

        int32_t funcLevel;
        int32_t loopID;
        int32_t count;
        LID begin;
    };

    typedef std::stack<LoopTableEntry> LoopTable;

    // For loop merging
    // Assumption: no more than one loops can begin at the same line
    struct LoopRecord
    {
        LoopRecord(LID e, int32_t t, int32_t n) : end(e), total(t), nEntered(n) {}

        LID end;
        int32_t total;
        int32_t nEntered;
    };

    typedef std::unordered_map<LID, LoopRecord *> LoopRecords;

    // For function merging
    // 1) when two BGN func are identical

    typedef std::unordered_map<LID, std::set<LID>*> BGNFuncList;

    // 2) when two END func are identical

    typedef std::set<LID> ENDFuncList;

    typedef std::set<int32_t> ReportedBBSet;
    typedef std::set<std::string> ReportedBBPairSet;

    /******* Helper functions *******/

    void addDep(depType type, LID curr, LID depOn, char *var);
    void outputDeps();
    void outputLoops();
    void outputFuncs();
    void readRuntimeInfo();
    void initParallelization();
    void *analyzeDeps(void *arg);
    void addAccessInfo(bool isRead, LID lid, char *var, ADDR addr);
    void finalizeParallelization();

    /******* Instrumentation functions *******/
    extern "C" {
#ifdef SKIP_DUP_INSTR
        void __dp_read(LID lid, ADDR addr, char *var, ADDR lastaddr, int64_t count);
        void __dp_write(LID lid, ADDR addr, char *var, ADDR lastaddr, int64_t count);
        void __dp_decl(LID lid, ADDR addr, char *var, ADDR lastaddr, int64_t count);
        void __dp_alloca(LID lid, ADDR addr, char *var, ADDR lastaddr, int64_t count);
#else
        void __dp_read(LID lid, ADDR addr, char *var);
        void __dp_write(LID lid, ADDR addr, char *var);
        void __dp_decl(LID lid, ADDR addr, char *var);
        void __dp_alloca(LID lid, ADDR addr, char *var);
#endif
        void __dp_report_bb(int32_t bbIndex);
        void __dp_report_bb_pair(int32_t counter, int32_t bbIndex);
        void __dp_add_bb_deps(char* depStringPtr);
        void __dp_finalize(LID lid);
        void __dp_call(LID lid);
        void __dp_func_entry(LID lid, int32_t isStart);
        void __dp_func_exit(LID lid, int32_t isExit);
        void __dp_loop_entry(LID lid, int32_t loopID);
        void __dp_loop_exit(LID lid, int32_t loopID);
    }
} // namespace __dp
#endif
