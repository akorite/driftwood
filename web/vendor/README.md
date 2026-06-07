# Vendored Libraries

The web UI uses two JavaScript libraries loaded via CDN by default:

- **chessground** v7.13.0 — Lichess's board component (renders the chessboard with drag-and-drop). Provides a global `Chessground` constructor.
- **chess.js** v0.13.4 — Move validation, SAN notation, FEN parsing. Provides a global `Chess` constructor.

These are non-ESM builds that work with plain `<script>` tags.

CDN URLs (used in `index.html`):
- `https://unpkg.com/chessground@7.13.0/dist/chessground.min.js`
- `https://unpkg.com/chessground@7.13.0/dist/chessground.min.css`
- `https://unpkg.com/chess.js@0.13.4/dist/chess.min.js`

## Vendoring Locally

To vendor these files locally (for offline use or faster load), download them into this directory and update `index.html`:

```bash
# chessground
curl -sL -o chessground.min.js \
  https://unpkg.com/chessground@7.13.0/dist/chessground.min.js
curl -sL -o chessground.min.css \
  https://unpkg.com/chessground@7.13.0/dist/chessground.min.css

# chess.js
curl -sL -o chess.min.js \
  https://unpkg.com/chess.js@0.13.4/dist/chess.min.js
```

Then update the `<link>` and `<script>` tags in `web/index.html` to reference local paths:

```html
<link rel="stylesheet" href="vendor/chessground.min.css">
...
<script src="vendor/chess.min.js"></script>
<script src="vendor/chessground.min.js"></script>
```
