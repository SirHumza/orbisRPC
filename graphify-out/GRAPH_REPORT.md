# Graph Report - v1.1  (2026-11-21)

## Corpus Check
- 728 files · ~2,839,642 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 25 file(s) not represented in the graph (top: (none) 12, .t0 8, .jsonl 2)

## Summary
- 11913 nodes · 52083 edges · 220 communities (210 shown, 10 thin omitted)
- Extraction: 86% EXTRACTED · 14% INFERRED · 0% AMBIGUOUS · INFERRED: 7405 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `e9139cdb`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- sqlite3.c
- sqlite3OomFault
- psa_status_t
- inner.h
- Parse
- Vdbe
- Fts5Index
- sqlite3TreeViewExpr
- stddef
- sqlite3expert
- br_ssl_engine_context
- mbedtls_pk_context
- Index
- ssl_tls.c
- sqlite3_stmt
- sqlite3_context
- ecp.c
- psa_crypto.c
- Fts5Context
- bearssl_block.h
- sqlite3_initialize
- do_meta_command
- mbedtls_mpi_uint
- Expr
- sqlite3_column_bytes
- memsys3MallocUnsafe
- cipher.c
- jsonParseFuncArg
- des.c
- mbedtls_ssl_context
- br_ecdsa_i15_sign_raw
- Fts5SegIter
- unixFile
- sqlite3_vfs
- sqlite3_file
- ssl_tls13_server.c
- sqlite3_value
- Btree
- ssl_tls12_server.c
- mbedtls_mpi
- crypto_extra.h
- stdio
- sqlite3_column_text
- sqlite3_stricmp
- test_utils.c
- x509write_crt.c
- health.c
- Fts5Expr
- ssl.h
- aes.c
- Foreground detection: from heuristics to asking the OS
- ssl_misc.h
- ssl_tls13_client.c
- sqlite3_free
- Rtree
- br_enc64be
- shell.c
- bearssl_x509.h
- jl_free
- net_sockets.c
- ec_c25519_m62.c
- sqlite3DbFree
- fts5yy_reduce
- sqlite3_recover
- installer.c
- sqlite3_result_text
- sqlite3_vmprintf
- br_hmac_drbg_generate
- BtCursor
- lmots.c
- mbedtls_asn1_get_tag
- SQLITE_API
- mbedtls_md_type_t
- Daemon design (v1.1.0)
- chachapoly.c
- sqlite3
- md.c
- u8
- mbedtls_ssl_session
- Fts3Hash
- u8
- sqlite3_create_module
- fts5UnicodeAddExceptions
- psa_crypto_random_impl.h
- psa_crypto_slot_management.c
- ssl_tls12_client.c
- pemdec.c
- mbedtls_platform_zeroize
- discord.c
- DateTime
- br_eax_context
- sha2small.c
- cipher_wrap.c
- constant_time_impl.h
- sqlite3_value_int
- crypto_se_driver.h
- sqlite3Malloc
- mbedtls_psa_hash_setup
- ec_p256_m31.c
- ec_c25519_m15.c
- sqlite3BtreeIntegrityCheck
- mbedtls_x509_crt
- aes_x86ni.c
- des_ct.c
- mbedtls_mpi_free
- OrbisRPC Migration Progress Log
- platform.c
- ecdh.c
- with
- psa_verify_hash_interruptible_operation_t
- x509.c
- mbedtls_x509_csr
- mbedtls_x509_crl
- test_ws.c
- ec_p256_m62.c
- ctr_drbg.c
- psa_crypto_mac.c
- ec_p256_m64.c
- aesce.c
- gamecache.c
- sha256.c
- update_fingerprint.py
- sha512.c
- btreeInvokeBusyHandler
- dhm.c
- memory_buffer_alloc.c
- steps
- with
- worker.js
- decimal_new
- MIN
- i64
- sqlite3_rtree_query_info
- ec_p256_m15.c
- u64
- sqlite3_intck
- br_rsa_private_key
- sqlite3Malloc
- x509_crt.h
- os
- orbisRPC — Discord Rich Presence for PS4 (GoldHEN RPC)
- library/hmac_drbg.c
- docs/README.md
- OrbisRPC Production Report
- br_aes_keysched
- sysrng.c
- ec_prime_i31.c
- Troubleshooting
- yy_destructor
- sha2big.c
- mbedtls_x509_time
- mbedtls_x509_buf
- playtime_report.py
- debug.c
- aria.c
- mps_reader.c
- x509_crt.c
- DWORD
- validate
- psa_crypto_driver_pake_step
- argparse
- json
- TestFetchParsing
- entropy.c
- camellia.c
- NameContext
- gen_fingerprint_header.py
- package.json
- [1.1.0-beta] - 2026-11-03
- Config reference (`/data/orbisRPC/config.json`)
- log_msg
- Workstreams
- geopolyOverlap
- aesni.c
- sessionChangesetInvert
- tmdb_crypto.c
- Injecting OrbisRPC into the PS4 — the complete guide
- Review Focus
- chacha20.c
- orbisRPC 1.1 release notes
- updates
- sqlite3_result_error_nomem
- Cover art pipeline
- Building
- SDK firmware gate: why the payload was silent on 13.52
- Security
- fts5ConfigParseSpecial
- Observing the console (for assistants)
- ELF linkage: why OpenOrbis binaries die in spawned processes
- fts5SnippetFunction
- ripemd160.c
- build.sh
- lms.c
- orbisRPC OAuth broker
- orbisRPC 1.2 beta implementation plan
- orbisRPC docs
- build_evict.sh
- build_sdk.sh
- sha3.c
- pull_request_template.md
- build_sdk_from_source.sh
- sync_icons.sh
- sqlite3_value_text
- statPush
- threading.c
- sqlite3OsOpenMalloc
- editPage
- mbedtls_md_clone
- CellInfo
- sha1QueryFunc
- bindText
- Hash
- mbedtls_x509_authority
- AGENTS.md — orbisRPC contributor notes
- countLookasideSlots

## God Nodes (most connected - your core abstractions)
1. `sqlite3_free()` - 610 edges
2. `sqlite3` - 499 edges
3. `mbedtls_ssl_context` - 478 edges
4. `psa_status_t` - 393 edges
5. `sqlite3_context` - 347 edges
6. `sqlite3_value` - 343 edges
7. `sqlite3_mprintf()` - 205 edges
8. `sqlite3_step()` - 200 edges
9. `sqlite3VdbeExec()` - 189 edges
10. `sqlite3_file` - 184 edges

## Surprising Connections (you probably didn't know these)
- `Changes made` --references--> `daemon_run()`  [INFERRED]
  PROGRESS_LOG.md → orbisrpc/daemon.c
- `The distinction that carries the design` --references--> `detect_current_game()`  [INFERRED]
  docs/research/proc-table-offsets.md → orbisrpc/detect.c
- `Cause` --references--> `log_msg()`  [INFERRED]
  docs/research/sdk-13.52.md → orbisrpc/log.c
- `Loop` --references--> `net_tcp_connect()`  [INFERRED]
  docs/DAEMON.md → orbisrpc/netconnect.c
- `Host-tested changes` --references--> `net_tcp_connect()`  [INFERRED]
  docs/RELEASE_NOTES_1.1.md → orbisrpc/netconnect.c

## Import Cycles
- None detected.

## Communities (220 total, 10 thin omitted)

### Community 0 - "sqlite3.c"
Cohesion: 0.01
Nodes (521): actionName(), addToVTrans(), agginfoFree(), allocateCursor(), allocateTempSpace(), allocSpace(), alsoAnInt(), analyzeFilterKeyword() (+513 more)

### Community 1 - "sqlite3OomFault"
Cohesion: 0.06
Nodes (64): MAX(), addSpaceSeparator(), displayP4Expr(), enlargeAndAppend(), explainAppendTerm(), explainAutomaticIndex(), explainIndexColumnName(), explainIndexRange() (+56 more)

### Community 2 - "psa_status_t"
Cohesion: 0.04
Nodes (149): mbedtls_md_type_from_psa_alg(), MBEDTLS_DEPRECATED psa_get_key_domain_parameters(), MBEDTLS_DEPRECATED psa_set_key_domain_parameters(), psa_aead_operation_t, psa_sign_hash_interruptible_operation_t, psa_get_key_lifetime(), psa_ecc_family_t, psa_key_attributes_t (+141 more)

### Community 3 - "inner.h"
Cohesion: 0.01
Nodes (87): br_config_option, br_ec_public_key, br_rsa_compute_modulus, br_rsa_compute_privexp, br_rsa_compute_pubexp, br_rsa_keygen, br_rsa_oaep_decrypt, br_rsa_oaep_encrypt (+79 more)

### Community 4 - "Parse"
Cohesion: 0.03
Nodes (283): addArgumentToVtab(), addModuleArgument(), adjustOrderByCol(), allocateIndexInfo(), binaryToUnaryIfNull(), cannotBeFunction(), changeTempStorage(), checkColumnOverlap() (+275 more)

### Community 5 - "Vdbe"
Cohesion: 0.05
Nodes (240): addOp4IntSlow(), analyzeDatabase(), analyzeOneTable(), analyzeTable(), analyzeVdbeCommentIndexWithColumnName(), autoIncBegin(), autoIncrementEnd(), autoIncStep() (+232 more)

### Community 6 - "Fts5Index"
Cohesion: 0.06
Nodes (96): MIN(), fts5AllocateSegid(), fts5AppendPoslist(), fts5AppendRowid(), fts5AppendTokendataIter(), fts5AssertComparisonResult(), fts5AssertMultiIterSetup(), fts5BufferCompare() (+88 more)

