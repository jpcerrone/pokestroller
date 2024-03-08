: - -DPRINT_STATE -> print every instruction and memory access
: - -DDISPLAY_FRAME_TIME -> print frame time 
: - -DINIT_EEPROM -> don't load an eeprom binary, initialize a new one
cl -Zi src\walker.c src\win_main.c src\queue.c /link Gdi32.lib User32.lib Ole32.lib Winmm.lib
