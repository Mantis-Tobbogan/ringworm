# ADR 0005: Liveness is the connection; no application-level heartbeats

Status: Accepted
Date: 2026-07-18

## Context

The daemon originally sent a `"Version"` heartbeat request every second over
the niri IPC connection, and faded the ring if no traffic arrived within a
staleness window. In daily use two failures appeared:

1. The ring faded out whenever the cursor rested (no motion events →
   heartbeat replies were the only traffic keeping the feed "alive").
2. ~5.5 minutes after every connect, the daemon's IPC send buffer wedged
   permanently (213KB stuck, all writes EAGAIN), so the heartbeat replies
   stopped entirely — the ring faded until the daemon was restarted.

The root cause of (2) is structural in niri's IPC server:
`handle_client` **returns after subscribing an event-stream client** — the
connection's read side is finished, and niri never reads another byte from
it. Every heartbeat ping accumulated in the server's receive queue as an
skb (~640 bytes of truesize each); 212992 bytes of default send buffer /
~640 bytes per ping ≈ 333 pings ≈ 5.5 minutes to wedge.

## Decision

The daemon performs **zero writes after the event-stream subscription**.
Liveness is the connection itself: a dead or restarted compositor closes
the socket and the read loop sees EOF immediately, dropping the ring via
the reconnect path. A resting cursor produces no events and that is normal,
not staleness. Stale-fade logic and the `stale_ms` config key were removed.

## Consequences

- The send buffer can never wedge from our side; the ring is stable for
  hours with a resting cursor.
- A *hung* (not dead) compositor leaves the ring frozen at the last
  position — accepted: a hung compositor freezes the entire desktop, and
  any client-side probing would only be possible by writing to the very
  connection niri no longer reads.
- If niri later reads requests from event-stream clients, a heartbeat could
  be reintroduced — not needed for correctness today.
