#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := sim800
PROJECT_VER := 0.1.0
CXXFLAGS += -std=c++17
CPPFLAGS += -DPROJECT_VERSION=\"$(PROJECT_VER)\"

include $(IDF_PATH)/make/project.mk

