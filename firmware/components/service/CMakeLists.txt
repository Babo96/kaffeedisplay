idf_component_register(
    SRCS "src/service.cpp" "src/auth.cpp" "src/backup.cpp"
    INCLUDE_DIRS "include"
    REQUIRES domain storage
    PRIV_REQUIRES mbedtls
)
