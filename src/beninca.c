#include "math.h"
#include "mgos.h"
#include "beninca.h"

struct timed_pin {
    mgos_timer_id timer;
    int pin;
};

static struct {
    struct timed_pin stop;
    struct timed_pin pp;
} _ctl;


enum click_kind {
    click_wtf = 0,
    click_opening,
    click_closing,
    click_stopped,
    _click_kind_count,
};

inline bool _within(double val, double nom, double tolerance) {
    if (fabs(val - nom) < (nom * tolerance)) return true;
    return false;
}

enum click_kind match_click(double click_time) {
    if (_within(click_time, BENINCA_OPENING_HALF_T, .1)) return click_opening;
    if (_within(click_time, BENINCA_CLOSING_HALF_T, .1)) return click_closing;
    if (click_time > (BENINCA_BLINKING_TIMEOUT/1000.0)) return click_stopped;
    return click_wtf;
}

/*
  sca contact  | gate            | sca pin input level
---------------|-----------------|---------------------
  open         | closed          | HIGH
  closed       | open            | LOW
  blinking     | moving          | .5Hz closing, 1Hz opening; 50% duty

*/

static struct beninca_status _status;
static mgos_timer_id _sca_hw_timer = MGOS_INVALID_TIMER_ID;
static mgos_timer_id _sca_moving_timer = MGOS_INVALID_TIMER_ID;
static beninca_status_cb_t _cb = NULL;


const char *beninca_dir_str(enum beninca_direction d) {
    switch (d) {
        case dir_unknown: return "unknown";
        case dir_open: return "open";
        case dir_close: return "close";
        default: return "_wtf_";
    }
}

static void _invoke_beninca_status_cb() {
    if (_cb) _cb(&_status);
}

static void _sca_timer_cb(void *arg) {
    _sca_moving_timer = MGOS_INVALID_TIMER_ID;
    _status.moving = false;
    _status.dir = _status.sca.is ? dir_close : dir_open;
    _status.since = mg_time();
    _invoke_beninca_status_cb();
}

static void _on_sca_change(void *arg) {
    bool new_state = (bool)arg;

    static unsigned total_clicks = 0;
    static unsigned click_buckets[_click_kind_count] = { 0 };

    if (_status.sca.is == new_state) {
        LOG(LL_WARN, ("repeated sca %d", _status.sca.is));
        if (_status.dir == dir_unknown) {
            _status.dir = _status.sca.is ? dir_close : dir_open;
        }
        return;
    }

    double event_time = mg_time();
    _status.sca.was = _status.sca.is;
    _status.sca.lasted = event_time - _status.sca.since;
    _status.sca.is = new_state;
    _status.sca.since = event_time;

    if (_sca_moving_timer != MGOS_INVALID_TIMER_ID) {
        mgos_clear_timer(_sca_moving_timer);
    }
    _sca_moving_timer = mgos_set_timer(BENINCA_BLINKING_TIMEOUT, 0,
                                       _sca_timer_cb, NULL);
    enum beninca_direction predicted_dir;
    if (!_status.moving) {
        _status.moving = true;
        _status.since = mg_time();
        total_clicks = 0;
        memset(&click_buckets, 0, sizeof(click_buckets));

        // predicted direction of motion, sometimes wrong
        switch (_status.dir) {
            case dir_open: predicted_dir = dir_close; break;
            case dir_close: predicted_dir = dir_open; break;
            default: predicted_dir = dir_unknown;
        }
        // maybe set naively?
        _status.dir = predicted_dir;
        LOG(LL_DEBUG, ("now moving, dir=%d", _status.dir));
    } else {
        // listen to the clicks
        total_clicks++;
        enum click_kind new_click_kind = match_click(_status.sca.lasted);
        click_buckets[new_click_kind]++;

        enum click_kind most_of = click_wtf;
        unsigned clicks_of = 0;
        for (unsigned i=0; i<_click_kind_count; i++) {
            if (click_buckets[i] > clicks_of) {
                clicks_of = click_buckets[i];
                most_of = i;
            }
        }
        LOG(LL_INFO, ("most of %d: %d, total %d", most_of, clicks_of, total_clicks));
        switch (most_of) {
            case click_closing:
            case click_opening: {
                if ((clicks_of * 2) >= total_clicks) {
                    _status.dir = (most_of == click_opening ? dir_open : dir_close);
                }
                break;
            }
            case click_wtf: {
                LOG(LL_WARN, ("SCA input makes no sense!"));
                break;
            }
            default: break;
        }
    }
    _status.time = mg_time();
    _invoke_beninca_status_cb();
}

