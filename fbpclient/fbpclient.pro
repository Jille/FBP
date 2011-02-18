# -------------------------------------------------
# Project created by QtCreator 2009-12-20T22:44:42
# -------------------------------------------------
QT += network
TARGET = fbpclient
TEMPLATE = app
SOURCES += main.cpp \
    mainwindow.cpp \
    fbpclient.cpp \
    receiverthread.cpp
HEADERS += mainwindow.h \
    fbpclient.h \
    ../common/fbp.h \
    ../common/bitmask.h \
    receiverthread.h \
    branding.h
FORMS += mainwindow.ui
