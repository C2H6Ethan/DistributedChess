# ── Stage 1: build ────────────────────────────────────────────────────────────
FROM node:20-alpine AS builder

WORKDIR /app

# Install deps in a separate layer — only re-runs when package files change.
COPY package.json package-lock.json ./
RUN npm ci

# Build the Vite production bundle.
COPY . .
RUN npm run build

# ── Stage 2: runtime ──────────────────────────────────────────────────────────
FROM nginx:alpine

# Replace the default config with one that handles SPA routing:
# every path that isn't a real file falls back to index.html so the
# React app can take over client-side.
RUN printf 'server {\n\
    listen 80;\n\
    root /usr/share/nginx/html;\n\
    index index.html;\n\
    location / {\n\
        try_files $uri $uri/ /index.html;\n\
    }\n\
}\n' > /etc/nginx/conf.d/default.conf

COPY --from=builder /app/dist /usr/share/nginx/html

EXPOSE 80
