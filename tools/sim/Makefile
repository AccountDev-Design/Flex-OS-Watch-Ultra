# Vista previa de PC de la interfaz (LVGL 9.5.0 sin hardware). Desde la raíz del repositorio:
#   make -C tools/sim LVGL_DIR=/ruta/a/lvgl-v9.5.0
#   tools/sim/build/sim tools/sim/out && python3 tools/sim/to_png.py tools/sim/out
#
# Con SAN=1 todo (LVGL incluido) se compila con AddressSanitizer y
# UndefinedBehaviorSanitizer en build-san/: detecta usos tras liberar,
# desbordes y comportamiento indefinido en el código de la interfaz.
#   make -C tools/sim LVGL_DIR=... SAN=1 && tools/sim/build-san/sim tools/sim/out
#
# El firmware vive en "Flex OS Ultra Watch/". Make no maneja rutas con
# espacios en sus funciones, así que se accede a través del enlace
# simbólico build/fw, creado al leer este Makefile.
LVGL_DIR ?= $(error define LVGL_DIR con la ruta a LVGL v9.5.0)
CC ?= cc
ifeq ($(SAN),1)
BUILD := build-san
SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
else
BUILD := build
SANFLAGS :=
endif
FW := $(BUILD)/fw
$(shell mkdir -p $(BUILD) && ln -sfn "../../../Flex OS Ultra Watch" $(FW))

CFLAGS := -std=gnu11 -O1 -g -Wall $(SANFLAGS) -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE \
          -I. -Istubs -I$(LVGL_DIR) -I$(LVGL_DIR)/src \
          -I$(FW)/components/watch_ui -I$(FW)/components/watch_ui/include -I$(FW)/components/watch_theme/include \
          -I$(FW)/components/watch_power/include -I$(FW)/components/watch_diag/include \
          -I$(FW)/components/watch_time/include -I$(FW)/components/watch_settings/include

LVGL_SRC := $(shell find $(LVGL_DIR)/src -name '*.c')
APP_SRC := $(wildcard sim_*.c) \
           $(wildcard $(FW)/components/watch_ui/*.c) \
           $(wildcard $(FW)/components/watch_theme/*.c) \
           $(wildcard $(FW)/components/watch_theme/fonts/*.c) \
           $(FW)/components/watch_power/battery_model.c \
           $(FW)/components/watch_power/battery_alerts.c \
           $(FW)/components/watch_time/wt_time.c \
           $(FW)/components/watch_time/rtc_pcf85063.c \
           $(FW)/components/watch_settings/ws_logic.c

LVGL_OBJ := $(patsubst $(LVGL_DIR)/%.c,$(BUILD)/lvgl/%.o,$(LVGL_SRC))

.PHONY: all clean
all: $(BUILD)/sim

# LVGL 9.5 desplaza a la izquierda un valor negativo en el rodillo
# (lv_roller.c:598, posición proporcional del texto elegido): GCC lo hace bien,
# pero UBSan lo marca. Solo en ese archivo de LVGL se omite esa comprobación;
# el código del reloj se sigue comprobando entero.
$(BUILD)/lvgl/src/widgets/roller/lv_roller.o: CFLAGS += -fno-sanitize=shift-base

$(BUILD)/lvgl/%.o: $(LVGL_DIR)/%.c lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -w -c $< -o $@

APP_HDR := $(wildcard $(FW)/components/*/include/*.h) $(wildcard $(FW)/components/watch_ui/*.h) \
           $(wildcard stubs/*.h stubs/*/*.h) sim.h lv_conf.h

$(BUILD)/sim: $(APP_SRC) $(APP_HDR) $(LVGL_OBJ)
	@echo "LD $@"
	@$(CC) $(CFLAGS) -Werror $(APP_SRC) $(LVGL_OBJ) -lm -o $@

clean:
	rm -rf build build-san out
