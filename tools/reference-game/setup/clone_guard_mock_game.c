/* Stand-in game process for clone_guard_test: the test copies it into a
 * synthetic clone under a fixture-only name and starts it, so the clone guard
 * sees a real PID, executable path and start time.
 *   clone_guard_mock_game MS            sleep MS milliseconds, exit 0
 *   clone_guard_mock_game MS STOPFILE   wait (at most 120 s) until STOPFILE
 *                                       exists, then sleep MS, exit 0
 *   clone_guard_mock_game MS STOPFILE CODE   as above, exit CODE (the
 *                                       exit code the runner must record);
 *                                       STOPFILE "-" means do not wait
 *   clone_guard_mock_game box TITLE TEXT   a message box like
 *                                       VSHELL's colour-depth notice
 *                                       (MB_ICONEXCLAMATION, owner NULL);
 *                                       exit 0 when it was answered IDOK,
 *                                       else 9
 * The second form lets a slow-starting test harness decide when the exit
 * countdown begins. Own code; never an original executable. */
#include <stdlib.h>
#include <string.h>
#include <windows.h>

int main(int argc, char **argv)
{
    if (argc > 3 && strcmp(argv[1], "box") == 0)
        return MessageBoxA(NULL, argv[3], argv[2], MB_ICONEXCLAMATION) == IDOK ? 0 : 9;
    unsigned long ms = argc > 1 ? strtoul(argv[1], NULL, 10) : 60000ul;
    if (ms > 600000ul) ms = 600000ul;
    if (argc > 2 && !(argv[2][0] == '-' && !argv[2][1])) {
        int waited;
        for (waited = 0; waited < 120000; waited += 50) {
            if (GetFileAttributesA(argv[2]) != INVALID_FILE_ATTRIBUTES) break;
            Sleep(50);
        }
    }
    Sleep((DWORD)ms);
    return argc > 3 ? (int)strtol(argv[3], NULL, 10) : 0;
}
