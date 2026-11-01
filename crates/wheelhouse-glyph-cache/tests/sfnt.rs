//! CoreGraphics won't open a font without outlines, so for a CBDT-only font
//! native reads its tables from the bytes (font_provider.c). Whatever a font
//! file or a collection holds, each face's tables come back as written, a
//! table the face lacks or a face the collection lacks comes back as none,
//! and bytes that aren't a font never read outside themselves.
#![cfg(glyph_cache)]
use hegel::generators as gs;
use hegel::TestCase;
use wheelhouse_glyph_cache::sfnt_table;

type Face = Vec<([u8; 4], Vec<u8>)>;

fn draw_face(tc: &TestCase) -> Face {
    let count = tc.draw(gs::integers::<usize>().min_value(0).max_value(6));
    let mut face: Face = vec![];
    while face.len() < count {
        // Tags from a small alphabet, so a lookup sometimes misses.
        let tag = tc.draw(gs::vecs(gs::sampled_from(vec![b'A', b'B', b'C'])).min_size(4).max_size(4));
        let tag: [u8; 4] = tag.try_into().unwrap();
        if face.iter().all(|(t, _)| *t != tag) {
            face.push((tag, tc.draw(gs::vecs(gs::integers::<u8>()).max_size(40))));
        }
    }
    face
}

/// The faces' table directories at `dirs`, their tables after all of them.
fn write_faces(font: &mut Vec<u8>, faces: &[Face], dirs: &[usize]) {
    for (face, &dir) in faces.iter().zip(dirs) {
        let tables = font.len();
        font[dir + 4..dir + 6].copy_from_slice(&(face.len() as u16).to_be_bytes());
        let mut at = tables;
        for (i, (tag, data)) in face.iter().enumerate() {
            let rec = dir + 12 + i * 16;
            font[rec..rec + 4].copy_from_slice(tag);
            font[rec + 8..rec + 12].copy_from_slice(&(at as u32).to_be_bytes());
            font[rec + 12..rec + 16].copy_from_slice(&(data.len() as u32).to_be_bytes());
            at += data.len();
        }
        for (_, data) in face {
            font.extend(data);
        }
    }
}

fn directory(face: &Face) -> usize {
    12 + face.len() * 16
}

#[hegel::test(test_cases = 500)]
fn a_faces_tables_come_back_as_written(tc: TestCase) {
    let collection = tc.draw(gs::booleans());
    let faces = (0..if collection { tc.draw(gs::integers::<usize>().min_value(1).max_value(3)) } else { 1 })
        .map(|_| draw_face(&tc))
        .collect::<Vec<_>>();
    let mut font = vec![];
    let mut dirs = vec![];
    if collection {
        font.extend(b"ttcf\x00\x02\x00\x00");
        font.extend((faces.len() as u32).to_be_bytes());
        let mut at = 12 + 4 * faces.len();
        for face in &faces {
            font.extend((at as u32).to_be_bytes());
            dirs.push(at);
            at += directory(face);
        }
    } else {
        dirs.push(0);
    }
    font.resize(dirs.last().unwrap() + directory(faces.last().unwrap()), 0);
    write_faces(&mut font, &faces, &dirs);

    for (index, face) in faces.iter().enumerate() {
        for tag in [*b"AAAA", *b"ABCA", *b"CCCC", *b"BACB"].iter().chain(face.iter().map(|(t, _)| t)) {
            let want = face.iter().find(|(t, _)| t == tag).map(|(_, d)| d.as_slice()).filter(|d| !d.is_empty());
            assert_eq!(sfnt_table(&font, index as u32, tag), want, "face {index}, {:?}", std::str::from_utf8(tag));
        }
    }
    if collection {
        let (tag, _) = faces.iter().flatten().next().cloned().unwrap_or((*b"AAAA", vec![]));
        assert_eq!(sfnt_table(&font, faces.len() as u32, &tag), None, "a face past the collection's");
    }
}

#[hegel::test(test_cases = 500)]
fn bytes_that_arent_a_font_stay_in_bounds(tc: TestCase) {
    let mut font = tc.draw(gs::vecs(gs::integers::<u8>()).max_size(200));
    if tc.draw(gs::booleans()) && font.len() >= 4 {
        font[..4].copy_from_slice(b"ttcf");
    } else if font.len() >= 6 {
        // Some records, so the lookup reads them.
        let tables = tc.draw(gs::integers::<u16>().min_value(0).max_value(8));
        font[4..6].copy_from_slice(&tables.to_be_bytes());
    }
    let face = tc.draw(gs::integers::<u32>().min_value(0).max_value(4));
    // A tag a record in the bytes carries, when there is one, so the lookup
    // finds a record whose offset and size are garbage.
    let records = (0..8).map(|i| 12 + i * 16).filter(|r| r + 4 <= font.len()).collect::<Vec<_>>();
    let tag: [u8; 4] = if !records.is_empty() && tc.draw(gs::booleans()) {
        let r = tc.draw(gs::sampled_from(records));
        font[r..r + 4].try_into().unwrap()
    } else {
        tc.draw(gs::vecs(gs::integers::<u8>()).min_size(4).max_size(4)).try_into().unwrap()
    };
    // sfnt_table slices font with what native returns, so an offset or size
    // past the end panics.
    let _ = sfnt_table(&font, face, &tag);
}

#[test]
fn a_table_past_the_end_is_none() {
    let mut font = vec![0u8; 28];
    font[4..6].copy_from_slice(&1u16.to_be_bytes());
    font[12..16].copy_from_slice(b"CBDT");
    font[20..24].copy_from_slice(&20u32.to_be_bytes());
    font[24..28].copy_from_slice(&9u32.to_be_bytes());
    assert_eq!(sfnt_table(&font, 0, b"CBDT"), None);
    font[24..28].copy_from_slice(&8u32.to_be_bytes());
    assert_eq!(sfnt_table(&font, 0, b"CBDT"), Some(&font[20..28]));
}

#[test]
fn the_bundled_emoji_fonts_bitmap_tables() {
    let font = std::fs::read(concat!(env!("CARGO_MANIFEST_DIR"), "/../../data/NotoColorEmoji.ttf")).unwrap();
    let cblc = sfnt_table(&font, 0, b"CBLC").expect("CBLC");
    let cbdt = sfnt_table(&font, 0, b"CBDT").expect("CBDT");
    // CBLC and CBDT both start with their version, 3.0.
    assert_eq!((&cblc[..4], &cbdt[..4]), (&b"\x00\x03\x00\x00"[..], &b"\x00\x03\x00\x00"[..]));
    assert_eq!(sfnt_table(&font, 0, b"glyf"), None, "it has no outlines");
}
