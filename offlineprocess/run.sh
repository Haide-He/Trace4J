#!/bin/bash

# Set the parent directory
parent_dir="/home/haide/test/moretest/Dacapo"

# Loop through all subdirectories
for dir in "$parent_dir"/*/ ; do
    cd $dir
#    echo  "$dir" >> $parent_dir/time.txt
#    rm *.out *.pftrace
#    python3 /home/haide/test/moretest/splitMethod.py
#    sed -i '$d' agent-trace-method96.run
#    rm agent-trace-method.run
    time python3 /project/offlineprocess/process_raw_data.py
done