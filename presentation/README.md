# Lightweight Presentations

This directory contains presentation slides for the Lightweight C++ ODBC library, built with [reveal.js](https://revealjs.com/).

## Available Presentations

- **[index.html](index.html)** - Main Lightweight library overview (low-level API, Query Builder, DataMapper)
- **[sql-migrations.html](sql-migrations.html)** - SQL Migrations API and dbtool CLI

## Quick Start

### Installation

```bash
cd presentation
npm install
```

### Development (Live Preview)

Start a live preview server with auto-reload:

```bash
npm run dev
```

This opens your browser at `http://localhost:8000` with the presentation and automatically refreshes when you edit `index.html`.

### Build for Production

Bundle the presentation for deployment:

```bash
npm run build
```

This creates a `dist/` folder with all assets ready for hosting.

## Hosting Online

### GitHub Pages (Automatic)

The presentation is automatically deployed alongside the API documentation when changes are pushed to the `master` branch.

**URL structure:**
- Documentation: `https://<org>.github.io/Lightweight/`
- Presentation: `https://<org>.github.io/Lightweight/presentation/`

The deployment is handled by `.github/workflows/docs.yml` which:
1. Builds Doxygen documentation
2. Builds the reveal.js presentation
3. Deploys both to the `gh-pages` branch

## PDF Export

1. Open the presentation in Chrome
2. Add `?print-pdf` to the URL
3. Print to PDF (Ctrl+P / Cmd+P)
4. Set Layout to "Landscape"
5. Enable "Background graphics"
