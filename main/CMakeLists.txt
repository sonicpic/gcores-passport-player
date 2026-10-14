idf_component_register(
    SRCS "main.c" "passport_ui.c" "passport_logic.c" "passport_wifi.c"
         "passport_catalog.cpp" "passport_audio.c" "passport_mp3_seek.c" "passport_audio_http.c"
         "gcores/rss/RssParser.cpp" "gcores/rss/TextUtils.cpp"
         "gcores/timeline/TimelineParser.cpp" "gcores/network/HttpRange.cpp"
         "gcores/storage/CacheCodec.cpp" "../assets/fonts/passport_cjk_16.c"
         "../assets/fonts/passport_cjk_bold_22.c" "../assets/fonts/passport_cjk_12.c"
         "../assets/fonts/passport_sans_12.c" "../assets/fonts/passport_sans_14.c"
         "../assets/fonts/passport_channel_11.c"
         "../assets/fonts/passport_hint_12.c"
         "../assets/fonts/passport_pixel_14.c" "../assets/fonts/passport_pixel_20.c"
         "../assets/gcores/extracted/passport_reference_assets.c"
         "../assets/gcores/extracted/passport_player_background.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash
             esp_http_client esp_http_server esp-tls mbedtls esp_audio_codec esp_pm
)
