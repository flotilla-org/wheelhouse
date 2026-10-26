import { defineConfig } from "vite";
import solid from "vite-plugin-solid";

// The page reads the daily driver's templates and the native fixture from
// data/sidebar, outside this directory. It awaits the core at top level.
export default defineConfig({
  plugins: [solid()],
  build: { target: "es2022" },
  server: { port: 5178, fs: { allow: [".."] } },
  preview: { port: 5179 },
});
