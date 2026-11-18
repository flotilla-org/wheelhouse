use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use conversation_events::FileMatch;
use reqwest::Url;
use tokio::time::sleep;

use crate::{decoded, on_endpoint, Endpoint, Wake};

/// How long a query waits for the next keystroke before it is asked.
pub const SEARCH_DEBOUNCE: Duration = Duration::from_millis(150);

/// The answer to a [`Search::query`].
#[derive(Clone, Debug, PartialEq)]
pub struct SearchAnswer {
    pub query: String,
    pub matches: Result<Vec<FileMatch>, String>,
}

/// A root's search endpoint (wheelhouse#694 C8): the latest query wins, and
/// an answer to any earlier one is dropped.
pub struct Search {
    endpoint: Endpoint,
    url: Url,
    shared: Arc<Shared>,
}

struct Shared {
    /// The latest query's number.
    asked: AtomicU64,
    answer: Mutex<Option<SearchAnswer>>,
    wake: Wake,
}

impl Endpoint {
    /// Searches the root whose `search` is `search`, a path on this
    /// endpoint. `wake` is called from another thread when an answer waits.
    pub fn search(&self, search: &str, wake: Wake) -> Result<Search, String> {
        let url = on_endpoint(search)?;
        let shared = Arc::new(Shared { asked: AtomicU64::new(0), answer: Mutex::new(None), wake });
        Ok(Search { endpoint: self.clone(), url, shared })
    }
}

impl Search {
    /// Asks for up to `limit` files matching `query`, once it has stood for
    /// [`SEARCH_DEBOUNCE`] with no later query.
    pub fn query(&self, query: &str, limit: u32) {
        // Under the answer's lock, so no earlier query's answer lands after.
        let mut answer = self.shared.answer.lock().unwrap();
        let asked = self.shared.asked.fetch_add(1, Ordering::AcqRel) + 1;
        *answer = None;
        drop(answer);
        let mut url = self.url.clone();
        url.query_pairs_mut().append_pair("q", query).append_pair("limit", &limit.to_string());
        let (link, shared, query) = (self.endpoint.0.link.clone(), self.shared.clone(), query.to_owned());
        self.endpoint.0.runtime.spawn(async move {
            let latest = || shared.asked.load(Ordering::Acquire) == asked;
            sleep(SEARCH_DEBOUNCE).await;
            if !latest() {
                return;
            }
            let matches = decoded::<Vec<FileMatch>>(&link, url).await;
            let mut answer = shared.answer.lock().unwrap();
            if latest() {
                *answer = Some(SearchAnswer { query, matches });
                drop(answer);
                (shared.wake)();
            }
        });
    }

    /// The latest query's answer, once, if it has come.
    pub fn take(&self) -> Option<SearchAnswer> {
        self.shared.answer.lock().unwrap().take()
    }
}
