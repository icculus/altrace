# alTrace; a debugging tool for OpenAL.
#
# Please see the file LICENSE.txt in the source's root directory.
#
#  This file written by Ryan C. Gordon.

cmake_minimum_required(VERSION 2.8)
project(alTrace)

include_directories(.)

add_library(altrace_record SHARED
    altrace_record.c
    altrace_common.c
)
set_target_properties(altrace_record PROPERTIES C_VISIBILITY_PRESET hidden)
target_link_libraries(altrace_record dl)
install(TARGETS altrace_record LIBRARY DESTINATION lib)

add_executable(altrace_cli
    altrace_cli.c
    altrace_playback.c
    altrace_common.c
)
target_link_libraries(altrace_cli dl)
install(TARGETS altrace_cli RUNTIME DESTINATION bin)

option(ALTRACE_WX "Build wxWidgets-based GUI" TRUE)
if(ALTRACE_WX)
    set(wxWidgets_USE_LIBS base core adv html)
    set(wxWidgets_INCLUDE_DIRS_NO_SYSTEM 1)
    find_package(wxWidgets)
    if(wxWidgets_FOUND)
        if(APPLE)
            set(ALTRACE_WX_COCOA_SRCS altrace_wx_cocoa.mm)
        endif()
        add_executable(altrace_wx
            altrace_wx.cpp
            altrace_playback.c
            altrace_common.c
            messageboxex.cpp
            ${ALTRACE_WX_COCOA_SRCS}
        )
        include(${wxWidgets_USE_FILE})
        target_link_libraries(altrace_wx "dl;${wxWidgets_LIBRARIES}")
        install(TARGETS altrace_wx RUNTIME DESTINATION bin)
    else()
        MESSAGE(STATUS "wxWidgets not found. GUI support is disabled.")
    endif()
endif()

# End of CMakeLists.txt ...

