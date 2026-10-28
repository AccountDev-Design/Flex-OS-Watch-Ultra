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
          -I$(FW)/components/watch_time/include -I$(FW)/components/watch_settings/include \
          -I$(FW)/components/watch_imu/include \
          -I$(FW)/components/watch_storage/include \
          -I$(FW)/components/watch_net/include \
          -I$(FW)/components/flexlink/include \
          -I$(FW)/components/watch_phone/include \
          -I$(FW)/components/watch_files/include \
          -I$(FW)/components/watch_weather/include \
          -I$(FW)/components/watch_browser/include \
          -I$(FW)/components/watch_ota/include \
          -I$(FW)/components/watch_gallery/include \
          -I$(FW)/components/watch_activity/include \
          -I$(FW)/components/watch_media/include \
          -I$(FW)/components/watch_audio/include \
          -I$(FW)/components/flexmedia/include \
          -I../../tests/host

LVGL_SRC := $(shell find $(LVGL_DIR)/src -name '*.c')
APP_SRC := $(wildcard sim_*.c) \
           $(wildcard $(FW)/components/watch_ui/*.c) \
           $(wildcard $(FW)/components/watch_theme/*.c) \
           $(wildcard $(FW)/components/watch_theme/fonts/*.c) \
           $(FW)/components/watch_power/battery_model.c \
           $(FW)/components/watch_power/battery_alerts.c \
           $(FW)/components/watch_time/wt_time.c \
           $(FW)/components/watch_time/rtc_pcf85063.c \
           $(FW)/components/watch_settings/ws_logic.c \
           $(FW)/components/watch_imu/qmi8658.c \
           $(FW)/components/watch_imu/imu_heading.c \
           $(FW)/components/watch_storage/stg_logic.c \
           $(FW)/components/watch_storage/stg_usage.c \
           $(FW)/components/watch_net/net_logic.c \
           $(FW)/components/watch_net/net_text.c \
           $(wildcard $(FW)/components/flexlink/*.c) \
           $(FW)/components/watch_phone/phone_core.c \
           $(FW)/components/watch_phone/phone_text.c \
           $(FW)/components/watch_files/files_logic.c \
           $(FW)/components/watch_files/flexfs.c \
           $(FW)/components/watch_files/fxp.c \
           $(FW)/components/watch_weather/wx_logic.c \
           $(FW)/components/watch_weather/wx_core.c \
           $(FW)/components/watch_browser/br_logic.c \
           $(FW)/components/watch_browser/br_net.c \
           $(FW)/components/watch_browser/br_lists.c \
           $(FW)/components/watch_browser/br_backend.c \
           ../../tests/host/fbp_fake.c \
           $(FW)/components/watch_ota/ota_core.c \
           $(FW)/components/watch_ota/ota_sig.c \
           ../../tests/host/ota_fake.c \
           $(filter-out $(FW)/components/watch_gallery/watch_gallery.c $(FW)/components/watch_gallery/gal_wm.c,$(wildcard $(FW)/components/watch_gallery/*.c)) \
           ../../tests/host/gal_fake.c \
           ../../tests/host/gal_fake_eco.c \
           $(FW)/components/watch_diag/flash_probe_logic.c \
           $(FW)/components/watch_media/wm_sd_logic.c \
           $(FW)/components/watch_media/wm_fsclass.c \
           $(FW)/components/watch_media/wm_path.c \
           $(FW)/components/watch_media/wm_text.c \
           $(FW)/components/watch_media/wm_web_logic.c \
           $(FW)/components/watch_media/wm_video_logic.c \
           $(FW)/components/watch_audio/wa_logic.c \
           $(filter-out $(FW)/components/watch_activity/watch_activity.c,$(wildcard $(FW)/components/watch_activity/*.c))

LVGL_OBJ := $(patsubst $(LVGL_DIR)/%.c,$(BUILD)/lvgl/%.o,$(LVGL_SRC))

# El codificador QR del Flex Web Server (C++ portable, sin biblioteca de C++):
# el mismo que pinta el QR de "Recibir desde el móvil" en el reloj.
CXX ?= c++
QR_OBJ := $(BUILD)/flexmedia/FlexOS_QR.o

# Iconos originales de las apps: el mismo .incbin alineado que genera
# components/watch_theme/CMakeLists.txt para el firmware.
ICONS := reloj calend calc clima musica notas flexphone nav brujula devcare almacen paint flexstore ajustes sensores galeria bienestar grabadora
ICON_BINS := $(foreach n,$(ICONS),$(FW)/assets/icons/app_$(n).bin)
ICON_ASM := $(BUILD)/flex_app_icons_data.S

# Fondos de las esferas fotográficas: el mismo .incbin que genera
# components/watch_theme/CMakeLists.txt (con su final, para el tamaño).
FACES := montana analogica gato tierra flores taj
FACE_JPGS := $(foreach n,$(FACES),$(FW)/assets/faces/face_$(n).jpg)
FACE_ASM := $(BUILD)/flex_face_bg_data.S

# Páginas JPEG de prueba del navegador (gen_br_pages.py) y fotos de la Galería (gen_gal_fixtures.py)
BR_JPGS := $(wildcard fixtures/br_*.jpg fixtures/gal_*.jpg)
BR_ASM := $(BUILD)/br_pages_data.S

# mbedTLS de ESP-IDF para el PC: la firma del OTA se verifica de verdad.
MBT_OUT := $(BUILD)/mbedtls
MBT_CC := $(CC)
include ../../tests/host/mbedtls_host.mk
.DEFAULT_GOAL := all
CFLAGS += $(MBT_INC)

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

$(ICON_ASM): $(ICON_BINS) Makefile
	@mkdir -p $(dir $@)
	@{ echo '.section .rodata.flex_app_icons, "a"'; \
	  for n in $(ICONS); do \
	    printf '.balign 4\n.global _binary_app_%s_bin_start\n_binary_app_%s_bin_start:\n.incbin "%s"\n' \
	      $$n $$n "$(abspath $(FW))/assets/icons/app_$$n.bin"; \
	  done; echo '.section .note.GNU-stack,"",@progbits'; } > $@

$(FACE_ASM): $(FACE_JPGS) Makefile
	@mkdir -p $(dir $@)
	@{ echo '.section .rodata.flex_face_bg, "a"'; \
	  for n in $(FACES); do \
	    printf '.balign 4\n.global _binary_face_%s_jpg_start\n_binary_face_%s_jpg_start:\n.incbin "%s"\n.global _binary_face_%s_jpg_end\n_binary_face_%s_jpg_end:\n' \
	      $$n $$n "$(abspath $(FW))/assets/faces/face_$$n.jpg" $$n $$n; \
	  done; echo '.section .note.GNU-stack,"",@progbits'; } > $@

$(BR_ASM): $(BR_JPGS) Makefile
	@mkdir -p $(dir $@)
	@{ echo '.section .rodata.br_pages, "a"'; \
	  for f in $(BR_JPGS); do n=$$(basename $$f .jpg); \
	    printf '.balign 4\n.global _binary_%s_jpg_start\n_binary_%s_jpg_start:\n.incbin "%s"\n.global _binary_%s_jpg_end\n_binary_%s_jpg_end:\n' \
	      $$n $$n "$(abspath $$f)" $$n $$n; \
	  done; echo '.section .note.GNU-stack,"",@progbits'; } > $@

$(QR_OBJ): $(FW)/components/flexmedia/FlexOS_QR.cpp $(FW)/components/flexmedia/include/FlexOS_QR.h
	@mkdir -p $(dir $@)
	@$(CXX) -std=gnu++17 -O1 -g -Wall -Werror $(SANFLAGS) -fno-exceptions -fno-rtti \
	  -I$(FW)/components/flexmedia/include -c $< -o $@

$(BUILD)/sim: $(APP_SRC) $(APP_HDR) $(LVGL_OBJ) $(ICON_ASM) $(FACE_ASM) $(BR_ASM) $(MBT_LIB) $(QR_OBJ)
	@echo "LD $@"
	@$(CC) $(CFLAGS) -Werror -pthread $(APP_SRC) $(ICON_ASM) $(FACE_ASM) $(BR_ASM) $(LVGL_OBJ) $(QR_OBJ) $(MBT_LIB) -lm -o $@

clean:
	rm -rf build build-san out
