set(COMPONENT_SRCS "main.c" "GsmModule.c" "wifi.c" "ErrorMessages.c" "MessageFormatter.c" "Utils.c" "Messages.c" "Memory.c")
set(COMPONENT_ADD_INCLUDEDIRS "")
set(COMPONENT_REQUIRES soc nvs_flash ulp esp_http_client)

register_component()

set(ulp_app_name ulp_${COMPONENT_NAME})
set(ulp_s_sources ulp/ulp_code.S)
set(ulp_exp_dep_srcs "main.c")

ulp_embed_binary(${ulp_app_name} "${ulp_s_sources}" "${ulp_exp_dep_srcs}")
