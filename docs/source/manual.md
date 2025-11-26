# Manual

## Table of Contents

1. [Operating Environment Check](#1-operating-environment-check)
2. [Online Tracer](#2-online-tracer)
3. [Offline Data Processor](#3-offline-data-processor)
4. [Web-based GUI](#4-web-based-gui)
5. [Optimization Loop](#5-optimization-loop)

---

## 1. Operating Environment Check

Before running Trace4J traces, ensure the hardware components are properly configured.

### Preparation Steps

* Ensure **PMU** and **hardware breakpoint** features are available on your system.
* In containers or restricted environments, verify that `perf_event_open` are permitted.

### Environment Verification

To check permissions and system capabilities:

```bash
sudo sysctl kernel.perf_event_paranoid
```

Make sure `perf_event_paranoid` < 0 for fully access to all processes, CPU-level hardware counters, and kernel events..

If required, adjust permissions temporarily:

```bash
sudo sysctl -w kernel.perf_event_paranoid=-1
```

---

## 2. Online Tracer

### Overview

The **Online Tracer** can attaches to a running Java process(or launch Java process with Trace4J Supervision) and dynamically collects execution traces using **Performance Monitoring Units (PMUs)** and **hardware breakpoints**. It offers flexible configuration to control the sampling rate, traced functions, and number of post-sample instances.

### Features

* **Attach to a running JVM** via JVMTI agent.
* **Sample function calls** using PMU counters (e.g., `BR_INST_RETIRED.NEAR_CALL`, `CPU_CYCLES`, `CACHE_MISSES`).
* **Intercept returns** through hardware breakpoints for fine-grained timing.
* **Configurable sampling depth** — allows a user-adjustable number of function instances to follow each sample.

### Usage

#### Attach to Existing Process

```bash
./run_attach.sh <running time in seconds> <pid>
```

#### Launch with TRACE4J Supervision

```bash
java -agentpath:$Trace4J_HOME/build/libagent.so=VARIANCE::BR_INST_RETIRED.NEAR_CALL@<sample_rate>,<perf_event> <your_program>
```

#### Runtime Requirements

* Ensure `perf_event_open` is enabled.
* Verify the target Java process allows `JVMTI` attachment.

---

## 3. Offline Data Processor

### Purpose

The data processor standardizes performance metrics collected by the tracer into two formats: granular and aggregate formats.

### Command Example

```bash
python3 offlineprocess/process_raw_data.py
```

### Operations

* Map function metrics to corresponding Java method call path.
* Export data for visualization.

### Output Files

* `.pftrace` — the structured trace format for GUI visualization.
* `.out` — exports for external analysis.

---

## 4. Web-based GUI

### Overview

The **TRACE4J Web-based GUI** provides an interactive environment for exploring and analyzing trace results. It is built atop **Perfetto UI**, offering hierarchical and temporal insights into Java method performance.

### Launch GUI

```bash
./scripts/GUI.sh
```

Then open your browser at:
[http://127.0.0.1:8888](http://127.0.0.1:8888)

### Interface Layout

1. **Timeline Pane (Top):**
   Displays method execution instances as rectangles ordered chronologically.
   Each rectangle’s length represents a performance metric (e.g., `PERF_COUNT_HW_INSTRUCTIONS`).

2. **Details Pane (Bottom Left):**
   Shows metadata of a selected instance — method name, call path, and performance counters.

3. **Aggregate Pane (Bottom Right):**
   Displays aggregated statistics:

   * Total, mean and dispersion of metric values
   * Instance counts

### Example

Example: profile a [SableCC](https://sablecc.org) workload.

```bash
java -agentpath:$Trace4J_HOME/build/libagent.so=VARIANCE::BR_INST_RETIRED.NEAR_CALL@100000,PERF_COUNT_HW_INSTRUCTIONS,100 -jar sablecc.jar java-1.5.sablecc
python3 $Trace4J_HOME/offlineprocess/process_raw_data.py
./$Trace4J_HOME/perfetto/ui/run-dev-server
```
![TRACE4J GUI Overview](figures/gui.png)

---

## 5. Optimization Loop

TRACE4J supports iterative optimization and validation.

1. Identify hotspots in GUI.
2. Map results to source code methods.
3. Apply optimizations (e.g., replace linear search with hashmap lookup).
4. Rebuild, rerun TRACE4J.
