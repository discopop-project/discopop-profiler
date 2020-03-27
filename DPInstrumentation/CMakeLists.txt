include_directories(
    ../share/include
    )

add_llvm_library(LLVMDPInstrumentation MODULE
	DPInstrumentation.cpp
	../share/lib/DPUtils.cpp
    )
