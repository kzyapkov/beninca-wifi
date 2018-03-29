import asyncio
import logging
import json

import voluptuous as vol

from homeassistant.core import callback
from homeassistant.const import (CONF_NAME, CONF_VALUE_TEMPLATE, CONF_COVERS)
from homeassistant.helpers import config_validation as cv
from homeassistant.components.cover import (
    CoverDevice, SUPPORT_OPEN, SUPPORT_CLOSE, SUPPORT_STOP)

from homeassistant.components import mqtt

_LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ['mqtt']

CONF_DEVICE_ID = 'device_id'

PLATFORM_SCHEMA = mqtt.MQTT_BASE_PLATFORM_SCHEMA.extend({
    vol.Required(CONF_DEVICE_ID): cv.string,
    vol.Optional(CONF_NAME, default="Beninca CORE"): cv.string,
})

@asyncio.coroutine
def async_setup_platform(hass, config, async_add_devices, discovery_info=None):
    """Set up the MQTT switch."""
    if discovery_info is not None:
        config = PLATFORM_SCHEMA(discovery_info)

    async_add_devices([BenincaGate(
        config.get(CONF_NAME),
        config.get(CONF_DEVICE_ID),
    )])

class BenincaGate(CoverDevice):
    def __init__(self, name, device_id):
        self._name = name
        self._device_id = device_id
        self._status = {}
        self._command_topic = "{}/beninca/command".format(device_id)
        self._status_topic = "{}/beninca/status".format(device_id)
    
    @property
    def name(self):
        return self._name

    @asyncio.coroutine
    def async_added_to_hass(self):
        @callback
        def on_status(topic, payload, qos):
            _LOGGER.debug("got ({}) {}: {}".format(qos, topic, payload))
            try:
                self._status = json.loads(payload)
                self.async_schedule_update_ha_state()
            except Exception as e:
                _LOGGER.exception("bad incoming status")
                
        yield from mqtt.async_subscribe(
                self.hass, self._status_topic, on_status, 1)

    @property
    def should_poll(self):
        return False

    @property
    def available(self) -> bool:
        return self._status # and self._status['time'] not long ago

    @property
    def supported_features(self):
        return (SUPPORT_OPEN | SUPPORT_CLOSE | SUPPORT_STOP)

    @property
    def is_opening(self):
        return self._status['moving'] == True and self._status['dir'] == 'open'

    @property
    def is_closing(self):
        return self._status['moving'] == True and self._status['dir'] == 'close'

    @property
    def is_open(self):
        return self._status['moving'] == False and self._status['dir'] == 'open'

    @property
    def is_closed(self):
        return self._status['moving'] == False and self._status['dir'] == 'close'

    @asyncio.coroutine
    def async_open_cover(self, **kwargs):
        if self.is_open or self.is_opening:
            return
        msg = {"cmd": "pp_push"}
        mqtt.async_publish(
            self.hass, self._command_topic, json.dumps(msg), 2, False)

    @asyncio.coroutine
    def async_close_cover(self, **kwargs):
        if self.is_closed or self.is_closing:
            return
        msg = {"cmd": "pp_push"}
        mqtt.async_publish(
            self.hass, self._command_topic, json.dumps(msg), 2, False)

    @asyncio.coroutine
    def async_stop_cover(self, **kwargs):
        msg = {"cmd": "stop_push"}
        mqtt.async_publish(
            self.hass, self._command_topic, json.dumps(msg), 2, False)
