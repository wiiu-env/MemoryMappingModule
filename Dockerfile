FROM wiiuenv/devkitppc:20211106

COPY --from=wiiuenv/libkernel:20211031 /artifacts $DEVKITPRO
COPY --from=wiiuenv/libfunctionpatcher:20210924 /artifacts $DEVKITPRO
COPY --from=wiiuenv/wiiumodulesystem:20211207 /artifacts $DEVKITPRO

WORKDIR project