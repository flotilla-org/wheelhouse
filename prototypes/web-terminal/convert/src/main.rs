//! PROTOTYPE - throwaway. `cast-to-screen IN.cast OUT.cbor`
//!
//! Feeds the recording through Cleat's Ghostty engine, takes a render
//! update every 1/60 s of recording time while output arrived (holding
//! frames during synchronized output, as Cleat's host does), and writes a
//! CBOR sequence of frames. Each frame is an integer-keyed map:
//!   0: time in ms
//!   1: [cols, rows] when the size changes
//!   2: cursor [col, row, visible, style, blink]
//!   3: styles first used in this frame, [id, fg, bg, flags, underline, link]
//!      with colours as 0xRRGGBB
//!   4: ops, [kind, rows] with kind 0 = full replace, 1 = row replace;
//!      each row is [row, wrap, texts, style_runs] where texts has one entry
//!      per column (null for a wide character's tail) and style_runs is a
//!      flat [style, count, style, count, ...]
//!   5: alternate screen
//! It also prints how big the same updates are in Cleat's postcard form.
use ciborium::value::Value;
use cleat::asciicast::{decode_event, decode_header, EventCode};
use cleat::provider::{DirtyState, TerminalCellWidth, TerminalRenderUpdateOpKind, TerminalRgb, TerminalStyleColorTag};
use cleat::vt::{ghostty::GhosttyVtEngine, VtEngine};
use std::collections::HashMap;
use std::io::{BufRead, Write};
use std::time::Duration;

fn rgb(c: &TerminalRgb) -> u64 {
    ((c.r as u64) << 16) | ((c.g as u64) << 8) | c.b as u64
}

