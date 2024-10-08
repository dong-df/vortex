ROOT_DIR := $(realpath ../../../..)
include $(ROOT_DIR)/config.mk

ifneq ($(findstring Makefile, $(MAKEFILE_LIST)), Makefile)
help:
	$(ECHO) "Makefile Usage:"
	$(ECHO) "  make all TARGET=<sw_emu/hw_emu/hw> PLATFORM=<FPGA platform>"
	$(ECHO) "      Command to generate the design for specified Target and Device."
	$(ECHO) ""
	$(ECHO) "  make clean"
	$(ECHO) "      Command to remove the generated non-hardware files."
	$(ECHO) ""
endif

TARGET ?= hw
PLATFORM ?=

NUM_CORES ?= 1
PREFIX ?= build$(XLEN)
MAX_JOBS ?= 8

SRC_DIR := $(VORTEX_HOME)/hw/syn/xilinx/xrt

RTL_DIR := $(VORTEX_HOME)/hw/rtl
DPI_DIR := $(VORTEX_HOME)/hw/dpi
AFU_DIR := $(RTL_DIR)/afu/xrt
SCRIPT_DIR := $(VORTEX_HOME)/hw/scripts

VIVADO := $(XILINX_VIVADO)/bin/vivado
VPP := $(XILINX_VITIS)/bin/v++

CP = cp -rf
RMDIR = rm -rf
ECHO = @echo

NCPUS := $(shell lscpu | grep "^Core(s) per socket:" | awk '{print $$4}')
JOBS ?= $(shell echo $$(( $(NCPUS) > $(MAX_JOBS) ? $(MAX_JOBS) : $(NCPUS) )))

PLATFORM_TO_XSA = $(strip $(patsubst %.xpfm, % , $(shell basename $(PLATFORM))))
XSA := $(call PLATFORM_TO_XSA, $(PLATFORM))

DEV_ARCH := $(shell platforminfo -p $(PLATFORM) | grep 'FPGA Family' | sed 's/.*://' | sed '/ai_engine/d' | sed 's/^[[:space:]]*//')
CPU_TYPE := $(shell platforminfo -p $(PLATFORM) | grep 'CPU Type' | sed 's/.*://' | sed '/ai_engine/d' | sed 's/^[[:space:]]*//')

BUILD_DIR := $(PREFIX)_$(XSA)_$(TARGET)
BIN_DIR := $(BUILD_DIR)/bin

XO_CONTAINER := $(BIN_DIR)/vortex_afu.xo
XCLBIN_CONTAINER := $(BIN_DIR)/vortex_afu.xclbin

# Control RTL debug tracing states
DBG_TRACE_FLAGS += -DDBG_TRACE_PIPELINE
DBG_TRACE_FLAGS += -DDBG_TRACE_MEM
DBG_TRACE_FLAGS += -DDBG_TRACE_CACHE
DBG_TRACE_FLAGS += -DDBG_TRACE_AFU
DBG_TRACE_FLAGS += -DDBG_TRACE_GBAR

# Control logic analyzer monitors
DBG_SCOPE_FLAGS += -DDBG_SCOPE_AFU
DBG_SCOPE_FLAGS += -DDBG_SCOPE_ISSUE
DBG_SCOPE_FLAGS += -DDBG_SCOPE_FETCH
DBG_SCOPE_FLAGS += -DDBG_SCOPE_LSU
DBG_SCOPE_FLAGS += -DDBG_SCOPE_MSCHED

# cluster configuration
CONFIGS_1c  := -DNUM_CLUSTERS=1 -DNUM_CORES=1
CONFIGS_2c  := -DNUM_CLUSTERS=1 -DNUM_CORES=2
CONFIGS_4c  := -DNUM_CLUSTERS=1 -DNUM_CORES=4
CONFIGS_8c	:= -DNUM_CLUSTERS=1 -DNUM_CORES=8
CONFIGS_16c	:= -DNUM_CLUSTERS=1 -DNUM_CORES=16
CONFIGS_32c := -DNUM_CLUSTERS=2 -DNUM_CORES=16
CONFIGS_64c := -DNUM_CLUSTERS=4 -DNUM_CORES=16
CONFIGS += $(CONFIGS_$(NUM_CORES)c)

# include paths
FPU_INCLUDE = -I$(RTL_DIR)/fpu
ifneq (,$(findstring FPU_FPNEW,$(CONFIGS)))
	FPU_INCLUDE += -I$(THIRD_PARTY_DIR)/fpnew/src/common_cells/include -I$(THIRD_PARTY_DIR)/fpnew/src/common_cells/src -I$(THIRD_PARTY_DIR)/fpnew/src/fpu_div_sqrt_mvp/hdl -I$(THIRD_PARTY_DIR)/fpnew/src
endif
RTL_INCLUDE = -I$(RTL_DIR) -I$(RTL_DIR)/libs -I$(RTL_DIR)/interfaces -I$(RTL_DIR)/core -I$(RTL_DIR)/mem -I$(RTL_DIR)/cache -I$(AFU_DIR)
RTL_INCLUDE += $(FPU_INCLUDE)

# Kernel compiler global settings
VPP_FLAGS += --link --target $(TARGET) --platform $(PLATFORM) --save-temps --no_ip_cache
VPP_FLAGS += --vivado.synth.jobs $(JOBS) --vivado.impl.jobs $(JOBS)