IRAM static void _status_timer_cb(void *arg) {

    static int8_t last_state = -1;
    static uint8_t samples = 0xaa;

    samples <<= 1;
    samples |= (mgos_gpio_read(mgos_sys_config_get_beninca_sca()) ? 1 : 0);

    bool new_state;
    if (samples == 0xff) {
        new_state = 1;
    } else if (samples == 0x00) {
        new_state = 0;
    } else {
        // no stable state, quit
        return;
    }

    if (last_state != new_state) {
        last_state = new_state;
        mgos_invoke_cb(_on_sca_change, (void *)new_state, true);
    }

    (void) arg;
}

static void _timed_pin_cb(void *arg) {
    struct timed_pin *p = (struct timed_pin *)arg;
    p->timer = MGOS_INVALID_TIMER_ID;
    mgos_gpio_write(p->pin, false);
}

static void timed_pin_arm(struct timed_pin *p, int msecs) {
    if (p->timer != MGOS_INVALID_TIMER_ID) {
        mgos_clear_timer(p->timer);
    }
    p->timer = mgos_set_timer(msecs, 0, _timed_pin_cb, (void*)p);
}

static void timed_pin_disarm(struct timed_pin *p) {
    if (p->timer != MGOS_INVALID_TIMER_ID) {
        mgos_clear_timer(p->timer);
        p->timer = MGOS_INVALID_TIMER_ID;
    }
}

static bool timed_pin_is_armed(const struct timed_pin *p) {
    return p->timer != MGOS_INVALID_TIMER_ID;
}

void beninca_stop() {
    mgos_gpio_write(_ctl.stop.pin, true);
    if (!_status.control.stop_hold) {
        timed_pin_arm(&_ctl.stop, BENINCA_PUSH_DURATION);
    }
    _status.control.stop_strobe = 1;
    _status.time = mg_time();
    _invoke_beninca_status_cb();
    _status.control.stop_strobe = 0;
}

void beninca_stop_hold() {
    if (!_status.control.stop_hold) {
        _status.control.stop_hold = true;
        _status.time = mg_time();
        _invoke_beninca_status_cb();

    }
    mgos_gpio_write(_ctl.stop.pin, true);
    timed_pin_disarm(&_ctl.stop);
}

void beninca_stop_release() {
    if (_status.control.stop_hold) {
        _status.control.stop_hold = 0;
        _status.time = mg_time();
        _invoke_beninca_status_cb();
    }
    if (!timed_pin_is_armed(&_ctl.stop)) {
        mgos_gpio_write(_ctl.stop.pin, false);
    }
}

void beninca_pp() {
    mgos_gpio_write(_ctl.pp.pin, true);
    timed_pin_arm(&_ctl.pp, BENINCA_PUSH_DURATION);
    _status.control.pp_strobe = 1;
    _status.time = mg_time();
    _invoke_beninca_status_cb();
    _status.control.pp_strobe = 0;
}

void time_change_cb(void *arg, double delta) {
    _status.sca.since += delta;

    (void) arg;
}

void beninca_init(beninca_status_cb_t cb) {
    _cb = cb;

    memset(&_ctl, 0, sizeof(_ctl));
    memset(&_status, 0, sizeof(_status));
    _status.sca.is = true;

    _ctl.stop.pin = mgos_sys_config_get_beninca_stop();
    _ctl.pp.pin = mgos_sys_config_get_beninca_pp();

    mgos_sntp_add_time_change_cb(time_change_cb, NULL);

    mgos_gpio_write(mgos_sys_config_get_beninca_stop(), false);
    mgos_gpio_set_mode(mgos_sys_config_get_beninca_stop(), MGOS_GPIO_MODE_OUTPUT);

    mgos_gpio_write(mgos_sys_config_get_beninca_pp(), false);
    mgos_gpio_set_mode(mgos_sys_config_get_beninca_pp(), MGOS_GPIO_MODE_OUTPUT);

    mgos_gpio_set_pull(mgos_sys_config_get_beninca_sca(), MGOS_GPIO_PULL_UP);
    mgos_gpio_set_mode(mgos_sys_config_get_beninca_sca(), MGOS_GPIO_MODE_INPUT);

    _sca_hw_timer = mgos_set_hw_timer(BENINCA_SCA_SAMPLE_T, 1, _status_timer_cb, NULL);
}

void beninca_deinit() {
    if (_sca_hw_timer != MGOS_INVALID_TIMER_ID) {
        mgos_clear_timer(_sca_hw_timer);
        _sca_hw_timer = MGOS_INVALID_TIMER_ID;
    }
}
