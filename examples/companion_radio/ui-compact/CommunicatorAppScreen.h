#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/ContactInfo.h>
#include <helpers/ChannelDetails.h>

class UITask;

enum CompactTouchGesture : uint8_t {
  COMPACT_TOUCH_TAP = 0,
  COMPACT_TOUCH_SWIPE_UP = 1,
  COMPACT_TOUCH_SWIPE_DOWN = 2,
  COMPACT_TOUCH_SWIPE_LEFT = 3,
  COMPACT_TOUCH_SWIPE_RIGHT = 4,
  COMPACT_TOUCH_LONG_PRESS = 5,
};

class CommunicatorAppScreen : public UIScreen {
public:
  CommunicatorAppScreen(UITask* task, mesh::RTCClock* rtc);

  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;

  bool handleTouch(int16_t x, int16_t y, uint8_t gesture);
  void addMessage(uint8_t path_len, const char* from, const char* text);
  void clearUnread();
  void markAllDirty();
  bool shouldWakeForMessage(const char* from_name);

  void openSettingsSingleTop();
  void openRadioSingleTop();
  void navigateBack();
  void navigateHome();

  void persistenceBegin();
  void persistenceCheckpoint(bool force = false);
  bool handlePersistentDataTouch(int16_t x, int16_t y, uint8_t gesture);

  void manualContactsBegin();
  bool tryBeginContactAdd(int16_t x, int16_t y, uint8_t gesture);
  bool contactAddActive() const;
  bool handleContactAddTouch(int16_t x, int16_t y, uint8_t gesture);
  bool handleContactAddInput(char c);
  void drawContactAddOverlay(DisplayDriver& display);

  void redrawHeaderActionIcons(DisplayDriver& display);
  void redrawHeaderActionIconsFluent(DisplayDriver& display);
  bool fullVisualRedrawPending() const;

  // Piece 4 delivery lifecycle. The radio ACK table remains authoritative;
  // these methods only reconcile and render the compact UI's local state.
  void reconcileDirectSendState();
  void drawDirectSendOverlay(DisplayDriver& display);

  // Piece 4 per-message actions are a modal overlay so they do not disturb the
  // validated chat renderer or GT911 input path.
  bool messageActionActive() const;
  bool tryOpenMessageActions(int16_t x, int16_t y, uint8_t gesture);
  bool handleMessageActionTouch(int16_t x, int16_t y, uint8_t gesture);
  bool handleMessageActionInput(char c);
  void drawMessageActionOverlay(DisplayDriver& display);

  // MeshCore plain/group text has no protocol-level reply field. Reply is kept
  // as durable local metadata keyed by the existing stable history IDs and is
  // labeled local in the UI rather than silently inventing an RF extension.
  bool beginReplyToMessage(int slot);
  void clearPendingReply();
  bool replyPending() const;
  bool handleReplyTouch(int16_t x, int16_t y, uint8_t gesture);
  void drawReplyComposerOverlay(DisplayDriver& display);
  uint64_t replyTargetForMessage(int slot) const;
  bool getReplyTargetPreview(int slot, char* out, size_t len) const;

  // Capture the unread boundary before openRow() clears unread flags, then keep
  // a lightweight on-screen divider/newest affordance without rewriting chat.
  void prepareUnreadNavigationForTouch(int16_t x, int16_t y, uint8_t gesture);
  void noteNewMessageForUnreadNavigation(const char* from_name);
  bool handleNewestTouch(int16_t x, int16_t y, uint8_t gesture);
  bool handleNewestInput(char c);
  void drawNewMessagesOverlay(DisplayDriver& display);

private:
  enum Route : uint8_t {
    ROUTE_MAIN = 0,
    ROUTE_CHAT,
    ROUTE_NEW_CONVERSATION,
    ROUTE_SETTINGS,
    ROUTE_SETTINGS_MESSAGING,
    ROUTE_SETTINGS_LOCATION,
    ROUTE_SETTINGS_APPEARANCE,
    ROUTE_SETTINGS_ADVANCED,
    ROUTE_RADIO,
    ROUTE_RADIO_STATS,
    ROUTE_DEVICE_DETAILS,
    ROUTE_CONVERSATION_DETAILS,
    ROUTE_REPEATER_DETAILS,
    ROUTE_REPEATER_MAP,
    ROUTE_NETWORK_HEALTH,
    ROUTE_CONTACT_MAP,
    ROUTE_QUICK_MENU,
    ROUTE_ALIAS_EDIT,
    ROUTE_FEATURE_NOTE,
  };

