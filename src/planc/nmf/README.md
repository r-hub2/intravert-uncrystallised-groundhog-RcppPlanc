# OpenMP NMF

## Install Instructions

This program depends on:

- [Armadillo](https://arma.sourceforge.net) library for linear algebra operations.
- [OpenBLAS](https://github.com/xianyi/OpenBLAS) for optimized kernels. If building with OpenBLAS and MKL is discoverable by `cmake`, use `-DCMAKE_IGNORE_MKL=1` while building. 

Once the above steps are completed, set the following environment variables.

````
export ARMADILLO_INCLUDE_DIR=/home/rnu/libraries/armadillo-6.600.5/include/
export LIB=$LIB:/home/rnu/libraries/openblas/lib:
export INCLUDE=$INCLUDE:/home/rnu/libraries/openblas/include:$ARMADILLO_INCLUDE_DIR:
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/rnu/libraries/openblas/lib/:
export NMFLIB_DIR=/ccs/home/ramki/rhea/research/nmflib/
export INCLUDE=$INCLUDE:$ARMADILLO_INCLUDE_DIR:$NMFLIB_DIR
export CPATH=$CPATH:$INCLUDE:
export MKLROOT=/ccs/compilers/intel/rh6-x86_64/16.0.0/mkl/
````
If you have got MKL, please source `mklvars.sh` before running `make`/`cmake`

* Create a build directory. 
* Change to the build directory 
* In case of MKL, `source` the ````$MKLROOT/bin/mkl_vars.sh intel64````
* run `cmake` [PATH TO THE CMakeList.txt]. 
* `make`

Refer to the build scripts found in the [build](build/README.md) directory for a sample script.

### Input types
PLANC supports both dense and sparse input matrices via the `-DCMAKE_BUILD_SPARSE` flag. To generate a sparse build run the following command.
```
cmake -DCMAKE_BUILD_SPARSE=1 [path to the CMakeLists.txt]
```
### Other Macros

`cmake` macros
1. `-DCMAKE_BUILD_SPARSE`: For handling sparse input matrices. Default is a dense build.
2. `-DCMAKE_BUILD_TYPE=Debug`: For creating debug builds.
3. `-DCMAKE_WITH_BARRIER_TIMING`: Default is set to barrier timing. To disable build with `-DCMAKE_WITH_BARRIER_TIMING:BOOL=OFF`.
4. `-DCMAKE_BUILD_CUDA`: Default is off.
5. `-DCMAKE_IGNORE_MKL`: Ignores MKL when discoverable by `cmake`. If using OpenBLAS, set `-DCMAKE_IGNORE_MKL=1` while building.

Code level macros - Defined in distutils.h
1. `MPI_VERBOSE` - Be **doubly sure** about what you do. It prints all intermediary matrices. Try this only for very very small matrix that is of size less than 10.
2. `WRITE_RAND_INPUT` - for dumping the generated random matrix.

## Runtime usage

Tell OpenBlas how many threads you want to use. For example on a quad core system use the following.

```
export OPENBLAS_NUM_THREADS=4
export MKL_NUM_THREADS=4
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:MKL_LIB
```

## Command Line options
For a full list of command line options please run `nmf -h`. Some common options are listed below.
* `-k` Sets the low-rank parameter for the approximation.
* `-d` Sets the input dimensions.
* `-e` Switch to toggle error calculation. Off by default.
* `-t` Maximum number of iterations to run the algorithms for.
* `-a` Algorithm to run.
* `-i` Input matrix to run on. Can be a synthetic input via options `rand_lowrank`/`rand_uniform`/`rand_normal` or a path to a file.

## Citation

Please refer to the [papers](papers.md) section for the appropriate reports to cite.
