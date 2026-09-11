set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories("../env")
include_directories("..")

include(configure.cmake)

if(CLR_CMAKE_TARGET_LIBNX)
  # Keep the GC algorithm/events and share the existing Horizon OS interface
  # with NativeAOT; Linux cgroups and NUMA discovery do not apply here.
  set(GC_PAL_SOURCES ../libnx/gcenv.libnx.cpp events.cpp)
else()
  set(GC_PAL_SOURCES gcenv.unix.cpp numasupport.cpp events.cpp cgroup.cpp)
endif()

add_library(gc_pal OBJECT ${GC_PAL_SOURCES} ${VERSION_FILE_PATH})
