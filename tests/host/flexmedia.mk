# Pila de medios de Flex OS Ultra portada al reloj (components/flexmedia) y
# sus pruebas, portadas también de Flex-OS-Ecosistema @ 5dbc949
# (tests/host/test_*.cpp). C++17 con ASan y UBSan, como en la referencia.
# El JPEG lo decodifica TJpgDec (el de LVGL en el PC): hace falta LVGL_DIR.
#   make -C tests/host flexmedia LVGL_DIR=... IDF_PATH=...
FM_DIR   := $(FW)/components/flexmedia
FM_BUILD := $(BUILD)/fm
CXX      ?= g++
FM_CXX   := -std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
            -fsanitize=address,undefined -fno-omit-frame-pointer
FM_INC   := -I$(FM_DIR)/include -I$(FM_DIR) -I$(IDF_PATH)/components/json/cJSON
FM_LIB   := $(FM_DIR)/FlexOS_MediaLib.cpp
FM_MEDIA := $(FM_DIR)/FlexOS_Media.cpp
FM_ENC   := $(FM_DIR)/FlexOS_JPEGEnc.cpp
FM_THUMB := $(FM_DIR)/FlexOS_MediaThumb.cpp
FM_STORE := $(FM_DIR)/FlexOS_MediaStore.cpp
FM_HTTP  := $(FM_DIR)/FlexOS_HttpShare.cpp
FM_WEB   := $(FM_DIR)/FlexOS_MediaWeb.cpp
FM_QR    := $(FM_DIR)/FlexOS_QR.cpp
FM_HDRS  := $(wildcard $(FM_DIR)/include/*.h) $(FM_DIR)/FlexOS_WebUI.h
FM_JPEG_O := $(FM_BUILD)/flex_jpeg_tjpgd.o $(FM_BUILD)/tjpgd.o $(FM_BUILD)/flex_mp3.o $(FM_BUILD)/flex_id3.o
FM_CJSON := $(FM_BUILD)/cJSON.o

$(FM_BUILD):
	@mkdir -p $(FM_BUILD)
$(FM_BUILD)/tjpgd.o: $(LVGL_DIR)/src/libs/tjpgd/tjpgd.c | $(FM_BUILD)
	$(CC) -std=c11 -O2 -w $(TJPGD_FLAGS) -c $< -o $@
$(FM_BUILD)/flex_jpeg_tjpgd.o: $(FM_DIR)/flex_jpeg_tjpgd.c $(FM_DIR)/include/FlexOS_JPEG.h | $(FM_BUILD)
	$(CC) $(CFLAGS) $(TJPGD_FLAGS) -I$(FM_DIR)/include -c $< -o $@
$(FM_BUILD)/flex_mp3.o: $(FM_DIR)/flex_mp3.c $(FM_DIR)/include/flex_mp3.h | $(FM_BUILD)
	$(CC) $(CFLAGS) -I$(FM_DIR)/include -c $< -o $@
$(FM_BUILD)/flex_id3.o: $(FM_DIR)/flex_id3.c $(FM_DIR)/include/flex_id3.h | $(FM_BUILD)
	$(CC) $(CFLAGS) -I$(FM_DIR)/include -c $< -o $@
$(FM_BUILD)/cJSON.o: $(IDF_PATH)/components/json/cJSON/cJSON.c | $(FM_BUILD)
	$(CC) -std=c11 -O1 -g -w -fsanitize=address,undefined -c $< -o $@

$(FM_BUILD)/test_qr: flexmedia/test_qr.cpp $(FM_QR) $(FM_HDRS) | $(FM_BUILD)
	$(CXX) $(FM_CXX) $(FM_INC) -o $@ flexmedia/test_qr.cpp $(FM_QR)
$(FM_BUILD)/test_httpshare: flexmedia/test_httpshare.cpp $(FM_HTTP) $(FM_HDRS) | $(FM_BUILD)
	$(CXX) $(FM_CXX) $(FM_INC) -o $@ flexmedia/test_httpshare.cpp $(FM_HTTP)
$(FM_BUILD)/test_media: flexmedia/test_media.cpp $(FM_MEDIA) $(FM_HDRS) | $(FM_BUILD)
	$(CXX) $(FM_CXX) $(FM_INC) -o $@ flexmedia/test_media.cpp $(FM_MEDIA)
$(FM_BUILD)/test_medialib: flexmedia/test_medialib.cpp $(FM_LIB) $(FM_HDRS) $(FM_CJSON) | $(FM_BUILD)
	$(CXX) $(FM_CXX) $(FM_INC) -o $@ flexmedia/test_medialib.cpp $(FM_LIB) $(FM_CJSON) -lz
$(FM_BUILD)/test_jpegenc: flexmedia/test_jpegenc.cpp $(FM_ENC) $(FM_HDRS) $(FM_JPEG_O) | $(FM_BUILD)
	$(CXX) $(FM_CXX) $(FM_INC) -o $@ flexmedia/test_jpegenc.cpp $(FM_ENC) $(FM_JPEG_O)
$(FM_BUILD)/test_mediathumb: flexmedia/test_mediathumb.cpp $(FM_THUMB) $(FM_ENC) $(FM_MEDIA) $(FM_HDRS) $(FM_JPEG_O) | $(FM_BUILD)
	$(CXX) $(FM_CXX) $(FM_INC) -o $@ flexmedia/test_mediathumb.cpp $(FM_THUMB) $(FM_ENC) $(FM_MEDIA) $(FM_JPEG_O)
$(FM_BUILD)/test_mediastore: flexmedia/test_mediastore.cpp $(FM_STORE) $(FM_LIB) $(FM_THUMB) $(FM_ENC) $(FM_MEDIA) $(FM_HDRS) $(FM_JPEG_O) | $(FM_BUILD)
	$(CXX) $(FM_CXX) $(FM_INC) -pthread -o $@ flexmedia/test_mediastore.cpp $(FM_STORE) $(FM_LIB) $(FM_THUMB) $(FM_ENC) \
	    $(FM_MEDIA) $(FM_JPEG_O)
$(FM_BUILD)/test_mediaweb: flexmedia/test_mediaweb.cpp $(FM_WEB) $(FM_LIB) $(FM_HTTP) $(FM_THUMB) $(FM_ENC) $(FM_MEDIA) \
                           $(FM_STORE) $(FM_HDRS) $(FM_JPEG_O) $(FM_CJSON) | $(FM_BUILD)
	$(CXX) $(FM_CXX) $(FM_INC) -o $@ flexmedia/test_mediaweb.cpp $(FM_WEB) $(FM_LIB) $(FM_HTTP) $(FM_THUMB) $(FM_ENC) \
	    $(FM_MEDIA) $(FM_STORE) $(FM_JPEG_O) $(FM_CJSON)
$(FM_BUILD)/test_mp3: flexmedia/test_mp3.cpp $(FM_BUILD)/flex_mp3.o $(FM_HDRS) | $(FM_BUILD)
	$(CXX) $(FM_CXX) $(FM_INC) -o $@ flexmedia/test_mp3.cpp $(FM_BUILD)/flex_mp3.o

$(FM_BUILD)/test_id3: flexmedia/test_id3.cpp $(FM_BUILD)/flex_id3.o $(FM_HDRS) | $(FM_BUILD)
	$(CXX) $(FM_CXX) $(FM_INC) -o $@ flexmedia/test_id3.cpp $(FM_BUILD)/flex_id3.o

FM_TESTS := test_mp3 test_id3 test_qr test_httpshare test_media test_medialib test_jpegenc test_mediathumb test_mediastore test_mediaweb
.PHONY: flexmedia flexmedia-skip
flexmedia: $(addprefix $(FM_BUILD)/,$(FM_TESTS))
	@set -e; for t in $(FM_TESTS); do echo "=== $$t"; ./$(FM_BUILD)/$$t; done
flexmedia-skip:
	@echo "=== Pila de medios (flexmedia): pruebas OMITIDAS (define LVGL_DIR e IDF_PATH) ==="
