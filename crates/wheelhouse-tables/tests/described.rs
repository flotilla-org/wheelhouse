//! A described table as a host drives it, against binnacle's fake table
//! server in-process: its link parses back to what made it; rows arrive
//! under their described headers and wake the host, once until it looks;
//! live changes apply in place under the sort; an unknown table says so;
//! and a stream that ends is read again.
use binnacle_core::{Axis, Key, SortColumn, SortSpec, ViewSpec, Viewport};
use binnacle_described::Wire;
use binnacle_described_http::Endpoint;
use binnacle_testkit::fake_tables::{FakeTable, FakeTables, Running, Step};
use std::path::PathBuf;
use std::sync::mpsc;
use std::time::{Duration, Instant};
use wheelhouse_tables::{Link, Metrics, Table};

const METRICS: Metrics = Metrics { line: 20.0, digit: 8.0 };
const WAIT: Duration = Duration::from_secs(10);

/// Ten generated rows keyed by `id`, in the `flotilla` namespace.
fn fake() -> Running {
    FakeTables::new().table(FakeTable::generated("crews", 10)).start().expect("the fake starts")
}

fn link(fake: &Running, table: &str) -> Link {
    let url = format!("{}/tables/{table}?namespace=flotilla", fake.url().expect("a TCP fake"));
    Link::parse(&url).expect("the link parses")
}

/// A table open on `link`, and how often it woke its host.
struct Host {
    table: Table,
    grid: binnacle_core::Grid,
    woken: mpsc::Receiver<()>,
}

impl Host {
    fn open(link: &Link) -> Host {
        let (wake, woken) = mpsc::channel();
        let wake = std::sync::Mutex::new(wake);
        let (table, grid) = Table::open_described(
            link,
            METRICS,
            0,
            Box::new(move || {
                let _ = wake.lock().unwrap().send(());
            }),
        )
        .expect("the table opens");
        Host { table, grid, woken }
    }

    /// One frame, as the native View draws it.
    fn frame(&mut self) {
        self.table.tick(&mut self.grid, Duration::ZERO);
        self.table.pump(&mut self.grid, |_| {});
        self.table.fill(&mut self.grid, Viewport::new(2000.0, 2000.0));
    }

    /// Draws a frame each time the table wakes its host, until `done`.
    fn until(&mut self, what: &str, done: impl Fn(&mut Host) -> bool) {
        let deadline = Instant::now() + WAIT;
        self.frame();
        while !done(self) {
            let left = deadline.saturating_duration_since(Instant::now());
            if self.woken.recv_timeout(left).is_err() {
                panic!("no wake in time for {what}; status {:?}, shown {:?}", self.table.status(), self.ids());
            }
            self.frame();
        }
    }

    fn headers(&mut self) -> Vec<String> {
        let snapshot = self.grid.snapshot();
        let keys: Vec<Key> = snapshot.strip(Axis::Cols).iter().filter_map(|c| c.key).collect();
        keys.into_iter().map(|k| self.table.header(Axis::Cols, k, 0)).collect()
    }

    fn column(&mut self, header: &str) -> Key {
        let snapshot = self.grid.snapshot();
        let keys: Vec<Key> = snapshot.strip(Axis::Cols).iter().filter_map(|c| c.key).collect();
        keys.into_iter().find(|&k| self.table.header(Axis::Cols, k, 0) == header).expect(header)
    }

    /// The ids of the rows shown, in order; empty before the columns come.
    fn ids(&mut self) -> Vec<String> {
        let snapshot = self.grid.snapshot();
        let cols: Vec<Key> = snapshot.strip(Axis::Cols).iter().filter_map(|c| c.key).collect();
        let Some(&id) = cols.iter().find(|&&k| self.table.header(Axis::Cols, k, 0) == "ID") else {
            return Vec::new();
        };
        let rows: Vec<Key> = snapshot.strip(Axis::Rows).iter().filter_map(|r| r.key).collect();
        rows.into_iter().map(|r| self.table.cell(r, id).text).collect()
    }
}

fn row(id: i64, name: &str) -> binnacle_described::Row {
    [("id".to_owned(), Wire::Int(id.into())), ("name".to_owned(), Wire::Text(name.into()))].into()
}

