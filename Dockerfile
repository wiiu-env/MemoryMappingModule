FROM ghcr.io/wiiu-env/devkitppc:20240505

COPY --from=ghcr.io/wiiu-env/libkernel:20230621 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/libfunctionpatcher:20230621 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/wiiumodulesystem:20240424 /artifacts $DEVKITPRO

WORKDIR project
