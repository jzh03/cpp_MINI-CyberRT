# Invoke from the existing build's working directory:
# make -C example -f demo/Makefile demo-transport
BUILD_DIR := $(CURDIR)/demo/build
include Makefile

CPPFLAGS += -include $(CURDIR)/demo/trace.h
CXXFLAGS += -O2 -g0
-include $(OBJ_DIR)/example/demo/demo_transport.d
$(SUBSCRIBER_CORE_OBJS) $(OBJ_DIR)/example/demo/demo_transport.o: demo/Makefile
.DEFAULT_GOAL := demo-transport

.PHONY: demo-transport
demo-transport: $(BIN_DIR)/demo_transport

$(BIN_DIR)/demo_transport: $(SUBSCRIBER_CORE_OBJS) $(OBJ_DIR)/example/demo/demo_transport.o demo/Makefile | check-fastdds $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)
