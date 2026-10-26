import { defineConfig } from "@playwright/test";

// The page is built (core included) and served by `vite preview`.
export default defineConfig({
  testDir: "tests",
  use: { baseURL: "http://localhost:5179" },
  webServer: { command: "pnpm build && pnpm exec vite preview", url: "http://localhost:5179", timeout: 300_000, reuseExistingServer: false },
});
