include_directories(
    ../share/include
    )

add_llvm_library(LLVMDPInstrumentationOmission MODULE
  DPInstrumentationOmission.cpp
  InstructionCFG.cpp
  InstructionDG.cpp
  ../share/lib/DPUtils.cpp
    )
