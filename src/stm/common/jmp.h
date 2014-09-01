/**
 * Mask differences related to long_jmp functions on different platforms.
 *
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 *
 */

#ifndef TLSTM_JMP_H_
#define TLSTM_JMP_H_

#ifdef TLSTM_ICC

#include "../../intel/jmp.h"
#define LONG_JMP_BUF begin_transaction_jmpbuf

#else

#include <setjmp.h>

#ifdef TLSTM_MACOS
#define LONG_JMP_BUF sigjmp_buf
#elif defined TLSTM_LINUXOS || defined TLSTM_SOLARIS
#define LONG_JMP_BUF jmp_buf
#endif /* MACOS */

#endif /* TLSTM_ICC */

#define LONG_JMP_FIRST_FLAG 0
#define LONG_JMP_RESTART_FLAG 1
#define LONG_JMP_ABORT_FLAG 2
#define LONG_JMP_INCONSISTENT_READ_FLAG 3

#endif /* TLSTM_JMP_H_ */

