#include_directories(
#    ../share/include
#    )

# Runtime library sources and build flags.
set(DiscoPoP_SOURCES
  iFunctions.cpp
  signature.cpp
  loop_counter.cpp
  #../share/lib/DPUtils.cpp
  )
  
set(CMAKE_CXX_FLAGS
  "-O2 -std=c++11 -pthread -fno-rtti -fPIE -ffreestanding")

# add_custom_target(DiscoPoP)

add_library(DiscoPoP_RT STATIC
    ${DiscoPoP_SOURCES})


