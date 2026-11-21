# AHI audio path

PicoDrive produces signed 16-bit stereo PCM. The FRF Amiga host runs a dedicated audio process and keeps emulation from blocking on individual AHI writes.

Current default is 44.1 kHz:

```text
PicoDrive PCM producer
  ↓
32768-frame SPSC ring
  ↓
dedicated Amiga Process
  ↓
two 1024-frame AHI buffers
  ↓
linked SendIO / WaitIO double buffering
  ↓
ahi.device AHIST_S16S
```

The linked request chain keeps AHI streaming rather than repeatedly stopping at block boundaries.

An interleaved audio-master governor protects against underruns. If buffered PCM becomes low, the host may shed an isolated video presentation while emulation/audio production continues. It deliberately does not enter the long multi-frame refill blackout used by the earlier V0.2.14 experiment.

Useful shutdown counters include `dropped`, `silence`, `ioerr`, audio-guard entries, skipped recovery slots, and minimum buffered frames.
