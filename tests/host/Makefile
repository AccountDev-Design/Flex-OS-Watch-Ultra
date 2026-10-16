# Pruebas de PC de la lógica pura (sin ESP-IDF): make -C tests/host
#
# El firmware vive en "Flex OS Ultra Watch/". Make no maneja rutas con
# espacios en sus funciones, así que se accede a través del enlace
# simbólico build/fw, creado al leer este Makefile.
CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
BUILD := build
FW := $(BUILD)/fw
$(shell mkdir -p $(BUILD) && ln -sfn "../../../Flex OS Ultra Watch" $(FW))

INC := -I$(FW)/components/watch_power/include \
       -I$(FW)/components/watch_diag/include \
       -I$(FW)/components/watch_theme/include \
       -I$(FW)/components/watch_ui/include \
       -I$(FW)/components/watch_time/include \
       -I$(FW)/components/watch_settings/include
SRC := test_main.c test_ui.c test_power_alerts.c test_time.c test_gesture.c test_power_sched.c test_safe.c \
       test_settings.c $(FW)/components/watch_settings/ws_logic.c \
       test_clock.c $(FW)/components/watch_ui/ui_clock_logic.c \
       test_screen.c $(FW)/components/watch_ui/ui_screen_logic.c \
       $(FW)/components/watch_power/battery_model.c \
       $(FW)/components/watch_power/battery_alerts.c \
       $(FW)/components/watch_power/power_sched.c \
       $(FW)/components/watch_diag/flash_probe_logic.c \
       $(FW)/components/watch_theme/flex_glass_math.c \
       $(FW)/components/watch_theme/flex_wallpaper.c \
       $(FW)/components/watch_theme/flex_batt_style.c \
       $(FW)/components/watch_theme/flex_safe.c \
       $(FW)/components/watch_ui/ui_logic.c \
       $(FW)/components/watch_ui/ui_gesture.c \
       $(FW)/components/watch_time/wt_time.c \
       $(FW)/components/watch_time/rtc_pcf85063.c

.PHONY: all test clean
all: test

$(BUILD)/test_host: $(SRC) $(wildcard $(FW)/components/*/include/*.h)
	$(CC) $(CFLAGS) $(INC) $(SRC) -lm -o $@

test: $(BUILD)/test_host
	./$(BUILD)/test_host

clean:
	rm -rf $(BUILD)
