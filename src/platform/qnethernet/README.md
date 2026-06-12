# src/platform/qnethernet — the only home for Arduino networking types

Adapts QNEthernet sockets/link state to the core transport interfaces. Makes
no protocol decisions. Read the `qnethernet-transport` skill first.

Planned files (created by backlog Phase 9):

```text
QNEthernetTransport.h/.cpp  socket reads/writes, deadline-bounded write, flush, setNoDelay
QNEthernetServer.h/.cpp     listen, accept, close-old supersession wiring §9.1
QNEthernetClock.h           millis()/micros() clock implementation
```
