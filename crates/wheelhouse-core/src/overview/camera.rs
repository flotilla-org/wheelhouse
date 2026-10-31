//! The camera on the overview scene, and its flights: the zoom-and-pan path
//! of van Wijk and Nuij, "Smooth and efficient zooming and panning" (2003),
//! as `d3.interpolateZoom` gives it.

/// A view of the scene: its centre and its width in scene units. Its height
/// is the width over the viewport's aspect.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Camera {
    pub cx: f64,
    pub cy: f64,
    pub w: f64,
}

/// The path from one camera to another: the camera pulls back just far
/// enough to show both ends, so near moves stay shallow and far ones go wide.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ZoomPath {
    from: Camera,
    to: Camera,
    rho: f64,
    shape: Shape,
    /// The path's length in the paper's units; a flight's duration follows it.
    pub length: f64,
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum Shape {
    /// Centres too close to pan: zoom only, with `s` the signed length.
    Zoom {
        s: f64,
    },
    Pan {
        d1: f64,
        r0: f64,
        s: f64,
    },
}

impl ZoomPath {
    /// `rho` sets how far the camera pulls back: the paper's and d3's
    /// default is the square root of 2.
    pub fn new(from: Camera, to: Camera, rho: f64) -> Self {
        let (dx, dy) = (to.cx - from.cx, to.cy - from.cy);
        let d2 = dx * dx + dy * dy;
        let (rho2, rho4) = (rho * rho, rho * rho * rho * rho);
        // The cutoff is absolute, in scene units, because the scene's units
        // are fixed: a tile is 1 high at any window size, so 1e-6 of a tile
        // is too close to pan at every zoom the overview reaches.
        let shape = if d2 < 1e-12 {
            Shape::Zoom { s: (to.w / from.w).ln() / rho }
        } else {
            let d1 = d2.sqrt();
            let b0 = (to.w * to.w - from.w * from.w + rho4 * d2) / (2.0 * from.w * rho2 * d1);
            let b1 = (to.w * to.w - from.w * from.w - rho4 * d2) / (2.0 * to.w * rho2 * d1);
            // ln(sqrt(b² + 1) - b) is -asinh(b), without the cancellation
            // that loses a far pan's precision.
            let (r0, r1) = (-b0.asinh(), -b1.asinh());
            Shape::Pan { d1, r0, s: (r1 - r0) / rho }
        };
        let length = match shape {
            Shape::Zoom { s } | Shape::Pan { s, .. } => s.abs(),
        };
        Self { from, to, rho, shape, length }
    }

    /// The camera at `t` along the path, from 0 to 1. The ends are exact.
    pub fn at(&self, t: f64) -> Camera {
        if t <= 0.0 {
            return self.from;
        }
        if t >= 1.0 {
            return self.to;
        }
        let (a, rho) = (self.from, self.rho);
        let (dx, dy) = (self.to.cx - a.cx, self.to.cy - a.cy);
        match self.shape {
            Shape::Zoom { s } => Camera { cx: a.cx + t * dx, cy: a.cy + t * dy, w: a.w * (rho * t * s).exp() },
            Shape::Pan { d1, r0, s } => {
                let st = t * s;
                let u = a.w / (rho * rho * d1) * (r0.cosh() * (rho * st + r0).tanh() - r0.sinh());
                Camera { cx: a.cx + u * dx, cy: a.cy + u * dy, w: a.w * r0.cosh() / (rho * st + r0).cosh() }
            }
        }
    }

    pub fn to(&self) -> Camera {
        self.to
    }
}

/// How flights are timed (docs/design/overview-scene.md, "Transitions").
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct FlightTiming {
    pub rho: f64,
    /// Milliseconds per unit of path length.
    pub ms_per_unit: f64,
    pub min_ms: f64,
    pub max_ms: f64,
    /// A flight that starts mid-flight takes this share of its duration.
    pub interrupted_share: f64,
    /// Nor less than this, however short the share makes it.
    pub interrupted_min_ms: f64,
}

impl Default for FlightTiming {
    fn default() -> Self {
        Self { rho: 1.4, ms_per_unit: 280.0, min_ms: 240.0, max_ms: 580.0, interrupted_share: 0.5, interrupted_min_ms: 90.0 }
    }
}

/// The ease the prototype settled on: fast away, slow to land.
pub fn ease_out(t: f64) -> f64 {
    let t = t.clamp(0.0, 1.0);
    1.0 - (1.0 - t).powi(3)
}

/// One camera flight along a path, from `start_ms` for `duration_ms`.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Flight {
    pub path: ZoomPath,
    pub start_ms: f64,
    pub duration_ms: f64,
}

impl Flight {
    /// A flight from `from` to `to` starting at `now_ms`, or none when the
    /// two are the same camera, `timing` gives it no time, or either camera
    /// can't be on a path (no width, or not finite): then the camera cuts.
    /// `interrupted` flights take a share of the time, so switching fast
    /// never chains full-length flights.
    pub fn new(from: Camera, to: Camera, now_ms: f64, timing: &FlightTiming, interrupted: bool) -> Option<Self> {
        let usable = |c: Camera| c.cx.is_finite() && c.cy.is_finite() && c.w.is_finite() && c.w > 0.0;
        if !usable(from) || !usable(to) {
            return None;
        }
        let path = ZoomPath::new(from, to, timing.rho);
        if !path.length.is_finite() || path.length < 1e-9 {
            return None;
        }
        let full = (path.length * timing.ms_per_unit).max(timing.min_ms).min(timing.max_ms);
        let duration_ms = if interrupted { (full * timing.interrupted_share).max(timing.interrupted_min_ms).min(full) } else { full };
        // A zero, negative or NaN duration would make `camera` divide by it.
        if duration_ms.is_nan() || duration_ms <= 0.0 {
            return None;
        }
        Some(Self { path, start_ms: now_ms, duration_ms })
    }

    pub fn camera(&self, now_ms: f64) -> Camera {
        self.path.at(ease_out((now_ms - self.start_ms) / self.duration_ms))
    }

    pub fn done(&self, now_ms: f64) -> bool {
        now_ms >= self.start_ms + self.duration_ms
    }
}

/// A camera and the flight it may be on. A new destination always flies
/// from where the camera is now; nothing queues.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct CameraRig {
    rest: Camera,
    flight: Option<Flight>,
}

impl CameraRig {
    pub fn new(camera: Camera) -> Self {
        Self { rest: camera, flight: None }
    }

    /// Where the camera is at `now_ms`.
    pub fn camera(&self, now_ms: f64) -> Camera {
        match self.flight {
            Some(flight) if !flight.done(now_ms) => flight.camera(now_ms),
            _ => self.rest,
        }
    }

    /// Whether a flight is still under way at `now_ms`: a host keeps asking
    /// for frames while it is.
    pub fn flying(&self, now_ms: f64) -> bool {
        self.flight.is_some_and(|flight| !flight.done(now_ms))
    }

    /// Fly to `to`, from where the camera is at `now_ms`.
    pub fn fly_to(&mut self, to: Camera, now_ms: f64, timing: &FlightTiming) {
        let from = self.camera(now_ms);
        self.flight = Flight::new(from, to, now_ms, timing, self.flying(now_ms));
        self.rest = to;
    }

    /// Put the camera at `to` at once: a cut, for reduced motion or a
    /// viewport that changed size.
    pub fn jump_to(&mut self, to: Camera) {
        self.flight = None;
        self.rest = to;
    }

    /// Where the camera is going, or is.
    pub fn destination(&self) -> Camera {
        self.rest
    }
}
