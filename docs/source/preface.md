# Preface

**TRACE4J** is a lightweight, flexible, and insightful performance tracing tool for Java, designed to bridge the gap between Java source code and its execution behavior on modern CPUs.
It seamlessly integrates **CPU hardware facilities (PMUs and breakpoints)**, **JVM interfaces (JVMTI)**, and the **Linux perf_event** subsystem to deliver accurate and low-overhead runtime tracing for unmodified Java programs.

Unlike traditional profilers or instrumentation-based analyzers, TRACE4J does **not** require code modifications or JVM patches. It works directly on standard JVMs and commodity x86 CPUs, supporting both **end-to-end tracing** and **on-demand tracing** modes.
This makes TRACE4J suitable for both **development debugging** and **production performance analysis**.

---

## Key Features

### Lightweight

Uses hardware sampling and breakpoints to capture runtime events with ≤ 5 % overhead, even on complex applications.

### Flexible

Supports configurable sampling periods and monitoring lengths, enabling users to balance tracing accuracy and performance overhead.

### Insightful

Collects rich hardware-level metrics such as CPU cycles, cache hits/misses, and retired instructions, providing actionable insights beyond simple timing data.

### Non-intrusive

Requires no modification to Java programs, JVM internals, or the Linux kernel.

### Cross-layer Integration

Combines JVM-level call tracing with hardware event counting, achieving function-level attribution of microarchitectural behavior.

### Visualization

Provides an intuitive, Perfetto-based web GUI to visualize function timelines, call paths, and performance metrics.

---

## Design Overview

As illustrated in the following diagram:

![TRACE4J Overview](figures/overview.pdf)

TRACE4J consists of three main components:

### 1. Online Tracer

* Attaches to running Java processes (end-to-end or on-demand).
* Samples function calls via PMUs and monitors return events via hardware breakpoints.
* Collects raw performance data without requiring JVM modifications.

### 2. Offline Data Processor

* Converts raw traces into **granular** and **aggregate** formats.
* Supports statistical analysis such as **mean**, **dispersion**, and **coefficient of variation**.

### 3. Web-based GUI

* Built atop **Perfetto UI**.
* Allows users to navigate traces, inspect function instances, and analyze hotspots interactively.
