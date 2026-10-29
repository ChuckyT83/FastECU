# Separate reviewed OpenPort USB lifecycle derivative

This BSD-3-Clause component is derived from NikolaKozina/j2534 revision
22ae8e85aa421dd74b2a3678bd9bf0f0db8ff55a and the previously reviewed diagnostic
patch. It is kept separate from FastECU application/SH7058 production code.
The accompanying source-revision.json pins all reviewed inputs. build.py refuses
changed source/dependency inputs, builds a Windows x64 DLL and ABI assertions,
and optionally executes only the fake-USB harness (never the real DLL).

Changes are limited to initialization/shutdown error preservation, partial USB
resource cleanup, exclusive in-library ownership, acknowledgment deadlines/short
writes in the existing helper, enabled log flushing and USBQUAL/1 lifecycle
markers. Commands remain ati, ata and atz. No updater, bootloader, persistent
configuration command, channel qualification or CAN/flashing repair is added.
General WriteMsgs/filter/IOCTL limitations remain; this is NOT a qualified flasher.

Example from a Windows x64 MinGW developer environment (software-only build):

    python build.py --cc gcc --libusb C:\deps\libusb-1.0.29 --output C:\build\driver --native-mocks

The libusb directory must contain the extracted official 1.0.29 Windows archive.
Its archive SHA-256 and DLL SHA-256 are in source-revision.json. On Linux use the
x64 MinGW compiler path without --native-mocks and run the existing fake-USB
harness with the native GCC separately. No installed driver is overwritten.
Native CI uses the existing diagnostic toolkit runner, with a build-time pin to
its separately produced driver artifact. Production hash pins cannot be changed
by runtime environment variables. Test-only fixture pins exist solely in the
separately labelled test executable, which must never be packaged for hardware.