#[test]
fn rows_arrive_under_their_described_headers_and_wake_the_host() {
    let fake = fake();
    let mut host = Host::open(&link(&fake, "crews"));
    assert_eq!(host.table.status().as_deref(), Some("Connecting…"));
    host.until("the rows", |h| h.ids().len() == 10);
    assert_eq!(host.headers(), ["ID", "Name", "Phase", "Started", "Load", "Spec"]);
    assert_eq!(host.ids(), ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9"]);
    assert_eq!(host.table.status(), None, "synced, so nothing to say");
    // The source said what the columns support, so the Grid offers find
    // and the quick filter.
    let snapshot = host.grid.snapshot();
    assert!(snapshot.find_support().is_some() && snapshot.quick_filter(Axis::Rows).is_some());
    // The rows draw as binnacle formats them: a null is ∅, a number sits
    // at the end of its cell.
    let (name, id) = (host.column("Name"), host.column("ID"));
    let rows: Vec<Key> = host.grid.snapshot().strip(Axis::Rows).iter().filter_map(|r| r.key).collect();
    assert_eq!(host.table.cell(rows[1], name).text, "row-00001");
    assert_eq!(host.table.cell(rows[7], name).text, "∅");
    assert_eq!(host.table.cell(rows[1], id).align, wheelhouse_tables::Align::Right);
}

#[test]
fn a_host_that_has_not_looked_is_woken_once() {
    let fake = fake();
    let mut host = Host::open(&link(&fake, "crews"));
    host.frame();
    host.woken.recv_timeout(WAIT).expect("a wake for the first message");
    // The rest of the snapshot comes while the host does not look: it is
    // not woken again for each.
    std::thread::sleep(Duration::from_millis(300));
    assert!(host.woken.try_recv().is_err(), "woken again before looking");
    host.until("the rows", |h| h.ids().len() == 10);
}

#[test]
fn live_changes_apply_in_place_under_the_sort() {
    let fake = fake();
    let mut host = Host::open(&link(&fake, "crews"));
    host.until("the rows", |h| h.ids().len() == 10);
    let id = host.column("ID");
    host.grid.request_view(ViewSpec::default().with_sort(Axis::Rows, SortSpec::new(vec![SortColumn::descending(id)])));
    host.frame();
    host.frame();
    assert_eq!(host.ids(), ["9", "8", "7", "6", "5", "4", "3", "2", "1", "0"]);
    fake.push(Step::Upsert { table: "crews".into(), row: row(42, "late") });
    fake.push(Step::Delete { table: "crews".into(), key: vec![Wire::Int(5.into())] });
    host.until("the upsert and the delete", |h| h.ids().first().map(String::as_str) == Some("42") && h.ids().len() == 10);
    assert_eq!(host.ids(), ["42", "9", "8", "7", "6", "4", "3", "2", "1", "0"]);
}

#[test]
fn an_unknown_table_says_so() {
    let fake = fake();
    let mut host = Host::open(&link(&fake, "nope"));
    host.until("the failure", |h| h.table.status().is_some_and(|s| s.starts_with("Couldn't")));
    let status = host.table.status().unwrap();
    assert!(status.contains("HTTP 404"), "{status}");
    assert!(host.ids().is_empty());
}

#[test]
fn a_stream_that_ends_is_read_again() {
    let fake = fake();
    let mut host = Host::open(&link(&fake, "crews"));
    host.until("the rows", |h| h.ids().len() == 10);
    // The stream ends, and a row goes while it is down: the read again
    // drops it.
    fake.push(Step::End);
    fake.push(Step::Delete { table: "crews".into(), key: vec![Wire::Int(3.into())] });
    host.until("the resync", |h| h.ids().len() == 9 && h.table.status().is_none());
    assert_eq!(host.ids(), ["0", "1", "2", "4", "5", "6", "7", "8", "9"]);
    assert_eq!(fake.requests().len(), 2, "one watch, then one more");
}

/// A link written from its parts, each percent-encoded as a server would.
fn written(endpoint: &Endpoint, table: &str, namespace: Option<&str>, wheres: &[String]) -> String {
    fn encode(text: &str) -> String {
        text.bytes()
            .map(|b| match b {
                b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'.' | b'_' | b'~' => (b as char).to_string(),
                _ => format!("%{b:02X}"),
            })
            .collect()
    }
    let base = match endpoint {
        Endpoint::Tcp(base) => base.clone(),
        Endpoint::Unix(path) => format!("http+unix://{}", encode(&path.to_string_lossy())),
    };
    let mut query: Vec<String> = namespace.map(|n| format!("namespace={}", encode(n))).into_iter().collect();
    query.extend(wheres.iter().map(|w| format!("where={}", encode(w))));
    let query = if query.is_empty() { String::new() } else { format!("?{}", query.join("&")) };
    format!("{base}/tables/{}{query}", encode(table))
}

#[hegel::test(test_cases = 200)]
fn a_link_parses_back_to_what_wrote_it(tc: hegel::TestCase) {
    use hegel::generators::{self as gs, Generator};
    let text = || gs::text().min_size(1).max_size(12);
    let endpoint = if tc.draw(gs::booleans()) {
        let port = tc.draw(gs::integers::<u16>());
        let prefix = tc.draw(gs::sampled_from(vec!["", "/api", "/v1/fleet"]));
        Endpoint::Tcp(format!("http://127.0.0.1:{port}{prefix}"))
    } else {
        Endpoint::Unix(PathBuf::from(format!("/tmp/{}", tc.draw(text()))))
    };
    let table = tc.draw(text().filter(|t: &String| !t.contains('/')));
    let namespace = tc.draw(gs::optional(text()));
    let wheres = tc.draw(gs::vecs(text()).max_size(3));
    let link = Link::parse(&written(&endpoint, &table, namespace.as_deref(), &wheres)).expect("parses");
    assert_eq!(link.endpoint, endpoint);
    assert_eq!(link.table, table);
    assert_eq!(link.namespace, namespace.unwrap_or_else(|| "default".into()));
    assert_eq!(link.wheres, wheres);
}

#[test]
fn what_is_no_table_link_parses_to_nothing() {
    for text in [
        "records",
        "https://example.com/tables/crews",
        "http:///tables/crews",
        "http://host/crews",
        "http://host/tables/",
        "http://host/tables/a/b",
        "http+unix:///tables/crews",
        "http://host/tables/%zz",
        "http://host/tables/crews%2",
        "http://host/tables/%ff",
        "http://host/tables/crews?where=%e2%82",
    ] {
        assert_eq!(Link::parse(text), None, "{text}");
    }
}

#[test]
fn a_plus_in_a_query_is_a_space_and_an_escaped_one_is_a_plus() {
    let link = Link::parse("http://host/tables/crews?where=a+%3E+1&where=b%2Bc").expect("parses");
    assert_eq!(link.wheres, ["a > 1", "b+c"]);
}
