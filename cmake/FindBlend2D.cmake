find_path(Blend2D_INCLUDE_DIR NAMES blend2d.h PATHS "${BLEND2D_ROOT_DIR}/include")
find_library(Blend2D_LIBRARY NAMES blend2d PATHS "${BLEND2D_ROOT_DIR}/lib")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Blend2D DEFAULT_MSG Blend2D_LIBRARY Blend2D_INCLUDE_DIR)

mark_as_advanced(Blend2D_INCLUDE_DIR Blend2D_LIBRARY)

if(Blend2D_FOUND)
  if(NOT TARGET Blend2D::Blend2D)
    add_library(Blend2D::Blend2D UNKNOWN IMPORTED)
    set_target_properties(Blend2D::Blend2D PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${Blend2D_INCLUDE_DIR}"
      IMPORTED_LOCATION "${Blend2D_LIBRARY}")
  endif()
endif()
