if(NOT IDF_TARGET STREQUAL "esp32c3")
    message(FATAL_ERROR "This vendored audio codec contains ESP32-C3 libraries only")
endif()
idf_component_register(INCLUDE_DIRS "include" "include/decoder"
    "include/decoder/impl" "include/simple_dec" "include/simple_dec/impl"
    REQUIRES esp_hw_support)
add_prebuilt_library(passport_acodec "${CMAKE_CURRENT_SOURCE_DIR}/lib/esp32c3/libesp_audio_codec.a"
    REQUIRES esp_hw_support)
add_prebuilt_library(passport_sdec "${CMAKE_CURRENT_SOURCE_DIR}/lib/esp32c3/libesp_audio_simple_dec.a")
target_link_libraries(${COMPONENT_LIB} INTERFACE passport_sdec passport_acodec)
