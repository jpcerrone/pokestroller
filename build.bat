: - -DPRINT_STATE -> print every instruction and memory access
cl -Zi src\walker.c src\win_main.c /link Gdi32.lib User32.lib Ole32.lib
