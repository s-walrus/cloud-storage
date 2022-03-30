#include <stdlib.h>
#include <stdio.h>

void DrawProgressBar(size_t completed, size_t total) {
    static int last_percent_printed = 0;
    if (completed == total) {
        printf("\r\033[1;32mCompleted!\033[0;37m\n");
    } else if (completed * 100 >= total * (last_percent_printed + 1)) {
        last_percent_printed = completed * 100 / total;
        printf("\r%d%%", last_percent_printed);
        fflush(stdout);
    }
}
