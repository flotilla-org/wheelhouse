//! Queries over a layout's arrays: plain functions, so they serve the arrays
//! in Rust memory and the copies in C memory alike.
use crate::{Cluster, Line, View, CLUSTER_ELLIPSIS, CLUSTER_HARD_BREAK, CLUSTER_RTL};

/// Which side of an offset a caret belongs to where the two sides are drawn
/// apart: at a soft line break, or a direction change.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Affinity {
    /// With the text after the offset.
    #[default]
    Downstream,
    /// With the text before the offset.
    Upstream,
}

/// A caret position: a byte offset and its affinity.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Position {
    pub offset: usize,
    pub affinity: Affinity,
}

/// Where a caret is drawn.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Caret {
    pub x: f32,
    /// The line's top.
    pub y: f32,
    pub height: f32,
    pub line: usize,
    /// The text it sits in runs right to left.
    pub rtl: bool,
}

/// A rectangle of a selection, on one line.
#[derive(Clone, Copy, Debug, PartialEq)]
#[repr(C)]
pub struct Rect {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
    pub line: u32,
}

/// A stretch of one direction and one style on a line: what a run id names.
#[derive(Clone, Copy, Debug, PartialEq)]
#[repr(C)]
pub struct Segment {
    pub line: u32,
    /// Its index among the line's segments, left to right.
    pub segment: u32,
    pub cluster_start: u32,
    pub cluster_count: u32,
    /// The bytes it covers: lowest start to highest end.
    pub text_start: u32,
    pub text_end: u32,
    pub x: f32,
    pub width: f32,
    pub style: u32,
    pub rtl: u32,
}

/// A run's identity across layouts of one Block: the Block's key, the line
/// and segment, and the layout's generation.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct RunId {
    pub key: u64,
    pub line: u32,
    pub segment: u32,
    pub generation: u64,
}

impl Segment {
    pub fn id(&self, key: u64, generation: u64) -> RunId {
        RunId { key, line: self.line, segment: self.segment, generation }
    }
}

fn line_clusters<'a>(view: &View<'a>, line: &Line) -> &'a [Cluster] {
    let start = line.cluster_start as usize;
    &view.clusters[start..start + line.cluster_count as usize]
}

fn rtl(c: &Cluster) -> bool {
    c.flags & CLUSTER_RTL != 0
}

/// The position nearest a point: the line by y, then the cluster by x in
/// visual order, then the side of it the point falls.
pub fn offset_at(view: &View<'_>, x: f32, y: f32) -> Position {
    let Some(last) = view.lines.len().checked_sub(1) else { return Position::default() };
    let line = &view.lines[view.lines.partition_point(|l| l.y + l.height <= y).min(last)];
    let clusters = line_clusters(view, line);
    if clusters.is_empty() {
        return Position { offset: line.text_start as usize, affinity: Affinity::Downstream };
    }
    let i = clusters.partition_point(|c| c.x + c.advance < x).min(clusters.len() - 1);
    let (mut c, mut left) = (&clusters[i], x < clusters[i].x + clusters[i].advance / 2.0);
    // In a gap (an inline box's), the nearer edge.
    if x < c.x && i > 0 {
        let before = &clusters[i - 1];
        if x - (before.x + before.advance) <= c.x - x {
            (c, left) = (before, false);
        }
    }
    // The ellipsis ends the text visually: hit it as the edge of the text
    // beside it.
    if c.flags & CLUSTER_ELLIPSIS != 0 {
        let beside = if i > 0 { Some((&clusters[i - 1], false)) } else { clusters.get(i + 1).map(|c| (c, true)) };
        match beside {
            Some(beside) => (c, left) = beside,
            None => return Position { offset: c.start as usize, affinity: Affinity::Upstream },
        }
    }
    // Past a hard break is the next line: its far side is the text beside
    // it, when parley left any on its line, or else the break itself.
    if c.flags & CLUSTER_HARD_BREAK != 0 {
        let at = clusters.iter().position(|o| std::ptr::eq(o, c)).unwrap_or(i);
        let beside = match (left, rtl(c)) {
            (false, false) => clusters.get(at + 1).map(|o| (o, true)),
            (true, true) => at.checked_sub(1).map(|b| (&clusters[b], false)),
            _ => None,
        };
        match beside {
            Some(beside) if beside.0.flags & CLUSTER_HARD_BREAK == 0 => (c, left) = beside,
            _ => return Position { offset: c.start as usize, affinity: Affinity::Downstream },
        }
    }
    let (start, end) = (c.start as usize, (c.start + c.len) as usize);
    let before = Position { offset: start, affinity: Affinity::Downstream };
    let after = Position { offset: end, affinity: Affinity::Upstream };
    if left != rtl(c) {
        before
    } else {
        after
    }
}

