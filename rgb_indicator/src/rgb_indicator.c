/*
 * Custom per-key RGB layer indicator for the splitkb Aurora Corne (ZMK).
 *
 * Drives the per-key SK6812 chain directly via the Zephyr led_strip API, so
 * ZMK's own RGB underglow driver must be disabled (one writer only).
 *
 * Two modes (Kconfig):
 *   RGB_INDICATOR_MAP_MODE = y : walking-dot chain mapper (one-time, to learn
 *                                the physical index -> key order).
 *   RGB_INDICATOR_MAP_MODE = n : real layer indicator.
 *
 * SPLIT NOTE: ZMK does not sync layer state to the peripheral, so only the
 * central (left) half knows the active layer. The same code on the peripheral
 * sees "no layer active" and stays dark -- i.e. this lights the LEFT half only
 * until/unless we add a central->peripheral layer-state sync.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(rgb_indicator, LOG_LEVEL_INF);

#define STRIP_NODE       DT_NODELABEL(led_strip)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_NODE, chain_length)

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[STRIP_NUM_PIXELS];

/* Moderate brightness keeps current (and glare) down. */
static const struct led_rgb RED   = {.r = 0x40, .g = 0x00, .b = 0x00};
static const struct led_rgb BLUE  = {.r = 0x00, .g = 0x00, .b = 0x40};
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
    /* index 0 lights BLUE as a cycle anchor; all others RED. */
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

/* ----- Real layer indicator (CENTRAL / left half only) -------------------- *
 * The keymap subsystem and layer events only exist on the central. The
 * peripheral has no zmk_keymap_* symbols, so its branch (below) must not
 * reference them -- it just stays dark.
 */

/* Keymap layer index for the CONFIG layer (see splitkb_aurora_corne.keymap). */
#define CONF_LAYER 8

/*
 * Physical LED chain index -> ZMK key position (LEFT half), captured from the
 * walking-dot mapper. Underglow LEDs are bypassed (JP2), so index 0 is the
 * first per-key LED. Chain order: 3 thumbs, then each row inner->outer, with
 * the two outer-column LEDs trailing at the end.
 */
static const uint8_t led_to_pos[STRIP_NUM_PIXELS] = {
    38, 37, 36,                 /* thumbs: Space, Bspc, Cmd */
    29, 28, 27, 26, 25,         /* bottom: V D C X Z */
    17, 16, 15, 14, 13,         /* home:   G T S R A */
    5,  4,  3,  2,  1,  0,      /* top:    B P F W Q ESC */
    12,                         /* TAB  (home outer) */
    24,                         /* LSHFT (bottom outer) */
};

/* Key positions with a real (non-transparent) binding on the CONFIG layer.
 * Right-half positions (6, 11, 40, 41) are listed for completeness but the
 * left strip has no LEDs for them, so they simply never match here. */
static const uint8_t conf_active[] = {
    0,                          /* left bootloader */
    12, 13, 14, 15, 16,         /* BT_CLR, BT0..BT3 */
    24, 25, 26, 27,             /* BT_NXT, BT_PRV, OUT_USB, OUT_BLE */
    6, 11, 40, 41,              /* right half (no left LEDs map to these) */
};

static bool pos_is_active(uint8_t pos) {
    for (size_t i = 0; i < ARRAY_SIZE(conf_active); i++) {
        if (conf_active[i] == pos) {
            return true;
        }
    }
    return false;
}

static void update_leds(void) {
    bool conf_on = zmk_keymap_layer_active(zmk_keymap_layer_index_to_id(CONF_LAYER));
    fill(BLACK);
    if (conf_on) {
        for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
            if (pos_is_active(led_to_pos[i])) {
                pixels[i] = RED;
            }
        }
    }
    led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
}

static int layer_listener(const zmk_event_t *eh) {
    update_leds();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(rgb_indicator, layer_listener);
ZMK_SUBSCRIPTION(rgb_indicator, zmk_layer_state_changed);

static int rgb_indicator_init(void) {
    if (!device_is_ready(strip)) {
        LOG_ERR("led_strip not ready");
        return -ENODEV;
    }
    update_leds(); /* start dark */
    return 0;
}

SYS_INIT(rgb_indicator_init, APPLICATION, 99);

#else

/* ----- Peripheral (right half): no keymap/layer state available ----------- *
 * Just hold the strip dark. (Lighting the right half on layer changes would
 * require syncing layer state central->peripheral, a future enhancement.)
 */
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
