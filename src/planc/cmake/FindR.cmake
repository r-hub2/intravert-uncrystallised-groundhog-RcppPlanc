# FindR.cmake from MLPACK
#[[ ----
Copyright (c) 2007-2023, mlpack contributors (see https://github.com/mlpack/mlpack/blob/master/COPYRIGHT.txt)
Modifications (c) 2023 Andrew Robbins, Welch Lab, University of Michigan
All rights reserved.

Redistribution and use of mlpack in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

------ ]]
# Make sure find package macros are included
set(TEMP_CMAKE_FIND_APPBUNDLE ${CMAKE_FIND_APPBUNDLE})
set(CMAKE_FIND_APPBUNDLE "NEVER")

# Find R.
find_program(R_EXECUTABLE R PATHS ${R_RHOME} PATH_SUFFIXES bin NO_CMAKE_SYSTEM_PATH NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_ENVIRONMENT_PATH NO_PACKAGE_ROOT_PATH)
find_program(R_EXECUTABLE R PATHS ${R_RHOME} PATH_SUFFIXES bin)
if(NOT R_EXECUTABLE)
  unset(R_EXECUTABLE CACHE)
  find_program(R_EXECUTABLE R DOC "R executable." PATHS /usr/local/ PATH_SUFFIXES bin)
endif()

if(R_EXECUTABLE)
    # Get the R version.
    execute_process(
        COMMAND ${R_EXECUTABLE} --version
        OUTPUT_VARIABLE R_VERSION_STRING
        ERROR_VARIABLE R_VERSION_STRING
        RESULT_VARIABLE RESULT
    )

    if(RESULT EQUAL 0)
        string(REGEX REPLACE ".*([0-9]+\\.[0-9]+\\.[0-9]+).*" "\\1"
            R_VERSION_STRING "${R_VERSION_STRING}")
    endif()

    set(R_HOME ${R_RHOME} CACHE PATH "R home directory obtained from R RHOME")
    mark_as_advanced(R_HOME)
endif()

# Find the Rscript program.
find_program(RSCRIPT_EXECUTABLE Rscript DOC "Rscript executable." HINTS "${R_HOME}/bin")
set(CMAKE_FIND_APPBUNDLE ${TEMP_CMAKE_FIND_APPBUNDLE})

# Search for non-standard R.h include path if header missing
execute_process(COMMAND ${RSCRIPT_EXECUTABLE} --vanilla "-e" "R.home('include')"
    RESULT_VARIABLE _haveR_h
    OUTPUT_VARIABLE _R_INCLUDE_location
    ERROR_VARIABLE _R_INCLUDE_location
    OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(COMMAND ${RSCRIPT_EXECUTABLE} --vanilla "-e" "file.path(R.home('etc'), .Platform$r_arch, 'Makeconf')"
                OUTPUT_VARIABLE R_MAKECONF
                ERROR_VARIABLE  R_MAKECONF
                OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX MATCHALL "\".*\"" R_MAKECONF "${R_MAKECONF}")
string(REGEX REPLACE "\"" "" R_MAKECONF "${R_MAKECONF}")
# find libR by way of makeconf
execute_process(COMMAND sed -e "s/^LIBR = //" -e "t" -e "d" "${R_MAKECONF}"
                OUTPUT_VARIABLE LIBR_STRING
                ERROR_VARIABLE  LIBR_STRING
                OUTPUT_STRIP_TRAILING_WHITESPACE)
if(LIBR_STRING)
string(REGEX MATCHALL "\\$\\([A-Za-z0-9_]*\\)" MAKECONF_REPLACE ${LIBR_STRING})
foreach(VAR IN LISTS MAKECONF_REPLACE)
    string(SUBSTRING ${VAR} 2 -1 VARCLEAN)
    string(REPLACE ")" "" VARCLEAN ${VARCLEAN})
    execute_process(COMMAND sed -e "s/^${VARCLEAN} = //" -e "t" -e "d" "${R_MAKECONF}"
            OUTPUT_VARIABLE TO_LIST
            ERROR_VARIABLE  TO_LIST
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(fromENV $ENV{${VARCLEAN}})
    if(TO_LIST)
        cmake_path(CONVERT ${TO_LIST} TO_CMAKE_PATH_LIST TO_LIST)
        string(REPLACE "${VAR}" "${TO_LIST}" LIBR_STRING "${LIBR_STRING}")
    elseif(fromENV)
        cmake_path(CONVERT $ENV{${VARCLEAN}} TO_CMAKE_PATH_LIST TO_LIST)
        string(REPLACE "${VAR}" "${TO_LIST}" LIBR_STRING "${LIBR_STRING}")
    else()
        string(REPLACE "${VAR}" "" LIBR_STRING "${LIBR_STRING}")
    endif()
endforeach()
else() # if the regex is still failing, you may not actually need to link R
    set(LIBR_STRING "")
endif()
    # Some cleanup in location of R.
    string(REGEX MATCHALL "\".*\"" _R_INCLUDE_location "${_R_INCLUDE_location}")
    string(REGEX REPLACE "\"" "" _R_INCLUDE_location "${_R_INCLUDE_location}")
    string(REGEX REPLACE "\"" "" LIBR_STRING "${LIBR_STRING}")
    set(R_INCLUDE_DIR ${_R_INCLUDE_location})
    set(R_LDFLAGS ${LIBR_STRING})

mark_as_advanced(RSCRIPT_EXECUTABLE R_EXECUTABLE)
set(_REQUIRED_R_VARIABLES R_EXECUTABLE RSCRIPT_EXECUTABLE R_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    R
    REQUIRED_VARS ${_REQUIRED_R_VARIABLES}
    VERSION_VAR R_VERSION_STRING
    FAIL_MESSAGE "R not found"
)
