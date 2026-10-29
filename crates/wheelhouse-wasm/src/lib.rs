//! The sidebar core for the browser (docs/design/change-stream.md). A page
//! feeds it facts, subscribes, and polls from its event loop; each delta
//! comes as a plain object it applies to its own model. It changes logical
//! state by intent. The TypeScript types are generated from `types`.
use tsify::{Ts, Tsify};
use wasm_bindgen::prelude::*;
use wheelhouse_core::{stream::SubscriptionId, MetadataPatch, Sidebar, WorkspaceId};

pub mod types;
use types::{Delta, Intent, Receipt, View};

#[cfg(test)]
mod tests;

#[wasm_bindgen]
pub struct Core {
    sidebar: Sidebar,
}

fn error(message: impl ToString) -> JsError {
    JsError::new(&message.to_string())
}

/// Facts from a stream's JSON lines, one metadata patch a line.
pub fn patches(jsonl: &str) -> Result<Vec<MetadataPatch>, String> {
    jsonl.lines().filter(|line| !line.trim().is_empty()).map(|line| serde_json::from_str(line).map_err(|e| e.to_string())).collect()
}

#[wasm_bindgen]
impl Core {
    /// A sidebar configured with a template catalog (KDL).
    #[wasm_bindgen(constructor)]
    pub fn new(config_kdl: &str) -> Result<Core, JsError> {
        Ok(Self { sidebar: Sidebar::new(config_kdl).map_err(error)? })
    }

    /// Apply a batch of patches from `provider` (a subscription ID), as JSON
    /// lines. Time is monotonic milliseconds, supplied by the page.
    pub fn apply(&mut self, now_ms: f64, provider: &str, jsonl: &str) -> Result<(), JsError> {
        self.sidebar.apply(now_ms as u64, provider, patches(jsonl).map_err(error)?).map_err(error)
    }

    pub fn tick(&mut self, now_ms: f64) {
        self.sidebar.tick(now_ms as u64);
    }

    /// Mark a provider's facts stale (its connection dropped) or fresh.
    pub fn set_provider_stale(&mut self, provider: &str, stale: bool) {
        self.sidebar.set_provider_stale(provider, stale);
    }

    /// Follow the sidebar's changes; the first poll gives everything.
    pub fn subscribe(&mut self) -> u32 {
        self.sidebar.subscribe().0 as u32
    }

    pub fn unsubscribe(&mut self, subscription: u32) -> bool {
        self.sidebar.unsubscribe(SubscriptionId(subscription.into()))
    }

    /// Whether a poll may have something; it builds nothing. False means
    /// only that there is nothing to deliver: an unknown subscription (never
    /// given, or unsubscribed) throws.
    #[wasm_bindgen(js_name = changedSince)]
    pub fn changed_since(&self, subscription: u32) -> Result<bool, JsError> {
        self.sidebar.changed_since(SubscriptionId(subscription.into())).map_err(error)
    }

    /// What changed since this subscription's last delta, or nothing when
    /// there is nothing to deliver. An unknown subscription (never given, or
    /// unsubscribed) throws.
    pub fn poll(&mut self, subscription: u32) -> Result<Option<Ts<Delta>>, JsError> {
        let delta = self.sidebar.poll(SubscriptionId(subscription.into())).map_err(error)?;
        Ok(delta.as_ref().map(|delta| Delta::from(delta).into_ts()).transpose()?)
    }

    /// Apply an intent. Its effects are the page's to carry out.
    #[wasm_bindgen(js_name = applyIntent)]
    pub fn apply_intent(&mut self, intent: Ts<Intent>) -> Result<Ts<Receipt>, JsError> {
        let intent = intent.to_rust()?.read().map_err(error)?;
        Ok(Receipt::from(self.sidebar.apply_intent(intent)).into_ts()?)
    }

    /// Complete a FOCUS or MATERIALIZE effect's request: with the new
    /// workspace's ID for a materialization, none for a focus, or an error.
    pub fn complete(&mut self, request: f64, workspace_id: Option<String>, error_message: Option<String>) -> Result<bool, JsError> {
        let result = match error_message {
            Some(message) => Err(message),
            None => Ok(workspace_id.map(|id| id.parse::<WorkspaceId>()).transpose().map_err(error)?),
        };
        Ok(self.sidebar.complete(request as u64, result))
    }

    /// The whole model as a fresh snapshot gives it: what following the
    /// deltas must have built.
    pub fn view(&self) -> Result<Ts<View>, JsError> {
        Ok(View::from(&wheelhouse_core::stream::StreamView::of(self.sidebar.snapshot_shared())).into_ts()?)
    }
}
