//! The overview scene (docs/design/overview-scene.md). The overview is one
//! scene that every open workspace sits in, and a window's workspace region
//! is a camera on it: a workspace is open when the camera fits its tile.
//!
//! Three steps make the picture, each a function of the one before:
//! [`logical_layout`] groups the sidebar's live workspaces, [`scene`] places
//! them for a view mode, and a [`CameraRig`] flies between the cameras the
//! scene gives ([`Scene::rest_camera`], [`Scene::overview_camera`],
//! [`Scene::group_camera`]). All three are pure; a host draws the scene
//! through the camera.
mod camera;
mod logical;
mod scene;
mod window;

pub use camera::{ease_out, Camera, CameraRig, Flight, FlightTiming, ZoomPath};
pub use logical::{entries, logical_layout, Cluster, Group, GroupKey, LogicalLayout, OverviewEntry, Tile, OTHER_WORKSPACES};
pub use scene::{scene, Scene, SceneGroup, SceneOptions, SceneRect, SceneTile, ViewMode};
pub use window::{OverviewMode, OverviewWindow};

#[cfg(test)]
mod tests;
