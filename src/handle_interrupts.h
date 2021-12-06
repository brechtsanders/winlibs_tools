/*
  handle Ctrl-C/break/interrupt/close signals
*/

#ifndef INCLUDED_HANDLE_INTERRUPTS_H
#define INCLUDED_HANDLE_INTERRUPTS_H

#ifdef _WIN32
#  include <windows.h>
#else
#  include <signal.h>
#endif

//!start definition of function to handle interrupt signals
/*!
  \param  function_name         name of function to handle interrupt signals
*/
#ifdef _WIN32
#  define DEFINE_INTERRUPT_HANDLER_BEGIN(function_name) BOOL WINAPI function_name (DWORD interrupt_signal) {  if (interrupt_signal == CTRL_C_EVENT || interrupt_signal == CTRL_BREAK_EVENT || interrupt_signal == CTRL_CLOSE_EVENT || interrupt_signal == CTRL_LOGOFF_EVENT || interrupt_signal == CTRL_SHUTDOWN_EVENT) {
#else
#  define DEFINE_INTERRUPT_HANDLER_BEGIN(function_name) void function_name (int interrupt_signal) {
#endif

//!finish definition of function to handle interrupt signals
/*!
  \param  function_name         name of function to handle interrupt signals
*/
#ifdef _WIN32
#  define DEFINE_INTERRUPT_HANDLER_END(function_name)   } else { return FALSE; } return TRUE; }
#else
#  define DEFINE_INTERRUPT_HANDLER_END(function_name)   signal(interrupt_signal, function_name); }
#endif

//!macros to check which signal was triggered
/*!
 * \name   INTERRUPT_HANDLER_SIGNAL_IS_*
 * \{
 */
#ifdef _WIN32
#  define INTERRUPT_HANDLER_SIGNAL_IS_CTRL_C            ((interrupt_signal) == CTRL_C_EVENT)
#  define INTERRUPT_HANDLER_SIGNAL_IS_BREAK             ((interrupt_signal) == CTRL_BREAK_EVENT)
#  define INTERRUPT_HANDLER_SIGNAL_IS_CLOSE             ((interrupt_signal) == CTRL_CLOSE_EVENT || (interrupt_signal) == CTRL_LOGOFF_EVENT || (interrupt_signal) == CTRL_SHUTDOWN_EVENT)
#else
#  define INTERRUPT_HANDLER_SIGNAL_IS_CTRL_C            ((interrupt_signal) == SIGINT)
#  if defined(SIGBREAK)
#    define INTERRUPT_HANDLER_SIGNAL_IS_BREAK           ((interrupt_signal) == SIGBREAK)
#  elif defined(SIGBRK)
#    define INTERRUPT_HANDLER_SIGNAL_IS_BREAK           ((interrupt_signal) == SIGBRK)
#  else
#    define INTERRUPT_HANDLER_SIGNAL_IS_BREAK           0
#  endif
#  define INTERRUPT_HANDLER_SIGNAL_IS_CLOSE             ((interrupt_signal) == SIGTERM)
#endif
/*! @} */

//!install function to handle interrupt signals
/*!
  \param  function_name         name of function to handle interrupt signals
*/
#ifdef _WIN32
#  define INSTALL_INTERRUPT_HANDLER(function_name)      { SetConsoleCtrlHandler(NULL, FALSE); SetConsoleCtrlHandler(function_name, TRUE); }
#else
#  if defined(SIGBREAK)
#    define INSTALL_INTERRUPT_HANDLER(function_name)    { signal(SIGINT, function_name); signal(SIGBREAK, function_name); signal(SIGTERM, function_name); }
#  elif defined(SIGBRK)
#    define INSTALL_INTERRUPT_HANDLER(function_name)    { signal(SIGINT, function_name); signal(SIGBRK, function_name); signal(SIGTERM, function_name); }
#  else
#    define INSTALL_INTERRUPT_HANDLER(function_name)    { signal(SIGINT, function_name); signal(SIGTERM, function_name); }
#  endif
#endif

#endif //INCLUDED_HANDLE_INTERRUPTS_H
