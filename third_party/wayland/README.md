# Wayland protocols: presentation-time, fifo-v1

From wayland-protocols (MIT license, in each file): `presentation-time.xml` (stable) and
`fifo-v1.xml` (staging). The `-client.h` and `-protocol.c` files are generated from them, so that
building doesn't need wayland-scanner or the wayland-protocols package:

```bash
wayland-scanner client-header presentation-time.xml presentation-time-client.h
wayland-scanner private-code presentation-time.xml presentation-time-protocol.c
wayland-scanner client-header fifo-v1.xml fifo-v1-client.h
wayland-scanner private-code fifo-v1.xml fifo-v1-protocol.c
```

`runtime/host/pacing.cpp` uses presentation-time to learn when each frame reached the screen, and
fifo-v1 to hold each frame for exactly its refreshes.
