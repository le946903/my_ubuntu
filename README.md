# Advanced P2P LAN Chat + Texas Hold'em

A peer-to-peer LAN chat client with causal-order message delivery and a
fully distributed Texas Hold'em poker table.

## Build

```bash
mkdir build && cd build
cmake ..
make -j
```

Produces `new_advanced_chat`.

## Run

```bash
./new_advanced_chat <your_name>
```

Two or more instances on the same LAN will discover each other via
UDP multicast on `239.255.42.99:45454`, then establish TCP connections.

## Chat commands

| Command | Effect |
|---|---|
| `/peers` | List known peers |
| `/quit`  | Leave the room |

## Poker commands

| Command | Effect |
|---|---|
| `/poker join`  | Sit down at the table |
| `/poker leave` | Leave the table (auto-fold if in hand) |
| `/poker rebuy` | Rebuy chips when broke (only between hands) |
| `/poker start` | Start auto-rotating hands |
| `/poker stop`  | Stop auto-rotation (current hand finishes) |
| `/poker deal`  | Play a single hand |
| `/poker state` | Print current table state |

## Betting actions

| Input | Effect |
|---|---|
| `f` / `fold` | Fold |
| `c` / `check` / `call` | Check or call |
| `r N` / `raise N` / `bet N` | Raise/bet to N |

## Architecture

- **`config.hpp`** – Compile-time constants.
- **`helpers`** – ANSI/UTF-8 aware console formatting and logging.
- **`crypto`** – SHA-256 identity hashes and a counter-mode PRNG.
- **`vector_clock`** – Lamport/vector clock serialization.
- **`card` / `hand_eval` / `deck`** – Poker primitives and 7-card evaluator.
- **`message`** – Wire format with SHA-256 integrity check.
- **`causal_broadcast`** – Causal-order delivery layer.
- **`peer` / `net_io`** – UDP discovery, TCP transport, peer reaper.
- **`poker_game`** – Distributed Texas Hold'em state machine
  (commit/reveal shuffle, betting rounds, showdown).
- **`app` / `main`** – Wiring, REPL, thread launch.

## Notes

- Uses OpenSSL for SHA-256.
- All poker messages are broadcast and verified via hash; hole cards are
  delivered via private (targeted) messages.
- The shuffle is a commit-reveal scheme: each player commits to a secret seed
  (SHA-256), then reveals it; all revealed seeds are fed into a SHA-256
  counter-mode PRNG to deterministically shuffle the deck.
