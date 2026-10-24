test_dir := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
XBE_TITLE = Kodi runtime checks
SRCS = $(test_dir)main.cpp $(test_dir)throw.cpp
NXDK_CXX = y
NXDK_CXXFLAGS += -std=c++17 -O2
ifeq ($(AUTO_RETURN),1)
NXDK_CXXFLAGS += -DRUNTIME_AUTO_RETURN
endif
include $(NXDK_DIR)/Makefile

# These small objects must not retain a different SDK or AUTO_RETURN setting.
.PHONY: runtime-force
runtime-force:
$(SRCS:.cpp=.obj): runtime-force