  enum MainTab : uint8_t { TAB_CHATS = 0, TAB_REPEATERS = 1 };
  enum ChatFilter : uint8_t { FILTER_ALL = 0, FILTER_FAVORITES, FILTER_UNREAD, FILTER_ATTENTION };
  enum RepeaterSort : uint8_t { REPEATER_RECENT = 0, REPEATER_DISTANCE = 1 };
  enum RowKind : uint8_t { ROW_NONE = 0, ROW_CONTACT, ROW_CHANNEL, ROW_REPEATER, ROW_RECENT_REPEATER, ROW_UNKNOWN };
  enum Dirty : uint8_t { DIRTY_NONE = 0, DIRTY_COMPOSER, DIRTY_ALL };
  // Preserve persisted schema-v1 SENT=1 and FAILED=2. CONFIRMED=4 is new and
  // may only be written after a real direct-message ACK is observed. STOPPED=5
  // means local ACK tracking was deliberately abandoned after RF handoff.
  enum SendState : uint8_t { SEND_NONE = 0, SEND_SENT = 1, SEND_FAILED = 2, SEND_SENDING = 3, SEND_CONFIRMED = 4, SEND_STOPPED = 5 };

  struct MessageEntry {
    uint32_t timestamp;
    uint32_t delivery_ack;
    uint32_t delivery_deadline_ms;
    uint8_t path_len;
    uint8_t unread;
    uint8_t send_state;
    bool outgoing;
    char origin[32];
    char text[144];
  };

  struct UiMeta {
    uint8_t kind;
    uint8_t identity[32];
    uint8_t channel_index;
    uint8_t flags;
    char alias[24];
  };

  struct Row {
    RowKind kind;
    char name[32];
    char preview[80];
    uint32_t timestamp;
    uint8_t unread;
    uint8_t attention;
    int8_t meta_index;
    ContactInfo contact;
    ChannelDetails channel;
    uint8_t channel_index;
  };

  static const int MESSAGE_CACHE = 96;
  static const int MAX_LOCAL_CONTACTS = 40;
  static const int MAX_LOCAL_REPEATERS = 40;
  static const int MAX_LOCAL_CHANNELS = 16;
  static const int MAX_ROWS = 64;
  static const int MAX_META = 48;

  UITask* _task;
  mesh::RTCClock* _rtc;
  Route _route;
  Route _route_stack[8];
  uint8_t _route_depth;
  MainTab _tab;
  ChatFilter _filter;
  RepeaterSort _repeater_sort;
  Dirty _dirty;

  bool _light_mode;
  bool _show_public;
  bool _search_active;
  bool _chat_search_active;
  bool _editing_alias;
  char _search[40];
  uint8_t _search_len;
  char _chat_search[40];
  uint8_t _chat_search_len;
  char _compose[MAX_TEXT_LEN + 1];
  uint16_t _compose_len;
  char _edit[32];
  uint8_t _edit_len;

  int _selected;
  int _list_offset;
  int _quick_selected;
  int _message_scroll;

  MessageEntry _messages[MESSAGE_CACHE];
  uint8_t _message_count;
  uint8_t _message_head;

  ContactInfo _contacts[MAX_LOCAL_CONTACTS];
  uint8_t _contact_count;
  ContactInfo _repeaters[MAX_LOCAL_REPEATERS];
  uint8_t _repeater_count;
  ChannelDetails _channels[MAX_LOCAL_CHANNELS];
  uint8_t _channel_indexes[MAX_LOCAL_CHANNELS];
  uint8_t _channel_count;

  UiMeta _meta[MAX_META];
  uint8_t _meta_count;

  Row _rows[MAX_ROWS];
  uint8_t _row_count;

  RowKind _active_kind;
  ContactInfo _active_contact;
  ChannelDetails _active_channel;
  uint8_t _active_channel_index;
  char _active_name[32];
  ContactInfo _info_contact;
  char _feature_title[32];
  char _feature_body[160];

  void loadPrefs();
  void savePrefs();
  void saveMeta();
  void loadContactsAndChannels();
  void buildChatRows();
  void buildRepeaterRows();
  void buildNewConversationRows();
  void sortChatRows();
  void clampOffset(int visible);

