include_directories(
    ../share/include
    )

add_llvm_library(LLVMCUInstantiation MODULE
    CUInstantiationPass.cpp
    ../share/lib/DPUtils.cpp
    )
