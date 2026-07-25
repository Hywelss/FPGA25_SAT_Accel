#!/bin/bash

rm -rf v++*.log xcd.log xrc.log src/bin/.run

RD='\033[0;31m'
GN='\033[0;32m'
CY='\033[0;36m'
NC='\033[0m'

COMMAND="${1:-help}"
FREQ_SC="${FREQ_SC:-}"
FREQ=()
FREQ[0]=250000000
FREQ[1]=250000000
FREQ[2]=250000000
FREQ[3]=200000000
FREQ[4]=250000000
FREQ[5]=200000000
FREQ[6]=200000000
EMU_TYPE=sw_emu
LIB_EMU_TYPE=-lxrt_swemu
VER="${VER:-2022.2}"
EN_PROF=""
PLATFORM="${PLATFORM:-xilinx_vck5000_gen4x8_qdma_2_202220_1}"

OPENCL_FILES_CPP="host.cpp xcl2.cpp"
OPENCL_FILES_OBJ="host.o xcl2.o"

VITIS_HLS_CPP=()
VITIS_HLS_CPP[0]="backtrack.cpp color.cpp copy_in.cpp decide.cpp discover.cpp learn.cpp minimize.cpp manage.cpp solver.cpp"
VITIS_HLS_CPP[1]="clause_store_handler.cpp"
VITIS_HLS_CPP[2]="location_handler.cpp"
VITIS_HLS_CPP[3]="restart.cpp"
VITIS_HLS_CPP[4]="timer.cpp"
VITIS_HLS_CPP[5]="priority_queue_functions.cpp pq_handler.cpp"
VITIS_HLS_CPP[6]="message.cpp"
VITIS_HLS_KERNEL=()
VITIS_HLS_KERNEL[0]="solver"
VITIS_HLS_KERNEL[1]="clause_store_handler"
VITIS_HLS_KERNEL[2]="location_handler"
VITIS_HLS_KERNEL[3]="restartCalculator"
VITIS_HLS_KERNEL[4]="timer"
VITIS_HLS_KERNEL[5]="pqHandler"
VITIS_HLS_KERNEL[6]="message"

VITIS_ROOT="${VITIS_ROOT:-/tools/Xilinx/Vitis/$VER}"
VITIS_HLS_ROOT="${VITIS_HLS_ROOT:-/tools/Xilinx/Vitis_HLS/$VER}"
XRT_ROOT="${XRT_ROOT:-/opt/xilinx/xrt}"
VITIS_INCLUDE="${VITIS_INCLUDE:-$VITIS_HLS_ROOT/include}"
XRT_INCLUDE="${XRT_INCLUDE:-$XRT_ROOT/include}"

DATA_PATH="/home/milo168/Desktop/SAT_workspace/SAT_test_cases"

case "$PLATFORM" in
	*vck5000*)
		CONNECTIVITY="${CONNECTIVITY:-k2k_vck5000.cfg}"
		SUPPORTS_HW_EMU=0
		PLATFORM_DEFINE="-DFPGA_VCK5000"
		FREQ_SC="${FREQ_SC:-220}"
		;;
	*)
		CONNECTIVITY="${CONNECTIVITY:-k2k.cfg}"
		SUPPORTS_HW_EMU=1
		PLATFORM_DEFINE=""
		FREQ_SC="${FREQ_SC:-235}"
		;;
esac

# Extra preprocessor defines for both the host and the kernels, e.g.
#   EXTRA_DEFINES=-DOCC_CACHE_STRESS ./runCompile.sh sw_emu
# to shrink the occurrence cache so the regression cases exercise the tiered
# (miss / evict / write-through / cross-tier walk) paths.
PLATFORM_DEFINE="$PLATFORM_DEFINE ${EXTRA_DEFINES:-}"

if [[ ! -f "$XRT_ROOT/setup.sh" || ! -f "$VITIS_ROOT/settings64.sh" ]]
then
	echo -e "${RD}XRT or Vitis setup script was not found.${NC}"
	echo "Set XRT_ROOT, VITIS_ROOT and VITIS_HLS_ROOT for this machine."
	exit 1
