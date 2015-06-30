TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt
QT =

INCLUDEPATH += ../include
QMAKE_LFLAGS += -maes
LIBS += -L$$OUT_PWD/../lib -lNOD

SOURCES += main.cpp
