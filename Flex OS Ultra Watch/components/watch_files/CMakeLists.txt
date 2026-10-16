# Archivos del usuario (Notas, Documentos, Papelera) sobre el LittleFS que monta
# watch_storage en /storage. Solo POSIX: lo mismo corre en tests/host.
idf_component_register(
    SRCS "files_logic.c" "flexfs.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES vfs
)
