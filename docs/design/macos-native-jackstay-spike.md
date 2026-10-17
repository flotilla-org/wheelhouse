# macOS native Jackstay import spike

The optional Luchs CEF experiment publishes GPU-owned frames through Jackstay's
existing native acquisition API. This companion path imports IOSurfaces directly
as Metal textures. The normal CPU and Windows paths remain available.

The explicit experimental View setting `macos_service` names a producer's Mach
service. Its ordinary `source_endpoint` supplies input and page affordances. A
scripted presentation record may set one-shot `connect: 1` for observation; taking
control still requires an explicit request. The producer must run in a launchd
job declaring its Mach service. These settings are intended for a private test
profile, not a default browser handler configuration.

A producer advertising `navigation.load` gets the same editable address field as
a web View, including click focus and Return navigation. This works for generic
Jackstay Views independently of their engine or frame transport.

The worker attaches the native consumer, registers the renderer's shared
completion event, and hands acquired frame leases to the existing GUI mailbox.
Only completed producer copies enter that mailbox: a bounded, cancellable worker
poll prevents a dead producer from leaving an unbounded wait on the GUI's GPU
queue. The GUI imports and caches IOSurface textures, GPU-waits for readiness,
and releases replaced leases against queue-ordered completion values. Texture
and timeline resources use normal renderer retirement. A new configuration is
installed without reconnecting or silently taking control.

The Metal pixel diagnostic covers RGBA/BGRA IOSurface import, surface lifetime,
and shared readiness/completion events using a second Metal device. Native
producer/consumer acceptance still needs the external Mach service; a CEF SDK is
not required to build Wheelhouse.

This is a spike. The worker's 1 ms readiness polling could become a completion
notification. There is no automatic content-handler negotiation for this native
path yet. The companion CEF renderer currently targets macOS arm64, with browser
parity and the other platform adapters left for subsequent work.
