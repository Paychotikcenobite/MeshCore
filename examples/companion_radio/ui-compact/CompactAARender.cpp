#include "CompactAARender.h"
#include <string.h>

namespace CompactAA {
namespace {

struct Glyph {
  uint16_t offset;
  uint8_t w;
  uint8_t h;
  int8_t xoff;
  int8_t yoff;
  uint8_t advance;
};

struct Font {
  const Glyph* glyphs;
  const uint8_t* bitmap;
  uint8_t ascent;
  uint8_t line_height;
};

// These .inc files contain only raster coverage and glyph metrics, never font files.
#include "CompactAAReg9.inc"
#include "CompactAAMed11.inc"
#include "CompactAAIcons.inc"

static bool decoded=false;

int b64v(char c) {
  if(c>='A'&&c<='Z')return c-'A';
  if(c>='a'&&c<='z')return c-'a'+26;
  if(c>='0'&&c<='9')return c-'0'+52;
  if(c=='+')return 62;
  if(c=='/')return 63;
  return -1;
}

size_t decode64(const char* s,uint8_t* out,size_t cap) {
  uint32_t acc=0; int bits=0; size_t n=0;
  for(;*s;++s) {
    if(*s=='=')break;
    int v=b64v(*s); if(v<0)continue;
    acc=(acc<<6)|(uint32_t)v; bits+=6;
    if(bits>=8) {
      bits-=8;
      if(n<cap) out[n++]=(uint8_t)((acc>>bits)&0xFF);
    }
  }
  return n;
}

void ensureDecoded() {
  if(decoded)return;
  decode64(REG9_A4_B64,REG9_A4,sizeof(REG9_A4));
  decode64(MED11_A4_B64,MED11_A4,sizeof(MED11_A4));
  decode64(BACK18_A4_B64,BACK18_A4,sizeof(BACK18_A4));
  decode64(SEARCH14_A4_B64,SEARCH14_A4,sizeof(SEARCH14_A4));
  decode64(CHEVRON8_A4_B64,CHEVRON8_A4,sizeof(CHEVRON8_A4));
  decoded=true;
}

Font choose(FontRole role) {
  ensureDecoded();
  if(role==MEDIUM_11)return {MED11_GLYPHS,MED11_A4,11,14};
  return {REG9_GLYPHS,REG9_A4,9,11};
}

ColorVal blend565(ColorVal fg,ColorVal bg,uint8_t a4) {
  if(!a4)return bg;
  if(a4>=15)return fg;
  uint16_t fr=(fg>>11)&31,fg6=(fg>>5)&63,fb=fg&31;
  uint16_t br=(bg>>11)&31,bg6=(bg>>5)&63,bb=bg&31;
  uint16_t r=(fr*a4+br*(15-a4)+7)/15;
  uint16_t g=(fg6*a4+bg6*(15-a4)+7)/15;
  uint16_t b=(fb*a4+bb*(15-a4)+7)/15;
  return (ColorVal)((r<<11)|(g<<5)|b);
}

uint8_t nibbleAt(const uint8_t* data,uint32_t pixel) {
  uint8_t v=data[pixel>>1];
  return (pixel&1)?(v&0x0F):(v>>4);
}

void drawMask(DisplayDriver& d,int x,int y,const uint8_t* data,int w,int h,ColorVal fg,ColorVal bg) {
  ColorVal ramp[16];
  for(int i=0;i<16;++i)ramp[i]=blend565(fg,bg,(uint8_t)i);
  for(int yy=0;yy<h;++yy) {
    int xx=0;
    while(xx<w) {
      uint8_t a=nibbleAt(data,(uint32_t)yy*w+xx);
      if(!a){++xx;continue;}
      int start=xx++;
      while(xx<w&&nibbleAt(data,(uint32_t)yy*w+xx)==a)++xx;
      d.setColor(ramp[a]);
      d.fillRect(x+start,y+yy,xx-start,1);
    }
  }
}

const Glyph& glyphFor(const Font& font,char c) {
  uint8_t u=(uint8_t)c;
  if(u<32||u>126)u='?';
  return font.glyphs[u-32];
}

} // namespace

int textWidth(const char* s,FontRole role) {
  if(!s)return 0;
  Font f=choose(role);
  int w=0;
  while(*s)w+=glyphFor(f,*s++).advance;
  return w;
}

int lineHeight(FontRole role){return choose(role).line_height;}

void text(DisplayDriver& d,int x,int y,const char* s,ColorVal fg,ColorVal bg,FontRole role) {
  if(!s)return;
  Font f=choose(role);
  int pen=x,baseline=y+f.ascent;
  while(*s) {
    const Glyph& g=glyphFor(f,*s++);
    if(g.w&&g.h)drawMask(d,pen+g.xoff,baseline+g.yoff,f.bitmap+g.offset,g.w,g.h,fg,bg);
    pen+=g.advance;
  }
}

void textCentered(DisplayDriver& d,int cx,int y,const char* s,ColorVal fg,ColorVal bg,FontRole role) {
  text(d,cx-textWidth(s,role)/2,y,s,fg,bg,role);
}

void textRight(DisplayDriver& d,int right,int y,const char* s,ColorVal fg,ColorVal bg,FontRole role) {
  text(d,right-textWidth(s,role),y,s,fg,bg,role);
}

void textEllipsized(DisplayDriver& d,int x,int y,int maxw,const char* s,ColorVal fg,ColorVal bg,FontRole role) {
  if(!s||maxw<=0)return;
  if(textWidth(s,role)<=maxw){text(d,x,y,s,fg,bg,role);return;}
  char tmp[160];
  size_t n=strlen(s);
  if(n>sizeof(tmp)-4)n=sizeof(tmp)-4;
  memcpy(tmp,s,n);tmp[n]=0;
  int dotw=textWidth("...",role);
  while(n>0&&textWidth(tmp,role)>maxw-dotw)tmp[--n]=0;
  if(n+3<sizeof(tmp))strcat(tmp,"...");
  text(d,x,y,tmp,fg,bg,role);
}

void circle(DisplayDriver& d,int cx,int cy,int r,ColorVal fg,ColorVal bg) {
  ColorVal ramp[16];
  for(int i=0;i<16;++i)ramp[i]=blend565(fg,bg,(uint8_t)i);
  constexpr int scale=4;
  int rr=(r*scale)*(r*scale);
  for(int py=-r-1;py<=r+1;++py) {
    auto coverage=[&](int sx)->uint8_t {
      int inside=0;
      for(int yy=0;yy<scale;++yy)for(int xx=0;xx<scale;++xx) {
        int dx=sx*scale*2+(xx*2+1)-scale;
        int dy=py*scale*2+(yy*2+1)-scale;
        if(dx*dx+dy*dy<=rr*4)++inside;
      }
      return inside>=16?15:(uint8_t)inside;
    };
    int px=-r-1;
    while(px<=r+1) {
      uint8_t a=coverage(px);
      int start=px++;
      while(px<=r+1&&coverage(px)==a)++px;
      d.setColor(ramp[a]);
      d.fillRect(cx+start,cy+py,px-start,1);
    }
  }
}

void backIcon(DisplayDriver& d,int x,int y,ColorVal fg,ColorVal bg){
  ensureDecoded();drawMask(d,x,y,BACK18_A4,18,18,fg,bg);
}
void searchIcon(DisplayDriver& d,int x,int y,ColorVal fg,ColorVal bg){
  ensureDecoded();drawMask(d,x,y,SEARCH14_A4,14,14,fg,bg);
}
void chevronIcon(DisplayDriver& d,int x,int y,ColorVal fg,ColorVal bg){
  ensureDecoded();drawMask(d,x,y,CHEVRON8_A4,8,12,fg,bg);
}

} // namespace CompactAA
