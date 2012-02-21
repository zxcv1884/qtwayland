QT += gui gui-private core-private compositor

# comment out the following to not use pkg-config in the pri files
CONFIG += use_pkgconfig

LIBS += -L ../../lib
#include (../../src/qt-compositor/qt-compositor.pri)

HEADERS += \
    qopenglwindow.h \
    qwindowcompositor.h \
    textureblitter.h

SOURCES += main.cpp \
    qopenglwindow.cpp \
    qwindowcompositor.cpp \
    textureblitter.cpp

# to make QtCompositor/... style includes working without installing
INCLUDEPATH += $$PWD/../../include

#  if you want to compile QtCompositor as part of the application
#  instead of linking to it, remove the QT += compositor and uncomment
#  the following line
#include(../../src/compositor/compositor.pri)

RESOURCES += qwindow-compositor.qrc

target.path = $$[QT_INSTALL_EXAMPLES]/qtwayland/qwindow-compositor
sources.files = $$SOURCES $$HEADERS $$RESOURCES $$FORMS qwindow-compositor.pro
sources.path = $$[QT_INSTALL_EXAMPLES]/qtwayland/qwindow-compositor
INSTALLS += target sources
