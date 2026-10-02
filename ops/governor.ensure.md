---
kind: ensure
role: governor
driver: udder
---
workflow: wheelhouse-governor
placement: docker-crew-image-udder
agents:
  - governor=claude-code:claude-opus-5-5
