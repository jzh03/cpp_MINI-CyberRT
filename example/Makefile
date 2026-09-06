# Minimal build for the publisher/subscriber runtime examples.

CXX              ?= g++
PROJECT_ROOT      := $(abspath ..)
FAST_DDS_HOME     ?= $(HOME)/cpp/fastdds_2.12/install
FAST_DDS_INC_DIR  := $(FAST_DDS_HOME)/include
FAST_DDS_LIB_DIR  := $(FAST_DDS_HOME)/lib

BUILD_DIR         ?= $(CURDIR)/build
OBJ_DIR           := $(BUILD_DIR)/obj
BIN_DIR           := $(BUILD_DIR)/bin
COMPAT_INC_DIR    := $(BUILD_DIR)/include
CMW_INCLUDE_LINK  := $(COMPAT_INC_DIR)/cmw

CPPFLAGS := -I$(COMPAT_INC_DIR) \
            -I$(PROJECT_ROOT)/thirdparty \
            -I$(FAST_DDS_INC_DIR)
CXXFLAGS := -std=c++14 -g -MMD -MP
LDFLAGS  := -L$(FAST_DDS_LIB_DIR) -Wl,-rpath,$(FAST_DDS_LIB_DIR)
LDLIBS   := -Wl,--no-as-needed \
            -lfastrtps -lfastcdr -lfoonathan_memory-0.7.3 \
            -Wl,--as-needed -lpthread -luuid -lrt -latomic -ldl

# Keep discovery inside explicit runtime directories. Tests, examples,
# thirdparty sources, class-loader plugins and components are excluded.
COMMON_SOURCE_DIRS     := common config discovery event log node serialize \
                          time transport
SUBSCRIBER_SOURCE_DIRS := croutine scheduler
COMMON_SOURCE_ROOTS    := $(addprefix $(PROJECT_ROOT)/,$(COMMON_SOURCE_DIRS))
SUBSCRIBER_SOURCE_ROOTS := $(addprefix $(PROJECT_ROOT)/,$(SUBSCRIBER_SOURCE_DIRS))
COMMON_CPP_SRCS := $(shell find $(COMMON_SOURCE_ROOTS) -type f -name '*.cpp' | sort)
SUBSCRIBER_CPP_SRCS := $(shell find $(SUBSCRIBER_SOURCE_ROOTS) -type f -name '*.cpp' | sort)
SUBSCRIBER_ASM_SRCS := $(shell find $(PROJECT_ROOT)/croutine -type f -name '*.S' | sort)

COMMON_CPP_RELS := $(patsubst $(PROJECT_ROOT)/%,%,$(COMMON_CPP_SRCS))
SUBSCRIBER_CPP_RELS := $(patsubst $(PROJECT_ROOT)/%,%,$(SUBSCRIBER_CPP_SRCS))
SUBSCRIBER_ASM_RELS := $(patsubst $(PROJECT_ROOT)/%,%,$(SUBSCRIBER_ASM_SRCS))

PUBLISHER_CORE_OBJS := $(OBJ_DIR)/init.o \
    $(addprefix $(OBJ_DIR)/,$(COMMON_CPP_RELS:.cpp=.o))
SUBSCRIBER_CORE_OBJS := $(PUBLISHER_CORE_OBJS) $(OBJ_DIR)/state.o \
    $(addprefix $(OBJ_DIR)/,$(SUBSCRIBER_CPP_RELS:.cpp=.o)) \
    $(addprefix $(OBJ_DIR)/,$(SUBSCRIBER_ASM_RELS:.S=.o))

# Preserve the original example test coverage while keeping source discovery
# bounded to CyberRT implementation directories.
TEST_SOURCE_DIRS := blocker class_loader common component config croutine \
                    discovery event log node scheduler serialize sysmo task \
                    time transport
TEST_SOURCE_ROOTS := $(addprefix $(PROJECT_ROOT)/,$(TEST_SOURCE_DIRS))
TEST_CPP_SRCS := $(shell find $(TEST_SOURCE_ROOTS) -type f -name '*.cpp' | sort)
TEST_ASM_SRCS := $(shell find $(PROJECT_ROOT)/croutine -type f -name '*.S' | sort)
TEST_ROOT_SRCS := $(PROJECT_ROOT)/binary.cpp $(PROJECT_ROOT)/init.cpp \
                  $(PROJECT_ROOT)/state.cpp
