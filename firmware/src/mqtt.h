#pragma once
// MQTT publisher with Home Assistant discovery. Broker URI from settings (mqtt://user:pass@host:port).
void mqtt_loop();             // call from loop(): connects, keeps alive, publishes every update
void mqtt_publish_state();    // publish now (called after each data update)
bool mqtt_connected();
