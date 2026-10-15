/*
 * watch_power — ver include/watch_power.h
 *
 * Acceso al AXP2101: cada lectura es una transacción I2C "escribir dirección
 * de registro + leer 1 byte" (i2c_master_transmit_receive). El byte escrito es
 * solo el puntero de registro; no se modifica el contenido de ningún registro.
 * El driver i2c_master serializa las transacciones del bus que comparte con el
 * táctil, el IMU y el RTC.
 */
#include "watch_power.h"

#include <string.h>
#include "power_sched.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "watch_power";

#define I2C_TIMEOUT_MS   50
/* 3072 dejaba 656 B libres en el reloj (Diagnóstico, 2026-10-15): margen escaso
 * con el registro por serie. */
#define TASK_STACK       4096
#define TASK_PRIO        2
#define TASK_CORE        0
/* Registro por serie: siempre que cambie algo y, como mínimo, cada 10 s. */
#define LOG_EVERY_US     (10 * 1000 * 1000)
/* Configuración del cargador (solo lectura): al arrancar y cada 60 s. Puede
 * cambiar sin que lo haga este firmware: el AXP2101 conserva sus registros
 * mientras tenga batería, aunque otro firmware los haya escrito antes. */
#define CFG_EVERY_US      (60 * 1000 * 1000)
/* Aviso si la pila de la tarea queda con menos margen que esto. */
#define STACK_WARN_BYTES  512

#ifdef CONFIG_BSP_I2C_CLK_SPEED_HZ
#define AXP_SCL_HZ CONFIG_BSP_I2C_CLK_SPEED_HZ
#else
#define AXP_SCL_HZ 400000
#endif

static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_lock;
static watch_power_snapshot_t s_snap;
static bool s_started;
static uint32_t s_gen; /* cambia cuando cambia algo que la interfaz muestra */

static esp_err_t rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, I2C_TIMEOUT_MS);
}

/* Muestra completa. 00h llega del sondeo ligero; el identificador, 18h y 30h
 * se leen con la configuración (cada 60 s) y se reutilizan aquí. VBAT ocupa
 * dos registros que se leen por separado (como XPowersLib); para no mezclar el
 * byte alto de una conversión con el bajo de otra se relee el alto y se
 * repite si cambió. 5 lecturas en el caso normal. */
static esp_err_t sample(axp2101_regs_t *r)
{
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_STATUS2, &r->status2), TAG, "status2");
    for (int i = 0; i < 3; i++) {
        uint8_t h2;
        ESP_RETURN_ON_ERROR(rd(AXP2101_REG_VBAT_H, &r->vbat_h), TAG, "vbat_h");
        ESP_RETURN_ON_ERROR(rd(AXP2101_REG_VBAT_L, &r->vbat_l), TAG, "vbat_l");
        ESP_RETURN_ON_ERROR(rd(AXP2101_REG_VBAT_H, &h2), TAG, "vbat_h2");
        if (h2 == r->vbat_h) {
            break;
        }
    }
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_PERCENT, &r->percent), TAG, "percent");
    return ESP_OK;
}

/* Registros de configuración del cargador: solo lectura, para diagnóstico. */
static esp_err_t read_cfg(axp2101_cfg_regs_t *c, uint8_t *chip_id)
{
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_CHIP_ID, chip_id), TAG, "id");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_VINDPM, &c->vindpm), TAG, "0x15");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_IIN_LIM, &c->iin_lim), TAG, "0x16");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_GAUGE_CTRL, &c->gauge_ctrl), TAG, "0x18");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_ADC_CTRL, &c->adc_ctrl), TAG, "0x30");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_TS_CTRL, &c->ts_ctrl), TAG, "0x50");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_IPRECHG, &c->iprechg), TAG, "0x61");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_ICC, &c->icc), TAG, "0x62");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_ITERM, &c->iterm), TAG, "0x63");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_CV, &c->cv), TAG, "0x64");
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_BAT_DET, &c->bat_det), TAG, "0x68");
    return ESP_OK;
}

static void log_cfg(const axp2101_cfg_regs_t *r, const axp2101_cfg_t *c)
{
    ESP_LOGI(TAG, "AXP2101 config regs 15=%02X 16=%02X 18=%02X 30=%02X 50=%02X 61=%02X 62=%02X 63=%02X 64=%02X 68=%02X",
             r->vindpm, r->iin_lim, r->gauge_ctrl, r->adc_ctrl, r->ts_ctrl, r->iprechg, r->icc, r->iterm,
             r->cv, r->bat_det);
    ESP_LOGI(TAG, "cargador=%s CV=%d mV ICC=%d mA precarga=%d mA fin=%d mA (%s) | entrada: VINDPM=%d mV limite=%d mA",
             c->charger_enabled ? "habilitado" : "DESHABILITADO", c->cv_mv, c->charge_ma, c->precharge_ma,
             c->term_ma, c->term_enabled ? "activo" : "inactivo", c->vindpm_mv, c->input_limit_ma);
    ESP_LOGI(TAG, "pin TS: %s, canal ADC TS=%d, fuente de corriente=%u | ADC VBAT=%d gauge=%d deteccion bateria=%d pila RTC=%d",
             c->ts_affects_charger ? "AFECTA al cargador" : "no afecta al cargador", c->ts_adc_enabled,
             (unsigned)c->ts_current_mode, c->vbat_adc_enabled, c->gauge_enabled, c->batt_detect_enabled,
             c->button_cell_charge);
}

