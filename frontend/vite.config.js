import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  server: {
    // Proxy /api to Traefik so the dev server doesn't need CORS headers.
    proxy: {
      '/api': 'http://localhost',
    },
  },
})
