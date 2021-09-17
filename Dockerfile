FROM wiiuenv/devkitppc:20210917

COPY --from=wiiuenv/libkernel:20210109 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libfunctionpatcher:20210109 /artifacts $DEVKITPRO
COPY --from=wiiuenv/wiiumodulesystem:20210917 /artifacts $DEVKITPRO

WORKDIR project