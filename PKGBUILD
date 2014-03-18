pkgname=deadbeef-plugin-waveform-git
pkgver=20140318
pkgrel=2
pkgdesc="Waveform Seekbar Plugin for the DeaDBeeF audio player (development version)"
url="https://github.com/cboxdoerfer/ddb_waveform_seekbar"
arch=('i686' 'x86_64')
license='GPL2'
depends=('deadbeef' 'sqlite')
makedepends=('git')
conflicts=('deadbeef-plugin-waveform')

_gitname=ddb_waveform_seekbar
_gitroot=https://github.com/cboxdoerfer/${_gitname}

build() {
  cd $srcdir
  msg "Connecting to GIT server..."
  rm -rf $srcdir/$_gitname-build

  if [ -d $_gitname ]; then
    cd $_gitname
    git pull origin master
  else
    git clone $_gitroot
    cd $_gitname
  fi

  msg "GIT checkout done or server timeout"
  msg "Starting make..."

  cd $srcdir
  cp -r $_gitname $_gitname-build

  cd $_gitname-build

  touch AUTHORS
  touch ChangeLog

  make
}

package() {
  install -D -v -c $srcdir/$_gitname-build/gtk2/ddb_misc_waveform_GTK2.so $pkgdir/usr/lib/deadbeef/ddb_misc_waveform_GTK2.so
  install -D -v -c $srcdir/$_gitname-build/gtk3/ddb_misc_waveform_GTK3.so $pkgdir/usr/lib/deadbeef/ddb_misc_waveform_GTK3.so
}
