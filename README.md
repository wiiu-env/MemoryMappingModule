Run via [SetupPayload](https://github.com/wiiu-env/SetupPayload). Requires [wut](https://github.com/decaf-emu/wut), [wums](https://github.com/wiiu-env/WiiUModuleSystem), [libkernel](https://github.com/wiiu-env/libkernel) and [libfunctionpatcher](https://github.com/wiiu-env/libfunctionpatcher) for building.

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