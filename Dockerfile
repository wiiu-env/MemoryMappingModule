FROM ghcr.io/wiiu-env/devkitppc:20240423

COPY --from=ghcr.io/wiiu-env/libkernel:20230621 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/libfunctionpatcher:20230621 /artifacts $DEVKITPRO
COPY --from=ghcr.io/wiiu-env/wiiumodulesystem:20230719 /artifacts $DEVKITPRO

WORKDIR project
