import { expect, test, type Page } from "@playwright/test";

// The page's model is built only from deltas; at every point it must be what
// a fresh snapshot of the core gives, and the page must show what it holds.
async function compare(page: Page) {
  const [model, fresh] = await page.evaluate(() => {
    const wheelhouse = (window as any).wheelhouse;
    return JSON.stringify([wheelhouse.model(), wheelhouse.fresh()]);
  }).then(JSON.parse);
  expect(model).toEqual(fresh);
  return model;
}

// Rows shown: each section's roots, and the children of open nodes.
function shown(view: any): number {
  let count = 0;
  for (const { nodes, children } of view.sections) {
    const visit = (key: string) => {
      count++;
      if (!nodes[key].collapsed) (children[key] ?? []).forEach(visit);
    };
    (children[""] ?? []).forEach(visit);
  }
  return count;
}

test.beforeEach(async ({ page }) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  await page.waitForFunction(() => (window as any).wheelhouse !== undefined);
  expect(errors).toEqual([]);
});

test("the page shows the fixture as the core presents it", async ({ page }) => {
  const view = await compare(page);
  expect(shown(view)).toBeGreaterThan(5);
  await expect(page.locator("li[data-key]")).toHaveCount(shown(view));
});

test("clicks change the core by intent, and the page follows", async ({ page }) => {
  // Collapse a node with children, then open it again.
  const before = shown(await compare(page));
  for (let i = 0; i < 2; i++) {
    await page.locator("button.caret").first().click();
    const view = await compare(page);
    await expect(page.locator("li[data-key]")).toHaveCount(shown(view));
    expect(shown(view) === before).toBe(i === 1);
  }
  const variable = page.locator(".variables input").first();
  if ((await variable.count()) > 0) {
    await variable.click();
    const view = await compare(page);
    await expect(page.locator("li[data-key]")).toHaveCount(shown(view));
  }
  // Opening a latent node fails in the browser, and the error shows.
  const latent = page.locator('button.label[data-state="latent"]').first();
  await latent.click();
  const view = await compare(page);
  expect(view.errors.length).toBeGreaterThan(0);
  await expect(page.locator(".error").first()).toBeVisible();
});

test("facts arriving later reach the page as deltas", async ({ page }) => {
  await page.evaluate(() => {
    const wheelhouse = (window as any).wheelhouse;
    wheelhouse.apply(
      1000,
      JSON.stringify({
        target: { kind: "entity", value: { kind: "project", id: "p" } },
        source_id: "fixture",
        set: { "display.label": { value: { type: "text", value: "Renamed by a later fact" } } },
      }),
    );
  });
  await compare(page);
  await expect(page.getByText("Renamed by a later fact").first()).toBeVisible();
});

test("a page whose subscription is lost follows again from a new one", async ({ page }) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.evaluate(() => {
    const wheelhouse = (window as any).wheelhouse;
    wheelhouse.forget();
    wheelhouse.apply(
      1000,
      JSON.stringify({
        target: { kind: "entity", value: { kind: "project", id: "p" } },
        source_id: "fixture",
        set: { "display.label": { value: { type: "text", value: "Renamed after the subscription was lost" } } },
      }),
    );
  });
  await page.evaluate(() => new Promise((resolve) => requestAnimationFrame(resolve)));
  await compare(page);
  await expect(page.getByText("Renamed after the subscription was lost").first()).toBeVisible();
  expect(errors).toEqual([]);
});
