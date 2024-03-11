## PokeStroller - a PokéWalker emulator
PokeStroller is an experimental pokewalker emulator for windows. It can load up your eeprom memory file and lets you visualize your pokemon, scroll through the menus and play the dowsing and pokeradar minigames.

![home](https://github.com/jpcerrone/pokestroller/blob/master/img/home.gif)
![dowsing](https://github.com/jpcerrone/pokestroller/blob/master/img/dowsing.gif)
![battle](https://github.com/jpcerrone/pokestroller/blob/master/img/battle.gif)

### Running
Download the latest release from the "Releases" section. 
To run the emulator you'll need a copy of the pokewalker's ROM and a copy of your EEPROM binary. You can dump both of these using PoroCYon's dumper for DSi/3ds or DmitryGR's PalmOS app.
Place both of these files in the same folder as the emulator binary and rename them to 'eeprom.bin' and 'rom.bin' accordingly.
Run the emulator, the buttons are controlled with 'Z', 'X' and the spacebar.

### TODO list
- Audio.
- IR emulation to connect to a Nintendo DS emulator or to another pokestroller instance.
- RTC.
- Accelerometer simulation.
- Save changes to eeprom file.
- Proper RNG

### Compiling
#### Windows
Install the MSVC build tools for windows and run build.bat from the command line
https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=msvc-170
#### Other OS's
Not supported yet

### Contributing
Feel free to contribute by writing a PR!