### Community 7 - "sqlite3TreeViewExpr"
Cohesion: 0.10
Nodes (55): codeRowTrigger(), getRowTrigger(), onErrorText(), sqlite3_test_control(), sqlite3BenignMallocHooks(), sqlite3BtreeSeekCount(), sqlite3CodeRowTriggerDirect(), sqlite3ParseObjectReset() (+47 more)

### Community 8 - "stddef"
Cohesion: 0.02
Nodes (66): br_aead_class, tag_size, br_hash_compat_context, mbedtls_mpi_gen_prime_flag_t, mbedtls_t_udbl, mbedtls_block_cipher_engine_t, mbedtls_block_cipher_id_t, mbedtls_cmac_context_t (+58 more)

### Community 9 - "sqlite3expert"
Cohesion: 0.09
Nodes (41): expertBestIndex(), expertFinish(), expertOpen(), idxAppendColDefn(), idxAppendText(), idxAuthCallback(), idxConstraintFree(), idxCreateCandidates() (+33 more)

### Community 10 - "br_ssl_engine_context"
Cohesion: 0.02
Nodes (241): br_ec_impl, br_ec_private_key, br_ecdsa_sign, br_ecdsa_vrfy, br_multihash_context, br_multihash_getimpl(), br_tls_prf_impl, br_rsa_pkcs1_sign (+233 more)

### Community 11 - "mbedtls_pk_context"
Cohesion: 0.05
Nodes (131): mbedtls_pk_context, mbedtls_pk_debug_item, mbedtls_pk_ec(), mbedtls_pk_restart_ctx, mbedtls_pk_rsa(), mbedtls_pk_type_t, mbedtls_ssl_ticket_context, MBEDTLS_PRIVATE (+123 more)

### Community 12 - "Index"
Cohesion: 0.04
Nodes (119): allConstraintsUsed(), allowedOp(), columnIsGoodIndexCandidate(), computeIndexAffStr(), computeMxChoice(), createMask(), decodeIntArray(), estimateIndexWidth() (+111 more)

### Community 13 - "ssl_tls.c"
Cohesion: 0.03
Nodes (162): mbedtls_ssl_config, mbedtls_svc_key_id_is_null(), mbedtls_asn1_free_named_data_list_shallow(), mbedtls_asn1_sequence_free(), mbedtls_mpi_mod_mul(), mbedtls_pkcs7_free(), mbedtls_free(), mbedtls_zeroize_and_free() (+154 more)

### Community 14 - "sqlite3_stmt"
Cohesion: 0.04
Nodes (189): bind_prepared_stmt(), dbdataLoadPage(), quoted_column(), scanStatsHeight(), tryToCloneData(), blobGrowBuffer(), columnMallocFailure(), columnMem() (+181 more)

### Community 15 - "sqlite3_context"
Cohesion: 0.04
Nodes (120): completionColumn(), dbdataColumn(), dbdataValue(), decimalSumValue(), dummyUDFvalue(), expertColumn(), fsdirColumn(), idxSampleFunc() (+112 more)

### Community 16 - "ecp.c"
Cohesion: 0.06
Nodes (94): mbedtls_ecp_curve_type, mbedtls_ecp_group, A, mbedtls_ecp_group_a_is_minus_3(), B, N, nbits, pbits (+86 more)

### Community 17 - "psa_crypto.c"
Cohesion: 0.04
Nodes (150): mbedtls_ctr_drbg_psa_context, key_id, operation, mbedtls_ssl_tls13_application_secrets, psa_cipher_operation_t, psa_key_derivation_operation_t, psa_mac_operation_t, psa_key_policy_t (+142 more)

### Community 18 - "Fts5Context"
Cohesion: 0.07
Nodes (40): fts5ApiColumnCount(), fts5ApiColumnTotalSize(), fts5ApiGetAuxdata(), fts5ApiInst(), fts5ApiInstCount(), fts5ApiInstToken(), fts5ApiPhraseCount(), fts5ApiPhraseFirst() (+32 more)

### Community 19 - "bearssl_block.h"
Cohesion: 0.04
Nodes (93): br_aes_big_cbcdec_keys, br_aes_ct64_cbcdec_keys, br_aes_ct64_cbcenc_keys, br_aes_ct64_ctr_keys, br_aes_ct64_ctrcbc_keys, br_aes_ct_cbcdec_keys, br_aes_ct_cbcenc_keys, br_aes_ct_ctr_keys (+85 more)

### Community 20 - "sqlite3_initialize"
Cohesion: 0.07
Nodes (45): orx_vfs_install(), recoverEnterMutex(), recoverLeaveMutex(), sqlite3_appendvfs_init(), localtime(), memsys3Enter(), multiThreadedCheckMutex(), osLocaltime() (+37 more)

### Community 21 - "do_meta_command"
Cohesion: 0.03
Nodes (184): anyOfInStr(), appendText(), arCheckEntries(), arCreateOrUpdateCommand(), arDotCommand(), arErrorMsg(), arExecSql(), arExtractCommand() (+176 more)

### Community 22 - "mbedtls_mpi_uint"
Cohesion: 0.05
Nodes (98): mbedtls_mpi_uint, exp_mod_calc_first_bit_optionally_safe(), exp_mod_get_window_size(), exp_mod_precompute_window(), exp_mod_table_lookup_optionally_safe(), mbedtls_mpi_core_add(), mbedtls_mpi_core_add_if(), mbedtls_mpi_core_bigendian_to_host() (+90 more)

### Community 23 - "Expr"
Cohesion: 0.03
Nodes (164): agginfoPersistExprCb(), aggregateIdxEprRefToColCallback(), analyzeAggregate(), binaryCompareP5(), bothImplyNotNullRow(), checkConstraintExprNode(), codeCursorHint(), codeCursorHintCheckExpr() (+156 more)

### Community 24 - "sqlite3_column_bytes"
Cohesion: 0.04
Nodes (91): exprToString(), findBarredChar(), fts3AllocateMSI(), fts3BestSnippet(), fts3ClearCursor(), fts3CloseMethod(), fts3ColumnlistCount(), fts3CursorFinalizeStmt() (+83 more)

### Community 25 - "memsys3MallocUnsafe"
Cohesion: 0.24
Nodes (14): memsys3Checkout(), memsys3Free(), memsys3FreeUnsafe(), memsys3FromKeyBlk(), memsys3Leave(), memsys3Link(), memsys3LinkIntoList(), memsys3Malloc() (+6 more)

### Community 26 - "cipher.c"
Cohesion: 0.03
Nodes (148): mbedtls_block_cipher_context_t, mbedtls_ccm_context, MBEDTLS_PRIVATE, mbedtls_cipher_base_t, mbedtls_cipher_context_t, mbedtls_cipher_get_block_size(), mbedtls_cipher_get_cipher_mode(), mbedtls_cipher_get_iv_size() (+140 more)

### Community 27 - "jsonParseFuncArg"
Cohesion: 0.08
Nodes (64): chacha_block(), json5Whitespace(), jsonAfterEditSizeAdjust(), jsonAppendPathName(), jsonArgIsJsonb(), jsonArrayLengthFunc(), jsonBadPathError(), jsonbArrayCount() (+56 more)

### Community 28 - "des.c"
Cohesion: 0.10
Nodes (39): mbedtls_des3_context, MBEDTLS_PRIVATE, mbedtls_des_context, MBEDTLS_PRIVATE, des3_crypt_cbc_wrap(), des3_crypt_ecb_wrap(), des3_ctx_alloc(), des3_ctx_free() (+31 more)

### Community 29 - "mbedtls_ssl_context"
Cohesion: 0.06
Nodes (102): mbedtls_ssl_context, mbedtls_ssl_is_handshake_over(), mbedtls_ssl_ep_len(), mbedtls_ssl_get_psk(), mbedtls_ssl_in_hdr_len(), mbedtls_ssl_out_hdr_len(), mbedtls_ssl_buffering_free(), mbedtls_ssl_check_dtls_clihlo_cookie() (+94 more)

### Community 30 - "br_ecdsa_i15_sign_raw"
Cohesion: 0.04
Nodes (76): api_generator(), api_mul(), api_mulgen(), byteswap(), c255_add(), c255_mul(), c255_sub(), cswap() (+68 more)

### Community 31 - "Fts5SegIter"
Cohesion: 0.08
Nodes (69): fts5BufferCompareBlob(), fts5ChunkIterate(), fts5DataRead(), fts5DataRelease(), fts5DlidxIterEof(), fts5DlidxIterFirst(), fts5DlidxIterFree(), fts5DlidxIterInit() (+61 more)

### Community 32 - "unixFile"
Cohesion: 0.05
Nodes (81): afpClose(), afpUnlock(), assertLockingArrayOk(), autolockIoFinderImpl(), closePendingFds(), closeUnixFile(), fcntlSizeHint(), fileHasMoved() (+73 more)

### Community 33 - "sqlite3_vfs"
Cohesion: 0.04
Nodes (57): orx_fullpath(), apndAccess(), apndCurrentTime(), apndDelete(), apndDlClose(), apndDlError(), apndDlOpen(), apndFullPathname() (+49 more)

### Community 34 - "sqlite3_file"
Cohesion: 0.03
Nodes (104): apndCheckReservedLock(), apndClose(), apndDeviceCharacteristics(), apndLock(), apndSectorSize(), apndShmBarrier(), apndShmLock(), apndShmMap() (+96 more)