fi

source "$XRT_ROOT/setup.sh"
source "$VITIS_ROOT/settings64.sh"

compile_opencl(){
	IS_HW_SIM="-DHW_SIM"

	cd src
	echo -e "${CY}Running Vitis make for $EMU_TYPE... ${NC}"
	mkdir -p obj bin

	if [[ $EMU_TYPE == hw_emu ]]
	then
		LIB_EMU_TYPE=-lxrt_hwemu
	fi

	if [[ $EMU_TYPE == real ]]
	then
		LIB_EMU_TYPE=-lxrt_core
		IS_HW_SIM=""
	fi

	(set -x; g++ -std=c++17 \
	-Wall -Wno-unknown-pragmas \
	-O3 \
	-DFPGA_DEVICE -DC_KERNEL $IS_HW_SIM $PLATFORM_DEFINE \
	-Irapid_json \
	-I$XRT_INCLUDE \
	-I$VITIS_INCLUDE \
	-Isrc \
	-c $OPENCL_FILES_CPP; mv $OPENCL_FILES_OBJ obj) 

	if [ $? -ne 0 ]
	then
    		echo -e "${RD}OpenCL section failed to compile ${NC}"
       		exit 1
	fi

	cd obj

	g++ -o ../bin/test.$EMU_TYPE.out $OPENCL_FILES_OBJ -L"$XRT_ROOT/lib" -lxilinxopencl -lpthread -lrt -lstdc++ -luuid $LIB_EMU_TYPE

	if [ $? -ne 0 ]
	then
		echo -e "${RD}OpenCL section failed to link ${NC}"
		exit 1
	fi

	cd ../../
}

