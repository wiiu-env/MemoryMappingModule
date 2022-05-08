## Usage
(`[ENVIRONMENT]` is a placeholder for the actual environment name.)

1. Copy the file `MemoryMappingModule.wms` into `sd:/wiiu/environments/[ENVIRONMENT]/modules`.  
2. Requires the [WUMSLoader](https://github.com/wiiu-env/WUMSLoader) in `sd:/wiiu/environments/[ENVIRONMENT]/modules/setup`.

## Buildflags

### Logging
Building via `make` only logs errors (via OSReport). To enable logging via the [LoggingModule](https://github.com/wiiu-env/LoggingModule) set `DEBUG` to `1` or `VERBOSE`.

`make` Logs errors only (via OSReport).  
`make DEBUG=1` Enables information and error logging via [LoggingModule](https://github.com/wiiu-env/LoggingModule).  
`make DEBUG=VERBOSE` Enables verbose information and error logging via [LoggingModule](https://github.com/wiiu-env/LoggingModule).  

## Building using the Dockerfile

It's possible to use a docker image for building. This way you don't need anything installed on your host system.

```
# Build docker image (only needed once)
docker build . -t memorymappingmodule-builder

# make 
docker run -it --rm -v ${PWD}:/project memorymappingmodule-builder make

# make clean
docker run -it --rm -v ${PWD}:/project memorymappingmodule-builder make clean
```

## Format the code via docker
`docker run --rm -it  -v ${PWD}:/src  wiiuenv/clang-format:13.0.0-2 -r ./source  -i`