### Community 35 - "ssl_tls13_server.c"
Cohesion: 0.06
Nodes (90): mbedtls_ssl_named_group_to_str(), mbedtls_ssl_conf_is_tls12_enabled(), mbedtls_ssl_tls13_key_exchange_mode_check(), mbedtls_ssl_tls13_key_exchange_mode_with_ephemeral(), mbedtls_ssl_tls13_key_exchange_mode_with_psk(), mbedtls_ssl_tls13_set_hs_sent_ext_mask(), mbedtls_ssl_finish_handshake_msg(), mbedtls_ssl_start_handshake_msg() (+82 more)

### Community 36 - "sqlite3_value"
Cohesion: 0.05
Nodes (73): dummyUDF(), ieee754func_to_blob(), ieee754inc(), shellDtostr(), shellStrtod(), absFunc(), ceilingFunc(), concatFunc() (+65 more)

### Community 37 - "Btree"
Cohesion: 0.07
Nodes (83): attachBackupObject(), backupOnePage(), backupTruncateFile(), backupUpdate(), btreeBeginTrans(), btreeClearHasContent(), btreeEndTransaction(), btreeLockCarefully() (+75 more)

### Community 38 - "ssl_tls12_server.c"
Cohesion: 0.05
Nodes (93): mbedtls_ssl_ciphersuite_get_id(), mbedtls_ssl_ciphersuite_get_name(), mbedtls_ssl_ciphersuite_t, mbedtls_ct_memcmp(), ciphersuite_is_removed(), mbedtls_ssl_ciphersuite_cert_req_allowed(), mbedtls_ssl_ciphersuite_has_pfs(), mbedtls_ssl_ciphersuite_no_pfs() (+85 more)

### Community 39 - "mbedtls_mpi"
Cohesion: 0.08
Nodes (91): mbedtls_mpi, mbedtls_mpi_sint, add_sub_mpi(), mbedtls_mpi_core_get_mont_r2_unsafe(), mbedtls_ct_mpi_sign_if(), mbedtls_mpi_add_abs(), mbedtls_mpi_add_int(), mbedtls_mpi_add_mpi() (+83 more)

### Community 40 - "crypto_extra.h"
Cohesion: 0.05
Nodes (79): mbedtls_psa_stats_s, mbedtls_psa_stats_t, psa_clear_key_slot_number(), psa_crypto_driver_pake_inputs_s, psa_crypto_driver_pake_inputs_t, psa_crypto_driver_pake_step_t, psa_drv_slot_number_t, psa_get_key_enrollment_algorithm() (+71 more)

### Community 41 - "stdio"
Cohesion: 0.04
Nodes (47): net_probe(), net_init(), send_file_loopback(), send_one(), sock_connect_deadline(), valid_tid(), art_cache_clear(), art_cache_put() (+39 more)

### Community 42 - "sqlite3_column_text"
Cohesion: 0.06
Nodes (131): appdb_title_from(), query_name(), table_exists(), test_appdb(), completionFilter(), completionNext(), db_int(), dbdataDbsize() (+123 more)

### Community 43 - "sqlite3_stricmp"
Cohesion: 0.05
Nodes (90): zipfileFindFunction(), fts3ShadowName(), fts5FindAuxiliary(), fts5FindFunctionMethod(), fts5ShadowName(), geopolyFindFunction(), rbuStrCompare(), rtreeShadowName() (+82 more)

### Community 44 - "test_utils.c"
Cohesion: 0.04
Nodes (100): Iteration 3 — 2026-10-28 — scheduling, auth, presence, Already done (host-tested), 1.1 verification matrix, Defects, Evidence levels, Feature requests, Fourth audit pass (2026-11-02, host only), Gate record (latest run) (+92 more)

### Community 46 - "x509write_crt.c"
Cohesion: 0.06
Nodes (81): mbedtls_asn1_named_data, next, oid, val, mbedtls_x509write_cert, mbedtls_x509write_csr, mbedtls_x509_san_list, next (+73 more)

### Community 47 - "health.c"
Cohesion: 0.11
Nodes (32): Updates and rollback, Iteration 2 — 2026-10-28 — data safety and recovery, clear_path(), crash_path(), dirty_path(), health_boot_note_crash(), health_check_binary(), health_mark_clean() (+24 more)

### Community 48 - "Fts5Expr"
Cohesion: 0.14
Nodes (31): fts5ExprAdvanceto(), fts5ExprCheckPoslists(), fts5ExprClearPoslists(), fts5ExprNearInitAll(), fts5ExprNearTest(), fts5ExprNodeFirst(), fts5ExprNodeNext_AND(), fts5ExprNodeNext_NOT() (+23 more)

### Community 49 - "ssl.h"
Cohesion: 0.03
Nodes (75): mbedtls_dtls_srtp_info, mbedtls_dtls_srtp_info_t, mbedtls_ssl_async_cancel_t, mbedtls_ssl_async_decrypt_t, mbedtls_ssl_async_resume_t, mbedtls_ssl_async_sign_t, mbedtls_ssl_cache_get_t, mbedtls_ssl_cache_set_t (+67 more)

### Community 50 - "aes.c"
Cohesion: 0.13
Nodes (33): mbedtls_aes_context, mbedtls_aes_xts_context, aes_gen_tables(), aes_maybe_realign(), mbedtls_aes_crypt_cbc(), mbedtls_aes_crypt_cfb128(), mbedtls_aes_crypt_cfb8(), mbedtls_aes_crypt_ctr() (+25 more)

### Community 51 - "Foreground detection: from heuristics to asking the OS"
Cohesion: 0.22
Nodes (8): Cost of the change, Foreground detection: from heuristics to asking the OS, Still true: offsets are not self-describing, Superseded, The distinction that carries the design, Unproven, What replaced it, What was removed

### Community 52 - "ssl_misc.h"
Cohesion: 0.05
Nodes (54): mbedtls_ssl_write_client_hello(), ssl_generate_random(), ssl_prepare_client_hello(), ssl_write_alpn_ext(), ssl_write_client_hello_body(), ssl_write_client_hello_cipher_suites(), ssl_write_hostname_ext(), ssl_write_supported_groups_ext() (+46 more)

### Community 53 - "ssl_tls13_client.c"
Cohesion: 0.08
Nodes (71): mbedtls_ssl_conf_tls13_is_some_ephemeral_enabled(), mbedtls_ssl_conf_tls13_is_some_psk_enabled(), mbedtls_ssl_get_groups(), mbedtls_ssl_handshake_set_state(), mbedtls_ssl_named_group_is_offered(), mbedtls_ssl_named_group_is_supported(), mbedtls_ssl_tls13_cipher_suite_is_offered(), mbedtls_ssl_tls13_named_group_is_ecdhe() (+63 more)

### Community 54 - "sqlite3_free"
Cohesion: 0.01
Nodes (301): completionBestIndex(), completionClose(), completionConnect(), completionCursorReset(), completionDisconnect(), completionEof(), completionOpen(), dbdataBestIndex() (+293 more)

### Community 55 - "Rtree"
Cohesion: 0.09
Nodes (67): AdjustTree(), cellArea(), cellContains(), cellMargin(), cellOverlap(), cellUnion(), ChooseLeaf(), deleteCell() (+59 more)

### Community 56 - "br_enc64be"
Cohesion: 0.07
Nodes (54): br_ccm_context, br_gcm_context, br_block_ctr_class, block_size, context_size, log_block_size, br_block_ctrcbc_class, block_size (+46 more)

### Community 57 - "shell.c"
Cohesion: 0.02
Nodes (192): add64(), allBase85(), apndCurrentTimeInt64(), apndFetch(), apndFileControl(), apndFileSize(), apndIsAppendvfsDatabase(), apndIsOrdinaryDatabaseFile() (+184 more)

### Community 58 - "bearssl_x509.h"
Cohesion: 0.06
Nodes (61): br_multihash_setimpl(), br_name_element, br_skey_decoder_context, br_skey_decoder_get_ec(), br_skey_decoder_get_rsa(), br_skey_decoder_key_type(), br_skey_decoder_last_error(), br_x500_name (+53 more)

### Community 59 - "jl_free"
Cohesion: 0.09
Nodes (72): Never break, Iteration 13 — 2026-11-02 — idle clear, JSON UTF-8, TMDB trust, Iteration 5 — 2026-10-28 — edge cases found after the parser fix, Iteration 6 — 2026-10-29 — verification integrity and upgrade safety, Follow-up audit fixes (2026-11-01, host only), icfg_get_int(), icfg_get_str(), icfg_read() (+64 more)

### Community 60 - "net_sockets.c"
Cohesion: 0.06
Nodes (47): boot_wall(), holder_stale(), lock_acquire(), lock_entry_reclaim(), lock_entry_stale(), lock_heartbeat_stale(), lock_owned(), lock_refresh() (+39 more)

### Community 61 - "ec_c25519_m62.c"
Cohesion: 0.06
Nodes (45): br_chacha20_run, br_poly1305_run, br_shake_context, br_sslrec_chapol_context, br_range_dec64le(), br_range_enc64le(), api_generator(), api_mul() (+37 more)

### Community 62 - "sqlite3DbFree"
Cohesion: 0.05
Nodes (66): attachWithToSelect(), callCollNeeded(), closeAllCursors(), closeCursorsInFrame(), cteClear(), execSqlF(), freeEphemeralFunction(), freeP4() (+58 more)

### Community 63 - "fts5yy_reduce"
Cohesion: 0.08
Nodes (50): assert_expr_depth_ok(), fts5CloneColset(), fts5ExprAssignXNext(), fts5ExprColsetTest(), fts5ExprGetToken(), fts5ExprIsspace(), fts5ExprPhraseFree(), fts5MergeColset() (+42 more)

