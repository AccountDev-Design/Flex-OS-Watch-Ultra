/*
 * wa_hw — el hardware de audio y la sesión exclusiva. Ver include/watch_audio.h.
 *
 * La inicialización es la del firmware de fábrica de Waveshare
 * (factory/components/waveshare__esp32_s3_touch_amoled_2_06,
 * bsp_audio_init_voice_24k y bsp_audio_codec_*_init), escrita directamente con
 * ESP-IDF y esp_codec_dev: el BSP del proyecto tiene CONFIG_BSP_ERROR_CHECK y
 * aborta ante cualquier fallo de un códec, y aquí un fallo tiene que ser un
 * error que la app enseña, no un reinicio.
 *
 * Dos configuraciones, según lo que pide la sesión:
 *  - SALIDA (reproductor, vídeo): solo el canal TX, estándar estéreo de 16
 *    bits a la frecuencia del archivo, y el ES8311. 8 búferes DMA de 480
 *    muestras (15 KB de RAM interna, 87 ms a 44,1 kHz) para que una lectura
 *    lenta de la microSD no corte el sonido.
 *  - VOZ (grabadora): TX + RX en dúplex con los relojes compartidos, exactamente
 *    el perfil de fábrica (24 kHz, TDM de 4 ranuras, bclk_div 8), con los
 *    búferes por defecto (6 x 240).
 * Todo se crea al abrir y se destruye al soltar la sesión.
 */
#include "watch_audio.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "es7210_adc.h"
#include "es8311_codec.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "watch_settings.h"

static const char *TAG = "wa_hw";

#define I2S_PORT  I2S_NUM_1      /* CONFIG_BSP_I2S_NUM del BSP y del firmware de fábrica */
#define PIN_MCLK  GPIO_NUM_16
#define PIN_BCLK  GPIO_NUM_41
#define PIN_WS    GPIO_NUM_45
#define PIN_DOUT  GPIO_NUM_40
#define PIN_DIN   GPIO_NUM_42
#define PIN_PA    GPIO_NUM_46
#define TDM_SLOTS 4
#define MIC_GAIN_DB 24.0f        /* CODEC_DEFAULT_ADC_VOLUME de fábrica */
#define MICS_CONNECTED (ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3)
#define MICS_FRONT     (ES7210_SEL_MIC1 | ES7210_SEL_MIC2)
#define PREEMPT_WAIT_MS 1500

typedef enum { MODE_NONE = 0, MODE_OUT, MODE_VOICE } mode_t;

static SemaphoreHandle_t s_mx;      /* la sesión */
static SemaphoreHandle_t s_hw;      /* el hardware (abrir, escribir, cerrar) */
static SemaphoreHandle_t s_freed;   /* se da al soltar una sesión */
static i2c_master_bus_handle_t s_bus;

static wa_owner_t s_owner;
static wa_preempt_cb_t s_pre_cb;
static void *s_pre_ctx;

static mode_t s_mode;
static i2s_chan_handle_t s_tx, s_rx;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static const audio_codec_ctrl_if_t *s_out_ctrl, *s_in_ctrl;
static const audio_codec_if_t *s_out_if, *s_in_if;
static esp_codec_dev_handle_t s_out, s_in;
static bool s_out_open, s_in_open;
static uint32_t s_rate;
static uint8_t s_vol = 60;
static volatile bool s_vol_dirty; /* la interfaz cambió el volumen: lo aplica quien escribe */
static wa_diag_t s_diag;

static void note_err(esp_err_t e, const char *what)
{
    if (e != ESP_OK) {
        s_diag.hw_errors++;
        s_diag.last_err = e;
        ESP_LOGW(TAG, "%s: %s", what, esp_err_to_name(e));
    }
}

/* ---------------- montaje y desmontaje ---------------- */

