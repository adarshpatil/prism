#!/bin/bash
set -x
# run this script as ./trace-gen-simlarge-roi.sh BENCHMARK_NAME THREADS
AFS_HOME=/afs/inf.ed.ac.uk/user/s18/s1897969
THREADS=$2
AFS_PROJ=/afs/inf.ed.ac.uk/group/project/dramrep
DISK_LOCAL=/disk/local/s1897969

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$AFS_PROJ/parsec-benchmark/pkgs/libs/hooks/inst/amd64-linux.gcc-hooks/lib
export LIBRARY_PATH=$LIBRARY_PATH:$AFS_PROJ/parsec-benchmark/pkgs/libs/hooks/inst/amd64-linux.gcc-hooks/lib

PARSEC_CD=("cd $AFS_PROJ/parsec-benchmark/pkgs/" "/run")
PARSEC_EXEC=("$AFS_HOME/prism/build/bin/prism --backend=stgen  -l textv2 -o $AFS_PROJ/traces/${THREADS}t-large-roi-parsec/" " --frontend=valgrind --start-func=__parsec_roi_begin --stop-func=__parsec_roi_end")

SPLASH_CD=("cd $AFS_PROJ/parsec-benchmark/ext/splash2x/" "/run")
SPLASH_EXEC=("$AFS_HOME/prism/build/bin/prism --backend=stgen  -l textv2 -o $DISK_LOCAL/traces/${THREADS}t-large-roi-splash/" " --frontend=valgrind --start-func=__parsec_roi_begin --stop-func=__parsec_roi_end")

function run() {
BENCH=$1

if [ $BENCH == "blackscholes" ]
then
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/blackscholes $THREADS in_64K.txt prices.txt" > $AFS_PROJ/traces/${THREADS}t-large-roi-parsec/$BENCH/tg.out 2>&1

elif [ $BENCH == "bodytrack" ]
then
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/bodytrack sequenceB_4 4 4 4000 5 0 $THREADS" > $AFS_PROJ/traces/${THREADS}t-large-roi-parsec/$BENCH/tg.out 2>&1

elif [ $BENCH == "canneal" ]
then
${PARSEC_CD[0]}"kernels/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/canneal $THREADS 15000 2000 400000.nets 128" > $AFS_PROJ/traces/${THREADS}t-large-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "dedup" ]
then
${PARSEC_CD[0]}"kernels/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/dedup -c -p -v -t $THREADS -i media.dat -o output.dat.ddp" > $AFS_PROJ/traces/${THREADS}t-large-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "facesim" ]
then
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/facesim -timing -threads $THREADS" > $AFS_PROJ/traces/${THREADS}t-large-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "ferret" ]
then
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/ferret corel lsh queries 10 20 $THREADS output.txt" > $AFS_PROJ/traces/${THREADS}t-large-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "fluidanimate" ]
then
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/fluidanimate $THREADS 5 in_300K.fluid out.fluid" > $AFS_PROJ/traces/${THREADS}t-large-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "freqmine" ]
then
export OMP_NUM_THREADS=$THREADS
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/freqmine kosarak_990k.dat 790" > $AFS_PROJ/traces/${THREADS}t-large-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "streamcluster" ]
then
${PARSEC_CD[0]}"kernels/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/streamcluster 10 20 128 16384 16384 1000 none output.txt $THREADS" > $AFS_PROJ/traces/${THREADS}t-large-roi-parsec/$BENCH/tg.out 2>&1 

elif [ $BENCH == "vips" ]
then
export IM_CONCURRENCY=$THREADS
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/vips im_benchmark bigben_2662x5500.v output.v" > $AFS_PROJ/traces/${THREADS}t-large-roi-parsec/$BENCH/tg.out 2>&1

elif [ $BENCH == "x264" ]
then
${PARSEC_CD[0]}"apps/"$BENCH${PARSEC_CD[1]}
${PARSEC_EXEC[0]}$BENCH${PARSEC_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/x264 --quiet --qp 20 --partitions b8x8,i4x4 --ref 5 --direct auto --b-pyramid --weightb --mixed-refs --no-fast-pskip --me umh --subme 7 --analyse b8x8,i4x4 --threads $THREADS -o eledream.264 eledream_640x360_128.y4m" > $AFS_PROJ/traces/${THREADS}t-large-roi-parsec/$BENCH/tg.out 2>&1

elif [ $BENCH == "cholesky" ]
then
${SPLASH_CD[0]}"kernels/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/cholesky -p16 < tk29.O" > $AFS_PROJ/traces/${THREADS}t-large-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "ocean_cp" ]
then
${SPLASH_CD[0]}"apps/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/ocean_cp -n2050 -p16 -e1e-07 -r20000 -t28800" > $DISK_LOCAL/traces/${THREADS}t-large-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "lu_cb" ]
then
${SPLASH_CD[0]}"kernels/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/lu_cb -p16 -n2048 -b16" > $DISK_LOCAL/traces/${THREADS}t-large-roi-splash/$BENCH/tg.out  2>&1


elif [ $BENCH == "fft" ]
then
${SPLASH_CD[0]}"kernels/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/fft -m24 -p16" > $DISK_LOCAL/traces/${THREADS}t-large-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "radix" ]
then
${SPLASH_CD[0]}"kernels/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/radix -p16 -r4096 -n67108864 -m2147483647" > $DISK_LOCAL/traces/${THREADS}t-large-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "raytrace" ]
then
${SPLASH_CD[0]}"apps/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/raytrace -s -p16 -a8 balls4.env" > $DISK_LOCAL/traces/${THREADS}t-large-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "barnes" ]
then
${SPLASH_CD[0]}"apps/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/barnes 16 < input_16" > $DISK_LOCAL/traces/${THREADS}t-large-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "volrend" ]
then
${SPLASH_CD[0]}"apps/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/volrend 16 head-scaleddown2 100" > $AFS_PROJ/traces/${THREADS}t-large-roi-splash/$BENCH/tg.out  2>&1

elif [ $BENCH == "water_nsquared" ]
then
${SPLASH_CD[0]}"apps/"$BENCH${SPLASH_CD[1]}
${SPLASH_EXEC[0]}$BENCH${SPLASH_EXEC[1]} --executable="../inst/amd64-linux.gcc-hooks/bin/water_nsquared 16 < input_16" > $AFS_PROJ/traces/${THREADS}t-large-roi-splash/$BENCH/tg.out  2>&1

fi
}

run $1
