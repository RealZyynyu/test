/* NOVA GAME INPUT: keyboard-to-game-action adapter.
 * PS/2 set 1 keyboard and future controllers can feed the same action state.
 * No actual USB/gamepad driver exists. This logic is hardware independent.
 */
#ifndef NOVA_GAME_INPUT_H
#define NOVA_GAME_INPUT_H
#include <stdint.h>
#include <stdbool.h>

enum nova_game_action {
    NOVA_GAME_NONE=0,
    NOVA_GAME_LEFT=1,
    NOVA_GAME_RIGHT=2,
    NOVA_GAME_UP=3,
    NOVA_GAME_DOWN=4,
    NOVA_GAME_CONFIRM=5,
    NOVA_GAME_PAUSE=6,
    NOVA_GAME_ACTION_COUNT=7
};
#define NOVA_GAME_KEYBOARD 1u
#define NOVA_GAME_ARROW 2u
#define NOVA_GAME_CONTROLLER 4u

struct nova_game_input {
    uint8_t held[NOVA_GAME_ACTION_COUNT];
    bool extended;
    uint8_t pause_bytes;
};
static inline void nova_game_reset(struct nova_game_input *g){
    if(!g)return;
    *g=(struct nova_game_input){0};
}
static inline bool nova_game_held(const struct nova_game_input *g,
                                  enum nova_game_action action){
    return g && action>NOVA_GAME_NONE && action<NOVA_GAME_ACTION_COUNT &&
           g->held[action]!=0;
}
/* Returns action only for a new press from an input source, so PS/2 typematic
 * repetitions don't accidentally count as multiple game actions.
 */
static inline enum nova_game_action nova_game_set(
    struct nova_game_input *g,enum nova_game_action action,
    uint8_t source,bool down){
    if(!g || action<=NOVA_GAME_NONE || action>=NOVA_GAME_ACTION_COUNT ||
       source==0 || (source&(uint8_t)~7u)!=0 ||
       (source&(source-1u))!=0)return NOVA_GAME_NONE;
    bool was=(g->held[action]&source)!=0;
    if(down)g->held[action]=(uint8_t)(g->held[action]|source);
    else g->held[action]=(uint8_t)(g->held[action]&~source);
    return down&&!was?action:NOVA_GAME_NONE;
}
static inline enum nova_game_action nova_game_scan(
    struct nova_game_input *g,uint8_t scancode){
    if(!g)return NOVA_GAME_NONE;
    if(g->pause_bytes){
        g->pause_bytes--;
        return NOVA_GAME_NONE; /* consume uncommon E1 Pause sequence */
    }
    if(scancode==0xE1){
        g->extended=false;
        g->pause_bytes=5;
        return NOVA_GAME_NONE;
    }
    if(scancode==0xE0){
        g->extended=true;
        return NOVA_GAME_NONE;
    }
    bool extended=g->extended;
    g->extended=false;
    bool released=(scancode&0x80u)!=0;
    uint8_t key=(uint8_t)(scancode&0x7Fu);
    enum nova_game_action action=NOVA_GAME_NONE;
    uint8_t source=extended?NOVA_GAME_ARROW:NOVA_GAME_KEYBOARD;
    if(extended){
        if(key==0x4B)action=NOVA_GAME_LEFT;
        else if(key==0x4D)action=NOVA_GAME_RIGHT;
        else if(key==0x48)action=NOVA_GAME_UP;
        else if(key==0x50)action=NOVA_GAME_DOWN;
        else if(key==0x1C)action=NOVA_GAME_CONFIRM;
    }else{
        if(key==0x1E)action=NOVA_GAME_LEFT;      /* A */
        else if(key==0x20)action=NOVA_GAME_RIGHT;/* D */
        else if(key==0x11)action=NOVA_GAME_UP;   /* W */
        else if(key==0x1F)action=NOVA_GAME_DOWN; /* S */
        else if(key==0x39)action=NOVA_GAME_CONFIRM; /* Space */
        else if(key==0x19)action=NOVA_GAME_PAUSE; /* P */
    }
    return nova_game_set(g,action,source,!released);
}
#endif
