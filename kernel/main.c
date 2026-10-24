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
        if(c<32||c>126)continue;
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
static void window(void) {
    rect(0,0,1280,800,0x24365E);
    rect(0,490,1280,310,0x536EAB);
    rect(0,650,1280,150,0x7C85C2);
    rect(0,0,1280,43,0x15223C);
    text(21,13,"NOVA",2,0xC7DBFF);
    text(1021,16,"NATIVE 0.1",1,0xDAE7FF);
    rect(166,88,950,580,0x223556);
    rect(171,84,940,579,0xEDF2F9);
    rect(171,84,940,62,0xFFFFFF);
    rect(191,103,15,15,0xF46B6B);
    rect(218,103,15,15,0xEABB58);
    rect(245,103,15,15,0x6BC49B);
    text(571,110,names[screen],2,0x394665);
    rect(171,146,205,517,0xE2E9F5);
    text(197,173,"NOVA OS",3,0x31458A);
    for(int i=0;i<5;i++){
        int y=246+i*67;
        if(i==screen)rect(182,y-10,184,42,0xBED0F2);
        text(205,y,names[i],2,i==screen?0x243F99:0x607094);
    }
    rect(312,706,662,69,0xE9EFF9);
    for(int i=0;i<5;i++){
        int x=332+i*127;
        if(i==screen)rect(x-9,714,118,50,0xC1D3F2);
        text(x,729,names[i],2,i==screen?0x254A99:0x6D7896);
    }
}
static void content(void) {
    if(screen==0){
        text(414,200,"WELCOME TO NOVA",4,0x263B79);
        text(415,260,"THE NEXT OS BEGINS HERE.",2,0x6A7793);
        rect(413,320,633,139,0xD0DFFC);
        text(437,344,"INDEPENDENT X86-64 KERNEL",3,0x2D50A6);
        text(437,411,"NOT DEBIAN. NOT LINUX. NOT WINDOWS.",2,0x4B628D);
        text(418,503,"PRESS 1-5 TO EXPLORE.",2,0x586B94);
        text(418,590,"EARLY PREVIEW - NOT READY TO REPLACE WINDOWS",2,0xA65469);
    } else if(screen==1){
        text(412,203,"FILES",4,0x263B79);
        rect(411,270,646,260,0xFFFFFF);
        text(440,301,"STORAGE DRIVER STATUS",3,0x31529D);
        text(441,382,"PERSISTENT FILESYSTEM: NOT READY",2,0xA65469);
        text(441,440,"READ AND WRITE SUPPORT IS PLANNED.",2,0x64748F);
    } else if(screen==2){
        text(412,203,"SYSTEM",4,0x263B79);
        rect(411,270,646,285,0xFFFFFF);
        text(437,302,"KERNEL: NOVA X86-64",3,0x31529D);
        text(438,371,"BOOTLOADER: LIMINE",2,0x536582);
        text(438,407,"DISPLAY: FRAMEBUFFER",2,0x536582);
        text(438,443,"INPUT: LEGACY PS/2",2,0x536582);
        text(438,498,"NO ADS. NO TRACKING.",2,0x3B7975);
    } else if(screen==3){
        text(412,185,"NOVA ARCADE",4,0x263B79);
        text(414,233,"MOVE WITH W A S D OR ARROWS.",2,0x66748E);
        for(int i=0;i<48;i++)
            rect(423+(i%8)*76,287+(i/8)*49,69,42,
                 i==player?0x597CD5:(i==coin?0xF1C66A:0xD5E2F5));
        text(422,606,"SCORE:",2,0x304E8D);
        number(511,606,(unsigned int)score,2,0x304E8D);
    } else {
        text(412,199,"HELP",4,0x263B79);
        rect(411,266,646,301,0xFFFFFF);
        text(440,301,"KEYBOARD SHORTCUTS",3,0x31529D);
        text(442,369,"1 HOME   2 FILES   3 SYSTEM",2,0x536582);
        text(442,414,"4 ARCADE   5 HELP",2,0x536582);
        text(442,459,"ESC RETURN TO HOME",2,0x536582);
        text(442,522,"WINDOWS REPLACEMENT: FUTURE GOAL",2,0xA65469);
    }
}
static void redraw(void){window();content();}
static void move(int dx,int dy){
    int x=player%8+dx,y=player/8+dy;
    if(x<0)x=7;if(x>=8)x=0;if(y<0)y=5;if(y>=6)y=0;
    player=y*8+x;
    if(player==coin){score++;coin=(coin+13)%48;if(coin==player)coin=(coin+7)%48;}
}
static void keyboard(void){
    bool prefix=false;
    for(;;){
        if(!(inb(0x64)&1)){__asm__ volatile("pause");continue;}
        uint8_t key=inb(0x60);
        if(key==0xE0){prefix=true;continue;}
        if(key&0x80){prefix=false;continue;}
        int oldscreen=screen,oldplayer=player;
        if(key>=0x02&&key<=0x06)screen=(int)key-0x02;
        if(key==0x01)screen=0;
        if(screen==3) {
            if(key==0x1E||key==0x4B)move(-1,0);
            if(key==0x20||key==0x4D)move(1,0);
            if(key==0x11||key==0x48)move(0,-1);
            if(key==0x1F||key==0x50)move(0,1);
        }
        prefix=false;
        if(oldscreen!=screen||oldplayer!=player)redraw();
    }
}
void kmain(void){
    if(!LIMINE_BASE_REVISION_SUPPORTED(base_revision))idle();
    if(!fb_request.response||fb_request.response->framebuffer_count<1)idle();
    fb=fb_request.response->framebuffers[0];
    if(!fb||fb->bpp!=32||fb->memory_model!=LIMINE_FRAMEBUFFER_RGB||fb->width<800||fb->height<600)idle();
    redraw();keyboard();idle();
}
