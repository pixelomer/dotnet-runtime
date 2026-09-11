set(SOURCES
    doublemapping.cpp
    dn-u16.cpp
)

if (CLR_CMAKE_TARGET_LIBNX)
    list(REMOVE_ITEM SOURCES doublemapping.cpp)
    list(APPEND SOURCES ../libnx/doublemapping.cpp)
endif()

add_library(coreclrminipal_objects
    OBJECT
    ${SOURCES}
)

add_library(coreclrminipal
    STATIC
    $<TARGET_OBJECTS:coreclrminipal_objects>
)

target_link_libraries(coreclrminipal PRIVATE minipal)
