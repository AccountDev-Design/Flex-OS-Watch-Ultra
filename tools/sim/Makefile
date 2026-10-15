# Vista previa de PC de la interfaz (LVGL 9.5.0 sin hardware).
#   make -C tools/sim LVGL_DIR=/ruta/a/lvgl-v9.5.0
#   tools/sim/build/sim tools/sim/out && python3 tools/sim/to_png.py tools/sim/out
ROOT := ../..
LVGL_DIR ?= $(error define LVGL_DIR con la ruta a LVGL v9.5.0)
CC ?= cc
BUILD := build
CFLAGS := -std=gnu11 -O1 -g -Wall -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE \
          -I. -Istubs -I$(LVGL_DIR) -I$(LVGL_DIR)/src \
          -I$(ROOT)/components/watch_ui/include -I$(ROOT)/components/watch_theme/include \
          -I$(ROOT)/components/watch_power/include -I$(ROOT)/components/watch_diag/include

LVGL_SRC := $(shell find $(LVGL_DIR)/src -name '*.c')
APP_SRC := sim_main.c \
           $(ROOT)/components/watch_ui/watch_home.c \
           $(ROOT)/components/watch_theme/watch_theme.c \
           $(ROOT)/components/watch_theme/flex_wallpaper.c \
           $(ROOT)/components/watch_theme/flex_glass_math.c \
           $(wildcard $(ROOT)/components/watch_theme/fonts/*.c)

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
