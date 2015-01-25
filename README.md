[![Build Status](https://drone.io/github.com/cboxdoerfer/ddb_waveform_seekbar/status.png)](https://drone.io/github.com/cboxdoerfer/ddb_waveform_seekbar/latest)

Waveform Seekbar plugin for DeaDBeeF audio player
====================

## Table of Contents

* [Installation](#installation)
  * [Arch Linux](#arch-linux)
  * [Gentoo](#gentoo)
  * [Binaries](#binaries)
    * [Stable](#stable)
    * [Dev](#dev)
  * [Compilation](#compilation)
* [Usage](#usage)
* [Screenshots](%screenshots)

## Installation
### Arch Linux
See the [AUR](https://aur.archlinux.org/packages/deadbeef-plugin-waveform-git/).
### Gentoo
See ebuilds [here](https://github.com/megabaks/stuff/tree/master/media-plugins/deadbeef-waveform-seekbar).
### Binaries
Install them as follows:

x86_64: ```tar -xvf ddb_waveform_seekbar_x86_64.tar.gz -C ~/.local/lib/deadbeef```

i686: ```tar -xvf ddb_waveform_seekbar_i686.tar.gz -C ~/.local/lib/deadbeef```
#### Stable
[x86_64](https://github.com/cboxdoerfer/ddb_waveform_seekbar/releases/download/v0.4/ddb_waveform_seekbar_x86_64.tar.gz)

[i686](https://github.com/cboxdoerfer/ddb_waveform_seekbar/releases/download/v0.4/ddb_waveform_seekbar_i686.tar.gz)
#### Dev
[x86_64](https://drone.io/github.com/cboxdoerfer/ddb_waveform_seekbar/files/deadbeef-plugin-builder/ddb_waveform_seekbar_x86_64.tar.gz)

[i686](https://drone.io/github.com/cboxdoerfer/ddb_waveform_seekbar/files/deadbeef-plugin-builder/ddb_waveform_seekbar_i686.tar.gz)

### Compilation
You need DeaDBeeF (>=0.6) and sqlite3 and their development files
```bash
make
./userinstall.sh
```
## Usage
Add it to your Layout with Design Mode (Edit -> Design Mode -> right click in player UI). 

There are two settings dialogs:

Right click on waveform and select Configure

and

Edit -> Preferences -> Plugins -> Waveform Seekbar -> Configure

## Screenshots
### Waveform
![](http://i.imgur.com/StjuEzc.png)

![](http://i.imgur.com/uI6YAzs.png)

### Settings
![](http://i.imgur.com/niEuKVT.png)
![](http://i.imgur.com/ZCa2Wog.png)
