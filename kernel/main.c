// NOVA OS Native 0.1: independent freestanding 64-bit kernel (not Linux).
// Boots via the Limine protocol. PS/2 input is intended only for virtual machines.
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limine.h>
#include "font.h"

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static struct limine_framebuffer *fb;
static int screen = 0, player = 9, coin = 22, score = 0;
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
static void text(int x,int y,const char *s,int scale,uint32_t col) {
    for(;*s;s++,x+=6*scale) {
        unsigned char c=(unsigned char)*s;
        if(c>='a' && c<='z')c=(unsigned char)(c-32); if(c<32||c>126)continue;
        for(int row=0;row<7;row++) for(int px=0;px<5;px++)
            if(glyphs[c-32][row]&(1u<<(4-px)))
                rect(x+px*scale,y+row*scale,scale,scale,col);
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
static const char *apps[]={"WELCOME","FINDER","NOTES","ARCADE","SETTINGS","ABOUT"};

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
    roundrect(178,100,930,571,22,night?0x0F1527:0x647CA7);
    roundrect(174,92,932,571,22,panel());
    roundrect(174,92,932,56,21,night?0x2C3853:0xFBFCFF);
    rect(174,122,932,27,night?0x2C3853:0xFBFCFF);
    circle(201,119,8,8,0xFF625E);
    circle(226,119,8,8,0xFFBF50);
    circle(251,119,8,8,0x35CA64);
    text(560,113,apps[screen],2,ink());
    rect(174,149,932,1,night?0x3D465E:0xE4EAF4);
    rect(174,150,202,513,night?0x2A3652:0xE9EFF8);
    text(197,174,"FAVORITES",1,sub());
    const char *titles[]={"WELCOME","FINDER","NOTES","ARCADE","SETTINGS","ABOUT"};
    for(int i=0;i<6;i++){
        int y=207+i*64;
        if(screen==i)roundrect(187,y-8,177,45,11,night?0x43557E:0xCADAF6);
        roundrect(200,y,28,28,8,screen==i?(night?0x7399EA:0x5D82D7):(night?0x405171:0xBCCCE7));
        text(207,y+7,"*",1,night?0xFAFCFF:0xFFFFFF);
        text(239,y+8,titles[i],1,screen==i?ink():sub());
    }
    text(199,625,"NOVA 0.2",1,sub());
}
static void app_content(void){
    uint32_t fg=ink(),secondary=sub(),white=panel();
    if(screen==0){
        text(414,190,"Good to see you.",3,fg);
        text(415,240,"Welcome to your independent desktop.",1,secondary);
        roundrect(414,278,651,142,19,night?0x36476C:0xDFE9FC);
        roundrect(441,307,49,49,13,0x6589E0);
        text(455,323,"N",2,0xFFFFFF);
        text(513,303,"NOVA OS",3,night?0xEEF3FF:0x3457AB);
        text(514,359,"Your own kernel. Your own future.",1,night?0xD9E4FC:0x5D7297);
        roundrect(414,437,314,151,18,night?0x303E5B:0xF3F6FC);
        roundrect(745,437,320,151,18,night?0x303E5B:0xF3F6FC);
        text(440,460,"DESKTOP",2,fg);
        text(440,499,"A clean place to build.",1,secondary);
        text(440,527,"Press 2 for Finder.",1,secondary);
        text(771,460,"ARCADE",2,fg);
        text(771,499,"A game built in the kernel.",1,secondary);
        text(771,527,"Press 4 to play.",1,secondary);
        text(415,623,"EXPERIMENTAL KERNEL - NOT READY TO REPLACE WINDOWS",1,night?0xF6B2C5:0xAA6077);
    }else if(screen==1){
        text(414,184,"Finder",3,fg);
        text(414,231,"Browse your NOVA system.",1,secondary);
        roundrect(414,271,649,290,15,night?0x303E5B:0xF2F5FB);
        roundrect(441,299,71,59,12,0x73B0F4);
        rect(452,322,49,24,0xE5F6FF);
        text(535,311,"NOVA SYSTEM",2,fg);
        text(536,355,"Boot files and storage are not available yet.",1,secondary);
        rect(445,390,578,1,night?0x4A5772:0xD8E0EF);
        text(447,420,"RECENT FILES",2,fg);
        text(447,463,"No files available in this preview.",1,secondary);
        text(447,513,"A filesystem driver is still being developed.",1,secondary);
    }else if(screen==2){
        text(415,182,"Notes",3,fg);
        text(414,230,"Local scratchpad - RAM only, resets on reboot.",1,secondary);
        roundrect(413,268,651,302,14,night?0x49412D:0xFFF8E7);
        rect(438,315,595,1,night?0x786741:0xE3D6B7);
        text(443,289,"UNTITLED NOTE",2,night?0xF9E9BA:0x6A572E);
        text(443,351,note,2,night?0xF9E9BA:0x665237);
        rect(444+(int)note_length*12,378,12,2,night?0xF9E9BA:0x665237);
        text(419,605,"TYPE LETTERS OR SPACE. BACKSPACE DELETES.",1,secondary);
    }else if(screen==3){
        text(413,179,"NOVA Arcade",3,fg);
        text(414,231,"Use WASD or arrow keys. Collect the gold tile.",1,secondary);
        roundrect(412,261,656,338,16,night?0x2A3755:0xF3F6FC);
        for(int i=0;i<48;i++){
            int x=445+(i%8)*74, y=279+(i/8)*47;
            roundrect(x,y,65,39,7,i==player?0x627FE0:(i==coin?0xE9B85D:(night?0x465676:0xD6E3F7)));
            if(i==player)roundrect(x+21,y+9,22,22,6,0xEFF5FF);
            if(i==coin)circle(x+32,y+20,11,11,0xFFF2BB);
        }
        text(432,562,"SCORE",1,fg);
        number(499,562,(unsigned int)score,2,night?0xF5D47E:0x4268C0);
    }else if(screen==4){
        text(412,187,"System Settings",3,fg);
        text(414,236,"Personalize your NOVA desktop.",1,secondary);
        roundrect(413,276,647,294,17,night?0x303E5B:0xF3F6FC);
        text(443,310,"APPEARANCE",2,fg);
        roundrect(442,353,282,144,15,0xD5E8FF);
        roundrect(751,353,281,144,15,0x272B4E);
        circle(630,409,58,52,0x9DA9F1);
        circle(952,408,58,52,0x7461B1);
        text(452,461,"LIGHT",1,0x2F518B);
        text(767,461,"DARK",1,0xF5EFFF);
        text(443,529,"Press T to change appearance.",1,secondary);
    }else{
        text(414,183,"About NOVA",3,fg);
        text(414,236,"NOVA OS Native 0.2 - Independent Preview",1,secondary);
        roundrect(414,273,648,300,18,night?0x303E5B:0xF3F6FC);
        roundrect(441,301,76,76,17,0x4E78DD);
        text(461,324,"N",4,0xFFFFFF);
        text(541,307,"NOVA OS",3,fg);
        text(542,353,"VERSION 0.2 - X86-64",1,secondary);
        rect(442,405,592,1,night?0x53617F:0xD3DFEF);
        text(445,429,"Kernel: NOVA Native",1,fg);
        text(445,455,"Bootloader: Limine   Input: PS/2 Keyboard",1,fg);
        text(445,481,"Graphics: Software Framebuffer",1,fg);
        text(445,519,"No Linux, no Debian, no NOVA ads.",1,secondary);
        text(414,608,"Keys 1-6 switch apps. ESC goes Home. T changes theme.",1,secondary);
    }
}
static void desktop(void){
    wallpaper();
    deskicon(59,111,1,"FINDER");
    deskicon(59,235,2,"NOTES");
    deskicon(59,358,3,"ARCADE");
    bar();
    window_draw();
    app_content();
    dock();
}
static char letter(uint8_t sc){
    static const char keys[128]={
        [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',[0x15]='Y',[0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',
        [0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',[0x23]='H',[0x24]='J',[0x25]='K',[0x26]='L',
        [0x2C]='Z',[0x2D]='X',[0x2E]='C',[0x2F]='V',[0x30]='B',[0x31]='N',[0x32]='M',
        [0x39]=' '
    };
    return sc<128?keys[sc]:0;
}
static void redraw(void){desktop();}
static void move(int dx,int dy){
    int x=player%8+dx,y=player/8+dy;
    if(x<0)x=7;if(x>=8)x=0;if(y<0)y=5;if(y>=6)y=0;
    player=y*8+x;
    if(player==coin){score++;coin=(coin+13)%48;if(coin==player)coin=(coin+7)%48;}
}
static void keyboard(void){
    for(;;){
        if(!(inb(0x64)&1)){__asm__ volatile("pause");continue;}
        uint8_t key=inb(0x60);
        if(key==0xE0)continue;
        if(key&0x80)continue;
        int changed=0;
        if(key>=0x02 && key<=0x07){screen=(int)key-0x02;changed=1;}
        if(key==0x01){screen=0;changed=1;}
        if(key==0x14 && screen!=2){night=!night;changed=1;}
        if(screen==2 && key!=0x01){
            if(key==0x0E && note_length>0){note[--note_length]=0;changed=1;}
            else{
                char c=letter(key);
                if(c && note_length<62){note[note_length++]=c;note[note_length]=0;changed=1;}
            }
        }
        if(screen==3){
            int previous=player;
            if(key==0x1E || key==0x4B)move(-1,0);
            if(key==0x20 || key==0x4D)move(1,0);
            if(key==0x11 || key==0x48)move(0,-1);
            if(key==0x1F || key==0x50)move(0,1);
            if(previous!=player)changed=1;
        }
        if(changed)redraw();
    }
}
void kmain(void){
    if(!LIMINE_BASE_REVISION_SUPPORTED(base_revision))idle();
    if(!fb_request.response||fb_request.response->framebuffer_count<1)idle();
    fb=fb_request.response->framebuffers[0];
    if(!fb||fb->bpp!=32||fb->memory_model!=LIMINE_FRAMEBUFFER_RGB||fb->width<800||fb->height<600)idle();
    redraw();keyboard();idle();
}
