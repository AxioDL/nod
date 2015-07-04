TEMPLATE = lib
CONFIG += staticlib c++11
CONFIG -= app_bundle
CONFIG -= qt
QT =
TARGET = NOD

QMAKE_CXXFLAGS += -maes

isEmpty(LOGVISOR_INCLUDE) {
    LOGVISOR_INCLUDE = ../LogVisor/include
}
INCLUDEPATH += ../include $$LOGVISOR_INCLUDE

SOURCES += \
    FileIOFILE.cpp \
    FileIOMEM.cpp \
    DiscBase.cpp \
    DiscGCN.cpp \
    DiscWii.cpp \
    DiscIOWBFS.cpp \
    DiscIOISO.cpp \
    aes.cpp \
    WideStringConvert.cpp \
    NOD.cpp

