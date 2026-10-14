#ifndef REMOTE_BLE_H
#define REMOTE_BLE_H
void remote_ble_start(int wifi_role);
int remote_sender_wifi_ready(void);
/* Returns 0 queued, 1 offline, 2 busy, 3 invalid. Never does HTTP. */
int remote_sender_submit_ble(char action);
#endif
