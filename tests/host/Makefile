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
       -I$(FW)/components/watch_settings/include \
       -I$(FW)/components/watch_imu/include \
       -I$(FW)/components/watch_storage/include \
       -I$(FW)/components/watch_net/include \
       -I$(FW)/components/flexlink/include \
       -I$(FW)/components/watch_files/include \
       -I$(FW)/components/watch_weather/include \
       -I$(FW)/components/watch_browser/include \
       -I$(FW)/components/watch_ota/include
SRC := test_main.c test_ui.c test_power_alerts.c test_time.c test_gesture.c test_power_sched.c test_safe.c \
       test_settings.c $(FW)/components/watch_settings/ws_logic.c \
       test_clock.c $(FW)/components/watch_ui/ui_clock_logic.c \
       test_screen.c $(FW)/components/watch_ui/ui_screen_logic.c \
       test_imu.c $(FW)/components/watch_imu/qmi8658.c \
       test_storage.c $(FW)/components/watch_storage/stg_logic.c \
       test_apps.c $(FW)/components/watch_ui/apps_logic.c \
       test_net.c $(FW)/components/watch_net/net_logic.c \
       test_flexlink.c $(FW)/components/flexlink/flex_link.c $(FW)/components/flexlink/flex_auth.c \
       $(FW)/components/flexlink/flex_phone.c $(FW)/components/flexlink/flex_phone_transport.c \
       $(FW)/components/flexlink/flex_phone_link.c \
       test_files.c $(FW)/components/watch_files/files_logic.c $(FW)/components/watch_files/flexfs.c $(FW)/components/watch_files/fxp.c \
       test_browser.c $(FW)/components/watch_browser/br_logic.c \
       test_br_net.c fbp_fake.c $(FW)/components/watch_browser/br_net.c $(FW)/components/watch_browser/br_lists.c \
       test_ota.c ota_fake.c $(FW)/components/watch_ota/ota_core.c $(FW)/components/watch_ota/ota_sig.c \
       test_weather.c $(FW)/components/watch_weather/wx_logic.c $(FW)/components/watch_weather/wx_core.c \
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

MBT_OUT := $(BUILD)/mbedtls
MBT_CC := $(CC)
include mbedtls_host.mk
.DEFAULT_GOAL := all

.PHONY: all test clean peer
all: test

# El motor del reloj como proceso de PC, para la prueba de interoperabilidad
# con el lado Android (FlexPhone: WatchPeerInteropTest).
PEER_SRC := flexlink_watch_peer.c $(FW)/components/flexlink/flex_link.c $(FW)/components/flexlink/flex_auth.c \
            $(FW)/components/flexlink/flex_phone.c $(FW)/components/flexlink/flex_phone_transport.c \
            $(FW)/components/flexlink/flex_phone_link.c
peer: $(BUILD)/flexlink_watch_peer
$(BUILD)/flexlink_watch_peer: $(PEER_SRC) $(wildcard $(FW)/components/flexlink/include/*.h)
	$(CC) $(CFLAGS) -I$(FW)/components/flexlink/include $(PEER_SRC) -o $@

$(BUILD)/test_host: $(SRC) $(wildcard $(FW)/components/*/include/*.h) $(MBT_LIB)
	$(CC) $(CFLAGS) $(INC) $(MBT_INC) $(SRC) $(MBT_LIB) -lm -o $@

test: $(BUILD)/test_host
	./$(BUILD)/test_host

clean:
	rm -rf $(BUILD)
