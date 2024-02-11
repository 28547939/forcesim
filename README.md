# `forcesim`, a "force-based" market simulator

### Dependencies/requirements

* C++
 * Google's `glog` logging library
 * `crow` HTTP server (https://github.com/CrowCpp/Crow)
 * `json` from (https://github.com/nlohmann/json)
 * `boost::program_options`, `boost::random`, and `google::glog`, as shared libraries
* Python
 * Python >= 3.9
 * `pyyaml`, `matplotlib`, `aiohttp`

### Building


1. In the project root, adjust `cmake.sh` based on your system as needed.
Set the following environment variables if needed:

 * `INCLUDE_PREFIX`: `/usr` on Linux (defaults to `/usr/local`)
 * `VENDOR`: path to the the C++ dependency directory: we will have, for example,
`$VENDOR/Crow/include`

2. Adjust `CMakeLists.txt` if needed and run `cmake.sh`
3. Clone the `crow` and `json` repositories into the dependency directory
4. In the `build` directory, run `make forcesim`, or other targets as needed

### Python setup

* Navigate to `forcesim/py`
* Run something like `python3.9 pip install .`

### Starting the simulator instance

`$ ./forcesim --interface-address 127.0.0.1 --interface-port 18080`

### Running a simulation using the `basic.py` test

* Navigate to `py/tests/basic`
* Copy `config.yml.sample` to `config.yml` and set the `output_path` key
* Adjust and/or create file(s) in `info-json`, `subscribers-json`
* Adjust `run.sh` if needed
* `./run.sh` to run the simulation, generating output

## Notes

