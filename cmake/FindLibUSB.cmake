# FindLibUSB.cmake - Try to find libusb.
# Once done this will define
#
#  LIBUSB_FOUND - System has libusb
#  LIBUSB_INCLUDE_DIR - The libusb include directory
#  LIBUSB_LIBRARIES - The libraries needed to use libusb
#  LIBUSB_DEFINITIONS - Compiler switches required for using libusb

IF(PKG_CONFIG_FOUND)
  IF(DEPENDS_DIR) #Otherwise use System pkg-config path
    SET(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:${DEPENDS_DIR}/libusb/lib/pkgconfig")
  ENDIF()
  SET(MODULE "libusb-1.0")
  IF(CMAKE_SYSTEM_NAME MATCHES "Linux")
    SET(MODULE "libusb-1.0>=1.0.20")
  ENDIF()
  IF(LibUSB_FIND_REQUIRED)
    SET(LibUSB_REQUIRED "REQUIRED")
  ENDIF()
  PKG_CHECK_MODULES(LibUSB ${LibUSB_REQUIRED} ${MODULE})

  FIND_LIBRARY(LibUSB_LIBRARY
    NAMES ${LibUSB_LIBRARIES}
    HINTS ${LibUSB_LIBRARY_DIRS}
  )
  SET(LibUSB_LIBRARIES ${LibUSB_LIBRARY})

  RETURN()
ENDIF()


FIND_PATH(LIBUSB_INCLUDE_DIR NAMES libusb.h
   HINTS
   /usr
   /usr/local
   /opt
   "${LIBUSB_DEPENDS_DIR}/"
   PATH_SUFFIXES 
    libusb-1.0
    include
    libusb
    include/libusb-1.0
   )

FIND_LIBRARY(LIBUSB_LIBRARIES NAMES 
   usb-1.0
   libusb-1.0
   HINTS
   /usr
   /usr/local
   /opt
   "${LIBUSB_DEPENDS_DIR}/"
   PATH_SUFFIXES
IF (BUILD_STATIC)
    IF(MSYS OR MINGW)
        /MinGW64/static
    ELSEIF(MSVC)
        /MS64/static
    ELSE()
        static
    ENDIF()
ELSEIF(MSYS OR MINGW)
   # Do not use the ms64 dll for mingw
	/MinGW64/dll
ELSE()
    /MS64/dll
ENDIF()
)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LibUSB DEFAULT_MSG LIBUSB_LIBRARIES LIBUSB_INCLUDE_DIR)

MARK_AS_ADVANCED(LIBUSB_INCLUDE_DIR LIBUSB_LIBRARIES)
