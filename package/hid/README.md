# HID event services

houji's built-in HID event services from `kernel/overlay/iokit/Kernel/arm/hid`, which UniXNU-houji compiles from
IOHIDFamily: IOHIDEvent, IOHIDEventServiceQueue and IOHIDEventServiceUserClient. They come with SweetHID.cpp, which publishes:

- `SweetHIDButtons`: consumer-control keys, through `sweet_hid_key()`;
- `SweetHIDTouch`: the touch screen, through `sweet_hid_touch()`.

backboardd opens the services marked `HIDServiceSupport`. `IOHIDUsageTables.h` comes from the macOS SDK's Kernel.framework.

Changes for NX769J:

- `sweet_hid_touch()` takes iOS canvas pixels, normalised over the 1125 x 2436 canvas.
- The houji DVFS hook is removed.

The classes take the names IOHIDFamily uses, so the OS profile leaves `com.apple.iokit.IOHIDFamily` out of the prelink.
`patches/xnu/nx769j-hid-personality.patch` matches both services on IOResources. `scripts/build-nx769j-input.py` builds them
together with `package/touch`.
