# `forcesim`, a "force-based" market simulator

### Dependencies/requirements

* C++ (likely C++20 or C++17)
  * Google's `glog` logging library (https://github.com/google/glog/)
  * `crow` HTTP server (https://github.com/CrowCpp/Crow)
  * `json` from (https://github.com/nlohmann/json)
  * `boost::program_options` shared library
  * `asio` shared library
* Python
  * Python >= 3.9
  * `pyyaml`, `matplotlib`, `aiohttp`

### Building


1. In the project root, adjust `cmake.sh` based on your system as needed.
Set the following environment variables if needed:

   * `INCLUDE_PREFIX`: `/usr` on Linux (defaults to `/usr/local`)
   * `VENDOR`: path to the the C++ dependency directory: we will have, for example,
`$VENDOR/Crow/include`

2. Clone the `crow`, `json` and `glog` repositories into the dependency directory
3. Adjust `CMakeLists.txt` if needed and run `cmake.sh`
4. In the `build` directory, run `make forcesim`, or other targets as needed

### Python setup

* Navigate to `forcesim/py`
* Run something like `python3.9 -m pip install .`

### Starting the simulator instance

`$ ./forcesim --interface-address 127.0.0.1 --interface-port 18080`

### Running a simulation using the `basic.py` test

* Navigate to `py/tests/basic`
* Copy `config.yml.sample` to `config.yml` and set the `output_path` key
* Adjust and/or create file(s) in `info-json`, `subscribers-json` (currently
some examples are already present)
* Run something like `export PYTHON=python3.9` so that `run.sh` knows how
to run Python (in case there are multiple versions available and/or there
is no link to `python`)
* `./run.sh` to run the simulation, generating output

## Notes

