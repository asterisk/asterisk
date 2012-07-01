win32 {
  DEFINES += PJ_WIN32=1
  INCLUDEPATH += ../../../pjlib/include ../../../pjlib-util/include \
		 ../../../pjnath/include ../../../pjmedia/include \
		 ../../../pjsip/include

  # These to enable static linking
  #CONFIG += static
  #DEFINES += STATIC

  CONFIG(debug) {
    LIBS += ../../../lib/libpjproject-i386-Win32-vc8-Debug.lib
  } else {
    LIBS += ../../../lib/libpjproject-i386-Win32-vc8-Release.lib
  }
  LIBS += Iphlpapi.lib  dsound.lib \
  	  dxguid.lib netapi32.lib mswsock.lib ws2_32.lib odbc32.lib \
  	  odbccp32.lib ole32.lib user32.lib gdi32.lib advapi32.lib 
} else {
  LIBS += $$system(make -f pj-pkgconfig.mak ldflags)
  QMAKE_CXXFLAGS += $$system(make --silent -f pj-pkgconfig.mak cflags)

  macx {
    QMAKE_CXXFLAGS += -ObjC++
  }
}

TEMPLATE = app
CONFIG += thread debug
TARGET = 
DEPENDPATH += .

# Input
HEADERS += vidgui.h vidwin.h
SOURCES += vidgui.cpp vidwin.cpp 

