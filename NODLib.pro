TEMPLATE = subdirs
CONFIG -= app_bundle
CONFIG -= qt
QT =

SUBDIRS += lib driver
driver.depends += lib

HEADERS += \
    include/NOD/Util.hpp \
    include/NOD/NOD.hpp \
    include/NOD/IDiscIO.hpp \
    include/NOD/IFileIO.hpp \
    include/NOD/DiscBase.hpp \
    include/NOD/DiscGCN.hpp \
    include/NOD/DiscWii.hpp \
    include/NOD/aes.hpp

