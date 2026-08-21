#include "CommunicatorAdminCodec.h"

#include <helpers/AdvertDataHelpers.h>
#include <string.h>
#include <stdio.h>

namespace CompactAdminCodec {
namespace {

const char* kContactPrefix = "meshcore://contact/add?";
const char* kChannelPrefix = "meshcore://channel/add?";

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseHex(const char* text, size_t chars, uint8_t* out, size_t bytes) {
  if (!text || strlen(text) != chars || chars != bytes * 2) return false;
  for (size_t i = 0; i < bytes; ++i) {
    int hi = hexNibble(text[i * 2]);
    int lo = hexNibble(text[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

void toHex(const uint8_t* bytes, size_t count, char* out) {
  static const char* h = "0123456789abcdef";
  for (size_t i = 0; i < count; ++i) {
    out[i * 2] = h[bytes[i] >> 4];
    out[i * 2 + 1] = h[bytes[i] & 15];
  }
  out[count * 2] = 0;
}

bool urlDecode(const char* src, size_t len, char* out, size_t out_len) {
  if (!out_len) return false;
  size_t w = 0;
  for (size_t i = 0; i < len; ++i) {
    char c = src[i];
    if (c == '+') c = ' ';
    else if (c == '%' && i + 2 < len) {
      int hi = hexNibble(src[i + 1]);
      int lo = hexNibble(src[i + 2]);
      if (hi < 0 || lo < 0) return false;
      c = (char)((hi << 4) | lo);
      i += 2;
    }
    if (w + 1 >= out_len) return false;
    out[w++] = c;
  }
  out[w] = 0;
  return true;
}

bool urlEncode(const char* src, char* out, size_t out_len) {
  static const char* h = "0123456789ABCDEF";
  size_t w = 0;
  for (size_t i = 0; src && src[i]; ++i) {
    uint8_t c = (uint8_t)src[i];
    bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      if (w + 1 >= out_len) return false;
      out[w++] = (char)c;
    } else {
      if (w + 3 >= out_len) return false;
      out[w++] = '%';
      out[w++] = h[c >> 4];
      out[w++] = h[c & 15];
    }
  }
  if (w >= out_len) return false;
  out[w] = 0;
  return true;
}

bool queryValue(const char* query, const char* wanted, char* out, size_t out_len) {
  if (!query || !wanted) return false;
  size_t wanted_len = strlen(wanted);
  const char* p = query;
  while (*p) {
    const char* end = strchr(p, '&');
    if (!end) end = p + strlen(p);
    const char* eq = (const char*)memchr(p, '=', end - p);
    if (eq && (size_t)(eq - p) == wanted_len && strncmp(p, wanted, wanted_len) == 0) {
      return urlDecode(eq + 1, end - (eq + 1), out, out_len);
    }
    p = *end ? end + 1 : end;
  }
  return false;
}

void setError(char* error, size_t len, const char* text) {
  if (!error || !len) return;
  StrHelper::strncpy(error, text ? text : "Invalid input", len);
}

bool validContactType(int type) {
  return type == ADV_TYPE_CHAT || type == ADV_TYPE_REPEATER ||
         type == ADV_TYPE_ROOM || type == ADV_TYPE_SENSOR;
}

} // namespace

bool parseContactInput(const char* input, const char* fallback_name, ContactInfo& out,
                       char* error, size_t error_len) {
  memset(&out, 0, sizeof(out));
  if (!input || !input[0]) { setError(error, error_len, "Enter a public key or contact link"); return false; }

  char name[32] = {0};
  char key_hex[65] = {0};
  int type = ADV_TYPE_CHAT;

  if (strncmp(input, kContactPrefix, strlen(kContactPrefix)) == 0) {
    const char* query = input + strlen(kContactPrefix);
    char type_buf[8] = {0};
    if (!queryValue(query, "public_key", key_hex, sizeof(key_hex))) {
      setError(error, error_len, "Contact link is missing public_key"); return false;
    }
    if (!queryValue(query, "name", name, sizeof(name))) {
      if (fallback_name) StrHelper::strncpy(name, fallback_name, sizeof(name));
    }
    if (queryValue(query, "type", type_buf, sizeof(type_buf))) type = atoi(type_buf);
    if (!validContactType(type)) { setError(error, error_len, "Unsupported contact type"); return false; }
  } else {
    if (strlen(input) != 64) { setError(error, error_len, "Key needs 64 hex digits or a contact URI"); return false; }
    StrHelper::strncpy(key_hex, input, sizeof(key_hex));
    if (fallback_name) StrHelper::strncpy(name, fallback_name, sizeof(name));
  }

  if (!name[0]) { setError(error, error_len, "A raw public key also needs a name"); return false; }
  uint8_t key[32];
  if (!parseHex(key_hex, 64, key, sizeof(key))) { setError(error, error_len, "Invalid contact public key"); return false; }

  out.id = mesh::Identity(key);
  StrHelper::strncpy(out.name, name, sizeof(out.name));
  out.type = (uint8_t)type;
  out.flags = 0;
  out.out_path_len = OUT_PATH_UNKNOWN;
  out.shared_secret_valid = false;
  out.last_advert_timestamp = 0;
  out.lastmod = 0;
  out.gps_lat = 0;
  out.gps_lon = 0;
  out.sync_since = 0;
  return true;
}

bool parseChannelUri(const char* input, ChannelDetails& out, char* error, size_t error_len) {
  memset(&out, 0, sizeof(out));
  if (!input || strncmp(input, kChannelPrefix, strlen(kChannelPrefix)) != 0) {
    setError(error, error_len, "Use a meshcore://channel/add link"); return false;
  }

  const char* query = input + strlen(kChannelPrefix);
  char name[32] = {0};
  char secret_hex[33] = {0};
  if (!queryValue(query, "name", name, sizeof(name)) || !name[0]) {
    setError(error, error_len, "Group link is missing name"); return false;
  }
  if (!queryValue(query, "secret", secret_hex, sizeof(secret_hex))) {
    setError(error, error_len, "Group link is missing secret"); return false;
  }
  if (!parseHex(secret_hex, 32, out.channel.secret, 16)) {
    setError(error, error_len, "Group secret needs 32 hex digits"); return false;
  }
  memset(&out.channel.secret[16], 0, 16);
  StrHelper::strncpy(out.name, name, sizeof(out.name));
  return true;
}

bool makeContactUri(const char* name, const uint8_t pub_key[32], uint8_t type,
                    char* out, size_t out_len) {
  char encoded[128];
  char key_hex[65];
  if (!urlEncode(name ? name : "", encoded, sizeof(encoded))) return false;
  toHex(pub_key, 32, key_hex);
  int n = snprintf(out, out_len, "%sname=%s&public_key=%s&type=%u",
                   kContactPrefix, encoded, key_hex, type);
  return n > 0 && (size_t)n < out_len;
}

bool isCommunicatorCompatibleChannel(const ChannelDetails& channel) {
  for (int i = 16; i < 32; ++i) if (channel.channel.secret[i]) return false;
  for (int i = 0; i < 16; ++i) if (channel.channel.secret[i]) return true;
  return false;
}

bool makeChannelUri(const ChannelDetails& channel, char* out, size_t out_len) {
  if (!channel.name[0] || !isCommunicatorCompatibleChannel(channel)) return false;
  char encoded[128];
  char secret_hex[33];
  if (!urlEncode(channel.name, encoded, sizeof(encoded))) return false;
  toHex(channel.channel.secret, 16, secret_hex);
  int n = snprintf(out, out_len, "%sname=%s&secret=%s", kChannelPrefix, encoded, secret_hex);
  return n > 0 && (size_t)n < out_len;
}

} // namespace CompactAdminCodec
