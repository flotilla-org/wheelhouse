//! One axis that can be grabbed and dragged, glides on when let go of, and
//! stretches past its ends and springs back (wheelhouse#527). It knows no
//! units: places, ranges, lengths and speeds are the caller's, times are
//! milliseconds. The place itself always keeps within its range; only the
//! stretch, drawn on top of it, goes past an end.
//!
//! Everything moving is solved exactly for any frame time, so how often the
//! host draws changes nothing but how smooth it looks.

/// What's let go of mid-drag glides on, its speed falling as e^(-t/τ): to a
/// stop over about a third of a second, or at its end.
pub const GLIDE_TAU_MS: f64 = 325.0;
/// A drag held still this long before its release doesn't glide. It is
/// also how long a gap between moves starts the drag's speed afresh rather
/// than smoothing it into the old one: both mean the hand stopped.
pub const HOLD_MS: f64 = 60.0;
/// A stretch springs back critically damped, with this time constant: it
/// settles in about seven of them, half a second.
pub const SPRING_MS: f64 = 70.0;
/// How hard a stretch resists: a drag `p` past an end draws
/// `(1 - 1/(p·STRETCH/length + 1))·length`, never as far as the whole length.
const STRETCH: f64 = 0.55;

/// The ends an axis keeps to, `lo <= hi`.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Range {
    pub lo: f64,
    pub hi: f64,
}

impl Range {
    pub fn new(a: f64, b: f64) -> Self {
        Self { lo: a.min(b), hi: a.max(b) }
    }

    pub fn clamp(&self, place: f64) -> f64 {
        place.clamp(self.lo, self.hi)
    }
}

/// Where the axis is drawn from and how fine a step matters: the region's
/// length along the axis, which a stretch never reaches, and the length of
/// one drawn point, below which movement stops.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Extent {
    pub length: f64,
    pub point: f64,
}

/// How far a drag `past` an end draws, for a region `length` long.
pub fn stretch(past: f64, length: f64) -> f64 {
    let d = length.max(f64::MIN_POSITIVE);
    // (1 - 1/(x + 1))·d, as x/(x + 1)·d: no cancellation for a small drag.
    let x = past.abs() * STRETCH / d;
    (x / (x + 1.0) * d).copysign(past)
}