### Community 64 - "sqlite3_recover"
Cohesion: 0.10
Nodes (60): fiddle_db_vfs(), fiddle_export_db(), recoverAddTable(), recoverAssertMutexHeld(), recoverBindValue(), recoverBitmapAlloc(), recoverBitmapFree(), recoverBitmapQuery() (+52 more)

### Community 65 - "installer.c"
Cohesion: 0.08
Nodes (27): Iteration 14 — 2026-11-02 — installer safety, gateway input, idle home, Console facts encoded, copy_file(), crash_guard(), crash_handler(), exists(), finish(), fnv1a() (+19 more)

### Community 66 - "sqlite3_result_text"
Cohesion: 0.10
Nodes (55): shellModuleSchema(), zipfileFunctionCds(), currentTimeFunc(), fts5SourceIdFunc(), intTestFunc(), jsonAllAlphanum(), jsonAppendChar(), jsonAppendCharExpand() (+47 more)

### Community 67 - "sqlite3_vmprintf"
Cohesion: 0.15
Nodes (22): fileLinkStat(), fileStat(), fsdirClose(), fsdirFilter(), fsdirNext(), fsdirResetCursor(), fsdirSetErrmsg(), iotracePrintf() (+14 more)

### Community 68 - "br_hmac_drbg_generate"
Cohesion: 0.08
Nodes (51): br_hmac_context, br_hmac_get_digest(), br_hmac_key_context, br_hmac_key_get_digest(), br_hmac_size(), br_hkdf_context, br_tls_prf_seed_chunk, br_hmac_drbg_context (+43 more)

### Community 69 - "BtCursor"
Cohesion: 0.10
Nodes (57): accessPayload(), accessPayloadChecked(), anotherValidCursor(), assertParentIndex(), btreeCursor(), btreeCursorWithLock(), btreeLast(), btreeMoveto() (+49 more)

### Community 70 - "lmots.c"
Cohesion: 0.12
Nodes (43): mbedtls_lmots_algorithm_type_t, mbedtls_lmots_parameters_t, mbedtls_lmots_private_t, mbedtls_lmots_public_t, mbedtls_lms_algorithm_type_t, mbedtls_lms_parameters_t, psa_hash_operation_t, psa_hash_operation_init() (+35 more)

### Community 71 - "mbedtls_asn1_get_tag"
Cohesion: 0.09
Nodes (50): mbedtls_asn1_buf, len, p, tag, mbedtls_asn1_sequence, buf, next, mbedtls_pem_context (+42 more)

### Community 72 - "SQLITE_API"
Cohesion: 0.03
Nodes (116): blobReadWrite(), checkActiveVdbeCnt(), columnName(), databaseName(), sessionChangesetApply(), sessionChangesetApplyV23(), sessionChangesetNext(), sessionChangesetStart() (+108 more)

### Community 73 - "mbedtls_md_type_t"
Cohesion: 0.09
Nodes (60): mbedtls_md_get_size_from_type(), mbedtls_md_type_t, mbedtls_pk_rsassa_pss_options, expected_salt_len, mgf1_hash_id, mbedtls_rsa_context, mbedtls_asn1_write_mpi(), mbedtls_mpi_size() (+52 more)

### Community 74 - "Daemon design (v1.1.0)"
Cohesion: 0.17
Nodes (12): Art, Daemon design (v1.1.0), Detection, Diagnostics, Fingerprint, Logging, Loop, Names (first hit wins; one small built-in table) (+4 more)

### Community 75 - "chachapoly.c"
Cohesion: 0.17
Nodes (28): mbedtls_chachapoly_context, mbedtls_chachapoly_mode_t, mbedtls_poly1305_context, MBEDTLS_PRIVATE, chachapoly_crypt_and_tag(), chachapoly_pad_aad(), chachapoly_pad_ciphertext(), mbedtls_chachapoly_auth_decrypt() (+20 more)

### Community 76 - "sqlite3"
Cohesion: 0.05
Nodes (142): fiddle_db_filename(), fiddle_db_handle(), fiddle_interrupt(), useDummyCS(), zipfileRegister(), addToBlockedList(), apiHandleError(), btreeEnterAll() (+134 more)

### Community 77 - "md.c"
Cohesion: 0.15
Nodes (47): mbedtls_md_context_t, entropy_gather_internal(), entropy_update(), mbedtls_entropy_func(), mbedtls_hkdf_expand(), mbedtls_md(), mbedtls_md_error_from_psa(), mbedtls_md_file() (+39 more)

### Community 78 - "u8"
Cohesion: 0.06
Nodes (71): fts5BufferAppendTerm(), fts5BufferSwap(), fts5DataWrite(), fts5DebugRowid(), fts5DebugStructure(), fts5DecodeAverages(), fts5DecodeDoclist(), fts5DecodeFunction() (+63 more)

### Community 79 - "mbedtls_ssl_session"
Cohesion: 0.11
Nodes (30): mbedtls_ms_time_t, mbedtls_ssl_cache_context, mbedtls_ssl_cache_entry, MBEDTLS_PRIVATE, mbedtls_ssl_cache_get_timeout(), mbedtls_ssl_session, mbedtls_ssl_session_get_ticket_creation_time(), mbedtls_ms_time() (+22 more)

### Community 80 - "Fts3Hash"
Cohesion: 0.30
Nodes (12): fts3FindElementByHash(), fts3HashFree(), fts3HashInsertElement(), fts3HashMalloc(), fts3Rehash(), fts3RemoveElementByHash(), fts3tokQueryTokenizer(), hashDestroy() (+4 more)

### Community 81 - "u8"
Cohesion: 0.11
Nodes (64): allocateBtreePage(), allocateSpace(), autoVacuumCommit(), balance(), balance_deeper(), balance_nonroot(), balance_quick(), btreeCellSizeCheck() (+56 more)

### Community 82 - "sqlite3_create_module"
Cohesion: 0.06
Nodes (39): fsdirRegister(), sqlite3CompletionVtabInit(), sqlite3DbdataRegister(), fts5_api, iVersion, fts5_tokenizer, fts5_tokenizer_v2, iVersion (+31 more)

### Community 83 - "fts5UnicodeAddExceptions"
Cohesion: 0.47
Nodes (5): fts5UnicodeAddExceptions(), fts5UnicodeIsAlnum(), fts5UnicodeIsException(), sqlite3Fts5UnicodeCategory(), sqlite3Fts5UnicodeIsdiacritic()

### Community 84 - "psa_crypto_random_impl.h"
Cohesion: 0.13
Nodes (16): psa_driver_wrapper_free(), mbedtls_psa_crypto_free(), mbedtls_psa_crypto_init_subsystem(), mbedtls_psa_inject_entropy(), mbedtls_psa_random_free(), mbedtls_psa_random_init(), mbedtls_psa_random_seed(), psa_crypto_init() (+8 more)

### Community 85 - "psa_crypto_slot_management.c"
Cohesion: 0.03
Nodes (75): psa_key_handle_is_null(), psa_key_handle_t, psa_key_id_is_builtin(), mbedtls_key_owner_id_equal(), psa_aead_operation_s, psa_cipher_operation_s, psa_hash_operation_s, psa_key_derivation_s (+67 more)

### Community 86 - "ssl_tls12_client.c"
Cohesion: 0.14
Nodes (38): mbedtls_ecjpake_check(), mbedtls_pk_can_do(), mbedtls_pk_free(), mbedtls_ssl_send_alert_message(), if(), mbedtls_ssl_tls12_write_client_hello_exts(), ssl_check_server_ecdh_params(), ssl_get_ecdh_params_from_cert() (+30 more)

### Community 87 - "pemdec.c"
Cohesion: 0.33
Nodes (9): br_pem_decoder_context, br_pem_decoder_name(), br_pem_decoder_setdest(), br_pem_decoder_event(), br_pem_decoder_init(), br_pem_decoder_push(), br_pem_decoder_run(), t0_parse7E_signed() (+1 more)

### Community 88 - "mbedtls_platform_zeroize"
Cohesion: 0.14
Nodes (30): mbedtls_md_psa_alg_from_type(), mbedtls_pem_write_buffer(), mbedtls_pk_write_pubkey_pem(), mbedtls_calloc(), mbedtls_ssl_key_set, client_write_iv, client_write_key, iv_len (+22 more)

### Community 89 - "discord.c"
Cohesion: 0.06
Nodes (71): Current gate status (2026-11-02, host), Gate commands, Improvement log, Iteration 10 — 2026-10-29 — desktop logout was self-inflicted token rotation, Iteration 11 — 2026-11-01 — post-list audit: updater staging, socket close, art trust, Iteration 15 — 2026-11-02 — notification spam, clear retry, drop visibility, Iteration 16 — 2026-11-02 — bounded connect on every network path, Iteration 18 — 2026-11-02 — GPL-3.0-or-later (+63 more)

### Community 90 - "DateTime"
Cohesion: 0.12
Nodes (38): autoAdjustDate(), cdateFunc(), clearYMD_HMS_TZ(), computeFloor(), computeHMS(), computeJD(), computeYMD(), computeYMD_HMS() (+30 more)

### Community 91 - "br_eax_context"
Cohesion: 0.27
Nodes (20): br_eax_context, br_eax_get_aad_mac(), br_eax_state, br_eax_aad_inject(), br_eax_capture(), br_eax_check_tag(), br_eax_check_tag_trunc(), br_eax_flip() (+12 more)

