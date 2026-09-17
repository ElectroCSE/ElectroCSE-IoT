/*
 * =============================================================================
 *  ElectroCSE IoT  —  the ArduinoJson compatibility seam
 * =============================================================================
 *  Do not include this directly. ElectroCSE_Core.h pulls it in.
 *
 *  ONE JOB: let this library keep StaticJsonDocument without printing a wall of
 *  deprecation warnings into somebody else's build.
 *
 *  WHY StaticJsonDocument IS KEPT
 *  ------------------------------
 *  ArduinoJson 7 deprecated it in favour of a bare JsonDocument, which
 *  allocates from the HEAP. Every document in this library is built on every
 *  check-in, for the life of the board - so on v7 that is an allocate/free
 *  cycle every poll, for ever, interleaved with the String that carries the
 *  body. That is the exact churn that fragments an ESP8266's ~40KB heap and
 *  stops a board dead after a couple of days, and the failure arrives long
 *  after anybody is still watching. StaticJsonDocument lives on the stack and
 *  costs nothing to release.
 *
 *  It also still compiles under ArduinoJson 6, which the README promises and a
 *  lot of installed IDEs still have.
 *
 *  WHY THE WARNINGS ARE SUPPRESSED RATHER THAN THE CODE CHANGED
 *  -----------------------------------------------------------
 *  The warnings name lines in THIS library, inside a decision that has been
 *  made deliberately. Whoever compiles a sketch cannot act on them: there is
 *  no edit they could make in their own file that removes one. Eight warnings
 *  per build, on a library whose entire promise is that a beginner does not
 *  have to read compiler output, is a bug in the library - it teaches people
 *  that a noisy build is normal, which is how the one warning that matters
 *  gets scrolled past.
 *
 *  The suppression is scoped to the few blocks that need it, never to a file,
 *  so an unrelated deprecation elsewhere still surfaces.
 *
 *  WHY library.properties PINS < 8.0.0
 *  -----------------------------------
 *  Deprecated is not removed, so v7 still compiles. Version 8 may well delete
 *  these, and an unbounded `depends=ArduinoJson` means the Library Manager
 *  hands every new user whatever is latest - so the day that release lands,
 *  every fresh install of this library stops compiling, with an error naming a
 *  file the user has never opened. The bound is what turns that from an
 *  outage into a version this library is known not to support yet.
 *
 *  IF YOU ARE HERE TO ADD v8 SUPPORT: the replacement that keeps the stack is a
 *  custom ArduinoJson::Allocator over a fixed buffer, not a bare JsonDocument.
 * =============================================================================
 */

#ifndef ELECTROCSE_JSON_H
#define ELECTROCSE_JSON_H

#include <ArduinoJson.h>

/*
 * _Pragma rather than #pragma because these have to expand from inside a
 * macro; #pragma cannot. GCC and Clang both understand it, and every core this
 * library supports is one or the other - but the fallback is a no-op rather
 * than an #error, because a compiler that cannot silence a warning should
 * still build the library.
 */
#if defined(__GNUC__) || defined(__clang__)
  #define ELECTROCSE_JSON_QUIET_BEGIN                                        \
      _Pragma("GCC diagnostic push")                                         \
      _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
  #define ELECTROCSE_JSON_QUIET_END _Pragma("GCC diagnostic pop")
#else
  #define ELECTROCSE_JSON_QUIET_BEGIN
  #define ELECTROCSE_JSON_QUIET_END
#endif

#endif  // ELECTROCSE_JSON_H
