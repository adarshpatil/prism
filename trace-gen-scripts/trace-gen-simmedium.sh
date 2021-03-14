#!/bin/bash
set -x
AFS_HOME=/afs/inf.ed.ac.uk/user/s18/s1897969
THREADS=$1
AFS_PROJ=/afs/inf.ed.ac.uk/group/project/dramrep

#cd $AFS_HOME/parsec-benchmark/pkgs/apps/blackscholes/run
#$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2 -o $AFS_PROJ/traces/${THREADS}t-med-parsec/blackscholes --executable="../inst/amd64-linux.gcc/bin/blackscholes $THREADS in_16K.txt prices.txt"

cd $AFS_HOME/parsec-benchmark/pkgs/apps/bodytrack/run
$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2 -o $AFS_PROJ/traces/${THREADS}t-med-parsec/bodytrack --executable="../inst/amd64-linux.gcc/bin/bodytrack sequenceB_2 4 2 2000 5 0 $THREADS"

cd $AFS_HOME/parsec-benchmark/pkgs/kernels/canneal/run
$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2 -o $AFS_PROJ/traces/${THREADS}t-med-parsec/canneal --executable="../inst/amd64-linux.gcc/bin/canneal $THREADS 15000 2000 200000.nets 64"

#cd $AFS_HOME/parsec-benchmark/pkgs/kernels/dedup/run
#$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2 -o $AFS_PROJ/traces/${THREADS}t-med-parsec/dedup --executable="../inst/amd64-linux.gcc/bin/dedup -c -p -v -t $THREADS -i media.dat -o output.dat.ddp"

#cd $AFS_HOME/parsec-benchmark/pkgs/apps/facesim/run
#$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2 -o $AFS_PROJ/traces/${THREADS}t-med-parsec/facesim --executable="../inst/amd64-linux.gcc/bin/facesim -timing -threads $THREADS"

cd $AFS_HOME/parsec-benchmark/pkgs/apps/ferret/run
$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2 -o $AFS_PROJ/traces/${THREADS}t-med-parsec/ferret --executable="../inst/amd64-linux.gcc/bin/ferret corel lsh queries 10 20 $THREADS output.txt"

cd $AFS_HOME/parsec-benchmark/pkgs/apps/fluidanimate/run
$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2 -o $AFS_PROJ/traces/${THREADS}t-med-parsec/fluidanimate --executable="../inst/amd64-linux.gcc/bin/fluidanimate $THREADS 5 in_100K.fluid out.fluid"

cd $AFS_HOME/parsec-benchmark/pkgs/apps/freqmine/run
export OMP_NUM_THREADS=$THREADS
$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2 -o $AFS_PROJ/traces/${THREADS}t-med-parsec/frewmine --executable="../inst/amd64-linux.gcc/bin/freqmine kosarak_500k.dat 410"

cd $AFS_HOME/parsec-benchmark/pkgs/kernels/streamcluster/run
$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2 -o $AFS_PROJ/traces/${THREADS}t-med-parsec/streamcluster --executable="../inst/amd64-linux.gcc/bin/streamcluster 10 20 64 8192 8192 1000 none output.txt $THREADS"

cd $AFS_HOME/parsec-benchmark/pkgs/apps/vips/run
export IM_CONCURRENCY=$THREADS
$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2 -o $AFS_PROJ/traces/${THREADS}t-med-parsec/vips --executable="../inst/amd64-linux.gcc/bin/vips im_benchmark vulture_2336x2336.v output.v"

#cd $AFS_HOME/parsec-benchmark/pkgs/apps/x264/run
#$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2 -o $AFS_PROJ/traces/${THREADS}t-med-parsec/x264 --executable="../inst/amd64-linux.gcc/bin/x264 --quiet --qp 20 --partitions b8x8,i4x4 --ref 5 --direct auto --b-pyramid --weightb --mixed-refs --no-fast-pskip --me umh --subme 7 --analyse b8x8,i4x4 --threads $THREADS -o eledream.264 eledream_640x360_32.y4m"
