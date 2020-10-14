# DiscoPoP - Discovery of Potential Parallelism
DiscoPoP is an open-source tool that helps software developers parallelize their programs with threads. It is a joint project of Technical University of Darmstadt and Iowa State University. 

In a nutshell, DiscoPoP performs the following steps:
* detect parts of the code (computational units or CUs) with little to no internal parallelization potential,
* find data dependences among them,
* identify parallel patterns that can be used to parallelize a code region,
* and finally suggest corresponding OpenMP parallelization constructs and clauses to programmers.

DiscoPoP is built on top of LLVM. Therefore, DiscoPoP can perform the above-mentioned steps on any source code which can be transferred into the LLVM IR.

A more comprehensive overview of DiscoPoP can be found on our [project website](http://www.discopop.org/).

## Getting started
### Pre-requisites
Before doing anything, you need a basic development setup. We have tested DiscoPoP on Ubuntu, and the prerequisite packages should be installed using the following command:

	sudo apt-get install git build-essential cmake

Additionally, you need to install LLVM on your system. Currently, DiscoPoP only supports LLVM 8.0. Due to API changes, which lead to compilation failures, it does not support lower and higher versions. Please follow the [installation tutorial](https://llvm.org/docs/GettingStarted.html), if you have not installed LLVM yet.

### DiscoPoP profiler installation
First, clone the source code into the designated folder. Then, create a build directory:

	mkdir build; cd build;

Next, configure the project using CMake. The preferred LLVM installation path for DiscoPoP can be set using the -DLLVM_DIST_PATH=<PATH_TO_LLVM_BUILD_FOLDER> CMake variable.

	cmake -DLLVM_DIST_PATH=<PATH_TO_LLVM_BUILD_FOLDER> ..

Once the configuration process is successfully finished, run `make` to compile and obtain the DiscoPoP libraries. All the shared objects will be stored in the build directory under a folder named as `libi/`.


### Running DiscoPoP
DiscoPoP contains different tools for analyzing the target sequential application, namely CUGeneration, DPInstrumentation, and DPReduction. In the following, we will explain how to run each of them. However, before executing anything, please run the `dp-fmap` script in the root folder of the target application to obtain the list of files. The output will be written in a file named `FileMapping.txt`.

	<DISCOPOP_PATH>/scripts/dp-fmap

#### CU generation 
To obtain the computational unit (CU) graph of the target application, please run the following command.

	clang++ -g -O0 -fno-discard-value-names -Xclang -load -Xclang <PATH_TO_DISCOPOP_BUILD_DIR>/libi/LLVMCUGeneration.so -mllvm -fm-path -mllvm ./FileMapping.txt -c <C_File>

#### Dependence profiling
To obtain data dependences, we need to instrument the target application. Running the instrumented application will result in a text file containing all the dependences that are located in the present working directory.

	clang++ -g -O0 -fno-discard-value-names -Xclang -load -Xclang <PATH_TO_DISCOPOP_BUILD_DIR>/libi/LLVMDPInstrumentation.so -mllvm -fm-path -mllvm ./FileMapping.txt -c <C_File> -o out.o
	clang++ out.o -L<PATH_TO_DISCOPOP_BUILD_DIR>/rtlib -lDiscoPoP_RT -lpthread
	./<APP_NAME>

#### Identifying reduction operations
To obtain the list of reduction operations in the target application, we need to instrument the target application. Running the instrumented application will result in a text file containing all the reductions that are located in the present working directory.

	clang++ -g -O0 -fno-discard-value-names -Xclang -load -Xclang <PATH_TO_DISCOPOP_BUILD_DIR>/libi/LLVMDPReduction.so -mllvm -fm-path -mllvm ./FileMapping.txt -c <C_File> -o out.o
	clang++ out.o -L<PATH_TO_DISCOPOP_BUILD_DIR>/rtlib -lDiscoPoP_RT -lpthread
	./<APP_NAME>
	
*NOTE:* Please use the exact compiler flags that we used. Otherwise, you might not get the correct results, or the analysis might fail.

DiscoPoP also provides a wrapper [discopop_cpp](./discopop_cpp/README.md) to
easily invoke clang with the DiscoPoP LLVM passes.

#### Pattern identfication
Once you have all the results generated by DiscoPoP passes, you can use them to identify possible parallel design patterns. To learn more, please read the pattern detection [README](/discopop_explorer/README.md), which explains how to run pattern identification in detail.

## Walk-through example
In the `test/` folder, we have provided sample programs to help you start using DiscoPoP. You can find the walk-through example [here](/docs/DPGuide.md).

## Troubleshooting
### How to use DiscoPoP with projects which use CMake build system?
To run DiscoPoP instrumentation on projects which use CMake, you need to use the following commands instead of the normal CMake.
1. You first need to run CMake to just configure the project for compilation:
```bash
cmake -DCMAKE_CXX_COMPILER=<PATH_TO_CLANG> -DCMAKE_CXX_FLAGS="-c -g -O0 -fno-discard-value-names -Xclang -load -Xclang <PATH_TO_DISCOPOP_BUILD_FOLDER>/libi/LLVMDPInstrumentation.so -mllvm -fm-path -mllvm <PATH_TO_FILE_MAPPING>"
```
2. Then, configure the project for linking:
```bash
cmake -DCMAKE_CXX_COMPILER=<PATH_TO_CLANG> -DCMAKE_CXX_FLAGS="-g -O0 -fno-discard-value-names -Xclang -load -Xclang <PATH_TO_DISCOPOP_BUILD_FOLDER>/libi/LLVMDPInstrumentation.so -mllvm -fm-path -mllvm <PATH_TO_FILE_MAPPING>" -DCMAKE_CXX_STANDARD_LIBRARIES="-L<PATH_TO_DISCOPOP_BUILD_FOLDER>/rtlib -lDiscoPoP_RT -lpthread" .
```
3. Running `make` will build the project with DiscoPoP instrumentation applied on the code.

You may use Github issues to report potential bugs or ask your questions. In case you need individual support, please contact us using discopop[at]lists.parallel.informatik.tu-darmstadt.de.

## License
© DiscoPoP is available under the terms of the BSD-3-Clause license, as specified in the LICENSE file.