### Community 92 - "sha2small.c"
Cohesion: 0.10
Nodes (35): br_md5_context, br_md5sha1_context, br_sha1_context, br_sha224_context, br_sha256_context, br_range_dec32be(), br_range_enc32be(), br_md5_init() (+27 more)

### Community 93 - "cipher_wrap.c"
Cohesion: 0.06
Nodes (34): mbedtls_operation_t, aes_crypt_cbc_wrap(), aes_crypt_cfb128_wrap(), aes_crypt_ecb_wrap(), aes_crypt_xts_wrap(), aes_ctx_alloc(), aes_ctx_free(), aes_setkey_dec_wrap() (+26 more)

### Community 94 - "constant_time_impl.h"
Cohesion: 0.20
Nodes (33): mbedtls_mpi_lt_mpi_ct(), get_one_and_zeros_padding(), get_zeros_and_len_padding(), get_zeros_padding(), mbedtls_get_pkcs_padding(), mbedtls_ct_bool(), mbedtls_ct_bool_and(), mbedtls_ct_bool_if() (+25 more)

### Community 95 - "sqlite3_value_int"
Cohesion: 0.12
Nodes (36): intckParseCreateIndexFunc(), lsModeFunc(), re_bytecode_func(), shellFkeyCollateClause(), compileoptiongetFunc(), dropColumnFunc(), errlogFunc(), filestatFunc() (+28 more)

### Community 96 - "crypto_se_driver.h"
Cohesion: 0.04
Nodes (92): psa_drv_se_aead_decrypt_t, psa_drv_se_aead_encrypt_t, psa_drv_se_aead_t, psa_drv_se_allocate_key_t, psa_drv_se_asymmetric_decrypt_t, psa_drv_se_asymmetric_encrypt_t, psa_drv_se_asymmetric_sign_t, psa_drv_se_asymmetric_t (+84 more)

### Community 97 - "sqlite3Malloc"
Cohesion: 0.06
Nodes (63): sqlite3GetVarint(), sqlite3OsClose(), sqlite3OsCloseFree(), sqlite3OsCurrentTimeInt64(), sqlite3OsRead(), sqlite3OsUnfetch(), sqlite3OsWrite(), sqlite3VarintLen() (+55 more)

### Community 98 - "mbedtls_psa_hash_setup"
Cohesion: 0.18
Nodes (26): P, mbedtls_md5_context, MBEDTLS_PRIVATE, mbedtls_sha1_context, MBEDTLS_PRIVATE, mbedtls_internal_md5_process(), mbedtls_md5(), mbedtls_md5_finish() (+18 more)

### Community 99 - "ec_p256_m31.c"
Cohesion: 0.23
Nodes (22): add_f256(), api_mul(), api_muladd(), api_mulgen(), be8_to_le30(), le30_to_be8(), lookup_Gwin(), mul9() (+14 more)

### Community 100 - "ec_c25519_m15.c"
Cohesion: 0.10
Nodes (28): api_generator(), api_mul(), api_mulgen(), cswap(), f255_add(), f255_mul_a24(), f255_mulgen(), f255_sub() (+20 more)

### Community 101 - "sqlite3BtreeIntegrityCheck"
Cohesion: 0.37
Nodes (13): btreeHeapInsert(), btreeHeapPull(), checkAppendMsg(), checkList(), checkOom(), checkProgress(), checkPtrmap(), checkRef() (+5 more)

### Community 102 - "mbedtls_x509_crt"
Cohesion: 0.06
Nodes (33): mbedtls_x509_crt, authority_key_id, certificate_policies, ext_key_usage, mbedtls_x509_crt_has_ext_type(), issuer, issuer_id, issuer_raw (+25 more)

### Community 103 - "aes_x86ni.c"
Cohesion: 0.03
Nodes (72): br_aes_pwr8_cbcdec_keys, br_aes_pwr8_cbcenc_keys, br_aes_pwr8_ctr_keys, br_aes_pwr8_ctrcbc_keys, br_aes_x86ni_cbcdec_keys, br_aes_x86ni_cbcenc_keys, br_aes_x86ni_ctr_keys, br_aes_x86ni_ctrcbc_keys (+64 more)

### Community 104 - "des_ct.c"
Cohesion: 0.10
Nodes (28): br_des_ct_cbcdec_keys, br_des_ct_cbcenc_keys, br_des_tab_cbcdec_keys, br_des_tab_cbcenc_keys, br_des_ct_keysched(), br_des_ct_process_block(), br_des_ct_skey_expand(), br_des_ct_cbcdec_init() (+20 more)

### Community 105 - "mbedtls_mpi_free"
Cohesion: 0.06
Nodes (90): manifest_verify_sig(), test_manifest_sig(), mbedtls_ecdsa_context, mbedtls_ecdsa_restart_ctx, mbedtls_ecdsa_restart_det, mbedtls_ecdsa_restart_det_ctx, mbedtls_ecdsa_restart_sig, mbedtls_ecdsa_restart_sig_ctx (+82 more)

### Community 106 - "OrbisRPC Migration Progress Log"
Cohesion: 0.06
Nodes (32): appdb_title(), Attempted approaches, Build command (run in WSL), Build error and fix (2026-09-29), Caveats to test, Change to `scripts/build_sdk.sh`, Changes made, Commands to run in WSL (+24 more)

### Community 107 - "platform.c"
Cohesion: 0.06
Nodes (9): mbedtls_platform_context, mbedtls_mps_trace_indent(), mbedtls_platform_setup(), mbedtls_platform_std_nv_seed_read(), mbedtls_platform_teardown(), mbedtls_platform_win32_vsnprintf(), platform_fprintf_uninit(), platform_setbuf_uninit() (+1 more)

### Community 108 - "ecdh.c"
Cohesion: 0.11
Nodes (36): mbedtls_ecdh_context, mbedtls_ecdh_context_mbed, mbedtls_ecdh_side, mbedtls_ecdh_variant, mbedtls_ecp_curve_info, bit_size, grp_id, name (+28 more)

### Community 109 - "with"
Cohesion: 0.08
Nodes (30): concurrency, cancel-in-progress, group, COMMIT_MSG, DISCORD_WEBHOOK_URL, ORBISRPC_STRICT_SECRETS, runs-on, steps (+22 more)

### Community 110 - "psa_verify_hash_interruptible_operation_t"
Cohesion: 0.23
Nodes (12): psa_verify_hash_interruptible_operation_t, psa_driver_wrapper_verify_hash_abort(), psa_driver_wrapper_verify_hash_complete(), psa_driver_wrapper_verify_hash_get_num_ops(), mbedtls_psa_interruptible_set_max_ops(), mbedtls_psa_verify_hash_complete(), mbedtls_psa_verify_hash_get_num_ops(), psa_interruptible_get_max_ops() (+4 more)

### Community 111 - "x509.c"
Cohesion: 0.12
Nodes (32): mbedtls_pk_get_len(), mbedtls_x509_dn_get_next(), mbedtls_x509_name, mbedtls_x509_subject_alternative_name, san, type, mbedtls_oid_get_numeric_string(), mbedtls_pk_get_bitlen() (+24 more)

### Community 112 - "mbedtls_x509_csr"
Cohesion: 0.11
Nodes (28): mbedtls_asn1_bitstring, len, p, unused_bits, mbedtls_x509_csr, cri, mbedtls_x509_csr_ext_cb_t, key_usage (+20 more)

### Community 113 - "mbedtls_x509_crl"
Cohesion: 0.09
Nodes (27): mbedtls_x509_crl, crl_ext, mbedtls_x509_crl_entry, entry_ext, next, raw, revocation_date, serial (+19 more)

### Community 114 - "test_ws.c"
Cohesion: 0.20
Nodes (27): log_close(), ws_close(), ws_connect(), ws_pong(), ws_recv_alloc(), ws_recv_frame(), begin(), end() (+19 more)

### Community 115 - "ec_p256_m62.c"
Cohesion: 0.23
Nodes (24): api_mul(), api_muladd(), api_mulgen(), f256_add(), f256_decode(), f256_encode(), f256_final_reduce(), f256_frommonty() (+16 more)

### Community 116 - "ctr_drbg.c"
Cohesion: 0.20
Nodes (24): mbedtls_ctr_drbg_context, MBEDTLS_PRIVATE, psa_cipher_operation_init(), block_cipher_df(), ctr_drbg_destroy_psa_contex(), ctr_drbg_update_internal(), good_nonce_len(), mbedtls_ctr_drbg_free() (+16 more)

### Community 117 - "psa_crypto_mac.c"
Cohesion: 0.12
Nodes (17): mac_init(), mbedtls_psa_mac_abort(), mbedtls_psa_mac_compute(), mbedtls_psa_mac_sign_finish(), mbedtls_psa_mac_update(), mbedtls_psa_mac_verify_finish(), psa_hmac_abort_internal(), psa_hmac_finish_internal() (+9 more)

### Community 118 - "ec_p256_m64.c"
Cohesion: 0.26
Nodes (21): api_mul(), api_muladd(), api_mulgen(), f256_add(), f256_final_reduce(), f256_frommonty(), f256_invert(), f256_montymul() (+13 more)

### Community 119 - "aesce.c"
Cohesion: 0.14
Nodes (17): aes_rot_word(), aes_sub_word(), aesce_decrypt_block(), aesce_encrypt_block(), aesce_setkey_enc(), mbedtls_aesce_crypt_ecb(), mbedtls_aesce_gcm_mult(), mbedtls_aesce_inverse_key() (+9 more)

### Community 120 - "gamecache.c"
Cohesion: 0.18
Nodes (20): Iteration 8 — 2026-10-29 — updater audit, replay, diagnostics, offline cache, art_close(), append(), diag_build(), diag_write_file(), fnv1a(), scrub_exact(), copy_field() (+12 more)

