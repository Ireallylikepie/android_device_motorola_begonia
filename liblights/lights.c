/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#define LOG_TAG "BegoniaLights"

#include <cutils/log.h>
#include <cutils/properties.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/ioctl.h>
#include <sys/types.h>

#include <hardware/lights.h>

// taken from led-lm3530.h in kernel source, these are als modes
#define MANUAL          0
#define AUTOMATIC       1
#define MANUAL_SENSOR   2

/******************************************************************************/

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static struct light_state_t g_battery;
static struct light_state_t g_notification;
static int g_use_charging_light;

char const*const KEYBOARD_FILE = "/sys/class/leds/keyboard-backlight/brightness";
char const*const KEYBOARD_FILE1 = "/sys/class/leds/keyboard1-backlight/brightness";
char const*const KEYBOARD_FILE2 = "/sys/class/leds/keyboard2-backlight/brightness";
char const*const LCD_FILE = "/sys/class/leds/lcd-backlight/brightness";
char const*const ALS_FILE = "/sys/class/leds/lcd-backlight/als";
char const*const BUTTON_FILE = "/sys/class/leds/button-backlight/brightness";
char const*const AF_LED_FILE = "/sys/class/leds/af-led/brightness";
char const*const SPOTLIGHT = "/sys/class/leds/spotlight/brightness";


/* RGB file descriptors */
char const*const RED_LED_FILE = "/sys/class/leds/red/brightness";
char const*const RED_BLINK_FILE = "/sys/class/leds/red/blink";
char const*const GREEN_LED_FILE = "/sys/class/leds/green/brightness";
char const*const BLUE_LED_FILE = "/sys/class/leds/blue/brightness";

void init_globals(void)
{
    char prop[PROPERTY_VALUE_MAX];

    // init the mutex
    pthread_mutex_init(&g_lock, NULL);
    memset(&g_battery, 0, sizeof(g_battery));
    memset(&g_notification, 0, sizeof(g_notification));

    property_get("ro.battery_light", prop, "true");
    g_use_charging_light = strcmp(prop, "false") != 0;
}

static int
write_int(char const* path, int value)
{
    int fd;
    static int already_warned = 0;

    fd = open(path, O_RDWR);
    if (fd >= 0) {
        char buffer[20];
        int bytes = sprintf(buffer, "%d\n", value);
        int amt = write(fd, buffer, bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            LOGE("write_int failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }
}

static int
is_lit(struct light_state_t const* state)
{
    return state->color & 0x00ffffff;
}

static int
rgb_to_brightness(struct light_state_t const* state)
{
    int red = (state->color >> 16) & 0xff;
    int green = (state->color >> 8) & 0xff;
    int blue = state->color & 0xff;

    return ((77 * red) + (150 * green) + (29 * blue)) >> 8;
}

static int
set_light_backlight(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    int brightness = rgb_to_brightness(state);
    int als_mode;

    switch (state->brightnessMode) {
        case BRIGHTNESS_MODE_SENSOR:
            als_mode = AUTOMATIC;
            break;
        case BRIGHTNESS_MODE_USER:
        default:
            als_mode = MANUAL_SENSOR;
            break;
    }

    pthread_mutex_lock(&g_lock);
    err = write_int(ALS_FILE, als_mode);
    if (!err) {
        err = write_int(LCD_FILE, brightness);
    }
    pthread_mutex_unlock(&g_lock);

    return err;
}
static int
set_light_keyboard1(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    int on = is_lit(state);

    pthread_mutex_lock(&g_lock);
    err = write_int(KEYBOARD_FILE1, on ? 255:0);
    pthread_mutex_unlock(&g_lock);

    return err;
}
static int
set_light_keyboard2(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    int on = is_lit(state);

    pthread_mutex_lock(&g_lock);
    err = write_int(KEYBOARD_FILE2, on ? 255:0);
    pthread_mutex_unlock(&g_lock);

    return err;
}
static int
set_light_keyboard(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    int on = is_lit(state);

    pthread_mutex_lock(&g_lock);
    err = write_int(KEYBOARD_FILE, on ? 255:0);
    pthread_mutex_unlock(&g_lock);

    return err;
}

static int
set_light_buttons(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    //int brightness = rgb_to_brightness(state);
    int on = is_lit(state);
    pthread_mutex_lock(&g_lock);
    err = write_int(BUTTON_FILE, on ? 255:0);
    pthread_mutex_unlock(&g_lock);

    return err;

}

static int
set_light_locked(struct light_device_t *dev, struct light_state_t *state)
{
    int err = 0;
    int red, green, blue;

    red = (state->color >> 16) & 0xFF;
    green = (state->color >> 8) & 0xFF;
    blue = state->color & 0xFF;

    // ensure blinking is off
    err = write_int(RED_BLINK_FILE, 0);

    // set colors
    err = write_int(RED_LED_FILE, red);
    err = write_int(GREEN_LED_FILE, green);
    err = write_int(BLUE_LED_FILE, blue);

    // blink if supposed to
    if (state->flashMode != LIGHT_FLASH_NONE) {
        err = write_int(RED_BLINK_FILE, 255);
    }

    return err;
}

static int
handle_light_locked(struct light_device_t *dev)
{
    if (is_lit(&g_notification)) {
	return set_light_locked(dev, &g_notification);
    }

    return set_light_locked(dev, &g_battery);
}

static int
set_light_battery(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&g_lock);

    g_battery = *state;

    /* if green is set, it means the device is charging -> only
     * use it if the user wants it */
    if ((state->color & 0xff00) && !g_use_charging_light) {
        memset(&g_battery, 0, sizeof(g_battery));
    }

    handle_light_locked(dev);
    pthread_mutex_unlock(&g_lock);

    return 0;
}

static int
set_light_notification(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&g_lock);
    g_notification = *state;
    handle_light_locked(dev);
    pthread_mutex_unlock(&g_lock);

    return 0;
}

static int
set_light_attention(struct light_device_t* dev,
        struct light_state_t const* state)
{
    /* we don't have an attention light */
    return 0;
}

static int
close_lights(struct light_device_t *dev)
{
    if (dev) {
        free(dev);
    }
    return 0;
}


/******************************************************************************/
static int open_lights(const struct hw_module_t* module, char const* name,
        struct hw_device_t** device)
{
    int (*set_light)(struct light_device_t* dev,
            struct light_state_t const* state);
	
	//LOGD("open lights dev file %s\n", name);
	
    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name)) {
        set_light = set_light_backlight;
    }
    else if (0 == strcmp(LIGHT_ID_KEYBOARD, name)) {
        set_light = set_light_keyboard;
    }
    else if (0 == strcmp(LIGHT_ID_BUTTONS, name)) {
        set_light = set_light_buttons;
    }
    else if (0 == strcmp(LIGHT_ID_BATTERY, name)) {
        set_light = set_light_battery;
    }
    else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name)) {
        set_light = set_light_notification;
    }
    else if (0 == strcmp(LIGHT_ID_ATTENTION, name)) {
        set_light = set_light_attention;
    }
    else {
        return -EINVAL;
    }

    pthread_once(&g_init, init_globals);

    struct light_device_t *dev = malloc(sizeof(struct light_device_t));
    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    dev->common.close = (int (*)(struct hw_device_t*))close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t*)dev;
    return 0;
}


static struct hw_module_methods_t lights_module_methods = {
    .open =  open_lights,
};

const struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "begonia lights Module",
    .author = "Google, Inc.",
    .methods = &lights_module_methods,
};
