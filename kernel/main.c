// NOVA OS Native 0.1: independent freestanding 64-bit kernel (not Linux).
// Boots via the Limine protocol. PS/2 input is intended only for virtual machines.
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limine.h>
#include "font.h"
#include "raster_font.h"
#include "ps2_mouse.h"
#include "ramfs.h"

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request nova_memmap_request={
    .id=LIMINE_MEMMAP_REQUEST_ID,.revision=0
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static struct limine_framebuffer *fb;
static int screen = 1, player = 9, coin = 22, score = 0;
static const char *names[] = {"HOME","FILES","SYSTEM","ARCADE","HELP"};

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static void idle(void) { for(;;) __asm__ volatile("pause"); }
static int X(int x){return (int)((int64_t)x * (int64_t)fb->width/1280);}
static int Y(int y){return (int)((int64_t)y * (int64_t)fb->height/800);}
static uint32_t encode(uint32_t rgb) {
    uint32_t r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
    return ((r*((1u<<fb->red_mask_size)-1)/255)<<fb->red_mask_shift)
        | ((g*((1u<<fb->green_mask_size)-1)/255)<<fb->green_mask_shift)
        | ((b*((1u<<fb->blue_mask_size)-1)/255)<<fb->blue_mask_shift);
}
static void rect(int x,int y,int w,int h,uint32_t rgb) {
    if(w<=0||h<=0)return;
    int x0=X(x),y0=Y(y),x1=X(x+w),y1=Y(y+h);
    if(x0<0)x0=0; if(y0<0)y0=0;
    if(x1>(int)fb->width)x1=(int)fb->width;
    if(y1>(int)fb->height)y1=(int)fb->height;
    uint32_t c=encode(rgb);
    for(int yy=y0;yy<y1;yy++){
        volatile uint32_t *row=(volatile uint32_t*)((volatile uint8_t*)fb->address+(size_t)yy*fb->pitch);
        for(int xx=x0;xx<x1;xx++)row[xx]=c;
    }
}

/* Anti-aliased Inter glyph rasterizer.
   Glyph bitmaps are generated during build from Ubuntu's OFL-licensed Inter.
   No Apple fonts or font files are embedded in the repository. */
static void color_pixel(int x,int y,uint32_t rgb,unsigned alpha){
    if(alpha==0)return;
    int x0=X(x),y0=Y(y),x1=X(x+1),y1=Y(y+1);
    if(x0<0)x0=0;if(y0<0)y0=0;
    if(x1>(int)fb->width)x1=(int)fb->width;
    if(y1>(int)fb->height)y1=(int)fb->height;
    if(x0>=x1||y0>=y1)return;
    uint32_t target=encode(rgb);
    if(alpha>=255){
        for(int py=y0;py<y1;py++){
            volatile uint32_t *line=(volatile uint32_t*)((volatile uint8_t*)fb->address+(size_t)py*fb->pitch);
            for(int px=x0;px<x1;px++)line[px]=target;
        }
        return;
    }
    uint32_t tr=(rgb>>16)&255,tg=(rgb>>8)&255,tb=rgb&255;
    const unsigned mr=(1u<<fb->red_mask_size)-1,mg=(1u<<fb->green_mask_size)-1,mb=(1u<<fb->blue_mask_size)-1;
    for(int py=y0;py<y1;py++){
        volatile uint32_t *line=(volatile uint32_t*)((volatile uint8_t*)fb->address+(size_t)py*fb->pitch);
        for(int px=x0;px<x1;px++){
            uint32_t v=line[px];
            uint32_t r=((v>>fb->red_mask_shift)&mr)*255/mr;
            uint32_t g=((v>>fb->green_mask_shift)&mg)*255/mg;
            uint32_t b=((v>>fb->blue_mask_shift)&mb)*255/mb;
            r=(r*(255-alpha)+tr*alpha)/255;
            g=(g*(255-alpha)+tg*alpha)/255;
            b=(b*(255-alpha)+tb*alpha)/255;
            line[px]=encode((r<<16)|(g<<8)|b);
        }
    }
}
static const uint8_t *font_advance_for(int size){
    return size==1?font_tiny_advance:size==2?font_regular_advance:font_title_advance;
}
static int font_height_for(int size){
    return size==1?FONT_TINY_H:size==2?FONT_REGULAR_H:FONT_TITLE_H;
}
static int font_width_for(int size){
    return size==1?FONT_TINY_W:size==2?FONT_REGULAR_W:FONT_TITLE_W;
}
static const uint8_t *font_bitmap_for(int size,int glyph){
    return size==1?font_tiny_data[glyph]:size==2?font_regular_data[glyph]:font_title_data[glyph];
}
static int text_width(const char *s,int size){
    const uint8_t *adv=font_advance_for(size);
    int width=0;
    for(;*s;s++){unsigned char c=(unsigned char)*s;if(c>=32&&c<127)width+=adv[c-32];}
    return width;
}
static void text(int x,int y,const char *s,int size,uint32_t color){
    const uint8_t *advance=font_advance_for(size);
    int gw=font_width_for(size), gh=font_height_for(size);
    for(;*s;s++){
        unsigned char c=(unsigned char)*s;
        if(c<32||c>126)continue;
        int glyph=c-32;
        const uint8_t *pixels=font_bitmap_for(size,glyph);
        for(int row=0;row<gh;row++){
            for(int col=0;col<gw;col++){
                int p=row*gw+col;
                uint8_t packed=pixels[p/2];
                unsigned alpha=((p&1)?(packed&15):(packed>>4))*17u;
                if(alpha)color_pixel(x+col,y+row,color,alpha);
            }
        }
        x+=advance[glyph];
    }
}
static void number(int x,int y,unsigned int n,int scale,uint32_t c){
    char tmp[12];int k=0;do{tmp[k++]=(char)('0'+n%10);n/=10;}while(n&&k<11);
    char buf[12];for(int i=0;i<k;i++)buf[i]=tmp[k-1-i];buf[k]=0;
    text(x,y,buf,scale,c);
}

static int night=0;
static char note[74]="HELLO FROM NOVA!";
static unsigned int note_length=16;
static const char *apps[]={"WELCOME","FINDER","NOTES","ARCADE","SETTINGS","ABOUT","TERMINAL"};
static int window_visible=1, file_open=-1;
static uint64_t usable_memory_mb=0;
static char shell_input[64]="";
static unsigned shell_len=0;
static char shell_output[3][100]={"NOVA SHELL 0.5","TYPE HELP FOR COMMANDS",""};
static const char *ram_labels[]={"NOTES.TXT","README.TXT","TODO.TXT"};

static void circle(int cx,int cy,int rx,int ry,uint32_t color) {
    if(rx<=0||ry<=0)return;
    int y0=Y(cy-ry),y1=Y(cy+ry),x0=X(cx-rx),x1=X(cx+rx);
    int midx=(x0+x1)/2,midy=(y0+y1)/2;
    int halfx=(x1-x0)/2,halfy=(y1-y0)/2;
    if(halfx==0||halfy==0)return;
    int64_t a=(int64_t)halfx*halfx,b=(int64_t)halfy*halfy;
    uint32_t c=encode(color);
    for(int y=y0<0?0:y0;y<y1 && y<(int)fb->height;y++) {
        int64_t dy=y-midy;
        volatile uint32_t *line=(volatile uint32_t*)((volatile uint8_t*)fb->address+(size_t)y*fb->pitch);
        for(int x=x0<0?0:x0;x<x1 && x<(int)fb->width;x++) {
            int64_t dx=x-midx;
            if(dx*dx*b+dy*dy*a<=a*b)line[x]=c;
        }
    }
}
static void roundrect(int x,int y,int w,int h,int r,uint32_t c) {
    if(r<=0){rect(x,y,w,h,c);return;}
    if(r>w/2)r=w/2;if(r>h/2)r=h/2;
    rect(x+r,y,w-2*r,h,c);
    rect(x,y+r,r,h-2*r,c);
    rect(x+w-r,y+r,r,h-2*r,c);
    circle(x+r,y+r,r,r,c);
    circle(x+w-r,y+r,r,r,c);
    circle(x+r,y+h-r,r,r,c);
    circle(x+w-r,y+h-r,r,r,c);
}
static uint32_t mix(uint32_t a,uint32_t b,int t,int steps){
    int ar=(int)((a>>16)&255),ag=(int)((a>>8)&255),ab=(int)(a&255);
    int br=(int)((b>>16)&255),bg=(int)((b>>8)&255),bb=(int)(b&255);
    return (uint32_t)((ar+(br-ar)*t/steps)<<16)| (uint32_t)((ag+(bg-ag)*t/steps)<<8)| (uint32_t)(ab+(bb-ab)*t/steps);
}

static void wallpaper(void){
    // Original colorful, layered-ribbon panorama (no Apple assets).
    uint32_t top=night?0x141738:0x183F70;
    uint32_t bot=night?0x6D4DAD:0x8CB8F2;
    for(int y=0;y<800;y+=2)rect(0,y,1280,2,mix(top,bot,y,800));
    circle(1090,135,710,470,night?0x373473:0x3D68B2);
    circle(1170,25,560,377,night?0x775CA8:0xA9A4F2);
    circle(1240,-78,370,255,night?0xC083B7:0xE9BCE4);
    circle(680,960,1060,600,night?0x2B438E:0x376DB4);
    circle(420,965,925,565,night?0x405AB7:0x77A2E4);
    circle(110,935,750,480,night?0x907BC6:0xC1C2F0);
    circle(-245,900,640,440,night?0xBF83C2:0xECB7D6);
    circle(-182,868,460,308,night?0xE1AED9:0xFCE0E9);
    circle(1230,815,400,215,night?0x7058BB:0x9E9CE4);
}
static void alpha_rect(int x,int y,int w,int h,uint32_t rgb,int a){
    if(a<=0)return; if(a>=255){rect(x,y,w,h,rgb);return;}
    int x0=X(x),y0=Y(y),x1=X(x+w),y1=Y(y+h);
    if(x0<0)x0=0;if(y0<0)y0=0;
    if(x1>(int)fb->width)x1=(int)fb->width;
    if(y1>(int)fb->height)y1=(int)fb->height;
    int cr=(rgb>>16)&255,cg=(rgb>>8)&255,cb=rgb&255;
    unsigned mr=(1u<<fb->red_mask_size)-1,mg=(1u<<fb->green_mask_size)-1,mb=(1u<<fb->blue_mask_size)-1;
    if(!mr||!mg||!mb)return;
    for(int yy=y0;yy<y1;yy++){
        volatile uint32_t *line=(volatile uint32_t*)((volatile uint8_t*)fb->address+(size_t)yy*fb->pitch);
        for(int xx=x0;xx<x1;xx++){
            uint32_t v=line[xx];
            int r=((v>>fb->red_mask_shift)&mr)*255/(int)mr;
            int g=((v>>fb->green_mask_shift)&mg)*255/(int)mg;
            int b=((v>>fb->blue_mask_shift)&mb)*255/(int)mb;
            r=(r*(255-a)+cr*a)/255;
            g=(g*(255-a)+cg*a)/255;
            b=(b*(255-a)+cb*a)/255;
            line[xx]=encode(((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b);
        }
    }
}
static void shadow(int x,int y,int w,int h,int rad){
    for(int i=15;i>1;i-=3)
        roundrect(x-i/2,y+i/2,w+i,h+i,rad+i/2,night?0x233052:0x5B658D);
}
static void bar(void){
    alpha_rect(0,0,1280,36,night?0x14172A:0xFFFFFF,night?220:202);
    rect(0,36,1280,1,night?0x374469:0xDDE3F0);
    roundrect(21,7,22,22,7,night?0xEDF1FF:0x293F72);
    text(27,12,"N",2,night?0x31427A:0xFFFFFF);
    text(61,13,screen==1?"Finder":apps[screen],2,night?0xFFFFFF:0x1F2B40);
    text(166,16,"File     Edit     View     Go     Window     Help",1,night?0xDCE7FC:0x2C384F);
    // Compact original status glyphs.
    circle(1045,14,11,9,night?0xEDF1FF:0x405978);
    circle(1045,13,7,6,night?0x2A375B:0xEAF0F9);
    circle(1045,16,3,3,night?0xEDF1FF:0x405978);
    roundrect(1072,10,27,13,3,night?0xE9EFF9:0x395873);
    rect(1100,14,3,5,night?0xE9EFF9:0x395873);
    rect(1075,13,21,7,night?0x435674:0xE9EFF9);
    text(1120,14,"Sat  9:41 AM",1,night?0xFFFFFF:0x28374D);
}


static void appicon(int x,int y,int i,int selected){
    static const uint32_t cols[]={0x5299E6,0x4E9FE9,0xF0C65B,0x8A7BDC,0xA2AABB,0x566B9B};
    int w=58;
    roundrect(x+2,y+4,w,w,13,night?0x323E60:0x5B6386);
    roundrect(x,y,w,w,13,cols[i]);
    if(i==0){
        circle(x+29,y+28,20,20,0xEDF5FF);
        text(x+23,y+18,"N",3,0x5489D3);
    }else if(i==1){
        roundrect(x+9,y+9,40,40,9,0xF0F9FF);
        rect(x+9,y+9,20,40,0xA6D8FB);
        rect(x+28,y+18,2,17,0x3A77AA);
        rect(x+19,y+20,3,6,0x3A77AA);
        rect(x+38,y+20,3,6,0x3A77AA);
        rect(x+20,y+37,18,2,0x3A77AA);
    }else if(i==2){
        roundrect(x+12,y+10,34,39,5,0xFFFCF2);
        rect(x+12,y+10,34,12,0xFFE18E);
        for(int t=0;t<3;t++)rect(x+18,y+28+t*7,22,2,0xBFC0C6);
    }else if(i==3){
        roundrect(x+8,y+17,42,29,11,0xF2ECFF);
        rect(x+20,y+24,5,16,0x8173C8);
        rect(x+15,y+29,16,5,0x8173C8);
        circle(x+40,y+26,4,4,0xE86B9C);
        circle(x+35,y+36,4,4,0x81D5AE);
    }else if(i==4){
        circle(x+29,y+29,20,20,0xF5F8FB);
        circle(x+29,y+29,14,14,0x8993AA);
        circle(x+29,y+29,8,8,0xDFE6F1);
    }else{
        circle(x+29,y+29,19,19,0xF4F6FF);
        text(x+24,y+20,"I",3,0x526C9A);
    }
    if(selected)circle(x+29,y+69,3,3,night?0xFFFFFF:0x334363);
}
static void dock(void){
    shadow(311,692,660,88,22);
    roundrect(307,688,665,96,23,night?0x8B8DB9:0x9DA8D1);
    roundrect(313,693,653,85,20,night?0x2D3559:0xE4E9FA);
    alpha_rect(313,694,653,83,night?0xCBD5EF:0xFFFFFF,night?60:82);
    rect(326,695,627,2,night?0x66709A:0xFFFFFF);
    for(int i=0;i<6;i++)appicon(346+i*101,704,i,i==screen);
}


static void deskicon(int x,int y,int i,const char *label){
    appicon(x,y,i,0);
    roundrect(x-12,y+69,85,16,4,night?0x243A68:0x536B99);
    text(x-2,y+73,label,1,0xFFFFFF);
}

static uint32_t ink(void){return night?0xE8ECFA:0x24334E;}
static uint32_t sub(void){return night?0xAEBEDA:0x657695;}
static uint32_t panel(void){return night?0x212B42:0xFFFFFF;}

static void window_draw(void){
    shadow(159,78,944,552,15);
    roundrect(156,76,947,558,17,night?0x242B45:0xFFFFFF);
    roundrect(156,76,947,58,17,night?0x343C58:0xF7F8FC);
    rect(156,110,947,25,night?0x343C58:0xF7F8FC);
    circle(181,99,7,7,0xFF5F58);
    circle(204,99,7,7,0xFEBE2E);
    circle(227,99,7,7,0x29C840);
    text(480,99,apps[screen],2,night?0xEDF3FF:0x273248);
    rect(156,134,947,1,night?0x454D68:0xE4E9F0);
    rect(156,135,208,499,night?0x2E354C:0xE8EBF4);
    // Sidebar: Finder-like navigation, sections and colored location dots.
    text(175,155,"FAVORITES",1,night?0x9DACCA:0x929BAB);
    const char *nav[]={"WELCOME","FINDER","NOTES","ARCADE","SETTINGS","ABOUT"};
    static const uint32_t hue[]={0x7F9FE6,0x57A8F1,0xE9B85A,0x9788E6,0x94ABC7,0x7A95C6};
    for(int i=0;i<6;i++){
        int y=181+i*57;
        if(screen==i)roundrect(164,y-7,193,39,9,night?0x4B5975:0xD0D6E3);
        circle(183,y+11,9,9,hue[i]);
        text(179,y+7,i==3?"+":" ",1,0xFFFFFF);
        text(205,y+8,nav[i],1,screen==i?(night?0xFFFFFF:0x253451):(night?0xCFD6E8:0x5A6376));
    }
    text(174,559,"TOOLS",1,night?0x9DACCA:0x929BAB);
    circle(181,589,6,6,0x6AA6EC);
    text(198,586,"TERMINAL  [7]",1,night?0xD9E0F2:0x6A7488);
    // Toolbar divider and understated chrome.
    rect(364,135,1,499,night?0x47536D:0xDBE0EB);
    rect(365,135,738,42,night?0x292F49:0xFDFEFF);
    text(387,148,"<    >",2,night?0xABBDDE:0x8C9CAE);
    roundrect(828,142,249,28,9,night?0x3B4862:0xEDF0F6);
    circle(847,156,6,6,night?0xB4C0D1:0x8B9BAB);
    text(864,153,"Search",1,night?0xB4C0D1:0x8B9BAB);
    rect(365,177,738,1,night?0x47536D:0xEFF1F7);
}
static void folder(int x,int y,const char *name){
    roundrect(x,y+18,91,66,11,0x5897DA);
    roundrect(x,y+9,48,22,8,0x73B5F1);
    roundrect(x+1,y+23,89,59,9,0x6EB4F1);
    roundrect(x+7,y+27,77,49,7,0x90C9F7);
    text(x+2,y+94,name,1,night?0xE6EDFC:0x475872);
}
static void app_content(void){
    uint32_t fg=ink(),secondary=sub();
    uint32_t pale=night?0x323E59:0xF3F6FB;
    if(screen==1){
        text(397,197,"NOVA RAM FILES",2,fg);
        text(397,231,"RAM DISK   /   HOME",1,secondary);
        rect(394,253,678,1,night?0x3D4661:0xE8ECF2);
        if(file_open<0){
            for(int i=0;i<3;i++){
                int x=410+i*214;
                roundrect(x,281,185,183,15,pale);
                folder(x+44,293,ram_labels[i]);
                text(x+29,414,ram_labels[i],1,fg);
                text(x+25,444,"RAM ONLY",1,secondary);
            }
            rect(394,492,678,1,night?0x3D4661:0xE8ECF2);
            text(399,514,"Click a file to read it. Notes can be edited.",1,secondary);
        }else{
            text(412,276,"< BACK",2,night?0xD4E5FF:0x427BC6);
            text(413,327,nova_files[file_open].name,2,fg);
            roundrect(406,368,654,148,12,pale);
            text(426,399,nova_files[file_open].data,1,fg);
            if(file_open==0)text(426,461,"Open Notes to edit this file.",1,secondary);
            text(406,552,"RAM DISK - CHANGES ARE LOST AT REBOOT",1,secondary);
        }
        roundrect(396,571,672,36,9,pale);
        text(410,583,"FILES: 3       RAM-ONLY VFS      NO DISK DRIVER",1,secondary);
    }else if(screen==0){
        text(402,197,"Welcome back.",3,fg);
        text(405,240,"Your personal workspace. Built from scratch.",1,secondary);
        roundrect(395,272,670,131,19,night?0x39547E:0xDBE9FC);
        roundrect(418,294,65,65,14,0x6090E6);
        circle(450,325,21,21,0xE8F2FF);
        text(443,314,"N",3,0x648ECA);
        text(507,299,"NOVA OS",3,night?0xFFFFFF:0x2D4F9A);
        text(510,354,"Your desktop. Your rules.",1,night?0xD2DFF9:0x637BA4);
        roundrect(396,421,321,139,16,pale);
        roundrect(736,421,329,139,16,pale);
        text(420,446,"Finder",2,fg);
        text(420,483,"Explore NOVA's desktop.",1,secondary);
        text(420,507,"Press 2 to open.",1,secondary);
        text(759,446,"Arcade",2,fg);
        text(759,483,"Collect the golden tile.",1,secondary);
        text(759,507,"Press 4 to play.",1,secondary);
        text(401,590,"PROTOTYPE  -  NOT READY TO REPLACE WINDOWS",1,night?0xE8B2C3:0xAE5772);
    }else if(screen==2){
        text(398,197,"NOTES",2,fg);
        text(399,232,"A little space for your thoughts.",1,secondary);
        roundrect(396,263,670,330,13,night?0x463F37:0xFFFBF1);
        rect(396,263,670,19,night?0x9B804B:0xF5DC89);
        text(419,298,"UNTITLED",2,night?0xFAE3A6:0x87734E);
        rect(416,329,628,1,night?0x7A715E:0xE8DCC7);
        text(416,355,note,2,night?0xF8ECD4:0x554E45);
        int caret=416+text_width(note,2);
        if(caret>1030)caret=1030;
        rect(caret,374,2,18,night?0xF7E1AF:0xB18F60);
        text(402,607,"AUTO-SAVED TO NOTES.TXT IN RAM  -  ERASED ON REBOOT",1,secondary);
    }else if(screen==3){
        text(401,191,"ARCADE",3,fg);
        text(402,231,"WASD OR ARROWS TO MOVE  -  COLLECT GOLD",1,secondary);
        roundrect(393,257,679,338,16,pale);
        for(int i=0;i<48;i++){
            int x=429+(i%8)*78, y=272+(i/8)*48;
            roundrect(x,y,69,41,9,i==player?0x607EE3:(i==coin?0xE6B75C:(night?0x4B5976:0xD9E5F7)));
            if(i==player)roundrect(x+24,y+10,21,21,6,0xF3F6FF);
            if(i==coin)circle(x+34,y+20,11,11,0xFFF3C4);
        }
        text(409,610,"SCORE:",1,fg);
        number(462,607,(unsigned int)score,2,night?0xF4D38A:0x5275B9);
    }else if(screen==4){
        text(399,192,"SYSTEM SETTINGS",2,fg);
        text(400,233,"Customize the way NOVA looks.",1,secondary);
        roundrect(396,264,672,327,17,pale);
        text(419,290,"APPEARANCE",2,fg);
        roundrect(423,336,297,172,14,0xE2EDFC);
        roundrect(742,336,297,172,14,0x353D65);
        circle(614,421,88,73,0xA4B9E9);
        circle(940,418,88,73,0x7562AF);
        text(437,480,"LIGHT",1,0x3C588B);
        text(757,480,"DARK",1,0xEDF1FF);
        text(421,551,"Press T to toggle appearance.",1,secondary);
    }else if(screen==5){
        text(398,191,"ABOUT THIS NOVA",2,fg);
        text(399,230,"An independent operating system project.",1,secondary);
        roundrect(396,260,673,327,17,pale);
        roundrect(422,293,74,74,17,0x5B8BE0);
        circle(459,330,24,24,0xE5F0FE);
        text(449,318,"N",4,0x5787CB);
        text(519,297,"NOVA OS",3,fg);
        text(522,352,"NATIVE 0.3  -  X86-64",1,secondary);
        rect(423,403,613,1,night?0x53617B:0xD6DEED);
        text(423,430,"KERNEL         NOVA NATIVE",1,fg);
        text(423,457,"BOOTLOADER     LIMINE",1,fg);
        text(423,484,"GRAPHICS       FRAMEBUFFER",1,fg);
        text(423,511,"INPUT          KEYBOARD + PS2 MOUSE",1,fg);
        text(423,550,"Independent. Ad-free. Early preview.",1,secondary);
        text(405,608,"1-7: APPS   ESC: HOME   T: THEME   MOUSE: CLICK",1,secondary);

    }else if(screen==6){
        text(402,192,"TERMINAL",3,fg);
        text(402,236,"A tiny command interpreter running in kernel mode.",1,secondary);
        roundrect(398,262,668,324,15,night?0x101823:0x202C40);
        text(416,283,"NOVA NATIVE TERMINAL 0.5",1,0x7DE2AB);
        text(417,337,shell_output[0],1,0xE3EDF9);
        text(417,375,shell_output[1],1,0xE3EDF9);
        text(417,413,shell_output[2],1,0xE3EDF9);
        text(418,477,">",2,0x6FE7AC);
        text(446,477,shell_input,2,0xE1EEFA);
        text(406,608,"TYPE HELP, LS, CAT NOTES.TXT, MEM OR UNAME.",1,secondary);
        if(nova_file_equal(shell_output[2],"RAM DETECTED (MIB):"))number(645,412,(unsigned)usable_memory_mb,1,0xA5E4B9);
    }
}

static void desktop(void){
    wallpaper();
    deskicon(1166,92,1,"FILES");
    deskicon(1166,214,2,"NOTES");
    deskicon(1166,336,3,"ARCADE");
    bar();
    if(window_visible){window_draw();app_content();}
    dock();
}
static char letter(uint8_t sc){
    static const char keys[128]={
        [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',[0x15]='Y',[0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',
        [0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',[0x23]='H',[0x24]='J',[0x25]='K',[0x26]='L',
        [0x2C]='Z',[0x2D]='X',[0x2E]='C',[0x2F]='V',[0x30]='B',[0x31]='N',[0x32]='M',
        [0x39]=' ',[0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',
        [0x06]='5',[0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',
        [0x34]='.',[0x35]='/',[0x0C]='-'
    };
    return sc<128?keys[sc]:0;
}
static int cursor_drawn=0;
static void cursor_paint(void);
static void redraw(void){cursor_drawn=0;desktop();cursor_paint();}
static void move(int dx,int dy){
    int x=player%8+dx,y=player/8+dy;
    if(x<0)x=7;if(x>=8)x=0;if(y<0)y=5;if(y>=6)y=0;
    player=y*8+x;
    if(player==coin){score++;coin=(coin+13)%48;if(coin==player)coin=(coin+7)%48;}
}

static void serial_out(uint16_t p,uint8_t b){__asm__ volatile("outb %0,%1"::"a"(b),"Nd"(p));}
static void serial_init(void){
    serial_out(0x3F9,0x00);
    serial_out(0x3FB,0x80);
    serial_out(0x3F8,0x03);
    serial_out(0x3F9,0x00);
    serial_out(0x3FB,0x03);
    serial_out(0x3FA,0xC7);
    serial_out(0x3FC,0x0B);
}
static void serial_log(const char *s){
    for(;*s;s++){
        for(int i=0;i<20000 && !(inb(0x3FD)&0x20);i++){}
        serial_out(0x3F8,(uint8_t)*s);
    }
}
#define CURSOR_W 16
#define CURSOR_H 23
static uint32_t cursor_under[CURSOR_W*CURSOR_H];
static int cursor_x=0,cursor_y=0;
static void cursor_hide(void){
    if(!cursor_drawn)return;
    for(int yy=0;yy<CURSOR_H;yy++){
        int py=cursor_y+yy;
        if(py<0||py>=(int)fb->height)continue;
        volatile uint32_t *row=(volatile uint32_t*)((volatile uint8_t*)fb->address+(size_t)py*fb->pitch);
        for(int xx=0;xx<CURSOR_W;xx++){
            int px=cursor_x+xx;
            if(px>=0&&px<(int)fb->width)row[px]=cursor_under[yy*CURSOR_W+xx];
        }
    }
    cursor_drawn=0;
}
static void cursor_paint(void){
    cursor_x=X(mouse.x);
    cursor_y=Y(mouse.y);
    for(int yy=0;yy<CURSOR_H;yy++){
        int py=cursor_y+yy;
        if(py<0||py>=(int)fb->height)continue;
        volatile uint32_t *row=(volatile uint32_t*)((volatile uint8_t*)fb->address+(size_t)py*fb->pitch);
        for(int xx=0;xx<CURSOR_W;xx++){
            int px=cursor_x+xx;
            if(px<0||px>=(int)fb->width)continue;
            uint32_t old=row[px];
            cursor_under[yy*CURSOR_W+xx]=old;
            int inside=(yy<17 && xx<=yy/2 && xx<=11) ||
                       (yy>=13 && yy<23 && xx>=5 && xx<=8 && yy<=xx+14);
            if(inside){
                int edge=(xx==0 || xx==yy/2 || yy==0 || yy==16);
                row[px]=encode(edge?0x182235:0xFFFFFF);
            }
        }
    }
    cursor_drawn=1;
}
static void shell_copy(char *dest,size_t cap,const char *src){
    size_t i=0;
    for(;src[i]&&i+1<cap;i++)dest[i]=src[i];
    dest[i]=0;
}
static void shell_result(const char *message){
    shell_copy(shell_output[0],sizeof(shell_output[0]),shell_output[1]);
    shell_copy(shell_output[1],sizeof(shell_output[1]),shell_output[2]);
    shell_copy(shell_output[2],sizeof(shell_output[2]),message);
}
static void shell_execute(void){
    if(nova_file_equal(shell_input,"HELP"))shell_result("HELP LS CAT NOTES.TXT CAT README.TXT CAT TODO.TXT");
    else if(nova_file_equal(shell_input,"LS"))shell_result("NOTES.TXT   README.TXT   TODO.TXT");
    else if(nova_file_equal(shell_input,"UNAME"))shell_result("NOVA-OS NATIVE 0.5 X86_64");
    else if(nova_file_equal(shell_input,"MEM")){
        shell_result("RAM DETECTED (MIB):");
        // Memory number is rendered directly in the terminal panel.
    } else if(nova_file_equal(shell_input,"CLEAR")){
        for(int i=0;i<3;i++)shell_output[i][0]=0;
    } else if(shell_input[0]=='C' && shell_input[1]=='A' &&
              shell_input[2]=='T' && shell_input[3]==' '){
        struct nova_file *f=nova_file_find(shell_input+4);
        shell_result(f?f->data:"FILE NOT FOUND");
    } else shell_result("UNKNOWN COMMAND - TYPE HELP");
    shell_len=0;shell_input[0]=0;
}
static void open_app(int app){
    if(app<0||app>6)return;
    window_visible=1;
    screen=app;
    serial_log("NOVA_APP_OPEN\n");
}
static void mouse_click(int x,int y){
    // Floating dock.
    if(y>=694&&y<=780){
        for(int i=0;i<6;i++)if(x>=341+i*101 && x<=410+i*101){
            open_app(i);return;
        }
    }
    // Desktop shortcut icons on the right.
    if(x>=1153&&x<=1240){
        if(y>=92&&y<187){open_app(1);return;}
        if(y>=214&&y<309){open_app(2);return;}
        if(y>=336&&y<431){open_app(3);return;}
    }
    // Menu button restores the Welcome app.
    if(y<38 && x<150){open_app(0);return;}
    if(!window_visible)return;
    if(y>=87&&y<117){
        if(x>=170&&x<192){window_visible=0;return;}
        if(x>=194&&x<218){window_visible=0;return;}
        if(x>=221&&x<243){open_app(1);return;} // Green: switch to Finder
    }
    for(int i=0;i<6;i++){
        int yy=181+i*57;
        if(x>=164&&x<=356 && y>=yy-7&&y<=yy+33){open_app(i);return;}
    }
    if(x>=169&&x<=355&&y>=570&&y<=613){open_app(6);return;}
    if(screen==4&&y>=336&&y<=510){
        if(x>=423&&x<=722)night=0;
        if(x>=742&&x<=1041)night=1;
    }
    if(screen==1){
        if(file_open>=0){
            if(x>=390&&x<540&&y>=270&&y<325)file_open=-1;
            return;
        }
        if(y>=273&&y<=468){
            for(int i=0;i<3;i++)if(x>=410+i*214&&x<=595+i*214){
                file_open=i;return;
            }
        }
    }
    if(screen==0){
        if(y>=421&&y<=560&&x>=395&&x<=717){open_app(1);return;}
        if(y>=421&&y<=560&&x>=736&&x<=1065){open_app(3);return;}
    }
}
static void keyboard_scancode(uint8_t key){
    static bool extended=false;
    if(key==0xE0){extended=true;return;}
    if(key&0x80){extended=false;return;}
    int changed=0;
    if(key==0x01){open_app(0);changed=1;}
    if(screen!=2 && screen!=6 && key>=0x02 && key<=0x08){
        open_app((int)key-0x02);changed=1;
    }
    if(key==0x14 && screen!=2&&screen!=6){night=!night;changed=1;}
    if(screen==2 && key!=0x01) {
        if(key==0x0E && note_length>0){note[--note_length]=0;changed=1;}
        else {
            char c=letter(key);
            if(c && note_length<62){note[note_length++]=c;note[note_length]=0;changed=1;}
        }
        if(changed)nova_file_write("NOTES.TXT",note,note_length);
    }
    if(screen==6 && key!=0x01) {
        if(key==0x0E && shell_len>0){shell_input[--shell_len]=0;changed=1;}
        else if(key==0x1C){shell_execute();changed=1;}
        else{
            char c=letter(key);
            if(c&&shell_len<49){shell_input[shell_len++]=c;shell_input[shell_len]=0;changed=1;}
        }
    }
    if(screen==3){
        int prev=player;
        if(key==0x1E || (extended&&key==0x4B))move(-1,0);
        if(key==0x20 || (extended&&key==0x4D))move(1,0);
        if(key==0x11 || (extended&&key==0x48))move(0,-1);
        if(key==0x1F || (extended&&key==0x50))move(0,1);
        if(prev!=player)changed=1;
    }
    extended=false;
    if(changed)redraw();
}
static void input_loop(void){
    bool was_pressed=false;
    for(;;){
        uint8_t status=inb(0x64);
        if(!(status&1)){__asm__ volatile("pause");continue;}
        uint8_t value=inb(0x60);
        if(status&0x20){
            if(nova_mouse_byte(value)){
                cursor_hide();
                if(mouse.left&&!was_pressed){
                    mouse_click(mouse.x,mouse.y);
                    redraw();
                    serial_log("NOVA_MOUSE_CLICK\n");
                }
                was_pressed=mouse.left;
                if(!cursor_drawn)cursor_paint();
            }
        }else keyboard_scancode(value);
    }
}

void kmain(void){
    if(!LIMINE_BASE_REVISION_SUPPORTED(base_revision))idle();
    if(!fb_request.response||fb_request.response->framebuffer_count<1)idle();
    fb=fb_request.response->framebuffers[0];
    if(!fb||fb->bpp!=32||fb->memory_model!=LIMINE_FRAMEBUFFER_RGB||fb->width<800||fb->height<600)idle();
    serial_init();
    if(nova_memmap_request.response){
        for(uint64_t i=0;i<nova_memmap_request.response->entry_count;i++){
            struct limine_memmap_entry *entry=nova_memmap_request.response->entries[i];
            if(entry->type==LIMINE_MEMMAP_USABLE)
                usable_memory_mb+=entry->length/1048576u;
        }
    }
    serial_log("NOVA_KERNEL_BOOT_OK\n");
    if(nova_mouse_init())serial_log("NOVA_PS2_MOUSE_ENABLED\n");
    else serial_log("NOVA_PS2_MOUSE_UNAVAILABLE\n");
    struct nova_file *initial=nova_file_find("NOTES.TXT");
    if(initial)serial_log("NOVA_RAMFS_READY\n");
    redraw();input_loop();idle();
}
