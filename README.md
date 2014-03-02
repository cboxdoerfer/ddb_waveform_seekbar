Waveform Seekbar plugin for DeaDBeeF audio player
====================

## Notes
If you feel a little adventurous you can enable the waveform cache in 

*Edit -> Preferences -> Plugins -> Waveform Seekbar -> Configure -> Enable cache (experimental)*

This way the waveform of tracks (which were already played once) should be displayed much faster. If you experience any issues with that - such as corrupted waveforms, crashes, ... - please let me know.

The cache file can be found at: *~/.cache/deadbeef/waveform/wavecache.db*

You can also delete already cached waveforms in the context menu of the resp. track(s).

## Installation
### Arch Linux
See the [AUR](https://aur.archlinux.org/packages/deadbeef-plugin-waveform-git/).

### Gentoo
See ebuilds [here](https://github.com/megabaks/stuff/tree/master/media-plugins/deadbeef-waveform-seekbar).

### Other distributions
#### Build from sources
First install DeaDBeeF (>=0.6) and sqlite3
```bash
make
./userinstall.sh
```
#### Binaries
Since v0.2 you can also get ![binaries](https://github.com/cboxdoerfer/ddb_waveform_seekbar/releases). Install them as follows
##### x86_64
```mkdir -p ~/.local/lib/deadbeef && tar -xvf ddb_waveform_seekbar_x86_64.tar.gz -C ~/.local/lib/deadbeef```
##### i686
```mkdir -p ~/.local/lib/deadbeef && tar -xvf ddb_waveform_seekbar_i686.tar.gz -C ~/.local/lib/deadbeef```

## Usage
Add it to your Layout with Design Mode (*Edit -> Design Mode -> right click in player UI*). 

There are two settings dialogs:

Right click on waveform and select Configure

and

Edit -> Preferences -> Plugins -> Waveform Seekbar -> Configure

## Screenshots
### Waveform
![](http://i.imgur.com/hLeecgF.png)

### Settings
![](http://i.imgur.com/eMqXgtP.png)
