# Maintainer: Christian Pellegrin <chripell@fsfe.org>
pkgname=yaaca
pkgver=20170212
pkgrel=1
pkgdesc="Yet Another AstroCam Application"
arch=('i686' 'x86_64')
url="https://github.com/chripell/yaaca"
license=('Apache')
depends=('python2>=2.7.13' 'python2-numpy>=1.11.3' 'python2-gobject>=3.22.0' 'gtk3>=3.22.6')

build() {
    cd ..
    make
}

package() {
    cd ..
    while read l; do
	FROM=`echo $l | cut -d \, -f 1`
	TO=`echo $l | cut -d \, -f 2`
	mkdir -p $pkgdir/$TO
	cp -a $FROM $pkgdir/$TO
    done < install_list
}

pkgver() {
    date '+%Y%m%d'
}
