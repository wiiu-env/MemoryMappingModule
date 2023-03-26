FROM ghcr.io/wiiu-env/devkitppc:20230326

COPY --from=ghcr.io/wiiu-env/libkernel:20220904 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/libfunctionpatcher:20230106 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/wiiumodulesystem:20230106 /artifacts $DEVKITPRO

WORKDIR project
