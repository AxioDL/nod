TEMPLATE = subdirs
CONFIG -= app_bundle
CONFIG -= qt
QT =

SUBDIRS += lib driver
driver.depends += lib

HEADERS += \
    include/Util.hpp \
    include/NODLib.hpp \
    include/IDiscIO.hpp \
    include/IFileIO.hpp \
    include/DiscBase.hpp \
    include/DiscGCN.hpp \
    include/DiscWii.hpp \
    include/aes.hpp

