/* launcher.h - Rebuilt GOT.EXE launcher / main menu
 *
 * This recreates the original God of Thunder launcher menu
 * (reverse-engineered from the 16-bit GOT.EXE) using raylib.
 *
 * Flow (matching original GOT.EXE sub_15DA8 / sub_190C1):
 *   1. Opening screens (title, credits) - skippable
 *   2. Main menu: Play Game, High Scores, Credits, Quit
 *   3. Episode selection: Part 1/2/3
 *   4. Return selected episode (or 0 for quit)
 */
#ifndef GOT_LAUNCHER_H
#define GOT_LAUNCHER_H

/* Initialize the launcher (creates raylib window, loads GRAPHICS.GOT).
 * Returns 0 on success. */
int  launcher_init(void);

/* Run the launcher menu loop.  Returns episode number (1-3) or 0 for quit. */
int  launcher_run(void);

/* Clean up launcher resources (does NOT close the raylib window). */
void launcher_shutdown(void);

#endif /* GOT_LAUNCHER_H */
