# Dependency patches

`lsmashworks-audio-only-index.patch` fixes an invalid read in L-SMASH-Works
when `LWLibavAudioSource` reads an existing index without loading the video
frame records that the downstream index setup still expects. The fault can
appear when an AviSynth script is opened again in the same process. Apply it at
the root of the L-SMASH-Works source tree before building
`libLSMASHSource.so`:

```sh
patch -p1 -i /path/to/lsmashworks-audio-only-index.patch
```

The patch is based on L-SMASH-Works commit
`6ae441a195dc65fa201c228c7ac6143bfdbedb0e`.
