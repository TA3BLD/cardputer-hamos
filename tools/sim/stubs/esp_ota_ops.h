#pragma once
#include <cstring>
// Two app slots; the simulator runs from slot 0, slot 1 holds the "old" version.
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
struct esp_partition_t { int slot; };
struct esp_app_desc_t { char version[32]; };
inline esp_partition_t sim_slots[2] = {{0}, {1}};
inline int sim_boot_slot = 0;
inline bool sim_other_slot_valid = true;
inline const esp_partition_t* esp_ota_get_running_partition() { return &sim_slots[0]; }
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return &sim_slots[1]; }
inline esp_err_t esp_ota_get_partition_description(const esp_partition_t* p, esp_app_desc_t* d) {
    if (p->slot == 1 && !sim_other_slot_valid) return ESP_FAIL;
    strcpy(d->version, p->slot == 0 ? "current" : "previous");
    return ESP_OK;
}
inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t* p) { sim_boot_slot = p->slot; return ESP_OK; }
