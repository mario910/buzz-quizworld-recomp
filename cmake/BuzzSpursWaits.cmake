# Keep the SDK's CPU-count policy unless a test build explicitly selects a
# bounded poll before its existing condition-variable waits.
set(BUZZ_SPURS_IDLE_SPIN_US "-1" CACHE STRING "Experimental SPURS idle polling in microseconds (-1: SDK policy, 0..1000: override)")
if(NOT BUZZ_SPURS_IDLE_SPIN_US MATCHES "^(-1|0|[1-9][0-9]*)$" OR
   BUZZ_SPURS_IDLE_SPIN_US GREATER 1000)
    message(FATAL_ERROR "BUZZ_SPURS_IDLE_SPIN_US must be -1 or an integer from 0 to 1000")
endif()
set_property(SOURCE runtime_defaults.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    "BUZZ_SPURS_IDLE_SPIN_US=${BUZZ_SPURS_IDLE_SPIN_US}")
include("${CMAKE_CURRENT_LIST_DIR}/BuzzSpursPoolLayout.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BuzzSpursPoolSpin.cmake")
