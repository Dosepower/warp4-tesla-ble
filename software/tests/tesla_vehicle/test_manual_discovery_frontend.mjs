// Approved guided UX replaces select-then-generic-save with direct adoption.
// Production component/event coverage is shared with guided tests, not removed:
// explicit scan, confirmation cancellation, busy, VIN validation, unique/fresh
// matching, no neighbour/manual selection, A-B-A, save response and no-auto gates.
await import('./test_guided_frontend.mjs');
console.log('MANUAL_DISCOVERY_FRONTEND_PASS guided production event contract');
