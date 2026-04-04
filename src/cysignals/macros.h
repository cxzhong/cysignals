/*
Interrupt and signal handling for Cython.

This code distinguishes between two kinds of signals:

(1) interrupt-like signals: SIGINT, SIGALRM, SIGHUP.  The word
"interrupt" refers to any of these signals.  These need not be handled
immediately, we might handle them at a suitable later time, outside of
sig_block() and with the Python GIL acquired.  SIGINT raises a
KeyboardInterrupt (as usual in Python), SIGALRM raises AlarmInterrupt
(a custom exception inheriting from KeyboardInterrupt), while SIGHUP
raises SystemExit, causing Python to exit.  The latter signal also
redirects stdin from /dev/null, to cause interactive sessions to exit.

(2) critical signals: SIGQUIT, SIGILL, SIGABRT, SIGFPE, SIGBUS, SIGSEGV.
These are critical because they cannot be ignored.  If they happen
outside of sig_on(), we can only exit Python with the dreaded
"unhandled SIG..." message.  Inside of sig_on(), they can be handled
and raise various exceptions (see cysignals/signals.pyx).  SIGQUIT
will never be handled and always causes Python to exit.

*/

/*****************************************************************************
 *       Copyright (C) 2006 William Stein <wstein@gmail.com>
 *                     2006 Martin Albrecht <martinralbrecht+cysignals@gmail.com>
 *                     2010-2016 Jeroen Demeyer <J.Demeyer@UGent.be>
 *
 * cysignals is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * cysignals is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with cysignals.  If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************************/


#ifndef CYSIGNALS_MACROS_H
#define CYSIGNALS_MACROS_H

#include <setjmp.h>
#include <signal.h>
#include "struct_signals.h"

#ifdef __cplusplus
extern "C" {
#endif


/**********************************************************************
 * HELPER MACROS                                                      *
 **********************************************************************/

/* Send a signal to the calling process. The POSIX raise() function
 * sends a signal to the calling thread, while kill() typically sends
 * a signal to the main thread (although this is not guaranteed by the
 * POSIX standard) */
#if HAVE_KILL
#define proc_raise(sig)  kill(getpid(), sig)
#else
/* On Windows, raise() actually signals the process */
#define proc_raise(sig)  raise(sig)
#endif

/**********************************************************************
 * IMPLEMENTATION OF SIG_ON/SIG_OFF                                   *
 **********************************************************************/

/*
 * Implementation of sig_on().  Applications should not use this
 * directly, use sig_on() or sig_str() instead.
 *
 * _sig_on_(message) is a macro which pretends to be a function.
 * Since this is declared as "cdef except 0", Cython will know that an
 * exception occurred if the value of _sig_on_() is 0 (false).
 *
 * INPUT:
 *
 *  - message -- a string to be displayed as error message when the code
 *    between sig_on() and sig_off() fails and raises an exception.
 *
 * OUTPUT: zero if an exception occurred, non-zero otherwise.
 *
 * The function cysetjmp() in the _sig_on_() macro can return:
 *  - zero: this happens in the actual sig_on() call. cysetjmp() sets
 *    up the address for the signal handler to jump to.  The
 *    program continues normally.
 *  - a signal number (e.g. 2 for SIGINT), assumed to be strictly
 *    positive: the cysignals signal handler handled a signal.  Since
 *    _sig_on_() will return 0 in this case, the Exception (raised by
 *    cysigs_signal_handler) will be detected by Cython.
 *  - a negative number: this is assumed to come from sig_retry().  In
 *    this case, the program continues as if nothing happened between
 *    sig_on() and sig_retry().
 *
 * We cannot simply put cysetjmp() in a function, because when that
 * function returns, we would lose the stack frame to cylongjmp() to.
 * That's why we need this hackish macro.  We use the fact that || is
 * a short-circuiting operator (the second argument is only evaluated
 * if the first returns 0).
 *
 * All helper functions (_sig_on_prejmp, _sig_on_postjmp, _sig_off_,
 * etc.) are defined in implementation.c and shared across modules via
 * Cython's capsule mechanism.  Since macros are expanded at the call
 * site (after capsule declarations), all symbols are available.
 */

#define _sig_on_(message) ( unlikely(_sig_on_prejmp(message, __FILE__, __LINE__)) || _sig_on_postjmp(cysetjmp(cysigs.env)) )


/**********************************************************************
 * USER MACROS                                                        *
 **********************************************************************/

/* The actual macros which should be used in a program. */
#define sig_on()           _sig_on_(NULL)
#define sig_str(message)   _sig_on_(message)
#define sig_off()          _sig_off_(__FILE__, __LINE__)

/* sig_check() should be functionally equivalent to sig_on(); sig_off();
 * but much faster.  Essentially, it checks whether we missed any
 * interrupts.
 *
 * OUTPUT: zero if an interrupt occurred, non-zero otherwise.
 */
#define sig_check() \
    ((unlikely(cysigs.interrupt_received) && cysigs.sig_on_count == 0) ? \
     (_sig_on_interrupt_received(), 0) : 1)

/*
 * Temporarily block interrupts from happening inside sig_on().  This
 * is meant to wrap malloc() for example.  sig_unblock() checks whether
 * an interrupt happened in the mean time.  If yes, the interrupt is
 * re-raised.
 *
 * NOTES:
 * - This only works inside sig_on()/sig_off().  Outside of sig_on(),
 *   interrupts behave as usual.  This is because we can't propagate
 *   Python exceptions from low-level C code.
 * - Despite the above note, it is still legal to use sig_block()
 *   outside of sig_on().
 * - Other signals still go through, because we can't really ignore
 *   SIGSEGV for example.
 * - It is NOT allowed to have an outer call of sig_on() inside
 *   sig_block().
 *   A deeper nesting (sig_on(); sig_block(); sig_on();) is OK.
 */
#define sig_block() ((void)(++cysigs.block_sigint))

#define sig_unblock() _sig_unblock_()

/*
 * Retry a failed computation starting from sig_on().
 */
#define sig_retry() _sig_retry_()

/* Used in error callbacks from C code (in particular NTL and PARI).
 * This should be used after an exception has been raised to jump back
 * to sig_on() where the exception will be seen. */
#define sig_error() _sig_error_()

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* ifndef CYSIGNALS_MACROS_H */
