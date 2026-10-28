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
       -I$(FW)/components/watch_ota/include \
       -I$(FW)/components/watch_activity/include \
       -I$(FW)/components/watch_media/include \
       -I$(FW)/components/watch_audio/include
ACT_DIR := $(FW)/components/watch_activity
SRC := test_main.c test_ui.c test_power_alerts.c test_time.c test_gesture.c test_power_sched.c test_safe.c \
       test_settings.c $(FW)/components/watch_settings/ws_logic.c \
       test_clock.c $(FW)/components/watch_ui/ui_clock_logic.c \
       test_screen.c $(FW)/components/watch_ui/ui_screen_logic.c \
       test_imu.c $(FW)/components/watch_imu/qmi8658.c $(FW)/components/watch_imu/imu_heading.c \
       test_storage.c $(FW)/components/watch_storage/stg_logic.c $(FW)/components/watch_storage/stg_usage.c \
       test_media.c $(FW)/components/watch_media/wm_sd_logic.c $(FW)/components/watch_media/wm_fsclass.c \
       $(FW)/components/watch_media/wm_path.c \
       test_audio.c $(FW)/components/watch_audio/wa_logic.c \
       test_apps.c $(FW)/components/watch_ui/apps_logic.c \
       test_grid.c $(FW)/components/watch_ui/ui_grid_logic.c test_glass.c \
       $(FW)/components/watch_theme/flex_thumb.c \
       test_net.c $(FW)/components/watch_net/net_logic.c \
       test_flexlink.c $(FW)/components/flexlink/flex_link.c $(FW)/components/flexlink/flex_auth.c \
       $(FW)/components/flexlink/flex_phone.c $(FW)/components/flexlink/flex_phone_transport.c \
       $(FW)/components/flexlink/flex_phone_link.c \
       test_files.c $(FW)/components/watch_files/files_logic.c $(FW)/components/watch_files/flexfs.c $(FW)/components/watch_files/fxp.c \
       test_browser.c $(FW)/components/watch_browser/br_logic.c \
       test_br_net.c fbp_fake.c $(FW)/components/watch_browser/br_net.c $(FW)/components/watch_browser/br_lists.c \
       test_br_backend.c $(FW)/components/watch_browser/br_backend.c \
       test_ota.c ota_fake.c $(FW)/components/watch_ota/ota_core.c $(FW)/components/watch_ota/ota_sig.c \
       test_weather.c $(FW)/components/watch_weather/wx_logic.c $(FW)/components/watch_weather/wx_core.c \
       test_activity.c $(ACT_DIR)/act_catalog.c $(ACT_DIR)/act_steps.c $(ACT_DIR)/act_session.c \
       $(ACT_DIR)/act_metrics.c $(ACT_DIR)/act_engine.c $(ACT_DIR)/act_store.c \
       $(FW)/components/watch_ui/ui_wb_logic.c \
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

.PHONY: all test clean peer gallery gallery-skip
all: test $(if $(LVGL_DIR),gallery,gallery-skip) $(if $(LVGL_DIR),flexmedia,flexmedia-skip)