compile_kernel(){
	## emconfig.json needs to be in same folder as the sw-emu/hw-emu xclbin file
	cd src
	if [[ $EMU_TYPE != hw ]]
	then
		emconfigutil --platform "$PLATFORM" --od emconfig_out
		cp emconfig_out/emconfig.json bin
	fi

	PIDS=""
	FAIL=0
	extraCommands="$PLATFORM_DEFINE"
	LINK_OUTPUT="bin/workload-$EMU_TYPE.xclbin"
	if [[ $PLATFORM == *vck5000* ]]
	then
		LINK_OUTPUT="bin/workload-$EMU_TYPE.xsa"
	fi
	(set -x; rm -f bin/*-$EMU_TYPE.xo bin/workload-$EMU_TYPE.xclbin bin/workload-$EMU_TYPE.xsa)

	if [[ $EMU_TYPE == hw_emu || $EMU_TYPE == hw ]]
	then
		extraCommands="$extraCommands -DFPGA_HW"
		#extraCommands="${extraCommands} --advanced.param compiler.fsanitize=address,memory"
		#extraCommands="${extraCommands} --advanced.param compiler.deadlockDetection=true"
	fi

	########## BUILDS KERNEL ##########
	echo -e "${CY}Running Vitis $EMU_TYPE make for the Vitis kernels... ${NC}"

	for VITIS_HLS_CPP_VAL in "${VITIS_HLS_CPP[@]}"
	do

		(set -x; g++ -std=c++17 -w -O3 \
		-I$XRT_INCLUDE \
		-I$VITIS_INCLUDE \
		$PLATFORM_DEFINE \
		-c $VITIS_HLS_CPP_VAL; rm *.o)

		if [ $? -ne 0 ]
		then
			echo -e "${RD}g++ compile check failed ${NC}"
			exit 1
		fi

	done

	for i in "${!VITIS_HLS_CPP[@]}"
	do
		echo -e "${CY} ${VITIS_HLS_KERNEL[i]} kernel running at ${FREQ[i]} ${NC}"

		(set -x; v++ -c -t $EMU_TYPE \
		--temp_dir ../_x \
		--log_dir ../_x \
		--report_dir ../_x \
		--include ./ \
		$extraCommands \
		--platform $PLATFORM \
		-s --kernel ${VITIS_HLS_KERNEL[i]} \
		--hls.clock ${FREQ[i]}:${VITIS_HLS_KERNEL[i]} \
		-R2 \
		${VITIS_HLS_CPP[i]} \
		-o bin/workload-${VITIS_HLS_KERNEL[i]}-$EMU_TYPE.xo) &

		PIDS="$PIDS $!"

	done

	for job in $PIDS
	do
		wait $job || let "FAIL+=1"
	done

	if [ "$FAIL" -ne "0" ];
	then
		echo -e "${RD}Build process failed${NC}"
		exit 1
	fi

	########## Return early, we do not want to generate bitstream for ./runCompile hls ##########
	if [[ $EMU_TYPE == hw ]]
	then
		cd ../
		return
	fi

	########## LINK THE KERNELS TOGETHER ##########
	echo -e "${CY}Running Vitis $EMU_TYPE link... ${NC}"

	v++ -l $EN_PROF -t $EMU_TYPE \
	--temp_dir ../_x \
	--log_dir ../_x \
	--report_dir ../_x \
	--config $CONNECTIVITY \
	--include ./ \
	--platform $PLATFORM \
	--kernel_frequency $FREQ_SC\
	-R2 \
	bin/workload-${VITIS_HLS_KERNEL[0]}-$EMU_TYPE.xo \
	bin/workload-${VITIS_HLS_KERNEL[1]}-$EMU_TYPE.xo \
	bin/workload-${VITIS_HLS_KERNEL[2]}-$EMU_TYPE.xo \
	bin/workload-${VITIS_HLS_KERNEL[3]}-$EMU_TYPE.xo \
	bin/workload-${VITIS_HLS_KERNEL[4]}-$EMU_TYPE.xo \
	bin/workload-${VITIS_HLS_KERNEL[5]}-$EMU_TYPE.xo \
	bin/workload-${VITIS_HLS_KERNEL[6]}-$EMU_TYPE.xo -o "$LINK_OUTPUT"

	if [[ $? -ne 0 || ! -s "$LINK_OUTPUT" ]]
	then
		echo -e "${RD}Failed to link kernel object ${NC}"
		exit 1
	fi

	if [[ $PLATFORM == *vck5000* ]]
	then
		v++ -p -t "$EMU_TYPE" \
		--platform "$PLATFORM" \
		"$LINK_OUTPUT" \
		-o "bin/workload-$EMU_TYPE.xclbin"

		if [[ $? -ne 0 || ! -s "bin/workload-$EMU_TYPE.xclbin" ]]
		then
			echo -e "${RD}VCK5000 packaging failed to produce workload-$EMU_TYPE.xclbin${NC}"
			exit 1
		fi
	fi
	cd ../
}

run_program(){
	OUTPUT_FILE="result_$EMU_TYPE.txt"
	CONFIG_FILE="configuration.json"
	cd src/bin
	rm $OUTPUT_FILE

	../../testcases_sim.sh $EMU_TYPE ../$CONFIG_FILE $OUTPUT_FILE
	if [ $? -ne 0 ]
    then
        exit 1
    fi

	cd ../../
}

if [[ $COMMAND == compilecl || $COMMAND == opencl ]]
then

	EMU_TYPE=sw_emu
	compile_opencl

	EMU_TYPE=hw_emu
	compile_opencl

	EMU_TYPE=real
	compile_opencl
fi

if [[ $COMMAND == compilekernel ]]
then
	if [[ $SUPPORTS_HW_EMU -eq 0 ]]
	then
		echo -e "${RD}$PLATFORM does not support hardware emulation.${NC}"
		echo "Use './runCompile.sh sw_emu' for software emulation or './runCompile.sh hls && ./runCompile.sh hw' for hardware."
		exit 1
	fi
	rm -rf _x
	EMU_TYPE=hw_emu
	#EN_PROF="--profile.data all:all:all --profile.exec all:all"

	compile_kernel
fi

if [[ $COMMAND == run ]]
then
	#EMU_TYPE=sw_emu
	#run_program

	EMU_TYPE=hw_emu
	run_program
fi

if [[ $COMMAND == doall ]]
then
	rm -rf _x
	#EN_PROF="--profile.data all:all:all --profile.exec all:all"

	EMU_TYPE=sw_emu
	compile_opencl	
	compile_kernel
	run_program

	if [[ $SUPPORTS_HW_EMU -eq 1 ]]
	then
		EMU_TYPE=hw_emu
		compile_opencl
		compile_kernel
		run_program
	else
		echo -e "${CY}Skipping hardware emulation because $PLATFORM does not support it.${NC}"
	fi

fi

if [[ $COMMAND == sw_emu ]]
then
	rm -rf _x
	EMU_TYPE=sw_emu
	compile_opencl
	compile_kernel
	run_program
fi

if [[ $COMMAND == hw ]]
then
	export SAT_ACCEL_ROOT="$PWD"
	if [[ $PLATFORM == *vck5000* ]]
	then
		HW_LINK_OUTPUT="src/bin/workload-hw.xsa"
	else
		HW_LINK_OUTPUT="src/bin/workload-hw.xclbin"
	fi
	rm -f "$HW_LINK_OUTPUT" src/bin/workload-hw.xclbin

	v++ -l -t hw \
	--config src/$CONNECTIVITY \
	--include src \
	--platform $PLATFORM \
	--vivado.prop "run.my_rm_synth_1.{STEPS.SYNTH_DESIGN.ARGS.MORE OPTIONS}={-directive AlternateRoutability}" \
	--vivado.prop run.impl_1.STEPS.OPT_DESIGN.TCL.PRE=tcl_scripts/constrain_blocks.tcl \
	--vivado.prop "run.impl_1.{STEPS.PLACE_DESIGN.ARGS.MORE OPTIONS}={-directive AltSpreadLogic_medium}" \
	--vivado.prop "run.impl_1.{STEPS.PHYS_OPT_DESIGN.ARGS.MORE OPTIONS}={-directive Explore}" \
	--vivado.prop run.impl_1.STEPS.PHYS_OPT_DESIGN.TCL.POST=tcl_scripts/phys_opt_loop.tcl \
	--vivado.prop "run.impl_1.{STEPS.ROUTE_DESIGN.ARGS.MORE OPTIONS}={-directive AlternateCLBRouting}" \
	-s --kernel_frequency $FREQ_SC \
	-R1 src/bin/workload-${VITIS_HLS_KERNEL[0]}-hw.xo \
	src/bin/workload-${VITIS_HLS_KERNEL[1]}-hw.xo \
	src/bin/workload-${VITIS_HLS_KERNEL[2]}-hw.xo \
	src/bin/workload-${VITIS_HLS_KERNEL[3]}-hw.xo \
	src/bin/workload-${VITIS_HLS_KERNEL[4]}-hw.xo \
	src/bin/workload-${VITIS_HLS_KERNEL[5]}-hw.xo \
	src/bin/workload-${VITIS_HLS_KERNEL[6]}-hw.xo \
	-o "$HW_LINK_OUTPUT"

	if [[ $? -ne 0 || ! -s "$HW_LINK_OUTPUT" ]]
	then
		echo -e "${RD}Hardware link failed to produce $HW_LINK_OUTPUT${NC}"
		exit 1
	fi

	if [[ $PLATFORM == *vck5000* ]]
	then
		v++ -p -t hw \
		--platform "$PLATFORM" \
		"$HW_LINK_OUTPUT" \
		-o src/bin/workload-hw.xclbin

		if [[ $? -ne 0 || ! -s src/bin/workload-hw.xclbin ]]
		then
			echo -e "${RD}VCK5000 packaging failed to produce workload-hw.xclbin${NC}"
			exit 1
		fi
	fi

	#--vivado.prop run.impl_1.STEPS.OPT_DESIGN.TCL.PRE=tcl_scripts/constrain_blocks.tcl \

	#--vivado.prop run.impl_1.STEPS.OPT_DESIGN.TCL.PRE=tcl_scripts/post_place_rerun.tcl \
	#--vivado.prop run.impl_1.STEPS.PLACE_DESIGN.TCL.POST=tcl_scripts/post_place_qor.tcl \
	#--vivado.prop "run.impl_1.{STEPS.OPT_DESIGN.ARGS.MORE OPTIONS}={-directive ExploreWithRemap}" \
	#--vivado.prop "run.impl_1.{STEPS.PLACE_DESIGN.ARGS.MORE OPTIONS}={-directive EarlyBlockPlacement -timing_summary}" \
	#--vivado.prop "run.impl_1.{STEPS.PHYS_OPT_DESIGN.ARGS.MORE OPTIONS}={-directive Explore}" \
	#--vivado.prop "run.impl_1.{STEPS.PLACE_DESIGN.ARGS.MORE OPTIONS}={-directive SSI_SpreadLogic_high}" \
	#--vivado.prop "run.impl_1.{STEPS.ROUTE_DESIGN.ARGS.MORE OPTIONS}={-directive AlternateCLBRouting}" \

	exit 0
fi

if [[ $COMMAND == hls ]]
then

	rm -rf FPGArpt/ _x
	mkdir FPGArpt
	EMU_TYPE=hw
	compile_kernel

	echo -e "${CY}Copying Vitis HLS reports for ${VITIS_HLS_KERNEL[0]} kernel... ${NC}"
	cp _x/workload-${VITIS_HLS_KERNEL[0]}-hw/${VITIS_HLS_KERNEL[0]}/${VITIS_HLS_KERNEL[0]}/solution/syn/report/*.rpt FPGArpt/

	echo -e "${CY}Copying Vitis HLS reports for ${VITIS_HLS_KERNEL[1]} kernel... ${NC}"
	cp _x/workload-${VITIS_HLS_KERNEL[1]}-hw/${VITIS_HLS_KERNEL[1]}/${VITIS_HLS_KERNEL[1]}/solution/syn/report/*.rpt FPGArpt/

	echo -e "${CY}Copying Vitis HLS reports for ${VITIS_HLS_KERNEL[2]} kernel... ${NC}"
	cp _x/workload-${VITIS_HLS_KERNEL[2]}-hw/${VITIS_HLS_KERNEL[2]}/${VITIS_HLS_KERNEL[2]}/solution/syn/report/*.rpt FPGArpt/

	echo -e "${CY}Copying Vitis HLS reports for ${VITIS_HLS_KERNEL[3]} kernel... ${NC}"
	cp _x/workload-${VITIS_HLS_KERNEL[3]}-hw/${VITIS_HLS_KERNEL[3]}/${VITIS_HLS_KERNEL[3]}/solution/syn/report/*.rpt FPGArpt/

	echo -e "${CY}Copying Vitis HLS reports for ${VITIS_HLS_KERNEL[4]} kernel... ${NC}"
	cp _x/workload-${VITIS_HLS_KERNEL[4]}-hw/${VITIS_HLS_KERNEL[4]}/${VITIS_HLS_KERNEL[4]}/solution/syn/report/*.rpt FPGArpt/

	echo -e "${CY}Copying Vitis HLS reports for ${VITIS_HLS_KERNEL[5]} kernel... ${NC}"
	cp _x/workload-${VITIS_HLS_KERNEL[5]}-hw/${VITIS_HLS_KERNEL[5]}/${VITIS_HLS_KERNEL[5]}/solution/syn/report/*.rpt FPGArpt/

	echo -e "${CY}Copying Vitis HLS reports for ${VITIS_HLS_KERNEL[6]} kernel... ${NC}"
	cp _x/workload-${VITIS_HLS_KERNEL[6]}-hw/${VITIS_HLS_KERNEL[6]}/${VITIS_HLS_KERNEL[6]}/solution/syn/report/*.rpt FPGArpt/

	exit 0
fi

if [[ $COMMAND == help || $COMMAND == -h || $COMMAND == --help ]]
then
	echo "Usage: $0 {hls|hw|opencl|compilecl|sw_emu|compilekernel|run|doall}"
	echo "Default platform: $PLATFORM"
	echo "Override with PLATFORM=<platform> and CONNECTIVITY=<config>."
	exit 0
fi
