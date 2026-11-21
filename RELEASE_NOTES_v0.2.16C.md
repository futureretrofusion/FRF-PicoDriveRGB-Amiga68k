# Future Retro Fusion PicoDriveRGB v0.2.16C

This is the first public-candidate packaging of the Future Retro Fusion AmigaOS 68k PicoDrive port.

The recommended default remains the proven direct-front path:

```text
--frfcp=on --dbuf=off --renderer=accurate --audio-rate=44100 --frameskip=off
```

`--dbuf=on` enables the optional native ScreenBuffer page-flip presenter.

The FRF Emu68 ARM coprocessor firmware is not bundled here. PicoDriveRGB only contains the optional client/ABI side; the firmware project is intended to be released separately with full source and provenance.

No ROMs or BIOS images are included.
