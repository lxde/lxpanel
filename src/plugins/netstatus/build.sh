gcc `pkg-config gconf-2.0 --cflags --libs` `pkg-config gtk+-2.0 --cflags --libs` -I. -I../ -I ../../ -shared -fPIC *.c -o netstatus.so
