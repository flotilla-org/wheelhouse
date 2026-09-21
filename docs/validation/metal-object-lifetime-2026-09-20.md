# Metal object lifetime, 2026-09-20

Wheelhouse's custom macOS update loop did not establish an autorelease pool.
Metal command buffers, encoders and descriptors created through autoreleasing
factory methods accumulated across frames. Explicit texture retirement did not
release those independently retained objects. This affects ordinary terminal
rendering as well as Jackstay CPU-frame presentation.

The initial system-exhaustion report is
`/tmp/wheelhouse-metal-shared-event-exhaustion-2026-09-20.md`, with supporting
artifacts under `/tmp/porthole-metal-probe-n6QCDC/` on kiwi. The live process
(PID 12940, built from the main checkout before this fix) held 65,473 typed
IOSurface shared-event references when rechecked. An independent Metal process
could not create a shared event. Closing the test Wheelhouse window restored
shared-event creation without restarting Porthole or rebooting.

A heap capture before closing that process reported:

| Object | Instances |
| --- | ---: |
| AGXG16GFamilyCommandBuffer | 4,416,835 |
| AGXG16GFamilyBlitContext | 862,806 |
| MTLTextureDescriptorInternal | 862,862 |
| IOSurfaceSharedEvent | 65,473 |
| AGXG16GFamilyTexture | 56 |

## Controlled comparison

`tools/check-metal-lifetime.py PID --samples 3 --interval 5` uses `lsmp` to count
ports explicitly identified as `IOSurfaceSharedEventReference`.
It requires `lsmp` on PATH (`/usr/bin/lsmp` on the macOS 26.6 test host).
The processes were actively rendering during sampling; the first sample is not
a startup count.

| Workload | Unpatched samples | Pool fix samples |
| --- | --- | --- |
| Jackstay view consuming local ffplay | 3,878; 4,154; 4,350 | 2; 4; 0; 0 |
| Ordinary terminal printing at 60 Hz | 2,186; 2,485; 2,786 | 0; 0; 1; 0 |

The fixed Jackstay heap sample contained four command buffers, two blit contexts,
two texture descriptors and one shared event. The stream process subsequently
exited with SIGPIPE, matching a separate intermittent recovery-test failure;
this is not evidence that the transport failure is fixed. The terminal-only
comparison establishes that Jackstay is not required for the Metal leak.

## Fix and limits

An Objective-C autorelease pool now surrounds each `update()` on macOS. This
covers font work, event processing, rendering and updates invoked from native
resize callbacks. Explicitly retained renderer resources remain owned across
frames; the pool releases temporary factory results after each update.

The checker compares the sample range (`max - min`) with the allowed threshold;
it does not establish a monotonic leak trend. Bounded fluctuations can exceed
the threshold, and slow leaks can remain below it during a short run. It checks
neither correctness of rendered pixels nor evidence that a workload is actually
active. Run it against an advancing source. Short bounded
samples establish the immediate ownership fix; they are not an overnight soak.
No kernel event-table implementation or historical pre-September-12 failure was
investigated here.

The `/tmp` paths above identify local incident artifacts, not repository fixtures.
They may disappear; the counts and conditions recorded here are the retained
summary, not a substitute for independently rerunning the comparison.
