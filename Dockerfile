FROM wiiuenv/devkitppc:20210101

COPY --from=wiiuenv/libkernel:20210109 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libfunctionpatcher:20210109 /artifacts $DEVKITPRO
COPY --from=wiiuenv/wiiumodulesystem:20210219 /artifacts $DEVKITPRO

WORKDIR project