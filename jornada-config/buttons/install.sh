gcc -I/usr/include/libevdev-1.0/ buttonhandler.c
mv a.out /usr/bin/jbuttons
cp j720b* /etc/
chmod 755 /etc/j720b*
cp j720-buttons.sh /etc/init.d/
update-rc.d j720-buttons.sh defaults
