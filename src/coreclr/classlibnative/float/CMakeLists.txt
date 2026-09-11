include_directories("../inc")

set(FLOAT_SOURCES
    floatdouble.cpp
    floatsingle.cpp
    divmodint.cpp
)

add_library_clr(comfloat_wks OBJECT ${FLOAT_SOURCES})
add_dependencies(comfloat_wks eventing_headers)

if(CLR_CMAKE_TARGET_LIBNX)
  # newlib implements sincos/sincosf but exposes their declarations only with
  # GNU feature visibility. Limit that visibility to the arithmetic callers.
  target_compile_definitions(comfloat_wks PRIVATE _GNU_SOURCE)
endif()
