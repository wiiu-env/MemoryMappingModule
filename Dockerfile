FROM wiiuenv/devkitppc:20211229

COPY --from=wiiuenv/libkernel:20211031 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libfunctionpatcher:20210924 /artifacts $DEVKITPRO
COPY --from=wiiuenv/wiiumodulesystem:20220123 /artifacts $DEVKITPRO

WORKDIR project