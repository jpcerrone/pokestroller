mkdir bin -p

wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml bin/xdg-shell-protocol.c
wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml bin/xdg-shell-client-protocol.h
# -g: debug symbols
gcc -g src/linux_main.c src/walker.c src/queue.c bin/xdg-shell-protocol.c -o bin/pokestroller -lwayland-client -lxkbcommon
