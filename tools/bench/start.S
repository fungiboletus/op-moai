/* bare start for the Blackfin simulator (bfin-elf-run): stack, main(), exit via the sim's syscall */
	.text
	.global __start
__start:
	SP.L = _bench_stack_top; SP.H = _bench_stack_top;
	FP = SP;
	R0 = SYSCFG; BITSET (R0, 1); SYSCFG = R0;   /* CCEN: run the CYCLES counter */
	CSYNC;
	SP += -12;
	CALL _main;
	P0 = 1;                 /* CB_SYS_exit, R0 = status */
	EXCPT 0;
1:	JUMP 1b;
