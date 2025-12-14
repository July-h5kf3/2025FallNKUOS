#include <ulib.h>
#include <stdio.h>
#include <string.h>

// Local buffer lives in .data; sharing this page exercises COW on write.
static char pagebuf[4096];

int main(void)
{
    // Touch data so the page is present before fork.
    strcpy(pagebuf, "parent");

    int pid = fork();
    if (pid < 0)
    {
        panic("fork failed\n");
    }

    if (pid == 0)
    {
        // Child writes to the shared page; should trigger COW.
        strcpy(pagebuf, "child");
        cprintf("child wrote: %s\n", pagebuf);
        exit(0);
    }

    assert(wait() == 0);
    cprintf("parent still sees: %s\n", pagebuf);

    if (strcmp(pagebuf, "parent") != 0)
    {
        panic("COW failed: parent data modified\n");
    }

    cprintf("COW test pass.\n");
    return 0;
}