### Community 121 - "sha256.c"
Cohesion: 0.22
Nodes (20): manifest_sha256_hex(), mbedtls_sha256_context, MBEDTLS_PRIVATE, mbedtls_a64_crypto_sha256_determine_support(), mbedtls_a64_crypto_sha256_has_support(), mbedtls_internal_sha256_process(), mbedtls_internal_sha256_process_a64_crypto(), mbedtls_internal_sha256_process_c() (+12 more)

### Community 122 - "update_fingerprint.py"
Cohesion: 0.13
Nodes (15): header_in_sync(), load(), main(), TestUpdate, age_days(), fetch_build_number(), fetch_doc(), fetch_electron_chrome() (+7 more)

### Community 123 - "sha512.c"
Cohesion: 0.20
Nodes (24): mbedtls_sha512_context, MBEDTLS_PRIVATE, mbedtls_a64_crypto_sha512_determine_support(), mbedtls_a64_crypto_sha512_has_support(), mbedtls_internal_sha512_process(), mbedtls_internal_sha512_process_a64_crypto(), mbedtls_internal_sha512_process_c(), mbedtls_internal_sha512_process_many() (+16 more)

### Community 125 - "dhm.c"
Cohesion: 0.17
Nodes (23): mbedtls_dhm_context, mbedtls_dhm_parameter, psa_pake_operation_init(), dhm_check_range(), dhm_make_common(), dhm_read_bignum(), load_file(), mbedtls_dhm_calc_secret() (+15 more)

### Community 126 - "memory_buffer_alloc.c"
Cohesion: 0.14
Nodes (15): buffer_alloc_calloc(), buffer_alloc_calloc_mutexed(), buffer_alloc_free(), buffer_alloc_free_mutexed(), check_pointer(), debug_chain(), debug_header(), mbedtls_memory_buffer_alloc_free() (+7 more)

### Community 127 - "steps"
Cohesion: 0.08
Nodes (23): GH_TOKEN, jobs, refresh, name, on, schedule, workflow_dispatch, permissions (+15 more)

### Community 128 - "with"
Cohesion: 0.08
Nodes (23): permissions, runs-on, steps, concurrency, cancel-in-progress, group, jobs, build-and-release (+15 more)

### Community 129 - "worker.js"
Cohesion: 0.16
Nodes (10): allowedVerificationUrl(), AuthSession, error(), fetch(), json(), keyOf(), positiveSeconds(), randomSecret() (+2 more)

### Community 130 - "decimal_new"
Cohesion: 0.22
Nodes (22): decimal_add(), decimal_clear(), decimal_cmp(), decimal_expand(), decimal_free(), decimal_new(), decimal_result(), decimal_result_sci() (+14 more)

### Community 131 - "MIN"
Cohesion: 0.16
Nodes (16): fts5ExprNearIsMatch(), fts5LookaheadReaderInit(), fts5LookaheadReaderNext(), fts5VocabCloseMethod(), fts5VocabFilterMethod(), fts5VocabInstanceNewTerm(), fts5VocabInstanceNext(), fts5VocabNextMethod() (+8 more)

### Community 132 - "i64"
Cohesion: 0.05
Nodes (102): fts5ApiColumnLocale(), fts5ApiColumnSize(), fts5ApiColumnText(), fts5ApiQueryPhrase(), fts5ApiRowCount(), fts5BeginMethod(), fts5CheckTransactionState(), fts5CommitMethod() (+94 more)

### Community 133 - "sqlite3_rtree_query_info"
Cohesion: 0.09
Nodes (21): sqlite3_rtree_geometry, aParam, nParam, pContext, pUser, sqlite3_rtree_query_info, aCoord, anQueue (+13 more)

### Community 134 - "ec_p256_m15.c"
Cohesion: 0.26
Nodes (21): api_mul(), api_muladd(), api_mulgen(), be8_to_le13(), le13_to_be8(), lookup_Gwin(), mul20(), mul_f256() (+13 more)

### Community 135 - "u64"
Cohesion: 0.09
Nodes (29): fts3ChecksumEntry(), fts5HashAddPoslistSize(), fts5HashCount(), fts5HashEntryMerge(), fts5HashEntrySort(), fts5HashKey(), fts5HashKey2(), fts5HashResize() (+21 more)

### Community 136 - "sqlite3_intck"
Cohesion: 0.13
Nodes (40): intckCheckObjectSql(), intckDatabaseCmd(), intckExec(), intckFinalize(), intckFindObject(), intckGetAutoIndex(), intckIsIndex(), intckMprintf() (+32 more)

### Community 137 - "br_rsa_private_key"
Cohesion: 0.05
Nodes (98): br_hash_class, context_size, desc, br_prng_class, context_size, br_rsa_private_key, br_rsa_public_key, api_generator() (+90 more)

### Community 138 - "sqlite3Malloc"
Cohesion: 0.09
Nodes (43): adjustStats(), checkListProperties(), corruptPageError(), dbReallocFinish(), enterMutex(), isLookaside(), leaveMutex(), lookasideMallocSize() (+35 more)

### Community 139 - "x509_crt.h"
Cohesion: 0.16
Nodes (25): mbedtls_x509_crt_ca_cb_t, mbedtls_x509_crt_profile, allowed_curves, allowed_mds, allowed_pks, rsa_min_bitlen, mbedtls_x509_crt_restart_ctx, mbedtls_x509_crt_verify_chain (+17 more)

### Community 140 - "os"
Cohesion: 0.15
Nodes (3): c_escape(), main(), resolve()

### Community 141 - "orbisRPC — Discord Rich Presence for PS4 (GoldHEN RPC)"
Cohesion: 0.11
Nodes (19): 1.2 OAuth beta, Attribution and acknowledgment, Building, Config, Contents, Credits, Developing, Earlier test-build fixes (+11 more)

### Community 142 - "library/hmac_drbg.c"
Cohesion: 0.20
Nodes (19): mbedtls_hmac_drbg_context, mbedtls_hkdf(), mbedtls_hkdf_extract(), hmac_drbg_reseed_core(), mbedtls_hmac_drbg_free(), mbedtls_hmac_drbg_init(), mbedtls_hmac_drbg_random(), mbedtls_hmac_drbg_random_with_add() (+11 more)

### Community 143 - "docs/README.md"
Cohesion: 0.21
Nodes (3): App lifecycle (fixed, verified), IME keyboard: init refuses (OPEN), TLS + IME + presence-display findings (hardware-verified)

### Community 144 - "OrbisRPC Production Report"
Cohesion: 0.11
Nodes (16): Auto-start, Building, Install flow, Installer (Setup PKG, `ORPC00001`), Navigation law, What it does, 0.4 fixes (hardware-verified on 9.00), 1.1 fixes (host-verified) (+8 more)

### Community 145 - "br_aes_keysched"
Cohesion: 0.06
Nodes (51): br_aes_big_cbcenc_keys, br_aes_big_ctr_keys, br_aes_big_ctrcbc_keys, br_aes_small_cbcdec_keys, br_aes_small_cbcenc_keys, br_aes_small_ctr_keys, br_aes_small_ctrcbc_keys, br_aes_big_cbcenc_init() (+43 more)

### Community 146 - "sysrng.c"
Cohesion: 0.13
Nodes (15): br_prng_seeder, br_ghash_pclmul(), br_ghash_pclmul_get(), pclmul_supported(), pclmulqdq00(), pclmulqdq11(), br_cpuid(), br_prng_seeder_system() (+7 more)

### Community 147 - "ec_prime_i31.c"
Cohesion: 0.26
Nodes (18): api_generator(), api_mul(), api_muladd(), api_mulgen(), api_order(), api_xoff(), id_to_curve(), id_to_curve_def() (+10 more)

### Community 148 - "Troubleshooting"
Cohesion: 0.12
Nodes (17): "Can't connect to Discord" (console is online), Collect a diagnostic bundle, Daemon will not stop / "another daemon holds the lock", Desktop client logs out while orbisRPC runs, Game cover not showing (name + timer fine, no art), GoldHEN has no "Utilities" payload entry, GTA V / GTA Online with nanoDNS (unverified), Installer / PKG problems (+9 more)

### Community 149 - "yy_destructor"
Cohesion: 0.26
Nodes (12): sqlite3Parser(), yy_accept(), yy_destructor(), yy_find_reduce_action(), yy_find_shift_action(), yy_parse_failed(), yy_pop_parser_stack(), yy_shift() (+4 more)

### Community 150 - "sha2big.c"
Cohesion: 0.22
Nodes (14): br_sha384_context, br_sha512_context, br_range_dec64be(), br_range_enc64be(), br_sha384_init(), br_sha384_out(), br_sha384_set_state(), br_sha384_state() (+6 more)

### Community 151 - "mbedtls_x509_time"
Cohesion: 0.13
Nodes (21): mbedtls_pkcs7, mbedtls_time_t, mbedtls_x509_time, day, hour, min, mon, sec (+13 more)

### Community 152 - "mbedtls_x509_buf"
Cohesion: 0.15
Nodes (22): mbedtls_x509_crt_ext_cb_t, mbedtls_x509_buf, mbedtls_x509_san_other_name, type_id, value, mbedtls_asn1_get_bool(), x509_get_crl_ext(), mbedtls_x509_crt_parse_der_with_ext_cb() (+14 more)

### Community 153 - "playtime_report.py"
Cohesion: 0.21
Nodes (6): hms(), load(), main(), parse_line(), summarize(), PlaytimeReportTest

