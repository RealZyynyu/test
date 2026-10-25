/* Host-side regression tests for NOVA Arcade's normalized inputs. */
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include "../kernel/game_input.h"
static void keyboard_tests(void){
    struct nova_game_input g={0};
    assert(nova_game_scan(&g,0x1E)==NOVA_GAME_LEFT);
    assert(nova_game_held(&g,NOVA_GAME_LEFT));
    assert(nova_game_scan(&g,0x1E)==NOVA_GAME_NONE); /* repeated make */
    assert(nova_game_scan(&g,0x9E)==NOVA_GAME_NONE);
    assert(!nova_game_held(&g,NOVA_GAME_LEFT));
    assert(nova_game_scan(&g,0xE0)==NOVA_GAME_NONE);
    assert(nova_game_scan(&g,0x4B)==NOVA_GAME_LEFT);
    assert(nova_game_held(&g,NOVA_GAME_LEFT));
    assert(nova_game_scan(&g,0x1E)==NOVA_GAME_LEFT);
    assert(nova_game_scan(&g,0x9E)==NOVA_GAME_NONE);
    assert(nova_game_held(&g,NOVA_GAME_LEFT)); /* arrow still held */
    assert(nova_game_scan(&g,0xE0)==NOVA_GAME_NONE);
    assert(nova_game_scan(&g,0xCB)==NOVA_GAME_NONE);
    assert(!nova_game_held(&g,NOVA_GAME_LEFT));
    assert(nova_game_scan(&g,0x20)==NOVA_GAME_RIGHT);
    assert(nova_game_scan(&g,0xA0)==NOVA_GAME_NONE);
    assert(nova_game_scan(&g,0x11)==NOVA_GAME_UP);
    assert(nova_game_scan(&g,0x91)==NOVA_GAME_NONE);
    assert(nova_game_scan(&g,0x1F)==NOVA_GAME_DOWN);
    assert(nova_game_scan(&g,0x9F)==NOVA_GAME_NONE);
    assert(nova_game_scan(&g,0x39)==NOVA_GAME_CONFIRM);
    assert(nova_game_scan(&g,0xB9)==NOVA_GAME_NONE);
    assert(nova_game_scan(&g,0x19)==NOVA_GAME_PAUSE);
    assert(nova_game_scan(&g,0x99)==NOVA_GAME_NONE);
    assert(nova_game_scan(&g,0xE0)==NOVA_GAME_NONE);
    assert(nova_game_scan(&g,0x1C)==NOVA_GAME_CONFIRM);
    assert(nova_game_scan(&g,0xE0)==NOVA_GAME_NONE);
    assert(nova_game_scan(&g,0x9C)==NOVA_GAME_NONE);
    assert(nova_game_scan(&g,0xE1)==NOVA_GAME_NONE);
    const uint8_t pause_followup[]={0x1D,0x45,0xE1,0x9D,0xC5};
    for(unsigned i=0;i<sizeof(pause_followup);i++)
        assert(nova_game_scan(&g,pause_followup[i])==NOVA_GAME_NONE);
    assert(!g.pause_bytes);
    nova_game_reset(&g);
    for(unsigned i=1;i<NOVA_GAME_ACTION_COUNT;i++)
        assert(!nova_game_held(&g,(enum nova_game_action)i));
}
static void controller_adapter_tests(void){
    struct nova_game_input g={0};
    assert(nova_game_set(&g,NOVA_GAME_LEFT,NOVA_GAME_CONTROLLER,true)==NOVA_GAME_LEFT);
    assert(nova_game_set(&g,NOVA_GAME_LEFT,NOVA_GAME_CONTROLLER,true)==NOVA_GAME_NONE);
    assert(nova_game_scan(&g,0x1E)==NOVA_GAME_LEFT);
    assert(nova_game_set(&g,NOVA_GAME_LEFT,NOVA_GAME_CONTROLLER,false)==NOVA_GAME_NONE);
    assert(nova_game_held(&g,NOVA_GAME_LEFT));
    assert(nova_game_scan(&g,0x9E)==NOVA_GAME_NONE);
    assert(!nova_game_held(&g,NOVA_GAME_LEFT));
    assert(nova_game_set(&g,NOVA_GAME_NONE,NOVA_GAME_CONTROLLER,true)==NOVA_GAME_NONE);
    assert(nova_game_set(&g,NOVA_GAME_LEFT,0,true)==NOVA_GAME_NONE);
    assert(nova_game_set(&g,NOVA_GAME_LEFT,8,true)==NOVA_GAME_NONE);
    assert(nova_game_set(&g,NOVA_GAME_LEFT,3,true)==NOVA_GAME_NONE);
    assert(!nova_game_held(NULL,NOVA_GAME_UP));
    assert(!nova_game_held(&g,NOVA_GAME_NONE));
    assert(nova_game_scan(NULL,0x1E)==NOVA_GAME_NONE);
    nova_game_reset(NULL);
}
int main(void){
    keyboard_tests();
    controller_adapter_tests();
    puts("Game input passed: WASD, arrows, repeats, release, simultaneous bindings, future controller API.");
    return 0;
}
