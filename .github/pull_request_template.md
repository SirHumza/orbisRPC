# Pull request checklist

- [ ] Host suite green: `make -C tests test`, `make -C tests asan`,
      `ORBISRPC_STRICT_SECRETS=1 python3 tests/e2e_consumer.py` (paste outputs)
- [ ] No Discord tokens or webhook URLs committed (e2e secret scan green)
- [ ] Linkage set unchanged (`libkernel_web`, `libSceLibcInternal`,
      `libSceNet`, `libSceSystemService` — never `libkernel.so`)
- [ ] Docs updated if behavior changed (`README.md`, `docs/`)
- [ ] `orbisrpc/` diff is empty unless this is a daemon change — daemon
      changes need on-console proof (log lines, firmware tested)