### Community 154 - "debug.c"
Cohesion: 0.24
Nodes (16): Presence display rules (verified 2026-09-23 via live holds + human eyes), debug_print_line_by_line(), debug_print_pk(), debug_send_line(), mbedtls_debug_print_buf(), mbedtls_debug_print_crt(), mbedtls_debug_print_ec_coord(), mbedtls_debug_print_ecp() (+8 more)

### Community 155 - "aria.c"
Cohesion: 0.23
Nodes (18): mbedtls_aria_context, aria_a(), aria_fe_xor(), aria_fo_xor(), aria_rot128(), aria_sl(), mbedtls_aria_crypt_cbc(), mbedtls_aria_crypt_cfb128() (+10 more)

### Community 156 - "mps_reader.c"
Cohesion: 0.39
Nodes (13): mbedtls_mps_size_t, mbedtls_mps_reader_commit(), mbedtls_mps_reader_feed(), mbedtls_mps_reader_free(), mbedtls_mps_reader_get(), mbedtls_mps_reader_init(), mbedtls_mps_reader_reclaim(), mps_reader_get_fragment_offset() (+5 more)

### Community 157 - "x509_crt.c"
Cohesion: 0.20
Nodes (17): mbedtls_x509_sequence, mbedtls_x509_crt_get_ca_istrue(), mbedtls_x509_crt_parse_cn_inet_pton(), x509_check_wildcard(), x509_crt_check_cn(), x509_crt_check_san(), x509_crt_check_san_ip(), x509_crt_check_san_uri() (+9 more)

### Community 158 - "DWORD"
Cohesion: 0.19
Nodes (14): sqlite3_win32_sleep(), winceCreateLock(), winceDestroyLock(), winceLockFile(), winceMutexAcquire(), winceUnlockFile(), winClose(), winHandleLockTimeout() (+6 more)

### Community 160 - "validate"
Cohesion: 0.33
Nodes (3): validate(), TestValidation, valid_doc()

### Community 161 - "psa_crypto_driver_pake_step"
Cohesion: 0.14
Nodes (14): psa_crypto_driver_pake_step, PSA_JPAKE_STEP_INVALID, PSA_JPAKE_X1_STEP_KEY_SHARE, PSA_JPAKE_X1_STEP_ZK_PROOF, PSA_JPAKE_X1_STEP_ZK_PUBLIC, PSA_JPAKE_X2_STEP_KEY_SHARE, PSA_JPAKE_X2_STEP_ZK_PROOF, PSA_JPAKE_X2_STEP_ZK_PUBLIC (+6 more)

### Community 162 - "argparse"
Cohesion: 0.24
Nodes (3): main(), split_pem(), subject_of()

### Community 163 - "json"
Cohesion: 0.23
Nodes (4): c_escape(), main(), main(), sha256_file()

### Community 164 - "TestFetchParsing"
Cohesion: 0.18
Nodes (4): TestFetchParsing, parse_build_number(), parse_electron_chrome(), parse_stable_version()

### Community 165 - "entropy.c"
Cohesion: 0.22
Nodes (15): mbedtls_entropy_context, mbedtls_entropy_f_source_ptr, mbedtls_entropy_source_state, mbedtls_entropy_add_source(), mbedtls_entropy_free(), mbedtls_entropy_gather(), mbedtls_entropy_init(), mbedtls_entropy_self_test() (+7 more)

### Community 166 - "camellia.c"
Cohesion: 0.25
Nodes (16): mbedtls_camellia_context, camellia_feistel(), mbedtls_camellia_crypt_cbc(), mbedtls_camellia_crypt_cfb128(), mbedtls_camellia_crypt_ctr(), mbedtls_camellia_crypt_ecb(), mbedtls_camellia_free(), mbedtls_camellia_init() (+8 more)

### Community 167 - "NameContext"
Cohesion: 0.17
Nodes (14): addAggInfoColumn(), addAggInfoFunc(), aggregateConvertIndexedExprRefToColumn(), analyzeAggFuncArgs(), areDoubleQuotedStringsEnabled(), assignAggregateRegisters(), columnTypeImpl(), isSimpleCount() (+6 more)

### Community 168 - "gen_fingerprint_header.py"
Cohesion: 0.23
Nodes (5): c_string(), render(), _str_field(), write_header(), TestHeader

### Community 169 - "package.json"
Cohesion: 0.20
Nodes (10): devDependencies, wrangler@^4.0.0, name, private, scripts, deploy, dev, test (+2 more)

### Community 170 - "[1.1.0-beta] - 2026-11-03"
Cohesion: 0.18
Nodes (10): [1.1.0-beta] - 2026-11-03, [1.2.0-beta] - Unreleased, Added, Changelog, Config, Features, Fixed, Fixes (+2 more)

### Community 171 - "Config reference (`/data/orbisRPC/config.json`)"
Cohesion: 0.18
Nodes (11): Auth, Client fingerprint (`fp_*`), Config reference (`/data/orbisRPC/config.json`), File handling, OAuth beta setup, Per-title rules, Presence text, Recovery & lifecycle (+3 more)

### Community 172 - "log_msg"
Cohesion: 0.06
Nodes (82): Iteration 12 — 2026-11-02 — second audit pass: parsers, cache, lifecycle, Iteration 9 (fingerprint and art cache), Buffer sizing, Settings, art_post(), builtin_title_name(), gmtime_r(), orbis_mono_s() (+74 more)

### Community 173 - "Workstreams"
Cohesion: 0.18
Nodes (10): 1. Tag-triggered releases, 2. Templates, 3. CI hygiene, 4. Binary cleanup, 5. Version traceability, Acceptance, Goal, Out of scope (+2 more)

### Community 174 - "geopolyOverlap"
Cohesion: 0.23
Nodes (7): geopolyAddOneSegment(), geopolyAddSegments(), geopolyEventMerge(), geopolyOverlap(), geopolySegmentMerge(), geopolySortEventsByX(), geopolySortSegmentsByYAndC()

### Community 175 - "aesni.c"
Cohesion: 0.24
Nodes (13): aesni_set_rk_128(), aesni_set_rk_192(), aesni_set_rk_256(), aesni_setkey_enc_128(), aesni_setkey_enc_192(), aesni_setkey_enc_256(), gcm_clmul(), gcm_mix() (+5 more)

### Community 176 - "sessionChangesetInvert"
Cohesion: 0.25
Nodes (16): sessionChangesetBufferRecord(), sessionChangesetBufferTblhdr(), sessionChangesetInvert(), sessionChangesetNextOne(), sessionChangesetReadTblhdr(), sessionDiscardData(), sessionInputBuffer(), sessionReadRecord() (+8 more)

### Community 177 - "tmdb_crypto.c"
Cohesion: 0.49
Nodes (9): rol(), sha1_block(), sha1_final(), sha1_init(), sha1_update(), tmdb_hmac_sha1(), tmdb_path(), tmdb_sha1() (+1 more)

### Community 178 - "Injecting OrbisRPC into the PS4 — the complete guide"
Cohesion: 0.22
Nodes (9): Injecting OrbisRPC into the PS4 — the complete guide, Method 1 — elfldr (recommended), Method 2 — GoldHEN BinLoader server (port 9020), Method 3 — sender page (no PC tools), THE ONE-SHOT RULE (read this twice), Troubleshooting, Watching it work, What NOT to use (+1 more)

### Community 179 - "Review Focus"
Cohesion: 0.22
Nodes (8): Global Constraints, Repo excellence batch Implementation Plan, Review Focus, Task 1: Tag-triggered ELF releases, Task 2: Issue + PR templates, Task 3: CI hygiene (checkout v5, concurrency, dependabot), Task 4: Remove stale root `eboot.bin`, Task 5: Untouched-code proof + full suite + push

### Community 180 - "chacha20.c"
Cohesion: 0.21
Nodes (16): mbedtls_chacha20_context, MBEDTLS_PRIVATE, chacha20_block(), chacha20_inner_block(), chacha20_quarter_round(), mbedtls_chacha20_crypt(), mbedtls_chacha20_free(), mbedtls_chacha20_init() (+8 more)

### Community 181 - "orbisRPC 1.1 release notes"
Cohesion: 0.29
Nodes (6): Console checklist (not yet run), Headline, Host-tested changes, Known limits, License, orbisRPC 1.1 release notes

### Community 182 - "updates"
Cohesion: 0.29
Nodes (6): interval, updates, directory, package-ecosystem, schedule, version

### Community 183 - "sqlite3_result_error_nomem"
Cohesion: 0.18
Nodes (16): re_maxlen(), re_sql_func(), stmtrandFunc(), zipfileFinal(), first_valueStepFunc(), geomCallback(), icuCaseFunc16(), icuFunctionError() (+8 more)

### Community 184 - "Cover art pipeline"
Cohesion: 0.33
Nodes (5): Cover art pipeline, Hard rule (verified on hardware), Path 1: Sony TMDB live (automatic), Path 2: icon pack (fallback, maintainer-hosted), Path 3: uploaded Discord asset keys

### Community 185 - "Building"
Cohesion: 0.33
Nodes (6): Building, CI (`.github/workflows/ci.yml`), Host tests (`tests/`), Requirements, Scripts (`scripts/`), The one rule

### Community 186 - "SDK firmware gate: why the payload was silent on 13.52"
Cohesion: 0.33
Nodes (6): Cause, Fix, Not yet proven, Observed, Ruled out, SDK firmware gate: why the payload was silent on 13.52

### Community 187 - "Security"
Cohesion: 0.33
Nodes (5): Discord user tokens, OAuth beta credentials, Reporting, Security, Webhook