static void hw_down(void)
{
    if (s_in_open) {
        esp_codec_dev_close(s_in); /* la captura antes que la salida, como en fábrica */
        s_in_open = false;
    }
    if (s_out_open) {
        esp_codec_dev_set_out_mute(s_out, true);
        esp_codec_dev_close(s_out); /* apaga el amplificador */
        s_out_open = false;
    }
    if (s_in) {
        esp_codec_dev_delete(s_in);
        s_in = NULL;
    }
    if (s_out) {
        esp_codec_dev_delete(s_out);
        s_out = NULL;
    }
    if (s_in_if) {
        audio_codec_delete_codec_if(s_in_if);
        s_in_if = NULL;
    }
    if (s_out_if) {
        audio_codec_delete_codec_if(s_out_if);
        s_out_if = NULL;
    }
    if (s_in_ctrl) {
        audio_codec_delete_ctrl_if(s_in_ctrl);
        s_in_ctrl = NULL;
    }
    if (s_out_ctrl) {
        audio_codec_delete_ctrl_if(s_out_ctrl);
        s_out_ctrl = NULL;
    }
    if (s_gpio_if) {
        audio_codec_delete_gpio_if(s_gpio_if);
        s_gpio_if = NULL;
    }
    if (s_data_if) {
        audio_codec_delete_data_if(s_data_if);
        s_data_if = NULL;
    }
    if (s_rx) {
        i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = NULL;
    }
    if (s_tx) {
        i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
    }
    /* El amplificador queda apagado pase lo que pase (su pull-down lo
     * mantiene así, pero no se deja a la suerte). */
    gpio_set_level(PIN_PA, 0);
    s_mode = MODE_NONE;
    s_rate = 0;
    s_diag.hw_up = false;
}

static esp_err_t make_out_codec(void)
{
    s_gpio_if = audio_codec_new_gpio();
    audio_codec_i2c_cfg_t i2c = {.port = 0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = s_bus};
    s_out_ctrl = s_gpio_if ? audio_codec_new_i2c_ctrl(&i2c) : NULL;
    if (!s_out_ctrl) {
        return ESP_ERR_NO_MEM;
    }
    es8311_codec_cfg_t cfg = {
        .ctrl_if = s_out_ctrl,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = PIN_PA,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    s_out_if = es8311_codec_new(&cfg);
    if (!s_out_if) {
        return ESP_FAIL; /* no contesta por I2C */
    }
    esp_codec_dev_cfg_t dc = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = s_out_if, .data_if = s_data_if};
    s_out = esp_codec_dev_new(&dc);
    return s_out ? ESP_OK : ESP_ERR_NO_MEM;
}

static i2s_std_config_t std_cfg(uint32_t rate)
{
    i2s_std_config_t c = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = PIN_MCLK, .bclk = PIN_BCLK, .ws = PIN_WS, .dout = PIN_DOUT, .din = I2S_GPIO_UNUSED},
    };
    c.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    return c;
}

static esp_err_t hw_up_out(uint32_t rate)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    cc.dma_desc_num = 8;
    cc.dma_frame_num = 480;
    cc.auto_clear = true; /* si el reproductor se retrasa, silencio y no ruido */
    esp_err_t e = i2s_new_channel(&cc, &s_tx, NULL);
    if (e == ESP_OK) {
        const i2s_std_config_t c = std_cfg(rate);
        e = i2s_channel_init_std_mode(s_tx, &c);
    }
    if (e == ESP_OK) {
        e = i2s_channel_enable(s_tx);
    }
    if (e == ESP_OK) {
        audio_codec_i2s_cfg_t ic = {.port = I2S_PORT, .rx_handle = NULL, .tx_handle = s_tx};
        s_data_if = audio_codec_new_i2s_data(&ic);
        e = s_data_if ? ESP_OK : ESP_ERR_NO_MEM;
    }
    if (e == ESP_OK) {
        e = make_out_codec();
    }
    if (e != ESP_OK) {
        note_err(e, "salida de audio");
        hw_down();
        return e;
    }
    s_mode = MODE_OUT;
    s_diag.hw_up = true;
    return ESP_OK;
}

