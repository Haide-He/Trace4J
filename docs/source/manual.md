# Manual

## Table of Contents

1. [Profile Using Trace4J](#profile-using-trace4j)
2. [Interpret Profile Data](#interpret-profile-data)
3. [Example](#example)

---

## Profile Using Trace4J

The `trace4j` automates tracing and supports flexible control via knobs.

**Operation modes:**

* **On-demand attach:** Attach Trace4J to a running Java process.
* **End-to-end launch:** Start a Java program directly under Trace4J.

Typical usage:

```bash
java -agentpath:$Trace4J_HOME/build/libagent.so=VARIANCE::BR_INST_RETIRED.NEAR_CALL@<sample_rate>,<perf_event> <your_program>
```

---

## Interpret Profile Data

Once tracing and processing are complete, use the web GUI to interpret results.

The interface consists of **three panes**:

1. **Top Pane – Execution Timeline**
   displays method execution instances as colored rectangles arranged chronologically.
   Each rectangle’s length is proportional to its performance metric (e.g., retired instructions `PERF_COUNT_HW_INSTRUCTIONS`).

2. **Bottom-Left Pane – Instance Details**
   shows detailed information about the selected method instance, including:

    * Method name and class
    * Call path (caller–callee chain)
    * Hardware metrics such as CPU cycles, cache-misses, and instruction counts

3. **Bottom-Right Pane – Aggregate Statistics**
   Summarizes all instances of the selected method:

    * Number of occurrences
    * Total, mean, and dispersion of collected metrics

---

## Example

Example: profiling a matrix-multiplication workload.

```bash
javac MatrixMultiply.java
java -agentpath:$Trace4J_HOME/build/libagent.so=VARIANCE::BR_INST_RETIRED.NEAR_CALL@<sample_rate>,<perf_event> MatrixMultiply 
python3 $Trace4J_HOME/offlineprocess/process_raw_data.py
./$Trace4J_HOME/perfetto/ui/run-dev-server
```

When the GUI opens, inspect the `MatrixMultiply.multiply` method. Trace4J may show high cache-miss rates.

---

**End of Manual**
