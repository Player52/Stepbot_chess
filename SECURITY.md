# Security Policy

## Supported Versions

Stepbot is a personal chess engine project. Only the latest version on the `main` branch is actively maintained.

| Version | Supported |
|---------|-----------|
| Latest (main) | ✅ |
| Older commits | ❌ |
| Python Files  | ❌ |

## Reporting a Vulnerability

Stepbot is a chess engine — it has no network access, no user authentication, and stores no personal data. The attack surface is minimal by design.

However, if you do discover a security issue (for example a vulnerability in how the engine handles UCI input or PGN files), please report it responsibly:

1. **Do not open a public GitHub issue** for security vulnerabilities
2. **Use GitHub's private vulnerability reporting feature**
3. Include a clear description of the issue and steps to reproduce it
4. Allow reasonable time for the issue to be addressed before public disclosure

## What Counts as a Security Issue?

- Buffer overflows or memory corruption in the C++ engine
- Malicious input via UCI commands or PGN files that causes unexpected behaviour
- Any issue that could affect users who run Stepbot on their system

## What Doesn't Count

- The engine making bad chess moves (that's a bug, not a security issue)
- Losing to Stockfish or other world-class bots (working as intended for now)