static esp_err_t hw_up_voice(void)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    esp_err_t e = i2s_new_channel(&cc, &s_tx, &s_rx);
    i2s_std_config_t tx = std_cfg(WA_VOICE_RATE);
    i2s_tdm_config_t rx = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(WA_VOICE_RATE),
        .slot_cfg = I2S_TDM_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
                                                       I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 |
                                                           I2S_TDM_SLOT3),
        .gpio_cfg = {.mclk = PIN_MCLK, .bclk = PIN_BCLK, .ws = PIN_WS, .dout = I2S_GPIO_UNUSED, .din = PIN_DIN},
    };
    rx.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    rx.clk_cfg.bclk_div = 8;
    rx.slot_cfg.total_slot = TDM_SLOTS;
    if (e == ESP_OK) {
        e = i2s_channel_init_std_mode(s_tx, &tx);
    }
    if (e == ESP_OK) {
        e = i2s_channel_init_tdm_mode(s_rx, &rx);
    }
    if (e == ESP_OK) {
        e = i2s_channel_enable(s_tx);
    }
    if (e == ESP_OK) {
        e = i2s_channel_enable(s_rx);
    }
    if (e == ESP_OK) {
        audio_codec_i2s_cfg_t ic = {.port = I2S_PORT, .rx_handle = s_rx, .tx_handle = s_tx};
        s_data_if = audio_codec_new_i2s_data(&ic);
        e = s_data_if ? ESP_OK : ESP_ERR_NO_MEM;
    }
    if (e == ESP_OK) {
        e = make_out_codec();
    }
    if (e == ESP_OK) {
        audio_codec_i2c_cfg_t i2c = {.port = 0, .addr = ES7210_CODEC_DEFAULT_ADDR, .bus_handle = s_bus};
        s_in_ctrl = audio_codec_new_i2c_ctrl(&i2c);
        es7210_codec_cfg_t cfg = {.ctrl_if = s_in_ctrl, .mic_selected = MICS_CONNECTED};
        s_in_if = s_in_ctrl ? es7210_codec_new(&cfg) : NULL;
        if (!s_in_if) {
            e = ESP_FAIL;
        } else {
            esp_codec_dev_cfg_t dc = {.dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = s_in_if, .data_if = s_data_if};
            s_in = esp_codec_dev_new(&dc);
            e = s_in ? ESP_OK : ESP_ERR_NO_MEM;
        }
    }
    if (e != ESP_OK) {
        note_err(e, "micrófonos");
        hw_down();
        return e;
    }
    s_mode = MODE_VOICE;
    s_diag.hw_up = true;
    return ESP_OK;
}

/* ---------------- sesión ---------------- */

/* Quién puede quitarle el audio a quién: la grabadora a todos (la persona la
 * acaba de pulsar), el vídeo a la música, y la música a nadie. */
static int prio(wa_owner_t o)
{
    return o == WA_OWNER_RECORDER ? 3 : o == WA_OWNER_VIDEO ? 2 : o == WA_OWNER_PLAYER ? 1 : 0;
}

esp_err_t watch_audio_start(i2c_master_bus_handle_t bus)
{
    if (s_mx) {
        return ESP_OK;
    }
    s_bus = bus;
    s_mx = xSemaphoreCreateMutex();
    s_hw = xSemaphoreCreateMutex();
    s_freed = xSemaphoreCreateBinary();
    if (!s_mx || !s_hw || !s_freed) {
        return ESP_ERR_NO_MEM;
    }
    watch_settings_t ws;
    watch_settings_get(&ws);
    s_vol = ws.media_vol;
    /* El amplificador apagado desde el arranque (el pull-down ya lo hace;
     * aquí se deja como salida a 0 por si algo lo dejó en el aire). */
    gpio_config_t io = {.pin_bit_mask = 1ull << PIN_PA, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&io);
    gpio_set_level(PIN_PA, 0);
    return ESP_OK;
}

esp_err_t wa_session_acquire(wa_owner_t who, bool preempt, wa_preempt_cb_t on_preempt, void *ctx)
{
    if (!s_mx || who <= WA_OWNER_NONE || who >= WA_OWNER_N) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (s_owner == who) {
        s_pre_cb = on_preempt;
        s_pre_ctx = ctx;
        xSemaphoreGive(s_mx);
        return ESP_OK;
    }
    if (s_owner != WA_OWNER_NONE) {
        if (!preempt || prio(who) <= prio(s_owner)) {
            const wa_owner_t cur = s_owner;
            xSemaphoreGive(s_mx);
            ESP_LOGI(TAG, "audio ocupado por %s (lo pide %s)", wa_owner_name(cur), wa_owner_name(who));
            return ESP_ERR_INVALID_STATE;
        }
        wa_preempt_cb_t cb = s_pre_cb;
        void *cctx = s_pre_ctx;
        xSemaphoreTake(s_freed, 0); /* se vacía el aviso viejo */
        xSemaphoreGive(s_mx);
        if (cb) {
            cb(cctx); /* el dueño actual para y suelta desde su tarea */
        }
        const bool freed = xSemaphoreTake(s_freed, pdMS_TO_TICKS(PREEMPT_WAIT_MS)) == pdTRUE;
        xSemaphoreTake(s_mx, portMAX_DELAY);
        if (!freed || s_owner != WA_OWNER_NONE) {
            xSemaphoreGive(s_mx);
            ESP_LOGW(TAG, "%s no soltó el audio a tiempo", wa_owner_name(s_owner));
            return ESP_ERR_TIMEOUT;
        }
    }
    s_owner = who;
    s_pre_cb = on_preempt;
    s_pre_ctx = ctx;
    s_diag.owner = who;
    s_diag.sessions++;
    xSemaphoreGive(s_mx);
    return ESP_OK;
}