### Community 188 - "fts5ConfigParseSpecial"
Cohesion: 0.18
Nodes (14): fts5_isdigit(), fts5_isopenquote(), fts5_iswhitespace(), fts5ConfigGobbleWord(), fts5ConfigParseSpecial(), fts5ConfigSetEnum(), fts5ConfigSkipArgs(), fts5ConfigSkipBareword() (+6 more)

### Community 189 - "Observing the console (for assistants)"
Cohesion: 0.15
Nodes (10): Channels, Key paths (FTP), Observing the console (for assistants), Proof markers, Rules learned the hard way, Loader behavior (observed on PS4 9.00, GoldHEN 2.4), Open, Proven facts (+2 more)

### Community 190 - "ELF linkage: why OpenOrbis binaries die in spawned processes"
Cohesion: 0.40
Nodes (5): Consequence, ELF linkage: why OpenOrbis binaries die in spawned processes, Observed, Ruled out, Structural comparison (llvm-readelf, verified locally)

### Community 191 - "fts5SnippetFunction"
Cohesion: 0.22
Nodes (11): fts5Bm25GetData(), fts5CInstIterInit(), fts5CInstIterNext(), Fts5ExtensionApi, iVersion, fts5HighlightAppend(), fts5HighlightCb(), fts5HighlightFunction() (+3 more)

### Community 192 - "ripemd160.c"
Cohesion: 0.35
Nodes (10): mbedtls_ripemd160_context, MBEDTLS_PRIVATE, mbedtls_internal_ripemd160_process(), mbedtls_ripemd160(), mbedtls_ripemd160_finish(), mbedtls_ripemd160_free(), mbedtls_ripemd160_init(), mbedtls_ripemd160_self_test() (+2 more)

### Community 193 - "build.sh"
Cohesion: 0.50
Nodes (4): fail(), OO_PS4_TOOLCHAIN, PATH, build.sh script

### Community 194 - "lms.c"
Cohesion: 0.31
Nodes (12): mbedtls_lms_private_t, mbedtls_lms_public_t, calculate_merkle_tree(), get_merkle_path(), mbedtls_lms_calculate_public_key(), mbedtls_lms_export_public_key(), mbedtls_lms_import_public_key(), mbedtls_lms_private_init() (+4 more)

### Community 195 - "orbisRPC OAuth broker"
Cohesion: 0.33
Nodes (6): Current deployment, Deploy, Free-tier capacity, HTTP contract, orbisRPC OAuth broker, Verification

### Community 196 - "orbisRPC 1.2 beta implementation plan"
Cohesion: 0.33
Nodes (6): Broker cost optimization, Issue coverage, orbisRPC 1.2 beta implementation plan, Sources and attribution, Verification record, Work and acceptance

### Community 197 - "orbisRPC docs"
Cohesion: 0.50
Nodes (4): For developers, For testers, Hardware research archive, orbisRPC docs

### Community 198 - "build_evict.sh"
Cohesion: 0.50
Nodes (3): PATH, PS4_PAYLOAD_SDK, build_evict.sh script

### Community 199 - "build_sdk.sh"
Cohesion: 0.50
Nodes (3): PATH, PS4_PAYLOAD_SDK, build_sdk.sh script

### Community 200 - "sha3.c"
Cohesion: 0.46
Nodes (12): mbedtls_sha3_context, mbedtls_sha3_id, keccak_f1600(), mbedtls_sha3(), mbedtls_sha3_finish(), mbedtls_sha3_free(), mbedtls_sha3_init(), mbedtls_sha3_kat_test() (+4 more)

### Community 204 - "sqlite3_value_text"
Cohesion: 0.08
Nodes (75): base64(), base85(), ctxErrorMsg(), editFunc(), idxRemFunc(), ieee754func(), ieee754func_from_blob(), is_base85() (+67 more)

### Community 206 - "statPush"
Cohesion: 0.47
Nodes (9): sampleClear(), sampleCopy(), sampleInsert(), sampleIsBetter(), sampleIsBetterPost(), samplePushPrevious(), sampleSetRowid(), sampleSetRowidInt64() (+1 more)

### Community 207 - "threading.c"
Cohesion: 0.27
Nodes (7): mbedtls_threading_mutex_t, threading_mutex_dummy(), threading_mutex_fail(), threading_mutex_free_pthread(), threading_mutex_init_pthread(), threading_mutex_lock_pthread(), threading_mutex_unlock_pthread()

### Community 208 - "sqlite3OsOpenMalloc"
Cohesion: 0.25
Nodes (7): memjrnlClose(), memjrnlCreateFile(), memjrnlFreeChunks(), memjrnlTruncate(), memjrnlWrite(), sqlite3OsOpen(), sqlite3OsOpenMalloc()

### Community 209 - "editPage"
Cohesion: 0.46
Nodes (7): cachedCellSize(), computeCellSize(), editPage(), pageFreeArray(), pageInsertArray(), populateCellCache(), rebuildPage()

### Community 210 - "mbedtls_md_clone"
Cohesion: 0.43
Nodes (8): mbedtls_md5_clone(), mbedtls_md_clone(), mbedtls_psa_hash_clone(), mbedtls_ripemd160_clone(), mbedtls_sha1_clone(), mbedtls_sha256_clone(), mbedtls_sha3_clone(), mbedtls_sha512_clone()

### Community 211 - "CellInfo"
Cohesion: 0.43
Nodes (6): assertCellInfo(), btreeParseCell(), btreeParseCellAdjustSizeForOverflow(), btreeParseCellPtr(), btreeParseCellPtrIndex(), cellInfoEqual()

### Community 212 - "sha1QueryFunc"
Cohesion: 0.52
Nodes (6): hash_finish(), hash_init(), hash_step(), hash_step_vformat(), sha1QueryFunc(), SHA1Transform()

### Community 215 - "bindText"
Cohesion: 0.33
Nodes (6): bindText(), parserStackRealloc(), sqlite3_bind_blob64(), sqlite3_bind_text16(), sqlite3_bind_text64(), sqlite3_realloc()

### Community 216 - "Hash"
Cohesion: 0.47
Nodes (4): findElementWithHash(), insertElement(), removeElement(), strHash()

### Community 217 - "mbedtls_x509_authority"
Cohesion: 0.40
Nodes (5): mbedtls_x509_authority, authorityCertIssuer, authorityCertSerialNumber, keyIdentifier, raw

### Community 218 - "AGENTS.md — orbisRPC contributor notes"
Cohesion: 0.50
Nodes (3): AGENTS.md — orbisRPC contributor notes, Conventions, Secrets

## Knowledge Gaps
- **271 isolated node(s):** `PATH`, `OO_PS4_TOOLCHAIN`, `PS4_PAYLOAD_SDK`, `PATH`, `PS4_PAYLOAD_SDK` (+266 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 1167 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **10 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `sqlite3_free()` connect `sqlite3_free` to `sqlite3.c`, `sqlite3OomFault`, `decimal_new`, `MIN`, `i64`, `Vdbe`, `Fts5Index`, `u64`, `sqlite3_intck`, `sqlite3expert`, `sqlite3Malloc`, `sqlite3TreeViewExpr`, `Index`, `Parse`, `sqlite3_stmt`, `sqlite3_context`, `Fts5Context`, `sqlite3_initialize`, `do_meta_command`, `sqlite3_column_bytes`, `memsys3MallocUnsafe`, `jsonParseFuncArg`, `DWORD`, `Fts5SegIter`, `unixFile`, `sqlite3_file`, `sqlite3_value`, `Btree`, `sqlite3_column_text`, `sqlite3_stricmp`, `log_msg`, `geopolyOverlap`, `sessionChangesetInvert`, `sqlite3_result_error_nomem`, `Rtree`, `shell.c`, `fts5ConfigParseSpecial`, `sqlite3DbFree`, `fts5SnippetFunction`, `sqlite3_recover`, `fts5yy_reduce`, `sqlite3_result_text`, `sqlite3_vmprintf`, `BtCursor`, `SQLITE_API`, `sqlite3_value_text`, `sqlite3`, `u8`, `Fts3Hash`, `sqlite3OsOpenMalloc`, `sqlite3_create_module`, `sha1QueryFunc`, `Hash`, `DateTime`, `sqlite3_value_int`, `sqlite3Malloc`, `sqlite3BtreeIntegrityCheck`?**
  _High betweenness centrality (0.014) - this node is a cross-community bridge._
- **Are the 172 inferred relationships involving `sqlite3_free()` (e.g. with `arCreateOrUpdateCommand()` and `arDotCommand()`) actually correct?**
  _`sqlite3_free()` has 172 INFERRED edges - model-reasoned connections that need verification._
- **What connects `PATH`, `OO_PS4_TOOLCHAIN`, `PS4_PAYLOAD_SDK` to the rest of the system?**
  _271 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `sqlite3.c` be split into smaller, more focused modules?**
  _Cohesion score 0.008828327415795029 - nodes in this community are weakly interconnected._
- **Why does `daemon_run()` connect `test_utils.c` to `stdio`, `OrbisRPC Migration Progress Log`, `log_msg`, `health.c`, `test_ws.c`, `discord.c`, `jl_free`, `net_sockets.c`?**
  _High betweenness centrality (0.013) - this node is a cross-community bridge._
- **Should `sqlite3OomFault` be split into smaller, more focused modules?**
  _Cohesion score 0.06317907444668008 - nodes in this community are weakly interconnected._
- **Should `psa_status_t` be split into smaller, more focused modules?**
  _Cohesion score 0.04362709975320436 - nodes in this community are weakly interconnected._