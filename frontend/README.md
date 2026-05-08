# MT5 ReportTool — Frontend

React + Vite + TypeScript + Tailwind CSS. Runs in Docker (nginx:alpine).

## Local dev (without Docker)

```bash
npm install
VITE_API_BASE=http://localhost:5151 npm run dev
# UI on http://localhost:5173
```

## Docker

```bash
docker compose up -d --build
# UI on http://localhost:8080
```

The container's nginx **reverse-proxies** `/api/*` and `/health` to
`host.docker.internal:5151`. The browser only talks to `localhost:8080`
(same-origin), which avoids Chrome's Private Network Access blocking
(`ERR_NETWORK_ACCESS_DENIED`) and CORS preflights.

If your backend lives on a different machine, edit `nginx.conf`'s
`proxy_pass` target and rebuild:

```bash
docker compose up -d --build --force-recreate
```

## Pages

- `/managers` — list / create / edit / test / delete managers
- `/managers/new` and `/managers/:id/edit` — full form, including four
  `RegexListEditor` instances (deposit / withdrawal / writeoff / adjustment)
- `/reports` — Top Winner and Summary tabs; runs a job, polls every 2s, and
  renders results inline once complete
- `/history` — past jobs with download links
