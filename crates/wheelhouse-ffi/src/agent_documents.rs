//! A watched thread's state documents (#692, #777): what a View Dock's "+"
//! menu lists, and the typed checklist and tasks their Renderers read.
use super::*;
use conversation_events::{AgentTask, ChecklistItem, ChecklistStatus, TaskKind, TaskStatus};
use wheelhouse_rich_text::{RichText, Typed};

use crate::rich_text::WheelhouseRichText;

/// One state document a thread has: its key, and its media type (empty when
/// its producer named none).
#[repr(C)]
pub struct WheelhouseAgentDocument {
    pub key: Text,
    pub media_type: Text,
}

/// The state documents of the thread a conversation shows, by key. Every
/// string is borrowed from the list until wheelhouse_agent_documents_free.
#[repr(C)]
pub struct WheelhouseAgentDocuments {
    pub documents: *const WheelhouseAgentDocument,
    pub document_count: usize,
    /// As wheelhouse_rich_text_conversation_documents_revision when read:
    /// reads with the same revision saw the same documents.
    pub revision: u64,
}

type DocumentsKeep = (Vec<(String, String)>, Vec<WheelhouseAgentDocument>);

/// The thread's state documents, unknown keys included; none for markdown.
/// Free them with wheelhouse_agent_documents_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_conversation_documents(
    h: *mut WheelhouseRichText,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseAgentDocuments {
    run(h, error_out, |h| {
        let documents: Vec<(String, String)> =
            h.rich.conversation_documents().into_iter().map(|d| (d.key.clone(), d.media_type.clone().unwrap_or_default())).collect();
        let views: Vec<WheelhouseAgentDocument> = documents
            .iter()
            .map(|(key, media_type)| WheelhouseAgentDocument { key: Text::borrowed(key), media_type: Text::borrowed(media_type) })
            .collect();
        let value = WheelhouseAgentDocuments {
            documents: views.as_ptr(),
            document_count: views.len(),
            revision: h.rich.conversation_documents_revision(),
        };
        Ok(give::<_, DocumentsKeep>(value, (documents, views)))
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_documents_free(documents: *mut WheelhouseAgentDocuments) {
    take::<_, DocumentsKeep>(documents)
}

/// Moves when one of the conversation's state documents is added or
/// changes, and only then: read the documents again when it does.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_conversation_documents_revision(
    h: *mut WheelhouseRichText,
    error_out: *mut *mut c_char,
) -> u64 {
    run(h, error_out, |h| Ok(h.rich.conversation_documents_revision())).unwrap_or(0)
}

pub const WHEELHOUSE_CHECKLIST_PENDING: u32 = 0;
pub const WHEELHOUSE_CHECKLIST_IN_PROGRESS: u32 = 1;
pub const WHEELHOUSE_CHECKLIST_COMPLETED: u32 = 2;
/// A status this version doesn't know.
pub const WHEELHOUSE_CHECKLIST_UNKNOWN: u32 = 3;

/// A Checklist step. active_form is how it reads while in progress, empty
/// when the producer gave none.
#[repr(C)]
pub struct WheelhouseChecklistItem {
    pub id: Text,
    pub content: Text,
    /// WHEELHOUSE_CHECKLIST_*.
    pub status: u32,
    pub active_form: Text,
}

/// A thread's Checklist, in the agent's order; empty when it has none. Every
/// string is borrowed from it until wheelhouse_checklist_free.
#[repr(C)]
pub struct WheelhouseChecklist {
    pub items: *const WheelhouseChecklistItem,
    pub item_count: usize,
    /// As in WheelhouseAgentDocuments.
    pub revision: u64,
}

type ChecklistKeep = (Vec<ChecklistItem>, Vec<WheelhouseChecklistItem>);

fn typed<T: Clone>(rich: &RichText, pick: impl Fn(&Typed) -> Option<&Vec<T>>) -> Vec<T> {
    rich.conversation_documents().into_iter().find_map(|d| pick(d.typed()).cloned()).unwrap_or_default()
}

/// The `checklist` document's steps. Free it with wheelhouse_checklist_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_conversation_checklist(
    h: *mut WheelhouseRichText,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseChecklist {
    run(h, error_out, |h| {
        let items = typed(&h.rich, |t| match t {
            Typed::Checklist(items) => Some(items),
            _ => None,
        });
        let views: Vec<WheelhouseChecklistItem> = items
            .iter()
            .map(|item| WheelhouseChecklistItem {
                id: Text::borrowed(&item.id),
                content: Text::borrowed(&item.content),
                status: match item.status {
                    ChecklistStatus::Pending => WHEELHOUSE_CHECKLIST_PENDING,
                    ChecklistStatus::InProgress => WHEELHOUSE_CHECKLIST_IN_PROGRESS,
                    ChecklistStatus::Completed => WHEELHOUSE_CHECKLIST_COMPLETED,
                    ChecklistStatus::Unknown => WHEELHOUSE_CHECKLIST_UNKNOWN,
                },
                active_form: Text::borrowed(item.active_form.as_deref().unwrap_or_default()),
            })
            .collect();
        let value =
            WheelhouseChecklist { items: views.as_ptr(), item_count: views.len(), revision: h.rich.conversation_documents_revision() };
        Ok(give::<_, ChecklistKeep>(value, (items, views)))
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_checklist_free(checklist: *mut WheelhouseChecklist) {
    take::<_, ChecklistKeep>(checklist)
}

pub const WHEELHOUSE_TASK_KIND_OTHER: u32 = 0;
pub const WHEELHOUSE_TASK_KIND_SUBAGENT: u32 = 1;
pub const WHEELHOUSE_TASK_KIND_SHELL: u32 = 2;
pub const WHEELHOUSE_TASK_KIND_MONITOR: u32 = 3;

pub const WHEELHOUSE_TASK_RUNNING: u32 = 0;
pub const WHEELHOUSE_TASK_COMPLETED: u32 = 1;
pub const WHEELHOUSE_TASK_FAILED: u32 = 2;
pub const WHEELHOUSE_TASK_CANCELLED: u32 = 3;
/// A status this version doesn't know.
pub const WHEELHOUSE_TASK_UNKNOWN: u32 = 4;

/// An Agent Task. Times are milliseconds since the Unix epoch. Empty text is
/// a field the producer left out.
#[repr(C)]
pub struct WheelhouseAgentTask {
    pub id: Text,
    /// WHEELHOUSE_TASK_KIND_*.
    pub kind: u32,
    pub title: Text,
    /// WHEELHOUSE_TASK_*.
    pub status: u32,
    pub started_at: u64,
    pub has_ended_at: u32,
    pub ended_at: u64,
    /// The call that started it, in this thread.
    pub tool_call_id: Text,
    /// A subagent's own thread.
    pub child_thread_id: Text,
    /// The turn that started it waits for it.
    pub awaited: u32,
    /// Where its output is read (wheelhouse_agent_output_open): output_path
    /// is on this machine when output_local is 1, and output_url is on the
    /// producer's endpoint.
    pub has_output: u32,
    pub output_path: Text,
    pub output_local: u32,
    pub output_url: Text,
    pub output_media_type: Text,
}

/// A thread's Agent Tasks, finished ones included, in the producer's order.
/// Every string is borrowed from it until wheelhouse_agent_tasks_free.
#[repr(C)]
pub struct WheelhouseAgentTasks {
    pub tasks: *const WheelhouseAgentTask,
    pub task_count: usize,
    /// As in WheelhouseAgentDocuments.
    pub revision: u64,
}

type TasksKeep = (Vec<AgentTask>, Vec<WheelhouseAgentTask>);

/// The `tasks` document's entries. Free them with wheelhouse_agent_tasks_free.
#[no_mangle]
pub unsafe extern "C" fn wheelhouse_rich_text_conversation_tasks(
    h: *mut WheelhouseRichText,
    error_out: *mut *mut c_char,
) -> *mut WheelhouseAgentTasks {
    run(h, error_out, |h| {
        let tasks = typed(&h.rich, |t| match t {
            Typed::Tasks(tasks) => Some(tasks),
            _ => None,
        });
        let text = |value: &Option<String>| Text::borrowed(value.as_deref().unwrap_or_default());
        let views: Vec<WheelhouseAgentTask> = tasks
            .iter()
            .map(|task| {
                let output = task.output.as_ref();
                WheelhouseAgentTask {
                    id: Text::borrowed(&task.id),
                    kind: match task.kind {
                        TaskKind::Other => WHEELHOUSE_TASK_KIND_OTHER,
                        TaskKind::Subagent => WHEELHOUSE_TASK_KIND_SUBAGENT,
                        TaskKind::Shell => WHEELHOUSE_TASK_KIND_SHELL,
                        TaskKind::Monitor => WHEELHOUSE_TASK_KIND_MONITOR,
                    },
                    title: Text::borrowed(&task.title),
                    status: match task.status {
                        TaskStatus::Running => WHEELHOUSE_TASK_RUNNING,
                        TaskStatus::Completed => WHEELHOUSE_TASK_COMPLETED,
                        TaskStatus::Failed => WHEELHOUSE_TASK_FAILED,
                        TaskStatus::Cancelled => WHEELHOUSE_TASK_CANCELLED,
                        TaskStatus::Unknown => WHEELHOUSE_TASK_UNKNOWN,
                    },
                    started_at: task.started_at,
                    has_ended_at: task.ended_at.is_some() as u32,
                    ended_at: task.ended_at.unwrap_or(0),
                    tool_call_id: Text::borrowed(&task.tool_call_id),
                    child_thread_id: text(&task.child_thread_id),
                    awaited: (task.awaited == Some(true)) as u32,
                    has_output: output.is_some() as u32,
                    output_path: Text::borrowed(output.and_then(|o| o.path.as_deref()).unwrap_or_default()),
                    output_local: output.is_some_and(|o| o.local) as u32,
                    output_url: Text::borrowed(output.and_then(|o| o.url.as_deref()).unwrap_or_default()),
                    output_media_type: Text::borrowed(output.and_then(|o| o.media_type.as_deref()).unwrap_or_default()),
                }
            })
            .collect();
        let value =
            WheelhouseAgentTasks { tasks: views.as_ptr(), task_count: views.len(), revision: h.rich.conversation_documents_revision() };
        Ok(give::<_, TasksKeep>(value, (tasks, views)))
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn wheelhouse_agent_tasks_free(tasks: *mut WheelhouseAgentTasks) {
    take::<_, TasksKeep>(tasks)
}

#[cfg(test)]
mod tests {
    use super::*;
    use conversation_events::{Event, StateSnapshot, CHECKLIST_MEDIA_TYPE, TASKS_MEDIA_TYPE};
    use serde_json::json;

    fn text(t: Text) -> String {
        unsafe { String::from_utf8(slice(t.data, t.len).unwrap().to_vec()).unwrap() }
    }

    fn state(seq: u64, key: &str, media_type: Option<&str>, snapshot: serde_json::Value) -> Event {
        Event::StateSnapshot(StateSnapshot {
            thread_id: "t".into(),
            seq,
            key: Some(key.into()),
            media_type: media_type.map(str::to_owned),
            snapshot,
        })
    }

    // Through C, a thread's documents list with their media types, unknown
    // keys included, and the checklist and tasks read back typed; restating
    // a document as it is leaves the revision alone (#777).
    #[test]
    fn documents_list_and_read_back_typed_through_c() {
        let mut doc = WheelhouseRichText::new(RichText::watch_conversation(None));
        let checklist = json!([{"id": "1", "content": "Run the tests", "status": "in_progress", "activeForm": "Running the tests"}]);
        let tasks = json!([{"id": "k", "kind": "shell", "title": "cargo test", "status": "completed", "startedAt": 5, "endedAt": 9,
            "toolCallId": "c1", "awaited": true, "output": {"path": "/tmp/out.log", "local": true, "mediaType": "text/x-ansi"}}]);
        doc.rich.receive(vec![
            state(1, "checklist", Some(CHECKLIST_MEDIA_TYPE), checklist.clone()),
            state(2, "tasks", Some(TASKS_MEDIA_TYPE), tasks),
            state(3, "x-mine", None, json!({})),
        ]);
        let h = &mut doc as *mut WheelhouseRichText;
        unsafe {
            let list = wheelhouse_rich_text_conversation_documents(h, ptr::null_mut());
            let entries = slice((*list).documents, (*list).document_count).unwrap();
            let read: Vec<(String, String)> = entries.iter().map(|d| (text(d.key), text(d.media_type))).collect();
            assert_eq!(
                read,
                [
                    ("checklist".into(), CHECKLIST_MEDIA_TYPE.into()),
                    ("tasks".into(), TASKS_MEDIA_TYPE.into()),
                    ("x-mine".into(), String::new())
                ]
            );
            let revision = (*list).revision;
            wheelhouse_agent_documents_free(list);

            let checklist_view = wheelhouse_rich_text_conversation_checklist(h, ptr::null_mut());
            let item = &slice((*checklist_view).items, (*checklist_view).item_count).unwrap()[0];
            assert_eq!(
                (text(item.content), item.status, text(item.active_form)),
                ("Run the tests".into(), WHEELHOUSE_CHECKLIST_IN_PROGRESS, "Running the tests".into())
            );
            wheelhouse_checklist_free(checklist_view);

            let tasks_view = wheelhouse_rich_text_conversation_tasks(h, ptr::null_mut());
            let task = &slice((*tasks_view).tasks, (*tasks_view).task_count).unwrap()[0];
            assert_eq!(
                (task.kind, task.status, task.started_at, task.has_ended_at, task.ended_at),
                (WHEELHOUSE_TASK_KIND_SHELL, WHEELHOUSE_TASK_COMPLETED, 5, 1, 9)
            );
            assert_eq!(
                (text(task.output_path), task.output_local, text(task.output_url), task.awaited),
                ("/tmp/out.log".into(), 1, String::new(), 1)
            );
            wheelhouse_agent_tasks_free(tasks_view);

            doc.rich.receive(vec![state(4, "checklist", None, checklist)]);
            assert_eq!(wheelhouse_rich_text_conversation_documents_revision(h, ptr::null_mut()), revision, "nothing changed");
        }
    }
}