static void log_sample(const watch_power_snapshot_t *s)
{
    const axp2101_regs_t *r = &s->regs;
    const axp2101_status_t *d = &s->raw;
    ESP_LOGI(TAG, "AXP2101 regs 00=%02X 01=%02X 03=%02X 18=%02X 30=%02X 34=%02X 35=%02X A4=%02X",
             r->status1, r->status2, r->chip_id, r->gauge_ctrl, r->adc_ctrl, r->vbat_h, r->vbat_l, r->percent);
    ESP_LOGI(TAG, "bateria=%s vbus=%d vindpm=%d lim_entrada=%d termica=%d dir=%d etapa=%d cargador=%d | "
             "VBAT=%d mV (filtrada %d) %%=%d (mostrado %d) estado=%s nivel=%d cargas=%lu",
             d->batt_present ? "si" : "no", d->vbus_present, d->vindpm, d->input_limited, d->thermal_reg,
             d->dir, d->stage, d->charger_enabled, d->vbat_mv, s->track.vbat_mv, d->percent,
             s->track.percent, batt_state_name(s->track.state), s->track.level,
             (unsigned long)s->track.charge_events);
    ESP_LOGI(TAG, "lectura I2C %lu us (max %lu us) | errores=%lu | pila libre minima %lu B",
             (unsigned long)s->sample_us_last, (unsigned long)s->sample_us_max, (unsigned long)s->errors,
             (unsigned long)s->stack_free_min);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Lo que la interfaz muestra: si cambia, se incrementa s_gen. */
static bool visible_changed(const watch_power_snapshot_t *a, const watch_power_snapshot_t *b)
{
    return a->track.state != b->track.state || a->track.percent != b->track.percent ||
           a->track.level != b->track.level || a->track.vbat_mv / 10 != b->track.vbat_mv / 10 ||
           a->track.charge_events != b->track.charge_events || a->raw.vbus_present != b->raw.vbus_present ||
           a->raw.vindpm != b->raw.vindpm || a->driver_ok != b->driver_ok || a->cfg_ok != b->cfg_ok;
}

static void power_task(void *arg)
{
    (void)arg;
    batt_state_t last_state = BATT_STATE_UNKNOWN;
    int last_pct = -2;
    int64_t last_log = 0;
    int64_t last_cfg = 0;
    uint8_t chip_id = AXP2101_CHIP_ID_VALUE; /* leído en watch_power_start() */
    uint8_t gauge_ctrl = 0, adc_ctrl = 0;
    power_sched_t ps;
    power_sched_init(&ps);

    for (;;) {
        /* Sondeo ligero: solo 00h (VBUS y batería presentes). */
        uint8_t status1 = 0;
        const esp_err_t perr = rd(AXP2101_REG_STATUS1, &status1);
        const uint32_t t_ms = now_ms();
        if (!power_sched_need_full(&ps, t_ms, perr == ESP_OK, status1, batt_tracker_pending(&s_snap.track))) {
            vTaskDelay(pdMS_TO_TICKS(power_sched_delay_ms(&ps, t_ms, batt_tracker_pending(&s_snap.track))));
            continue;
        }

        /* Configuración del cargador (solo lectura): al arrancar y cada 60 s,
         * fuera del bloqueo de la instantánea. Trae también el identificador
         * y los bits de gauge y ADC que necesita la decodificación. */
        axp2101_cfg_regs_t cfg_regs = {0};
        const int64_t t_us = esp_timer_get_time();
        const bool cfg_due = !s_snap.cfg_ok || t_us - last_cfg >= CFG_EVERY_US;
        bool cfg_ok = false;
        if (cfg_due && perr == ESP_OK) {
            cfg_ok = read_cfg(&cfg_regs, &chip_id) == ESP_OK;
            last_cfg = t_us;
            if (cfg_ok) {
                gauge_ctrl = cfg_regs.gauge_ctrl;
                adc_ctrl = cfg_regs.adc_ctrl;
            }
        }

        axp2101_regs_t regs = {0};
        const int64_t t0 = esp_timer_get_time();
        esp_err_t err = perr;
        if (err == ESP_OK) {
            err = sample(&regs);
        }
        const uint32_t dt = (uint32_t)(esp_timer_get_time() - t0);
        regs.status1 = status1;
        regs.chip_id = chip_id;
        regs.gauge_ctrl = gauge_ctrl;
        regs.adc_ctrl = adc_ctrl;
        axp2101_status_t dec = {0};
        if (err == ESP_OK) {
            axp2101_decode(&regs, &dec);
        }
        const bool ok = (err == ESP_OK) && dec.chip_ok;

        const uint32_t stack_free = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        bool cfg_changed = false;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        const watch_power_snapshot_t before = s_snap;
        if (ok) {
            s_snap.regs = regs;
            s_snap.raw = dec;
            s_snap.samples++;
            s_snap.last_ok_us = esp_timer_get_time();
        } else {
            s_snap.errors++;
        }
        if (cfg_ok) {
            cfg_changed = !s_snap.cfg_ok || memcmp(&cfg_regs, &s_snap.cfg_regs, sizeof(cfg_regs)) != 0;
            s_snap.cfg_regs = cfg_regs;
            axp2101_decode_cfg(&cfg_regs, &s_snap.cfg);
            s_snap.cfg_ok = true;
        } else if (cfg_due) {
            s_snap.errors++;
        }
        s_snap.sample_us_last = dt;
        if (dt > s_snap.sample_us_max) {
            s_snap.sample_us_max = dt;
        }
        if (s_snap.stack_free_min == 0 || stack_free < s_snap.stack_free_min) {
            s_snap.stack_free_min = stack_free;
        }
        batt_tracker_update_at(&s_snap.track, &dec, ok, now_ms());
        const bool state_changed = s_snap.track.state != before.track.state;
        const bool pending = batt_tracker_pending(&s_snap.track);
        if (visible_changed(&before, &s_snap)) {
            __atomic_add_fetch(&s_gen, 1, __ATOMIC_RELEASE);
        }
        const watch_power_snapshot_t copy = s_snap;
        xSemaphoreGive(s_lock);
        power_sched_full_done(&ps, t_ms, state_changed, pending);

        if (cfg_changed) {
            log_cfg(&copy.cfg_regs, &copy.cfg);
        }
        if (stack_free < STACK_WARN_BYTES) {
            ESP_LOGW(TAG, "pila de la tarea casi agotada: %lu B libres", (unsigned long)stack_free);
        }

        const int64_t now = esp_timer_get_time();
        if (copy.track.state != last_state || copy.track.percent != last_pct ||
            copy.track.charging_started || now - last_log >= LOG_EVERY_US) {
            if (ok) {
                log_sample(&copy);
            } else {
                ESP_LOGW(TAG, "lectura del AXP2101 fallida (%s, id=%02X); errores=%lu",
                         esp_err_to_name(err), regs.chip_id, (unsigned long)copy.errors);
            }
            if (copy.track.charging_started) {
                ESP_LOGI(TAG, "transicion CONFIRMADA a 'cargando' (evento %lu)",
                         (unsigned long)copy.track.charge_events);
            }
            last_state = copy.track.state;
            last_pct = copy.track.percent;
            last_log = now;
        }
        vTaskDelay(pdMS_TO_TICKS(power_sched_delay_ms(&ps, now_ms(), pending)));
    }
}

esp_err_t watch_power_start(i2c_master_bus_handle_t bus)
{
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_ARG, TAG, "bus I2C NULL");
    ESP_RETURN_ON_FALSE(!s_started, ESP_ERR_INVALID_STATE, TAG, "ya iniciado");

    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    memset(&s_snap, 0, sizeof(s_snap));
    batt_tracker_init(&s_snap.track);

    /* Comprobación de presencia: solo la dirección, sin datos. */
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, AXP2101_I2C_ADDR, I2C_TIMEOUT_MS), TAG,
                        "el AXP2101 no responde en 0x%02X", AXP2101_I2C_ADDR);

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDR,
        .scl_speed_hz = AXP_SCL_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "add_device");

    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(rd(AXP2101_REG_CHIP_ID, &id), TAG, "lectura del identificador");
    if (id != AXP2101_CHIP_ID_VALUE) {
        ESP_LOGE(TAG, "identificador 0x%02X distinto de 0x%02X: no se usa", id, AXP2101_CHIP_ID_VALUE);
        return ESP_ERR_NOT_FOUND;
    }
    s_snap.driver_ok = true;
    ESP_LOGI(TAG, "AXP2101 detectado (id 0x%02X). Modo SOLO LECTURA.", id);

    BaseType_t ok = xTaskCreatePinnedToCore(power_task, "watch_power", TASK_STACK, NULL,
                                            TASK_PRIO, NULL, TASK_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "tarea");
    s_started = true;
    return ESP_OK;
}

uint32_t watch_power_generation(void)
{
    return __atomic_load_n(&s_gen, __ATOMIC_ACQUIRE);
}

bool watch_power_get(watch_power_snapshot_t *out)
{
    if (!s_started || !out) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_snap;
    xSemaphoreGive(s_lock);
    return true;
}
