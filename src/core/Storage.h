// SD card access. HamOS still boots without a card; settings just don't persist.
#pragma once

namespace storage {
bool begin();
bool ready();
}
