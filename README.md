# FPGA25_artifact
## Instructions:

Following needs to be installed and is the version used for our design:  
-XRT version 2.14.384  
-Vitis version 2022.2  
-xilinx_vck5000_gen4x8_qdma_2_202220_1 platform (default)
-gcc/g++ version 10+  

`runCompile.sh` compiles the host program and HLS kernels and generates the
bitstream. The checked-in binary artifacts were built for the original U55C
target and must be rebuilt for VCK5000.

The build script defaults to the paths used on this machine. They can be
overridden without editing the script:

```sh
XRT_ROOT=/path/to/xrt VITIS_ROOT=/path/to/Vitis/2022.2 \
VITIS_HLS_ROOT=/path/to/Vitis_HLS/2022.2 ./runCompile.sh hls
```
To build for real hardware:  
```sh
./runCompile.sh hls && ./runCompile.sh hw
```

VCK5000 uses the `MC_NOC0` memory tag rather than the U55C `HBM[n]` tags.
`runCompile.sh` selects `src/k2k_vck5000.cfg` automatically. The installed
2022.2 VCK5000 platform does not support hardware emulation; use
`./runCompile.sh sw_emu` for software emulation. For hardware, the script
links a Versal `.xsa` first and then packages `src/bin/workload-hw.xclbin`.
The VCK5000 hardware link defaults to 220 MHz (override with `FREQ_SC`). A
235 MHz trial routed successfully but Vitis automatically scaled it to
223 MHz, so 220 MHz avoids relying on automatic frequency scaling. The U55C
default remains 235 MHz.
Because VCK5000 provides 463 URAMs versus 960 on U55C, the VCK5000 build sets
`FPGA_VCK5000` and reduces `_FPGA_MAX_LITERAL_ELEMENTS` from 1,048,576 to
524,288. The host and every kernel receive the same compile-time definition.

To run all emulation modes supported by the selected platform:
```sh
./runCompile.sh doall
```

## To run hardware execution after hardware build:

VCK5000 uses device DDR through `MC_NOC0`; host-memory configuration is not
required. When building for the original U55C platform, enable its host memory
once with:

```sh
sudo PATH_TO_XBUTIL/xbutil configure --host-mem -d DEVICE_ID -s 1G ENABLE  (only need to do once)
```
where PATH_TO_XBUTIL is the install path for xbutil and DEVICE_ID is the device
id reported by `xbutil examine`. If several Xilinx cards are installed, set
`FPGA_DEVICE_NAME` to a unique substring of the desired OpenCL device name.

### To run provided testcases:
-First compile openCL with ./runCompile.sh opencl  
-Then run the ./testcases.sh  
-A .txt file (which is in CSV format) called answers.txt will be found in src/bin  
-ColumnJ is the FPGA runtime in seconds.  
-Please note answers.txt is an appended file. Therefore, it is advised to remove it for new runs.

### To run other SAT instances:
```sh
cd src/bin
./test.real.out workload-hw.xclbin ../configuration.json <YOU_SAT_DIMACS> <SAVE_METRICS_FILE.TXT> <0_OR_1>
```

-To verify our solver, the SAT instance needed to match other solvers. Therefore, you must know beforehand if it is SAT(1) or UNSAT(0).  
-You should modify the host.cpp in src to your desire to not do this check.  
-If host.cpp is modified, remember to recompile with ./runCompile.sh opencl  

## To run MiniSat or Kissat:  
-First clone repository and follow the install instructions provided by those authors.  
https://github.com/niklasso/minisat  
https://github.com/arminbiere/kissat  
-Locate the installed executables and run each individual SAT instance, for example:  
```sh
./minisat PATH_TO_DIMACS/unsat/4_4_2.dimacs  
./kisat PATH_TO_DIMACS/unsat/4_4_2.dimacs
```
where PATH_TO_DIMACS is the folder SAT_test_cases in this repository.  

## Publication
To cite this work:

```bibtex
@inproceedings{10.1145/3706628.3708869,
author = {Lo, Michael and Chang, Mau-Chung Frank and Cong, Jason},
title = {SAT-Accel: A Modern SAT Solver on a FPGA},
year = {2025},
isbn = {9798400713965},
publisher = {Association for Computing Machinery},
address = {New York, NY, USA},
url = {https://doi.org/10.1145/3706628.3708869},
doi = {10.1145/3706628.3708869},
abstract = {Boolean satisfiability (SAT) solving is the first known NP-complete problem and is widely used in many application domains. Over the years, there have been so many consistent improvements in this area such that larger instances can be solved relatively quickly. Although these improvements have found their way onto CPU implementations, there has been limited progress adopting this on hardware accelerators mainly because it is difficult to implement the dynamic data structures needed to support a modern SAT solving algorithm. In this work, we present SAT-Accel, an algorithm-hardware co-design solver that applies many of the core improvements found in modern SAT solvers. SAT-Accel uses a novel memory management system and representation that supports the dynamic data structures required by a modern SAT solving algorithm. Our design can achieve on average a 17.9x speedup against MiniSat, the previous state of the art CPU solver, and a 2.8x speedup against Kissat, the current state of the art CPU solver. Compared to the current state-of-the-art stand-alone hardware accelerator, SAT-Hard, SAT-Accel achieves on average 800.0x speedup.},
booktitle = {Proceedings of the 2025 ACM/SIGDA International Symposium on Field Programmable Gate Arrays},
pages = {234–246},
numpages = {13},
keywords = {accelerator, boolean satisfiability, fpga, high-level synthesis},
location = {Monterey, CA, USA},
series = {FPGA '25}
}
```