# Galería (watch_gallery): binario aparte porque decodifica con el TJpgDec de
# LVGL (en el reloj, el de la ROM del S3). Sin LVGL_DIR se dice, no se da por pasada.
#   make -C tests/host gallery LVGL_DIR=/ruta/a/lvgl-9.5.0
GAL_DIR := $(FW)/components/watch_gallery
GAL_CORE := $(filter-out $(GAL_DIR)/watch_gallery.c,$(wildcard $(GAL_DIR)/*.c)) # sin la capa de ESP-IDF
GAL_SRC := test_gallery.c gal_fake.c gal_fake_eco.c $(GAL_CORE) \
           $(FW)/components/watch_files/files_logic.c $(FW)/components/watch_files/flexfs.c \
           $(FW)/components/watch_ota/ota_sig.c
GAL_INC := -I. -I$(GAL_DIR)/include -I$(GAL_DIR) -I$(FW)/components/watch_files/include \
           -I$(FW)/components/watch_ota/include
TJPGD_FLAGS = -DLV_CONF_SKIP -DLV_USE_TJPGD=1 -I$(LVGL_DIR) -I$(LVGL_DIR)/src
include flexmedia.mk
$(BUILD)/tjpgd.o: $(LVGL_DIR)/src/libs/tjpgd/tjpgd.c
	$(CC) -std=c11 -O2 -w $(TJPGD_FLAGS) -c $< -o $@
$(BUILD)/test_gallery: $(GAL_SRC) $(wildcard $(GAL_DIR)/include/*.h) $(GAL_DIR)/gal_priv.h gal_fake.h gal_fake_eco.h \
                       $(BUILD)/tjpgd.o $(MBT_LIB)
	$(CC) $(CFLAGS) $(TJPGD_FLAGS) $(GAL_INC) $(MBT_INC) $(GAL_SRC) $(BUILD)/tjpgd.o $(MBT_LIB) -lm -o $@
gallery: $(BUILD)/test_gallery
	./$(BUILD)/test_gallery fixtures
	./gallery_checks.sh
gallery-skip:
	@echo "=== Galería: pruebas OMITIDAS (define LVGL_DIR con la ruta a LVGL 9.5.0) ==="

# Galería contra los servidores REALES: el Flex Web Server del P4 (de REF_DIR,
# compilado fuera de su repositorio) y Flex Cloud (CLOUD_DIR, Node). Ver gallery_e2e.sh.
GAL_E2E_SRC := gal_e2e.c gal_posix_io.c gal_fake.c $(GAL_CORE) \
               $(FW)/components/watch_files/files_logic.c $(FW)/components/watch_files/flexfs.c \
               $(FW)/components/watch_ota/ota_sig.c
$(BUILD)/gal_e2e: $(GAL_E2E_SRC) $(wildcard $(GAL_DIR)/include/*.h) gal_posix_io.h $(BUILD)/tjpgd.o $(MBT_LIB)
	$(CC) $(CFLAGS) $(TJPGD_FLAGS) $(GAL_INC) $(MBT_INC) $(GAL_E2E_SRC) $(BUILD)/tjpgd.o $(MBT_LIB) -lm -o $@
.PHONY: gallery-e2e
gallery-e2e: $(BUILD)/gal_e2e
	./gallery_e2e.sh

# Galería con DOS hilos (la tarea del motor y la interfaz, como en el reloj)
# bajo ThreadSanitizer: las demás pruebas ejecutan el motor en un solo hilo.
#   make -C tests/host race LVGL_DIR=/ruta/a/lvgl-9.5.0
RACE_SRC := gal_race.c $(GAL_CORE) $(FW)/components/watch_files/files_logic.c $(FW)/components/watch_files/flexfs.c \
            $(FW)/components/watch_ota/ota_sig.c
$(BUILD)/tsan/tjpgd.o: $(LVGL_DIR)/src/libs/tjpgd/tjpgd.c
	@mkdir -p $(BUILD)/tsan
	$(CC) -std=c11 -O2 -w -fsanitize=thread $(TJPGD_FLAGS) -c $< -o $@
$(BUILD)/tsan/gal_race: $(RACE_SRC) $(wildcard $(GAL_DIR)/include/*.h) $(GAL_DIR)/gal_priv.h $(BUILD)/tsan/tjpgd.o $(MBT_LIB)
	$(CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=thread $(TJPGD_FLAGS) $(GAL_INC) $(MBT_INC) $(RACE_SRC) \
	    $(BUILD)/tsan/tjpgd.o $(MBT_LIB) -lm -lpthread -o $@
.PHONY: race
race: $(BUILD)/tsan/gal_race
	TSAN_OPTIONS="halt_on_error=1" ./$(BUILD)/tsan/gal_race 60

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
