#include "CommunicatorAppScreen.h"
#include "CompactAARender.h"
#include "UITask.h"
#include <string.h>

namespace {

constexpr ColorVal aaRgb565(uint8_t r,uint8_t g,uint8_t b) {
  return (ColorVal)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct AAPalette {
  ColorVal bg,card,card2,text,sub,accent,incoming,outgoing,input,stroke,danger;
};

AAPalette aaPalette(bool light) {
  if(light) return {aaRgb565(247,249,252),aaRgb565(255,255,255),aaRgb565(238,243,249),aaRgb565(20,28,40),aaRgb565(92,108,128),aaRgb565(11,58,117),aaRgb565(229,234,240),aaRgb565(31,99,198),aaRgb565(222,233,245),aaRgb565(176,198,221),aaRgb565(195,45,45)};
  return {aaRgb565(5,16,32),aaRgb565(12,29,50),aaRgb565(17,38,63),aaRgb565(255,255,255),aaRgb565(166,185,207),aaRgb565(18,63,122),aaRgb565(31,48,70),aaRgb565(19,79,166),aaRgb565(27,57,91),aaRgb565(55,88,124),aaRgb565(220,70,70)};
}

void aaAge(mesh::RTCClock* rtc,uint32_t ts,char* out,size_t len) {
  if(!ts){out[0]=0;return;}
  int32_t age=(int32_t)(rtc->getCurrentTime()-ts);if(age<0)age=0;
  if(age<60)snprintf(out,len,"now");
  else if(age<3600)snprintf(out,len,"%ldm",(long)(age/60));
  else if(age<86400)snprintf(out,len,"%ldh",(long)(age/3600));
  else if(age<14*86400)snprintf(out,len,"%ldd",(long)(age/86400));
  else snprintf(out,len,"old");
}

void aaInitials(const char* name,char out[3]) {
  out[0]='M';out[1]='C';out[2]=0;if(!name||!name[0])return;
  out[0]=name[0];out[1]=0;const char* p=name;while(*p&&*p!=' ')++p;
  if(*p==' '&&p[1]){out[1]=p[1];out[2]=0;}else if(name[1]){out[1]=name[1];out[2]=0;}
}

void clearText(DisplayDriver& d,int x,int y,int w,int h,ColorVal bg){d.setColor(bg);d.fillRect(x,y,w,h);}

const char* detailTitle(uint8_t route) {
  switch(route) {
    case 3:return "Settings";
    case 4:return "Messaging";
    case 5:return "Location & maps";
    case 6:return "Appearance";
    case 7:return "Advanced settings";
    case 8:return "Radio Status";
    case 9:return "Radio statistics";
    case 10:return "Device details";
    case 11:return "Conversation details";
    case 12:return "Repeater details";
    case 13:return "Map coverage";
    case 14:return "Network health";
    case 15:return "Contact location";
    case 17:return "Rename";
    default:return nullptr;
  }
}

} // namespace

void CommunicatorAppScreen::redrawAAComposerText(DisplayDriver& d) {
  AAPalette p=aaPalette(_light_mode);
  clearText(d,12,211,181,15,p.input);
  CompactAA::textEllipsized(d,16,213,173,_compose_len?_compose:"Message",_compose_len?p.text:p.sub,p.input,CompactAA::REGULAR_9);
  clearText(d,209,211,39,13,p.card2);
  CompactAA::textCentered(d,228,213,"Voice",p.text,p.card2,CompactAA::REGULAR_9);
  clearText(d,264,211,44,13,p.accent);
  CompactAA::textCentered(d,286,213,"Send",aaRgb565(255,255,255),p.accent,CompactAA::REGULAR_9);
}

void CommunicatorAppScreen::redrawAATextPass(DisplayDriver& d) {
  AAPalette p=aaPalette(_light_mode);

  clearText(d,39,4,145,34,p.bg);
  CompactAA::circle(d,19,20,15,p.accent,p.bg);
  CompactAA::textCentered(d,19,15,"MC",aaRgb565(255,255,255),p.accent,CompactAA::REGULAR_9);
  CompactAA::text(d,41,6,"MeshCore",p.text,p.bg,CompactAA::MEDIUM_11);
  CompactAA::text(d,41,22,"Communicator",p.sub,p.bg,CompactAA::REGULAR_9);

  if(_route==ROUTE_MAIN) {
    char rep[24];snprintf(rep,sizeof(rep),"Repeaters %u",_repeater_count);
    const char* labels[2]={"Chats",rep};
    for(int i=0;i<2;++i){int x=i?162:6,w=152;ColorVal bg=_tab==i?p.card2:p.bg;clearText(d,x+8,50,w-16,14,bg);CompactAA::textCentered(d,x+w/2,52,labels[i],_tab==i?p.text:p.sub,bg,CompactAA::REGULAR_9);}
    if(_tab==TAB_CHATS) {
      clearText(d,14,80,286,17,p.input);
      CompactAA::textEllipsized(d,17,82,268,_search[0]?_search:"Search conversations",_search[0]?p.text:p.sub,p.input,CompactAA::REGULAR_9);
      CompactAA::searchIcon(d,289,81,p.sub,p.input);
      const char* chips[4]={"All","Favorites","Unread","Attention"};const int xs[4]={7,84,161,238};
      for(int i=0;i<4;++i){ColorVal bg=_filter==i?p.card2:p.bg;clearText(d,xs[i]+4,108,66,13,bg);CompactAA::textCentered(d,xs[i]+37,109,chips[i],_filter==i?p.text:p.sub,bg,CompactAA::REGULAR_9);}
      buildChatRows();
      for(int i=0;i<2;++i){int idx=_list_offset+i;if(idx>=_row_count)break;const Row&r=_rows[idx];int y=129+i*37;char shown[32];displayNameFor(r,shown,sizeof(shown));clearText(d,43,y+3,260,29,p.card);ColorVal av=r.kind==ROW_CHANNEL?aaRgb565(77,93,125):p.accent;CompactAA::circle(d,26,y+17,13,av,p.card);char ini[3];aaInitials(shown,ini);CompactAA::textCentered(d,26,y+12,ini,aaRgb565(255,255,255),av,CompactAA::REGULAR_9);int nx=45;if(metaFlag(r.meta_index,0x01)){CompactAA::text(d,nx,y+4,"*",p.accent,p.card);nx+=7;}if(metaFlag(r.meta_index,0x02)){CompactAA::text(d,nx,y+4,"P",p.accent,p.card);nx+=7;}CompactAA::textEllipsized(d,nx,y+3,175-(nx-45),shown,p.text,p.card,CompactAA::MEDIUM_11);CompactAA::textEllipsized(d,45,y+20,205,r.attention?"Failed message - needs attention":r.preview,r.attention?p.danger:p.sub,p.card,CompactAA::REGULAR_9);char age[12];aaAge(_rtc,r.timestamp,age,sizeof(age));CompactAA::textRight(d,302,y+4,age,p.sub,p.card,CompactAA::REGULAR_9);if(r.unread){CompactAA::circle(d,291,y+23,7,p.accent,p.card);char u[5];snprintf(u,sizeof(u),"%u",r.unread);CompactAA::textCentered(d,291,y+18,u,aaRgb565(255,255,255),p.accent,CompactAA::REGULAR_9);}}
      clearText(d,44,214,232,14,p.accent);CompactAA::textCentered(d,160,215,"+  New conversation",aaRgb565(255,255,255),p.accent,CompactAA::REGULAR_9);
    } else {
      clearText(d,18,82,284,14,p.card2);CompactAA::textCentered(d,160,83,"Refresh repeaters",p.text,p.card2,CompactAA::REGULAR_9);
      clearText(d,16,110,132,13,p.card2);CompactAA::textCentered(d,82,111,"Map coverage",p.text,p.card2,CompactAA::REGULAR_9);
      clearText(d,172,110,132,13,p.card2);CompactAA::textCentered(d,238,111,"Network health",p.text,p.card2,CompactAA::REGULAR_9);
      clearText(d,10,136,178,14,p.bg);CompactAA::text(d,11,138,"Known repeaters",p.sub,p.bg,CompactAA::REGULAR_9);
      buildRepeaterRows();
      for(int i=0;i<2;++i){int idx=_list_offset+i;if(idx>=_row_count)break;const Row&r=_rows[idx];int y=156+i*39;clearText(d,33,y+3,270,29,p.card);CompactAA::textEllipsized(d,35,y+3,183,r.name,p.text,p.card,CompactAA::MEDIUM_11);CompactAA::textEllipsized(d,35,y+20,190,r.preview,p.sub,p.card,CompactAA::REGULAR_9);char age[12];aaAge(_rtc,r.timestamp,age,sizeof(age));CompactAA::textRight(d,302,y+4,age,p.sub,p.card,CompactAA::REGULAR_9);CompactAA::chevronIcon(d,297,y+17,p.sub,p.card);}
    }
    return;
  }

  if(_route==ROUTE_CHAT) {
    clearText(d,12,51,20,18,p.card2);CompactAA::backIcon(d,12,51,p.text,p.card2);
    char shown[32];strncpy(shown,_active_name,sizeof(shown));shown[sizeof(shown)-1]=0;
    if(_active_kind==ROW_CONTACT){int m=findMetaForContact(_active_contact,false);if(m>=0&&_meta[m].alias[0])strncpy(shown,_meta[m].alias,sizeof(shown));}
    if(_active_kind==ROW_CHANNEL){int m=findMetaForChannel(_active_channel,_active_channel_index,false);if(m>=0&&_meta[m].alias[0])strncpy(shown,_meta[m].alias,sizeof(shown));}
    ColorVal av=_active_kind==ROW_CHANNEL?aaRgb565(77,93,125):p.accent;CompactAA::circle(d,53,59,13,av,p.bg);char ini[3];aaInitials(shown,ini);CompactAA::textCentered(d,53,54,ini,aaRgb565(255,255,255),av,CompactAA::REGULAR_9);
    clearText(d,70,46,140,31,p.bg);CompactAA::textEllipsized(d,72,47,137,shown,p.text,p.bg,CompactAA::MEDIUM_11);char subtitle[30];if(_active_kind==ROW_CONTACT){char age[18];aaAge(_rtc,_active_contact.last_advert_timestamp,age,sizeof(age));snprintf(subtitle,sizeof(subtitle),"Heard %s",age[0]?age:"unknown");}else strncpy(subtitle,_active_channel_index==0?"Public / World":"Group chat",sizeof(subtitle));CompactAA::textEllipsized(d,72,64,137,subtitle,p.sub,p.bg,CompactAA::REGULAR_9);
    const int bx[3]={214,249,284};const char* bl[3]={"S","i","M"};for(int i=0;i<3;++i){ColorVal bg=p.card2;clearText(d,bx[i]+4,52,23,13,bg);CompactAA::textCentered(d,bx[i]+15,54,bl[i],p.text,bg,CompactAA::REGULAR_9);}
    if(_chat_search_active){clearText(d,14,84,286,17,p.input);CompactAA::textEllipsized(d,17,86,280,_chat_search[0]?_chat_search:"Search this conversation",_chat_search[0]?p.text:p.sub,p.input,CompactAA::REGULAR_9);}
    int top=_chat_search_active?108:80;int idxs[4];int maxn=_chat_search_active?2:3,count=collectActiveMessages(idxs,maxn),y=top+5;d.setTextSize(1);
    for(int i=0;i<count;++i){const MessageEntry&m=_messages[idxs[i]];int tw=d.getTextWidth(m.text);if(tw>205)tw=205;int w=tw+18;if(w<72)w=72;if(w>226)w=226;int x=m.outgoing?312-w:8;ColorVal bg=m.outgoing?p.outgoing:p.incoming;clearText(d,x+6,y+4,w-12,24,bg);ColorVal fg=m.outgoing&&_light_mode?aaRgb565(255,255,255):p.text;CompactAA::textEllipsized(d,x+8,y+5,w-16,m.text,fg,bg,CompactAA::REGULAR_9);char age[12];aaAge(_rtc,m.timestamp,age,sizeof(age));ColorVal sf=m.outgoing&&_light_mode?aaRgb565(225,235,250):p.sub;CompactAA::textRight(d,x+w-7,y+20,age,sf,bg,CompactAA::REGULAR_9);if(m.outgoing)CompactAA::text(d,x+8,y+20,m.send_state==SEND_FAILED?"Failed":"Sent",m.send_state==SEND_FAILED?p.danger:sf,bg,CompactAA::REGULAR_9);y+=35;}
    redrawAAComposerText(d);
    return;
  }

  if(_route==ROUTE_NEW_CONVERSATION) {
    clearText(d,12,51,20,18,p.card2);CompactAA::backIcon(d,12,51,p.text,p.card2);clearText(d,46,47,172,20,p.bg);CompactAA::text(d,47,49,"New conversation",p.text,p.bg,CompactAA::MEDIUM_11);
    const int xs[3]={7,111,215};const char* labels[3]={"Add contact","Group chat","Expedition"};for(int i=0;i<3;++i){clearText(d,xs[i]+5,87,88,14,p.card2);CompactAA::textCentered(d,xs[i]+49,89,labels[i],p.text,p.card2,CompactAA::REGULAR_9);}clearText(d,9,111,130,14,p.bg);CompactAA::text(d,11,113,"People & groups",p.sub,p.bg,CompactAA::REGULAR_9);buildNewConversationRows();for(int i=0;i<3;++i){int idx=_list_offset+i;if(idx>=_row_count)break;const Row&r=_rows[idx];int y=125+i*36;char shown[32];displayNameFor(r,shown,sizeof(shown));clearText(d,43,y+3,260,27,p.card);ColorVal av=r.kind==ROW_CHANNEL?aaRgb565(77,93,125):p.accent;CompactAA::circle(d,26,y+17,13,av,p.card);char ini[3];aaInitials(shown,ini);CompactAA::textCentered(d,26,y+12,ini,aaRgb565(255,255,255),av,CompactAA::REGULAR_9);CompactAA::textEllipsized(d,45,y+3,180,shown,p.text,p.card,CompactAA::MEDIUM_11);CompactAA::textEllipsized(d,45,y+19,205,r.preview,p.sub,p.card,CompactAA::REGULAR_9);}
    return;
  }

  const char* title=detailTitle((uint8_t)_route);
  if(title){clearText(d,12,51,20,18,p.card2);CompactAA::backIcon(d,12,51,p.text,p.card2);clearText(d,46,47,172,21,p.bg);CompactAA::textEllipsized(d,47,49,170,title,p.text,p.bg,CompactAA::MEDIUM_11);}

  if(_route==ROUTE_SETTINGS) {
    const char* t[6]={"Connection & radio","Messaging","Location & maps","Appearance","Data & backup","Advanced settings"};const char* s[6]={"On-device SX1262",_show_public?"World visible":"World hidden","Coverage & privacy",_light_mode?"Light":"Dark","History / export","Hardware & service"};
    for(int i=0;i<6;++i){int y=80+i*25;clearText(d,13,y+3,289,17,p.card);CompactAA::text(d,15,y+5,t[i],p.text,p.card,CompactAA::REGULAR_9);CompactAA::textRight(d,302,y+13,s[i],p.sub,p.card,CompactAA::REGULAR_9);}
  }
}
