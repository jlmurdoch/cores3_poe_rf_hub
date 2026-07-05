idf_component_register(SRCS "main.c" "lvgl_ui/lvgl_ui.c" "otlp_climate/otlp_climate.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES sx127x otlp_nanopb esp_event esp_timer esp_eth esp_http_client esp_netif mbedtls ulp)
set(ulp_app_name ulp_data_433)
set(ulp_s_sources "ulp/data_433.S")
set(ulp_exp_dep_srcs "main.c")
ulp_embed_binary(${ulp_app_name} "${ulp_s_sources}" "${ulp_exp_dep_srcs}")       
