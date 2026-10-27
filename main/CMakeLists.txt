idf_component_register(SRCS main.cpp blehandler.cpp bitchat_protocol.cpp nvs.cpp mesh.cpp
                    INCLUDE_DIRS "."
                    REQUIRES bt nvs_flash esp_system esp_rom freertos log libsodium)
