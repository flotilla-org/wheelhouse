# Jackstay presentation demand

Status: follow-up design; no protocol extension implemented.

Wheelhouse now limits Jackstay preview frame uptake independently of foreground
views. The existing mailbox keeps the latest pending frame, replacing and
releasing unsampled frames. This bounds GUI uploads/imports but does not reduce
producer rendering, transport acquisition, CPU mailbox copies or producer wakes.
It does not establish the cause of the reported slow keyboard input with a
pinned CEF card.

A pending final frame currently calls `rd_request_frame` until its 100–200 ms
preview deadline. This preserves convergence but does not lower the shell's UI
frame rate during that interval, especially with continuous producer wakes.
There is no shared timed-wake interface yet. Wheelhouse #406 tracks scheduling
preview deadlines without full-rate shell frame requests; uptake counts alone
do not establish input latency or idle-power improvement.

The next step spans Jackstay and its producers. A consumer should be able to
report its presentation role, displayed pixel demand and useful refresh rate.
These are advisory hints: hosts must retain their own admission policies when a
producer ignores them or cannot adapt.

A producer with one preview-only consumer could reduce rendering cadence and
output resolution. Multiple consumers require aggregation of their demands: a
preview consumer must not reduce the quality needed by a foreground consumer.
Demand must expire or be withdrawn when a consumer disconnects or disappears.

Logical viewport size and render resolution are separate. For a browser, reducing
the viewport changes page layout. A lower-resolution preview should be able to
retain the same logical viewport. Capture and video producers have different
constraints; the protocol should express demand without prescribing browser
behavior.

Before choosing message fields, establish:

- How hints relate to existing presentation size, scale, visibility and focus.
- How consumer demands are combined, withdrawn and recovered after reconnects.
- Which capabilities distinguish cadence control from resolution control.
- How to coalesce wakes and avoid CPU copies before GUI admission.
- How paused, hidden, preview and foreground transitions converge without
  retaining obsolete frame leases or missing the final publication.

Validation should include an animated pinned CEF workspace beside an active
terminal, with keyboard delivery/echo latency and main-thread active time measured
against an unpinned control. Repeat with a CPU producer and several consumers.
Check that a foreground consumer stays immediate, preview demand reduces actual
producer work, and GPU completion still governs reuse of sampled frames.
