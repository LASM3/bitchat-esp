#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

esp_err_t bitchat_peer_init();
size_t bitchat_make_announcement(uint8_t* output, size_t capacity);
void bitchat_process_packet(const uint8_t* data, size_t length);
