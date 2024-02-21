: - -DPRINT_STATE -> print every instruction and memory access
: - -DINIT_EEPROM -> don't load an eeprom binary, initialize a new one
cl -Zi src\walker.c src\win_main.c /link Gdi32.lib User32.lib Ole32.lib Winmm.lib
