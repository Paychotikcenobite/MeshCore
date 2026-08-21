#include "CommunicatorAppScreen.h"
#include "CompactAARender.h"
#include "UITask.h"
#include "../MyMesh.h"
#include <string.h>

namespace {

struct AdvertPromptState {
  bool primed = false;
  bool active = false;
  uint8_t selected = 0; // 0 add, 1 dismiss
  unsigned long next_poll = 0;
  ContactInfo contact{};
  uint8_t known[100][7]{};
  uint8_t known_count = 0;
  uint8_t dismissed[24][7]{};
  uint8_t dismissed_count = 0;
};

AdvertPromptState g_advert;

ColorVal rgb565Advert(uint8_t r,uint8_t g,uint8_t b) {
  return (ColorVal)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

bool prefixEquals(const uint8_t a[7],const uint8_t* b) {
  return memcmp(a,b,7)==0;
}

bool listContains(const uint8_t list[][7],uint8_t count,const uint8_t* key) {
  for(uint8_t i=0;i<count;++i) if(prefixEquals(list[i],key)) return true;
  return false;
}

void listAdd(uint8_t list[][7],uint8_t& count,uint8_t cap,const uint8_t* key) {
  if(listContains(list,count,key)) return;
  if(count<cap) memcpy(list[count++],key,7);
}

bool heardRecently(const ContactInfo& contact,mesh::RTCClock* rtc) {
  AdvertPath heard[16];
  int count=the_mesh.getRecentlyHeard(heard,16);
  uint32_t now=rtc->getCurrentTime();
  for(int i=0;i<count;++i) {
    if(memcmp(heard[i].pubkey_prefix,contact.id.pub_key,7)!=0) continue;
    uint32_t ts=heard[i].recv_timestamp;
    if(!ts) continue;
    uint32_t age=now>=ts?now-ts:0;
    if(age<=30) return true;
  }
  return false;
}

void scanCurrentChats(void (*visit)(const ContactInfo&)) {
  ContactInfo c;
  ContactsIterator it=the_mesh.startContactsIterator();
  while(it.hasNext(&the_mesh,c)) if(c.type==ADV_TYPE_CHAT && c.name[0]) visit(c);
}

} // namespace

void CommunicatorAppScreen::primeAdvertContactBaseline() {
  memset(&g_advert,0,sizeof(g_advert));
  g_advert.primed=true;
  g_advert.next_poll=millis()+500;
  ContactInfo c;
  ContactsIterator it=the_mesh.startContactsIterator();
  while(it.hasNext(&the_mesh,c)) {
    if(c.type==ADV_TYPE_CHAT && c.name[0])
      listAdd(g_advert.known,g_advert.known_count,100,c.id.pub_key);
  }
}

void CommunicatorAppScreen::syncAdvertContactBaseline() {
  if(!g_advert.primed) primeAdvertContactBaseline();
  ContactInfo c;
  ContactsIterator it=the_mesh.startContactsIterator();
  while(it.hasNext(&the_mesh,c)) {
    if(c.type==ADV_TYPE_CHAT && c.name[0])
      listAdd(g_advert.known,g_advert.known_count,100,c.id.pub_key);
  }
}

void CommunicatorAppScreen::pollAdvertContactChanges() {
  if(!g_advert.primed) { primeAdvertContactBaseline(); return; }
  if(g_advert.active || millis()<g_advert.next_poll) return;
  g_advert.next_poll=millis()+450;

  ContactInfo c;
  ContactsIterator it=the_mesh.startContactsIterator();
  while(it.hasNext(&the_mesh,c)) {
    if(c.type!=ADV_TYPE_CHAT || !c.name[0]) continue;
    if(listContains(g_advert.known,g_advert.known_count,c.id.pub_key)) continue;

    // A contact inserted manually or over BLE is not a heard-advert prompt.
    // Require local recent-advert evidence from MyMesh's inbound advert cache.
    if(!heardRecently(c,_rtc)) {
      listAdd(g_advert.known,g_advert.known_count,100,c.id.pub_key);
      continue;
    }

    if(listContains(g_advert.dismissed,g_advert.dismissed_count,c.id.pub_key)) {
      // MeshCore auto-add can learn the same dismissed node again. Keep the
      // user's decision for this session instead of repeatedly nagging them.
      the_mesh.compactRemoveContactVerified(c.id.pub_key);
      return;
    }

    g_advert.contact=c;
    g_advert.selected=0;
    g_advert.active=true;
    _dirty=DIRTY_ALL;
    return;
  }
}

bool CommunicatorAppScreen::advertPromptActive() const { return g_advert.active; }

bool CommunicatorAppScreen::handleAdvertPromptTouch(int16_t x,int16_t y,uint8_t gesture) {
  if(!g_advert.active || gesture!=COMPACT_TOUCH_TAP) return g_advert.active;
  if(y<133 || y>174) return true;
  g_advert.selected=x<187?0:1;
  return handleAdvertPromptInput(KEY_ENTER);
}

bool CommunicatorAppScreen::handleAdvertPromptInput(char c) {
  if(!g_advert.active) return false;
  if(c==KEY_LEFT || c==KEY_RIGHT) {
    g_advert.selected=g_advert.selected?0:1;
    _dirty=DIRTY_ALL;
    return true;
  }
  if(c==KEY_CANCEL) g_advert.selected=1;
  else if(c!=KEY_ENTER) return true;

  if(g_advert.selected==0) {
    ContactInfo readback{};
    if(!the_mesh.compactUpsertContactVerified(g_advert.contact,readback)) {
      _task->showAlert("Could not verify contact save",1400);
      return true;
    }
    listAdd(g_advert.known,g_advert.known_count,100,readback.id.pub_key);
    _task->showAlert("Added to contacts",900);
  } else {
    if(!the_mesh.compactRemoveContactVerified(g_advert.contact.id.pub_key)) {
      _task->showAlert("Could not dismiss contact",1400);
      return true;
    }
    listAdd(g_advert.dismissed,g_advert.dismissed_count,24,g_advert.contact.id.pub_key);
    _task->showAlert("Advert dismissed",700);
  }
  g_advert.active=false;
  _dirty=DIRTY_ALL;
  return true;
}

void CommunicatorAppScreen::drawAdvertPromptOverlay(DisplayDriver& d) {
  if(!g_advert.active) return;
  const ColorVal scrim=_light_mode?rgb565Advert(222,229,237):rgb565Advert(3,10,20);
  const ColorVal card=_light_mode?rgb565Advert(255,255,255):rgb565Advert(17,38,63);
  const ColorVal text=_light_mode?rgb565Advert(20,28,40):rgb565Advert(255,255,255);
  const ColorVal sub=_light_mode?rgb565Advert(92,108,128):rgb565Advert(166,185,207);
  const ColorVal accent=_light_mode?rgb565Advert(11,58,117):rgb565Advert(31,99,198);
  const ColorVal divider=_light_mode?rgb565Advert(176,198,221):rgb565Advert(55,88,124);

  d.setColor(scrim);d.fillRect(0,42,320,198);
  d.setColor(card);d.fillRoundRect(24,70,272,108,9);
  d.setColor(divider);d.drawRoundRect(24,70,272,108,9);

  CompactAA::text(d,40,83,"Heard:",sub,card,CompactAA::REGULAR_9);
  CompactAA::textEllipsized(d,40,101,240,g_advert.contact.name,text,card,CompactAA::MEDIUM_11);

  const bool addOn=g_advert.selected==0;
  d.setColor(addOn?accent:card);d.fillRoundRect(38,136,145,30,7);
  d.setColor(addOn?accent:divider);d.drawRoundRect(38,136,145,30,7);
  CompactAA::textCentered(d,110,146,"Add to contacts",addOn?rgb565Advert(255,255,255):text,addOn?accent:card,CompactAA::REGULAR_9);

  const bool dismissOn=g_advert.selected==1;
  d.setColor(dismissOn?accent:card);d.fillRoundRect(190,136,92,30,7);
  d.setColor(dismissOn?accent:divider);d.drawRoundRect(190,136,92,30,7);
  CompactAA::textCentered(d,236,146,"Dismiss",dismissOn?rgb565Advert(255,255,255):text,dismissOn?accent:card,CompactAA::REGULAR_9);
}
