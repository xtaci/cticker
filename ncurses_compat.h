#ifndef CTICKER_NCURSES_COMPAT_H
#define CTICKER_NCURSES_COMPAT_H

/**
 * @file ncurses_compat.h
 * @brief Portable ncurses include and mouse-button fallbacks.
 *
 * Every source file that touches ncurses should include this header
 * instead of <ncurses.h> or <ncursesw/ncurses.h> directly.
 */

/* ── ncurses header resolution ─────────────────────────────────────── */
#if defined(__has_include)
#  if __has_include(<ncursesw/ncurses.h>)
#    include <ncursesw/ncurses.h>
#  elif __has_include(<ncurses.h>)
#    include <ncurses.h>
#  else
#    error "ncurses headers not found"
#  endif
#else
#  include <ncurses.h>
#endif

/* ── Mouse button fallbacks (older ncurses builds) ─────────────────── */
#ifndef BUTTON4_PRESSED
#define BUTTON4_PRESSED 0
#endif

#ifndef BUTTON5_PRESSED
#define BUTTON5_PRESSED 0
#endif

#endif /* CTICKER_NCURSES_COMPAT_H */