void wa_session_release(wa_owner_t who)
{
    if (!s_mx) {
        return;
    }
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (s_owner != who) {
        xSemaphoreGive(s_mx);
        return;
    }
    xSemaphoreTake(s_hw, portMAX_DELAY);
    hw_down(); /* amplificador, códecs, I2S y DMA: todo fuera */
    s_diag.out_open = s_diag.in_open = false;
    xSemaphoreGive(s_hw);
    s_owner = WA_OWNER_NONE;
    s_pre_cb = NULL;
    s_pre_ctx = NULL;
    s_diag.owner = WA_OWNER_NONE;
    xSemaphoreGive(s_mx);
    xSemaphoreGive(s_freed);
}

wa_owner_t wa_session_owner(void)
{
    if (!s_mx) {
        return WA_OWNER_NONE;
    }
    xSemaphoreTake(s_mx, portMAX_DELAY);
    const wa_owner_t o = s_owner;
    xSemaphoreGive(s_mx);
    return o;
}

const char *wa_owner_name(wa_owner_t who)
{
    switch (who) {
    case WA_OWNER_PLAYER:   return "Música";
    case WA_OWNER_RECORDER: return "Grabadora";
    case WA_OWNER_VIDEO:    return "Vídeo";
    default:                return "nadie";
    }
}

/* ---------------- salida ---------------- */

static int vol_codec(uint8_t pct)
{
    return pct > 100 ? 100 : pct;
}

esp_err_t wa_out_open(uint32_t rate)
{
    static const uint32_t ok_rates[] = {8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000};
    bool supported = false;
    for (size_t i = 0; i < sizeof(ok_rates) / sizeof(ok_rates[0]); i++) {
        supported = supported || ok_rates[i] == rate;
    }
    if (!supported) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    xSemaphoreTake(s_hw, portMAX_DELAY);
    esp_err_t e = ESP_OK;
    if (s_mode == MODE_VOICE) {
        e = ESP_ERR_INVALID_STATE; /* la grabadora tiene los relojes a 24 kHz */
    } else if (s_mode == MODE_NONE) {
        e = hw_up_out(rate);
    }
    if (e == ESP_OK && (!s_out_open || s_rate != rate)) {
        if (s_out_open) {
            esp_codec_dev_set_out_mute(s_out, true);
            esp_codec_dev_close(s_out);
            s_out_open = false;
        }
        esp_codec_dev_sample_info_t fs = {.bits_per_sample = 16, .channel = 2, .sample_rate = rate};
        const int r = esp_codec_dev_open(s_out, &fs);
        e = r == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
        if (e == ESP_OK) {
            s_out_open = true;
            s_rate = rate;
            esp_codec_dev_set_out_vol(s_out, vol_codec(s_vol));
            esp_codec_dev_set_out_mute(s_out, s_vol == 0);
        }
    }
    note_err(e, "abrir el altavoz");
    s_diag.out_open = s_out_open;
    s_diag.out_rate = s_rate;
    xSemaphoreGive(s_hw);
    return e;
}

