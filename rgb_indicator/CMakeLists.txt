if(CONFIG_RGB_INDICATOR)
  zephyr_library()
  zephyr_library_sources(src/rgb_indicator.c)
  # ZMK exposes app/include only as PRIVATE to its own `app` target, so a
  # separate module library must add it explicitly to reach <zmk/...> headers.
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
endif()
