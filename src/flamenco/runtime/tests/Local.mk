$(call add-hdrs,generated/context.pb.h,generated/elf.pb.h,generated/invoke.pb.h,generated/txn.pb.h)
$(call add-objs,generated/context.pb generated/elf.pb generated/invoke.pb generated/txn.pb,fd_flamenco)

ifdef FD_HAS_INT128
ifdef FD_HAS_SECP256K1
$(call add-hdrs,fd_exec_instr_test.h)
$(call add-objs,fd_exec_instr_test,fd_flamenco)

$(call make-unit-test,test_exec_instr,test_exec_instr,fd_flamenco fd_funk fd_ballet fd_util,$(SECP256K1_LIBS))
$(call make-unit-test,test_elf_loader,test_elf_loader,fd_flamenco fd_funk fd_ballet fd_util,$(SECP256K1_LIBS))
$(call make-shared,libfd_exec_sol_compat.so,fd_exec_sol_compat,fd_flamenco fd_funk fd_ballet fd_util,$(SECP256K1_LIBS))
endif
endif

run-runtime-test: $(OBJDIR)/bin/fd_ledger
	python3.11 ./src/flamenco/runtime/tests/run_ledger_tests_all.py ./src/flamenco/runtime/tests/run_ledger_tests_all.txt

run-runtime-test-nightly: $(OBJDIR)/bin/fd_ledger
	OBJDIR=$(OBJDIR) src/flamenco/runtime/tests/run_ledger_tests.sh -l mainnet-257033306 -s snapshot-257033306-EE3WdRoE4J1LTjegJMK3ZzxKZbSMQhLMaTM5Jp4SygMU.tar.zst -p 50 -P 40 -y 350 -m 500000000 -M 20000000 -e 257213306 --zst -cp /data/nightly_checkpt -cm 1 -pbf 1 -pf 1
