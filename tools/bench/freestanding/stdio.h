/* The simulator builds are freestanding (no libc), but op1_plugin.h's host op1_dbg includes <stdio.h> (op1r 45):
 * this stub declares what it uses; op1_dbg is never called in the bench, so nothing is linked. */
#pragma once
typedef struct FILE FILE;
extern FILE *stderr;
int fprintf(FILE *f, const char *fmt, ...);
