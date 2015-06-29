TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt
QT =

QMAKE_CXXFLAGS += -maes
QMAKE_LFLAGS += -maes

INCLUDEPATH += include

HEADERS += \
    include/Util.hpp \
    include/NODLib.hpp \
    include/IDiscIO.hpp \
    include/IFileIO.hpp \
    include/DiscBase.hpp \
    include/DiscGCN.hpp \
    include/DiscWii.hpp \
    include/aes.hpp

SOURCES += \
    lib/NODLib.cpp \
    lib/FileIOFILE.cpp \
    lib/FileIOMEM.cpp \
    lib/DiscBase.cpp \
    lib/DiscGCN.cpp \
    lib/DiscWii.cpp \
    lib/DiscIOWBFS.cpp \
    lib/DiscIOISO.cpp \
    main.cpp \
    lib/aes.cpp
