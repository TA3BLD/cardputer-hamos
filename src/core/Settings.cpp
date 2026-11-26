#include "Settings.h"
#include "AprsIs.h"
#include "Ble.h"
#include "Config.h"
#include "Lang.h"
#include "LoraAprs.h"
#include "Mesh.h"
#include "Net.h"
#include "Rig.h"
#include "Weather.h"
#include "Wsjtx.h"

namespace settings {

void apply(int what) {
    if (what & WIFI) net::begin();
    if (what & MESH) {
        mesh::begin();
        loraaprs::begin();
        if (config::get("mesh_ble", "0") == "1") ble::meshStart();
        else ble::meshStop();
    }
    if (what & APRS) aprsis::begin();
    if (what & RIG) {
        rig::begin();
        weather::begin();
    }
    if (what & FT8) wsjtxlink::begin();
    if (what & LANG) lang::reload();
}

}  // namespace settings
