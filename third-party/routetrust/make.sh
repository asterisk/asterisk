cd ~/projects/asterisk
./contrib/scripts/get_mp3_source.sh
./configure --with-pjproject-bundled --with-jansson-bundled
cp menuselect.makeopts.routetrust ~/asterisk.makeopts
make