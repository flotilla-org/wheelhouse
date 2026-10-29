// The sidebar in the browser: the core runs as wasm, the page replays the
// native fixture's facts into it, follows its change stream from the event
// loop, and changes it by intent. No relay yet: the facts are recorded.
import { batch, For, Show } from "solid-js";
import { render } from "solid-js/web";
import { unwrap } from "solid-js/store";
import init, { Core, type Effect, type Intent, type Node, type Receipt } from "./core/wheelhouse_wasm";
import { asView, createModel, type SectionModel } from "./model";
import config from "../../data/sidebar/daily-driver.kdl?raw";
import facts from "../../data/sidebar/fixture.jsonl?raw";
import "./style.css";

const PROVIDER = "fixture";

await init();
const core = new Core(config);
let subscription = core.subscribe();
const { model, apply, reset } = createModel();

// Both calls throw for a subscription the core no longer knows. Uncaught,
// that would stop the frame loop for good: follow again from a new
// subscription, whose first delta rebuilds the model.
function follow() {
  try {
    if (core.changedSince(subscription)) {
      const delta = core.poll(subscription);
      if (delta) apply(delta);
    }
  } catch (error) {
    console.warn("following the sidebar again:", error);
    subscription = core.subscribe();
    const delta = core.poll(subscription);
    batch(() => {
      reset();
      if (delta) apply(delta);
    });
  }
}

// The browser opens no workspaces yet: requests that would need one fail.
function carryOut(effect: Effect) {
  switch (effect.effect) {
    case "focus":
      core.complete(effect.request, undefined, undefined);
      break;
    case "materialize":
      core.complete(effect.request, undefined, "the browser can't open workspaces yet");
      break;
    case "open-url":
      window.open(effect.url, "_blank", "noopener");
      break;
    case "copy-url":
      void navigator.clipboard?.writeText(effect.url);
      break;
    case "inspect":
      break;
  }
}

function intend(intent: Intent): Receipt {
  const receipt = core.applyIntent(intent);
  if (receipt.receipt === "accepted") receipt.effects.forEach(carryOut);
  follow();
  return receipt;
}

function NodeRow(props: { section: SectionModel; nodeKey: string }) {
  const node = (): Node => props.section.nodes[props.nodeKey];
  const children = () => props.section.children[props.nodeKey] ?? [];
  const state = () => node().state;
  return (
    <li classList={{ stale: node().stale, attention: node().content.presented.attention }} data-key={props.nodeKey}>
      <div class="row">
        <Show when={children().length > 0} fallback={<span class="caret" />}>
          <button class="caret" aria-label="toggle" onClick={() => intend({ intent: "toggle-placement", key: props.nodeKey })}>
            {node().collapsed ? "▸" : "▾"}
          </button>
        </Show>
        <button class="label" data-state={state().state} onClick={() => intend({ intent: "activate-placement", key: props.nodeKey })}>
          {node().label}
        </button>
        <Show when={node().content.presented.status}>{(status) => <span class="status">{status()}</span>}</Show>
      </div>
      <Show when={!node().collapsed && children().length > 0}>
        <ul>
          <For each={children()}>{(key) => <NodeRow section={props.section} nodeKey={key} />}</For>
        </ul>
      </Show>
    </li>
  );
}

function Sidebar() {
  return (
    <main>
      <nav class="variables">
        <For each={model.variables.filter((v) => typeof v.value === "boolean")}>
          {(variable) => (
            <label>
              <input
                type="checkbox"
                checked={variable.value === true}
                onChange={() => intend({ intent: "toggle-display-variable", name: variable.name })}
              />
              {variable.label || variable.name}
            </label>
          )}
        </For>
      </nav>
      <For each={model.order}>
        {(name) => {
          const section = () => model.sections[name];
          const roots = () => section().children[""] ?? [];
          return (
            <Show when={roots().length > 0}>
              <section data-section={name}>
                <h2>{section().section.content.fields.map((f) => f.text).join(" ") || name}</h2>
                <ul>
                  <For each={roots()}>{(key) => <NodeRow section={section()} nodeKey={key} />}</For>
                </ul>
              </section>
            </Show>
          );
        }}
      </For>
      <For each={model.errors}>{(error) => <p class="error">{error.entity.id}: {error.message}</p>}</For>
    </main>
  );
}

render(() => <Sidebar />, document.getElementById("root")!);

// Recorded facts at time 0, and no tick: the page is static until a relay
// gives it live facts and a clock to tick the core by.
core.apply(0, PROVIDER, facts);
follow();
const frame = () => {
  follow();
  requestAnimationFrame(frame);
};
requestAnimationFrame(frame);

// For tests: the page's model, and a fresh view to compare it with.
Object.assign(window, {
  wheelhouse: {
    model: () => structuredClone(asView(unwrap(model))),
    fresh: () => core.view(),
    apply: (now: number, jsonl: string) => {
      core.apply(now, PROVIDER, jsonl);
      follow();
    },
    intend,
    forget: () => core.unsubscribe(subscription),
  },
});