esp_err_t wa_out_write(const void *pcm, size_t bytes)
{
    /* Solo lo llama la tarea dueña de la sesión, que es también la única que
     * la cierra: el cerrojo no se mantiene mientras el DMA hace esperar. */
    xSemaphoreTake(s_hw, portMAX_DELAY);
    esp_codec_dev_handle_t out = s_out_open && s_mode == MODE_OUT ? s_out : NULL;
    if (out && s_vol_dirty) {
        s_vol_dirty = false;
        esp_codec_dev_set_out_vol(out, vol_codec(s_vol));
        esp_codec_dev_set_out_mute(out, s_vol == 0);
    }
    xSemaphoreGive(s_hw);
    if (!out) {
        return ESP_ERR_INVALID_STATE;
    }
    if (esp_codec_dev_write(out, (void *)pcm, (int)bytes) != ESP_CODEC_DEV_OK) {
        note_err(ESP_FAIL, "escribir audio");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void wa_out_close(void)
{
    xSemaphoreTake(s_hw, portMAX_DELAY);
    if (s_out_open && s_mode == MODE_OUT) {
        esp_codec_dev_set_out_mute(s_out, true);
        esp_codec_dev_close(s_out);
        s_out_open = false;
    }
    s_diag.out_open = s_out_open;
    xSemaphoreGive(s_hw);
}

void wa_out_mute(bool mute)
{
    xSemaphoreTake(s_hw, portMAX_DELAY);
    if (s_out_open && s_mode == MODE_OUT) {
        esp_codec_dev_set_out_mute(s_out, mute || s_vol == 0);
    }
    xSemaphoreGive(s_hw);
}

/* ---------------- entrada ---------------- */

esp_err_t wa_in_open_voice(void)
{
    xSemaphoreTake(s_hw, portMAX_DELAY);
    esp_err_t e = ESP_OK;
    if (s_mode == MODE_OUT) {
        hw_down(); /* la sesión cambia de uso: se monta el dúplex de fábrica */
    }
    if (s_mode == MODE_NONE) {
        e = hw_up_voice();
    }
    if (e == ESP_OK && !s_out_open) {
        /* La salida da los relojes; muda y sin amplificador. */
        esp_codec_dev_sample_info_t fs = {.bits_per_sample = 16, .channel = 2, .sample_rate = WA_VOICE_RATE};
        e = esp_codec_dev_open(s_out, &fs) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
        if (e == ESP_OK) {
            s_out_open = true;
            s_rate = WA_VOICE_RATE;
            esp_codec_dev_set_out_mute(s_out, true);
            gpio_set_level(PIN_PA, 0); /* ni ruido de fondo ni realimentación */
        }
    }
    if (e == ESP_OK && !s_in_open) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = TDM_SLOTS,
            .channel_mask = 0x0F, /* las 4 ranuras: MIC1, MIC3 (eco), MIC2, MIC4 */
            .sample_rate = WA_VOICE_RATE,
        };
        e = esp_codec_dev_open(s_in, &fs) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
        if (e == ESP_OK) {
            s_in_open = true;
            esp_codec_dev_set_in_channel_gain(s_in, MICS_FRONT, MIC_GAIN_DB);
        }
    }
    note_err(e, "abrir los micrófonos");
    s_diag.out_open = s_out_open;
    s_diag.in_open = s_in_open;
    s_diag.out_rate = s_rate;
    xSemaphoreGive(s_hw);
    return e;
}

esp_err_t wa_in_read(int16_t *tdm, size_t bytes)
{
    /* Sin el cerrojo del hardware: la lectura bloquea hasta que llegan las
     * muestras y solo la hace la tarea de captura, dueña de la sesión. */
    if (!s_in_open || s_mode != MODE_VOICE || bytes % (TDM_SLOTS * 2)) {
        return ESP_ERR_INVALID_STATE;
    }
    const int r = esp_codec_dev_read(s_in, tdm, (int)bytes);
    if (r != ESP_CODEC_DEV_OK) {
        note_err(ESP_FAIL, "leer los micrófonos");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void wa_in_close(void)
{
    xSemaphoreTake(s_hw, portMAX_DELAY);
    if (s_in_open) {
        esp_codec_dev_close(s_in);
        s_in_open = false;
    }
    s_diag.in_open = false;
    xSemaphoreGive(s_hw);
}

/* ---------------- volumen y diagnóstico ---------------- */

void wa_set_volume(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    /* Sin tocar el códec aquí (puede llamarlo la interfaz): lo aplica la
     * tarea de audio en su próxima escritura (unos 26 ms). */
    s_vol = percent;
    s_vol_dirty = true;
    watch_settings_t ws;
    watch_settings_get(&ws);
    if (ws.media_vol != percent) {
        ws.media_vol = percent;
        watch_settings_update(&ws); /* se guarda agrupado (watch_settings) */
    }
}

uint8_t wa_get_volume(void)
{
    return s_vol;
}

void wa_diag(wa_diag_t *out)
{
    if (!s_mx) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mx, portMAX_DELAY);
    *out = s_diag;
    xSemaphoreGive(s_mx);
}
