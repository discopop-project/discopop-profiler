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

#ifndef _DP_UTIL_H_
#define _DP_UTIL_H_

#include <stdint.h>
#include <string>
#include <sstream>
#include <stdexcept>
#include <iostream>   // std::cerr 
#include <fstream>
#include <vector>
#include <utility>
#include <unistd.h>
#include <assert.h>

#define LIDSIZE 14    // Number of bits for holding LID
#define MAXLNO 16384  // Maximum number of lines in a single file. Has to be 2^LIDSIZE.

typedef int32_t LID;
typedef int64_t ADDR;

using namespace std;

namespace dputil
{

    inline string decodeLID(int32_t lid)
    {
        if (lid == 0)
            return "*";

        stringstream ss;
        ss << (lid >> LIDSIZE) << ":" << lid % MAXLNO;
        return ss.str();
    }

    inline vector<string> *split(string input, char delim)
    {
        vector<string> *substrings = new vector<string>();
        istringstream inputStringStream(input);
        string sub;

        while(getline(inputStringStream, sub, delim))
        {
            substrings->push_back(sub);
        }

        return substrings;
    }

    int32_t getFileID(string fileMapping, string fullPathName)
    {
        int32_t index = 0; // if the associated file id is not found, then we return 0
        string line;
        ifstream fileMap(fileMapping.c_str());

        if (fileMap.is_open())
        {
            vector<string> *substrings = NULL;
            while (getline(fileMap, line))
            {
                substrings = split(line, '\t');
                if (substrings->size() == 2)
                {
                    string indexString = (*substrings)[0];
                    string fileName = (*substrings)[1];
                    if (fileName.compare(fullPathName) == 0)
                    {
                        index = (int32_t)atoi(indexString.c_str());
                        break;
                    }
                }
                substrings->clear();
                delete substrings;
            }
            fileMap.close();
        }

        return index;
    }

    inline bool fexists(const string &filename)
    {
        ifstream ifile(filename.c_str());

        if(ifile.fail())
            return false;
        else
            return true;
    }

    string get_exe_dir()
    {
        char buff[1024];
        ssize_t len = ::readlink("/proc/self/exe", buff, sizeof(buff) - 1);
        if (len != -1)
        {
            buff[len] = '\0';
            string fullPath = std::string(buff);
            return fullPath.substr(0, fullPath.find_last_of('/'));
        }
        else
        {
            return "";
        }
    }

} // namespace
#endif
