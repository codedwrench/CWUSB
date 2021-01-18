cmake_minimum_required(VERSION 3.15)
project(PSPXLinkBridge)

set(CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  "${CMAKE_MODULE_PATH}"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_DOC "Build doxygen" OFF)
option(ENABLE_TESTS "Build unittests" OFF)
option(BUILD_STATIC "Statically link all libraries that can be statically linked" OFF)

include_directories(Sources)
include_directories(Tests)

# Check if Doxygen is installed
find_package(Doxygen)
if (BUILD_DOC AND DOXYGEN_FOUND)
    # Set input and output files
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # Request to configure the file
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    message("Doxygen build started")

    add_custom_target(doc_doxygen ALL
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM)
else (DOXYGEN_FOUND)
    message("Doxygen need to be installed to generate the doxygen documentation")
endif (BUILD_DOC AND DOXYGEN_FOUND)

# Windows specific options
if (MSVC OR MSYS OR MINGW)
    message("You're running Windows! Make sure your paths are set correctly.")

    if (MSYS OR MINGW)
        # Add WinSock to the platform specific libraries, MINGW needs it.
        string(APPEND PLATFORM_SPECIFIC_LIBRARIES "ws2_32")
    
        # Needed because otherwise the libraries cannot be found.
        set(Boost_ARCHITECTURE "-x64")
    endif ()
    
    set(BOOST_ROOT "d:/boost_1_75_0")
    set(Boost_LIBRARY_DIR "d:/boost_1_75_0/stage/lib")

    if (NOT BUILD_STATIC)
        # Magic that boost on windows seems to need.
        add_definitions( -DWIN32_LEAN_AND_MEAN )
        add_definitions( -DBOOST_ALL_NO_LIB )
        add_definitions( -DBOOST_ALL_DYN_LINK )
    endif ()

    # The logger module uses ERROR in an enum class, GDI also uses this, but screw GDI
    # We have no GUI anyway!
    add_definitions( -DNOGDI )
    set(LIBUSB_DEPENDS_DIR "C:/libusb")
endif ()

if (BUILD_STATIC)
    message("Static linking enabled")
    set(BUILD_STATIC ON)
    set(Boost_USE_STATIC_LIBS ON)
endif()

# Concepts seems to break on a bunch of compilers, see: https://github.com/boostorg/asio/issues/312
add_definitions(-DBOOST_ASIO_DISABLE_CONCEPTS)

find_package(LibUSB REQUIRED)
find_package(Threads REQUIRED)
find_package(Boost 1.71 REQUIRED COMPONENTS chrono date_time atomic thread system program_options)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/PSPLinkBSD.txt
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/cnf/config.txt
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
	
# TODO: Make this search for source files automatically, this is very ugly!
add_executable(PSPXLinkBridge main.cpp
        Sources/Logger.cpp
        Sources/SettingsModel.cpp
        Sources/XLinkKaiConnection.cpp
        Sources/USBReader.cpp
        Includes/USBConstants.h
        Includes/Logger.h
        Includes/NetworkingHeaders.h
        Includes/XLinkKaiConnection.h
        Includes/NetConversionFunctions.h
        Includes/SettingsModel.h
        Includes/USBReader.h
        ${EXTRA_INCLUDES})

if (BUILD_STATIC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
endif ()

target_include_directories(PSPXLinkBridge PRIVATE ${LIBUSB_INCLUDE_DIR} ${Boost_INCLUDE_DIR})
target_link_libraries(PSPXLinkBridge PRIVATE ${Boost_LIBRARIES} ${LIBUSB_LIBRARIES} ${PLATFORM_SPECIFIC_LIBRARIES})