/// Where the caret for a position is drawn. An offset inside a cluster
/// counts as its start; a caret inside a ligature sits where its graphemes
/// divide the ligature's advance.
pub fn caret_rect(view: &View<'_>, offset: usize, affinity: Affinity) -> Caret {
    let Some(last) = view.lines.len().checked_sub(1) else {
        return Caret { x: 0.0, y: 0.0, height: 0.0, line: 0, rtl: false };
    };
    let offset = offset as u32;
    // The lines holding the offset: one, or more where lines of inline
    // boxes alone, or a line's end, meet it.
    let first = view.lines.partition_point(|l| l.text_end < offset).min(last);
    let end = first + view.lines[first..].partition_point(|l| l.text_start <= offset).max(1);
    let lines = first..end;
    let mut next = None;
    let mut prev = None;
    let mut inside = None;
    let mut hard_break = None;
    for index in lines.clone() {
        for c in line_clusters(view, &view.lines[index]) {
            if c.flags & CLUSTER_ELLIPSIS != 0 {
                continue;
            }
            if c.start == offset && next.is_none() {
                next = Some((index, c));
            }
            // After a hard break is the next line's start.
            if c.start + c.len == offset && c.len > 0 {
                if c.flags & CLUSTER_HARD_BREAK == 0 {
                    prev = Some((index, c));
                } else {
                    hard_break = Some((index, c));
                }
            }
            if c.start < offset && offset < c.start + c.len {
                inside = Some((index, c));
            }
        }
    }
    let leading = |(line, c): (usize, &Cluster)| (line, if rtl(c) { c.x + c.advance } else { c.x }, rtl(c));
    let trailing = |(line, c): (usize, &Cluster)| (line, if rtl(c) { c.x } else { c.x + c.advance }, rtl(c));
    let found = match affinity {
        _ if inside.is_some() => inside.map(leading),
        Affinity::Downstream => next.map(leading).or(prev.map(trailing)),
        Affinity::Upstream => prev.map(trailing).or(next.map(leading)),
    }
    // With no line after a hard break (the text was cut there), before it.
    .or_else(|| if lines.len() == 1 { hard_break.map(leading) } else { None });
    let (line, x, rtl) = found.unwrap_or((lines.end - 1, 0.0, false));
    let l = &view.lines[line];
    Caret { x, y: l.y, height: l.height, line, rtl }
}

/// The selection of bytes `start..end`, as rectangles per line. Visually
/// adjacent clusters merge, so bidi text can give several on one line.
pub fn selection_rects(view: &View<'_>, start: usize, end: usize) -> Vec<Rect> {
    let mut rects = vec![];
    if start >= end {
        return rects;
    }
    let (start, end) = (start as u32, end as u32);
    let first = view.lines.partition_point(|l| l.text_end <= start);
    for (index, line) in view.lines.iter().enumerate().skip(first) {
        if line.text_start >= end {
            break;
        }
        let mut open: Option<Rect> = None;
        for c in line_clusters(view, line) {
            let selected = c.len > 0 && c.start < end && start < c.start + c.len;
            match (&mut open, selected) {
                (Some(r), true) if (r.x + r.width - c.x).abs() < 0.01 => r.width = r.width.max(c.x + c.advance - r.x),
                (_, true) => {
                    rects.extend(open.take());
                    open = Some(Rect { x: c.x, y: line.y, width: c.advance, height: line.height, line: index as u32 });
                }
                (_, false) => rects.extend(open.take()),
            }
        }
        rects.extend(open);
    }
    rects
}

/// Each line's segments: visually consecutive clusters of one direction and
/// one style.
pub fn segments(view: &View<'_>) -> Vec<Segment> {
    let mut out: Vec<Segment> = vec![];
    for (index, line) in view.lines.iter().enumerate() {
        let mut count = 0;
        for (i, c) in line_clusters(view, line).iter().enumerate() {
            let cluster = line.cluster_start + i as u32;
            match out.last_mut() {
                Some(s) if count > 0 && s.style == c.style && s.rtl == rtl(c) as u32 => {
                    s.cluster_count += 1;
                    s.text_start = s.text_start.min(c.start);
                    s.text_end = s.text_end.max(c.start + c.len);
                    s.width = c.x + c.advance - s.x;
                }
                _ => {
                    out.push(Segment {
                        line: index as u32,
                        segment: count,
                        cluster_start: cluster,
                        cluster_count: 1,
                        text_start: c.start,
                        text_end: c.start + c.len,
                        x: c.x,
                        width: c.advance,
                        style: c.style,
                        rtl: rtl(c) as u32,
                    });
                    count += 1;
                }
            }
        }
    }
    out
}
