#include "mgos.h"
#include "mgos_mqtt.h"
#include "beninca.h"


static struct beninca_status last_status;
static bool uplink_ok = false;
static double uplink_since = 0;

const char * _topic(const char *t) {
    static char topic[256];
    snprintf(topic, sizeof(topic), "%s/%s", mgos_sys_config_get_device_id(), t);
    return topic;
}

static uint32_t btn_count = 0;
void _on_button(int pin, void *arg) {
    LOG(LL_INFO, ("button event on pin %d: %d",
                  pin, mgos_gpio_read(pin)));
    btn_count++;
    (void) pin;
    (void) arg;
}

void publish_lock() {
    char *s = last_status.control.stop_hold ? "ON" : "OFF";
    mgos_mqtt_pub(_topic("beninca/lock"), s, strlen(s),
                  1 /*qos*/, true /*retain*/);
}

void publish_status() {

    // TODO: remember when publishing fails and re-pubhish on reconnect
    // TODO: observe connected status and num unsent bytes

    struct beninca_status *status = &last_status;
    struct mbuf buf;
    mbuf_init(&buf, 50);
    struct json_out msg = JSON_OUT_MBUF(&buf);
    json_printf(&msg,
            "{moving:%B,since:%.2f,dir:%Q,time:%.2f,"
            "ctl:{stop_hold:%B,stop:%B,pp:%B},"
            "sca:{is:%B,since:%.2f,was:%B,lasted:%.2f},btn:%u}",
            status->moving, status->since, beninca_dir_str(status->dir), status->time,
            status->control.stop_hold, status->control.stop_strobe, status->control.pp_strobe,
            status->sca.is, status->sca.since, status->sca.was, status->sca.lasted,
            btn_count);
    LOG(LL_INFO, ("status: %.*s", buf.len, buf.buf));
    if (!mgos_mqtt_pub(_topic("beninca/status"), buf.buf, buf.len, 1 /*qos*/, true /*retain*/)) {
        LOG(LL_INFO, ("error publishing!"));
    }
    mbuf_free(&buf);

    publish_lock();
}

void _on_command(struct mg_connection *nc, const char *_topic_str,
              int _topic_len, const char *msg, int msg_len,
              void *ud) {

    bool ret = false;
    char *cmd = NULL;
    int req_id = -1;
    json_scanf(msg, msg_len, "{cmd:%Q,req_id:%d}", &cmd, &req_id);
    if (cmd == NULL) {
        LOG(LL_INFO, ("no cmd in %.*s", msg_len, msg));
        return;
    }

    if (strcmp(cmd, "stop_push") == 0) {
        beninca_stop();
        ret = true;
    } else if (strcmp(cmd, "stop_hold") == 0) {
        beninca_stop_hold();
        ret = true;
    } else if (strcmp(cmd, "stop_release") == 0) {
        beninca_stop_release();
        ret = true;
    } else if (strcmp(cmd, "pp_push") == 0) {
        beninca_pp();
        ret = true;
    } else if (strcmp(cmd, "get_status") == 0) {
        publish_status();
        ret = true;
    } else {
        LOG(LL_WARN, ("unknown command: %s", cmd));
    }

    struct mbuf buf;
    mbuf_init(&buf, 50);
    struct json_out out = JSON_OUT_MBUF(&buf);
    json_printf(&out, "{req_id:%d,cmd:%Q,resp:%B}", req_id, cmd, ret);
    mgos_mqtt_pub(_topic("beninca/response"), buf.buf, buf.len,
                  1 /*qos*/, false /*retain*/);

    free((void *)cmd);
    mbuf_free(&buf);

    (void) nc;
    (void) _topic_str;
    (void) _topic_len;
}

void _on_lock(struct mg_connection *nc, const char *_topic,
              int _topic_len, const char *msg, int msg_len,
              void *ud) {
    bool was_locked = last_status.control.stop_hold;
    int is_locked = -1;
    if (msg_len >= 2 && strncasecmp(msg, "on", 2) == 0) {
        is_locked = 1;
        beninca_stop_hold();
    } else if (msg_len >= 3 && strncasecmp(msg, "off", 3) == 0) {
        is_locked = 0;
        beninca_stop_release();
    }

    if (is_locked < 0 || is_locked == was_locked) publish_lock();

    (void) nc;
    (void) _topic;
    (void) _topic_len;
}
void _beninca_status_cb(const struct beninca_status *status) {
    last_status = *status;
    publish_status();
}

bool update_cb(enum mgos_upd_event ev, const void *ev_arg, void *cb_arg) {
    switch (ev) {
        case MGOS_UPD_EV_INIT: {
            return true;
        }
        case MGOS_UPD_EV_BEGIN: {
            const struct mgos_upd_info *info = (const struct mgos_upd_info *) ev_arg;
            LOG(LL_INFO, ("BEGIN %.*s", (int) info->build_id.len, info->build_id.ptr));
            return true;
        }
        case MGOS_UPD_EV_END: {
            int result = *((int *) ev_arg);
            LOG(LL_INFO, ("END, result %d", result));
            if (result) beninca_deinit();
            break;
        }
        default:
            break;
    }
    (void) cb_arg;
    return false;
}
void _mqtt_net_ev(struct mg_connection *nc, int ev,
                    void *ev_data MG_UD_ARG(void *user_data)) {

    switch (ev) {
        case MG_EV_MQTT_CONNACK:
            uplink_ok = true;
            uplink_since = mg_time();
            // push update
            break;
        case MG_EV_CLOSE:
            uplink_ok = false;
            uplink_since = mg_time();
            break;
        default:
            break;
    }

    (void) nc;
    (void) user_data;
}


enum mgos_app_init_result mgos_app_init(void) {
    memset(&last_status, 0, sizeof(last_status));
    // LED and user button
    mgos_gpio_set_mode(mgos_sys_config_get_pinmux_led(), MGOS_GPIO_MODE_OUTPUT);
    mgos_gpio_set_button_handler(mgos_sys_config_get_pinmux_button(),
        MGOS_GPIO_PULL_NONE, MGOS_GPIO_INT_EDGE_NEG, 200 /*debounce_ms*/,
        _on_button, NULL);

    beninca_init(_beninca_status_cb);

    // MQTT
    mgos_mqtt_add_global_handler(_mqtt_net_ev, NULL);
    mgos_mqtt_sub(_topic("beninca/command"), _on_command, NULL);
    mgos_mqtt_sub(_topic("beninca/lock/command"), _on_lock, NULL);



    // Stop timer on update
    mgos_upd_set_event_cb(update_cb, NULL);
    return MGOS_APP_INIT_SUCCESS;
}
