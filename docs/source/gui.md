# Graphical User Interface (GUI)

Currently, **TRACE4J** integrates with the **Perfetto UI** framework to visualize performance traces and hardware metrics in an interactive and intuitive way.
The GUI enables developers to explore **function-level timelines**, **hardware counter overlays**, and **data-flow dependencies** directly from their browser.

---

## Table of Contents

1. [Install](#install)
2. [Deploy](#deploy)
3. [Usage & Example](#usage--example)

---

## Install

You can use either the **built-in GUI** packaged inside the Docker image or deploy **Perfetto UI** locally.

### Option 1 — Use the Built-in GUI (Recommended)

When running TRACE4J in the official Docker image, all required components are pre-installed.
The latest image and scripts can be downloaded directly from **Zenodo**:
👉 [https://doi.org/10.5281/zenodo.16900736](https://doi.org/10.5281/zenodo.16900736)

```bash
docker run --rm \
  --cap-add SYS_ADMIN \
  --cap-add SYS_PTRACE \
  --security-opt seccomp=unconfined \
  -p 8888:8888 \
  -it trace4j_ae /bin/bash
```

Inside the container:

```bash
./scripts/GUI.sh
```

Then on your host machine use ssh tunnel to link to sever then open:

> [http://127.0.0.1:8888](http://127.0.0.1:8888)

---

### Option 2 — Install Perfetto UI Locally

If you want to use the GUI outside Docker:

1. **Clone the Perfetto repository**

   ```bash
   git clone https://github.com/Haide-He/Trace4J.git
   cd Trace4J/perfetto
   ```
   
2. **Launch**

   ```bash
   /ui/run-dev-server
   ```
3. Visit [http://localhost:9000](http://localhost:9000) in your browser.
   You can now load TRACE4J trace files (`.pftrace`) directly.

---

## Deploy

TRACE4J provides a simple launch script for serving traces and metrics.

### Using the Provided Script

```bash
./scripts/GUI.sh
```

This command automatically:

* Starts a local HTTP server hosting the Perfetto UI
* Opens port `8888` for browser access


## Usage & Example

### 1. Download from Zenodo (Reproducible Package)

```bash
wget https://zenodo.org/records/16900736/files/trace4j_ae.tar.gz
gzip -d trace4j_ae.tar.gz
```

### 2. Load and Run in Docker

```bash
docker load -i trace4j_ae.tar
./scripts/GUI.sh
```

### 3. Open Browser

Go to [http://127.0.0.1:8888](http://127.0.0.1:8888) and upload the file `your_trace.pftrace`.

### 4. Explore Metrics

In Perfetto UI, you can:

* Zoom and pan along the timeline
* Filter by threads
* View hardware counters such as CPU cycles or cache misses

### Example Screenshot

![TRACE4J GUI Overview](figures/gui.png)

---

**End of GUI Section**
