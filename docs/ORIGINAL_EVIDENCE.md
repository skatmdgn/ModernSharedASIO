# Original DLL evidence -> modernization decisions

Original reference: https://github.com/skatmdgn/ASIOGLLD, latest examined Batch23, file SHA256 `e2a9088a71e61362b4b1cf9d11717a1278ef26c940e7ca716290cab7a93c5cfd`.

These are assembly-backed *static* observations documented in the reverse-engineering repository, not Cubase runtime failures proven on a user machine.

| Original observation | ASIOGLLD evidence | ModernSharedASIO decision |
|---|---|---|
| `createBuffers` discards worker setup failure around RVA 0x1CE7 | `BATCH17_FAILURE_PROPAGATION.md` | New WASAPI backend publishes initialization HRESULT. Future ASIO adapter must forward failures too. |
| `WorkerStop` waits up to 3000ms but releases resources without testing wait result | `BATCH16_WORKER_COM_LIFETIME.md` | Stop signal + unbounded join before COM/handle teardown; cancellation mechanism must be tested. |
| Poll worker uses 1ms waits and persistent port completion mask | `BATCH10_WORKER_CONTROL_FLOW.md` | Windows audio-event scheduling instead of periodic 1ms poll; profile CPU before making improvement claims. |
| Critical section includes ASIO callback in recovered worker path | `BATCH10_WORKER_CONTROL_FLOW.md` | No callback under driver mutex; SPSC buffer decouples producer and consumer. |
| 24-slot IASIO virtual table and callback paths recovered | `ASIO_VTABLE.md`, `ASIO_CALLBACK_PIPELINE.md` | Preserve host ABI contract in future COM DLL; NOT implemented yet. |
| `getLatencies` reports 2x buffer frames for input/output | `IASIO_BASIC_METHODS.md` | Plan device-aware latency reporting; no invented physical latency. |
| MMCSS and IAudioClock already present | `BATCH11_PCM_CLOCK.md` | Reuse established concepts instead of assuming all old code is inferior. |
| Stereo/PCM copy helpers exist | `BATCH11_PCM_CLOCK.md` | Optimize only after profiler evidence. |
| Existing shared/exclusive configuration defaults may favor exclusive | `CONFIG_REGISTRY_RECOVERY.md` | New backend explicitly shared only. |

**Not established:** that the original has high CPU overhead in a particular project, that it caused dropouts, or that the new backend is measurably faster. No Windows measurements or real Cubase A/B have occurred yet.
