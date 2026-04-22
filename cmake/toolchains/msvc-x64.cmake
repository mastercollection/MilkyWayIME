# Configure Ninja builds to use the latest installed MSVC x64 toolchain.

macro(_mwime_import_env _source_name _target_name)
    string(REGEX MATCH "(^|\n)${_source_name}=([^\n]*)" _mwime_match "\n${_mwime_vs_env}")
    if(NOT _mwime_match STREQUAL "")
        set(ENV{${_target_name}} "${CMAKE_MATCH_2}")
    endif()
endmacro()

function(_mwime_seed_msvc_env)
    if(DEFINED ENV{VCToolsInstallDir} AND DEFINED ENV{VSINSTALLDIR})
        return()
    endif()

    set(_mwime_vswhere "")
    foreach(_mwime_vswhere_candidate IN ITEMS
        "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
        "C:/Program Files/Microsoft Visual Studio/Installer/vswhere.exe"
    )
        if(EXISTS "${_mwime_vswhere_candidate}")
            set(_mwime_vswhere "${_mwime_vswhere_candidate}")
            break()
        endif()
    endforeach()

    if(_mwime_vswhere STREQUAL "")
        message(FATAL_ERROR "vswhere.exe was not found. Install Visual Studio with C++ tools.")
    endif()

    execute_process(
        COMMAND "${_mwime_vswhere}" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        OUTPUT_VARIABLE _mwime_vs_install
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _mwime_vswhere_result
    )

    if(NOT _mwime_vswhere_result EQUAL 0 OR _mwime_vs_install STREQUAL "")
        message(FATAL_ERROR "Could not locate a Visual Studio installation with MSVC tools.")
    endif()

    set(_mwime_vsdevcmd "${_mwime_vs_install}/Common7/Tools/VsDevCmd.bat")
    if(NOT EXISTS "${_mwime_vsdevcmd}")
        message(FATAL_ERROR "VsDevCmd.bat was not found at ${_mwime_vsdevcmd}.")
    endif()

    file(TO_NATIVE_PATH "${_mwime_vsdevcmd}" _mwime_vsdevcmd_native)
    set(_mwime_vsenv_script "${CMAKE_CURRENT_BINARY_DIR}/mwime-msvc-env.cmd")
    file(TO_NATIVE_PATH "${_mwime_vsenv_script}" _mwime_vsenv_script_native)

    file(WRITE "${_mwime_vsenv_script}" "@echo off\r\n")
    file(APPEND "${_mwime_vsenv_script}" "call \"${_mwime_vsdevcmd_native}\" -arch=x64 -host_arch=x64 >nul\r\n")
    file(APPEND "${_mwime_vsenv_script}" "if errorlevel 1 exit /b %errorlevel%\r\n")
    file(APPEND "${_mwime_vsenv_script}" "set\r\n")

    execute_process(
        COMMAND cmd /d /c "${_mwime_vsenv_script_native}"
        OUTPUT_VARIABLE _mwime_vs_env
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _mwime_vsdevcmd_result
    )

    if(NOT _mwime_vsdevcmd_result EQUAL 0)
        message(FATAL_ERROR "Failed to initialize the MSVC developer environment.")
    endif()

    string(REPLACE "\r\n" "\n" _mwime_vs_env "${_mwime_vs_env}")

    foreach(_mwime_env_name IN ITEMS
        INCLUDE
        LIB
        LIBPATH
        UCRTVersion
        UniversalCRTSdkDir
        VCToolsInstallDir
        VSINSTALLDIR
        WindowsSdkDir
        WindowsSDKVersion
    )
        _mwime_import_env("${_mwime_env_name}" "${_mwime_env_name}")
    endforeach()

    _mwime_import_env("Path" "PATH")
    if(NOT DEFINED ENV{PATH})
        _mwime_import_env("PATH" "PATH")
    endif()
endfunction()

_mwime_seed_msvc_env()

find_program(_mwime_ninja NAMES ninja ninja.exe HINTS "C:/Strawberry/c/bin" REQUIRED)

set(_mwime_vctools_install_dir "$ENV{VCToolsInstallDir}")
if(NOT _mwime_vctools_install_dir STREQUAL "")
    file(TO_CMAKE_PATH "${_mwime_vctools_install_dir}" _mwime_vctools_install_dir)
    set(_mwime_cl "${_mwime_vctools_install_dir}/bin/Hostx64/x64/cl.exe")
endif()
if(NOT EXISTS "${_mwime_cl}")
    find_program(_mwime_cl cl REQUIRED)
endif()

set(CMAKE_MAKE_PROGRAM "${_mwime_ninja}" CACHE FILEPATH "Ninja build tool" FORCE)
set(CMAKE_C_COMPILER "${_mwime_cl}" CACHE FILEPATH "MSVC C compiler" FORCE)
set(CMAKE_CXX_COMPILER "${_mwime_cl}" CACHE FILEPATH "MSVC CXX compiler" FORCE)