fn int(v: u64) -> Value {
    Value::Integer(v.into())
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let input = std::io::BufReader::new(std::fs::File::open(&args[1]).expect("open cast"));
    let mut out = std::io::BufWriter::new(std::fs::File::create(&args[2]).expect("create output"));
    let mut lines = input.lines();
    let header = decode_header(&lines.next().unwrap().unwrap()).expect("header");
    let (cols, rows) = (header.cols, header.rows);
    let mut engine = GhosttyVtEngine::new(cols, rows);

    let frame = Duration::from_micros(16_667);
    let mut prev = Duration::ZERO;
    let mut next_frame = frame;
    let mut pending = false;
    let mut sync_since: Option<Duration> = None;
    let mut styles: HashMap<Vec<u8>, u64> = HashMap::new();
    let (mut frames, mut cbor_bytes, mut postcard_bytes, mut vt_bytes) = (0u64, 0u64, 0u64, 0u64);
    let mut first = true;
    let mut last_size = (0, 0);

    let mut emit = |engine: &mut GhosttyVtEngine, t: Duration, out: &mut dyn Write| {
        let update = engine.render_update(if first { DirtyState::Full } else { DirtyState::Partial }).expect("render");
        first = false;
        if update.ops.is_empty() && update.dirty == DirtyState::Clean {
            return;
        }
        postcard_bytes += postcard::to_allocvec(&update).unwrap().len() as u64;
        let mut new_styles = Vec::new();
        let mut ops = Vec::new();
        for op in &update.ops {
            let kind = match op.kind {
                TerminalRenderUpdateOpKind::FullVisibleReplace => 0,
                TerminalRenderUpdateOpKind::RowReplace => 1,
                TerminalRenderUpdateOpKind::ScrollCopy => 2,
            };
            let mut rows_out = Vec::new();
            for row in &op.rows {
                let mut texts = Vec::with_capacity(row.cells.len());
                let mut runs: Vec<Value> = Vec::new();
                let (mut run_style, mut run_len) = (u64::MAX, 0u64);
                for cell in &row.cells {
                    let s = &cell.style;
                    texts.push(match s.width {
                        TerminalCellWidth::SpacerTail => Value::Null,
                        _ => Value::Text(cell.graphemes.iter().filter_map(|&c| char::from_u32(c)).collect()),
                    });
                    let underline = if s.flags.bits() & (1 << 8) != 0 { s.underline_style.max(1) } else { 0 };
                    let ul_color = match s.underline_color.tag {
                        TerminalStyleColorTag::None => Value::Null,
                        _ => s.underline_color.rgb.as_ref().map(|c| int(rgb(c))).unwrap_or(Value::Null),
                    };
                    let mut key = Vec::new();
                    key.extend_from_slice(&rgb(&s.resolved_fg).to_le_bytes());
                    key.extend_from_slice(&rgb(&s.resolved_bg).to_le_bytes());
                    key.extend_from_slice(&s.flags.bits().to_le_bytes());
                    key.extend_from_slice(&underline.to_le_bytes());
                    key.extend_from_slice(format!("{ul_color:?}").as_bytes());
                    key.push(0);
                    key.extend_from_slice(&s.hyperlink_uri);
                    let next_id = styles.len() as u64;
                    let id = *styles.entry(key).or_insert_with(|| {
                        let link = if s.hyperlink_uri.is_empty() { Value::Null } else { Value::Text(String::from_utf8_lossy(&s.hyperlink_uri).into()) };
                        new_styles.push(Value::Array(vec![
                            int(next_id), int(rgb(&s.resolved_fg)), int(rgb(&s.resolved_bg)), int(s.flags.bits() as u64), int(underline as u64), ul_color.clone(), link,
                        ]));
                        next_id
                    });
                    if id == run_style {
                        run_len += 1;
                    } else {
                        if run_len > 0 {
                            runs.push(int(run_style));
                            runs.push(int(run_len));
                        }
                        run_style = id;
                        run_len = 1;
                    }
                }
                if run_len > 0 {
                    runs.push(int(run_style));
                    runs.push(int(run_len));
                }
                rows_out.push(Value::Array(vec![int(row.row as u64), Value::Bool(row.wrap), Value::Array(texts), Value::Array(runs)]));
            }
            ops.push(Value::Array(vec![int(kind), Value::Array(rows_out)]));
        }
        let c = &update.cursor;
        let mut map = vec![
            (int(0), int(t.as_millis() as u64)),
            (int(2), Value::Array(vec![int(c.col as u64), int(c.row as u64), Value::Bool(c.visible), int(c.style as u64), Value::Bool(c.blink)])),
            (int(4), Value::Array(ops)),
            (int(5), Value::Bool(update.terminal_modes.active_alternate_screen)),
        ];
        if (update.cols, update.rows) != last_size {
            last_size = (update.cols, update.rows);
            map.push((int(1), Value::Array(vec![int(update.cols as u64), int(update.rows as u64)])));
        }
        if !new_styles.is_empty() {
            map.push((int(3), Value::Array(new_styles)));
        }
        let mut buf = Vec::new();
        ciborium::into_writer(&Value::Map(map), &mut buf).unwrap();
        cbor_bytes += buf.len() as u64;
        frames += 1;
        out.write_all(&buf).unwrap();
    };

    for line in lines {
        let line = line.unwrap();
        if line.trim().is_empty() {
            continue;
        }
        let event = decode_event(&line, &mut prev).expect("event");
        // Present every frame boundary that passed before this event.
        while pending && event.time >= next_frame {
            let synced = engine.synchronized_output_active().unwrap_or(false);
            let held = synced && sync_since.map_or(true, |s| next_frame - s < Duration::from_secs(1));
            if synced && sync_since.is_none() {
                sync_since = Some(next_frame);
            }
            if !held {
                emit(&mut engine, next_frame, &mut out);
                pending = false;
                sync_since = None;
            }
            next_frame += frame;
        }
        if event.time >= next_frame {
            next_frame = event.time + frame;
        }
        if let EventCode::Output = event.code {
            vt_bytes += event.data.len() as u64;
            engine.feed(event.data.as_bytes()).expect("feed");
            pending = true;
        }
    }
    if pending {
        emit(&mut engine, next_frame, &mut out);
    }
    out.flush().unwrap();
    eprintln!("{frames} frames, {} styles; VT input {vt_bytes} B, CBOR {cbor_bytes} B, postcard {postcard_bytes} B", styles.len());
}
