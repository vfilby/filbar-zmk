/*
 * Custom per-key RGB layer indicator for the splitkb Aurora Corne (ZMK).
 *
 * Drives the per-key SK6812 chain directly via the Zephyr led_strip API, so
 * ZMK's own RGB underglow driver must be disabled (one writer only).
 *
 * Modes (Kconfig):
 *   RGB_INDICATOR_MAP_MODE = y : walking-dot chain mapper (one-time).
 *   RGB_INDICATOR_MAP_MODE = n : real layer indicator.
 *
 * SPLIT: LEFT (central) only. ZMK doesn't sync layer state to the peripheral,
 * and pushing it over the split BLE link on every layer change congested the
 * link and hurt typing -- so the right half is intentionally left dark.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rgb_indicator, LOG_LEVEL_INF);

#define STRIP_NODE       DT_NODELABEL(led_strip)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_NODE, chain_length)

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[STRIP_NUM_PIXELS];

/* Moderate brightness keeps current (and glare) down. */
static const struct led_rgb RED   = {.r = 0x40, .g = 0x00, .b = 0x00};
static const struct led_rgb BLUE  = {.r = 0x00, .g = 0x00, .b = 0x40};
static const struct led_rgb WHITE = {.r = 0x30, .g = 0x30, .b = 0x30};
static const struct led_rgb BLACK = {.r = 0x00, .g = 0x00, .b = 0x00};

static void fill(struct led_rgb c) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = c;
    }
}

#if IS_ENABLED(CONFIG_RGB_INDICATOR_MAP_MODE)

/* ----- Mapping mode: walking dot ------------------------------------------ */

static uint8_t walk_idx;
static struct k_work_delayable walk_work;

static void walk_step(struct k_work *work) {
    fill(BLACK);
    pixels[walk_idx] = (walk_idx == 0) ? BLUE : RED;
    led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
    LOG_INF("rgb map: lit index %d / %d", walk_idx, STRIP_NUM_PIXELS - 1);
    walk_idx = (walk_idx + 1) % STRIP_NUM_PIXELS;
    k_work_reschedule(&walk_work, K_MSEC(1500));
}

static int rgb_indicator_init(void) {
    if (!device_is_ready(strip)) {
        LOG_ERR("led_strip not ready");
        return -ENODEV;
    }
    k_work_init_delayable(&walk_work, walk_step);
    k_work_reschedule(&walk_work, K_SECONDS(2));
    return 0;
}

SYS_INIT(rgb_indicator_init, APPLICATION, 99);

#elif IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

/* ----- Real layer indicator (CENTRAL / left half only) -------------------- */

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

/* Keymap layer indices (see splitkb_aurora_corne.keymap). */
#define BASE_LAYER 0
#define NAV_LAYER  3
#define CONF_LAYER 8

/*
 * Physical LED chain index -> ZMK key position (LEFT half), from the walking-
 * dot mapper. Underglow LEDs are bypassed (JP2), so index 0 is the first
 * per-key LED. Chain order: 3 thumbs, each row inner->outer, then the two
 * outer-column LEDs.
 */
static const uint8_t led_to_pos[STRIP_NUM_PIXELS] = {
    38, 37, 36,                 /* thumbs: Space, Bspc, Cmd */
    29, 28, 27, 26, 25,         /* bottom: V D C X Z */
    17, 16, 15, 14, 13,         /* home:   G T S R A */
    5,  4,  3,  2,  1,  0,      /* top:    B P F W Q ESC */
    12,                         /* TAB  (home outer) */
    24,                         /* LSHFT (bottom outer) */
};

static bool in_set(uint8_t pos, const uint8_t *set, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (set[i] == pos) {
            return true;
        }
    }
    return false;
}

/* CONFIG layer: keys that do something -> red. */
static const uint8_t conf_active[] = {
    0,                          /* bootloader */
    12, 13, 14, 15, 16,         /* BT_CLR, BT0..BT3 */
    24, 25, 26, 27,             /* BT_NXT, BT_PRV, OUT_USB, OUT_BLE */
};

/* NAV layer: left-hand modifier column -> blue. (Arrows/etc. are on the right
 * half, which stays dark.) */
static const uint8_t nav_blue[] = {
    13, 14, 15, 16,             /* GUI/ALT/SFT/CTL */
};

static struct led_rgb color_for(uint8_t layer, uint8_t pos) {
    switch (layer) {
    case CONF_LAYER:
        return in_set(pos, conf_active, ARRAY_SIZE(conf_active)) ? RED : BLACK;
    case NAV_LAYER:
        return in_set(pos, nav_blue, ARRAY_SIZE(nav_blue)) ? BLUE : BLACK;
    default:
        return BLACK;
    }
}

static void update_strip(uint8_t layer) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = color_for(layer, led_to_pos[i]);
    }
    led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
}

static int layer_listener(const zmk_event_t *eh) {
    update_strip(zmk_keymap_highest_layer_active());
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(rgb_indicator, layer_listener);
ZMK_SUBSCRIPTION(rgb_indicator, zmk_layer_state_changed);

static int rgb_indicator_init(void) {
    if (!device_is_ready(strip)) {
        LOG_ERR("led_strip not ready");
        return -ENODEV;
    }
    update_strip(BASE_LAYER); /* start dark */
    return 0;
}

SYS_INIT(rgb_indicator_init, APPLICATION, 99);

#else

/* ----- Peripheral (right half): stay dark --------------------------------- */

static int rgb_indicator_init(void) {
    if (!device_is_ready(strip)) {
        LOG_ERR("led_strip not ready");
        return -ENODEV;
    }
    fill(BLACK);
    led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
    return 0;
}

SYS_INIT(rgb_indicator_init, APPLICATION, 99);

#endif /* mode selection */
