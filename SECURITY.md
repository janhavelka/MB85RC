# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| Current 4.x development line | :white_check_mark: |
| Latest tagged 3.x release | :white_check_mark: |
| 2.x and older | :x: |

Until the 4.0.0 tag is published, the current reviewed 4.x branch and latest
tagged 3.x release are the supported reporting targets. Security fixes for
older release lines are best effort only.

## Reporting a Vulnerability

If you discover a security vulnerability within this library, please follow responsible disclosure:

1. **Do NOT** open a public GitHub issue.
2. Email the maintainer at: `info@thymos.cz`.
3. Include:
   - A description of the vulnerability
   - Steps to reproduce
   - Potential impact
   - Any suggested fixes (optional)

We will acknowledge receipt within 48 hours and aim to provide a fix or mitigation within 14 days for critical issues.

## Scope

This library is designed for embedded systems. Security considerations include:

- No dynamic memory allocation in steady state (reduces attack surface)
- No network code (networking is out of scope for this library)
- Raw persistent storage only: the library provides no authentication,
  encryption, access control, record schema, atomic journal, or rollback policy
- Application-owned I2C bus, locking, deadlines, retry, and recovery policy
- Diagnostic examples include destructive commands and are not production
  authorization boundaries

## Security Best Practices for Users

- Validate addresses, lengths, and all externally sourced data before issuing
  storage operations
- Serialize access to each driver instance and shared I2C bus
- Keep destructive diagnostic CLIs disabled or access-controlled in production
- Use readback verification plus an application-level journal for critical data
- Use hardware watchdogs in production deployments
- Keep dependencies updated
