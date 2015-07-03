TEMPLATE = lib
CONFIG += staticlib c++11
CONFIG -= app_bundle
CONFIG -= qt
QT =
TARGET = NOD

QMAKE_CXXFLAGS += -maes
INCLUDEPATH += ../include ../../../LogVisor/include

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

