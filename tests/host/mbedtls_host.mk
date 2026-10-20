# mbedTLS de ESP-IDF compilado para el PC: las pruebas y la vista previa
# verifican las firmas del OTA con el MISMO código que el reloj.
# Uso: define MBT_OUT (carpeta de salida) y MBT_CC, e incluye este fichero.
# Da $(MBT_INC) y $(MBT_LIB).
ifeq ($(strip $(IDF_PATH)),)
$(error define IDF_PATH (ESP-IDF 5.5): el OTA usa su mbedTLS)
endif
MBEDTLS_DIR ?= $(IDF_PATH)/components/mbedtls/mbedtls
MBT_CC ?= cc
MBT_INC := -I$(MBEDTLS_DIR)/include
MBT_SRC := $(wildcard $(MBEDTLS_DIR)/library/*.c)
MBT_OBJ := $(patsubst $(MBEDTLS_DIR)/library/%.c,$(MBT_OUT)/%.o,$(MBT_SRC))
MBT_LIB := $(MBT_OUT)/libmbedcrypto_host.a

# Sin sanitizadores ni avisos: es código de terceros; lo nuestro sí los lleva.
$(MBT_OUT)/%.o: $(MBEDTLS_DIR)/library/%.c
	@mkdir -p $(MBT_OUT)
	@$(MBT_CC) -std=c99 -O2 -w $(MBT_INC) -I$(MBEDTLS_DIR)/library -c $< -o $@

$(MBT_LIB): $(MBT_OBJ)
	@echo "AR $@"
	@ar rcs $@ $^
