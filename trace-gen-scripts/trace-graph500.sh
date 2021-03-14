# identify region of interest for graph500
export OMP_NUM_THREADS=16
cd $AFS_HOME/graph500-2.1.4/omp-csr
$AFS_HOME/prism/build/bin/prism --backend=stgen -l textv2  -o /disk/local/s1897969/traces/16t-graph500 --executable="./omp-csr -s 20" > /disk/local/s1897969/traces/16t-graph500/trace-gen-out 2>&1 & disown
