# Installation

## Dependencies

Before installing Trace4J, ensure the following prerequisites are met:

* Intel Broadwell CPUs or later.
* Linux x86-64 systems (kernel version ≥ 5.15).
* A JDK 8 or above installed and available in your `PATH`.
* C/C++ tooling for building native components (e.g., GCC, Make, and CMake).
* `perf_event_open` support enabled in your kernel, and system permissions to use PMUs (e.g., `CAP_SYS_ADMIN` or `perf_event_paranoid` appropriately configured).
* For the GUI and trace-processing pipeline: Python 3.10 or above, and a modern web browser.

---

## Download & Build

You can either **download the prebuilt artifact from Zenodo** or **build from source**.

### Option 1 — Download Prebuilt Package (Recommended)

The latest official release is available at Zenodo:
👉 [https://doi.org/10.5281/zenodo.16900736](https://doi.org/10.5281/zenodo.16900736)

```bash
wget https://zenodo.org/records/16900736/files/trace4j_ae.tar.gz
gzip -d trace4j_ae.tar.gz
docker load -i trace4j_ae.tar
```

This package includes a ready-to-run Docker image with Trace4J and all dependencies.

### Option 2 — Build from Source

1. **Clone the repository:**

   ```bash
   git clone https://github.com/Haide-He/Trace4J.git
   cd Trace4J
   ```

2. **Build and Install the native tracer components:**

   ```bash
   make
   ```

---

## Docker / Reproducible Image

For reproducible experiments (especially for artifact evaluation or production deployment), Trace4J provides a ready-to-use Docker image:


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
./scripts/install.sh  
```

Then start the GUI:

```bash
./scripts/GUI.sh  
```

On your host machine, map port 8888 and open the GUI at [http://127.0.0.1:8888](http://127.0.0.1:8888).

---

## Environment Variables & PATH Setup

Once installed, you may need to set or export environment variables to use Trace4J smoothly:

```bash
export PRECISE_IP=1
export Trace4J_HOME=/path/to/Trace4J
export PATH=$Trace4J_HOME/bin:$PATH
export LD_LIBRARY_PATH=$Trace4J_HOME/lib:$LD_LIBRARY_PATH
```

If you use the Docker image, these environment variables are preconfigured automatically.

