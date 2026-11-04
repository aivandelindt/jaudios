# ONNX Runtime 1.19.2 (MIT, Microsoft) — official prebuilt binaries from the Microsoft.ML.OnnxRuntime
# NuGet package (same builds as the GitHub release; includes the CoreML EP on macOS). Pinned by
# URL + sha256. Defines the imported target `onnxruntime::onnxruntime` and the helper
# jaudios_copy_onnxruntime(<target>) that places the runtime library next to an executable.

set(JAUDIOS_ORT_VERSION 1.19.2)

FetchContent_Declare(onnxruntime_pkg
    URL https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${JAUDIOS_ORT_VERSION}/microsoft.ml.onnxruntime.${JAUDIOS_ORT_VERSION}.nupkg
    URL_HASH SHA256=fa133c8fe6238445a0dd3e8447baa5c7d16f418caa8e576c7e253ba77b27b61b
    DOWNLOAD_NAME onnxruntime-${JAUDIOS_ORT_VERSION}.zip  # a .nupkg is a zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(onnxruntime_pkg)

set(_ort_root "${onnxruntime_pkg_SOURCE_DIR}")
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64" OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(_ort_rid osx-arm64)
    else()
        set(_ort_rid osx-x64)
    endif()
    set(_ort_lib_name libonnxruntime.dylib)
elseif(WIN32)
    set(_ort_rid win-x64)
    set(_ort_lib_name onnxruntime.dll)
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(_ort_rid linux-arm64)
    else()
        set(_ort_rid linux-x64)
    endif()
    set(_ort_lib_name libonnxruntime.so)
endif()

set(_ort_native "${_ort_root}/runtimes/${_ort_rid}/native")
if(NOT EXISTS "${_ort_native}/${_ort_lib_name}")
    message(FATAL_ERROR "ONNX Runtime package has no ${_ort_rid} library at ${_ort_native}")
endif()

# Stage the library under the name the loader will look for (Linux SONAME libonnxruntime.so.1).
set(JAUDIOS_ORT_LIB_DIR "${PROJECT_BINARY_DIR}/onnxruntime/lib")
file(MAKE_DIRECTORY "${JAUDIOS_ORT_LIB_DIR}")
if(UNIX AND NOT APPLE)
    file(COPY_FILE "${_ort_native}/${_ort_lib_name}" "${JAUDIOS_ORT_LIB_DIR}/libonnxruntime.so.1" ONLY_IF_DIFFERENT)
    set(_ort_location "${JAUDIOS_ORT_LIB_DIR}/libonnxruntime.so.1")
else()
    file(COPY_FILE "${_ort_native}/${_ort_lib_name}" "${JAUDIOS_ORT_LIB_DIR}/${_ort_lib_name}" ONLY_IF_DIFFERENT)
    set(_ort_location "${JAUDIOS_ORT_LIB_DIR}/${_ort_lib_name}")
endif()

add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION "${_ort_location}"
    INTERFACE_INCLUDE_DIRECTORIES "${_ort_root}/build/native/include")
if(UNIX AND NOT APPLE)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES IMPORTED_SONAME libonnxruntime.so.1)
endif()
if(WIN32)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES IMPORTED_IMPLIB "${_ort_native}/onnxruntime.lib")
endif()

# Copies the runtime next to an executable (or into Contents/Frameworks of an app bundle).
function(jaudios_copy_onnxruntime target)
    get_target_property(_bundle ${target} MACOSX_BUNDLE)
    if(APPLE AND _bundle)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_ort_location}" "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks/")
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "@executable_path/../Frameworks")
        set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "@executable_path/../Frameworks")
    elseif(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_ort_location}" "$<TARGET_FILE_DIR:${target}>")
    endif()
    # Linux / plain macOS executables: the imported location's directory is on the build RPATH.
endfunction()
