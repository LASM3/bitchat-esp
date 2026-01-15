idf_component_register(SRCS "main.cpp" "blehandler.cpp" "nvs.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES bt
                    REQUIRES nvs_flash)
