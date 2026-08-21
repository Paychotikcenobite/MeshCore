#pragma once

#include <Arduino.h>
#include <helpers/ContactInfo.h>
#include <helpers/ChannelDetails.h>

namespace CompactAdminCodec {

bool parseContactInput(const char* input, const char* fallback_name, ContactInfo& out,
                       char* error, size_t error_len);
bool parseChannelUri(const char* input, ChannelDetails& out, char* error, size_t error_len);
bool makeContactUri(const char* name, const uint8_t pub_key[32], uint8_t type,
                    char* out, size_t out_len);
bool makeChannelUri(const ChannelDetails& channel, char* out, size_t out_len);
bool isCommunicatorCompatibleChannel(const ChannelDetails& channel);

} // namespace CompactAdminCodec