ifeq ($(DEV_ARCH), zynquplus)
# ztnq
else ifeq ($(DEV_ARCH), versal)
# versal
else
# alveo
VPP_FLAGS += --connectivity.sp vortex_afu_1.m_axi_mem_0:HBM[0:15]
endif

VPP_FLAGS += --report_level 2
VPP_FLAGS += --config $(SRC_DIR)/vitis.ini

# Enable perf counters
ifdef PERF
	CFLAGS += -DPERF_ENABLE
endif

# Generates profile summary report
ifdef PROFILE
	VPP_FLAGS += --profile_kernel data:all:all:all
	VPP_FLAGS += --profile_kernel stall:all:all:all
endif

ifeq ($(TARGET), hw_emu)
	CFLAGS += -DSIMULATION
endif

# Debugging
ifdef DEBUG
	VPP_FLAGS += -g --debug.protocol all
	ifneq ($(TARGET), hw)
		VPP_FLAGS += --vivado.prop fileset.sim_1.xsim.elaborate.debug_level=all
		CFLAGS += -DDEBUG_LEVEL=$(DEBUG) $(DBG_TRACE_FLAGS)
	else
		CFLAGS += -DNDEBUG
	endif
else
	VPP_FLAGS += --optimize 3
	CFLAGS += -DNDEBUG
endif

# Enable scope analyzer
ifdef SCOPE
	CFLAGS += -DSCOPE $(DBG_SCOPE_FLAGS)
	SCOPE_JSON += $(BUILD_DIR)/scope.json
endif

# compilation flags
CFLAGS += -DSYNTHESIS -DVIVADO
CFLAGS += -DXLEN_$(XLEN)
CFLAGS += $(CONFIGS)
CFLAGS += $(RTL_INCLUDE)

# ast dump flags
XML_CFLAGS = $(filter-out -DSYNTHESIS -DVIVADO, $(CFLAGS)) -I$(DPI_DIR)

# RTL Kernel only supports Hardware and Hardware Emulation.
ifneq ($(TARGET),$(findstring $(TARGET), hw hw_emu))
	$(warning WARNING:Application supports only hw hw_emu TARGET. Please use the target for running the application)
endif

.PHONY: all clean gen-sources gen-ast emconfig check-devices

all: check-devices emconfig $(XCLBIN_CONTAINER) report

gen-sources: $(BUILD_DIR)/sources.txt
$(BUILD_DIR)/sources.txt:
	mkdir -p $(BUILD_DIR); cd $(BUILD_DIR); $(SCRIPT_DIR)/gen_sources.sh -P $(CFLAGS) -Csrc -Osources.txt

gen-ast: $(BUILD_DIR)/vortex.xml
$(BUILD_DIR)/vortex.xml:
	mkdir -p $(BUILD_DIR); cd $(BUILD_DIR); verilator --xml-only -O0 $(XML_CFLAGS) vortex_afu.v --xml-output vortex.xml

scope-json: $(BUILD_DIR)/scope.json
$(BUILD_DIR)/scope.json: $(BUILD_DIR)/vortex.xml
	mkdir -p $(BUILD_DIR); cd $(BUILD_DIR); $(SCRIPT_DIR)/scope.py vortex.xml -o scope.json

gen-xo: $(XO_CONTAINER)
$(XO_CONTAINER): $(BUILD_DIR)/sources.txt
	mkdir -p $(BUILD_DIR); cd $(BUILD_DIR);	$(VIVADO) -mode batch -source $(SRC_DIR)/scripts/gen_xo.tcl -tclargs ../$(XO_CONTAINER) vortex_afu sources.txt $(SCRIPT_DIR) ../$(BUILD_DIR)

gen-bin: $(XCLBIN_CONTAINER)
$(XCLBIN_CONTAINER): $(XO_CONTAINER) $(SCOPE_JSON)
	mkdir -p $(BIN_DIR); cd $(BUILD_DIR); $(VPP) $(VPP_FLAGS) -o ../$(XCLBIN_CONTAINER) ../$(XO_CONTAINER)

emconfig: $(BIN_DIR)/emconfig.json
$(BIN_DIR)/emconfig.json:
	mkdir -p $(BIN_DIR); cd $(BUILD_DIR); emconfigutil --platform $(PLATFORM) --od ../$(BIN_DIR)

report: $(XCLBIN_CONTAINER)
ifeq ($(TARGET),$(findstring $(TARGET), hw))
	cp $(BUILD_DIR)/_x/logs/link/syn/ulp_vortex_afu_1_0_synth_1_runme.log $(BUILD_DIR)/bin/runme.log
	cp $(BUILD_DIR)/_x/reports/link/imp/impl_1_full_util_routed.rpt $(BUILD_DIR)/bin/synthesis.log
	cp $(BUILD_DIR)/_x/reports/link/imp/impl_1_hw_bb_locked_timing_summary_routed.rpt $(BUILD_DIR)/bin/timing.log
endif

hwserver:
	debug_hw --xvc_pcie /dev/xfpga/xvc_pub.u2305.0 --hw_server &

chipscope:
	debug_hw --vivado --host localhost --ltx_file $(BUILD_DIR)/_x/link/vivado/vpl/prj/prj.runs/impl_1/debug_nets.ltx &

clean:
	$(RMDIR) $(BUILD_DIR)

# Check the devices avaiable
check-devices:
ifndef PLATFORM
	$(error PLATFORM not set. Please set the PLATFORM properly and rerun. Run "make help" for more details.)
endif
ifndef XILINX_VITIS
	$(error XILINX_VITIS variable is not set, please set correctly and rerun)
endif