TEST_CPP_RELS := $(patsubst $(PROJECT_ROOT)/%,%,$(TEST_ROOT_SRCS) $(TEST_CPP_SRCS))
TEST_ASM_RELS := $(patsubst $(PROJECT_ROOT)/%,%,$(TEST_ASM_SRCS))
TEST_CORE_OBJS := $(addprefix $(OBJ_DIR)/,$(TEST_CPP_RELS:.cpp=.o)) \
                  $(addprefix $(OBJ_DIR)/,$(TEST_ASM_RELS:.S=.o))
DEPENDENCY_OBJS = $(PUBLISHER_CORE_OBJS) $(SUBSCRIBER_CORE_OBJS) \
                   $(TEST_CORE_OBJS) $(OBJ_DIR)/example/test_publisher.o \
                   $(OBJ_DIR)/example/test_subscriber.o \
                   $(addprefix $(OBJ_DIR)/example/,$(addsuffix .o,$(ALL_TEST_TARGETS))) \
                   $(OBJ_DIR)/example/shm_segment_benchmark.o \
                   $(OBJ_DIR)/example/shm_zero_copy_benchmark.o
DEPENDENCY_FILES = $(DEPENDENCY_OBJS:.o=.d)

# `tests` is deliberately build-only.  These are the binaries which have a
# reliable pass/fail contract and are eligible for the check targets below.
FAST_TEST_TARGETS := test_serialize test_blocker test_node \
                     test_publisher_subscriber test_transport_mode_selection \
                     test_shm_segment_robustness test_shm_block_lease_generation \
                     test_shm_dispatcher_robustness test_shm_transmitter_receiver \
                     test_shm_loaned_message test_shm_transmitter_lifecycle_regression \
                     test_loaned_message_hybrid test_hybrid_intra
INTEGRATION_TEST_TARGETS := test_posix_segment_multiprocess \
                            test_loaned_message_dynamic_shm_lifecycle \
                            test_shm_loaned_message_multiprocess \
                            test_hybrid_shm_multiprocess \
                            test_rtps_same_host_multiprocess \
                            test_hybrid_dynamic_shm_lifecycle \
                            test_rtps_lifecycle_regression \
                            test_loaned_message_rtps_multiprocess
FORMAL_TEST_TARGETS := $(FAST_TEST_TARGETS) $(INTEGRATION_TEST_TARGETS)

# Hand-operated programs and diagnostics remain buildable, but are never run
# by `check`.  The other legacy targets are intentionally left outside both
# formal regression and demos until they gain reliable result reporting.
DEMO_TEST_TARGETS := test_writer test_reader test_publisher test_subscriber \
                     test_topology_manager test_log test_getenv
LEGACY_TEST_TARGETS := test_node_manager test_channel_manager test_croutine \
                       test_scheduler test_task test_class_loader
ALL_TEST_TARGETS := $(FORMAL_TEST_TARGETS) $(DEMO_TEST_TARGETS) \
                    $(LEGACY_TEST_TARGETS)
BENCHMARK_TARGETS := shm_segment_benchmark shm_zero_copy_benchmark
CLASS_LOADER_PLUGIN_DIR := $(PROJECT_ROOT)/class_loader/test
CLASS_LOADER_PLUGINS := $(CLASS_LOADER_PLUGIN_DIR)/libplugin1.so \
                        $(CLASS_LOADER_PLUGIN_DIR)/libplugin2.so

TEST_RUNNER ?= $(CURDIR)/run_tests.sh
CHECK_FAST_TIMEOUT ?= 30
CHECK_INTEGRATION_TIMEOUT ?= 90

PUBLISHER_OBJ  := $(OBJ_DIR)/example/test_publisher.o
SUBSCRIBER_OBJ := $(OBJ_DIR)/example/test_subscriber.o
PUBLISHER_BIN  := $(BIN_DIR)/publisher
SUBSCRIBER_BIN := $(BIN_DIR)/subscriber

FAST_DDS_REQUIRED := \
    $(FAST_DDS_INC_DIR)/fastrtps/rtps/RTPSDomain.h \
    $(FAST_DDS_INC_DIR)/fastrtps/rtps/participant/RTPSParticipant.h \
    $(FAST_DDS_LIB_DIR)/libfastrtps.so \
    $(FAST_DDS_LIB_DIR)/libfastcdr.so \
    $(FAST_DDS_LIB_DIR)/libfoonathan_memory-0.7.3.so

