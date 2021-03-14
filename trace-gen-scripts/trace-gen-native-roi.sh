#!/bin/bash
set -x
# run this script as ./trace-gen-native-roi.sh BENCHMARK_NAME THREADS
# this scipt produces N ops traces (change the -n flag in line 13 and 16 if needed)
AFS_HOME=/afs/inf.ed.ac.uk/user/s18/s1897969
THREADS=$2
TRACE_DIR=/disk/local/s1897969
AFS_PROJ=/afs/inf.ed.ac.uk/group/project/dramrep


export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$AFS_PROJ/parsec-benchmark/pkgs/libs/hooks/inst/amd64-linux.gcc-hooks/lib
export LIBRARY_PATH=$LIBRARY_PATH:$AFS_PROJ/parsec-benchmark/pkgs/libs/hooks/inst/amd64-linux.gcc-hooks/lib

PARSEC_CD=("cd $AFS_PROJ/parsec-benchmark/pkgs/" "/run")
PARSEC_EXEC=("$AFS_HOME/prism/build/bin/prism --backend=stgen -n 70000000000  -l textv2 -o $TRACE_DIR/traces/${THREADS}t-native-roi-parsec/" " --frontend=valgrind --start-func=__parsec_roi_begin --stop-func=__parsec_roi_end")

SPLASH_CD=("cd $AFS_PROJ/parsec-benchmark/ext/splash2x/" "/run")
SPLASH_EXEC=("$AFS_HOME/prism/build/bin/prism --backend=stgen -n 70000000000 -l textv2 -o $TRACE_DIR/traces/${THREADS}t-native-roi-splash/" " --frontend=valgrind --start-func=__parsec_roi_begin --stop-func=__parsec_roi_end")

function run() {
BENCH=$1

if [ $BENCH == "blackscholes" ]
then
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/blackscholes $THREADS in_10M.txt prices.txt" > $TRACE_DIR/traces/${THREADS}t-native-roi-parsec/$BENCH/tg.out 2>&1

elif [ $BENCH == "bodytrack" ]
then
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/bodytrack sequenceB_261 4 261 4000 5 0 $THREADS" > $TRACE_DIR/traces/${THREADS}t-native-roi-parsec/$BENCH/tg.out 2>&1

elif [ $BENCH == "canneal" ]
then
${PARSEC_CD[0]}"kernels/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/canneal $THREADS 15000 2000 2500000.nets 6000" > $TRACE_DIR/traces/${THREADS}t-native-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "dedup" ]
then
${PARSEC_CD[0]}"kernels"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="" > $TRACE_DIR/traces/${THREADS}t-native-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "facesim" ]
then
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/facesim -timing -threads $THREADS -lastframe 100" > $TRACE_DIR/traces/${THREADS}t-native-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "ferret" ]
then
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/ferret corel lsh queries 50 20 $THREADS output.txt" > $TRACE_DIR/traces/${THREADS}t-native-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "fluidanimate" ]
then
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/fluidanimate $THREADS 500 in_500K.fluid out.fluid" > $TRACE_DIR/traces/${THREADS}t-native-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "freqmine" ]
then
export OMP_NUM_THREADS=$THREADS
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/freqmine webdocs_250k.dat 11000" > $TRACE_DIR/traces/${THREADS}t-native-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "streamcluster" ]
then
${PARSEC_CD[0]}"kernels/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/streamcluster 10 20 128 1000000 200000 5000 none output.txt $THREADS" > $TRACE_DIR/traces/${THREADS}t-native-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "vips" ]
then
export IM_CONCURRENCY=$THREADS
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="" > $TRACE_DIR/traces/${THREADS}t-native-roi-parsec/$BENCH/tg.out 2>&1

elif [ $BENCH == "x264" ]
then
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="" > $TRACE_DIR/traces/${THREADS}t-native-roi-parsec/$BENCH/tg.out 2>&1

elif [ $BENCH == "cholesky" ]
then
${SPLASH_CD[0]}"kernels/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/cholesky -p16 < tk29.O" > $TRACE_DIR/traces/${THREADS}t-native-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "ocean_cp" ]
then
${SPLASH_CD[0]}"apps/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/ocean_cp -n4098 -p16 -e1e-07 -r10000 -t14400" > $TRACE_DIR/traces/${THREADS}t-native-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "lu_cb" ]
then
${SPLASH_CD[0]}"kernels/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/lu_cb -p16 -n8096 -b32" > $TRACE_DIR/traces/${THREADS}t-native-roi-splash/$BENCH/tg.out  2>&1


elif [ $BENCH == "fft" ]
then
${SPLASH_CD[0]}"kernels/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/fft -m28 -p16" > $TRACE_DIR/traces/${THREADS}t-native-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "radix" ]
then
${SPLASH_CD[0]}"kernels/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/radix -p16 -r4096 -n268435456 -m2147483647" > $TRACE_DIR/traces/${THREADS}t-native-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "raytrace" ]
then
${SPLASH_CD[0]}"apps/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/raytrace -s -p16 -a128 car.env" > $TRACE_DIR/traces/${THREADS}t-native-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "radiosity" ]
then
${SPLASH_CD[0]}"apps/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/radiosity -bf 1.5e-4 -batch -largeroom -p 16" > $TRACE_DIR/traces/${THREADS}t-native-roi-splash/$BENCH/tg.out  2>&1


elif [ $BENCH == "barnes" ]
then
${SPLASH_CD[0]}"apps/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/barnes 16 < input_16" > $TRACE_DIR/traces/${THREADS}t-native-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "volrend" ]
then
${SPLASH_CD[0]}"apps/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/volrend 16 head 1000" > $TRACE_DIR/traces/${THREADS}t-native-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "watern2" ]
then
${SPLASH_CD[0]}"apps/water_nsquared"${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/water_nsquared 16 < input_16" > $TRACE_DIR/traces/${THREADS}t-native-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "fmm" ]
then
${SPLASH_CD[0]}"apps/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/fmm 16 < input_16" > $TRACE_DIR/traces/${THREADS}t-native-roi-splash/$BENCH/tg.out  2>&1


fi
}

run $1


