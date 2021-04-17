FROM wiiuenv/devkitppc:20210414

COPY --from=wiiuenv/libkernel:20210109 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libfunctionpatcher:20210109 /artifacts $DEVKITPRO
COPY --from=wiiuenv/wiiumodulesystem:20210414 /artifacts $DEVKITPRO

WORKDIR project