/// The drag past an end that draws as `drawn`: `stretch`'s inverse.
pub fn unstretch(drawn: f64, length: f64) -> f64 {
    let d = length.max(f64::MIN_POSITIVE);
    // A stretch never reaches the whole length; keep the inverse finite.
    let share = (drawn.abs() / d).min(1.0 - 1e-9);
    (share / (1.0 - share) * d / STRETCH).copysign(drawn)
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum Phase {
    /// Still, at its place.
    Rest,
    /// Held: `from` is where the drag started, past any end; `at` where it
    /// last was, and `moved_ms` when it got there.
    Held { from: f64, at: f64, moved_ms: f64 },
    /// Gliding on at `v`, as of `ms`.
    Gliding { ms: f64 },
    /// Springing back from the stretch, as of `ms`.
    Springing { ms: f64 },
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Scroller {
    place: f64,
    /// The stretch past an end, drawn on top of `place`.
    over: f64,
    /// The speed: of the drag while held, of the glide, or while it
    /// springs of the drag past the end the stretch draws.
    v: f64,
    phase: Phase,
}

impl Scroller {
    /// At rest at `place`.
    pub fn at(place: f64) -> Self {
        Self { place, over: 0.0, v: 0.0, phase: Phase::Rest }
    }

    /// Where the axis is, within its range.
    pub fn place(&self) -> f64 {
        self.place
    }

    /// The stretch past an end, drawn on top of the place.
    pub fn over(&self) -> f64 {
        self.over
    }

    /// Where it is drawn.
    pub fn drawn(&self) -> f64 {
        self.place + self.over
    }

    pub fn held(&self) -> bool {
        matches!(self.phase, Phase::Held { .. })
    }

    /// Gliding or springing back: the host draws again next frame.
    pub fn moving(&self) -> bool {
        matches!(self.phase, Phase::Gliding { .. } | Phase::Springing { .. })
    }

    /// Take hold of it where it is drawn, catching a glide or a spring there.
    /// True when it was moving.
    pub fn grab(&mut self, extent: Extent, now_ms: f64) -> bool {
        let caught = self.moving();
        let from = self.place + unstretch(self.over, extent.length);
        self.v = 0.0;
        self.phase = Phase::Held { from, at: from, moved_ms: now_ms };
        caught
    }

    /// While held: move to `by` from where the grab started, keeping the
    /// place within `range` and stretching past it.
    pub fn drag(&mut self, by: f64, range: Range, extent: Extent, now_ms: f64) {
        let Phase::Held { from, at, moved_ms } = self.phase else { return };
        let to = from + by;
        let dt = now_ms - moved_ms;
        let (mut at, mut moved_ms) = (at, moved_ms);
        if to != at && dt > 0.0 {
            let v = (to - at) / dt;
            // Smoothed over the last few moves, unless the hand stopped.
            self.v = if dt > HOLD_MS { v } else { self.v + (v - self.v) * 0.6 };
            at = to;
            moved_ms = now_ms;
        }
        self.place = range.clamp(to);
        self.over = stretch(to - self.place, extent.length);
        self.phase = Phase::Held { from, at, moved_ms };
    }

    /// Let go: a stretch springs back; otherwise it glides on at the drag's
    /// speed, unless the hand stopped first. Without motion it stays where
    /// it is, within its range.
    pub fn release(&mut self, motion: bool, now_ms: f64) {
        let Phase::Held { moved_ms, .. } = self.phase else { return };
        if !motion {
            self.stop();
        } else if self.over != 0.0 {
            self.v = 0.0;
            self.phase = Phase::Springing { ms: now_ms };
        } else if now_ms - moved_ms > HOLD_MS || self.v == 0.0 {
            self.stop();
        } else {
            self.phase = Phase::Gliding { ms: now_ms };
        }
    }

    /// Stop a glide where it is (a glide never stretches); a spring carries
    /// on.
    pub fn stop_glide(&mut self) {
        if let Phase::Gliding { .. } = self.phase {
            self.stop();
        }
    }

    /// At rest where it is, within its range.
    pub fn stop(&mut self) {
        self.over = 0.0;
        self.v = 0.0;
        self.phase = Phase::Rest;
    }

    /// Move on to `now_ms`: a glide slows, meeting an end carries it past
    /// into a spring, and a spring settles. `range` may have moved under it.
    pub fn advance(&mut self, range: Range, extent: Extent, now_ms: f64) {
        if !self.held() {
            self.place = range.clamp(self.place);
        }
        match self.phase {
            Phase::Gliding { ms } => {
                let (place, v) = (self.place, self.v);
                // Exact for a speed falling as v·e^(-t/τ): it would go `reach`
                // in all, and stops once slower than a point in a tenth of a
                // second.
                let reach = v * GLIDE_TAU_MS;
                let fast = v.abs() * 100.0 / extent.point;
                let stop_ms = if fast > 1.0 { GLIDE_TAU_MS * fast.ln() } else { 0.0 };
                let dt = (now_ms - ms).max(0.0).min(stop_ms);
                let to = place + reach * (1.0 - (-dt / GLIDE_TAU_MS).exp());
                let end = range.clamp(to);
                if end != to {
                    // It meets the end, and its speed then carries it past
                    // into a spring.
                    let t = -GLIDE_TAU_MS * (1.0 - (end - place) / reach).max(f64::MIN_POSITIVE).ln();
                    self.place = end;
                    self.over = 0.0;
                    self.v = v * (-t / GLIDE_TAU_MS).exp();
                    self.phase = Phase::Springing { ms: ms + t };
                    self.advance(range, extent, now_ms);
                } else if dt >= stop_ms {
                    self.place = to;
                    self.stop();
                } else {
                    self.place = to;
                    self.v = v * (-dt / GLIDE_TAU_MS).exp();
                    self.phase = Phase::Gliding { ms: now_ms };
                }
            }
            Phase::Springing { ms } => {
                let t = (now_ms - ms).max(0.0);
                let w = 1.0 / SPRING_MS;
                // Solved as a drag past the end, drawn through the stretch:
                // however fast a glide carries it past, it draws less than
                // the region's length.
                let (x0, v0) = (unstretch(self.over, extent.length), self.v);
                let e = (-w * t).exp();
                self.over = stretch((x0 + (v0 + w * x0) * t) * e, extent.length);
                self.v = (v0 - w * (v0 + w * x0) * t) * e;
                self.phase = Phase::Springing { ms: now_ms };
                if self.over.abs() < 0.25 * extent.point && self.v.abs() < 0.005 * extent.point {
                    self.stop();
                }
            }
            Phase::Rest | Phase::Held { .. } => {}
        }
    }
}
