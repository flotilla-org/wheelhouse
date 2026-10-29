// The page's model of the sidebar, built only from the core's deltas
// (docs/design/change-stream.md): sections in order, each with its nodes by
// key and its child lists by parent key ("" for the section's own). Each
// delta is applied whole, inside one store update, so nothing renders a
// half-applied change.
import { createStore, produce, reconcile } from "solid-js/store";
import type { ActivationError, Delta, ListOp, Node, Section, Variable, View } from "./core/wheelhouse_wasm";

export interface SectionModel {
  section: Section;
  nodes: Record<string, Node>;
  children: Record<string, string[]>;
}

export interface Model {
  generation: number;
  order: string[];
  sections: Record<string, SectionModel>;
  variables: Variable[];
  diagnostics: string[];
  errors: ActivationError[];
}

function applyList(list: string[], ops: ListOp[]) {
  for (const op of ops) {
    switch (op.op) {
      case "insert":
        list.splice(op.at, 0, op.key);
        break;
      case "remove":
        list.splice(op.at, 1);
        break;
      case "move":
        list.splice(op.to, 0, list.splice(op.from, 1)[0]);
        break;
    }
  }
}

export function applyDelta(model: Model, delta: Delta) {
  for (const op of delta.ops) {
    switch (op.op) {
      case "section": {
        const existing = model.sections[op.section.name];
        if (existing) existing.section = op.section;
        else model.sections[op.section.name] = { section: op.section, nodes: {}, children: {} };
        break;
      }
      case "remove-section":
        delete model.sections[op.name];
        break;
      case "sections":
        applyList(model.order, op.ops);
        break;
      case "upsert":
        model.sections[op.section].nodes[op.node.key] = op.node;
        break;
      case "remove": {
        const section = model.sections[op.section];
        delete section.nodes[op.key];
        delete section.children[op.key];
        break;
      }
      case "children": {
        const section = model.sections[op.section];
        const parent = op.parent ?? "";
        const list = section.children[parent] ?? (section.children[parent] = []);
        applyList(list, op.ops);
        if (list.length === 0) delete section.children[parent];
        break;
      }
      case "display-variables":
        model.variables = op.variables;
        break;
      case "diagnostics":
        model.diagnostics = op.diagnostics;
        break;
      case "errors":
        model.errors = op.errors;
        break;
    }
  }
  model.generation = delta.generation;
}

const empty = (): Model => ({ generation: 0, order: [], sections: {}, variables: [], diagnostics: [], errors: [] });

/// The model, a delta's whole application to it, and its reset to the
/// initial model, to follow again from a new subscription's first delta.
export function createModel() {
  const [model, setModel] = createStore<Model>(empty());
  return {
    model,
    apply: (delta: Delta) => setModel(produce((m) => applyDelta(m, delta))),
    reset: () => setModel(reconcile(empty())),
  };
}

/// The model in the shape of the core's View, to compare with a fresh one.
export function asView(model: Model): View {
  return {
    generation: model.generation,
    sections: model.order.map((name) => {
      const { section, nodes, children } = model.sections[name];
      return { section, nodes, children };
    }),
    variables: model.variables,
    diagnostics: model.diagnostics,
    errors: model.errors,
  } as View;
}
