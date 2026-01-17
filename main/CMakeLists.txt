idf_component_register(SRCS main.cpp blehandler.cpp nvs.cpp mesh.cpp
                    INCLUDE_DIRS "."
                    REQUIRES bt
                    REQUIRES nvs_flash
                    REQUIRES esp_system
                    REQUIRES freertos
                    REQUIRES log
                    )
