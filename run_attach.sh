#!/bin/bash
export LD_PRELOAD=$Trace4J_HOME/build/preload/libpreload.so
ATTACH=$Trace4J_HOME/bin/jattach/attach
# Pick one of following modes
MODE=VARIANCE::BR_INST_RETIRED.NEAR_CALL@1000000,PERF_COUNT_HW_CACHE_L1D:MISS

DURATION="$1"
PID="$2"
LOAD=load
INSTRUMENT=instrument
TRUE_FLAG=true
FALSE_FLAG=false
JVMTI_AGENT_START=$Trace4J_HOME/build/libagent.so

#"$ATTACH" "$PID" "$LOAD" "$INSTRUMENT" "$FALSE_FLAG" $JAVA_AGENT
"$ATTACH" "$PID" "$LOAD" "$JVMTI_AGENT_START" "$TRUE_FLAG" "$MODE"s
while (( DURATION-- > 0 ))
do
    sleep 1
done
"$ATTACH" "$PID" "$LOAD" "$JVMTI_AGENT_START" "$TRUE_FLAG" "$MODE"p
