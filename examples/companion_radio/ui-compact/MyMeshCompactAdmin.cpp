#include "../MyMesh.h"

#ifdef MESHCORE_COMPACT_UI

#include <string.h>

namespace {

bool readPersistedContact(DataStore* store, const uint8_t pub_key[32], ContactInfo& out, bool& found) {
  found = false;
  File file = store->openRead("/contacts3");
  if (!file) return false;

  while (file.available()) {
    ContactInfo c{};
    uint8_t key[32];
    uint8_t unused = 0;
    bool ok = file.read(key, 32) == 32;
    ok = ok && file.read((uint8_t*)c.name, 32) == 32;
    ok = ok && file.read(&c.type, 1) == 1;
    ok = ok && file.read(&c.flags, 1) == 1;
    ok = ok && file.read(&unused, 1) == 1;
    ok = ok && file.read((uint8_t*)&c.sync_since, 4) == 4;
    ok = ok && file.read((uint8_t*)&c.out_path_len, 1) == 1;
    ok = ok && file.read((uint8_t*)&c.last_advert_timestamp, 4) == 4;
    ok = ok && file.read(c.out_path, MAX_PATH_SIZE) == MAX_PATH_SIZE;
    ok = ok && file.read((uint8_t*)&c.lastmod, 4) == 4;
    ok = ok && file.read((uint8_t*)&c.gps_lat, 4) == 4;
    ok = ok && file.read((uint8_t*)&c.gps_lon, 4) == 4;
    if (!ok) {
      file.close();
      return false;
    }
    if (memcmp(key, pub_key, 32) == 0) {
      c.id = mesh::Identity(key);
      c.name[sizeof(c.name) - 1] = 0;
      out = c;
      found = true;
      file.close();
      return true;
    }
  }

  file.close();
  return true;
}

bool readPersistedChannel(DataStore* store, uint8_t channel_idx, ChannelDetails& out) {
  File file = store->openRead("/channels2");
  if (!file) return false;

  const size_t record_size = 4 + sizeof(out.name) + sizeof(out.channel.secret);
  const size_t offset = (size_t)channel_idx * record_size;
  if (!file.seek(offset)) {
    file.close();
    return false;
  }

  uint8_t unused[4];
  bool ok = file.read(unused, sizeof(unused)) == sizeof(unused);
  ok = ok && file.read((uint8_t*)out.name, sizeof(out.name)) == sizeof(out.name);
  ok = ok && file.read(out.channel.secret, sizeof(out.channel.secret)) == sizeof(out.channel.secret);
  file.close();
  if (!ok) return false;
  out.name[sizeof(out.name) - 1] = 0;
  return true;
}

bool channelIsEmpty(const ChannelDetails& ch) {
  if (ch.name[0]) return false;
  for (size_t i = 0; i < sizeof(ch.channel.secret); ++i) {
    if (ch.channel.secret[i]) return false;
  }
  return true;
}

} // namespace

bool MyMesh::compactUpsertContactVerified(const ContactInfo& requested, ContactInfo& readback) {
  if (!requested.name[0]) return false;

  ContactInfo* existing = lookupContactByPubKey(requested.id.pub_key, PUB_KEY_SIZE);
  if (existing) {
    StrHelper::strncpy(existing->name, requested.name, sizeof(existing->name));
    existing->type = requested.type;
    existing->lastmod = getRTCClock()->getCurrentTime();
    existing->shared_secret_valid = false;
  } else {
    ContactInfo copy = requested;
    copy.name[sizeof(copy.name) - 1] = 0;
    copy.lastmod = getRTCClock()->getCurrentTime();
    copy.shared_secret_valid = false;
    if (!addContact(copy)) return false;
  }

  // Piece 5 deliberately saves immediately rather than relying on the normal
  // lazy write timer: an on-device admin action must be durable before success.
  saveContacts();

  bool found = false;
  ContactInfo persisted{};
  if (!readPersistedContact(_store, requested.id.pub_key, persisted, found) || !found) return false;
  if (memcmp(persisted.id.pub_key, requested.id.pub_key, PUB_KEY_SIZE) != 0) return false;
  if (strcmp(persisted.name, requested.name) != 0 || persisted.type != requested.type) return false;

  ContactInfo* live = lookupContactByPubKey(requested.id.pub_key, PUB_KEY_SIZE);
  if (!live) return false;
  readback = *live;
  return true;
}

bool MyMesh::compactRemoveContactVerified(const uint8_t pub_key[32]) {
  ContactInfo* existing = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (!existing) return false;

  ContactInfo target = *existing;
  if (!removeContact(target)) return false;
  _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE);
  saveContacts();

  ContactInfo persisted{};
  bool found = false;
  if (!readPersistedContact(_store, pub_key, persisted, found)) return false;
  return !found && lookupContactByPubKey(pub_key, PUB_KEY_SIZE) == nullptr;
}

bool MyMesh::compactSetPrivateChannelVerified(uint8_t channel_idx, const ChannelDetails& requested,
                                               ChannelDetails& readback) {
#ifdef MAX_GROUP_CHANNELS
  // Channel zero is the Public / World channel. Piece 5 private-group admin
  // must never rewrite its name or key.
  if (channel_idx == 0 || channel_idx >= MAX_GROUP_CHANNELS || !requested.name[0]) return false;
  if (!setChannel(channel_idx, requested)) return false;
  saveChannels();

  ChannelDetails persisted{};
  if (!readPersistedChannel(_store, channel_idx, persisted)) return false;
  if (strcmp(persisted.name, requested.name) != 0) return false;
  if (memcmp(persisted.channel.secret, requested.channel.secret, sizeof(requested.channel.secret)) != 0) return false;

  if (!getChannel(channel_idx, readback)) return false;
  return strcmp(readback.name, requested.name) == 0 &&
         memcmp(readback.channel.secret, requested.channel.secret, sizeof(requested.channel.secret)) == 0;
#else
  (void)channel_idx; (void)requested; (void)readback;
  return false;
#endif
}

bool MyMesh::compactClearPrivateChannelVerified(uint8_t channel_idx) {
#ifdef MAX_GROUP_CHANNELS
  if (channel_idx == 0 || channel_idx >= MAX_GROUP_CHANNELS) return false;
  ChannelDetails empty{};
  if (!setChannel(channel_idx, empty)) return false;
  saveChannels();

  ChannelDetails persisted{};
  if (!readPersistedChannel(_store, channel_idx, persisted)) return false;
  ChannelDetails live{};
  if (!getChannel(channel_idx, live)) return false;
  return channelIsEmpty(persisted) && channelIsEmpty(live);
#else
  (void)channel_idx;
  return false;
#endif
}

int MyMesh::compactFindFreePrivateChannel() {
#ifdef MAX_GROUP_CHANNELS
  for (int i = 1; i < MAX_GROUP_CHANNELS; ++i) {
    ChannelDetails ch{};
    if (getChannel(i, ch) && channelIsEmpty(ch)) return i;
  }
#endif
  return -1;
}

#endif // MESHCORE_COMPACT_UI