.DEFAULT_GOAL := all

-include $(DEPENDENCY_FILES)

.PHONY: all publisher subscriber tests demos benchmarks check check-fast \
        check-integration $(ALL_TEST_TARGETS) $(BENCHMARK_TARGETS) \
        check-fastdds clean
.SECONDARY: $(TEST_CORE_OBJS) \
            $(addprefix $(OBJ_DIR)/example/,$(addsuffix .o,$(ALL_TEST_TARGETS)))

all: publisher subscriber

publisher: $(PUBLISHER_BIN)

subscriber: $(SUBSCRIBER_BIN)

tests: $(FORMAL_TEST_TARGETS)

demos: $(DEMO_TEST_TARGETS)

benchmarks: $(BENCHMARK_TARGETS)

# Each group first builds its complete target list.  The runner then launches
# every binary in sequence, even when make itself was invoked with -j.
check-fast: $(FAST_TEST_TARGETS) $(TEST_RUNNER)
	@bash "$(TEST_RUNNER)" --bin-dir "$(BIN_DIR)" \
		--timeout "$(CHECK_FAST_TIMEOUT)" $(FAST_TEST_TARGETS)

check-integration: $(INTEGRATION_TEST_TARGETS) $(TEST_RUNNER)
	@bash "$(TEST_RUNNER)" --bin-dir "$(BIN_DIR)" \
		--timeout "$(CHECK_INTEGRATION_TIMEOUT)" $(INTEGRATION_TEST_TARGETS)

# Use recipes rather than parallel prerequisites so `make -j check` still
# executes the two groups in order.  Run integration even if fast regresses,
# then report one combined result.
check:
	@status=0; \
	$(MAKE) --no-print-directory check-fast || status=1; \
	$(MAKE) --no-print-directory check-integration || status=1; \
	if test $$status -ne 0; then \
		echo "check: FAILED (see group summaries above)" >&2; \
	else \
		echo "check: PASSED"; \
	fi; \
	exit $$status

$(ALL_TEST_TARGETS): %: $(BIN_DIR)/%

$(BENCHMARK_TARGETS): %: $(BIN_DIR)/%

test_class_loader: $(CLASS_LOADER_PLUGINS)

check-fastdds:
	@for file in $(FAST_DDS_REQUIRED); do \
		test -e "$$file" || { echo "Missing Fast DDS file: $$file" >&2; exit 1; }; \
	done

$(PUBLISHER_BIN): $(PUBLISHER_CORE_OBJS) $(PUBLISHER_OBJ) Makefile | check-fastdds $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(SUBSCRIBER_BIN): $(SUBSCRIBER_CORE_OBJS) $(SUBSCRIBER_OBJ) Makefile | check-fastdds $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(BIN_DIR)/test_class_loader: LDFLAGS += -Wl,--export-dynamic

$(BIN_DIR)/test_%: $(TEST_CORE_OBJS) $(OBJ_DIR)/example/test_%.o Makefile | check-fastdds $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS) -lgtest

$(BIN_DIR)/shm_segment_benchmark: $(TEST_CORE_OBJS) $(OBJ_DIR)/example/shm_segment_benchmark.o Makefile | check-fastdds $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(BIN_DIR)/shm_zero_copy_benchmark: $(TEST_CORE_OBJS) $(OBJ_DIR)/example/shm_zero_copy_benchmark.o Makefile | check-fastdds $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(CLASS_LOADER_PLUGIN_DIR)/lib%.so: $(CLASS_LOADER_PLUGIN_DIR)/%.cc | $(CMW_INCLUDE_LINK)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -shared -fPIC -o $@ $^ -lpthread -ldl

$(OBJ_DIR)/%.o: $(PROJECT_ROOT)/%.cpp | $(CMW_INCLUDE_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: $(PROJECT_ROOT)/%.S | $(CMW_INCLUDE_LINK)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -c -o $@ $<

$(CMW_INCLUDE_LINK):
	@mkdir -p $(COMPAT_INC_DIR)
	@ln -sfn $(PROJECT_ROOT) $@

$(BIN_DIR):
	@mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(CLASS_LOADER_PLUGINS)
	rm -f $(CURDIR)/*.log
