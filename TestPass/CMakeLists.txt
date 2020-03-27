include_directories(
    ../share/include
    )

add_llvm_library(LLVMDPTestPass MODULE
	TestPass.cpp
	../share/lib/DPUtils.cpp
    )
