# Workflow

## Table of Contents

1. [Patch the Runtime / Instrumentation](#patch-the-runtime--instrumentation)
2. [Execute the Tracer and Collect Data](#execute-the-tracer-and-collect-data)
3. [Offline Processing and Aggregation](#offline-processing-and-aggregation)
4. [Visualization & Interpretation](#visualization--interpretation)
5. [Optimization Loop](#optimization-loop)

---

## Patch the Runtime / Instrumentation

Before running full-scale traces, ensure the runtime and instrumentation components are properly configured.

* Apply the Java runtime agent via JVMTI.
* Ensure PMU and hardware breakpoint features are available on your system.
* In containers or restricted environments, verify that `ptrace` and `perf_event_open` are permitted.

---

## Execute the Tracer and Collect Data

Use TRACE4J to collect runtime traces from Java applications.

### Modes of Operation

* **On-demand attach:** Attach to an existing Java process.
* **Launch wrapper:** Start the program directly under TRACE4J supervision.

### Example

```bash
java -agentpath:$Trace4J_HOME/build/libagent.so=VARIANCE::BR_INST_RETIRED.NEAR_CALL@<sample_rate>,<perf_event> <your_program>
```

During execution, TRACE4J records:

* Function entry.
* Hardware counter (CPU cycles, cache misses, instructions retired).
* Breakpoint-triggered traces.

For long runs, ensure trace data is periodically flushed to disk to prevent buffer overflow.

---

## Offline Processing and Aggregation

After collection, process the raw data to produce aggregated insights.

### Process Traces

```bash
python3 /offlineprocess/process_raw_data.py
```

### What Happens in Processing

* Maps metrics to Java functions.
* Aggregates per-instance data into statistical summaries.
* Produces `.pftrace` data for GUI visualization.

---

## Visualization & Interpretation

Once processing completes, open the TRACE4J GUI to explore results.

### Launch GUI

```bash
./scripts/GUI.sh
```

Then open [http://127.0.0.1:8888](http://127.0.0.1:8888) in your browser.

---

## Optimization Loop

TRACE4J supports iterative optimization and validation.

1. Identify hotspots or redundant patterns in GUI.
2. Map results to source code lines or methods.
3. Apply optimizations (e.g., data structure layout, loop transformations).
4. Rebuild, rerun TRACE4J, and compare updated metrics.

---

**End of Workflow**
