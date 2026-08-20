# Central Web Application & Controller

The central fleet management and WebSocket controller for the S905X Bitcoin Mining Fleet.

---

## Architecture
- **Frontend**: React 18 + TypeScript + Tailwind CSS (Vite build)
- **Backend Server**: Node.js + Express
- **Real-Time Communication**: WebSocket (`ws`) on port `3010` (or `PORT` environment variable)
  - `/ws/client`: Web browser dashboards
  - `/ws/worker`: Outbound worker connections from S905X TV boxes

---

## Getting Started

### 1. Install Dependencies
```bash
npm install
```

### 2. Configure Environment
```bash
cp .env.example .env
```

### 3. Development Mode
```bash
npm run dev
```

### 4. Production Build & Start
```bash
npm run build
npm start
```
