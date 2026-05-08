# syntax=docker/dockerfile:1.6
ARG NODE_IMAGE=node:20-alpine

# ---- build ----
FROM ${NODE_IMAGE} AS build
WORKDIR /app
COPY package.json package-lock.json* ./
RUN npm install --no-audit --no-fund
COPY . .
# Same-origin: nginx proxies /api/* to the Windows backend.
# Browser stays on http://localhost:8080 → no Private Network Access blocking.
ARG VITE_API_BASE=
ENV VITE_API_BASE=$VITE_API_BASE
RUN npm run build

# ---- serve ----
FROM nginx:alpine
COPY --from=build /app/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 80
