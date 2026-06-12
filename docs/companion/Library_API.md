# Library API (companion — produced by its backlog task; operator review of
this document gates all `src/api/` work)

Documents the exact public C++ surface (`src/TeensyCommandServer.h`,
`src/api/*`) and its one-to-one mapping to the contract §5 operations:

```text
set board identity | register command | register telemetry schema
register state schema | set telemetry provider | set e-stop hook
set controller-loss hook | start command server | service command server
```

Until written, no public signature is final. Authority: contract §5 wins on
any conflict with this document.
