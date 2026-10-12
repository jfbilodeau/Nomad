include_guard(GLOBAL)

include(FetchContent)

function(nomad_configure_archive_dependencies)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
    set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}")
    set(ZLIB_LIBRARY zlibstatic)

    set(BUILD_SHARED_LIBS OFF)
    foreach(FEATURE IN ITEMS
        ACL BZip2 CAT CNG COVERAGE CPIO EXPAT ICONV INSTALL LIBB2
        LIBGCC LIBXML2 LZ4 LZMA LZO MBEDTLS NETTLE OPENSSL PCRE2POSIX
        PCREPOSIX TAR TEST UNZIP WERROR WIN32_XMLLITE XATTR ZSTD
    )
        set(ENABLE_${FEATURE} OFF CACHE BOOL "" FORCE)
    endforeach()
    if(NOT WIN32)
        set(ENABLE_ICONV ON CACHE BOOL "" FORCE)
    endif()
    set(ENABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(POSIX_REGEX_LIB NONE CACHE STRING "" FORCE)

    FetchContent_MakeAvailable(libarchive)
    target_compile_definitions(archive_static INTERFACE LIBARCHIVE_STATIC)
endfunction()

nomad_configure_archive_dependencies()

FetchContent_GetProperties(libarchive SOURCE_DIR libarchive_SOURCE_DIR)
FetchContent_GetProperties(zlib SOURCE_DIR zlib_SOURCE_DIR)
