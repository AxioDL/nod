win32-g++ {
QMAKE_LFLAGS += -municode
}
TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt
QT =

isEmpty(LOGVISOR_INCLUDE) {
    LOGVISOR_INCLUDE = ../LogVisor/include
}
INCLUDEPATH += ../include $$LOGVISOR_INCLUDE

isEmpty(LOGVISOR_LIBS) {
    LOGVISOR_LIBS = -L$$OUT_PWD/../LogVisor -lLogVisor
}
LIBS += -L$$OUT_PWD/../lib -lNOD $$LOGVISOR_LIBS -lpthread

QMAKE_LFLAGS += -maes
SOURCES += main.cpp
