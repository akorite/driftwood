#pragma once

namespace driftwood {

// Run the HTTP server on the given port (default 8080).
// Serves the web UI and exposes the engine API.
// Returns 0 on clean shutdown, 1 on error.
int run_serve(int port);

} // namespace driftwood
