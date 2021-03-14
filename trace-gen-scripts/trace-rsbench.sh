set -x
# identify region of interest for graph500
export OMP_NUM_THREADS=16
cd $AFS_HOME/RSBench/openmp-threading
$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2  -o /disk/scratch/s1897969/traces/16t-large-roi-hpc/rsbench --start-func=roi_begin --stop-func=roi_end --executable="./rsbench -t 16" > /disk/scratch/s1897969/traces/16t-large-roi-hpc/rsbench/trace-gen-out 2>&1 & disown
