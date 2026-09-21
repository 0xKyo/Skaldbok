import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';

// In development the Vue client runs on :5173 and forwards /api to the Express server on :8080 (npm run dev:server).
export default defineConfig({
  plugins: [vue()],
  server: { proxy: { '/api': process.env.API_URL ?? 'http://localhost:8080' } },
  build: { outDir: 'dist', sourcemap: false },
  test: { environment: 'jsdom', globals: false, include: ['src/**/*.spec.js'] },
});