  int findContactByName(const char* name) const;
  int findChannelByName(const char* name) const;
  int findMetaForContact(const ContactInfo& c, bool create);
  int findMetaForChannel(const ChannelDetails& c, uint8_t idx, bool create);
  bool metaFlag(int idx, uint8_t mask) const;
  void toggleMetaFlag(int idx, uint8_t mask);
  const char* displayNameFor(const Row& row, char* out, size_t len) const;
  bool containsInsensitive(const char* hay, const char* needle) const;
  bool rowNameExists(const char* name) const;
  void pushRow(const Row& row);
  bool rowNeedsAttention(const Row& row) const;
  void clearUnreadFor(const char* name);
  void deleteLocalMessagesFor(const char* name);
  bool hasDistanceOrigin() const;
  double distanceKm(const ContactInfo& contact) const;

  void pushRoute(Route route);
  void replaceRoute(Route route);
  void goBack();
  void goHome();
  void openRow(const Row& row);
  void openFeature(const char* title, const char* body);
  void beginAliasEdit();
  void commitAliasEdit();

  void addCachedMessage(const char* origin, const char* text, bool outgoing, uint8_t path_len, SendState state = SEND_NONE);
  bool sendCompose();
  int collectActiveMessages(int indexes[], int maxn) const;

  void fillScreen(DisplayDriver& d);
  void drawAppHeader(DisplayDriver& d);
  void drawRadioGlyph(DisplayDriver& d, int cx, int cy);
  void drawGear(DisplayDriver& d, int cx, int cy);
  void drawLogo(DisplayDriver& d);
  void drawBack(DisplayDriver& d, int y);
  void drawAvatar(DisplayDriver& d, int cx, int cy, const char* name, bool channel);
  void drawTabs(DisplayDriver& d);
  void drawSearch(DisplayDriver& d);
  void drawFilterChips(DisplayDriver& d);
  void drawConversationRow(DisplayDriver& d, const Row& row, int y, int h, bool selected);
  void drawRepeaterRow(DisplayDriver& d, const Row& row, int y, int h, bool selected);
  void drawDetailTitle(DisplayDriver& d, const char* title, const char* subtitle = nullptr);
  void drawButton(DisplayDriver& d, int x, int y, int w, int h, const char* label, bool primary, bool enabled = true);
  void drawInfoCard(DisplayDriver& d, int x, int y, int w, int h, const char* title, const char* value, const char* sub = nullptr);
  void drawWrapped(DisplayDriver& d, int x, int y, int max_width, int lines, const char* text);

  void drawMain(DisplayDriver& d);
  void drawChat(DisplayDriver& d);
  void drawChatToolbar(DisplayDriver& d);
  void drawComposer(DisplayDriver& d);
  void drawNewConversation(DisplayDriver& d);
  void drawSettings(DisplayDriver& d);
  void drawMessagingSettings(DisplayDriver& d);
  void drawLocationSettings(DisplayDriver& d);
  void drawAppearanceSettings(DisplayDriver& d);
  void drawAdvancedSettings(DisplayDriver& d);
  void drawRadio(DisplayDriver& d);
  void drawRadioStats(DisplayDriver& d);
  void drawDeviceDetails(DisplayDriver& d);
  void drawConversationDetails(DisplayDriver& d);
  void drawRepeaterDetails(DisplayDriver& d);
  void drawRepeaterMap(DisplayDriver& d);
  void drawNetworkHealth(DisplayDriver& d);
  void drawContactMap(DisplayDriver& d);
  void drawQuickMenu(DisplayDriver& d);
  void drawAliasEdit(DisplayDriver& d);
  void drawFeatureNote(DisplayDriver& d);

  bool touchMain(int16_t x, int16_t y, uint8_t gesture);
  bool touchChat(int16_t x, int16_t y, uint8_t gesture);
  bool touchNewConversation(int16_t x, int16_t y, uint8_t gesture);
  bool touchSettings(int16_t x, int16_t y, uint8_t gesture);
  bool touchRadio(int16_t x, int16_t y, uint8_t gesture);
  bool touchDetail(int16_t x, int16_t y, uint8_t gesture);
  bool touchQuickMenu(int16_t x, int16_t y, uint8_t gesture);
};