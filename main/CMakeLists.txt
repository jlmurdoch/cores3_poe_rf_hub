idf_component_register(SRCS "main.c" "lvgl_ui/lvgl_ui.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES ulp esp_event esp_eth esp_netif sx127x)
set(ulp_app_name ulp_data_433)
set(ulp_s_sources "ulp/data_433.S")
set(ulp_exp_dep_srcs "main.c")
ulp_embed_binary(${ulp_app_name} "${ulp_s_sources}" "${ulp_exp_dep_srcs}")       
