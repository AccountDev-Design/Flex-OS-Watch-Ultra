# Pruebas de PC de la lógica pura (sin ESP-IDF): make -C tests/host
ROOT := ../..
CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
INC := -I$(ROOT)/components/watch_power/include \
       -I$(ROOT)/components/watch_diag/include \
       -I$(ROOT)/components/watch_theme/include
SRC := test_main.c \
       $(ROOT)/components/watch_power/battery_model.c \
       $(ROOT)/components/watch_diag/flash_probe_logic.c \
       $(ROOT)/components/watch_theme/flex_glass_math.c \
       $(ROOT)/components/watch_theme/flex_wallpaper.c
BUILD := build

.PHONY: all test clean
all: test

$(BUILD)/test_host: $(SRC) $(wildcard $(ROOT)/components/*/include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@

test: $(BUILD)/test_host
	./$(BUILD)/test_host

clean:
	rm -rf $(BUILD)
