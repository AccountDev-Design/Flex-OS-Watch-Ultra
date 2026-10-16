# Núcleo Flex Link v2 (port C puro de Flex OS Ultra): trama, autenticación,
# modelo/códecs de Flex Phone y máquina del enlace. Sin ESP-IDF dentro: lo
# mismo se compila en tests/host y en tools/sim.
idf_component_register(
    SRCS "flex_link.c" "flex_auth.c" "flex_phone.c" "flex_phone_transport.c" "flex_phone_link.c"
    INCLUDE_DIRS "include"
)
