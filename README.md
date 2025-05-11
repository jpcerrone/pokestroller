# PokeStroller - a PokéWalker emulator
PokeStroller is an experimental pokewalker emulator for windows. It loads up your eeprom memory file and lets you visualize your pokemon, scroll through the menus and play the dowsing and pokeradar minigames.

![home](https://github.com/jpcerrone/pokestroller/blob/master/img/home.gif)
![menu](https://github.com/jpcerrone/pokestroller/blob/master/img/menu.gif)
![dowsing](https://github.com/jpcerrone/pokestroller/blob/master/img/dowsing.gif)
![battle](https://github.com/jpcerrone/pokestroller/blob/master/img/battle.gif)

## Running
To run the emulator you'll need a copy of your pokewalker's ROM and a copy of your EEPROM binary. You can dump both of these from your pokewalker using [PoroCYon's dumper for DSi/3ds](https://gitlab.ulyssis.org/pcy/pokewalker-rom-dumper) or [DmitryGR's PalmOS app](https://dmitry.gr/?r=05.Projects&proj=28.%20pokewalker#_TOC_377b8050cfd1e60865685a4ca39bc4c0).

Download the latest release from the "Releases" section. 

Place both the eeprom and rom files in the same folder as the emulator binary and rename them to `eeprom.bin` and `rom.bin` accordingly.

Run the emulator, the buttons are controlled with `Z`, `X` and the `spacebar`.

## TODO list
- Audio.
- IR emulation to connect to a Nintendo DS emulator or to another pokestroller instance.
- RTC.
- Accelerometer simulation (Step counting).
- Save changes to eeprom file.
- Fix pokeRadar bug that appears when clicking the wrong bush.

## Compiling
### Windows
Install the MSVC build tools for windows and run `build.bat` from the command line
https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=msvc-170
### Other OS's
Not supported yet

## Contributing
Feel free to contribute by opening up a PR!

## Shameless plug
Check out my ear training software for guitar! [http://gapsguitar.com/](http://gapsguitar.com/)
