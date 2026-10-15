# Vista previa de PC de la interfaz (LVGL 9.5.0 sin hardware). Desde la raíz del repositorio:
#   make -C tools/sim LVGL_DIR=/ruta/a/lvgl-v9.5.0
#   tools/sim/build/sim tools/sim/out && python3 tools/sim/to_png.py tools/sim/out
#
# El firmware vive en "Flex OS Ultra Watch/". Make no maneja rutas con
# espacios en sus funciones, así que se accede a través del enlace
# simbólico build/fw, creado al leer este Makefile.
LVGL_DIR ?= $(error define LVGL_DIR con la ruta a LVGL v9.5.0)
CC ?= cc
BUILD := build
FW := $(BUILD)/fw
$(shell mkdir -p $(BUILD) && ln -sfn "../../../Flex OS Ultra Watch" $(FW))

CFLAGS := -std=gnu11 -O1 -g -Wall -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE \
          -I. -Istubs -I$(LVGL_DIR) -I$(LVGL_DIR)/src \
          -I$(FW)/components/watch_ui/include -I$(FW)/components/watch_theme/include \
          -I$(FW)/components/watch_power/include -I$(FW)/components/watch_diag/include

LVGL_SRC := $(shell find $(LVGL_DIR)/src -name '*.c')
APP_SRC := sim_main.c \
           $(wildcard $(FW)/components/watch_ui/*.c) \
           $(wildcard $(FW)/components/watch_theme/*.c) \
           $(wildcard $(FW)/components/watch_theme/fonts/*.c) \
           $(FW)/components/watch_power/battery_model.c

LVGL_OBJ := $(patsubst $(LVGL_DIR)/%.c,$(BUILD)/lvgl/%.o,$(LVGL_SRC))

.PHONY: all clean
all: $(BUILD)/sim

$(BUILD)/lvgl/%.o: $(LVGL_DIR)/%.c lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -w -c $< -o $@

$(BUILD)/sim: $(APP_SRC) $(LVGL_OBJ)
	@echo "LD $@"
	@$(CC) $(CFLAGS) -Werror $(APP_SRC) $(LVGL_OBJ) -lm -o $@

clean:
	rm -rf $(BUILD) out
