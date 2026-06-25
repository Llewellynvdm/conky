#
# Conky, a system monitor, based on torsmo
#
# Please see COPYING for details
#
# Copyright (c) 2005-2024 Brenden Matthews, et. al. (see AUTHORS) All rights
# reserved.
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details. You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

# Set system vars
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  set(OS_LINUX true)
endif(CMAKE_SYSTEM_NAME MATCHES "Linux")

if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
  set(OS_FREEBSD true)
endif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")

if(CMAKE_SYSTEM_NAME MATCHES "DragonFly")
  set(OS_DRAGONFLY true)
endif(CMAKE_SYSTEM_NAME MATCHES "DragonFly")

if(CMAKE_SYSTEM_NAME MATCHES "OpenBSD")
  set(OS_OPENBSD true)
endif(CMAKE_SYSTEM_NAME MATCHES "OpenBSD")

if(CMAKE_SYSTEM_NAME MATCHES "SunOS")
  set(OS_SOLARIS true)
endif(CMAKE_SYSTEM_NAME MATCHES "SunOS")

if(CMAKE_SYSTEM_NAME MATCHES "NetBSD")
  set(OS_NETBSD true)
endif(CMAKE_SYSTEM_NAME MATCHES "NetBSD")

if(CMAKE_SYSTEM_NAME MATCHES "Haiku")
  set(OS_HAIKU true)
endif(CMAKE_SYSTEM_NAME MATCHES "Haiku")

if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  set(OS_DARWIN true)
endif(CMAKE_SYSTEM_NAME MATCHES "Darwin")

if(NOT OS_LINUX
  AND NOT OS_FREEBSD
  AND NOT OS_OPENBSD
  AND NOT OS_NETBSD
  AND NOT OS_DRAGONFLY
  AND NOT OS_SOLARIS
  AND NOT OS_HAIKU
  AND NOT OS_DARWIN)
  message(
    FATAL_ERROR
    "Your platform, '${CMAKE_SYSTEM_NAME}', is not currently supported.  Patches are welcome."
  )
endif(NOT
  OS_LINUX
  AND
  NOT
  OS_FREEBSD
  AND
  NOT
  OS_OPENBSD
  AND
  NOT
  OS_NETBSD
  AND
  NOT
  OS_DRAGONFLY
  AND
  NOT
  OS_SOLARIS
  AND
  NOT
  OS_HAIKU
  AND
  NOT
  OS_DARWIN)

include(FindThreads)
find_package(Threads)

set(conky_libs ${CMAKE_THREAD_LIBS_INIT})
set(conky_includes ${CMAKE_BINARY_DIR})

#
# On Darwin _POSIX_C_SOURCE must be >= __DARWIN_C_FULL for asprintf to be
# enabled! Thus disable this and _LARGEFILE64_SOURCE isnt needed, it is already
# used on macOS.
#
if(NOT OS_DARWIN AND NOT OS_OPENBSD)
  add_definitions(-D_LARGEFILE64_SOURCE -D_POSIX_C_SOURCE=200809L) # Standard definitions
  set(
    CMAKE_REQUIRED_DEFINITIONS
    "${CMAKE_REQUIRED_DEFINITIONS} -D_LARGEFILE64_SOURCE -D_POSIX_C_SOURCE=200809L"
  )
endif(NOT OS_DARWIN AND NOT OS_OPENBSD)

if(OS_FREEBSD)
  add_definitions(-D__BSD_VISIBLE=1 -D_XOPEN_SOURCE=700)
  set(
    CMAKE_REQUIRED_DEFINITIONS
    "${CMAKE_REQUIRED_DEFINITIONS} -D_LARGEFILE64_SOURCE -D_POSIX_C_SOURCE=200809L -D__BSD_VISIBLE=1 -D_XOPEN_SOURCE=700"
  )
endif(OS_FREEBSD)

if(OS_DRAGONFLY)
  set(conky_libs ${conky_libs} -L/usr/pkg/lib)
  set(conky_includes ${conky_includes} -I/usr/pkg/include)
endif(OS_DRAGONFLY)

if(OS_OPENBSD)
  # For asprintf
  add_definitions(-D_GNU_SOURCE) # Standard definitions
  set(
    CMAKE_REQUIRED_DEFINITIONS
    "${CMAKE_REQUIRED_DEFINITIONS} -D_GNU_SOURCE"
  )
endif(OS_OPENBSD)
if(OS_NETBSD)
  set(conky_libs ${conky_libs} -L/usr/pkg/lib)

  add_definitions(-D_NETBSD_SOURCE -D_XOPEN_SOURCE=700)
  set(
    CMAKE_REQUIRED_DEFINITIONS
    "${CMAKE_REQUIRED_DEFINITIONS} -D_NETBSD_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700"
  )
endif(OS_NETBSD)

if(OS_SOLARIS)
  set(conky_libs ${conky_libs} -L/usr/local/lib)
endif(OS_SOLARIS)

if(OS_HAIKU)
  # For asprintf
  add_definitions(-D_GNU_SOURCE) # Standard definitions
  set(
    CMAKE_REQUIRED_DEFINITIONS
    "${CMAKE_REQUIRED_DEFINITIONS} -D_GNU_SOURCE"
  )
endif(OS_HAIKU)

find_program(APP_AWK awk)
if(NOT APP_AWK)
  message(FATAL_ERROR "Unable to find program 'awk'")
endif(NOT APP_AWK)
mark_as_advanced(APP_AWK)

find_program(APP_WC wc)
if(NOT APP_WC)
  message(FATAL_ERROR "Unable to find program 'wc'")
endif(NOT APP_WC)
mark_as_advanced(APP_WC)

find_program(APP_UNAME uname)
if(NOT APP_UNAME)
  message(FATAL_ERROR "Unable to find program 'uname'")
endif(NOT APP_UNAME)
mark_as_advanced(APP_UNAME)

if(NOT RELEASE)
  find_program(APP_GIT git)
  if(NOT APP_GIT)
    message(FATAL_ERROR "Unable to find program 'git'")
  endif(NOT APP_GIT)
  mark_as_advanced(APP_GIT)
endif(NOT RELEASE)

# Moved for labeler
include(Version)

# A function to print the target build properties
function(print_target_properties tgt)
  if(NOT TARGET ${tgt})
    message("There is no target named '${tgt}'")
    return()
  endif()

  # this list of properties can be extended as needed
  set(CMAKE_PROPERTY_LIST
    SOURCE_DIR
    BINARY_DIR
    COMPILE_DEFINITIONS
    COMPILE_OPTIONS
    INCLUDE_DIRECTORIES
    LINK_LIBRARIES)

  message("Configuration for target ${tgt}")

  foreach(prop ${CMAKE_PROPERTY_LIST})
    get_property(propval TARGET ${tgt} PROPERTY ${prop} SET)

    if(propval)
      get_target_property(propval ${tgt} ${prop})
      message(STATUS "${prop} = ${propval}")
    endif()
  endforeach(prop)
endfunction(print_target_properties)
